// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_appearance.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/components/avatar_corners_preview.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_polls.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

void BuildAppIcon(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/appIcon"_q,
		.title = tr::ayu_AppIconHeader(),
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	builder.addDivider();
	builder.addSkip();
	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationBadge"_q,
		.title = tr::ayu_HideNotificationBadge(),
		.getter = &AyuSettings::hideNotificationBadge,
		.setter = &AyuSettings::setHideNotificationBadge,
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_HideNotificationBadgeDescription());
	builder.addSkip();
#else
    builder.addDivider();
    builder.addSkip();
#endif
}

void BuildAvatarCorners(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::ayu_AvatarCornersSquare(tr::now).toUpper();
		} else if (val == AyuUiSettings::kMaxAvatarCorners) {
			return tr::ayu_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::ayu_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersValue() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::ayuBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::ayuBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"ayu/avatarCorners"_q,
			.title = tr::ayu_AvatarCorners(tr::now),
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	ayu.addSlider({
		.id = u"ayu/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = AyuUiSettings::kMaxAvatarCorners + 1,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			ShowRestartPrompt(controller);
		},
	});

	ayu.addSettingToggle({
		.id = u"ayu/singleCornerRadius"_q,
		.title = tr::ayu_SingleCornerRadius(),
		.getter = &AyuSettings::singleCornerRadius,
		.setter = &AyuSettings::setSingleCornerRadius,
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_SingleCornerRadiusDescription());
	builder.addSkip();
}

void BuildAppearance(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle(tr::ayu_CategoryAppearance());

	ayu.addSettingToggle({
		.id = u"ayu/materialSwitches"_q,
		.altIds = { u"ayu/newSwitchStyle"_q },
		.title = tr::ayu_MaterialSwitches(),
		.getter = &AyuSettings::materialSwitches,
		.setter = &AyuSettings::setMaterialSwitches,
	});
	ayu.addSettingToggle({
		.id = u"ayu/disableCustomBackgrounds"_q,
		.altIds = { u"ayu/customThemes"_q },
		.title = tr::ayu_DisableCustomBackgrounds(),
		.getter = &AyuSettings::disableCustomBackgrounds,
		.setter = &AyuSettings::setDisableCustomBackgrounds,
	});
	const auto controller = builder.controller();

	// Everything that hides part of the interface lives here, next to the
	// premium statuses toggle.
	ayu.addSettingToggle({
		.id = u"ayu/hidePremiumStatuses"_q,
		.title = tr::ayu_HidePremiumStatuses(),
		.getter = &AyuSettings::hidePremiumStatuses,
		.setter = &AyuSettings::setHidePremiumStatuses,
	});
	ayu.addSettingToggle({
		.id = u"ayu/hideExteraBadges"_q,
		.altIds = { u"ayu/hideSupporterBadges"_q },
		.title = tr::ayu_HideExteraBadges(),
		.getter = &AyuSettings::hideExteraBadges,
		.setter = &AyuSettings::setHideExteraBadges,
	});
	ayu.addToggle({
		.id = u"ayu/disableStories"_q,
		.altIds = { u"ayu/hideStories"_q },
		.title = tr::ayu_DisableStories(),
		.getter = [=] { return settings->disableStories(); },
		.setter = [=](bool enabled) {
			AyuSettings::getInstance().setDisableStories(enabled);
			ShowRestartPrompt(controller);
		},
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/hideReactions"_q,
		.title = tr::ayu_HideReactions(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_HideReactionsInChannels(tr::now),
				[] { return !AyuSettings::getInstance().showChannelReactions(); },
				[](bool v) { AyuSettings::getInstance().setShowChannelReactions(!v); }
			},
			NestedEntry{
				tr::ayu_HideReactionsInGroups(tr::now),
				[] { return !AyuSettings::getInstance().showGroupReactions(); },
				[](bool v) { AyuSettings::getInstance().setShowGroupReactions(!v); }
			},
			NestedEntry{
				tr::ayu_HideReactionsInPrivateChats(tr::now),
				[] { return !AyuSettings::getInstance().showPrivateChatReactions(); },
				[](bool v) { AyuSettings::getInstance().setShowPrivateChatReactions(!v); }
			}
		},
		.toggledWhenAll = false,
	});

	ayu.addSettingToggle({
		.id = u"ayu/hideFastShare"_q,
		.altIds = { u"ayu/hideShareButton"_q },
		.title = tr::ayu_HideShareButton(),
		.getter = &AyuSettings::hideFastShare,
		.setter = &AyuSettings::setHideFastShare,
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/similarChannels"_q,
		.title = tr::ayu_DisableSimilarChannels(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_CollapseSimilarChannels(tr::now),
				[] { return AyuSettings::getInstance().collapseSimilarChannels(); },
				[](bool v) { AyuSettings::getInstance().setCollapseSimilarChannels(v); }
			},
			NestedEntry{
				tr::ayu_HideSimilarChannelsTab(tr::now),
				[] { return AyuSettings::getInstance().hideSimilarChannels(); },
				[](bool v) { AyuSettings::getInstance().setHideSimilarChannels(v); }
			}
		},
		.toggledWhenAll = true,
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/profileInfo"_q,
		.title = tr::ayu_ShowInProfile(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_ProfileRegistrationDate(tr::now),
				[] { return Core::App().settings().birthDateEnabled(); },
				[](bool v) {
					Core::App().settings().setBirthDateEnabled(v);
					Core::App().saveSettingsDelayed();
				}
			},
			NestedEntry{
				tr::ayu_ProfileDatacenter(tr::now),
				[] { return Core::App().settings().datacenterEnabled(); },
				[](bool v) {
					Core::App().settings().setDatacenterEnabled(v);
					Core::App().saveSettingsDelayed();
				}
			},
			// Profile photos a user hid, fetched from the Gamee cache.
			NestedEntry{
				tr::ayu_ProfileHiddenPhotos(tr::now),
				[] { return Core::App().settings().gameeEnabled(); },
				[](bool v) {
					Core::App().settings().setGameeEnabled(v);
					Core::App().saveSettingsDelayed();
				}
			}
		},
		.toggledWhenAll = false,
	});

	builder.addButton({
		.id = u"ayu/monoFont"_q,
		.title = tr::ayu_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::ayu_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			AyuUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					AyuSettings::getInstance().setMonoFont(font);
				});
		},
	});

	ayu.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_ChatFoldersHeader());

	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationCounters"_q,
		.altIds = { u"ayu/tabCounter"_q },
		.title = tr::ayu_HideNotificationCounters(),
		.getter = &AyuSettings::hideNotificationCounters,
		.setter = &AyuSettings::setHideNotificationCounters,
	});
	ayu.addSettingToggle({
		.id = u"ayu/hideAllChatsFolder"_q,
		.altIds = { u"ayu/hideAllChats"_q },
		.title = tr::ayu_HideAllChats(),
		.getter = &AyuSettings::hideAllChatsFolder,
		.setter = &AyuSettings::setHideAllChatsFolder,
	});

	ayu.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::ayu_TrayElementsHeader());

	ayu.addSettingToggle({
		.id = u"ayu/showGhostToggleInTray"_q,
		.title = tr::ayu_EnableGhostModeTray(),
		.getter = &AyuSettings::showGhostToggleInTray,
		.setter = &AyuSettings::setShowGhostToggleInTray,
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	ayu.addSettingToggle({
		.id = u"ayu/showStreamerToggleInTray"_q,
		.title = tr::ayu_EnableStreamerModeTray(),
		.getter = &AyuSettings::showStreamerToggleInTray,
		.setter = &AyuSettings::setShowStreamerToggleInTray,
	});
#endif

	ayu.addSectionDivider();
}

struct DrawerItem {
	QString settingId;
	rpl::producer<QString> title;
	IconDescriptor icon;
	Fn<bool()> getter;
	Fn<void(bool)> setter;
};

[[nodiscard]] DrawerItem LookupDrawerItem(const QString &id) {
	using Getter = bool (AyuSettings::*)() const;
	using Setter = void (AyuSettings::*)(bool);
	const auto toggle = [](
			QString settingId,
			rpl::producer<QString> title,
			IconDescriptor icon,
			Getter getter,
			Setter setter) {
		return DrawerItem{
			.settingId = std::move(settingId),
			.title = std::move(title),
			.icon = std::move(icon),
			.getter = [=] { return (AyuSettings::getInstance().*getter)(); },
			.setter = [=](bool v) { (AyuSettings::getInstance().*setter)(v); },
		};
	};
	if (id == u"myProfile"_q) {
		return toggle(
			u"ayu/showMyProfileInDrawer"_q,
			tr::lng_menu_my_profile(),
			{ &st::menuIconProfile },
			&AyuSettings::showMyProfileInDrawer,
			&AyuSettings::setShowMyProfileInDrawer);
	} else if (id == u"bots"_q) {
		return toggle(
			u"ayu/showBotsInDrawer"_q,
			tr::lng_filters_type_bots(),
			{ &st::menuIconBot },
			&AyuSettings::showBotsInDrawer,
			&AyuSettings::setShowBotsInDrawer);
	} else if (id == u"newGroup"_q) {
		return toggle(
			u"ayu/showNewGroupInDrawer"_q,
			tr::lng_create_group_title(),
			{ &st::menuIconGroups },
			&AyuSettings::showNewGroupInDrawer,
			&AyuSettings::setShowNewGroupInDrawer);
	} else if (id == u"newChannel"_q) {
		return toggle(
			u"ayu/showNewChannelInDrawer"_q,
			tr::lng_create_channel_title(),
			{ &st::menuIconChannel },
			&AyuSettings::showNewChannelInDrawer,
			&AyuSettings::setShowNewChannelInDrawer);
	} else if (id == u"contacts"_q) {
		return toggle(
			u"ayu/showContactsInDrawer"_q,
			tr::lng_menu_contacts(),
			{ &st::menuIconUserShow },
			&AyuSettings::showContactsInDrawer,
			&AyuSettings::setShowContactsInDrawer);
	} else if (id == u"calls"_q) {
		return toggle(
			u"ayu/showCallsInDrawer"_q,
			tr::lng_menu_calls(),
			{ &st::menuIconPhone },
			&AyuSettings::showCallsInDrawer,
			&AyuSettings::setShowCallsInDrawer);
	} else if (id == u"savedMessages"_q) {
		return toggle(
			u"ayu/showSavedMessagesInDrawer"_q,
			tr::lng_saved_messages(),
			{ &st::menuIconSavedMessages },
			&AyuSettings::showSavedMessagesInDrawer,
			&AyuSettings::setShowSavedMessagesInDrawer);
	} else if (id == u"wallet"_q) {
		return toggle(
			u"ayu/drawerWallet"_q,
			tr::lng_wallet_menu(),
			{ &st::walletMenuIcon },
			&AyuSettings::showWalletInDrawer,
			&AyuSettings::setShowWalletInDrawer);
	} else if (id == u"lread"_q) {
		return toggle(
			u"ayu/showLReadToggleInDrawer"_q,
			tr::ayu_LReadMessages(),
			{ &st::ayuLReadMenuIcon },
			&AyuSettings::showLReadToggleInDrawer,
			&AyuSettings::setShowLReadToggleInDrawer);
	} else if (id == u"sread"_q) {
		return toggle(
			u"ayu/showSReadToggleInDrawer"_q,
			tr::ayu_SReadMessages(),
			{ &st::ayuSReadMenuIcon },
			&AyuSettings::showSReadToggleInDrawer,
			&AyuSettings::setShowSReadToggleInDrawer);
	} else if (id == u"nightMode"_q) {
		return toggle(
			u"ayu/showNightModeToggleInDrawer"_q,
			tr::lng_menu_night_mode(),
			{ &st::menuIconNightMode },
			&AyuSettings::showNightModeToggleInDrawer,
			&AyuSettings::setShowNightModeToggleInDrawer);
	} else if (id == u"ghost"_q) {
		return toggle(
			u"ayu/showGhostToggleInDrawer"_q,
			tr::ayu_GhostModeToggle(),
			{ &st::ayuGhostIcon },
			&AyuSettings::showGhostToggleInDrawer,
			&AyuSettings::setShowGhostToggleInDrawer);
	} else if (id == u"streamer"_q) {
		return toggle(
			u"ayu/showStreamerToggleInDrawer"_q,
			tr::ayu_StreamerModeToggle(),
			{ &st::ayuStreamerModeMenuIcon },
			&AyuSettings::showStreamerToggleInDrawer,
			&AyuSettings::setShowStreamerToggleInDrawer);
	}
	// The settings entry is always in the menu; it can only move.
	return DrawerItem{
		.settingId = u"ayu/drawerSettings"_q,
		.title = tr::lng_menu_settings(),
		.icon = { &st::menuIconSettings },
	};
}

// A settings row with room on the left for the drag handle.
[[nodiscard]] const style::SettingsButton &DrawerRowStyle() {
	static const auto result = [] {
		auto st = st::settingsButton;
		const auto shift = st::pollBoxMenuPollOrderIcon.width();
		st.padding.setLeft(st.padding.left() + shift);
		st.iconLeft += shift;
		return st;
	}();
	return result;
}

[[nodiscard]] not_null<Ui::RpWidget*> AddDragHandle(
		not_null<Ui::SettingsButton*> button) {
	const auto &icon = st::pollBoxMenuPollOrderIcon;
	// A button of its own, so a press on the handle never toggles the row.
	const auto handle = Ui::CreateChild<Ui::AbstractButton>(button.get());
	handle->resize(icon.width(), icon.height());
	handle->setCursor(Qt::SizeVerCursor);
	handle->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(handle);
		icon.paint(p, 0, 0, handle->width());
	}, handle->lifetime());
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		handle->moveToLeft(
			st::settingsButton.iconLeft / 2,
			(size.height() - handle->height()) / 2);
	}, handle->lifetime());
	return handle;
}

[[nodiscard]] not_null<Ui::AbstractButton*> AddRemoveButton(
		not_null<Ui::RpWidget*> row,
		Fn<void()> callback) {
	const auto size = st::drawerSeparatorRemoveSize;
	const auto button = Ui::CreateChild<Ui::AbstractButton>(row.get());
	button->resize(size, size);
	button->setCursor(style::cur_pointer);
	button->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(button);
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::windowActiveTextFg->p;
		pen.setWidthF(st::drawerSeparatorRemoveStroke);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto inset = double(st::drawerSeparatorRemoveInset);
		const auto circle = QRectF(button->rect()).marginsRemoved(
			{ inset, inset, inset, inset });
		p.drawEllipse(circle);
		const auto shift = circle.width() * (1. - M_SQRT1_2) / 2.;
		p.drawLine(
			circle.topLeft() + QPointF(shift, shift),
			circle.bottomRight() - QPointF(shift, shift));
	}, button->lifetime());
	row->sizeValue(
	) | rpl::on_next([=](QSize rowSize) {
		button->moveToRight(
			st::drawerSeparatorRemoveRight,
			(rowSize.height() - button->height()) / 2,
			rowSize.width());
	}, button->lifetime());
	button->setClickedCallback(std::move(callback));
	return button;
}

[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::SettingsButton>> MakeSeparatorRow(
		not_null<QWidget*> parent) {
	return object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
		parent,
		object_ptr<Ui::SettingsButton>(
			parent,
			tr::ayu_DrawerSeparator(),
			DrawerRowStyle()));
}

// Puts the items a list editor does not show (bots when there are none,
// the streamer toggle on Linux) back after the item that preceded them.
[[nodiscard]] std::vector<QString> RestoreUnlisted(
		std::vector<QString> listed,
		const std::vector<QString> &old) {
	for (auto i = 0; i != int(old.size()); ++i) {
		const auto &id = old[i];
		if (id == u"separator"_q || ranges::contains(listed, id)) {
			continue;
		}
		auto at = 0;
		for (auto j = i; j > 0;) {
			--j;
			if (old[j] == u"separator"_q) {
				continue;
			}
			const auto found = ranges::find(listed, old[j]);
			if (found != end(listed)) {
				at = int(found - begin(listed)) + 1;
				break;
			}
		}
		listed.insert(begin(listed) + at, id);
	}
	return listed;
}

void BuildDrawerElements(SectionBuilder &builder, AyuSectionBuilder &) {
	builder.addSubsectionTitle(tr::ayu_DrawerElementsHeader());

	const auto controller = builder.controller();
	const auto hasBots = controller && HasDrawerBots(controller);
#if defined Q_OS_WIN || defined Q_OS_MAC
	constexpr auto hasStreamer = true;
#else // Q_OS_WIN || Q_OS_MAC
	constexpr auto hasStreamer = false;
#endif // Q_OS_WIN || Q_OS_MAC

	auto shown = std::vector<QString>();
	for (const auto &id : AyuSettings::getInstance().drawerOrder()) {
		if ((id == u"bots"_q && !hasBots)
			|| (id == u"streamer"_q && !hasStreamer)) {
			continue;
		}
		shown.push_back(id);
	}

	// The rows follow the saved order and are dragged by their handles.
	// A removed separator keeps its collapsed row and an empty id, so the
	// indices the reorder reports still match these vectors.
	struct Rows {
		std::vector<QString> ids;
		std::vector<not_null<Ui::RpWidget*>> widgets;
		std::vector<not_null<Ui::RpWidget*>> handles;
		Ui::VerticalLayout *list = nullptr;
		Ui::VerticalLayoutReorder *reorder = nullptr;
	};
	const auto rows = std::make_shared<Rows>();
	const auto saveOrder = [=] {
		auto listed = std::vector<QString>();
		for (const auto &id : rows->ids) {
			if (!id.isEmpty()) {
				listed.push_back(id);
			}
		}
		AyuSettings::getInstance().setDrawerOrder(RestoreUnlisted(
			std::move(listed),
			AyuSettings::getInstance().drawerOrder()));
	};
	const auto setupSeparator = [=](
			not_null<Ui::SlideWrap<Ui::SettingsButton>*> wrap) {
		const auto handle = AddDragHandle(wrap->entity());
		AddRemoveButton(wrap->entity(), [=] {
			const auto i = ranges::find(rows->widgets, wrap.get());
			if (i == end(rows->widgets)) {
				return;
			}
			rows->ids[i - begin(rows->widgets)] = QString();
			wrap->hide(anim::type::normal);
			saveOrder();
		});
		rows->widgets.push_back(wrap);
		rows->handles.push_back(handle);
	};

	const auto list = builder.scope([&] {
		for (const auto &id : shown) {
			if (id == u"separator"_q) {
				const auto row = builder.addControl({
					.factory = [=](not_null<Ui::VerticalLayout*> parent) {
						auto result = MakeSeparatorRow(parent);
						setupSeparator(result.data());
						return object_ptr<Ui::RpWidget>(std::move(result));
					},
				});
				if (row) {
					rows->ids.push_back(id);
				}
				continue;
			}
			auto item = LookupDrawerItem(id);
			const auto getter = item.getter;
			const auto setter = item.setter;
			const auto button = builder.addButton({
				.id = item.settingId,
				.title = std::move(item.title),
				.st = &DrawerRowStyle(),
				.icon = std::move(item.icon),
				.toggled = (getter
					? rpl::producer<bool>(rpl::single(getter()))
					: rpl::producer<bool>()),
			});
			if (!button) {
				continue;
			}
			if (getter) {
				button->toggledValue(
				) | rpl::filter([=](bool enabled) {
					return (enabled != getter());
				}) | rpl::on_next([=](bool enabled) {
					setter(enabled);
				}, button->lifetime());
			}
			rows->ids.push_back(id);
			rows->widgets.push_back(button);
			rows->handles.push_back(AddDragHandle(button));
		}
	});
	if (!list
		|| rows->ids.size() != shown.size()
		|| rows->widgets.size() != shown.size()) {
		builder.addSkip();
		return;
	}
	rows->list = list;
	list->lifetime().add([rows] {
		rows->list = nullptr;
		rows->reorder = nullptr;
	});

	const auto reorder = list->lifetime().make_state<
		Ui::VerticalLayoutReorder>(list);
	rows->reorder = reorder;
	reorder->setMouseEventProxy([=](int index) -> not_null<Ui::RpWidget*> {
		return rows->handles[index];
	});
	reorder->updates(
	) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
		using ReorderState = Ui::VerticalLayoutReorder::State;
		if (data.state != ReorderState::Applied) {
			return;
		}
		const auto move = [&](auto &vector) {
			auto moved = std::move(vector[data.oldPosition]);
			vector.erase(begin(vector) + data.oldPosition);
			vector.insert(begin(vector) + data.newPosition, std::move(moved));
		};
		move(rows->ids);
		move(rows->widgets);
		move(rows->handles);
		saveOrder();
	}, list->lifetime());
	reorder->start();

	builder.addButton({
		.id = u"ayu/drawerAddSeparator"_q,
		.title = tr::ayu_DrawerAddSeparator(),
		.icon = { &st::menuIconAdd },
		.onClick = [=] {
			if (!rows->list || !rows->reorder) {
				return;
			}
			const auto wrap = rows->list->add(MakeSeparatorRow(rows->list));
			setupSeparator(wrap);
			rows->ids.push_back(u"separator"_q);
			saveOrder();
			rows->reorder->cancel();
			rows->reorder->start();
		},
	});

	builder.addSkip();
}

struct AttachItem {
	rpl::producer<QString> title;
	IconDescriptor icon;
};

[[nodiscard]] AttachItem LookupAttachItem(const QString &id) {
	if (id == u"photo"_q) {
		return { tr::lng_attach_photo_or_video(), { &st::menuIconPhoto } };
	} else if (id == u"document"_q) {
		return { tr::lng_attach_document(), { &st::menuIconFile } };
	} else if (id == u"money"_q) {
		return { tr::lng_wallet_menu(), { &st::walletMenuIcon } };
	} else if (id == u"poll"_q) {
		return { tr::lng_polls_menu_item(), { &st::menuIconCreatePoll } };
	} else if (id == u"todo"_q) {
		return { tr::lng_todo_menu_item(), { &st::menuIconCreateTodoList } };
	} else if (id == u"article"_q) {
		return { tr::lng_article_menu_item(), { &st::menuIconArticle } };
	} else if (id == u"location"_q) {
		return { tr::lng_maps_point(), { &st::menuIconAddress } };
	}
	return { tr::lng_all_music(), { &st::menuIconSoundOn } };
}

void BuildAttachElements(SectionBuilder &builder, AyuSectionBuilder &) {
	builder.addSubsectionTitle(tr::ayu_AttachMenuHeader());

	auto ids = AyuSettings::getInstance().attachOrder();
	auto handles = std::vector<not_null<Ui::RpWidget*>>();
	const auto list = builder.scope([&] {
		for (const auto &id : ids) {
			auto item = LookupAttachItem(id);
			const auto button = builder.addButton({
				.id = u"ayu/attach_"_q + id,
				.title = std::move(item.title),
				.st = &DrawerRowStyle(),
				.icon = std::move(item.icon),
				.toggled = rpl::single(
					AyuSettings::getInstance().attachItemShown(id)),
			});
			if (!button) {
				continue;
			}
			button->toggledValue(
			) | rpl::filter([=](bool enabled) {
				return (enabled != AyuSettings::getInstance().attachItemShown(id));
			}) | rpl::on_next([=](bool enabled) {
				AyuSettings::getInstance().setAttachItemShown(id, enabled);
			}, button->lifetime());
			handles.push_back(AddDragHandle(button));
		}
	});
	if (!list || handles.size() != ids.size()) {
		builder.addSkip();
		return;
	}

	struct Rows {
		std::vector<QString> ids;
		std::vector<not_null<Ui::RpWidget*>> handles;
	};
	const auto rows = list->lifetime().make_state<Rows>(Rows{
		.ids = std::move(ids),
		.handles = std::move(handles),
	});
	const auto reorder = list->lifetime().make_state<
		Ui::VerticalLayoutReorder>(list);
	reorder->setMouseEventProxy([=](int index) -> not_null<Ui::RpWidget*> {
		return rows->handles[index];
	});
	reorder->updates(
	) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
		using ReorderState = Ui::VerticalLayoutReorder::State;
		if (data.state != ReorderState::Applied) {
			return;
		}
		const auto move = [&](auto &vector) {
			auto moved = std::move(vector[data.oldPosition]);
			vector.erase(begin(vector) + data.oldPosition);
			vector.insert(begin(vector) + data.newPosition, std::move(moved));
		};
		move(rows->ids);
		move(rows->handles);
		AyuSettings::getInstance().setAttachOrder(rows->ids);
	}, list->lifetime());
	reorder->start();

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuAppearance::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildAppIcon(builder, ayu);
	BuildAvatarCorners(builder, ayu);
	BuildAppearance(builder, ayu);
	BuildChatFolders(builder, ayu);
	BuildTrayElements(builder, ayu);
	BuildAttachElements(builder, ayu);
	BuildDrawerElements(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuAppearance::title() {
	return tr::ayu_CategoryAppearance();
}

AyuAppearance::AyuAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuAppearanceId() {
	return AyuAppearance::Id();
}

} // namespace Settings
