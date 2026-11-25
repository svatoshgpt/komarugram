# This is the source code of KomaruGram for Desktop.
#
# Caller identification helpers matching exteraGram's extera_utils.get_caller.
import inspect
import os

ELYX_PLUGIN_NAMESPACE = "ElyxPlugins"
KOMARU_PLUGIN_NAMESPACE = "komaru_plugins"


def _get_callable_location(fn):
    target = fn
    seen = set()

    for _ in range(5):
        if target is None or id(target) in seen:
            break
        seen.add(id(target))

        code = getattr(target, "__code__", None)
        if code is not None:
            return (
                code.co_filename,
                code.co_name,
                code.co_firstlineno,
                getattr(target, "__module__", ""),
            )

        next_target = getattr(target, "__wrapped__", None)
        if next_target is None:
            next_target = getattr(target, "__func__", None)
        if next_target is None:
            next_target = getattr(target, "func", None)
        if next_target is None:
            next_target = getattr(type(target), "__call__", None)
        target = next_target

    return None


def _plugin_id_from_location(filename, module_name=""):
    module_parts = str(module_name).split(".")
    if len(module_parts) > 1 and module_parts[0] in (
        ELYX_PLUGIN_NAMESPACE,
        KOMARU_PLUGIN_NAMESPACE,
    ):
        return module_parts[1]

    path_parts = str(filename).replace("\\", "/").split("/")
    for marker in (ELYX_PLUGIN_NAMESPACE, "installed", "plugins"):
        try:
            idx = path_parts.index(marker)
            if idx + 1 < len(path_parts):
                candidate = path_parts[idx + 1]
                if marker == "plugins" and idx + 1 == len(path_parts) - 1:
                    return os.path.splitext(candidate)[0]
                return candidate
        except ValueError:
            pass

    return os.path.splitext(os.path.basename(str(filename)))[0]


def _format_location(filename, function, lineno, module_name=""):
    plugin_id = _plugin_id_from_location(filename, module_name)
    return f"[{os.path.basename(str(filename))}:{function}:{lineno}] ", plugin_id


def get_plugin_id(fn=None):
    try:
        location = _get_callable_location(fn)
    except Exception:
        location = None
    if location is not None:
        return _format_location(*location)

    frame = None
    caller_frame = None
    try:
        frame = inspect.currentframe()
        if frame and frame.f_back:
            caller_frame = frame.f_back.f_back
        if caller_frame:
            code = caller_frame.f_code
            return _format_location(
                code.co_filename,
                code.co_name,
                caller_frame.f_lineno,
                caller_frame.f_globals.get("__name__", ""),
            )
    except Exception:
        pass
    finally:
        del frame
        del caller_frame

    return "", ""
