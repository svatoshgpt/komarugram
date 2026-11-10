// This is the source code of KomaruGram for Desktop.
#include "ayu/ui/middle_autoscroll.h"

#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_basic.h"
#include "styles/palette.h"

#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>

#include <cmath>

namespace KomaruAutoscroll {
namespace {

constexpr auto kDeadZone = 15;
constexpr auto kMarkSize = 28;
constexpr auto kMaxSpeed = 20000.;
constexpr auto kMaxStep = crl::time(50);

struct Target {
	QPointer<QWidget> widget;
	Fn<int()> top;
	Fn<int()> max;
	Fn<void(int)> scrollTo;
	Fn<void()> stopSmooth;
};

[[nodiscard]] std::optional<Target> FindTarget(QWidget *widget) {
	for (auto w = widget; w; w = w->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(w)) {
			if (scroll->scrollTopMax() > 0) {
				return Target{
					.widget = scroll,
					.top = [=] { return scroll->scrollTop(); },
					.max = [=] { return scroll->scrollTopMax(); },
					.scrollTo = [=](int y) { scroll->scrollToY(y); },
					.stopSmooth = [=] { scroll->stopSmoothScroll(); },
				};
			}
		} else if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(w)) {
			if (scroll->scrollTopMax() > 0) {
				return Target{
					.widget = scroll,
					.top = [=] { return scroll->scrollTop(); },
					.max = [=] { return scroll->scrollTopMax(); },
					.scrollTo = [=](int y) { scroll->scrollToY(y); },
					.stopSmooth = [=] { scroll->stopSmoothScroll(); },
				};
			}
		}
		if (w->isWindow()) {
			break;
		}
	}
	return std::nullopt;
}

class Mark final : public Ui::RpWidget {
public:
	explicit Mark(QWidget *parent) : RpWidget(parent) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto side = style::ConvertScale(kMarkSize);
		resize(side, side);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto side = width();
		const auto line = style::ConvertScaleExact(1.);
		auto ink = st::windowFg->c;
		auto edge = ink;
		edge.setAlphaF(0.38);
		p.setPen(QPen(edge, line));
		p.setBrush(st::windowBg);
		p.drawEllipse(QRectF(line, line, side - 2 * line, side - 2 * line));
		p.setPen(Qt::NoPen);
		p.setBrush(ink);
		const auto c = side / 2.;
		const auto dot = style::ConvertScaleExact(2.);
		p.drawEllipse(QPointF(c, c), dot, dot);
		const auto half = style::ConvertScaleExact(4.);
		const auto tall = style::ConvertScaleExact(4.);
		const auto gap = style::ConvertScaleExact(5.);
		for (const auto dir : { -1., 1. }) {
			const auto tip = c + dir * (gap + tall);
			const auto base = c + dir * gap;
			const QPointF points[] = {
				{ c, tip },
				{ c - half, base },
				{ c + half, base },
			};
			p.drawPolygon(points, 3);
		}
	}

};

class Autoscroll final : public QObject {
public:
	Autoscroll()
	: QObject(qApp)
	, _animation([=](crl::time now) { return step(now); }) {
	}

	bool eventFilter(QObject *object, QEvent *e) override;

private:
	void start(Target target, QPoint origin);
	void stop();
	bool step(crl::time now);
	[[nodiscard]] float64 distance() const;

	std::optional<Target> _target;
	QPoint _origin;
	bool _held = false;
	bool _dragged = false;
	crl::time _last = 0;
	float64 _carry = 0.;
	Ui::Animations::Basic _animation;
	base::unique_qptr<Mark> _mark;

};

bool Autoscroll::eventFilter(QObject *object, QEvent *e) {
	const auto type = e->type();
	if (_target) {
		switch (type) {
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonDblClick:
			// Any button ends it and does nothing else.
			stop();
			return true;
		case QEvent::MouseButtonRelease:
			if (static_cast<QMouseEvent*>(e)->button() == Qt::MiddleButton
				&& _held) {
				_held = false;
				if (_dragged) {
					stop();
				}
			}
			return true;
		case QEvent::Wheel:
			stop();
			return false;
		case QEvent::KeyPress:
			if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
				stop();
				return true;
			}
			return false;
		case QEvent::ApplicationDeactivate:
			stop();
			return false;
		default:
			return false;
		}
	} else if (type != QEvent::MouseButtonPress) {
		return false;
	}
	const auto mouse = static_cast<QMouseEvent*>(e);
	if (mouse->button() != Qt::MiddleButton
		|| mouse->source() != Qt::MouseEventNotSynthesized
		|| !object->isWidgetType()) {
		return false;
	}
	auto target = FindTarget(static_cast<QWidget*>(object));
	if (!target) {
		return false;
	}
	start(std::move(*target), mouse->globalPosition().toPoint());
	return true;
}

void Autoscroll::start(Target target, QPoint origin) {
	target.stopSmooth();
	_target = std::move(target);
	_origin = origin;
	_held = true;
	_dragged = false;
	_carry = 0.;
	_last = crl::now();
	const auto window = _target->widget->window();
	_mark = base::make_unique_q<Mark>(window);
	_mark->move(window->mapFromGlobal(origin)
		- QPoint(_mark->width() / 2, _mark->height() / 2));
	_mark->show();
	_mark->raise();
	QGuiApplication::setOverrideCursor(Qt::SizeVerCursor);
	_animation.start();
}

void Autoscroll::stop() {
	if (!_target) {
		return;
	}
	_target = std::nullopt;
	_animation.stop();
	_mark = nullptr;
	QGuiApplication::restoreOverrideCursor();
}

float64 Autoscroll::distance() const {
	const auto along = float64(QCursor::pos().y() - _origin.y());
	const auto dead = float64(style::ConvertScale(kDeadZone));
	if (std::abs(along) <= dead) {
		return 0.;
	}
	return (along < 0.) ? (along + dead) : (along - dead);
}

bool Autoscroll::step(crl::time now) {
	if (!_target || !_target->widget) {
		crl::on_main(this, [=] { stop(); });
		return false;
	}
	const auto elapsed = std::clamp(now - _last, crl::time(0), kMaxStep);
	_last = now;
	const auto d = distance();
	if (d != 0. && _held) {
		_dragged = true;
	}
	if (d == 0. || elapsed == 0) {
		return true;
	}
	// Slow near the mark, for reading, and much faster far from it.
	const auto ratio = style::ConvertScaleExact(1.);
	const auto dp = std::abs(d) / ratio;
	const auto speed = std::min((8. * dp + dp * dp / 25.) * ratio, kMaxSpeed);
	_carry += ((d < 0.) ? -speed : speed) * elapsed / 1000.;
	const auto move = int(std::trunc(_carry));
	if (move != 0) {
		_carry -= move;
		const auto top = _target->top();
		_target->scrollTo(std::clamp(top + move, 0, _target->max()));
	}
	return true;
}

} // namespace

void Install() {
	static const auto instance = new Autoscroll();
	qApp->installEventFilter(instance);
}

} // namespace KomaruAutoscroll
