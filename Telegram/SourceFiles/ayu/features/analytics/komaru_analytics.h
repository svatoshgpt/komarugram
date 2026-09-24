// This is the source code of KomaruGram for Desktop.
//
// Anonymous usage statistics sent to Google Analytics 4 through the
// Measurement Protocol. Only updates and crashes are reported, tagged with
// a random per-install id that is not tied to any account.
#pragma once

namespace KomaruAnalytics {

// Whether this build was compiled with Measurement Protocol credentials.
[[nodiscard]] bool Available();

// Reports an update if the version grew since the last launch.
void TrackUpdate();

// Reports that the previous run crashed. Reads only the version and the
// kind of crash from the report; nothing else in it leaves the machine.
void TrackCrash(const QByteArray &report);

} // namespace KomaruAnalytics
