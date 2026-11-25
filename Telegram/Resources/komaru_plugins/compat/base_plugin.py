# This is the source code of KomaruGram for Desktop.
#
# The exteraGram plugin base class (SDK 1.4.6.5), implemented over the desktop
# host. Hooks that exist on desktop work: outgoing text, updates, settings,
# lifecycle, logging and menu items. Xposed/partial/file/intent hooks have
# nothing to attach to here, so they are accepted and ignored with one log
# line each.
import functools
import inspect
from abc import ABC, abstractmethod
from dataclasses import dataclass
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Union

import _sdk_version
import plugin_settings
from android_utils import log as _android_log
from extera_utils import lifecycle
from file_utils import FilesController
from intents import IntentsManager
from komaru_runtime import context
from log_utils import Logger, _plugin_logger, _reset_plugin_logger


@dataclass
class PluginMetadata:
    id: str
    name: str
    description: str
    author: str
    version: str
    icon: str
    min_version: str
    requirements: List[str]


class HookStrategy(Enum):
    DEFAULT = 0
    CANCEL = 1
    MODIFY = 2
    MODIFY_FINAL = 3


class IntentHookType(Enum):
    BEFORE = 0
    AFTER = 1


@dataclass
class HookResult:
    strategy: HookStrategy = HookStrategy.DEFAULT
    request: Any = None
    response: Any = None
    update: Any = None
    updates: Any = None
    error: Any = None
    params: Any = None


class PluginError(Exception):
    def __init__(self, message: str, plugin_id: Optional[str] = None):
        super().__init__(message)
        self.plugin_id = plugin_id


class XposedHook(ABC):
    pass


class MethodReplacement(XposedHook):
    @abstractmethod
    def replace_hooked_method(self, param) -> Any:
        pass


class MethodHook(XposedHook):
    def before_hooked_method(self, param):
        pass

    def after_hooked_method(self, param):
        pass


class PartialChain:
    """Desktop stand-in for exteraGram's PartialChain."""

    __slots__ = ("java", "threw", "_param_types", "_this_object", "_caller", "_args")

    def __init__(self, chain=None, param_types=()):
        self.java = chain
        self.threw = False
        self._param_types = param_types
        self._this_object = None
        self._caller = None
        self._args = []

    @property
    def this_object(self) -> Any:
        return self._this_object

    @property
    def caller(self) -> Any:
        return self._caller

    @property
    def args(self) -> List[Any]:
        return list(self._args)

    def arg(self, index: int) -> Any:
        return self._args[index]

    def proceed(self, *args) -> Any:
        return None

    def throw_to_caller(self, throwable):
        self.threw = True
        if isinstance(throwable, BaseException):
            raise throwable
        raise RuntimeError(str(throwable))


def fn_hook_filters(field_name: str):
    def decorator(func):
        func.__hook_filters__ = lambda self: getattr(self, field_name, []) or []
        return func
    return decorator


def _while_plugin_active(method):
    @functools.wraps(method)
    def wrapped(self, *args, **kwargs):
        scope = getattr(self, "_lifecycle", None)
        if scope is None:
            return method(self, *args, **kwargs)
        with scope._lock:
            if scope.stop_event.is_set():
                return None
            return method(self, *args, **kwargs)
    return wrapped


class BaseHook(MethodHook):
    def __init__(
        self,
        plugin: Optional["BasePlugin"] = None,
        *,
        before: Optional[Callable] = None,
        after: Optional[Callable] = None,
        before_filters: Optional[List[Any]] = None,
        after_filters: Optional[List[Any]] = None,
    ):
        self.plugin = plugin
        self.before = before
        self.after = after
        self.before_filters = before_filters
        self.after_filters = after_filters

    @fn_hook_filters("before_filters")
    def before_hooked_method(self, param):
        if self.before:
            self.before(param)

    @fn_hook_filters("after_filters")
    def after_hooked_method(self, param):
        if self.after:
            self.after(param)


class AppEvent(Enum):
    START = "START"
    STOP = "STOP"
    PAUSE = "PAUSE"
    RESUME = "RESUME"


class MenuItemType(Enum):
    MESSAGE_CONTEXT_MENU = "message_context_menu"
    DRAWER_MENU = "drawer_menu"
    MAIN_MENU = "main_menu"
    CHAT_ACTION_MENU = "chat_action_menu"
    PROFILE_ACTION_MENU = "profile_action_menu"


@dataclass
class MenuItemData:
    menu_type: Any = None
    text: str = ""
    on_click: Optional[Callable[[Dict[str, Any]], None]] = None
    item_id: Optional[str] = None
    icon: Optional[str] = None
    subtext: Optional[str] = None
    condition: Optional[str] = None
    priority: int = 0
    on_long_click: Optional[Callable[[Dict[str, Any]], None]] = None


@dataclass
class HookFilterData:
    filter_type: str
    arg_index: Optional[int] = None
    or_filters: Optional[Any] = None
    mvel_expression: Optional[str] = None
    instance_of: Optional[Any] = None
    object: Any = None

    def to_java_filter(self):
        return self


class HookFilter(Enum):
    RESULT_IS_NULL = "RESULT_IS_NULL"
    RESULT_IS_TRUE = "RESULT_IS_TRUE"
    RESULT_IS_FALSE = "RESULT_IS_FALSE"
    RESULT_NOT_NULL = "RESULT_NOT_NULL"

    @property
    def filter_data(self):
        return HookFilterData(filter_type=self.value)

    @staticmethod
    def ResultIsInstanceOf(clazz):
        return HookFilterData(filter_type="RESULT_IS_INSTANCE_OF", instance_of=clazz)

    @staticmethod
    def ResultEqual(value):
        return HookFilterData(filter_type="RESULT_EQUAL", object=value)

    @staticmethod
    def ResultNotEqual(value):
        return HookFilterData(filter_type="RESULT_NOT_EQUAL", object=value)

    @staticmethod
    def ArgumentIsNull(index: int):
        return HookFilterData(filter_type="ARGUMENT_IS_NULL", arg_index=index)

    @staticmethod
    def ArgumentIsTrue(index: int):
        return HookFilterData(filter_type="ARGUMENT_IS_TRUE", arg_index=index)

    @staticmethod
    def ArgumentIsFalse(index: int):
        return HookFilterData(filter_type="ARGUMENT_IS_FALSE", arg_index=index)

    @staticmethod
    def ArgumentNotNull(index: int):
        return HookFilterData(filter_type="ARGUMENT_NOT_NULL", arg_index=index)

    @staticmethod
    def ArgumentIsInstanceOf(index: int, clazz):
        return HookFilterData(
            filter_type="ARGUMENT_IS_INSTANCE_OF", arg_index=index, instance_of=clazz
        )

    @staticmethod
    def ArgumentEqual(index: int, value):
        return HookFilterData(
            filter_type="ARGUMENT_EQUAL", arg_index=index, object=value
        )

    @staticmethod
    def ArgumentNotEqual(index: int, value):
        return HookFilterData(
            filter_type="ARGUMENT_NOT_EQUAL", arg_index=index, object=value
        )

    @staticmethod
    def Condition(condition: str, object: Any = None):
        return HookFilterData(
            filter_type="CONDITION", mvel_expression=condition, object=object
        )

    @staticmethod
    def Or(*filters):
        if not filters:
            return None
        items = [
            f if isinstance(f, HookFilterData) else f.filter_data
            for f in filters
            if isinstance(f, (HookFilter, HookFilterData))
        ]
        return HookFilterData(filter_type="OR", or_filters=items)


def hook_filters(*filters):
    def decorator(func):
        raw = filters[0] if len(filters) == 1 and isinstance(filters[0], list) else filters
        func.__hook_filters__ = [
            f if isinstance(f, HookFilterData) else f.filter_data
            for f in raw
        ]
        return func
    return decorator


_HOOK_ACCOUNT_ARG = {
    "pre_request_hook": 1,
    "post_request_hook": 1,
    "on_update_hook": 1,
    "on_updates_hook": 1,
    "on_send_message_hook": 0,
}


def _account_scoped_hook(fn: Callable, account_index: int) -> Callable:
    @functools.wraps(fn)
    def wrapper(self, *args, **kwargs):
        from client_utils import account_scope

        if len(args) > account_index:
            account = args[account_index]
        else:
            account = kwargs.get("account")

        with account_scope(account):
            return fn(self, *args, **kwargs)

    wrapper.__extera_account_scoped__ = True
    return wrapper


class BasePlugin:
    """Base class of exteraGram plugins and of KomaruGram userbots."""

    __fh: Dict[str, List[Any]] = {}
    __ih: Dict[str, List[Any]] = {}

    _begin_lifecycle = staticmethod(lifecycle.begin)
    _stop_lifecycle = staticmethod(lifecycle.stop)
    _stop_all_lifecycles = staticmethod(lifecycle.stop_all)

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        for name, account_index in _HOOK_ACCOUNT_ARG.items():
            fn = cls.__dict__.get(name)
            if fn is None or getattr(fn, "__extera_account_scoped__", False):
                continue
            try:
                setattr(cls, name, _account_scoped_hook(fn, account_index))
            except Exception as e:
                _android_log(f"Could not bind {cls.__name__}.{name} to its account: {e}")

    @classmethod
    def _findPluginClass(cls, module):
        for _, obj in inspect.getmembers(module):
            if inspect.isclass(obj):
                if issubclass(obj, cls) and obj is not cls:
                    return obj
                ancestors = [_cls.__name__ for _cls in inspect.getmro(obj)]
                if cls.__name__ in ancestors and obj.__name__ != cls.__name__:
                    return obj
        return None

    @classmethod
    def _cleanup(cls, plugin_id: str):
        for ext, secret in cls.__fh.pop(plugin_id, []):
            try:
                FilesController.unregister(ext, secret)
            except BaseException:
                pass

        for handle in cls.__ih.pop(plugin_id, []):
            try:
                handle.unhandle()
            except BaseException:
                pass

        _reset_plugin_logger(plugin_id)

    def __init__(self):
        _sdk_version.Z.c()
        record = context.current_record()
        meta = record.meta if record else {}
        self._komaru_record = record
        self._lifecycle = lifecycle.capture_scope(type(self))
        self.id: str = record.id if record else self.__class__.__name__
        self.name: str = str(meta.get("name", self.id))
        self.description: str = str(meta.get("description", ""))
        self.author: str = str(meta.get("author", ""))
        self.min_version: str = str(meta.get("min_version", ""))
        self.version: str = str(meta.get("version", ""))
        reqs = meta.get("requirements") or []
        if isinstance(reqs, str):
            reqs = [r.strip() for r in reqs.split(",") if r.strip()]
        self.requirements: List[str] = list(reqs)
        self.icon: Optional[str] = meta.get("icon") or None
        self.error_message: Optional[str] = None
        self.enabled: bool = bool(record.enabled) if record else False
        self.initialized: bool = False

    @property
    def stop_event(self):
        return self._lifecycle.stop_event if self._lifecycle is not None else None

    def register_cleanup(self, cleanup):
        if self._lifecycle is None:
            raise RuntimeError("Plugin lifecycle is not initialized")
        return self._lifecycle.register_cleanup(cleanup)

    # Lifecycle.
    def on_plugin_load(self):
        pass

    def _invoke_plugin_unload(self):
        if self._lifecycle is not None:
            with lifecycle.cleanup_context(self._lifecycle):
                self.on_plugin_unload()
        else:
            self.on_plugin_unload()

    def on_plugin_unload(self):
        pass

    def on_app_event(self, event_type: AppEvent):
        pass

    def create_settings(self) -> List[Any]:
        return []

    # Hooks.
    def on_send_message_hook(self, account: int, params: Any) -> HookResult:
        return HookResult()

    def on_update_hook(self, update_name: str, account: int, update: Any) -> HookResult:
        return HookResult()

    def on_updates_hook(self, container_name: str, account: int, updates: Any) -> HookResult:
        return HookResult()

    def pre_request_hook(self, request_name: str, account: int, request: Any) -> HookResult:
        return HookResult()

    def post_request_hook(self, request_name: str, account: int, response: Any, error: Any) -> HookResult:
        return HookResult()

    @_while_plugin_active
    def add_on_send_message_hook(self, priority: int = 0):
        if self._komaru_record is None:
            return
        self._komaru_record.send_hook = True
        self._komaru_record.send_hook_priority = int(priority or 0)
        if context.manager():
            context.manager().notify_changed()

    def remove_on_send_message_hook(self):
        if self._komaru_record is None:
            return
        self._komaru_record.send_hook = False
        if context.manager():
            context.manager().notify_changed()

    @_while_plugin_active
    def add_hook(self, name: str, match_substring: bool = False, priority: int = 0):
        if not name or self._komaru_record is None:
            return
        if name == "send_message_hook":
            self.add_on_send_message_hook(priority=priority)
            return
        self._komaru_record.update_hooks.append((name, bool(match_substring), int(priority or 0)))
        if context.manager():
            context.manager().ensure_update_listener(self._komaru_record)
            context.manager().notify_changed()

    def remove_hook(self, name: str):
        if not name or self._komaru_record is None:
            return
        if name == "send_message_hook":
            self.remove_on_send_message_hook()
            return
        hooks = self._komaru_record.update_hooks
        hooks[:] = [entry for entry in hooks if entry[0] != name]

    @_while_plugin_active
    def add_file_hook(self, file_info: FilesController.FileInfo):
        if not file_info or not isinstance(file_info, FilesController.FileInfo):
            raise ValueError("You must provide a file info (instance of FilesController.FileInfo)")
        secret = FilesController.register(file_info)
        BasePlugin.__fh[self.id] = BasePlugin.__fh.get(self.id, []) + [(file_info.ext, secret)]
        return secret

    def remove_file_hook(self, ext: str, secret: str):
        if not ext or not secret:
            raise ValueError("You must provide a file extension and secret for unregister")
        FilesController.unregister(ext, secret)
        BasePlugin.__fh[self.id] = [
            item for item in BasePlugin.__fh.get(self.id, []) if item[0] != ext
        ]

    @_while_plugin_active
    def add_intent_hook(self, info: IntentsManager.HandlerInfo, type: IntentHookType):
        if not info or not isinstance(info, IntentsManager.HandlerInfo):
            raise ValueError("You must provide a handler info (instance of IntentsManager.HandlerInfo)")
        if not type or not isinstance(type, IntentHookType):
            raise ValueError("You must provide a handler type (instance of IntentHookType)")
        if type == IntentHookType.BEFORE:
            handle = IntentsManager.new_global_before_handler(info)
        else:
            handle = IntentsManager.new_global_after_handler(info)
        BasePlugin.__ih[self.id] = BasePlugin.__ih.get(self.id, []) + [handle]
        return handle

    def remove_intent_hook(self, handler_id: Union[IntentsManager.HandlerHandle, str]):
        if not handler_id or not isinstance(handler_id, (IntentsManager.HandlerHandle, str)):
            raise ValueError("You must provide a handler info (instance of IntentsManager.HandlerHandle)")
        target_id = handler_id if isinstance(handler_id, str) else handler_id.handler_id
        IntentsManager.unhandle(target_id)
        BasePlugin.__ih[self.id] = [
            h for h in BasePlugin.__ih.get(self.id, []) if h.handler_id != target_id
        ]

    # Android-only Xposed & partial hooks.
    def _unsupported(self, what: str):
        if self._komaru_record is not None:
            self._komaru_record.unsupported(what)

    def hook_method(self, *args, **kwargs):
        self._unsupported("hook_method")
        return None

    def hook_all_methods(self, *args, **kwargs):
        self._unsupported("hook_all_methods")
        return []

    def hook_all_constructors(self, *args, **kwargs):
        self._unsupported("hook_all_constructors")
        return []

    def unhook_method(self, *args, **kwargs):
        return None

    def hook_call_site(self, in_method, callee, callback, occurrence: int = -1, priority: Optional[int] = None):
        self._unsupported("hook_call_site")
        return None

    def hook_field_read(self, in_method, field, callback, occurrence: int = -1, priority: Optional[int] = None):
        self._unsupported("hook_field_read")
        return None

    def hook_field_write(self, in_method, field, callback, occurrence: int = -1, priority: Optional[int] = None):
        self._unsupported("hook_field_write")
        return None

    def hook_string_constant(self, in_method, text: str, callback, occurrence: int = -1, priority: Optional[int] = None):
        self._unsupported("hook_string_constant")
        return None

    def hook_return_value(self, in_method, callback, occurrence: int = -1, priority: Optional[int] = None):
        self._unsupported("hook_return_value")
        return None

    def unhook_partial(self, handle):
        return None

    # Logging.
    @property
    def logger(self) -> Logger:
        custom = self.__dict__.get("_custom_logger")
        if custom is not None:
            return custom
        plugin_id = getattr(self, "id", "")
        if plugin_id:
            return _plugin_logger(plugin_id)
        return Logger(f"plugin.{type(self).__name__}")

    @logger.setter
    def logger(self, value):
        self.__dict__["_custom_logger"] = value

    def log(self, message: Any):
        _android_log(f"[{self.id}] {message}")

    # Menu items.
    @_while_plugin_active
    def add_menu_item(self, item=None, *args, **kwargs) -> Optional[str]:
        if self._komaru_record is None:
            return None
        if not isinstance(item, MenuItemData):
            item = MenuItemData(*((item,) + args if item is not None else args), **kwargs)
        record = self._komaru_record
        item_id = item.item_id or "item%d" % (len(record.menu_items) + 1)
        item.item_id = item_id
        record.menu_items[item_id] = item
        if context.manager():
            context.manager().notify_changed()
        return item_id

    def remove_menu_item(self, item_id=None, *args, **kwargs) -> bool:
        if self._komaru_record is None:
            return False
        removed = self._komaru_record.menu_items.pop(
            getattr(item_id, "item_id", item_id), None
        )
        if removed is not None and context.manager():
            context.manager().notify_changed()
        return removed is not None

    # Settings.
    def get_setting(self, key: str, default: Any = None) -> Any:
        if not self.id or not key or self.error_message:
            return default
        return plugin_settings.get_setting(self.id, key, default)

    def set_setting(self, key: str, value: Any, reload_settings: bool = False):
        if not self.id or not key or self.error_message:
            return
        plugin_settings.set_setting(self.id, key, value)
        if reload_settings and self._komaru_record is not None and context.manager():
            context.manager().notify_settings(self._komaru_record)

    def export_settings(self) -> dict:
        if not self.id or self.error_message:
            return {}
        return plugin_settings.get_all_settings(self.id)

    def import_settings(self, settings: dict, reload_settings: bool = True):
        if not self.id or self.error_message:
            return
        plugin_settings.set_all_settings(self.id, settings)
        if reload_settings and self._komaru_record is not None and context.manager():
            context.manager().notify_settings(self._komaru_record)

    # Accounts.
    def client(self, account: Optional[int] = None):
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
