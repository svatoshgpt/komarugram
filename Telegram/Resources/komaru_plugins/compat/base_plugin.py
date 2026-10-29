# This is the source code of KomaruGram for Desktop.
#
# The exteraGram plugin base class, implemented over the desktop host. Hooks
# that exist on desktop work: outgoing text, updates and settings. Xposed
# method hooks have nothing to attach to here, so they are accepted and
# ignored with one log line each. Chat menu items go to the Plugins submenu
# of the chat menu in KomaruGram.
import asyncio
import logging
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, List, Optional

from komaru_runtime import context


class HookStrategy(Enum):
    DEFAULT = 0
    CANCEL = 1
    MODIFY = 2
    MODIFY_FINAL = 3


@dataclass
class HookResult:
    strategy: HookStrategy = HookStrategy.DEFAULT
    params: Any = None
    request: Any = None
    response: Any = None
    update: Any = None
    updates: Any = None
    error: Any = None


class AppEvent(Enum):
    START = "START"
    STOP = "STOP"
    PAUSE = "PAUSE"
    RESUME = "RESUME"


class MenuItemType(Enum):
    MESSAGE_CONTEXT_MENU = "message_context_menu"
    DRAWER_MENU = "drawer_menu"
    CHAT_ACTION_MENU = "chat_action_menu"
    PROFILE_ACTION_MENU = "profile_action_menu"


@dataclass
class MenuItemData:
    menu_type: Any = None
    text: str = ""
    on_click: Optional[Callable] = None
    item_id: Optional[str] = None
    icon: Optional[str] = None
    subtext: Optional[str] = None
    condition: Optional[str] = None
    priority: int = 0


class MethodHook:
    def before_hooked_method(self, param):
        pass

    def after_hooked_method(self, param):
        pass


class MethodReplacement(MethodHook):
    def replace_hooked_method(self, param):
        return None


class XposedHook(MethodHook):
    pass


class PluginLogger(logging.LoggerAdapter):
    def dump(self, value, *args, **kwargs):
        self.info("%r", value)

    def process(self, msg, kwargs):
        return msg, kwargs


class BasePlugin:
    """Base class of exteraGram plugins and of KomaruGram userbots."""

    def __init__(self):
        record = context.current_record()
        self.id = record.id if record else self.__class__.__name__
        self.name = record.meta.get("name", self.id) if record else self.id
        self.logger = PluginLogger(logging.getLogger("plugin." + self.id), {})
        self._komaru_record = record

    # Lifecycle.
    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass

    def on_app_event(self, event_type):
        pass

    def create_settings(self) -> List[Any]:
        return []

    # Hooks.
    def on_send_message_hook(self, account, params) -> HookResult:
        return HookResult()

    def on_update_hook(self, update_name, account, update) -> HookResult:
        return HookResult()

    def on_updates_hook(self, container_name, account, updates) -> HookResult:
        return HookResult()

    def pre_request_hook(self, request_name, account, request) -> HookResult:
        return HookResult()

    def post_request_hook(self, request_name, account, response, error) -> HookResult:
        return HookResult()

    def add_on_send_message_hook(self, priority: int = 0):
        self._komaru_record.send_hook = True
        context.manager().notify_changed()

    def remove_on_send_message_hook(self):
        self._komaru_record.send_hook = False
        context.manager().notify_changed()

    def add_hook(self, name: str, match_substring: bool = False, priority: int = 0):
        self._komaru_record.update_hooks.append((name, match_substring))
        context.manager().ensure_update_listener(self._komaru_record)

    def remove_hook(self, name: str):
        hooks = self._komaru_record.update_hooks
        hooks[:] = [entry for entry in hooks if entry[0] != name]

    def hook_method(self, *args, **kwargs):
        self._komaru_record.unsupported("hook_method")
        return None

    def hook_all_methods(self, *args, **kwargs):
        self._komaru_record.unsupported("hook_all_methods")
        return []

    def hook_all_constructors(self, *args, **kwargs):
        self._komaru_record.unsupported("hook_all_constructors")
        return []

    def unhook_method(self, *args, **kwargs):
        return None

    def add_menu_item(self, item=None, *args, **kwargs):
        if not isinstance(item, MenuItemData):
            item = MenuItemData(*((item,) + args if item is not None else args), **kwargs)
        record = self._komaru_record
        item_id = item.item_id or "item%d" % (len(record.menu_items) + 1)
        item.item_id = item_id
        record.menu_items[item_id] = item
        context.manager().notify_changed()
        return item_id

    def remove_menu_item(self, item_id=None, *args, **kwargs):
        removed = self._komaru_record.menu_items.pop(
            getattr(item_id, "item_id", item_id), None)
        if removed is not None:
            context.manager().notify_changed()
        return removed is not None

    # Settings.
    def get_setting(self, key, default=None):
        return self._komaru_record.settings.get(key, default)

    def set_setting(self, key, value, reload_settings=False):
        self._komaru_record.settings[key] = value
        context.manager().save_state()
        if reload_settings:
            context.manager().notify_settings(self._komaru_record)

    def export_settings(self):
        return dict(self._komaru_record.settings)

    def import_settings(self, settings, reload_settings=True):
        self._komaru_record.settings.update(settings or {})
        context.manager().save_state()
        if reload_settings:
            context.manager().notify_settings(self._komaru_record)

    # Accounts.
    def client(self, account=None):
        from client_utils import get_client
        return get_client(account)

    # KomaruGram additions for full userbots.
    async def telethon(self, account=None):
        return await context.manager().accounts.telethon(account)

    async def kurigram(self, account=None):
        return await context.manager().accounts.kurigram(account)

    def session(self, account=None):
        return context.manager().accounts.session(account)

    def spawn(self, coroutine):
        """Runs a coroutine on the host loop and cancels it on unload."""
        return context.manager().spawn(self._komaru_record, coroutine)

    def on(self, builder, account=None):
        """Decorator for a Telethon event handler that is removed on
        unload: @self.on(events.NewMessage(outgoing=True))."""
        def decorator(callback):
            async def attach():
                client = await self.telethon(account)
                client.add_event_handler(callback, builder)
                self._komaru_record.telethon_handlers.append((client, callback, builder))
            self.spawn(attach())
            return callback
        return decorator
