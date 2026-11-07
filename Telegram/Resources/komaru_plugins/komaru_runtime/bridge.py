# This is the source code of KomaruGram for Desktop.
#
# Plugins act as the account through KomaruGram's own connection. The host
# never opens one with the account's key: Telegram revokes a key it sees
# from two places at once (AUTH_KEY_DUPLICATED), which logs the user out.
# So every request a plugin makes is serialized here, sent by the client as
# its own and answered with the raw result; updates the client receives
# come back the same way.
import asyncio
import base64
import itertools

from komaru_runtime import context, protocol


class _InputPeerReader:
    def read_result(self, reader):
        return reader.tgread_object()


class BridgeSender:
    """Stands in for Telethon's MTProtoSender."""

    def __init__(self, account, updates_queue, log):
        self.account = account
        self.auth_key = None
        self._updates_queue = updates_queue
        self._log = log
        self._pending = {}
        self._ids = itertools.count(1)
        self._connected = True

    def is_connected(self):
        return self._connected

    async def connect(self, *args, **kwargs):
        self._connected = True
        return True

    async def disconnect(self):
        self._connected = False
        for future, _ in self._pending.values():
            if not future.done():
                future.set_exception(ConnectionError("The plugin host stopped"))
        self._pending.clear()

    def send(self, request, ordered=False):
        if isinstance(request, (list, tuple)):
            return [self._send_one(item) for item in request]
        return self._send_one(request)

    def _send_one(self, request):
        future = asyncio.get_running_loop().create_future()
        if not self._connected:
            future.set_exception(ConnectionError("Not connected"))
            return future
        request_id = next(self._ids)
        self._pending[request_id] = (future, request)
        record = context.current_record()
        protocol.event(
            "invoke",
            request=request_id,
            account=self.account,
            plugin=record.id if record else "",
            data=base64.b64encode(bytes(request)).decode("ascii"))
        return future

    def input_peer(self, kind, bare_id):
        """The client's InputPeer for a peer Telethon has no hash for."""
        future = asyncio.get_running_loop().create_future()
        request_id = next(self._ids)
        self._pending[request_id] = (future, _InputPeerReader())
        protocol.event(
            "input_peer",
            request=request_id,
            account=self.account,
            kind=kind,
            id=int(bare_id))
        return future

    def resolve(self, message):
        """The client's answer to one request."""
        entry = self._pending.pop(int(message.get("request", 0)), None)
        if entry is None:
            return
        future, request = entry
        if future.done():
            return
        if message.get("ok"):
            from telethon.extensions import BinaryReader
            try:
                data = base64.b64decode(message.get("data") or "")
                with BinaryReader(data) as reader:
                    future.set_result(request.read_result(reader))
            except Exception as error:
                future.set_exception(error)
            return
        from telethon.errors import rpc_message_to_error
        from telethon.tl.types import RpcError
        future.set_exception(rpc_message_to_error(
            RpcError(
                error_code=int(message.get("code") or 400),
                error_message=str(message.get("type") or "BRIDGE_ERROR")),
            request))

    def feed_updates(self, data):
        from telethon.extensions import BinaryReader
        try:
            with BinaryReader(base64.b64decode(data)) as reader:
                updates = reader.tgread_object()
        except Exception as error:
            # The client may speak a newer layer than Telethon knows.
            self._log("info", "Skipped an update Telethon cannot read: %r" % (error,))
            return
        self._updates_queue.put_nowait(updates)


def make_client(account, entry, config, device, log):
    from telethon import TelegramClient
    from telethon.sessions import MemorySession

    class BridgeClient(TelegramClient):
        """Telethon over the client bridge: no network of its own."""

        async def connect(self):
            if self._loop is None:
                self._loop = asyncio.get_running_loop()
            await self._sender.connect()
            me = await self.get_me()
            if me:
                # As _on_login, without its getDifference: the client keeps
                # the account's updates and forwards them here.
                from telethon._updates import SessionState
                from telethon.tl import functions
                self._mb_entity_cache.set_self_user(me.id, me.bot, me.access_hash)
                self._authorized = True
                state = await self(functions.updates.GetStateRequest())
                self._message_box.load(SessionState(
                    0, 0, 0, state.pts, state.qts,
                    int(state.date.timestamp()), state.seq, 0), [])
            self._updates_handle = self.loop.create_task(self._update_loop())

        async def get_input_entity(self, peer):
            try:
                return await super().get_input_entity(peer)
            except ValueError:
                from telethon import utils
                from telethon.tl import types
                if isinstance(peer, types.PeerUser):
                    kind, bare = "user", peer.user_id
                elif isinstance(peer, types.PeerChannel):
                    kind, bare = "channel", peer.channel_id
                elif isinstance(peer, types.PeerChat):
                    kind, bare = "chat", peer.chat_id
                elif isinstance(peer, int):
                    bare, made = utils.resolve_id(peer)
                    kind = {types.PeerUser: "user",
                            types.PeerChannel: "channel"}.get(made, "chat")
                else:
                    raise
                # The host session is empty; the client knows the access hash.
                try:
                    return await self._sender.input_peer(kind, bare)
                except Exception:
                    raise ValueError("Could not find the input entity for %r" % (peer,))

        async def _borrow_exported_sender(self, dc_id):
            raise ConnectionError(
                "Files on other data centers are not available to plugins yet")

        async def _switch_dc(self, new_dc):
            raise ConnectionError("Plugins cannot switch data centers")

    session = MemorySession()
    session.set_dc(int(entry["dc_id"]), "127.0.0.1", 443)
    client = BridgeClient(
        session,
        int(config["api_id"]),
        config["api_hash"],
        device_model=device.get("model") or "KomaruGram",
        system_version=device.get("system") or "",
        app_version=device.get("app") or "",
        lang_code=device.get("lang") or "en",
        system_lang_code=device.get("system_lang") or "en",
        receive_updates=True,
    )
    client._sender = BridgeSender(account, client._updates_queue, log)
    return client
