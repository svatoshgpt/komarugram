# This is the source code of KomaruGram for Desktop.
#
# The native API for KomaruGram userbots. A userbot is a .py file with the
# same top-level metadata as an exteraGram plugin and a Plugin subclass:
#
#     from komaru import Plugin
#     from telethon import events
#
#     __id__ = "ping"
#     __name__ = "Ping"
#
#     class Ping(Plugin):
#         def on_plugin_load(self):
#             @self.on(events.NewMessage(outgoing=True, pattern=r"\.ping"))
#             async def ping(event):
#                 await event.edit("pong")
#
# self.telethon() and self.kurigram() return connected clients for the
# account, self.spawn() runs a coroutine that is cancelled on unload, and
# every exteraGram helper keeps working.
from base_plugin import (  # noqa: F401
    AppEvent,
    BasePlugin,
    HookResult,
    HookStrategy,
)

Plugin = BasePlugin
