<div align="center">
<img src="./docs/assets/logo.svg" width="140" align="center" alt="KomaruGram">

# KomaruGram

Telegram Desktop fork — **[materialgram](https://github.com/kukuruzka165/materialgram)** design on an **[AyuGram](https://github.com/AyuGram/AyuGramDesktop)** feature base.

[ English | [Русский](README-RU.md) ]

</div>

## What is this

KomaruGram merges two well-known [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) forks
and is currently based on Telegram Desktop **7.2.9**.

- **Feature base — AyuGram:** ghost mode, deleted and edited message history, streamer mode,
  local Telegram Premium, translator, font customization, ID copying, extended appearance.
- **Design layer — materialgram:** Material colors and icons, reduced uppercase, snappier
  timeouts, faster uploads, higher media quality, more recent stickers, larger account limits.

Crash reports stay on your machine: after a crash KomaruGram offers to open a prefilled
GitHub issue, and you decide what to send. Nothing is uploaded automatically.

## Download

Get the latest build from [Releases](https://github.com/svatoshgpt/komarugram/releases/latest):

| File | Platform |
| --- | --- |
| `KomaruGram-<version>-windows-x64.zip` | Windows 10/11, 64-bit |
| `KomaruGram-<version>-windows-x86.zip` | Windows, 32-bit |
| `KomaruGram-<version>.tar.zst` | Linux x64 |

On Windows, unpack the whole zip and keep `Updater.exe` next to `KomaruGram.exe` —
the client needs it to install updates. Updates are checked every 15–30 minutes and can
be checked by hand in Settings → Advanced. The Linux build does not update itself yet;
download new releases manually.

News: [@komarugraminfo](https://t.me/komarugraminfo) · Chat: [@komarugram](https://t.me/komarugram)

## Reporting bugs

Open an issue at <https://github.com/svatoshgpt/komarugram/issues>. After a crash the
client offers to open one with the report already filled in; please add what you were
doing when it happened.

## Building

KomaruGram builds like Telegram Desktop. Follow the guides in `docs/`:

- [Windows](docs/building-win-x64.md)
- [Linux](docs/building-linux.md)

You need your own Telegram API credentials (`api_id` / `api_hash`) — see
<https://core.telegram.org/api/obtaining_api_id>. Pass them to CMake as
`-D TDESKTOP_API_ID=…` and `-D TDESKTOP_API_HASH=…`.

### CI

Pushing a tag builds Windows x64, Windows x86 and Linux x64 and publishes them as one
release. The workflows need these repository secrets:

- `TDESKTOP_API_ID`, `TDESKTOP_API_HASH` — Telegram API credentials.
- `UPDATE_PRIVATE_KEY` — RSA-2048 private key that signs Windows update packages.
  The matching public key is in `Telegram/SourceFiles/config.h`. Keep the private key
  safe: without it, installed clients cannot receive updates.

The release version comes from `Telegram/SourceFiles/core/version.h`. KomaruGram
numbers its releases on top of the Telegram Desktop base as `patch * 10 + fix`:
7.2.9-k1.2 is `7002092`. The number must grow with every release, or installed
clients will not see the update.

## Support the project

Gram (TON): `UQAirl_g-9BqSSTGqcX0LHGEyhN_ewZ0ucNakXGrw-h9tMY4`

The same address is in the app under Settings → KomaruGram Preferences → Other.

## Credits

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) by Telegram Messenger
- [AyuGram](https://github.com/AyuGram/AyuGramDesktop) by Radolyn Labs
- [materialgram](https://github.com/kukuruzka165/materialgram) by kukuruzka165
- Bundled fonts keep their own licences (SIL OFL 1.1, Apache 2.0) — see
  [Telegram/Resources/fonts/LICENSE.md](Telegram/Resources/fonts/LICENSE.md).

## License

GPLv3 with OpenSSL exception, inherited from Telegram Desktop. See [LICENSE](LICENSE)
and [LEGAL](LEGAL).
