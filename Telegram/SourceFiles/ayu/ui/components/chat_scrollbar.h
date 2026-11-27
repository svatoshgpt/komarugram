// This is the source code of KomaruGram for Desktop.
#pragma once

#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/widgets/elastic_scroll.h"
#include "styles/style_widgets.h"

namespace KomaruUi {

// Smooth virtual scrollbar for HistoryWidget that does not jump or resize
// wildly when Telegram's virtualized message slices prepend, append, or trim.
class ChatScrollBar final : public Ui::RpWidget {
public:
	ChatScrollBar(
		Ui::ElasticScroll *scroll,
		const style::ScrollArea &st);

	void reset(bool atBottom = true);
	void onScroll(
		int scrollTop,
		int scrollTopMax,
		int viewportHeight,
		bool loadedAtTop,
		bool loadedAtBottom,
		bool synthetic);

	void setTopSkip(int skip);
	void setBottomSkip(int skip);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

private:
	void recountGeometry();
	void updateThumbRect();
	void showAndScheduleHide();
	void setOver(bool over);
	void setOverBar(bool overBar);
	void setMoving(bool moving);
	void hideTimer();

	const not_null<Ui::ElasticScroll*> _scroll;
	const style::ScrollArea &_st;

	int _topSkip = 0;
	int _bottomSkip = 0;

	double _pos = 1.0;
	double _thumbRatio = 0.12;
	int _lastScrollTop = 0;
	int _lastScrollMax = 0;
	int _lastViewport = 0;
	bool _loadedAtTop = false;
	bool _loadedAtBottom = true;
	bool _hasScroll = false;

	bool _hiding = true;
	bool _over = false;
	bool _overBar = false;
	bool _moving = false;
	int _dragStartY = 0;
	int _dragLastY = 0;
	double _dragStartPos = 1.0;

	QRect _bar;
	base::Timer _hideTimer;
	Ui::Animations::Simple _a_over;
	Ui::Animations::Simple _a_barOver;
	Ui::Animations::Simple _a_opacity;
};

} // namespace KomaruUi
