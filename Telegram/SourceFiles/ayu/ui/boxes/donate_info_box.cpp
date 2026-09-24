// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/donate_info_box.h"

#include "lang_auto.h"
#include "ayu/features/komaru/komaru_badges.h"
#include "base/timer.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include <QSvgRenderer>

namespace Ui {
namespace {

// Who takes the donations and the proof of them.
constexpr auto kDonateUsername = "kdecat";

// The face a supporter gets, with its particles moving. It is drawn in the
// text colour: supporters choose their own colours when they send proof.
object_ptr<Ui::RpWidget> CreateTopLogoWidget(
		not_null<Ui::RpWidget*> parent) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();

	const auto timer = raw->lifetime().make_state<base::Timer>([=] {
		raw->update();
	});
	if (KomaruBadges::Animated()) {
		timer->callEach(KomaruBadges::kFrameDelay);
	}
	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		const auto size = std::min(raw->width(), raw->height());
		const auto color = st::windowBoldFg->c;
		KomaruBadges::Paint(
			p,
			QRect(
				(raw->width() - size) / 2,
				(raw->height() - size) / 2,
				size,
				size),
			KomaruBadges::Entry{
				.kind = KomaruBadges::Kind::Supporter,
				.face = color,
				.particles = color,
			},
			crl::now());
	}, raw->lifetime());

	return result;
}

object_ptr<Ui::RpWidget> InfoRow(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Session*> session,
	const QString &title,
	const TextWithEntities &text,
	not_null<const style::icon*> icon,
	const Text::MarkedContext &context) {
	auto row = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = row.data();

	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			rpl::single(tr::bold(title)),
			st::defaultFlatLabel),
		st::settingsPremiumRowTitlePadding);

	const auto label = raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			st::boxDividerLabel),
		st::settingsPremiumRowAboutPadding);

	label->setMarkedText(
		text,
		std::move(context)
	);

	object_ptr<Info::Profile::FloatingIcon>(
		raw,
		*icon,
		st::starrefInfoIconPosition);

	return row;
}

} // namespace

void FillDonateInfoBox(not_null<Ui::GenericBox*> box, not_null<Window::SessionController*> controller) {
	// box->setStyle(st::starrefFooterBox);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(int(st::aboutWidth * 1.1));
	box->verticalLayout()->resizeToWidth(box->width());

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	const auto logoWidget = box->verticalLayout()->add(
		CreateTopLogoWidget(box->verticalLayout()));

	logoWidget->resize(st::supportLogoSize, st::supportLogoSize);

	Ui::AddSkip(box->verticalLayout());

	box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box->verticalLayout(),
			tr::ayu_SupportBoxHeader(tr::bold),
			st::boxTitle),
		st::boxRowPadding,
		style::al_top);

	box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box->verticalLayout(),
			tr::ayu_SupportBoxInfo(),
			st::starrefCenteredText),
		st::boxRowPadding);

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	// The badge costs a fixed sum in dollars; the Gram amount follows
	// the latest exchange rate.
	const auto usd = u"$"_q
		+ QString::number(KomaruBadges::kDonateUsd, 'f', 2);
	const auto gram = KomaruBadges::DonateGramAmount();
	auto amount = gram.isEmpty()
		? tr::bold(usd)
		: tr::bold(gram + u" Gram"_q).append(u" (≈ "_q + usd + ')');
	const auto str = tr::ayu_KomaruSupportDonationInfo(
		tr::now,
		lt_amount,
		std::move(amount),
		tr::rich);

	box->verticalLayout()->add(InfoRow(
		box->verticalLayout(),
		&controller->session(),
		tr::ayu_SupportBoxMakeDonationHeader(tr::now),
		str,
		&st::menuIconEarn,
		Core::TextContext({
			.session = &controller->session(),
		})));

	Ui::AddSkip(box->verticalLayout());

	const auto username = u"@"_q + QString::fromLatin1(kDonateUsername);
	auto usernameTrimmed = username;
	if (usernameTrimmed.startsWith('@')) {
		usernameTrimmed.remove(0, 1);
	}
	const TextWithEntities proofText = tr::ayu_KomaruSupportProofInfo(
		tr::now,
		lt_item,
		Ui::Text::Link(username, controller->session().createInternalLinkFull(usernameTrimmed)),
		tr::rich);
	box->verticalLayout()->add(InfoRow(
		box->verticalLayout(),
		&controller->session(),
		tr::ayu_SupportBoxSendProofHeader(tr::now),
		proofText,
		&st::menuIconPhoto,
		Core::TextContext({
			.session = std::move(&controller->session()),
		})));

	Ui::AddSkip(box->verticalLayout());

	box->verticalLayout()->add(InfoRow(
		box->verticalLayout(),
		&controller->session(),
		tr::ayu_SupportBoxReceiveBadgeHeader(tr::now),
		TextWithEntities{
			tr::ayu_KomaruSupportBadgeInfo(tr::now)
		},
		&st::menuIconStarRefShare,
		Core::TextContext({
			.session = std::move(&controller->session()),
		})));

	const auto closeButton = box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::starrefFooterBox.buttonPadding);
	closeButton->widthValue() | rpl::filter([=]
	{
		return (closeButton->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=]
							  {
								  closeButton->resizeToWidth(buttonWidth);
							  },
							  closeButton->lifetime());
}

} // namespace Ui
