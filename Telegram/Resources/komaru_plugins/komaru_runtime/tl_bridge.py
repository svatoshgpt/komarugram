# This is the source code of KomaruGram for Desktop.
#
# exteraGram plugins build requests from org.telegram.tgnet.TLRPC classes
# and peers from the Android MessagesController. Here TLRPC.TL_<name>
# objects only collect the fields a plugin sets, and send_request turns them
# into the Telethon request of the same TL name right before sending:
# TL_messages_sendScreenshotNotification becomes
# functions.messages.SendScreenshotNotificationRequest. Peers stay lazy
# until then, so a plugin can ask for one synchronously.
import inspect
import random

NAMESPACES = {
    "account", "auth", "bots", "channels", "chatlists", "contacts",
    "folders", "fragment", "help", "langpack", "messages", "payments",
    "phone", "photos", "premium", "smsjobs", "stats", "stickers", "stories",
    "updates", "upload", "users",
}


class TLObject:
    """A TLRPC object as an exteraGram plugin sees it."""

    def __init__(self, **fields):
        self.__dict__.update(fields)

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        return None

    def __repr__(self):
        return "%s(%s)" % (type(self).__name__, self.__dict__)


class _TLRPCMeta(type):
    def __getattr__(cls, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        made = type(name, (TLObject,), {})
        setattr(cls, name, made)
        return made


class TLRPC(metaclass=_TLRPCMeta):
    TLObject = TLObject


class LazyPeer:
    """A peer by id, resolved to an input entity when the request is sent.
    Android dialog ids are used: users positive, chats and channels
    negative."""

    def __init__(self, peer_id):
        self.peer_id = int(peer_id)

    async def resolve(self, client):
        from telethon.tl import types
        if self.peer_id > 0:
            return await client.get_input_entity(types.PeerUser(self.peer_id))
        bare = -self.peer_id
        if bare > 1000000000000:
            bare -= 1000000000000
        for make in (types.PeerChannel, types.PeerChat):
            try:
                return await client.get_input_entity(make(bare))
            except (ValueError, TypeError):
                continue
        raise ValueError("Unknown peer %d" % self.peer_id)


class LazyUser(TLObject):
    def __init__(self, user_id):
        super().__init__(id=int(user_id), access_hash=0)


class MessagesController:
    """The few MessagesController calls request-building plugins make."""

    def getInputPeer(self, peer_id):
        return LazyPeer(getattr(peer_id, "id", peer_id))

    getInputUser = getInputPeer
    getInputChannel = getInputPeer

    def getUser(self, user_id):
        return LazyUser(user_id)

    def getChat(self, chat_id):
        return TLObject(id=int(chat_id))

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        return lambda *args, **kwargs: None


def _upper(name):
    return name[:1].upper() + name[1:]


def _telethon_class(name):
    from telethon.tl import functions, types
    bare = name[3:] if name.startswith("TL_") else name
    candidates = []
    space, _, rest = bare.partition("_")
    if rest and space in NAMESPACES:
        candidates += [
            (getattr(functions, space, None), _upper(rest) + "Request"),
            (getattr(types, space, None), _upper(rest)),
        ]
    candidates += [(functions, _upper(bare) + "Request"), (types, _upper(bare))]
    for module, attribute in candidates:
        found = getattr(module, attribute, None) if module else None
        if inspect.isclass(found):
            return found
    raise ValueError("No Telethon type for %s" % name)


async def _convert(value, client):
    from telethon.tl.tlobject import TLObject as TelethonObject
    if isinstance(value, LazyPeer):
        return await value.resolve(client)
    if isinstance(value, TelethonObject):
        return value
    if isinstance(value, (list, tuple)):
        return [await _convert(item, client) for item in value]
    if not isinstance(value, TLObject):
        return value
    cls = _telethon_class(type(value).__name__)
    fields = value.__dict__
    kwargs = {}
    for parameter in list(inspect.signature(cls.__init__).parameters)[1:]:
        if parameter in fields:
            kwargs[parameter] = await _convert(fields[parameter], client)
        elif parameter == "reply_to" and fields.get("reply_to_msg_id") is not None:
            # Older layers had a plain reply_to_msg_id field.
            from telethon.tl.types import InputReplyToMessage
            kwargs[parameter] = InputReplyToMessage(
                reply_to_msg_id=int(fields["reply_to_msg_id"]))
        elif parameter == "random_id":
            kwargs[parameter] = random.randint(-(2 ** 63), 2 ** 63 - 1)
        else:
            kwargs[parameter] = None
    # A required reply_to that the plugin cleared the old way.
    reply_to = inspect.signature(cls.__init__).parameters.get("reply_to")
    if (reply_to is not None
            and reply_to.default is inspect.Parameter.empty
            and kwargs.get("reply_to") is None):
        from telethon.tl.types import InputReplyToMessage
        kwargs["reply_to"] = InputReplyToMessage(reply_to_msg_id=0)
    return cls(**kwargs)


async def to_telethon(request, client):
    return await _convert(request, client)


def _send_proxy(enabled, proxy_type="SOCKS5", address="", port=0,
                username="", password="", secret=""):
    from komaru_runtime import protocol
    if enabled and (not isinstance(address, str) or not address):
        return
    protocol.event(
        "proxy",
        enabled=bool(enabled),
        type=str(proxy_type or "SOCKS5").upper(),
        address=str(address or ""),
        port=int(port or 0),
        username=str(username or ""),
        password=str(password or ""),
        secret=str(secret or ""))


class ProxySettings(TLObject):
    """org.telegram.tgnet.ConnectionsManager.ProxySettings."""

    def __init__(self, address="", port=0, username="", password="",
                 secret="", type="SOCKS5", *args, **kwargs):
        super().__init__(
            address=address, port=port, username=username,
            password=password, secret=secret, type=type)


class ConnectionsManager:
    """The proxy part of ConnectionsManager: it switches the KomaruGram
    proxy, which every account uses."""

    ConnectionStateConnecting = 1
    ConnectionStateWaitingForNetwork = 2
    ConnectionStateConnected = 3
    ConnectionStateConnectingToProxy = 4
    ConnectionStateUpdating = 5
    ProxySettings = ProxySettings

    @staticmethod
    def setProxySettings(enabled, *args):
        if not enabled:
            _send_proxy(False)
            return
        if len(args) == 1 and args[0] is not None:
            settings = args[0]
            secret = str(getattr(settings, "secret", "") or "")
            kind = getattr(settings, "type", None) or ("MTPROTO" if secret else "SOCKS5")
            _send_proxy(
                True, kind,
                getattr(settings, "address", ""),
                getattr(settings, "port", 0),
                getattr(settings, "username", ""),
                getattr(settings, "password", ""),
                secret)
            return
        address, port, username, password, secret = (list(args) + [""] * 5)[:5]
        _send_proxy(
            True, "MTPROTO" if secret else "SOCKS5",
            address, port, username, password, secret)

    @staticmethod
    def getInstance(*args):
        return ConnectionsManager()

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        return lambda *args, **kwargs: None
