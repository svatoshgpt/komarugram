// This is the source code of KomaruGram for Desktop.
#include "ayu/ui/settings/settings_plugins.h"

#include "ayu/features/plugins/komaru_plugins.h"
#include "ayu/features/plugins/plugin_permissions.h"
#include "base/unixtime.h"
#include "core/file_utilities.h"
#include "lang_auto.h"
#include "mainwindow.h"
#include "settings/settings_common.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace Settings {
namespace {

using namespace KomaruPlugins;

[[nodiscard]] QJsonObject Command(const QString &cmd, const QString &plugin) {
	auto result = QJsonObject();
	result.insert(u"cmd"_q, cmd);
	if (!plugin.isEmpty()) {
		result.insert(u"plugin"_q, plugin);
	}
	return result;
}

[[nodiscard]] const PluginInfo *FindPlugin(const QString &id) {
	for (const auto &plugin : Plugins()) {
		if (plugin.id == id) {
			return &plugin;
		}
	}
	return nullptr;
}

[[nodiscard]] QString StatusText(const PluginInfo &plugin) {
	if (!plugin.error.isEmpty()) {
		return tr::ayu_PluginsFailed(tr::now);
	}
	return plugin.enabled
		? tr::ayu_PluginsEnabled(tr::now)
		: tr::ayu_PluginsDisabled(tr::now);
}

void ShowTextValueBox(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &value,
		Fn<void(QString)> save) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(title),
			value));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		box->addButton(tr::lng_settings_save(), [=] {
			save(field->getLastText());
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowSelectorBox(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QStringList &items,
		Fn<void(int)> choose) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto content = box->verticalLayout();
		for (auto i = 0; i != int(items.size()); ++i) {
			AddButtonWithIcon(
				content,
				rpl::single(items[i]),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				choose(i);
				box->closeBox();
			});
		}
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowPluginBox(
	not_null<Window::SessionController*> controller,
	const QString &id,
	const QString &page);

void FillSettingsItems(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		const QString &id,
		const QString &page,
		const QJsonArray &items,
		Fn<void(QJsonArray)> refill) {
	const auto setValue = [=](int index, QJsonValue value) {
		auto command = Command(u"set_setting"_q, id);
		command.insert(u"page"_q, page);
		command.insert(u"index"_q, index);
		command.insert(u"value"_q, value);
		Request(command, [=](QJsonValue result, QString error) {
			if (error.isEmpty()) {
				refill(result.toArray());
			}
		});
	};
	for (const auto &entry : items) {
		const auto item = entry.toObject();
		const auto type = item.value(u"type"_q).toString();
		const auto index = item.value(u"index"_q).toInt();
		const auto text = item.value(u"text"_q).toString();
		const auto subtext = item.value(u"subtext"_q).toString();
		if (type == u"header"_q) {
			Ui::AddSkip(container);
			Ui::AddSubsectionTitle(container, rpl::single(text));
		} else if (type == u"divider"_q) {
			Ui::AddSkip(container);
			if (text.isEmpty()) {
				Ui::AddDivider(container);
			} else {
				Ui::AddDividerText(container, rpl::single(text));
			}
		} else if (type == u"switch"_q) {
			const auto value = item.value(u"value"_q).toBool();
			const auto button = container->add(object_ptr<Ui::SettingsButton>(
				container,
				rpl::single(text),
				st::settingsButtonNoIcon));
			button->toggleOn(rpl::single(value));
			button->toggledValue(
			) | rpl::filter([=](bool toggled) {
				return toggled != value;
			}) | rpl::take(1) | rpl::on_next([=](bool toggled) {
				setValue(index, toggled);
			}, button->lifetime());
		} else if (type == u"selector"_q) {
			auto options = QStringList();
			for (const auto &option : item.value(u"items"_q).toArray()) {
				options.push_back(option.toString());
			}
			const auto current = item.value(u"value"_q).toInt();
			const auto label = (current >= 0 && current < options.size())
				? options[current]
				: QString();
			AddButtonWithLabel(
				container,
				rpl::single(text),
				rpl::single(label),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				ShowSelectorBox(controller, text, options, [=](int chosen) {
					setValue(index, chosen);
				});
			});
		} else if (type == u"input"_q || type == u"edittext"_q) {
			const auto title = text.isEmpty()
				? item.value(u"hint"_q).toString()
				: text;
			const auto value = item.value(u"value"_q).toString();
			AddButtonWithLabel(
				container,
				rpl::single(title),
				rpl::single(value),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				ShowTextValueBox(controller, title, value, [=](QString saved) {
					setValue(index, saved);
				});
			});
		} else if (type == u"text"_q) {
			const auto button = AddButtonWithIcon(
				container,
				rpl::single(text),
				item.value(u"red"_q).toBool()
					? st::settingsAttentionButton
					: st::settingsButtonNoIcon);
			if (item.value(u"clickable"_q).toBool()) {
				button->setClickedCallback([=] {
					auto command = Command(u"click"_q, id);
					command.insert(u"page"_q, page);
					command.insert(u"index"_q, index);
					Request(command, [=](QJsonValue result, QString error) {
						const auto open = result.toObject()
							.value(u"open"_q).toString();
						if (error.isEmpty() && !open.isEmpty()) {
							ShowPluginBox(controller, id, open);
						}
					});
				});
			} else {
				button->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
		} else {
			continue;
		}
		if (!subtext.isEmpty() && type != u"header"_q) {
			Ui::AddDividerText(container, rpl::single(subtext));
		}
	}
}

void ShowPluginBox(
		not_null<Window::SessionController*> controller,
		const QString &id,
		const QString &page) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto plugin = FindPlugin(id);
		box->setTitle(rpl::single(plugin ? plugin->name : id));
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();

		if (page.isEmpty() && plugin) {
			const auto about = QStringList{
				plugin->version,
				plugin->author,
				plugin->format,
			}.filter(QRegularExpression(u"\\S"_q)).join(u" · "_q);
			Ui::AddDividerText(content, rpl::single(about));
			if (!plugin->description.isEmpty()) {
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						rpl::single(plugin->description),
						st::boxLabel),
					st::boxRowPadding);
			}
			if (!plugin->error.isEmpty()) {
				Ui::AddSkip(content);
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						rpl::single(plugin->error),
						st::boxDividerLabel),
					st::boxRowPadding);
			}
			Ui::AddSkip(content);
			const auto enabled = plugin->enabled;
			const auto toggle = content->add(object_ptr<Ui::SettingsButton>(
				content,
				tr::ayu_PluginEnable(),
				st::settingsButtonNoIcon));
			toggle->toggleOn(rpl::single(enabled));
			toggle->toggledValue(
			) | rpl::filter([=](bool toggled) {
				return toggled != enabled;
			}) | rpl::take(1) | rpl::on_next([=](bool toggled) {
				const auto apply = [=] {
					auto command = Command(u"set_enabled"_q, id);
					command.insert(u"enabled"_q, toggled);
					Request(command);
					box->closeBox();
				};
				if (!toggled) {
					apply();
					return;
				}
				controller->show(Ui::MakeConfirmBox({
					.text = tr::ayu_PluginWarning(),
					.confirmed = [=](Fn<void()> &&close) {
						apply();
						close();
					},
					.cancelled = [=](Fn<void()> &&close) {
						box->closeBox();
						close();
					},
					.confirmText = tr::ayu_PluginEnable(),
				}));
			}, toggle->lifetime());
		}

		if (page.isEmpty() && plugin) {
			const auto permissions = content->add(
				object_ptr<Ui::SettingsButton>(
					content,
					tr::ayu_PluginPermissions(),
					st::settingsButtonNoIcon));
			permissions->setClickedCallback([=] {
				ShowPermissionsBox(controller, id);
			});
		}

		const auto settings = content->add(
			object_ptr<Ui::VerticalLayout>(content));
		const auto refill = std::make_shared<Fn<void(QJsonArray)>>();
		*refill = [=](QJsonArray items) {
			settings->clear();
			if (!items.isEmpty() && page.isEmpty()) {
				Ui::AddSkip(settings);
				Ui::AddDivider(settings);
				Ui::AddSkip(settings);
				Ui::AddSubsectionTitle(settings, tr::ayu_PluginSettingsHeader());
			}
			FillSettingsItems(controller, settings, id, page, items, *refill);
			settings->resizeToWidth(content->width());
		};
		const auto load = [=] {
			auto command = Command(u"settings"_q, id);
			command.insert(u"page"_q, page);
			Request(command, crl::guard(box, [=](QJsonValue result, QString e) {
				if (e.isEmpty()) {
					(*refill)(result.toArray());
				}
			}));
		};
		if (!plugin || plugin->loaded) {
			load();
		}
		SettingsChanged(
		) | rpl::filter(rpl::mappers::_1 == id) | rpl::on_next([=] {
			load();
		}, box->lifetime());

		if (page.isEmpty()) {
			box->addLeftButton(tr::ayu_PluginDelete(), [=] {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::ayu_PluginDeleteSure(),
					.confirmed = [=](Fn<void()> &&close) {
						UninstallPlugin(id);
						box->closeBox();
						close();
					},
					.confirmText = tr::ayu_PluginDelete(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			});
		}
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowPermissionsBox(
		not_null<Window::SessionController*> controller,
		const QString &id) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto plugin = FindPlugin(id);
		box->setTitle(tr::ayu_PluginPermissions());
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		Ui::AddDividerText(
			content,
			tr::ayu_PluginPermissionsAbout(
				lt_name,
				rpl::single(plugin ? plugin->name : id)));
		for (const auto &group : PermissionGroups()) {
			const auto actions = group.actions;
			const auto size = int(actions.size());
			const auto count = [=] {
				return int(ranges::count_if(actions, [&](PluginAction a) {
					return Permission(id, a).value_or(false);
				}));
			};
			const auto title = content->lifetime().make_state<
				rpl::variable<QString>>();
			const auto all = content->lifetime().make_state<
				rpl::variable<bool>>();
			const auto name = group.title;
			const auto refresh = [=] {
				const auto now = count();
				*title = name
					+ u"  "_q
					+ QString::number(now)
					+ u"/"_q
					+ QString::number(size);
				*all = (now == size);
			};
			refresh();
			Ui::AddSkip(content);
			const auto toggle = content->add(object_ptr<Ui::SettingsButton>(
				content,
				title->value(),
				st::settingsButtonNoIcon));
			toggle->toggleOn(all->value());
			auto checks = std::vector<Ui::Checkbox*>();
			for (const auto action : actions) {
				const auto check = content->add(
					object_ptr<Ui::Checkbox>(
						content,
						ActionTitle(action),
						Permission(id, action).value_or(false),
						st::defaultBoxCheckbox),
					st::boxRowPadding + QMargins(0, st::boxLittleSkip, 0, 0));
				check->checkedChanges(
				) | rpl::on_next([=](bool checked) {
					SetPermission(id, action, checked);
					refresh();
				}, check->lifetime());
				checks.push_back(check);
			}
			// The switch turns the whole group on or off.
			toggle->toggledChanges(
			) | rpl::on_next([=](bool on) {
				const auto now = count();
				if ((on && now < size) || (!on && now == size)) {
					for (const auto check : checks) {
						check->setChecked(on);
					}
				}
			}, toggle->lifetime());
		}

		Ui::AddSkip(content);
		Ui::AddDivider(content);
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, tr::ayu_PluginPermissionsLog());
		const auto &log = PermissionLog(id);
		if (log.empty()) {
			content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					tr::ayu_PluginPermissionsLogEmpty(),
					st::boxDividerLabel),
				st::boxRowPadding);
		}
		for (const auto &entry : log) {
			content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					base::unixtime::parse(entry.date).toString(u"HH:mm:ss"_q)
						+ u"  "_q
						+ entry.method
						+ u" — "_q
						+ (entry.allowed
							? tr::ayu_PluginPermissionAllowed(tr::now)
							: tr::ayu_PluginPermissionDenied(tr::now)),
					st::boxDividerLabel),
				st::boxRowPadding);
		}
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

// What a plugin's manifest asks for, all allowed unless unchecked.
void ShowRequestedPermissionsBox(
		not_null<Window::SessionController*> controller,
		const QString &id,
		const QString &name,
		QStringList keys,
		Fn<void()> done) {
	auto actions = std::vector<PluginAction>();
	for (const auto &key : keys) {
		if (const auto action = ActionByKey(key)) {
			actions.push_back(*action);
		}
	}
	if (actions.empty()) {
		done();
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(name));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::ayu_PluginPermissionsRequested(),
			st::boxLabel));
		auto checks = std::vector<std::pair<PluginAction, Ui::Checkbox*>>();
		for (const auto action : actions) {
			checks.emplace_back(action, box->addRow(object_ptr<Ui::Checkbox>(
				box,
				ActionTitle(action),
				true,
				st::defaultBoxCheckbox)));
		}
		box->boxClosing() | rpl::on_next([=] {
			for (const auto &[action, check] : checks) {
				SetPermission(id, action, check->checked());
			}
			done();
		}, box->lifetime());
		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	}));
}

void InstallFromFile(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		QPointer<QWidget>(controller->widget().get()),
		tr::ayu_PluginsInstall(tr::now),
		u"KomaruGram plugins (*.plugin *.elyx *.eaf *.zip *.py);;All files (*.*)"_q,
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			if (!result.paths.isEmpty()) {
				InstallPluginFile(controller, result.paths.front());
			}
		}));
}

void FillRuntime(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::ayu_PluginsRuntimeHeader());
	const auto state = CurrentRuntimeState();
	const auto installing = (state == RuntimeState::Installing);
	auto label = installing
		? RuntimeStatusValue()
		: rpl::producer<QString>(rpl::single(QString()));
	AddButtonWithLabel(
		container,
		(state == RuntimeState::Failed)
			? tr::ayu_PluginsRuntimeRetry()
			: tr::ayu_PluginsRuntimeInstall(),
		std::move(label),
		st::settingsButtonActive
	)->setClickedCallback([=] {
		InstallRuntime();
	});
	AddButtonWithIcon(
		container,
		tr::ayu_PluginsGetPython(),
		st::settingsButton,
		{ &st::menuIconDownload }
	)->setClickedCallback([] {
		File::OpenUrl(u"https://www.python.org/downloads/"_q);
	});
	Ui::AddSkip(container);
	if (state == RuntimeState::Failed) {
		Ui::AddDividerText(container, RuntimeStatusValue());
	} else {
		Ui::AddDividerText(container, tr::ayu_PluginsRuntimeAbout());
	}
}

void FillList(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	const auto toggle = container->add(object_ptr<Ui::SettingsButton>(
		container,
		tr::ayu_PluginsEnableAll(),
		st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(PluginsEnabled()));
	toggle->toggledChanges(
	) | rpl::on_next([](bool enabled) {
		// Switching rebuilds this screen, button included.
		crl::on_main([=] { SetPluginsEnabled(enabled); });
	}, toggle->lifetime());
	if (!PluginsEnabled()) {
		Ui::AddSkip(container);
		Ui::AddDividerText(container, tr::ayu_PluginsAllOff());
		return;
	}
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	AddButtonWithIcon(
		container,
		tr::ayu_PluginsInstall(),
		st::settingsButtonActive,
		{ &st::menuIconAdd }
	)->setClickedCallback([=] {
		InstallFromFile(controller);
	});
	AddButtonWithIcon(
		container,
		tr::ayu_PluginsOpenLog(),
		st::settingsButton,
		{ &st::menuIconReport }
	)->setClickedCallback([=] {
		File::ShowInFolder(LogPath());
	});
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::ayu_PluginsListHeader());
	if (Plugins().empty()) {
		Ui::AddDividerText(container, tr::ayu_PluginsEmpty());
		return;
	}
	for (const auto &plugin : Plugins()) {
		const auto id = plugin.id;
		AddButtonWithLabel(
			container,
			rpl::single(plugin.name),
			rpl::single(StatusText(plugin)),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			ShowPluginBox(controller, id, QString());
		});
	}
	Ui::AddSkip(container);
}

} // namespace

KomaruPluginsSection::KomaruPluginsSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> KomaruPluginsSection::title() {
	return tr::ayu_PluginsTitle();
}

void KomaruPluginsSection::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto rebuild = [=] {
		content->clear();
		if (CurrentRuntimeState() == RuntimeState::Ready) {
			FillList(controller, content);
		} else {
			FillRuntime(controller, content);
		}
		content->resizeToWidth(width());
	};
	rpl::merge(
		RuntimeStateValue() | rpl::to_empty,
		PluginsChanged()
	) | rpl::on_next(rebuild, lifetime());
	if (CurrentRuntimeState() == RuntimeState::Ready) {
		EnsureHost();
		Refresh();
	}
	Ui::ResizeFitChild(this, content);
}

Type KomaruPluginsId() {
	return KomaruPluginsSection::Id();
}

void InstallPluginFile(
		not_null<Window::SessionController*> controller,
		const QString &path,
		Fn<void()> installed) {
	if (!PluginsEnabled()) {
		controller->showToast(tr::ayu_PluginsAllOff(tr::now));
		controller->showSettings(KomaruPluginsId());
		return;
	}
	if (CurrentRuntimeState() != RuntimeState::Ready) {
		controller->showToast(tr::ayu_PluginsNeedRuntime(tr::now));
		controller->showSettings(KomaruPluginsId());
		return;
	}
	auto command = Command(u"install"_q, QString());
	command.insert(u"path"_q, path);
	Request(command, crl::guard(controller, [=](
			QJsonValue value,
			QString error) {
		if (!error.isEmpty()) {
			controller->showToast(error);
			return;
		}
		const auto object = value.toObject();
		const auto id = object.value(u"id"_q).toString();
		const auto finish = crl::guard(controller, [=] {
			if (installed) {
				Refresh();
				installed();
				return;
			}
			// The box needs the plugin in the list, which the refresh brings.
			PluginsChanged(
			) | rpl::take(1) | rpl::on_next(crl::guard(controller, [=] {
				ShowPluginBox(controller, id, QString());
			}), controller->lifetime());
			Refresh();
		});
		ShowRequestedPermissionsBox(
			controller,
			id,
			object.value(u"name"_q).toString(),
			object.value(u"permissions"_q).toVariant().toStringList(),
			finish);
	}));
}

void UninstallPlugin(const QString &id, Fn<void()> done) {
	ForgetPermissions(id);
	Request(Command(u"uninstall"_q, id), [=](QJsonValue, QString) {
		Refresh();
		if (done) {
			done();
		}
	});
}

bool PluginInstalled(const QString &id) {
	return (FindPlugin(id) != nullptr) || InstalledOnDisk(id);
}

} // namespace Settings
