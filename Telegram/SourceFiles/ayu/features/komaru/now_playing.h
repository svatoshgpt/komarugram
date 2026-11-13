// This file is part of KomaruGram for Desktop, see https://github.com/svatoshgpt/komarugram
#pragma once

#include <rpl/producer.h>

enum class NowPlayingService;

namespace KomaruNowPlaying {

struct Track {
	QString title;
	QString artist;
	QString url;
	QString cover;
	int duration = 0;

	friend inline bool operator==(const Track &, const Track &) = default;
};

void Start();

// Clears the profile music and bio before the app quits; true while that is in flight.
[[nodiscard]] bool IsQuitPrevent();

[[nodiscard]] std::optional<Track> Current();
[[nodiscard]] rpl::producer<std::optional<Track>> Value();
[[nodiscard]] rpl::producer<QImage> CoverValue();

[[nodiscard]] QString SiteUrl(NowPlayingService service);

inline constexpr auto kUsernameLimit = 40;

}
