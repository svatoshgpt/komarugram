// This is the source code of KomaruGram for Desktop.
#pragma once

namespace KomaruExteraNowPlaying {

struct Track {
	QString title;
	QString artist;
	QString url;
	QString cover;

	[[nodiscard]] bool empty() const {
		return title.isEmpty();
	}
	friend inline bool operator==(const Track &, const Track &) = default;
};

// What the exteraGram user plays right now, empty once they stop.
[[nodiscard]] rpl::producer<Track> Value(uint64 userId);

// The album art from iTunes, so no cover a music bot drew its ads on.
[[nodiscard]] rpl::producer<QImage> CoverValue(
	const QString &title,
	const QString &artist,
	const QString &known = QString());

} // namespace KomaruExteraNowPlaying
