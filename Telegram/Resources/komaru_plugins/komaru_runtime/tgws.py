# This is the source code of KomaruGram for Desktop.
#
# The built-in tg-ws-proxy by Flowseal, kept unchanged in tgws_proxy: a local
# MTProto proxy that carries Telegram over WebSocket connections. It runs on
# its own thread and event loop, so its traffic never delays plugins.
import asyncio
import logging
import os
import socket
import threading

log = logging.getLogger("komaru.tgws")

HOST = "127.0.0.1"
START_TIMEOUT = 15


class _State:
    thread = None
    loop = None
    stop_event = None
    info = None
    error = None


def _free_port():
    with socket.socket() as probe:
        probe.bind((HOST, 0))
        return probe.getsockname()[1]


def _accepts(port):
    try:
        with socket.create_connection((HOST, port), timeout=0.2):
            return True
    except OSError:
        return False


def _serve():
    async def main():
        from tgws_proxy import tg_ws_proxy
        _State.loop = asyncio.get_running_loop()
        _State.stop_event = asyncio.Event()
        await tg_ws_proxy._run(_State.stop_event)

    try:
        asyncio.run(main())
    except Exception as error:
        _State.error = error
        log.exception("tg-ws-proxy stopped")
    _State.thread = None
    _State.info = None


async def start():
    """Starts the proxy once. Returns host, port, secret and a tg:// link."""
    if _State.info is not None:
        return _State.info
    if _State.thread is None:
        from tgws_proxy.config import proxy_config
        _State.error = None
        proxy_config.host = HOST
        proxy_config.port = _free_port()
        proxy_config.secret = os.urandom(16).hex()
        _State.thread = threading.Thread(
            target=_serve, name="komaru-tgws", daemon=True)
        _State.thread.start()
    from tgws_proxy.config import proxy_config
    for _ in range(int(START_TIMEOUT / 0.1)):
        if _State.error is not None:
            raise _State.error
        if await asyncio.to_thread(_accepts, proxy_config.port):
            _State.info = {
                "host": HOST,
                "port": proxy_config.port,
                "secret": proxy_config.secret,
                "link": "tg://proxy?server=%s&port=%d&secret=dd%s" % (
                    HOST, proxy_config.port, proxy_config.secret),
            }
            return _State.info
        await asyncio.sleep(0.1)
    raise TimeoutError("tg-ws-proxy did not start")


def info():
    return _State.info


def stop():
    if _State.loop is not None and _State.stop_event is not None:
        try:
            _State.loop.call_soon_threadsafe(_State.stop_event.set)
        except RuntimeError:
            pass
