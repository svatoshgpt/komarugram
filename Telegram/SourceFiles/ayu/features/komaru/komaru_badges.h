// This is the source code of KomaruGram for Desktop.
//
// Badges for KomaruGram developers, supporters and partners. The lists live
// in a gist: badges.txt for developers, donates.txt for supporters and
// partner.txt for partners from KomaruGif, one person per
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
	Partner,
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
// Supporters and partners share the supporter face; Supporter() returns
// either. IsPartner() picks the popup text and wins when someone is in both
// lists, so partners can stay in donates.txt for clients that predate
// partner.txt.
[[nodiscard]] const Entry *Supporter(uint64 peerId);
[[nodiscard]] bool IsPartner(uint64 peerId);

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
