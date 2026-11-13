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
	{ "messages_count", u"Messages", u"Сообщений" },
	{ "badge_title", u"Your badge", u"Ваш значок" },
	{ "badge_hint", u"Click the badge to choose any emoji.", u"Нажмите на значок, чтобы выбрать любой эмодзи." },
	{ "badge_caption", u"Caption", u"Подпись" },
	{ "badge_about", u"Everyone who clicks your badge sees the caption. {name} becomes your name, **bold** and [links](url) work.", u"Подпись видят все, кто нажмёт на ваш значок. {name} заменяется вашим именем, работают **жирный** и [ссылки](url)." },
	{ "badge_reset", u"Reset badge", u"Сбросить значок" },
	{ "badge_saved", u"Saved. Others will see it in a few minutes.", u"Сохранено. У других обновится через несколько минут." },
	{ "badge_reset_done", u"Badge reset.", u"Значок сброшен." },
	{ "hide_paid_stickers", u"Hide Premium stickers and emoji", u"Скрыть Premium-стикеры и эмодзи" },
	{ "save_as_gif", u"Save as GIF", u"Сохранить в GIF" },
	{ "save_sticker_video", u"Save as video", u"Сохранить как видео" },
	{ "save_gif_download_first", u"Download it first.", u"Сначала скачайте файл." },
	{ "save_gif_preparing", u"Converting…", u"Конвертирую…" },
	{ "save_gif_failed", u"Could not convert.", u"Не удалось конвертировать." },
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
