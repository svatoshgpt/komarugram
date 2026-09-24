// This is the source code of KomaruGram for Desktop.
//
// Badges for KomaruGram developers and supporters. Both lists live in a gist:
// badges.txt for developers and donates.txt for supporters, one person per
// line as "id,#particles,#face,alpha", the order the mobile client reads
// them in. The Komaru face (and a developer's rosette) is drawn in the
// second colour with animated particles around it in the first, and the
// alpha applies to both. Channels are listed by their bare id, without -100.
#pragma once

#include <crl/crl_time.h>
#include <rpl/producer.h>

namespace KomaruBadges {

enum class Kind : uchar {
	Developer,
	Supporter,
};

struct Entry {
	Kind kind = Kind::Supporter;
	QColor face;
	QColor particles;
};

// The donation that earns a supporter badge, in US dollars.
inline constexpr auto kDonateUsd = 2.5;

// How often an animated badge asks to be repainted.
inline constexpr auto kFrameDelay = crl::time(50);

// Loads both lists and the Gram rate, then refreshes them every 10 minutes.
void Start();

// A person can be in both lists and then shows both faces.
[[nodiscard]] const Entry *Developer(uint64 peerId);
[[nodiscard]] const Entry *Supporter(uint64 peerId);

// Fires when a list or the rate changes.
[[nodiscard]] rpl::producer<> Updated();

// Gram needed for kDonateUsd at the latest rate, like "1.77", rounded up.
// Empty until the first rate arrives.
[[nodiscard]] QString DonateGramAmount();

// Whether the particles move; they stand still in power saving mode.
[[nodiscard]] bool Animated();

// Paints the face and its particles into `rect`, which should be square.
void Paint(QPainter &p, QRect rect, const Entry &entry, crl::time now);

} // namespace KomaruBadges
