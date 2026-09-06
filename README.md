<div align="center">
<img src="./docs/assets/logo.svg" width="140" align="center" alt="KomaruGram">

# KomaruGram

Telegram Desktop fork — **[materialgram](https://github.com/kukuruzka165/materialgram)** design on an **[AyuGram](https://github.com/AyuGram/AyuGramDesktop)** feature base.

</div>

## What is this

KomaruGram merges two well-known [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) forks:

- **Feature base — AyuGram:** Ghost mode, message history / anti-recall, streamer mode,
  local Telegram Premium, translator, font customization, ID copying, extended appearance.
- **Design layer — materialgram:** Material colors and icons, reduced uppercase, snappier
  timeouts, faster uploads, higher media quality, more recent stickers, larger account limits.

The result is branded as KomaruGram, with the Komaru cat as the app icon.

## Status

Early. The merge is done and the tree is consistent, but the project has not yet had a
full verified build — expect rough edges and compile fixes in the first releases.

## Building

KomaruGram builds exactly like Telegram Desktop / AyuGram. Follow the upstream guides:

- [Windows](docs/building-win-x64.md)
- [macOS](docs/building-mac.md)
- [Linux](docs/building-linux.md)

You must provide your own Telegram API credentials (`api_id` / `api_hash`) —
see <https://core.telegram.org/api/obtaining_api_id>. Pass them to CMake as
`-D TDESKTOP_API_ID=…` and `-D TDESKTOP_API_HASH=…`.

### CI

`.github/workflows/` builds Windows, Linux and macOS artifacts when you push a tag.
Add repository secrets `API_ID` and `API_HASH` first. Auto-update is disabled —
releases come from the GitHub Releases page only.

## Credits

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) by Telegram Messenger
- [AyuGram](https://github.com/AyuGram/AyuGramDesktop) by Radolyn Labs
- [materialgram](https://github.com/kukuruzka165/materialgram) by kukuruzka165

## License

GPLv3 with OpenSSL exception, inherited from Telegram Desktop. See [LICENSE](LICENSE)
and [LEGAL](LEGAL).
