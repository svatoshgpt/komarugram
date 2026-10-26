# This is the source code of KomaruGram for Desktop.
#
# exteraGram Android helpers that make sense on desktop.
import logging
import threading

from komaru_runtime import context, protocol


def log(data, *args, **kwargs):
    record = context.current_record()
    name = "plugin." + record.id if record else "plugin"
    logging.getLogger(name).info("%s", data)


def run_on_ui_thread(fn, delay=0):
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
    if delay:
        manager.loop.call_later(delay / 1000.0, call)
    else:
        manager.loop.call_soon_threadsafe(call)


def copy_to_clipboard(text, *args, **kwargs):
    protocol.event("clipboard", text=str(text))
    return True


def show_toast(text, *args, **kwargs):
    protocol.event("toast", text=str(text))


class R:
    pass


class OnClickListener:
    def __init__(self, fn=None):
        self.fn = fn


class OnLongClickListener(OnClickListener):
    pass
