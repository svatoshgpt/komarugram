// This is the source code of KomaruGram for Desktop.
#pragma once

namespace KomaruCryptoRates {

// "1 грам =" gives "81,85 ₽"; rates load in the background.
[[nodiscard]] QString Completion(const QString &text);
[[nodiscard]] rpl::producer<> Updated();

} // namespace KomaruCryptoRates
