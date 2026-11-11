// This is the source code of KomaruGram for Desktop.
#include "ayu/komaru_strings.h"

#include "lang/lang_keys.h"

namespace Komaru {
namespace {

struct Entry {
	const char *key = nullptr;
	const char16_t *en = nullptr;
	const char16_t *ru = nullptr;
};

// Add new strings here: { key, English, Russian }. Placeholders such as {name} are replaced by the caller.
constexpr Entry kStrings[] = {
	{ "translate_subtitle", u"Translate Messages", u"Перевод сообщений" },
};

} // namespace

QString Text(const char *key) {
	const auto ru = Lang::Id().startsWith(u"ru"_q);
	for (const auto &entry : kStrings) {
		if (!qstrcmp(entry.key, key)) {
			return QString::fromUtf16(ru ? entry.ru : entry.en);
		}
	}
	return QString::fromLatin1(key);
}

rpl::producer<QString> TextValue(const char *key) {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::Updated()
	) | rpl::map([=] {
		return Text(key);
	});
}

} // namespace Komaru
