// This file is part of KomaruGram for Desktop, see https://github.com/svatoshgpt/komarugram
#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
}

namespace Settings {

class AyuNowPlaying : public Section<AyuNowPlaying> {
public:
	AyuNowPlaying(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

[[nodiscard]] Type AyuNowPlayingId();

}
