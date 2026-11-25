# This is the source code of KomaruGram for Desktop.
#
# The Elyx public API facade (matching exteraGram SDK 1.4.6.5). Plugin-bound
# values (assets, metainfo, refmap, settings, strings, plugin_dir) are
# resolved dynamically for the calling plugin.
import sys

from elyxcore import (  # noqa: F401
    Asset,
    AssetNotFoundException,
    Assets,
    AssetsDirNotFoundException,
    Callback,
    Callback2,
    Callback3,
    CallbackReturn,
    LazyDict,
    OnClickListener,
    Runnable,
    SettingsController,
    Strings,
    gen,
    gen2,
    get_environment,
    import_module,
    mvel_execute,
)
from komaru_runtime import context


def _record():
    frame = sys._getframe(1)
    while frame is not None:
        record = context.record_for_module(frame.f_globals.get("__name__", ""))
        if record is not None:
            return record
        frame = frame.f_back
    return context.current_record()


def __getattr__(name):
    record = _record()
    if record is None:
        raise AttributeError(name)
    if name == "metainfo":
        return dict(record.meta)
    if name == "refmap":
        return dict(record.meta.get("refmap", {}))
    if name == "assets":
        return Assets(record.assets_dir())
    if name == "strings":
        return Strings(record.strings())
    if name == "settings":
        return SettingsController(record.id)
    if name == "plugin_dir":
        return record.dir
    raise AttributeError(name)
