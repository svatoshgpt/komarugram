# This is the source code of KomaruGram for Desktop.
#
# Desktop compatibility for exteraGram's intents.IntentsManager (SDK 1.4.6.5).
# Android intents do not exist on desktop, so handlers are recorded cleanly
# and URL parsing works natively.
from dataclasses import dataclass
import secrets
from typing import Callable, Dict, List, Optional

from komaru_runtime import context


class IntentsManager:
    _AS = ["tg", "https", "http"]
    __handlers: Dict[str, object] = {}

    @dataclass
    class HandlerInfo:
        callback: Callable
        scheme: Optional[str] = None
        host: Optional[str] = None
        path: Optional[str] = None
        required_path_args_names: Optional[List[str]] = None
        priority: Optional[int] = 0
        action: Optional[str] = None
        whitelist_flags: Optional[List[int]] = None
        blacklist_flags: Optional[List[int]] = None
        type: Optional[str] = None
        categories: Optional[List[str]] = None

    @dataclass
    class HandlerHandle:
        handler_id: str

        def unhandle(self):
            IntentsManager.unhandle(self.handler_id)

    class HandlerNotRegistered(Exception):
        def __init__(self, handler_id: str):
            super().__init__(f"Handler '{handler_id}' is not registered.")

    @staticmethod
    def parse(url: str) -> Dict[str, str]:
        s1 = url.split("://", 1)
        if "://" not in url or len(s1) == 1:
            raise ValueError(f"Invalid uri: {url}")

        rest, has_query, query = s1[1].partition("?")
        s2 = rest.split("/", 1)
        result = {
            "scheme": s1[0],
            "host": s2[0],
        }
        if len(s2) > 1:
            result["path"] = s2[1]
        if has_query:
            result["query"] = query
        return result

    @classmethod
    def _register(cls, info: "IntentsManager.HandlerInfo", hook_type: str) -> "IntentsManager.HandlerHandle":
        record = context.current_record()
        if record is not None:
            record.unsupported("add_intent_hook")
        handler_id = secrets.token_hex(16)
        cls.__handlers[handler_id] = (hook_type, info)
        return cls.HandlerHandle(handler_id=handler_id)

    @classmethod
    def new_global_before_handler(cls, info: "IntentsManager.HandlerInfo") -> "IntentsManager.HandlerHandle":
        return cls._register(info, "before")

    @classmethod
    def new_global_after_handler(cls, info: "IntentsManager.HandlerInfo") -> "IntentsManager.HandlerHandle":
        return cls._register(info, "after")

    @classmethod
    def unhandle(cls, handler_id: str):
        if handler_id not in cls.__handlers:
            raise cls.HandlerNotRegistered(handler_id)
        cls.__handlers.pop(handler_id, None)

    @classmethod
    def _stop(cls):
        cls.__handlers.clear()
