// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/komaru_ws_proxy.h"

#include "ayu/features/plugins/komaru_plugins.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace KomaruWsProxy {
namespace {

constexpr auto kPingUrl = "https://kws2.web.telegram.org/apiws";
constexpr auto kPingTimeoutMs = 8000;

struct State {
	QString host;
	uint32 port = 0;
	QString secret;
	bool connecting = false;
	rpl::event_stream<> changed;
};

State &Instance() {
	static auto result = State();
	return result;
}

QNetworkAccessManager &Network() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

} // namespace

bool IsActive() {
	const auto &state = Instance();
	if (!state.port || state.host.isEmpty()) {
		return false;
	}
	const auto &proxySettings = Core::App().settings().proxy();
	if (!proxySettings.isEnabled()) {
		return false;
	}
	const auto &selected = proxySettings.selected();
	return (selected.type == MTP::ProxyData::Type::Mtproto)
		&& (selected.host == state.host)
		&& (selected.port == state.port);
}

bool IsConnecting() {
	return Instance().connecting;
}

void Enable(Fn<void(QString error)> done) {
	auto &state = Instance();
	if (state.connecting) {
		return;
	}
	state.connecting = true;
	state.changed.fire({});

	KomaruPlugins::EnsureHost();
	KomaruPlugins::Request(
		QJsonObject{ { u"command"_q, u"tgws_start"_q } },
		[done = std::move(done)](QJsonValue result, QString error) {
			auto &state = Instance();
			state.connecting = false;
			if (!error.isEmpty() || !result.isObject()) {
				state.changed.fire({});
				if (done) {
					done(error.isEmpty() ? u"Failed to start WebSocket proxy"_q : error);
				}
				return;
			}
			const auto obj = result.toObject();
			const auto host = obj.value(u"host"_q).toString();
			const auto port = uint32(obj.value(u"port"_q).toInt());
			const auto secret = obj.value(u"secret"_q).toString();
			if (host.isEmpty() || !port || secret.isEmpty()) {
				state.changed.fire({});
				if (done) {
					done(u"Invalid WebSocket proxy configuration"_q);
				}
				return;
			}
			state.host = host;
			state.port = port;
			state.secret = secret;

			auto proxy = MTP::ProxyData();
			proxy.type = MTP::ProxyData::Type::Mtproto;
			proxy.host = host;
			proxy.port = port;
			proxy.password = u"dd"_q + secret;

			Core::App().setCurrentProxy(
				proxy,
				MTP::ProxyData::Settings::Enabled);
			Core::App().saveSettingsDelayed();
			state.changed.fire({});
			if (done) {
				done(QString());
			}
		});
}

void Disable() {
	auto &state = Instance();
	if (!state.port && !state.connecting) {
		return;
	}
	state.port = 0;
	state.host.clear();
	state.secret.clear();
	state.connecting = false;
	if (KomaruPlugins::HostRunning()) {
		KomaruPlugins::Request(QJsonObject{ { u"command"_q, u"tgws_stop"_q } });
	}
	state.changed.fire({});
}

void Ping(Fn<void(int ms)> done) {
	if (!done) {
		return;
	}
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kPingUrl)));
	request.setTransferTimeout(kPingTimeoutMs);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"KomaruGram"_q);

	auto timer = std::make_shared<QElapsedTimer>();
	timer->start();

	const auto reply = Network().head(request);
	QObject::connect(reply, &QNetworkReply::finished, [reply, timer, done = std::move(done)] {
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto elapsed = int(timer->elapsed());
		const auto reachable = (status > 0)
			|| (reply->error() == QNetworkReply::NoError);
		reply->deleteLater();
		done(reachable ? std::max(1, elapsed) : -1);
	});
}

rpl::producer<> StateChanged() {
	return Instance().changed.events();
}

} // namespace KomaruWsProxy
