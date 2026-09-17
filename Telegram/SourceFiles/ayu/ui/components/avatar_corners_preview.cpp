// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/avatar_corners_preview.h"

#include "ayu/ui/ayu_userpic.h"

#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/effects/ripple_animation.h"
#include "window/window_session_controller.h"

#include <QSvgRenderer>
#include <QPainterPath>

namespace {

constexpr auto kPreviewName = "KomaruGram";
constexpr auto kPreviewText = "Better late than never";

} // namespace

AvatarCornersPreview::AvatarCornersPreview(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _emptyUserpic(
	Ui::EmptyUserpic::UserpicColor(
		Data::DecideColorIndex(
			peerFromChannel(ChannelId(2331068091)))),
	QString::fromUtf8(kPreviewName))
, _logo(std::make_unique<QSvgRenderer>(u":/gui/icons/komarugram.svg"_q)) {
	const auto &row = st::defaultDialogRow;
	setFixedHeight(row.height);
	setCursor(Qt::PointingHandCursor);
}

AvatarCornersPreview::~AvatarCornersPreview() = default;

void AvatarCornersPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto &row = st::defaultDialogRow;
	const auto photoSize = row.photoSize;
	const auto xShift = st::settingsButtonNoIcon.padding.left()
		- row.padding.left();
	const auto userpicX = row.padding.left() + xShift;
	const auto userpicY = (height() - photoSize) / 2;

	p.fillRect(rect(), st::windowBg);

	if (_ripple) {
		_ripple->paint(p, 0, 0, width());
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}

	// Mirror the avatar corners setting this preview exists to demonstrate.
	if (_logo && _logo->isValid()) {
		const auto target = QRectF(userpicX, userpicY, photoSize, photoSize);
		auto shape = QPainterPath();
		if (AyuUserpic::IsCircle()) {
			shape.addEllipse(target);
		} else if (const auto r = AyuUserpic::ComputeRadiusF(photoSize)
			; r > 0.) {
			shape.addRoundedRect(target, r, r);
		} else {
			shape.addRect(target);
		}
		auto hq = PainterHighQualityEnabler(p);
		p.save();
		p.setClipPath(shape);
		_logo->render(&p, target);
		p.restore();
	} else if (AyuUserpic::IsCircle()) {
		_emptyUserpic.paintCircle(p, userpicX, userpicY, width(), photoSize);
	} else if (const auto radius = AyuUserpic::ComputeRadius(photoSize)) {
		_emptyUserpic.paintRounded(
			p,
			userpicX,
			userpicY,
			width(),
			photoSize,
			radius);
	} else {
		_emptyUserpic.paintSquare(p, userpicX, userpicY, width(), photoSize);
	}

	const auto nameText = QString::fromUtf8(kPreviewName);
	p.setPen(st::dialogsNameFg);
	p.setFont(st::semiboldFont);
	p.drawText(row.nameLeft + xShift, row.nameTop + st::semiboldFont->ascent, nameText);

	const auto nameWidth = st::semiboldFont->width(nameText);
	const auto &badge = st::dialogsExteraOfficialIcon.icon;
	badge.paint(p, row.nameLeft + xShift + nameWidth, row.nameTop, width());

	p.setPen(st::dialogsTextFg);
	p.setFont(st::dialogsTextFont);
	p.drawText(
		row.textLeft + xShift,
		row.textTop + st::dialogsTextFont->ascent,
		QString::fromUtf8(kPreviewText));
}

void AvatarCornersPreview::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!_ripple) {
			auto mask = Ui::RippleAnimation::RectMask(size());
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { update(); });
		}
		_ripple->add(e->pos());
	}
}

void AvatarCornersPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (_ripple) {
		_ripple->lastStop();
	}
}
