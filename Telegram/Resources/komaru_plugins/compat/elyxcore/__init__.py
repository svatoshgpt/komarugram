# This is the source code of KomaruGram for Desktop.
#
# Public API for structured Elyx plugins matching exteraGram's elyxcore (SDK 1.4.6.5).
from __future__ import annotations

import importlib
import platform
import sys
from typing import Any, Dict, Optional

from elyxcore.assets import (
    Asset,
    AssetNotFoundException,
    Assets,
    AssetsDirNotFoundException,
)
from elyxcore.localization import Strings
from elyxcore.settings import SettingsController
from elyxcore.utils import LazyDict, gen, gen2, mvel_execute
from komaru_runtime import context


class _StubInterface:
    pass


OnClickListener = gen(_StubInterface, "onClick")
Runnable = gen(_StubInterface, "run")
Callback = gen(_StubInterface, "run")
Callback2 = gen(_StubInterface, "run")
Callback3 = gen(_StubInterface, "run")
CallbackReturn = gen(_StubInterface, "run", True)


def _caller_record():
    frame = sys._getframe(1)
    while frame is not None:
        record = context.record_for_module(frame.f_globals.get("__name__", ""))
        if record is not None:
            return record
        frame = frame.f_back
    return context.current_record()


def get_environment() -> Dict[str, Any]:
    record = _caller_record()
    env = {
        "host": "komarugram-desktop",
        "platform": sys.platform,
        "machine": platform.machine(),
        "python": platform.python_version(),
    }
    if record is not None:
        env.update({
            "metainfo": dict(record.meta),
            "refmap": dict(record.meta.get("refmap", {})),
            "assets": Assets(record.assets_dir()),
            "strings": Strings(record.strings()),
            "settings": SettingsController(record.id),
            "plugin_dir": record.dir,
        })
    return env


def import_module(name: str, package: Optional[str] = None):
    if package is None:
        record = _caller_record()
        if record is not None and name.startswith("."):
            package = "komaru_plugins." + record.id
    return importlib.import_module(name, package=package)


__all__ = (
    "Asset",
    "AssetNotFoundException",
    "Assets",
    "AssetsDirNotFoundException",
    "Callback",
    "Callback2",
    "Callback3",
    "CallbackReturn",
    "LazyDict",
    "OnClickListener",
    "Runnable",
    "SettingsController",
    "Strings",
    "gen",
    "gen2",
    "get_environment",
    "import_module",
    "mvel_execute",
)


def _public_api() -> Dict[str, Any]:
    return {name: globals()[name] for name in __all__}


def _setup():
    pass


def _stop():
    pass
