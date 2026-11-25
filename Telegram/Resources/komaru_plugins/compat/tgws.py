# This is the source code of KomaruGram for Desktop.
#
# The built-in tg-ws-proxy for plugins: await tgws.start() gives its address
# and secret, tgws.use_in_client() points KomaruGram itself at it.
from komaru_runtime import tgws
from komaru_runtime.context import manager
from komaru_runtime.tl_bridge import ConnectionsManager

start = tgws.start
info = tgws.info
stop = tgws.stop


def use_in_client(enabled=True):
    async def apply():
        if not enabled:
            ConnectionsManager.setProxySettings(False)
            return
        proxy = await tgws.start()
        ConnectionsManager.setProxySettings(
            True, proxy["host"], proxy["port"], "", "", "dd" + proxy["secret"])
    return manager().spawn(None, apply())
