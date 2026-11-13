// This file is part of KomaruGram for Desktop, see https://github.com/svatoshgpt/komarugram
#include "ayu/ui/boxes/extera_badge_box.h"

#include "lang_auto.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Ui {
namespace {

constexpr auto kProfileUrl = "https://api.exteragram.app/api/v1/profiles/";

// The arrow every supporter gets, shown until exteraGram names the real one.
constexpr auto kDefaultArrow = DocumentId(5391059537102927631ULL);

struct Profile {
	int number = 0;
	DocumentId badge = 0;

	friend inline bool operator==(const Profile &, const Profile &) = default;
};

[[nodiscard]] Profile ParseProfile(const QByteArray &bytes) {
	const auto root = QJsonDocument::fromJson(bytes).object();
	return {
		.number = root.value(u"number"_q).toInt(),
		.badge = DocumentId(root.value(u"badge"_q).toObject()
			.value(u"documentId"_q).toVariant().toULongLong()),
	};
}

object_ptr<RpWidget> CreateBadgeLogo(
		not_null<RpWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<DocumentId> document) {
	auto result = object_ptr<RpWidget>(parent);
	const auto raw = result.data();
	const auto size = st::supportLogoSize;
	raw->resize(size, size);

	struct State {
		std::unique_ptr<Text::CustomEmoji> emoji;
		DocumentId id = 0;
	};
	const auto state = raw->lifetime().make_state<State>();
	std::move(
		document
	) | rpl::on_next([=](DocumentId id) {
		if (!id || id == state->id) {
			return;
		}
		state->id = id;
		state->emoji = session->data().customEmojiManager().create(
			id,
			[=] { raw->update(); },
			Data::CustomEmojiManager::SizeTag::Isolated,
			size * style::DevicePixelRatio());
		raw->update();
	}, raw->lifetime());

	raw->paintRequest(
	) | rpl::on_next([=] {
		if (!state->emoji) {
			return;
		}
		const auto origin = QPoint(
			(raw->width() - size) / 2,
			(raw->height() - size) / 2);
		const auto color = st::profileVerifiedCheckBg->c;
		if (state->id != kDefaultArrow) {
			// A chosen emoji keeps its own colours.
			auto p = QPainter(raw);
			state->emoji->paint(p, {
				.textColor = color,
				.now = crl::now(),
				.position = origin,
			});
			return;
		}
		// The arrow is as blue as next to the name, keeping its shading.
		const auto ratio = style::DevicePixelRatio();
		auto image = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			state->emoji->paint(q, {
				.textColor = color,
				.now = crl::now(),
				.position = QPoint(),
			});
		}
		auto tinted = image;
		{
			auto q = QPainter(&tinted);
			q.setCompositionMode(QPainter::CompositionMode_Multiply);
			q.fillRect(QRect(0, 0, size, size), color);
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawImage(0, 0, image);
		}
		auto p = QPainter(raw);
		p.drawImage(origin, tinted);
	}, raw->lifetime());
	return result;
}

object_ptr<RpWidget> PerkRow(
		not_null<RpWidget*> parent,
		const QString &title,
		const QString &text,
		not_null<const style::icon*> icon) {
	auto row = object_ptr<VerticalLayout>(parent);
	const auto raw = row.data();
	raw->add(
		object_ptr<FlatLabel>(
			raw,
			rpl::single(tr::bold(title)),
			st::defaultFlatLabel),
		st::settingsPremiumRowTitlePadding);
	raw->add(
		object_ptr<FlatLabel>(raw, text, st::boxDividerLabel),
		st::settingsPremiumRowAboutPadding);
	object_ptr<Info::Profile::FloatingIcon>(
		raw,
		*icon,
		st::starrefInfoIconPosition);
	return row;
}

} // namespace

void FillExteraBadgeBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto session = &controller->session();
	const auto self = peer->isSelf();
	const auto profile = box->lifetime().make_state<
		rpl::variable<Profile>>(Profile{ .badge = kDefaultArrow });

	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(int(st::aboutWidth * 1.1));
	box->verticalLayout()->resizeToWidth(box->width());
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto content = box->verticalLayout();
	AddSkip(content);
	AddSkip(content);
	content->add(
		CreateBadgeLogo(
			content,
			session,
			profile->value() | rpl::map([](const Profile &value) {
				return value.badge;
			})),
		style::al_top);
	AddSkip(content);

	content->add(
		object_ptr<FlatLabel>(
			content,
			(self
				? tr::ayu_ExteraBadgeTitleSelf(tr::bold)
				: tr::ayu_ExteraBadgeTitle(tr::bold)),
			st::boxTitle),
		st::boxRowPadding,
		style::al_top);

	const auto name = peer->name();
	// Centered by its natural width, so the late number re-centers it.
	const auto about = content->add(
		object_ptr<FlatLabel>(
			content,
			profile->value() | rpl::map([=](const Profile &value) {
				const auto number = value.number
					? (u"#"_q + QString::number(value.number))
					: QString();
				return self
					? tr::ayu_ExteraBadgeAboutSelf(
						tr::now,
						lt_number,
						number)
					: tr::ayu_ExteraBadgeAbout(
						tr::now,
						lt_item,
						name,
						lt_number,
						number);
			}),
			st::starrefCenteredText),
		st::boxRowPadding,
		style::al_top);
	about->setTryMakeSimilarLines(true);

	AddSkip(content);
	AddSkip(content);
	content->add(PerkRow(
		content,
		tr::ayu_ExteraBadgeNumberTitle(tr::now),
		tr::ayu_ExteraBadgeNumberAbout(tr::now),
		&st::ayuExteraBadgePerkIcon));
	AddSkip(content);

	const auto close = box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::starrefFooterBox.buttonPadding);
	close->widthValue() | rpl::filter([=] {
		return (close->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=] {
		close->resizeToWidth(buttonWidth);
	}, close->lifetime());

	const auto manager = new QNetworkAccessManager(box);
	const auto reply = manager->get(QNetworkRequest(QUrl(
		QString::fromLatin1(kProfileUrl)
			+ QString::number(getBareID(peer)))));
	QObject::connect(reply, &QNetworkReply::finished, box, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		auto parsed = ParseProfile(reply->readAll());
		if (!parsed.badge) {
			parsed.badge = kDefaultArrow;
		}
		*profile = parsed;
	});
}

} // namespace Ui
