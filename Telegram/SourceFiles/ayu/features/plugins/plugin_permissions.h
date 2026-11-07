// This is the source code of KomaruGram for Desktop.
#pragma once

#include <deque>

namespace KomaruPlugins {

// What a plugin may do with the account; keep in sync with ACTIONS in rust/komaru_tools/src/permissions.rs.
enum class PluginAction : uchar {
	Base,
	Forbidden,
	Other,
	MessagesRead,
	MessagesSend,
	MessagesMarkRead,
	MessagesEdit,
	MessagesDelete,
	ProfileInfo,
	ProfilePhoto,
	ProfileUsername,
	ProfileStatus,
	ProfileMusic,
	ChatsJoin,
	ChatsManage,
	Contacts,
	GiftsView,
	GiftsBuy,
	GiftsTransfer,
	StoriesView,
	StoriesPost,
	// Not API methods: the send hook and incoming updates.
	MessagesOutgoing,
};

struct MethodInfo {
	const char *name = nullptr;
	PluginAction action = PluginAction::Other;
};

// Generated from api.tl by `komaru tl-permissions`.
[[nodiscard]] MethodInfo LookupMethod(uint32 id);

struct PermissionGroup {
	QString title;
	std::vector<PluginAction> actions;
};

[[nodiscard]] std::vector<PermissionGroup> PermissionGroups();
[[nodiscard]] QString ActionKey(PluginAction action);
[[nodiscard]] std::optional<PluginAction> ActionByKey(const QString &key);
[[nodiscard]] QString ActionTitle(PluginAction action);

// Allowed, denied, or not asked yet.
[[nodiscard]] std::optional<bool> Permission(
	const QString &plugin,
	PluginAction action);
void SetPermission(const QString &plugin, PluginAction action, bool allowed);
void ForgetPermissions(const QString &plugin);
[[nodiscard]] rpl::producer<QString> PermissionsChanged();

struct PermissionLogEntry {
	TimeId date = 0;
	QString method;
	bool allowed = false;
};
void LogPermission(const QString &plugin, QString method, bool allowed);
[[nodiscard]] const std::deque<PermissionLogEntry> &PermissionLog(
	const QString &plugin);

// Allowed now, or asks the user once and answers later.
void CheckPermission(
	const QString &plugin,
	const QString &pluginName,
	PluginAction action,
	Fn<void(bool)> done);

} // namespace KomaruPlugins
