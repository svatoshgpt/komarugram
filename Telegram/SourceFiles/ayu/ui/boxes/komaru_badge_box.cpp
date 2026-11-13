// This is the source code of KomaruGram for Desktop.
#include "ayu/ui/boxes/komaru_badge_box.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "ayu/features/komaru/komaru_badges.h"
#include "ayu/komaru_strings.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Ui {
namespace {

constexpr auto kCaptionLimit = 180;

// The bot checks who wrote and puts the status into the shared list.
void SendToBot(not_null<Main::Session*> session, const QString &text) {
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(KomaruBadges::EditBot()),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peer = session->data().peerLoaded(
			peerFromMTP(data.vpeer()));
		if (!peer) {
			return;
		}
		auto message = Api::MessageToSend(
			Api::SendAction(session->data().history(peer)));
		message.textWithTags = { text };
		session->api().sendMessage(std::move(message));
	}).send();
}

} // namespace

void FillKomaruBadgeBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto selfId = uint64(session->userId().bare);

	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(int(st::aboutWidth * 1.1));
	box->verticalLayout()->resizeToWidth(box->width());
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	struct State {
		Info::Profile::EmojiStatusPanel panel;
		std::unique_ptr<Text::CustomEmoji> emoji;
		DocumentId id = 0;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto content = box->verticalLayout();
	AddSkip(content);
	AddSkip(content);
	const auto size = st::supportLogoSize;
	const auto logo = content->add(
		object_ptr<AbstractButton>(content),
		style::al_top);
	logo->resize(size, size);
	logo->paintRequest() | rpl::on_next([=] {
		if (!state->emoji) {
			return;
		}
		auto p = QPainter(logo);
		state->emoji->paint(p, {
			.textColor = st::windowFg->c,
			.now = crl::now(),
			.position = QPoint(),
		});
	}, logo->lifetime());

	const auto setEmoji = [=](DocumentId id) {
		if (!id || id == state->id) {
			return;
		}
		state->id = id;
		state->emoji = session->data().customEmojiManager().create(
			id,
			[=] { logo->update(); },
			Data::CustomEmojiManager::SizeTag::Isolated,
			size * style::DevicePixelRatio());
		logo->update();
	};
	const auto current = KomaruBadges::Statuses(selfId);
	setEmoji(current.empty() ? DocumentId() : DocumentId(current.front()));

	logo->setClickedCallback([=] {
		state->panel.show({
			.controller = controller,
			.button = logo,
			.channelStatusMode = true,
		});
	});
	state->panel.someCustomChosen(
	) | rpl::on_next([=](Info::Profile::EmojiStatusPanel::CustomChosen chosen) {
		setEmoji(chosen.id.documentId);
	}, box->lifetime());

	AddSkip(content);
	content->add(
		object_ptr<FlatLabel>(
			content,
			Komaru::TextValue("badge_title") | rpl::map(tr::bold),
			st::boxTitle),
		st::boxRowPadding,
		style::al_top);
	content->add(
		object_ptr<FlatLabel>(
			content,
			Komaru::TextValue("badge_hint"),
			st::starrefCenteredText),
		st::boxRowPadding,
		style::al_top);
	AddSkip(content);

	const auto field = content->add(
		object_ptr<InputField>(
			content,
			st::defaultInputField,
			InputField::Mode::SingleLine,
			Komaru::TextValue("badge_caption"),
			KomaruBadges::StatusText(selfId)),
		st::boxRowPadding);
	field->setMaxLength(kCaptionLimit);
	AddLengthLimitLabel(field, kCaptionLimit);
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	AddSkip(content);
	content->add(
		object_ptr<FlatLabel>(
			content,
			Komaru::TextValue("badge_about"),
			st::boxDividerLabel),
		st::boxRowPadding);
	AddSkip(content);

	const auto reset = content->add(
		object_ptr<RoundButton>(
			content,
			Komaru::TextValue("badge_reset"),
			st::defaultLightButton),
		style::al_top);
	reset->setClickedCallback([=] {
		SendToBot(session, u"/badge reset"_q);
		KomaruBadges::SetLocalStatus(selfId, 0, QString());
		controller->showToast(Komaru::Text("badge_reset_done"));
		box->closeBox();
	});
	AddSkip(content);

	const auto save = box->addButton(tr::lng_settings_save(), [=] {
		if (!state->id) {
			return;
		}
		const auto caption = field->getLastText().simplified();
		SendToBot(
			session,
			(u"/badge "_q + QString::number(state->id) + ' ' + caption));
		KomaruBadges::SetLocalStatus(selfId, state->id, caption);
		controller->showToast(Komaru::Text("badge_saved"));
		box->closeBox();
	});
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::starrefFooterBox.buttonPadding);
	save->widthValue() | rpl::filter([=] {
		return (save->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=] {
		save->resizeToWidth(buttonWidth);
	}, save->lifetime());
}

} // namespace Ui
