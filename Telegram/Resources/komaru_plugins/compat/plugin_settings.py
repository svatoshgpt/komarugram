# This is the source code of KomaruGram for Desktop.
#
# Plugin settings storage matching exteraGram's plugin_settings module, backed
# by KomaruGram's host state.json.
import copy
import threading
from typing import Any

from komaru_runtime import context

_lock = threading.RLock()
_fallback_cache = {}


def _store():
    manager = context.manager()
    if manager is not None:
        return manager.state.setdefault("settings", {}), manager
    return _fallback_cache, None


def get_setting(plugin_id: str, key: str, default: Any = None) -> Any:
    with _lock:
        settings_map, _ = _store()
        plugin_data = settings_map.get(plugin_id, {})
        if key not in plugin_data:
            return default
        return copy.deepcopy(plugin_data[key])


def set_setting(plugin_id: str, key: str, value: Any):
    with _lock:
        settings_map, manager = _store()
        plugin_data = dict(settings_map.get(plugin_id, {}))
        plugin_data[key] = copy.deepcopy(value)
        settings_map[plugin_id] = plugin_data
        if manager is not None:
            manager.save_state()


def clear_settings(plugin_id: str):
    with _lock:
        settings_map, manager = _store()
        if plugin_id in settings_map:
            settings_map.pop(plugin_id, None)
            if manager is not None:
                manager.save_state()


def get_all_settings(plugin_id: str) -> dict:
    with _lock:
        settings_map, _ = _store()
        return copy.deepcopy(settings_map.get(plugin_id, {}))


def set_all_settings(plugin_id: str, settings: dict):
    with _lock:
        settings_map, manager = _store()
        settings_map[plugin_id] = copy.deepcopy(settings or {})
        if manager is not None:
            manager.save_state()
