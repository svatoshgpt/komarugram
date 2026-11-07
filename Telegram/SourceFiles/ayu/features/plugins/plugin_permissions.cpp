// This is the source code of KomaruGram for Desktop.
#include "ayu/features/plugins/plugin_permissions.h"

#include "base/unixtime.h"
#include "core/application.h"
#include "lang_auto.h"
#include "settings.h"
#include "styles/style_layers.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <set>

namespace KomaruPlugins {
namespace {

constexpr auto kLogLimit = 50;

struct Asking {
	bool shown = false;
	std::vector<Fn<void(bool)>> waiting;
};

struct State {
	bool loaded = false;
	std::map<QString, std::map<QString, bool>> granted;
	std::set<std::pair<QString, PluginAction>> once;
	std::map<QString, std::deque<PermissionLogEntry>> log;
	std::map<std::pair<QString, PluginAction>, Asking> asking;
	rpl::event_stream<QString> changed;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString StorePath() {
	return cWorkingDir() + u"tdata/plugins/data/permissions.json"_q;
}

void Load() {
	auto &state = Instance();
	if (state.loaded) {
		return;
	}
	state.loaded = true;
	auto file = QFile(StorePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = root.begin(); i != root.end(); ++i) {
		const auto actions = i.value().toObject();
		auto &entry = state.granted[i.key()];
		for (auto j = actions.begin(); j != actions.end(); ++j) {
			entry[j.key()] = j.value().toBool();
		}
	}
}

void Save() {
	auto root = QJsonObject();
	for (const auto &[plugin, actions] : Instance().granted) {
		auto object = QJsonObject();
		for (const auto &[key, allowed] : actions) {
			object.insert(key, allowed);
		}
		root.insert(plugin, object);
	}
	auto file = QFile(StorePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson());
	}
}

constexpr auto kKeys = std::array{
	std::pair{ PluginAction::Other, "other" },
	std::pair{ PluginAction::MessagesRead, "messages.read" },
	std::pair{ PluginAction::MessagesSend, "messages.send" },
	std::pair{ PluginAction::MessagesOutgoing, "messages.outgoing" },
	std::pair{ PluginAction::MessagesMarkRead, "messages.mark_read" },
	std::pair{ PluginAction::MessagesEdit, "messages.edit" },
	std::pair{ PluginAction::MessagesDelete, "messages.delete" },
	std::pair{ PluginAction::ProfileInfo, "profile.info" },
	std::pair{ PluginAction::ProfilePhoto, "profile.photo" },
	std::pair{ PluginAction::ProfileUsername, "profile.username" },
	std::pair{ PluginAction::ProfileStatus, "profile.status" },
	std::pair{ PluginAction::ProfileMusic, "profile.music" },
	std::pair{ PluginAction::ChatsJoin, "chats.join" },
	std::pair{ PluginAction::ChatsManage, "chats.manage" },
	std::pair{ PluginAction::Contacts, "chats.contacts" },
	std::pair{ PluginAction::GiftsView, "gifts.view" },
	std::pair{ PluginAction::GiftsBuy, "gifts.buy" },
	std::pair{ PluginAction::GiftsTransfer, "gifts.transfer" },
	std::pair{ PluginAction::StoriesView, "stories.view" },
	std::pair{ PluginAction::StoriesPost, "stories.post" },
};

void Answer(const QString &plugin, PluginAction action, bool allowed) {
	auto &state = Instance();
	const auto i = state.asking.find({ plugin, action });
	if (i == end(state.asking)) {
		return;
	}
	auto waiting = base::take(i->second.waiting);
	state.asking.erase(i);
	for (const auto &done : waiting) {
		done(allowed);
	}
}

void Ask(const QString &plugin, const QString &name, PluginAction action) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		Answer(plugin, action, false);
		return;
	}
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto answered = box->lifetime().make_state<bool>(false);
		const auto reply = [=](std::optional<bool> remember, bool allowed) {
			*answered = true;
			if (remember) {
				SetPermission(plugin, action, *remember);
			} else {
				Instance().once.emplace(plugin, action);
				Instance().changed.fire_copy(plugin);
			}
			Answer(plugin, action, allowed);
			box->closeBox();
		};
		box->setTitle(rpl::single(name));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::ayu_PluginPermissionAsk(
				lt_action,
				rpl::single(ActionTitle(action))),
			st::boxLabel));
		box->addButton(tr::ayu_PluginPermissionAllow(), [=] {
			reply(true, true);
		});
		box->addButton(tr::ayu_PluginPermissionOnce(), [=] {
			reply(std::nullopt, true);
		});
		box->addLeftButton(tr::ayu_PluginPermissionDeny(), [=] {
			reply(false, false);
		});
		box->boxClosing() | rpl::on_next([=] {
			if (!*answered) {
				Answer(plugin, action, false);
			}
		}, box->lifetime());
	}), Ui::LayerOption::KeepOther);
}

} // namespace

std::vector<PermissionGroup> PermissionGroups() {
	using A = PluginAction;
	return {
		{ tr::ayu_PluginPermMessages(tr::now), {
			A::MessagesRead, A::MessagesSend, A::MessagesOutgoing,
			A::MessagesMarkRead, A::MessagesEdit, A::MessagesDelete } },
		{ tr::ayu_PluginPermProfile(tr::now), {
			A::ProfileInfo, A::ProfilePhoto, A::ProfileUsername,
			A::ProfileStatus, A::ProfileMusic } },
		{ tr::ayu_PluginPermChats(tr::now), {
			A::ChatsJoin, A::ChatsManage, A::Contacts } },
		{ tr::ayu_PluginPermGifts(tr::now), {
			A::GiftsView, A::GiftsBuy, A::GiftsTransfer } },
		{ tr::ayu_PluginPermStories(tr::now), {
			A::StoriesView, A::StoriesPost } },
		{ tr::ayu_PluginPermOther(tr::now), { A::Other } },
	};
}

QString ActionKey(PluginAction action) {
	for (const auto &[value, key] : kKeys) {
		if (value == action) {
			return QString::fromLatin1(key);
		}
	}
	return QString();
}

std::optional<PluginAction> ActionByKey(const QString &key) {
	for (const auto &[value, name] : kKeys) {
		if (key == QLatin1String(name)) {
			return value;
		}
	}
	return std::nullopt;
}

QString ActionTitle(PluginAction action) {
	using A = PluginAction;
	switch (action) {
	case A::MessagesRead: return tr::ayu_PluginPermRead(tr::now);
	case A::MessagesSend: return tr::ayu_PluginPermSend(tr::now);
	case A::MessagesOutgoing: return tr::ayu_PluginPermOutgoing(tr::now);
	case A::MessagesMarkRead: return tr::ayu_PluginPermMarkRead(tr::now);
	case A::MessagesEdit: return tr::ayu_PluginPermEdit(tr::now);
	case A::MessagesDelete: return tr::ayu_PluginPermDelete(tr::now);
	case A::ProfileInfo: return tr::ayu_PluginPermInfo(tr::now);
	case A::ProfilePhoto: return tr::ayu_PluginPermPhoto(tr::now);
	case A::ProfileUsername: return tr::ayu_PluginPermUsername(tr::now);
	case A::ProfileStatus: return tr::ayu_PluginPermStatus(tr::now);
	case A::ProfileMusic: return tr::ayu_PluginPermMusic(tr::now);
	case A::ChatsJoin: return tr::ayu_PluginPermJoin(tr::now);
	case A::ChatsManage: return tr::ayu_PluginPermManage(tr::now);
	case A::Contacts: return tr::ayu_PluginPermContacts(tr::now);
	case A::GiftsView: return tr::ayu_PluginPermGiftsView(tr::now);
	case A::GiftsBuy: return tr::ayu_PluginPermGiftsBuy(tr::now);
	case A::GiftsTransfer: return tr::ayu_PluginPermGiftsTransfer(tr::now);
	case A::StoriesView: return tr::ayu_PluginPermStoriesView(tr::now);
	case A::StoriesPost: return tr::ayu_PluginPermStoriesPost(tr::now);
	default: return tr::ayu_PluginPermOtherAbout(tr::now);
	}
}

std::optional<bool> Permission(const QString &plugin, PluginAction action) {
	if (action == PluginAction::Base) {
		return true;
	} else if (action == PluginAction::Forbidden) {
		return false;
	}
	Load();
	const auto &granted = Instance().granted;
	const auto i = granted.find(plugin);
	if (i != end(granted)) {
		const auto j = i->second.find(ActionKey(action));
		if (j != end(i->second)) {
			return j->second;
		}
	}
	// "Only now" lasts until the client restarts.
	if (Instance().once.contains({ plugin, action })) {
		return true;
	}
	return std::nullopt;
}

void SetPermission(const QString &plugin, PluginAction action, bool allowed) {
	const auto key = ActionKey(action);
	if (key.isEmpty()) {
		return;
	}
	Load();
	Instance().granted[plugin][key] = allowed;
	Save();
	Instance().changed.fire_copy(plugin);
}

void ForgetPermissions(const QString &plugin) {
	Load();
	auto &state = Instance();
	state.log.erase(plugin);
	if (state.granted.erase(plugin)) {
		Save();
		state.changed.fire_copy(plugin);
	}
}

rpl::producer<QString> PermissionsChanged() {
	return Instance().changed.events();
}

void LogPermission(const QString &plugin, QString method, bool allowed) {
	auto &list = Instance().log[plugin];
	list.push_front({
		.date = base::unixtime::now(),
		.method = std::move(method),
		.allowed = allowed,
	});
	if (list.size() > kLogLimit) {
		list.pop_back();
	}
}

const std::deque<PermissionLogEntry> &PermissionLog(const QString &plugin) {
	return Instance().log[plugin];
}

void CheckPermission(
		const QString &plugin,
		const QString &pluginName,
		PluginAction action,
		Fn<void(bool)> done) {
	if (const auto known = Permission(plugin, action)) {
		done(*known);
		return;
	} else if (plugin.isEmpty()) {
		done(false);
		return;
	}
	auto &asking = Instance().asking[{ plugin, action }];
	asking.waiting.push_back(std::move(done));
	if (!asking.shown) {
		asking.shown = true;
		Ask(plugin, pluginName.isEmpty() ? plugin : pluginName, action);
	}
}

} // namespace KomaruPlugins
