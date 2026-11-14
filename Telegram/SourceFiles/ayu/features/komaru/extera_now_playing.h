// This is the source code of KomaruGram for Desktop.
#pragma once

namespace KomaruExteraNowPlaying {

struct Track {
	QString title;
	QString artist;
	QString url;
	bool playing = false;

	[[nodiscard]] bool empty() const {
		return title.isEmpty();
	}
	friend inline bool operator==(const Track &, const Track &) = default;
};

// What the exteraGram user listens to, empty for anyone without Last.fm there.
[[nodiscard]] rpl::producer<Track> Value(uint64 userId);

} // namespace KomaruExteraNowPlaying
