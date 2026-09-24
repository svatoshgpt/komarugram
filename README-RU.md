<div align="center">
<img src="./docs/assets/logo.svg" width="140" align="center" alt="KomaruGram">

# KomaruGram

Форк Telegram Desktop — дизайн **[materialgram](https://github.com/kukuruzka165/materialgram)** поверх функций **[AyuGram](https://github.com/AyuGram/AyuGramDesktop)**.

[ [English](README.md) | Русский ]

</div>

## Что это

KomaruGram объединяет два известных форка [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).
Сейчас он основан на Telegram Desktop **7.2.9**.

- **Функции — AyuGram:** режим призрака, история удалённых и изменённых сообщений, режим
  стримера, локальный Telegram Premium, переводчик, настройка шрифтов, копирование ID,
  расширенные настройки внешнего вида.
- **Дизайн — materialgram:** цвета и иконки Material, меньше КАПСА, более отзывчивый
  интерфейс, быстрая загрузка файлов, выше качество медиа, больше недавних стикеров,
  больше аккаунтов.

После краша KomaruGram предлагает открыть заполненный issue на GitHub, а что отправить,
решаете вы; сам отчёт и дамп памяти никуда не загружаются.

KomaruGram отправляет анонимную статистику в Google Analytics: обновления и факт сбоя
с версией, в которой он произошёл. К ним прикладывается случайный ID, не связанный с
аккаунтом; сообщения, контакты, номер телефона и юзернеймы не отправляются никогда.
Отключить: «Настройки → Настройки KomaruGram → Другое → Анонимная статистика».

## Скачать

Последняя версия — на странице [Releases](https://github.com/svatoshgpt/komarugram/releases/latest):

| Файл | Платформа |
| --- | --- |
| `KomaruGram-<версия>-windows-x64.zip` | Windows 10/11, 64 бит |
| `KomaruGram-<версия>-windows-x86.zip` | Windows, 32 бит |
| `KomaruGram-<версия>.tar.zst` | Linux x64 |

На Windows распакуйте архив целиком: `Updater.exe` должен лежать рядом с
`KomaruGram.exe`, без него клиент не сможет установить обновление. Обновления
проверяются каждые 15–30 минут, вручную — в «Настройки → Продвинутые». Версия для
Linux пока не обновляется сама, новые релизы нужно скачивать вручную.

Новости: [@komarugraminfo](https://t.me/komarugraminfo) · Чат: [@komarugram](https://t.me/komarugram)

## Сообщить об ошибке

Создайте issue на <https://github.com/svatoshgpt/komarugram/issues>. После краша клиент
сам предложит открыть его с уже заполненным отчётом — допишите, что вы делали в этот
момент.

## Сборка

KomaruGram собирается так же, как Telegram Desktop. Инструкции в `docs/`:

- [Windows](docs/building-win-x64.md)
- [Linux](docs/building-linux.md)

Понадобятся собственные ключи Telegram API (`api_id` / `api_hash`) — см.
<https://core.telegram.org/api/obtaining_api_id>. Передайте их в CMake как
`-D TDESKTOP_API_ID=…` и `-D TDESKTOP_API_HASH=…`.

### CI

Пуш тега собирает Windows x64, Windows x86 и Linux x64 и публикует их одним релизом.
В репозитории должны быть секреты:

- `TDESKTOP_API_ID`, `TDESKTOP_API_HASH` — ключи Telegram API.
- `GA_MEASUREMENT_ID`, `GA_API_SECRET` — ID потока и секрет Measurement Protocol
  Google Analytics 4 для анонимной статистики. Без них сборка выйдет без статистики.
- `UPDATE_PRIVATE_KEY` — приватный ключ RSA-2048 для подписи обновлений Windows.
  Публичный ключ лежит в `Telegram/SourceFiles/config.h`. Храните приватный ключ
  надёжно: без него установленные клиенты не смогут получать обновления.

Версия релиза берётся из `Telegram/SourceFiles/core/version.h`. KomaruGram нумерует
свои релизы поверх базы Telegram Desktop: номер базы, умноженный на десять, плюс
счётчик релизов, поэтому 7.2.9-k1.2 — это `7002092`, а 7.2.9-k1.3.1 — `7002094`. Каждый
релиз берёт следующий номер, иначе установленные клиенты не увидят обновление.

## Поддержать проект

Gram (TON): `UQAirl_g-9BqSSTGqcX0LHGEyhN_ewZ0ucNakXGrw-h9tMY4`

Этот же адрес есть в приложении: «Настройки → Настройки KomaruGram → Другое».

## Благодарности

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) — Telegram Messenger
- [AyuGram](https://github.com/AyuGram/AyuGramDesktop) — Radolyn Labs
- [materialgram](https://github.com/kukuruzka165/materialgram) — kukuruzka165
- Шрифты распространяются под своими лицензиями (SIL OFL 1.1, Apache 2.0) — см.
  [Telegram/Resources/fonts/LICENSE.md](Telegram/Resources/fonts/LICENSE.md).

## Лицензия

GPLv3 с исключением для OpenSSL, унаследована от Telegram Desktop. См. [LICENSE](LICENSE)
и [LEGAL](LEGAL).
