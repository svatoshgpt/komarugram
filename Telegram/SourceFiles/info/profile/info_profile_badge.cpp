/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_badge.h"

#include "data/data_changes.h"
#include "data/data_emoji_statuses.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/text/text_custom_emoji.h"
#include "main/main_session.h"
#include "styles/style_info.h"

// AyuGram includes
#include "ayu/features/komaru/komaru_badges.h"
#include "base/timer.h"
#include "styles/style_ayu_icons.h"


namespace Info::Profile {
namespace {

[[nodiscard]] bool HasPremiumClick(const Badge::Content &content) {
	return content.badge == BadgeType::Premium
		|| (content.badge == BadgeType::Verified && content.emojiStatusId)
		|| (content.badge == BadgeType::Extera)
		|| (content.badge == BadgeType::ExteraSupporter)
		|| (content.badge == BadgeType::ExteraCustom)
		|| (content.badge == BadgeType::Komaru);
}

[[nodiscard]] int KomaruFacesCount(const Badge::Content &content) {
	return (content.komaruDeveloper ? 1 : 0)
		+ (content.komaruSupporter ? 1 : 0);
}

// Looks the faces up on every paint: the lists are replaced when they
// refresh, so their entries cannot be kept.
void PaintKomaruFaces(
		QPainter &p,
		const Badge::Content &content,
		int left,
		int height) {
	const auto &icon = st::infoExteraSupporterBadge;
	const auto now = crl::now();
	const auto top = (height - icon.height()) / 2;
	const auto paint = [&](const KomaruBadges::Entry *entry) {
		if (entry) {
			KomaruBadges::Paint(
				p,
				QRect(left, top, icon.width(), icon.height()),
				*entry,
				now);
		}
		left += icon.width();
	};
	if (content.komaruDeveloper) {
		paint(KomaruBadges::Developer(content.komaruPeer));
	}
	if (content.komaruSupporter) {
		paint(KomaruBadges::Supporter(content.komaruPeer));
	}
}

} // namespace

Badge::Badge(
	not_null<QWidget*> parent,
	const style::InfoPeerBadge &st,
	not_null<Main::Session*> session,
	rpl::producer<Content> content,
	EmojiStatusPanel *emojiStatusPanel,
	Fn<bool()> animationPaused,
	int customStatusLoopsLimit,
	base::flags<BadgeType> allowed)
: _parent(parent)
, _st(st)
, _session(session)
, _emojiStatusPanel(emojiStatusPanel)
, _customStatusLoopsLimit(customStatusLoopsLimit)
, _allowed(allowed)
, _animationPaused(std::move(animationPaused)) {
	std::move(
		content
	) | rpl::on_next([=](Content content) {
		setContent(content);
	}, _lifetime);
}

Badge::~Badge() = default;

Ui::RpWidget *Badge::widget() const {
	return _view.data();
}

void Badge::setContent(Content content) {
	// KomaruGram faces go wherever the supporter arrow is allowed.
	const auto permission = (content.badge == BadgeType::Komaru)
		? BadgeType::ExteraSupporter
		: content.badge;
	if (!(_allowed & permission)
		|| (!_session->premiumBadgesShown()
			&& content.badge == BadgeType::Premium)) {
		content.badge = BadgeType::None;
	}
	if (content.badge == BadgeType::None) {
		content = Content();
	}
	if (_content == content) {
		return;
	}
	_content = content;
	_emojiStatus = nullptr;
	_komaruTimer = nullptr;
	_view.destroy();
	if (_content.badge == BadgeType::None) {
		_updated.fire({});
		return;
	}
	_view.create(_parent);
	_view->setAccessibleName([&] {
		switch (_content.badge) {
		case BadgeType::Verified:
			return tr::lng_sr_verified_badge(tr::now);
		case BadgeType::BotVerified:
			return tr::lng_sr_bot_verified_badge(tr::now);
		default:
		case BadgeType::Premium:
			if (_content.emojiStatusId) {
				return tr::lng_profile_bot_emoji_status_access(tr::now);
			}
			return tr::lng_premium_summary_title(tr::now);
		case BadgeType::Scam:
			return tr::lng_scam_badge(tr::now);
		case BadgeType::Fake:
			return tr::lng_fake_badge(tr::now);
		case BadgeType::Direct:
			return tr::lng_direct_badge(tr::now);
		}
		Unexpected("badge type");
	}());
	_view->show();
	const auto faces = _content;
	const auto facesWidth = KomaruFacesCount(_content)
		* st::infoExteraSupporterBadge.width();
	if (facesWidth > 0) {
		_komaruTimer = std::make_unique<base::Timer>([=] {
			const auto paused = _animationPaused && _animationPaused();
			if (_view && !paused && KomaruBadges::Animated()) {
				_view->update();
			}
		});
		_komaruTimer->callEach(KomaruBadges::kFrameDelay);
	}
	switch (_content.badge) {
	case BadgeType::ExteraCustom:
	case BadgeType::Verified:
	case BadgeType::BotVerified:
	case BadgeType::Premium: {
		const auto id = _content.emojiStatusId;
		const auto emoji = id
			? (Data::FrameSizeFromTag(sizeTag())
				/ style::DevicePixelRatio())
			: 0;
		const auto &style = st();
		const auto icon = (_content.badge == BadgeType::Verified)
			? &style.verified
			: id
			? nullptr
			: &style.premium;
		const auto iconForeground = (_content.badge == BadgeType::Verified)
			? &style.verifiedCheck
			: nullptr;
		if (id) {
			_emojiStatus = _session->data().customEmojiManager().create(
				Data::EmojiStatusCustomId(id),
				[raw = _view.data()] { raw->update(); },
				sizeTag());
			if (_content.badge == BadgeType::BotVerified) {
				_emojiStatus = MakeWrappedEmoji<Ui::Text::FirstFrameEmoji>(
					std::move(_emojiStatus));
			} else if (_customStatusLoopsLimit > 0) {
				_emojiStatus = MakeWrappedEmoji<Ui::Text::LimitedLoopsEmoji>(
					std::move(_emojiStatus),
					_customStatusLoopsLimit);
			}
		}
		// Only the custom badge from the supporter list carries faces here.
		const auto prefix = (_content.badge == BadgeType::ExteraCustom)
			? facesWidth
			: 0;
		const auto width = prefix + emoji + (icon ? icon->width() : 0);
		const auto height = std::max(emoji, icon ? icon->height() : 0);
		_view->resize(width, height);
		_view->paintRequest(
		) | rpl::on_next([=, check = _view.data()]{
			if (prefix) {
				auto p = QPainter(check);
				PaintKomaruFaces(p, faces, 0, check->height());
			}
			if (_emojiStatus) {
				auto args = Ui::Text::CustomEmoji::Context{
					.textColor = style.premiumFg->c,
					.now = crl::now(),
					.position = QPoint(prefix, 0),
					.paused = ((_animationPaused && _animationPaused())
						|| On(PowerSaving::kEmojiStatus)),
				};
				if (!_emojiStatusPanel
					|| !_emojiStatusPanel->paintBadgeFrame(check)) {
					Painter p(check);
					_emojiStatus->paint(p, args);
				}
			}
			if (icon) {
				auto p = Painter(check);
				if (_overrideSt && !iconForeground) {
					icon->paint(
						p,
						prefix + emoji,
						0,
						check->width(),
						_overrideSt->premiumFg->c);
				} else {
					icon->paint(p, prefix + emoji, 0, check->width());
				}
				if (iconForeground) {
					if (_overrideSt) {
						iconForeground->paint(
							p,
							prefix + emoji,
							0,
							check->width(),
							_overrideSt->premiumFg->c);
					} else {
						iconForeground->paint(
							p,
							prefix + emoji,
							0,
							check->width());
					}
				}
			}
		}, _view->lifetime());
	} break;
	case BadgeType::Scam:
	case BadgeType::Fake:
	case BadgeType::Direct: {
		const auto type = (_content.badge == BadgeType::Direct)
			? Ui::TextBadgeType::Direct
			: (_content.badge == BadgeType::Fake)
			? Ui::TextBadgeType::Fake
			: Ui::TextBadgeType::Scam;
		const auto size = Ui::TextBadgeSize(type);
		const auto skip = st::infoVerifiedCheckPosition.x();
		_view->resize(
			size.width() + 2 * skip,
			size.height() + 2 * skip);
		_view->paintRequest(
		) | rpl::on_next([=, badge = _view.data()]{
			Painter p(badge);
			Ui::DrawTextBadge(
				type,
				p,
				badge->rect().marginsRemoved({ skip, skip, skip, skip }),
				badge->width(),
				_overrideSt
					? _overrideSt->premiumFg
					: (type == Ui::TextBadgeType::Direct
						? st::windowSubTextFg
						: st::attentionButtonFg));
			}, _view->lifetime());
	} break;
	case BadgeType::Extera:
	case BadgeType::ExteraSupporter: {
		const auto icon = (_content.badge == BadgeType::Extera
							   ? &st::infoExteraOfficialBadge
							   : &st::infoExteraSupporterBadge);
		const auto skip = st::infoVerifiedCheckPosition.x();
		_view->resize(
			skip + facesWidth + icon->width(),
			icon->height());
		_view->paintRequest(
		) | rpl::on_next([=, check = _view.data()]{
			Painter p(check);
			PaintKomaruFaces(p, faces, skip, check->height());
			const auto left = skip + facesWidth;
			if (_overrideSt) {
				icon->paint(p, left, 0, check->width(), _overrideSt->premiumFg->c);
			} else {
				icon->paint(p, left, 0, check->width());
			}
		}, _view->lifetime());
	} break;
	case BadgeType::Komaru: {
		const auto skip = st::infoVerifiedCheckPosition.x();
		_view->resize(
			skip + facesWidth,
			st::infoExteraSupporterBadge.height());
		_view->paintRequest(
		) | rpl::on_next([=, check = _view.data()]{
			auto p = QPainter(check);
			PaintKomaruFaces(p, faces, skip, check->height());
		}, _view->lifetime());
	} break;
	}

	if (!HasPremiumClick(_content) || !_premiumClickCallback) {
		_view->setAttribute(Qt::WA_TransparentForMouseEvents);
	} else {
		_view->setClickedCallback(_premiumClickCallback);
	}

	_updated.fire({});
}

void Badge::setPremiumClickCallback(Fn<void()> callback) {
	_premiumClickCallback = std::move(callback);
	if (_view && HasPremiumClick(_content)) {
		if (!_premiumClickCallback) {
			_view->setAttribute(Qt::WA_TransparentForMouseEvents);
		} else {
			_view->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			_view->setClickedCallback(_premiumClickCallback);
		}
	}
}

void Badge::setOverrideStyle(const style::InfoPeerBadge *st) {
	const auto was = _content;
	_overrideSt = st;
	_content = {};
	setContent(was);
}

rpl::producer<> Badge::updated() const {
	return _updated.events();
}

void Badge::move(int left, int top, int bottom) {
	if (!_view) {
		return;
	}
	const auto &style = st();
	const auto star = !_emojiStatus
		&& (_content.badge == BadgeType::Premium
			|| _content.badge == BadgeType::Verified);
	const auto fake = !_emojiStatus && !star;
	const auto skip = fake ? 0 : style.position.x();
	const auto badgeLeft = left + skip;
	const auto badgeTop = top
		+ (star
			? style.position.y()
			: (bottom - top - _view->height()) / 2);
	_view->moveToLeft(badgeLeft, badgeTop);
}

const style::InfoPeerBadge &Badge::st() const {
	return _overrideSt ? *_overrideSt : _st;
}

Data::CustomEmojiSizeTag Badge::sizeTag() const {
	using SizeTag = Data::CustomEmojiSizeTag;
	const auto &style = st();
	return (style.sizeTag == 2)
		? SizeTag::Isolated
		: (style.sizeTag == 1)
		? SizeTag::Large
		: SizeTag::Normal;
}

rpl::producer<Badge::Content> BadgeContentForPeer(not_null<PeerData*> peer) {
	const auto statusOnlyForPremium = peer->isUser();
	return rpl::combine(
		BadgeValue(peer),
		EmojiStatusIdValue(peer)
	) | rpl::map([=](BadgeType badge, EmojiStatusId emojiStatusId) {
		if (emojiStatusId.collectible && (badge == BadgeType::Verified)) {
			return Badge::Content{ BadgeType::Premium, emojiStatusId };
		}
		if (badge == BadgeType::Verified) {
			badge = BadgeType::None;
		}
		if (statusOnlyForPremium && badge != BadgeType::Premium) {
			emojiStatusId = EmojiStatusId();
		} else if (emojiStatusId && badge == BadgeType::None) {
			badge = BadgeType::Premium;
		}
		return Badge::Content{ badge, emojiStatusId };
	});
}

rpl::producer<Badge::Content> VerifiedContentForPeer(
		not_null<PeerData*> peer) {
	return BadgeValue(peer) | rpl::map([=](BadgeType badge) {
		if (badge != BadgeType::Verified) {
			badge = BadgeType::None;
		}
		return Badge::Content{ badge };
	});
}

rpl::producer<Badge::Content> BotVerifyBadgeForPeer(
		not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::VerifyInfo
	) | rpl::map([=] {
		const auto info = peer->botVerifyDetails();
		return Badge::Content{
			.badge = info ? BadgeType::BotVerified : BadgeType::None,
			.emojiStatusId = { info ? info->iconId : DocumentId() },
		};
	});
}

} // namespace Info::Profile
