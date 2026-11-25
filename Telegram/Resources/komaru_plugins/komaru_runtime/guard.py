# This is the source code of KomaruGram for Desktop.
#
# Finds the plugin behind a host crash or a hang and keeps it off on the next
# start. A crash leaves a faulthandler dump, a hang a stack the watchdog
# writes before it ends the host; the first plugin file in that stack is the
# one to blame. The client starts the host again after either.
import faulthandler
import logging
import os
import re
import sys
import threading
import time
import traceback

log = logging.getLogger("komaru.guard")

CRASH_FILE = "last-crash.txt"
HANG_MARK = "HANG"
HANG_AFTER = 20.0
BEAT_EVERY = 1.0

_PLUGIN_PATH = re.compile(r"(?:installed|libs)[\\/]+([^\\/\"']+)[\\/]")


def culprit(text, known):
    for match in _PLUGIN_PATH.finditer(text):
        if match.group(1) in known:
            return match.group(1)
    return None


class Guard:
    def __init__(self, data_dir):
        self.path = os.path.join(data_dir, CRASH_FILE)
        self.file = None
        self.beat = time.monotonic()

    def previous(self, known):
        """The plugin that took the last host down and why: crash or hang."""
        try:
            with open(self.path, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            return None, None
        if not text.strip():
            return None, None
        found = culprit(text, known)
        if found is None:
            log.warning("The last host stopped outside any plugin:\n%s", text[-2000:])
            return None, None
        return found, ("hang" if text.startswith(HANG_MARK) else "crash")

    def arm(self):
        # Truncated now, so a clean exit leaves it empty.
        self.file = open(self.path, "w", encoding="utf-8")
        faulthandler.enable(file=self.file, all_threads=True)

    def watch(self, loop, known):
        loop_thread = threading.get_ident()

        def tick():
            self.beat = time.monotonic()
            loop.call_later(BEAT_EVERY, tick)
        loop.call_soon(tick)

        def check():
            warned = False
            while True:
                time.sleep(BEAT_EVERY * 2)
                stalled = time.monotonic() - self.beat
                if stalled < HANG_AFTER:
                    warned = False
                    continue
                frame = sys._current_frames().get(loop_thread)
                if frame is None:
                    continue
                # Innermost call first, as faulthandler writes it.
                stack = "".join(reversed(traceback.format_stack(frame)))
                found = culprit(stack, known())
                if found is None:
                    if not warned:
                        log.warning("Host stalled %.0f s outside plugins:\n%s", stalled, stack)
                        warned = True
                    continue
                log.error("Plugin %s froze the host for %.0f s, restarting without it", found, stalled)
                try:
                    self.file.seek(0)
                    self.file.truncate()
                    self.file.write(HANG_MARK + "\n" + stack)
                    self.file.flush()
                finally:
                    os._exit(3)

        threading.Thread(target=check, name="komaru-watchdog", daemon=True).start()
