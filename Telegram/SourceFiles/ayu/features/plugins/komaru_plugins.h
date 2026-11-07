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

class PeerData;

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

// An item a plugin added to a menu with add_menu_item.
struct PluginMenuItem {
	QString id;
	QString type;
	QString text;
	QString subtext;
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
	bool wantsUpdates = false;
	std::vector<PluginMenuItem> menu;
	// Actions from the manifest, asked for at install.
	QStringList permissions;
};

using Done = Fn<void(QJsonValue result, QString error)>;

// Prepares the plugins at launch and starts the host a few seconds later,
// so Telegram itself comes up first, when the runtime is installed and some
// plugin is enabled.
void Start();

// The switch on the plugins screen. While off, the host does not run at all
// and the choice of each plugin stays as it was.
[[nodiscard]] bool PluginsEnabled();
void SetPluginsEnabled(bool enabled);

[[nodiscard]] RuntimeState CurrentRuntimeState();
[[nodiscard]] rpl::producer<RuntimeState> RuntimeStateValue();
[[nodiscard]] rpl::producer<QString> RuntimeStatusValue();
void InstallRuntime();

// Makes sure the host runs, for the plugins screen.
void EnsureHost();
[[nodiscard]] bool HostRunning();

void Request(QJsonObject command, Done done = nullptr);

[[nodiscard]] const std::vector<PluginInfo> &Plugins();

// Looks at the plugin folder, so it works before the host is running.
[[nodiscard]] bool InstalledOnDisk(const QString &id);
[[nodiscard]] rpl::producer<> PluginsChanged();
[[nodiscard]] rpl::producer<QString> SettingsChanged();
void Refresh();

[[nodiscard]] QString LogPath();

// Hands what the client receives to plugins that listen for updates.
void ForwardUpdates(
	not_null<Main::Session*> session,
	const MTPUpdates &updates);

// Runs a plugin menu item for a chat, as exteraGram does from the chat
// action menu.
void ClickMenuItem(
	not_null<Main::Session*> session,
	const QString &plugin,
	const QString &item,
	not_null<PeerData*> peer);

struct Outgoing {
	QString text;
	bool cancel = false;
};

[[nodiscard]] bool HasSendHooks();

// Asks the send hooks in sending order without blocking; a late answer sends the text as is.
void TransformOutgoing(
	not_null<Main::Session*> session,
	int64 peer,
	const QString &text,
	Fn<void(Outgoing)> done);

} // namespace KomaruPlugins
