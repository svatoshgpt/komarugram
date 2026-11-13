// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/scroll_autoload.h"

#include "base/timer.h"

namespace KomaruAutoload {
namespace {

// A screen or more within this time counts as scrolling fast.
constexpr auto kFastWindow = crl::time(120);
constexpr auto kResumeAfter = crl::time(250);

struct State {
	bool paused = false;
	crl::time windowStart = 0;
	int windowDelta = 0;
	Fn<void()> resumed;
	base::Timer timer;
};

[[nodiscard]] State &Instance() {
	// Never destroyed: its timer must not outlive the application.
	static const auto result = new State();
	return *result;
}

void Resume() {
	auto &state = Instance();
	state.paused = false;
	if (const auto callback = base::take(state.resumed)) {
		callback();
	}
}

} // namespace

bool Paused() {
	return Instance().paused;
}

void Scrolled(int delta, int viewHeight, Fn<void()> resumed) {
	auto &state = Instance();
	const auto now = crl::now();
	if (now - state.windowStart > kFastWindow) {
		state.windowStart = now;
		state.windowDelta = 0;
	}
	state.windowDelta += std::abs(delta);
	if (viewHeight > 0 && state.windowDelta >= viewHeight) {
		state.paused = true;
		state.resumed = std::move(resumed);
		state.timer.setCallback(Resume);
	}
	if (state.paused) {
		state.timer.callOnce(kResumeAfter);
	}
}

} // namespace KomaruAutoload
