# This is the source code of KomaruGram for Desktop.
#
# One JSON object per line. The client sends {"id", "cmd", ...} and gets
# {"id", "ok", "result" | "error"} back; the host may also send
# {"event", ...} at any time. Plugins printing to stdout would corrupt the
# stream, so sys.stdout is pointed at stderr and only this module writes to
# the real stdout.
import json
import sys
import threading

for _stream in (sys.stdin, sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
_out = sys.stdout
_lock = threading.Lock()
sys.stdout = sys.stderr


def send(message):
    line = json.dumps(message, ensure_ascii=False, default=str)
    with _lock:
        _out.write(line + "\n")
        _out.flush()


def event(name, **fields):
    fields["event"] = name
    send(fields)


def start_reader(loop, dispatch):
    def read():
        for raw in sys.stdin:
            raw = raw.strip()
            if not raw:
                continue
            try:
                message = json.loads(raw)
            except ValueError:
                continue
            loop.call_soon_threadsafe(dispatch, message)
        loop.call_soon_threadsafe(dispatch, {"cmd": "shutdown"})

    thread = threading.Thread(target=read, name="komaru-stdin", daemon=True)
    thread.start()
    return thread
