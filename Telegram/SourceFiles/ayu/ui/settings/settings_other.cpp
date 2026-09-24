// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_other.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/analytics/komaru_analytics.h"
#include "ayu/ui/boxes/donate_qr_box.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "lang/lang_text_entity.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QGuiApplication>
#include <QSvgRenderer>

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

struct Asset {
	QString icon;
};

Asset getAsset(const QString &name) {
	const auto normalized = name.toLower();
	return {
		.icon = QString(":/gui/icons/ayu/donates/%1.svg").arg(normalized),
	};
}

QImage getImage(const QString &name) {
	const auto iconData = getAsset(name);

	const auto factor = style::DevicePixelRatio();
	const auto size = st::menuIconLink.size();
	auto image = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);

		// The coin logos carry their own colours; no plate behind them.
		auto svgIcon = QSvgRenderer(iconData.icon);
		svgIcon.render(&p, Rect(size));
	}

	return image;
}

[[nodiscard]] not_null<Ui::SettingsButton*> AddDonate(
		not_null<Ui::SettingsButton*> button,
		const QString &name) {
	const auto btnContainer = Ui::CreateChild<Ui::RpWidget>(button);
	const auto &buttonSt = button->st();
	const auto fullHeight = buttonSt.height
		+ rect::m::sum::v(buttonSt.padding);

	const auto iconWidget = Ui::CreateChild<Ui::RpWidget>(button.get());

	auto icon = getImage(name);
	iconWidget->resize(icon.size() / style::DevicePixelRatio());
	iconWidget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(iconWidget);
		p.drawImage(0, 0, icon);
	}, iconWidget->lifetime());

	button->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		iconWidget->moveToLeft(
			button->st().iconLeft
				+ (st::menuIconShop.width() - iconWidget->width()) / 2,
			(s.height() - iconWidget->height()) / 2);
		btnContainer->moveToLeft(
			iconWidget->x() - (fullHeight - iconWidget->height()) / 2,
			0);
	}, iconWidget->lifetime());

	btnContainer->resize(fullHeight, fullHeight);

	return button;
}

void AddCryptoDonate(
		const QString &name,
		const QString &address,
		not_null<Ui::VerticalLayout*> container) {
	const auto button = AddDonate(
		AddButtonWithIcon(
			container,
			rpl::single(name),
			st::settingsButton),
		name);
	button->setClickedCallback([=] {
		auto box = Box(
			Ui::FillDonateQrBox,
			address,
			getAsset(name).icon);
		Ui::show(std::move(box));
	});
}

void BuildDonations(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;

			AddSubsectionTitle(container, tr::ayu_SupportHeader());
			AddCryptoDonate("Gram", QString("UQAirl_g-9BqSSTGqcX0LHGEyhN_ewZ0ucNakXGrw-h9tMY4"), container);
			AddSkip(container);

			AddDividerText(container,
				tr::ayu_SupportDescription2(
					lt_item,
					rpl::single(tr::link(
						tr::ayu_SupportDescription1(tr::now),
						u"tg://support"_q)),
					tr::marked));
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"ayu/donate"_q,
				.title = tr::ayu_SupportHeader(tr::now),
				.section = sctx.sectionId,
			});
		});
	});
}

void BuildCrashReporting(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	// Statistics are offered on every platform, but only in builds that
	// carry Measurement Protocol credentials.
	const auto statistics = KomaruAnalytics::Available();
#ifdef TDESKTOP_DISABLE_AUTOUPDATE
	if (!statistics) {
		return;
	}
#endif
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_CategoryOther());

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	ayu.addSettingToggle({
		.id = u"ayu/crashReporting"_q,
		.altIds = { u"ayu/crashlytics"_q },
		.title = tr::ayu_CrashReporting(),
		.getter = &AyuSettings::crashReporting,
		.setter = &AyuSettings::setCrashReporting,
		.icon = { &st::menuIconReport },
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_CrashReportingDescription());
#endif

	if (statistics) {
		builder.addSkip();
		ayu.addSettingToggle({
			.id = u"ayu/anonymousStatistics"_q,
			.title = tr::ayu_AnonymousStatistics(),
			.getter = &AyuSettings::sendAnonymousStatistics,
			.setter = &AyuSettings::setSendAnonymousStatistics,
			.icon = { &st::menuIconStats },
		});
		builder.addSkip();
		builder.addDividerText(tr::ayu_AnonymousStatisticsDescription());
	}
}

void BuildOtherThings(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addButton({
		.id = u"ayu/registerUrlScheme"_q,
		.title = tr::ayu_RegisterURLScheme(),
		.icon = { &st::menuIconLink },
		.onClick = [=] {
			Core::Application::RegisterUrlScheme();
			controller->showToast(tr::lng_box_done(tr::now));
		},
	});
	builder.addButton({
		.id = u"ayu/resetSettings"_q,
		.title = tr::ayu_ResetSettings(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_ResetSettingsConfirmation(tr::rich),
				.confirmed = [=](Fn<void()> &&close) {
					AyuSettings::reset();
					controller->showToast(tr::lng_box_done(tr::now));
					close();
				},
				.confirmText = tr::lng_box_yes(),
			}));
		},
	});
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuOther::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryOther,
	.icon = &st::menuIconFave,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildDonations(builder);
	BuildCrashReporting(builder, ayu);
	BuildOtherThings(builder);
});

} // namespace

rpl::producer<QString> AyuOther::title() {
	return tr::ayu_CategoryOther();
}

AyuOther::AyuOther(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuOther::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuOtherId() {
	return AyuOther::Id();
}

} // namespace Settings
