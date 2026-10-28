// This is the source code of KomaruGram for Desktop.
//
// Plugins run in a separate Python process, the plugin host, so a broken
// plugin can never take the client down. It runs on the Python installed on
// the computer (3.9 or newer); the client installs Telethon and Kurigram for
// it with pip into tdata. Without Python the user is asked to install it. The host gets each logged in
// account's DC and auth key over its stdin, so Telethon, Kurigram and Go
// plugins act as that account without a separate login. The key is never
// written to disk by the client.
#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Main {
class Session;
} // namespace Main

namespace KomaruPlugins {

enum class RuntimeState {
	Missing,
	Installing,
	Ready,
	Failed,
};

struct PluginInfo {
	QString id;
	QString name;
	QString description;
	QString author;
	QString version;
	QString format;
	QString runtime;
	QString error;
	bool enabled = false;
	bool loaded = false;
	bool hasSettings = false;
	bool sendHook = false;
};

using Done = Fn<void(QJsonValue result, QString error)>;

// Starts the host at launch when the runtime is installed and some plugin
// is enabled.
void Start();

[[nodiscard]] RuntimeState CurrentRuntimeState();
[[nodiscard]] rpl::producer<RuntimeState> RuntimeStateValue();
[[nodiscard]] rpl::producer<QString> RuntimeStatusValue();
void InstallRuntime();

// Makes sure the host runs, for the plugins screen.
void EnsureHost();
[[nodiscard]] bool HostRunning();

void Request(QJsonObject command, Done done = nullptr);

[[nodiscard]] const std::vector<PluginInfo> &Plugins();
[[nodiscard]] rpl::producer<> PluginsChanged();
[[nodiscard]] rpl::producer<QString> SettingsChanged();
void Refresh();

[[nodiscard]] QString LogPath();

struct Outgoing {
	QString text;
	bool cancel = false;
};

[[nodiscard]] bool HasSendHooks();

// Asks the plugins with a send hook about outgoing text, waiting at most a
// second and a half; on a timeout the text goes out unchanged.
[[nodiscard]] Outgoing TransformOutgoing(
	not_null<Main::Session*> session,
	int64 peer,
	const QString &text);

} // namespace KomaruPlugins
