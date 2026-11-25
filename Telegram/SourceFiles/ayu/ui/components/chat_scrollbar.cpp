// This is the source code of KomaruGram for Desktop.
#include "ayu/ui/components/chat_scrollbar.h"

#include "ui/painter.h"
#include "ui/widgets/scroll_area.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QtEvents>
#include <algorithm>
#include <cmath>

namespace KomaruUi {
namespace {

constexpr auto kVirtualFloor = 0.06;
constexpr auto kVirtualCeil = 0.94;
constexpr auto kVirtualThumbRatio = 0.11;

} // namespace

ChatScrollBar::ChatScrollBar(
	not_null<Ui::ScrollArea*> scroll,
	const style::ScrollArea &st)
: RpWidget(scroll)
, _scroll(scroll)
, _st(st)
, _hiding(_st.hiding != 0)
, _hideTimer([=] { hideTimer(); }) {
	setMouseTracking(true);
	hide();

	_scroll->sizeValue(
	) | rpl::on_next([=] {
		recountGeometry();
	}, lifetime());

	recountGeometry();
}

void ChatScrollBar::reset(bool atBottom) {
	_pos = atBottom ? 1.0 : 0.5;
	_thumbRatio = kVirtualThumbRatio;
	_lastScrollTop = _scroll->scrollTop();
	_lastScrollMax = _scroll->scrollTopMax();
	_lastViewport = _scroll->height();
	_loadedAtTop = false;
	_loadedAtBottom = atBottom;
	_hasScroll = (_lastScrollMax > 0);
	_moving = false;
	updateThumbRect();
	update();
}

void ChatScrollBar::setTopSkip(int skip) {
	if (_topSkip == skip) {
		return;
	}
	_topSkip = skip;
	recountGeometry();
}

void ChatScrollBar::setBottomSkip(int skip) {
	if (_bottomSkip == skip) {
		return;
	}
	_bottomSkip = skip;
	recountGeometry();
}

void ChatScrollBar::recountGeometry() {
	const auto areaWidth = _scroll->width();
	const auto areaHeight = _scroll->height();
	const auto barWidth = _st.width;
	const auto x = style::RightToLeft() ? 0 : (areaWidth - barWidth);
	const auto y = _st.deltat + _topSkip;
	const auto h = std::max(
		areaHeight - _st.deltat - _st.deltab - _topSkip - _bottomSkip,
		0);
	setGeometry(x, y, barWidth, h);
	raise();
	updateThumbRect();
}

void ChatScrollBar::onScroll(
		int scrollTop,
		int scrollTopMax,
		int viewportHeight,
		bool loadedAtTop,
		bool loadedAtBottom,
		bool synthetic) {
	const auto prevScrollTop = _lastScrollTop;
	const auto prevScrollMax = _lastScrollMax;
	_lastScrollTop = scrollTop;
	_lastScrollMax = scrollTopMax;
	_lastViewport = viewportHeight;
	_loadedAtTop = loadedAtTop;
	_loadedAtBottom = loadedAtBottom;

	const auto scrollable = (scrollTopMax > 0)
		|| !loadedAtTop
		|| !loadedAtBottom;
	_hasScroll = scrollable;
	if (!scrollable) {
		hide();
		return;
	}

	const auto fullyLoaded = loadedAtTop && loadedAtBottom;
	if (fullyLoaded) {
		_pos = (scrollTopMax > 0)
			? std::clamp(double(scrollTop) / double(scrollTopMax), 0.0, 1.0)
			: 1.0;
		const auto total = double(viewportHeight + scrollTopMax);
		_thumbRatio = (total > 0.0)
			? std::clamp(double(viewportHeight) / total, 0.06, 1.0)
			: 1.0;
	} else {
		_thumbRatio = kVirtualThumbRatio;
		if (synthetic) {
			if (loadedAtTop && scrollTop <= 0) {
				_pos = 0.0;
			} else if (loadedAtBottom && scrollTop >= scrollTopMax) {
				_pos = 1.0;
			}
			// Otherwise keep _pos untouched so slice loads/trims never jump the thumb.
			updateThumbRect();
			update();
			return;
		}
		const auto dy = scrollTop - prevScrollTop;
		if (loadedAtTop && scrollTop <= 0) {
			_pos = 0.0;
		} else if (loadedAtBottom && scrollTop >= scrollTopMax) {
			_pos = 1.0;
		} else if (dy < 0) {
			if (loadedAtTop && prevScrollTop > 0) {
				const auto ratio = std::clamp(
					double(scrollTop) / double(prevScrollTop),
					0.0,
					1.0);
				_pos *= ratio;
			} else {
				const auto step = double(-dy)
					/ std::max(double(viewportHeight) * 10.0, 1.0);
				_pos = kVirtualFloor
					+ (_pos - kVirtualFloor) * std::exp(-step);
			}
		} else if (dy > 0) {
			const auto rem = std::max(0, scrollTopMax - scrollTop);
			const auto prevRem = std::max(1, prevScrollMax - prevScrollTop);
			if (loadedAtBottom && prevScrollMax == scrollTopMax) {
				const auto ratio = std::clamp(
					double(rem) / double(prevRem),
					0.0,
					1.0);
				_pos = 1.0 - (1.0 - _pos) * ratio;
			} else {
				const auto step = double(dy)
					/ std::max(double(viewportHeight) * 10.0, 1.0);
				_pos = kVirtualCeil
					- (kVirtualCeil - _pos) * std::exp(-step);
			}
		}
		_pos = std::clamp(_pos, 0.0, 1.0);
	}

	updateThumbRect();
	if (!synthetic || _moving || _over) {
		showAndScheduleHide();
	}
	update();
}

void ChatScrollBar::updateThumbRect() {
	const auto h = height();
	if (h <= 0 || !_hasScroll) {
		_bar = QRect();
		return;
	}
	const auto minH = std::max(_st.minHeight, style::ConvertScale(44));
	const auto maxH = std::max(minH, h);
	const auto thumbH = std::clamp(
		int(std::lround(h * _thumbRatio)),
		minH,
		maxH);
	const auto travel = std::max(0, h - thumbH);
	const auto top = int(std::lround(travel * std::clamp(_pos, 0.0, 1.0)));
	_bar = QRect(_st.deltax, top, width() - 2 * _st.deltax, thumbH);
}

void ChatScrollBar::showAndScheduleHide() {
	if (!_hasScroll) {
		return;
	}
	if (isHidden()) {
		show();
		raise();
	}
	if (_hiding) {
		_hiding = false;
		_a_opacity.start(
			[=] { update(); },
			_a_opacity.value(0.0),
			1.0,
			_st.duration);
	}
	if (_st.hiding > 0 && !_over && !_moving) {
		_hideTimer.callOnce(_st.hiding);
	} else {
		_hideTimer.cancel();
	}
}

void ChatScrollBar::hideTimer() {
	if (_over || _moving || _st.hiding <= 0) {
		return;
	}
	_hiding = true;
	_a_opacity.start(
		[=] {
			update();
			if (!_a_opacity.animating() && _hiding) {
				hide();
			}
		},
		_a_opacity.value(1.0),
		0.0,
		_st.duration);
}

void ChatScrollBar::setOver(bool over) {
	if (_over == over) {
		return;
	}
	_over = over;
	_a_over.start(
		[=] { update(); },
		_over ? 0.0 : 1.0,
		_over ? 1.0 : 0.0,
		_st.duration);
	if (_over) {
		showAndScheduleHide();
	} else if (_st.hiding > 0 && !_moving) {
		_hideTimer.callOnce(_st.hiding);
	}
}

void ChatScrollBar::setOverBar(bool overBar) {
	if (_overBar == overBar) {
		return;
	}
	_overBar = overBar;
	_a_barOver.start(
		[=] { update(); },
		_overBar ? 0.0 : 1.0,
		_overBar ? 1.0 : 0.0,
		_st.duration);
}

void ChatScrollBar::setMoving(bool moving) {
	if (_moving == moving) {
		return;
	}
	_moving = moving;
	_scroll->setMovingByScrollBar(moving);
	if (!_moving && !_over && _st.hiding > 0) {
		_hideTimer.callOnce(_st.hiding);
	}
}

void ChatScrollBar::paintEvent(QPaintEvent *e) {
	if (_bar.isEmpty()) {
		return;
	}
	const auto opacity = _a_opacity.value(_hiding ? 0.0 : 1.0);
	if (opacity <= 0.0) {
		return;
	}
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(opacity);

	const auto over = _a_over.value(_over ? 1.0 : 0.0);
	const auto barOver = _a_barOver.value((_overBar || _moving) ? 1.0 : 0.0);

	// Track background on hover.
	if (over > 0.0) {
		auto bg = anim::color(_st.bg, _st.bgOver, over);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		const auto trackRect = QRect(
			_st.deltax,
			0,
			width() - 2 * _st.deltax,
			height());
		p.drawRoundedRect(trackRect, _st.round, _st.round);
	}

	// Thumb pill.
	auto barColor = anim::color(_st.barBg, _st.barBgOver, barOver);
	p.setPen(Qt::NoPen);
	p.setBrush(barColor);
	p.drawRoundedRect(_bar, _st.round, _st.round);
}

void ChatScrollBar::enterEventHook(QEnterEvent *e) {
	setOver(true);
}

void ChatScrollBar::leaveEventHook(QEvent *e) {
	if (!_moving) {
		setOver(false);
		setOverBar(false);
	}
}

void ChatScrollBar::mouseMoveEvent(QMouseEvent *e) {
	const auto pos = e->pos();
	if (!_moving) {
		setOverBar(_bar.contains(pos));
		return;
	}
	const auto travel = std::max(1, height() - _bar.height());
	const auto fullyLoaded = _loadedAtTop && _loadedAtBottom;
	if (fullyLoaded && _lastScrollMax > 0) {
		const auto deltaFrac = double(pos.y() - _dragStartY) / double(travel);
		const auto targetPos = std::clamp(_dragStartPos + deltaFrac, 0.0, 1.0);
		_pos = targetPos;
		updateThumbRect();
		update();
		_scroll->scrollToY(int(std::lround(targetPos * _lastScrollMax)));
	} else if (_lastScrollMax > 0) {
		const auto dy = pos.y() - _dragLastY;
		_dragLastY = pos.y();
		if (dy != 0) {
			const auto deltaFrac = double(dy) / double(travel);
			_pos = std::clamp(_pos + deltaFrac, 0.0, 1.0);
			const auto pixelScale = std::max(
				double(_lastScrollMax) / double(travel),
				double(std::max(_lastViewport, 400)) / 40.0);
			const auto targetY = std::clamp(
				_scroll->scrollTop() + int(std::lround(dy * pixelScale)),
				0,
				_lastScrollMax);
			updateThumbRect();
			update();
			_scroll->scrollToY(targetY);
		}
	}
}

void ChatScrollBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto pos = e->pos();
	if (_bar.contains(pos)) {
		setMoving(true);
		setOverBar(true);
		_dragStartY = pos.y();
		_dragLastY = pos.y();
		_dragStartPos = _pos;
	} else {
		const auto page = std::max(100, int(_lastViewport * 0.85));
		const auto dir = (pos.y() < _bar.center().y()) ? -1 : 1;
		_scroll->scrollToY(std::clamp(
			_scroll->scrollTop() + dir * page,
			0,
			std::max(0, _lastScrollMax)));
	}
}

void ChatScrollBar::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton || !_moving) {
		return;
	}
	setMoving(false);
	if (!rect().contains(e->pos())) {
		setOver(false);
		setOverBar(false);
	}
}

void ChatScrollBar::wheelEvent(QWheelEvent *e) {
	QCoreApplication::sendEvent(_scroll->viewport(), e);
}

} // namespace KomaruUi
