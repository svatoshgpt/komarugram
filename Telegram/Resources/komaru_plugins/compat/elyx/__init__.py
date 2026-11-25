# This is the source code of KomaruGram for Desktop.
#
# The Elyx public API. Its values belong to the plugin that reads them, so
# every attribute is looked up from the plugin module that asks for it.
import os
import platform
import sys

from komaru_runtime import context


def _record():
    frame = sys._getframe(1)
    while frame is not None:
        record = context.record_for_module(frame.f_globals.get("__name__", ""))
        if record is not None:
            return record
        frame = frame.f_back
    return context.current_record()


class Assets:
    def __init__(self, root):
        self.root = root

    def path(self, name):
        full = os.path.normpath(os.path.join(self.root, name))
        if not full.startswith(os.path.normpath(self.root)):
            raise ValueError("Asset path leaves the assets folder")
        return full

    def exists(self, name):
        return bool(self.root) and os.path.exists(self.path(name))

    def read_bytes(self, name):
        with open(self.path(name), "rb") as f:
            return f.read()

    def read_text(self, name, encoding="utf-8"):
        return self.read_bytes(name).decode(encoding)


class Strings(dict):
    def __call__(self, key, **kwargs):
        value = self.get(key, key)
        return value.format(**kwargs) if kwargs else value


def get_environment():
    return {
        "host": "komarugram-desktop",
        "platform": sys.platform,
        "machine": platform.machine(),
        "python": platform.python_version(),
    }


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
    if name == "plugin_dir":
        return record.dir
    raise AttributeError(name)
