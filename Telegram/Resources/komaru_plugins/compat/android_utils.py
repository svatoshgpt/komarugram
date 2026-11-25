# This is the source code of KomaruGram for Desktop.
#
# exteraGram Android helpers (SDK 1.4.6.5) that make sense on desktop.
import logging
from typing import Any

from extera_utils.lifecycle import ManagedCallback, bind, bind_task
from komaru_runtime import context, protocol
from log_utils import dump


def log(data: Any, *args, **kwargs):
    record = context.current_record()
    name = "plugin." + record.id if record else "plugin"
    if isinstance(data, (str, int, float, bool)) or data is None:
        text = str(data)
    else:
        text = dump(data)
    logging.getLogger(name).info("%s", text)


def run_on_ui_thread(fn, delay: int = 0):
    # The host loop is the closest thing to a UI thread: callbacks run one
    # after another, like on Android's main looper.
    manager = context.manager()
    record = context.current_record()

    def call():
        with context.running(record):
            try:
                fn()
            except Exception:
                if record:
                    record.log_exception("run_on_ui_thread")

    callback = bind_task(call, once=True)
    if callback.cancelled:
        return callback

    if manager is None:
        callback()
        return callback

    if delay:
        handle = manager.loop.call_later(max(delay, 0) / 1000.0, callback)
        callback.set_cancel(handle.cancel)
    else:
        handle = manager.loop.call_soon_threadsafe(callback)
        callback.set_cancel(handle.cancel)
    return callback


def copy_to_clipboard(text, *args, **kwargs):
    protocol.event("clipboard", text=str(text))
    return True


def show_toast(text, *args, **kwargs):
    protocol.event("toast", text=str(text))


class R:
    def __init__(self, fn=None):
        if fn is not None and not callable(fn):
            raise TypeError("fn must be callable")
        if fn is None:
            self._fn = None
        else:
            self._fn = fn if isinstance(fn, ManagedCallback) else bind_task(fn)

    def run(self):
        if self._fn is not None:
            self._fn()

    __call__ = run


class OnClickListener:
    def __init__(self, fn=None):
        self.fn = bind(fn) if callable(fn) else fn

    def onClick(self, view=None):
        if callable(self.fn):
            self.fn(view)


class OnLongClickListener(OnClickListener):
    def onLongClick(self, view=None):
        if callable(self.fn):
            result = self.fn(view)
            return True if result is None else bool(result)
        return False
