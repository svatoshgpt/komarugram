# This is the source code of KomaruGram for Desktop.
#
# Utilities matching exteraGram's elyxcore.utils (SDK 1.4.6.5).
import io
import traceback
from typing import Any, Callable, Optional

from android_utils import log as logcat
from client_utils import run_on_queue


class LazyDict(dict):
    def __setattr__(self, key, value):
        self.__setitem__(key, value)

    def __getattr__(self, item):
        try:
            return self.__getitem__(item)
        except KeyError:
            raise AttributeError(item)


def gen(java_class, method_name: str, return_value: bool = False, default_value=None):
    if default_value and not return_value:
        raise ValueError("provide default_value only if return_value is True")

    def _run(instance, *java_args):
        try:
            value = instance._fn(*java_args, *instance._args, **instance._kwargs)
            if return_value:
                return value
        except Exception:
            logcat("[ERROR] [ElyxCore] " + traceback.format_exc())
            if return_value:
                return default_value

    def __init__(self, fn, *args, **kwargs):
        self._fn = fn
        self._args = args
        self._kwargs = kwargs

    name = getattr(java_class, "__name__", str(java_class))
    return type("GeneratedProxyClass_" + name, (), {
        "__init__": __init__,
        method_name: _run,
        "__call__": _run,
    })


def gen2(java_class, return_value: bool = False, **methods):
    def _wrap(method: Callable):
        def _fn(instance, *java_args):
            try:
                value = method(*java_args, *instance._args, **instance._kwargs)
                if return_value:
                    return value
            except Exception:
                logcat("[ERROR] [ElyxCore] " + traceback.format_exc())
        return _fn

    def __init__(self, *args, **kwargs):
        self._args = args
        self._kwargs = kwargs

    name = getattr(java_class, "__name__", str(java_class))
    attrs = {"__init__": __init__}
    for method_name, method in methods.items():
        attrs[method_name] = _wrap(method)
    return type("GeneratedProxyClass_" + name, (), attrs)


def mvel_execute(script: str, data: Optional[dict] = None, to_type=None, java_instance=None):
    return None


def safe_run_on_queue(fn):
    def _fn():
        try:
            fn()
        except Exception:
            logcat("[ERROR] [Elyx:RunOnQueue] " + traceback.format_exc())

    return run_on_queue(_fn)


def safe_func(fn):
    def _fn(*args, **kwargs):
        try:
            return fn(*args, **kwargs)
        except Exception:
            logcat(f"[ERROR] [Elyx:SafeFunc] {fn.__name__} failed\n" + traceback.format_exc())
    return _fn


def hash_map(dict_):
    return dict(dict_ or {})


def resize_image(image: bytes, width: int, height: int) -> bytes:
    from PIL import Image
    img = Image.open(io.BytesIO(image))
    resized_image = img.resize((width, height), Image.Resampling.LANCZOS)
    output = io.BytesIO()
    resized_image.save(output, format="PNG")
    return output.getvalue()
