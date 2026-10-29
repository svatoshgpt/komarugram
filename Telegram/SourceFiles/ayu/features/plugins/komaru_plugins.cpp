// This is the source code of KomaruGram for Desktop.
#include "ayu/features/plugins/komaru_plugins.h"

#include "config.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_dc_options.h"
#include "ui/toast/toast.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QLocale>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QSysInfo>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtNetwork/QNetworkProxy>

namespace KomaruPlugins {
namespace {

constexpr auto kPackages = {
	"telethon==1.45.0",
	"kurigram==2.2.26",
	"pyyaml==6.0.3",
};
// Only speeds up Kurigram, and has no wheels for new Pythons.
constexpr auto kOptionalPackage = "tgcrypto==1.2.5";
constexpr auto kRuntimeVersion = 2;
constexpr auto kProbeTimeout = 10000;
constexpr auto kSendHookTimeout = 1500;
constexpr auto kCreateNoWindow = 0x08000000;

struct State {
	rpl::variable<RuntimeState> runtime = RuntimeState::Missing;
	rpl::variable<QString> status;
	QProcess *process = nullptr;
	QByteArray buffer;
	int nextId = 0;
	base::flat_map<int, Done> waiting;
	std::vector<QByteArray> queued;
	std::vector<PluginInfo> plugins;
	rpl::event_stream<> pluginsChanged;
	rpl::event_stream<QString> settingsChanged;
	rpl::lifetime lifetime;
	bool ready = false;
	bool started = false;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString Root() {
	return cWorkingDir() + u"tdata/plugins/"_q;
}

// Plugins use the Python installed on the computer; Telethon and Kurigram
// are installed for it with pip into tdata, not into that Python.
[[nodiscard]] QString RuntimeDir() {
	return Root() + u"runtime/"_q;
}

[[nodiscard]] QString DataDir() {
	return Root() + u"data/"_q;
}

[[nodiscard]] QString HostDir() {
	return Root() + u"host/"_q;
}

[[nodiscard]] QString SitePackages() {
	return RuntimeDir() + u"site-packages"_q;
}

[[nodiscard]] QString MarkerPath() {
	return RuntimeDir() + u"komaru-runtime.json"_q;
}

[[nodiscard]] QJsonObject ReadMarker() {
	auto file = QFile(MarkerPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

// The interpreter the packages were installed for.
[[nodiscard]] QString PythonExecutable() {
	return ReadMarker().value(u"python"_q).toString();
}

[[nodiscard]] bool RuntimeInstalled() {
	const auto marker = ReadMarker();
	const auto python = marker.value(u"python"_q).toString();
	return (marker.value(u"version"_q).toInt() == kRuntimeVersion)
		&& !python.isEmpty()
		&& QFile::exists(python);
}

[[nodiscard]] bool AnyPluginEnabled() {
	auto file = QFile(DataDir() + u"state.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto enabled = QJsonDocument::fromJson(file.readAll())
		.object().value(u"enabled"_q).toObject();
	for (auto i = enabled.begin(); i != enabled.end(); ++i) {
		if (i.value().toBool()) {
			return true;
		}
	}
	return false;
}

void Fail(const QString &text) {
	auto &state = Instance();
	LOG(("Komaru Plugins Error: %1").arg(text));
	state.status = text;
	state.runtime = RuntimeState::Failed;
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &bytes) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		&& (file.write(bytes) == bytes.size());
}

void HideConsole(not_null<QProcess*> process) {
#ifdef Q_OS_WIN
	process->setCreateProcessArgumentsModifier([](
			QProcess::CreateProcessArguments *args) {
		args->flags |= kCreateNoWindow;
	});
#endif // Q_OS_WIN
}

// Runs a candidate and returns the real interpreter path when it is
// Python 3.9 or newer with pip.
[[nodiscard]] QString ProbePython(
		const QString &program,
		const QStringList &prefix) {
	if (program.isEmpty()) {
		return QString();
	}
	auto process = QProcess();
	HideConsole(&process);
	process.start(program, prefix + QStringList{
		u"-c"_q,
		u"import sys, pip; print(sys.executable if sys.version_info >= "
			"(3, 9) else '')"_q,
	});
	if (!process.waitForFinished(kProbeTimeout)
		|| process.exitStatus() != QProcess::NormalExit
		|| process.exitCode() != 0) {
		process.kill();
		return QString();
	}
	const auto result = QString::fromLocal8Bit(
		process.readAllStandardOutput()).trimmed();
	return QFile::exists(result) ? result : QString();
}

[[nodiscard]] QString FindPython() {
#ifdef Q_OS_WIN
	// The python.exe in WindowsApps only opens the Store.
	const auto real = [](const QString &name) {
		const auto path = QStandardPaths::findExecutable(name);
		return path.contains(u"WindowsApps"_q, Qt::CaseInsensitive)
			? QString()
			: path;
	};
	for (const auto &name : { u"python"_q, u"python3"_q }) {
		if (const auto found = ProbePython(real(name), {}); !found.isEmpty()) {
			return found;
		}
	}
	if (const auto found = ProbePython(real(u"py"_q), { u"-3"_q })
		; !found.isEmpty()) {
		return found;
	}
	// The python.org installer leaves PATH alone unless asked to.
	const auto roots = {
		qEnvironmentVariable("LOCALAPPDATA") + u"/Programs/Python"_q,
		qEnvironmentVariable("ProgramFiles"),
		qEnvironmentVariable("ProgramFiles(x86)"),
	};
	for (const auto &root : roots) {
		auto folders = QDir(root).entryList(
			{ u"Python3*"_q },
			QDir::Dirs | QDir::NoDotAndDotDot,
			QDir::Name | QDir::Reversed);
		for (const auto &folder : folders) {
			const auto path = root + '/' + folder + u"/python.exe"_q;
			if (const auto found = ProbePython(path, {}); !found.isEmpty()) {
				return found;
			}
		}
	}
	return QString();
#else // Q_OS_WIN
	for (const auto &name : { u"python3"_q, u"python"_q }) {
		const auto path = QStandardPaths::findExecutable(name);
		if (const auto found = ProbePython(path, {}); !found.isEmpty()) {
			return found;
		}
	}
	return QString();
#endif // Q_OS_WIN
}

void RunPip(
		const QString &python,
		const QStringList &packages,
		Fn<void(bool ok, QString output)> done) {
	auto arguments = QStringList{
		u"-m"_q,
		u"pip"_q,
		u"install"_q,
		u"--disable-pip-version-check"_q,
		u"--no-warn-script-location"_q,
		u"--upgrade"_q,
		u"--target"_q,
		SitePackages(),
	} + packages;
	const auto process = new QProcess(QCoreApplication::instance());
	HideConsole(process);
	process->setProcessChannelMode(QProcess::MergedChannels);
	QObject::connect(process, &QProcess::finished, [=](
			int code,
			QProcess::ExitStatus status) {
		const auto output = QString::fromUtf8(process->readAll());
		process->deleteLater();
		done(status == QProcess::NormalExit && code == 0, output);
	});
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			process->deleteLater();
			done(false, u"Python could not be started"_q);
		}
	});
	process->start(python, arguments);
}

void InstallPackages(const QString &python) {
	auto &state = Instance();
	state.status = u"Installing Telethon and Kurigram…"_q;
	QDir(SitePackages()).removeRecursively();
	QDir().mkpath(SitePackages());
	auto packages = QStringList();
	for (const auto package : kPackages) {
		packages.push_back(QString::fromLatin1(package));
	}
	RunPip(python, packages, [=](bool ok, QString output) {
		if (!ok) {
			const auto lines = output.trimmed().split('\n');
			Fail(lines.isEmpty() ? u"pip failed"_q : lines.back().trimmed());
			return;
		}
		const auto finish = [=] {
			auto marker = QJsonObject();
			marker.insert(u"version"_q, kRuntimeVersion);
			marker.insert(u"python"_q, python);
			WriteFile(MarkerPath(), QJsonDocument(marker).toJson());
			Instance().status = QString();
			Instance().runtime = RuntimeState::Ready;
			EnsureHost();
		};
		RunPip(
			python,
			{ QString::fromLatin1(kOptionalPackage) },
			[=](bool, QString) { finish(); });
	});
}


void CopyHostScripts() {
	const auto prefix = u":/komaru_plugins"_q;
	auto i = QDirIterator(prefix, QDir::Files, QDirIterator::Subdirectories);
	while (i.hasNext()) {
		const auto source = i.next();
		const auto target = HostDir() + source.mid(prefix.size() + 1);
		auto in = QFile(source);
		if (!in.open(QIODevice::ReadOnly)) {
			continue;
		}
		const auto content = in.readAll();
		auto existing = QFile(target);
		if (existing.open(QIODevice::ReadOnly)
			&& existing.readAll() == content) {
			continue;
		}
		existing.close();
		WriteFile(target, content);
	}
}

[[nodiscard]] QString AppArchitecture() {
	return QSysInfo::buildCpuArchitecture()
		.replace(u"x86_64"_q, u"x64"_q)
		.replace(u"i386"_q, u"x86"_q);
}

[[nodiscard]] int AccountIndex(not_null<Main::Account*> account) {
	for (const auto &[index, existing] : Core::App().domain().accounts()) {
		if (existing.get() == account) {
			return index;
		}
	}
	return -1;
}

[[nodiscard]] std::optional<QJsonObject> AccountEntry(
		int index,
		not_null<Main::Account*> account) {
	if (!account->sessionExists()) {
		return std::nullopt;
	}
	auto &mtp = account->mtp();
	const auto dcId = mtp.mainDcId();
	auto key = MTP::AuthKeyPtr();
	for (const auto &candidate : mtp.getKeysForWrite()) {
		if (candidate && candidate->dcId() == dcId) {
			key = candidate;
			break;
		}
	}
	if (!key) {
		return std::nullopt;
	}
	using Variants = MTP::DcOptions::Variants;
	const auto variants = mtp.dcOptions().lookup(
		dcId,
		MTP::DcType::Regular,
		false);
	const auto &endpoints = variants.data[Variants::IPv4][Variants::Tcp];
	if (endpoints.empty()) {
		return std::nullopt;
	}
	const auto data = key->data();
	auto result = QJsonObject();
	result.insert(u"index"_q, index);
	result.insert(
		u"user_id"_q,
		QString::number(account->session().userId().bare));
	result.insert(u"dc_id"_q, dcId);
	result.insert(u"ip"_q, QString::fromStdString(endpoints.front().ip));
	result.insert(u"port"_q, endpoints.front().port);
	result.insert(u"auth_key"_q, QString::fromLatin1(QByteArray(
		reinterpret_cast<const char*>(data.data()),
		data.size()).toHex()));
	result.insert(u"test"_q, mtp.isTestMode());
	return result;
}

// Plugins connect on their own, so they get the proxy the client uses:
// the selected one, or the system one when the client follows the system.
[[nodiscard]] QJsonValue SystemProxyEntry() {
	const auto list = QNetworkProxyFactory::systemProxyForQuery(
		QNetworkProxyQuery(u"149.154.167.50"_q, 443));
	for (const auto &proxy : list) {
		const auto type = (proxy.type() == QNetworkProxy::Socks5Proxy)
			? u"socks5"_q
			: (proxy.type() == QNetworkProxy::HttpProxy)
			? u"http"_q
			: QString();
		if (type.isEmpty() || proxy.hostName().isEmpty()) {
			continue;
		}
		auto result = QJsonObject();
		result.insert(u"type"_q, type);
		result.insert(u"host"_q, proxy.hostName());
		result.insert(u"port"_q, int(proxy.port()));
		result.insert(u"user"_q, proxy.user());
		result.insert(u"password"_q, proxy.password());
		result.insert(u"label"_q, u"system"_q);
		return result;
	}
	return QJsonValue();
}

[[nodiscard]] QJsonValue ProxyEntry() {
	const auto &settings = Core::App().settings().proxy();
	if (settings.isSystem()) {
		return SystemProxyEntry();
	} else if (!settings.isEnabled()) {
		return QJsonValue();
	}
	const auto &proxy = settings.selected();
	using Type = MTP::ProxyData::Type;
	const auto type = (proxy.type == Type::Socks5)
		? u"socks5"_q
		: (proxy.type == Type::Http)
		? u"http"_q
		: (proxy.type == Type::Mtproto)
		? u"mtproto"_q
		: QString();
	auto result = QJsonObject();
	if (type.isEmpty() || proxy.host.isEmpty() || !proxy.port) {
		// Still a proxy: the host refuses to go direct.
		result.insert(u"type"_q, u"unsupported"_q);
		result.insert(u"label"_q, u"selected"_q);
		return result;
	}
	result.insert(u"type"_q, type);
	result.insert(u"host"_q, proxy.host);
	result.insert(u"port"_q, int(proxy.port));
	result.insert(u"user"_q, proxy.user);
	result.insert(u"password"_q, proxy.password);
	return result;
}

void SendAccounts() {
	auto accounts = QJsonArray();
	auto device = QJsonObject();
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto entry = AccountEntry(index, account.get())) {
			accounts.push_back(*entry);
			if (device.isEmpty()) {
				auto &mtp = account->mtp();
				device.insert(u"model"_q, mtp.deviceModel());
				device.insert(u"system"_q, mtp.systemVersion());
				device.insert(u"app"_q, AppArchitecture());
			}
		}
	}
	device.insert(u"lang"_q, Lang::Id().left(2));
	device.insert(
		u"system_lang"_q,
		QLocale::system().name().section('_', 0, 0));
	auto command = QJsonObject();
	command.insert(u"cmd"_q, u"accounts"_q);
	command.insert(u"api_id"_q, ApiId);
	command.insert(u"api_hash"_q, QString::fromLatin1(ApiHash));
	command.insert(u"device"_q, device);
	command.insert(u"accounts"_q, accounts);
	command.insert(u"proxy"_q, ProxyEntry());
	Request(command);
}

[[nodiscard]] std::vector<PluginMenuItem> ParseMenu(const QJsonValue &value) {
	auto result = std::vector<PluginMenuItem>();
	for (const auto &entry : value.toArray()) {
		const auto object = entry.toObject();
		result.push_back({
			.id = object.value(u"id"_q).toString(),
			.type = object.value(u"type"_q).toString(),
			.text = object.value(u"text"_q).toString(),
			.subtext = object.value(u"subtext"_q).toString(),
		});
	}
	return result;
}

// A plugin switches the proxy every account uses, as ConnectionsManager
// does on Android.
void ApplyPluginProxy(const QJsonObject &message) {
	if (!message.value(u"enabled"_q).toBool()) {
		Core::App().setCurrentProxy(
			Core::App().settings().proxy().selected(),
			MTP::ProxyData::Settings::Disabled);
		return;
	}
	const auto type = message.value(u"type"_q).toString();
	auto proxy = MTP::ProxyData();
	proxy.host = message.value(u"address"_q).toString();
	proxy.port = uint32(message.value(u"port"_q).toInt());
	if (type == u"MTPROTO"_q) {
		proxy.type = MTP::ProxyData::Type::Mtproto;
		proxy.password = message.value(u"secret"_q).toString();
	} else {
		proxy.type = (type == u"HTTP"_q)
			? MTP::ProxyData::Type::Http
			: MTP::ProxyData::Type::Socks5;
		proxy.user = message.value(u"username"_q).toString();
		proxy.password = message.value(u"password"_q).toString();
	}
	if (proxy.host.isEmpty() || !proxy.port || !proxy) {
		LOG(("Komaru Plugins: proxy from a plugin ignored, %1:%2"
			).arg(proxy.host).arg(proxy.port));
		return;
	}
	auto &list = Core::App().settings().proxy().list();
	if (!ranges::contains(list, proxy)) {
		list.push_back(proxy);
	}
	Core::App().setCurrentProxy(proxy, MTP::ProxyData::Settings::Enabled);
	Core::App().saveSettingsDelayed();
}

void ParsePlugins(const QJsonValue &value) {
	auto &state = Instance();
	state.plugins.clear();
	for (const auto &entry : value.toArray()) {
		const auto object = entry.toObject();
		state.plugins.push_back({
			.id = object.value(u"id"_q).toString(),
			.name = object.value(u"name"_q).toString(),
			.description = object.value(u"description"_q).toString(),
			.author = object.value(u"author"_q).toString(),
			.version = object.value(u"version"_q).toString(),
			.format = object.value(u"format"_q).toString(),
			.runtime = object.value(u"runtime"_q).toString(),
			.error = object.value(u"error"_q).toString(),
			.enabled = object.value(u"enabled"_q).toBool(),
			.loaded = object.value(u"loaded"_q).toBool(),
			.hasSettings = object.value(u"has_settings"_q).toBool(),
			.sendHook = object.value(u"send_hook"_q).toBool(),
			.menu = ParseMenu(object.value(u"menu"_q)),
		});
	}
	state.pluginsChanged.fire({});
}

void HandleEvent(const QJsonObject &message) {
	auto &state = Instance();
	const auto event = message.value(u"event"_q).toString();
	if (event == u"ready"_q) {
		state.ready = true;
		for (const auto &line : base::take(state.queued)) {
			state.process->write(line);
		}
		SendAccounts();
		Refresh();
	} else if (event == u"changed"_q) {
		Refresh();
	} else if (event == u"settings_changed"_q) {
		state.settingsChanged.fire(message.value(u"plugin"_q).toString());
	} else if (event == u"proxy"_q) {
		ApplyPluginProxy(message);
	} else if (event == u"toast"_q) {
		Ui::Toast::Show(message.value(u"text"_q).toString());
	} else if (event == u"clipboard"_q) {
		QGuiApplication::clipboard()->setText(
			message.value(u"text"_q).toString());
	} else if (event == u"log"_q) {
		LOG(("Komaru Plugin %1: %2").arg(
			message.value(u"plugin"_q).toString(),
			message.value(u"text"_q).toString()));
	}
}

void HandleLine(const QByteArray &line) {
	const auto message = QJsonDocument::fromJson(line).object();
	if (message.isEmpty()) {
		return;
	}
	if (message.contains(u"event"_q)) {
		HandleEvent(message);
		return;
	}
	auto &state = Instance();
	const auto id = message.value(u"id"_q).toInt();
	const auto i = state.waiting.find(id);
	if (i == end(state.waiting)) {
		return;
	}
	const auto done = std::move(i->second);
	state.waiting.erase(i);
	if (done) {
		done(
			message.value(u"result"_q),
			message.value(u"ok"_q).toBool()
				? QString()
				: message.value(u"error"_q).toString(u"error"_q));
	}
}

void ReadOutput() {
	auto &state = Instance();
	if (!state.process) {
		return;
	}
	state.buffer += state.process->readAllStandardOutput();
	while (true) {
		const auto newline = state.buffer.indexOf('\n');
		if (newline < 0) {
			break;
		}
		const auto line = state.buffer.left(newline);
		state.buffer.remove(0, newline + 1);
		HandleLine(line);
	}
}

void HostStopped() {
	auto &state = Instance();
	if (state.process) {
		state.process->deleteLater();
		state.process = nullptr;
	}
	state.ready = false;
	state.buffer.clear();
	state.queued.clear();
	for (auto &[id, done] : base::take(state.waiting)) {
		if (done) {
			done(QJsonValue(), u"The plugin host stopped"_q);
		}
	}
	for (auto &plugin : state.plugins) {
		plugin.loaded = false;
	}
	state.pluginsChanged.fire({});
}

void StartHost() {
	auto &state = Instance();
	if (state.process || !RuntimeInstalled()) {
		return;
	}
	QDir().mkpath(DataDir());
	CopyHostScripts();
	const auto process = new QProcess(QCoreApplication::instance());
	state.process = process;
	HideConsole(process);
	auto environment = QProcessEnvironment::systemEnvironment();
	environment.insert(u"PYTHONIOENCODING"_q, u"utf-8"_q);
	environment.insert(u"PYTHONUNBUFFERED"_q, u"1"_q);
	environment.insert(u"PYTHONPATH"_q, SitePackages());
	process->setProcessEnvironment(environment);
	process->setStandardErrorFile(DataDir() + u"host-stderr.log"_q);
	QObject::connect(process, &QProcess::readyReadStandardOutput, ReadOutput);
	QObject::connect(process, &QProcess::finished, [=] {
		if (Instance().process == process) {
			HostStopped();
		}
	});
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart
			&& Instance().process == process) {
			HostStopped();
		}
	});
	process->start(
		PythonExecutable(),
		{ HostDir() + u"host.py"_q, u"--data"_q, DataDir() });
}

} // namespace

void Start() {
	auto &state = Instance();
	if (state.started) {
		return;
	}
	state.started = true;
	// Test builds downloaded their own Python here.
	QDir(RuntimeDir() + u"python"_q).removeRecursively();
	QFile::remove(RuntimeDir() + u"pip.whl"_q);
	state.runtime = RuntimeInstalled()
		? RuntimeState::Ready
		: RuntimeState::Missing;
	rpl::merge(
		Core::App().domain().accountsChanges(),
		Core::App().domain().activeSessionValue() | rpl::to_empty,
		Core::App().proxyChanges() | rpl::to_empty
	) | rpl::on_next([] {
		if (Instance().ready) {
			SendAccounts();
		}
	}, state.lifetime);
	QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] {
		auto &state = Instance();
		if (const auto process = state.process) {
			state.process = nullptr;
			process->write("{\"cmd\": \"shutdown\"}\n");
			if (!process->waitForFinished(2000)) {
				process->kill();
			}
		}
	});
	if (state.runtime.current() == RuntimeState::Ready && AnyPluginEnabled()) {
		StartHost();
	}
}

RuntimeState CurrentRuntimeState() {
	return Instance().runtime.current();
}

rpl::producer<RuntimeState> RuntimeStateValue() {
	return Instance().runtime.value();
}

rpl::producer<QString> RuntimeStatusValue() {
	return Instance().status.value();
}

void InstallRuntime() {
	auto &state = Instance();
	if (state.runtime.current() == RuntimeState::Installing) {
		return;
	}
	state.runtime = RuntimeState::Installing;
	state.status = u"Looking for Python…"_q;
	QDir().mkpath(RuntimeDir());
	const auto python = FindPython();
	if (python.isEmpty()) {
		Fail(tr::ayu_PluginsPythonMissing(tr::now));
		return;
	}
	InstallPackages(python);
}

void EnsureHost() {
	StartHost();
}

bool HostRunning() {
	return Instance().process != nullptr;
}

void Request(QJsonObject command, Done done) {
	auto &state = Instance();
	if (!state.process) {
		StartHost();
	}
	if (!state.process) {
		if (done) {
			done(QJsonValue(), u"The plugin host is not running"_q);
		}
		return;
	}
	const auto id = ++state.nextId;
	command.insert(u"id"_q, id);
	if (done) {
		state.waiting.emplace(id, std::move(done));
	}
	const auto line = QJsonDocument(command).toJson(QJsonDocument::Compact)
		+ '\n';
	if (state.ready) {
		state.process->write(line);
	} else {
		state.queued.push_back(line);
	}
}

bool InstalledOnDisk(const QString &id) {
	return !id.isEmpty()
		&& !id.contains('/')
		&& !id.contains('\\')
		&& QFile::exists(DataDir() + u"installed/"_q + id + u"/komaru.json"_q);
}

void ClickMenuItem(
		not_null<Main::Session*> session,
		const QString &plugin,
		const QString &item,
		not_null<PeerData*> peer) {
	// exteraGram passes Android dialog ids: chats and channels negative.
	const auto bare = peer->isUser()
		? qint64(peerToUser(peer->id).bare)
		: peer->isChat()
		? qint64(peerToChat(peer->id).bare)
		: qint64(peerToChannel(peer->id).bare);
	auto command = QJsonObject();
	command.insert(u"cmd"_q, u"menu_click"_q);
	command.insert(u"plugin"_q, plugin);
	command.insert(u"item"_q, item);
	command.insert(u"account"_q, AccountIndex(&session->account()));
	command.insert(u"dialog_id"_q, peer->isUser() ? bare : -bare);
	Request(command, [](QJsonValue, QString error) {
		if (!error.isEmpty()) {
			Ui::Toast::Show(error);
		}
	});
}

const std::vector<PluginInfo> &Plugins() {
	return Instance().plugins;
}

rpl::producer<> PluginsChanged() {
	return Instance().pluginsChanged.events();
}

rpl::producer<QString> SettingsChanged() {
	return Instance().settingsChanged.events();
}

void Refresh() {
	auto command = QJsonObject();
	command.insert(u"cmd"_q, u"list"_q);
	Request(command, [](QJsonValue result, QString error) {
		if (error.isEmpty()) {
			ParsePlugins(result);
		}
	});
}

QString LogPath() {
	return DataDir() + u"host.log"_q;
}

bool HasSendHooks() {
	for (const auto &plugin : Instance().plugins) {
		if (plugin.loaded && plugin.sendHook) {
			return true;
		}
	}
	return false;
}

Outgoing TransformOutgoing(
		not_null<Main::Session*> session,
		int64 peer,
		const QString &text) {
	auto result = Outgoing{ .text = text };
	auto &state = Instance();
	if (text.isEmpty() || !state.ready || !state.process || !HasSendHooks()) {
		return result;
	}
	auto command = QJsonObject();
	command.insert(u"cmd"_q, u"transform_outgoing"_q);
	command.insert(u"account"_q, AccountIndex(&session->account()));
	command.insert(u"peer"_q, QString::number(peer));
	command.insert(u"text"_q, text);
	const auto reply = std::make_shared<std::optional<QJsonObject>>();
	Request(command, [=](QJsonValue value, QString error) {
		*reply = error.isEmpty() ? value.toObject() : QJsonObject();
	});
	auto timer = QElapsedTimer();
	timer.start();
	while (!reply->has_value()
		&& Instance().process
		&& timer.elapsed() < kSendHookTimeout) {
		Instance().process->waitForReadyRead(50);
	}
	if (!reply->has_value()) {
		return result;
	}
	const auto &object = **reply;
	const auto action = object.value(u"action"_q).toString();
	if (action == u"cancel"_q) {
		result.cancel = true;
	} else if (action == u"modify"_q) {
		result.text = object.value(u"text"_q).toString(text);
	}
	return result;
}

} // namespace KomaruPlugins
