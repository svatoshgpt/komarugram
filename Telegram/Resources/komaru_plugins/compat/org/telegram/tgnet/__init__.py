# This is the source code of KomaruGram for Desktop.
#
# org.telegram.tgnet for exteraGram plugins: TLRPC objects that send_request
# turns into Telethon requests, and the proxy part of ConnectionsManager.
# Everything else is the usual do-nothing stand-in.
from komaru_runtime.tl_bridge import ConnectionsManager, TLObject, TLRPC


def __getattr__(name):
    if name.startswith("__") and name.endswith("__"):
        raise AttributeError(name)
    from komaru_runtime import stubs
    made = stubs._StubMeta(name, (stubs.Stub,), {})
    globals()[name] = made
    return made
