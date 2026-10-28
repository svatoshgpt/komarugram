# KomaruGram plugins

Plugins live under **Plugins** in the main menu, right below My Profile.
They run in a separate Python process, so a broken plugin cannot crash the
client. On Windows the official embeddable Python 3.11 with Telethon and
Kurigram ships with KomaruGram in `plugins_runtime` next to the executable,
so nothing is downloaded. On Linux KomaruGram installs them for the system
`python3` with pip.

A `.plugin`, `.elyx` or `.eaf` file opened in a chat shows an Install
button, and so does a `.py` file that imports Telethon, Kurigram or the
`komaru` SDK. A `.py` without `__id__` is named after its file.

Every plugin acts as your account. KomaruGram hands each logged in account's
DC address and auth key to the plugin host over a pipe, never writes it to
disk, and asks before a plugin is enabled.

## Formats

| File | What it is |
| --- | --- |
| `.plugin` | A single-file exteraGram plugin |
| `.elyx`, `.eaf`, `.zip` | An Elyx archive: `refmap.yml`, metainfo, entry module, optional `wheels/` |
| `.py` | A single-file Python plugin or userbot |
| `.elyx` with `runtime: go` | A Go plugin, see below |

Metadata is read without running the plugin: top-level `__id__`, `__name__`,
`__description__`, `__author__`, `__version__`, `__icon__` and
`__requirements__` in a single file, or the metainfo file of an archive.
`requirements` are installed with pip into the plugin's own folder; bundled
wheels are unpacked the same way.

## exteraGram plugins

The exteraGram SDK is available: `base_plugin`, `ui.settings`,
`ui.bulletin`, `ui.alert`, `client_utils`, `android_utils`, `hook_utils`,
`file_utils` and `elyx`. What works on desktop:

- `on_plugin_load`, `on_plugin_unload`, `on_app_event`;
- `create_settings` with Header, Divider, Switch, Selector, Input, Text and
  EditText, sub-pages included, drawn as native KomaruGram settings;
- `add_on_send_message_hook` and `on_send_message_hook` for outgoing text;
- `add_hook("TL_updateNewMessage")` and other updates, delivered as Telethon
  objects;
- `send_text`, `send_message`, `send_photo`/`document`/`video`/`audio`,
  `edit_message` and `send_request` (with Telethon requests), per account;
- bulletins and dialogs become toasts.

Xposed hooks, Java classes and Android menu items have nothing to attach to
on desktop. Plugins that import them still load: the Android modules are
stand-ins that do nothing, and `hook_method`/`add_menu_item` are logged once
and ignored.

## Python userbots

```python
from komaru import Plugin
from telethon import events

__id__ = "ping"
__name__ = "Ping"
__version__ = "1.0"


class Ping(Plugin):
    def on_plugin_load(self):
        @self.on(events.NewMessage(outgoing=True, pattern=r"\.ping"))
        async def ping(event):
            await event.edit("pong")

        self.spawn(self.hello())

    async def hello(self):
        app = await self.kurigram()          # a started Kurigram client
        me = await app.get_me()
        self.logger.info("Kurigram works as %s", me.first_name)
```

`self.telethon(account)` and `self.kurigram(account)` return connected
clients for the account (the first one by default). `self.on(...)` registers
a Telethon handler and `self.spawn(...)` a coroutine; both are removed when
the plugin is disabled.

## Go plugins

An archive with this metainfo:

```yaml
id: go_ping
name: Go Ping
version: "1.0"
runtime: go
hooks: [send_message]      # optional, for send hooks
binaries:
  windows-amd64: bin/go_ping.exe
  windows-386: bin/go_ping_386.exe
  linux-amd64: bin/go_ping
settings:
  - { type: switch, key: loud, text: "Loud replies", default: false }
```

The host starts the binary for the current platform, or `go run .` when the
archive carries source and Go is installed. The plugin talks JSON lines over
stdin/stdout; the `komaru` package in [`go/komaru`](go/komaru/komaru.go)
wraps the protocol, and every account's session data is there for gotd/td.
