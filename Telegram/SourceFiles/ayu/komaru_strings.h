// This is the source code of KomaruGram for Desktop.
#pragma once

namespace Komaru {

// KomaruGram strings live in komaru_strings.cpp, not lang.strings: a new lang.strings key rebuilds most of the client.
[[nodiscard]] QString Text(const char *key);
[[nodiscard]] rpl::producer<QString> TextValue(const char *key);

} // namespace Komaru
