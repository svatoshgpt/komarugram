# This is the source code of KomaruGram for Desktop.
#
# exteraGram client helpers (SDK 1.4.6.5) over Telethon. Sending goes through
# the account the current hook belongs to, or the first account outside a
# hook. Android controllers do not exist on desktop and come back as None.
import contextlib
import threading
from typing import Any, Optional

from extera_utils.lifecycle import bind_request, bind_task, capture_scope
from komaru_runtime import context

STAGE_QUEUE = "stageQueue"
GLOBAL_QUEUE = "globalQueue"
CACHE_CLEAR_QUEUE = "cacheClearQueue"
SEARCH_QUEUE = "searchQueue"
PHONE_BOOK_QUEUE = "phoneBookQueue"
THEME_QUEUE = "themeQueue"
EXTERNAL_NETWORK_QUEUE = "externalNetworkQueue"
PLUGINS_QUEUE = "pluginsQueue"


def _account(account):
    if account is not None:
        return account
    hook = context.hook_account()
    return hook if hook is not None else get_selected_account()


def _schedule(coroutine):
    manager = context.manager()
    if manager is None:
        return None
    return manager.spawn(context.current_record(), coroutine)


@contextlib.contextmanager
def account_scope(account: Optional[int]):
    with context.running(context.current_record(), account):
        yield


def run_on_queue(fn, queue_name: str = PLUGINS_QUEUE, delay: int = 0):
    record = context.current_record()
    account = context.hook_account()

    def wrapped():
        with context.running(record, account):
            try:
                fn()
            except Exception:
                if record:
                    record.log_exception("run_on_queue")

    callback = bind_task(wrapped, once=True, scope=capture_scope(fn))
    if callback.cancelled:
        return callback

    timer = threading.Timer(max(delay, 0) / 1000.0, callback)
    timer.daemon = True
    callback.set_cancel(timer.cancel)
    timer.start()
    return callback


def get_queue_by_name(name: str):
    return None


def get_selected_account() -> int:
    manager = context.manager()
    indices = manager.accounts.indices() if manager else []
    return indices[0] if indices else 0


def get_hook_account() -> Optional[int]:
    return context.hook_account()


def _parse_mode(parse_mode):
    if not parse_mode:
        return None
    lowered = str(parse_mode).lower()
    if lowered == "html":
        return "html"
    if lowered in ("markdown", "md"):
        return "md"
    raise ValueError(f"Invalid parse mode: {parse_mode}. Must be 'html' or 'markdown'.")


def _extract_reply_id(reply_to):
    if reply_to is None:
        return None
    if isinstance(reply_to, int):
        return reply_to
    if hasattr(reply_to, "getId") and callable(reply_to.getId):
        try:
            return int(reply_to.getId())
        except Exception:
            pass
    return getattr(reply_to, "id", reply_to)


def send_message(params: dict, parse_mode: Optional[str] = None, account: Optional[int] = None):
    params = dict(params or {})
    peer = params.get("peer")
    text = params.get("message") or params.get("caption") or ""
    path = params.get("path")
    reply_to = _extract_reply_id(params.get("replyToMsg"))
    schedule = params.get("scheduleDate")
    silent = params.get("notify") is False
    spoiler = bool(params.get("hasMediaSpoilers", False))
    mode = _parse_mode(parse_mode)
    index = _account(account)

    async def send():
        client = await context.manager().accounts.telethon(index)
        kwargs = {"reply_to": reply_to, "parse_mode": mode, "silent": silent}
        if schedule:
            import datetime
            kwargs["schedule"] = datetime.datetime.fromtimestamp(schedule)
        if path:
            if spoiler:
                kwargs["spoiler"] = True
            await client.send_file(peer, path, caption=text, **kwargs)
        else:
            await client.send_message(peer, text, **kwargs)

    return _schedule(send())


def send_text(
    peer: int,
    text: str,
    replyToMsg=None,
    parse_mode: Optional[str] = None,
    account: Optional[int] = None,
    **kwargs,
):
    params = {"peer": peer, "message": text}
    if replyToMsg is not None:
        params["replyToMsg"] = replyToMsg
    params.update(kwargs)
    return send_message(params, parse_mode=parse_mode, account=account)


def send_photo(
    peer: int,
    file_path: str,
    caption: str = "",
    high_quality: bool = False,
    *,
    account: Optional[int] = None,
    parse_mode: Optional[str] = None,
    **kwargs,
):
    params = {
        "peer": peer,
        "path": file_path,
        "caption": caption or "",
        "sendingHighQuality": high_quality,
    }
    params.update(kwargs)
    return send_message(params, parse_mode=parse_mode, account=account)


def _send_file(
    peer: int,
    file_path: str,
    caption: str = "",
    *,
    account: Optional[int] = None,
    parse_mode: Optional[str] = None,
    **kwargs,
):
    params = {"peer": peer, "path": file_path, "caption": caption or ""}
    params.update(kwargs)
    return send_message(params, parse_mode=parse_mode, account=account)


send_document = _send_file
send_video = _send_file
send_audio = _send_file


def edit_message(
    message_obj: Any,
    text: Optional[str] = None,
    file_path: Optional[str] = None,
    with_spoiler: bool = False,
    *,
    account: Optional[int] = None,
    parse_mode: Optional[str] = None,
    **kwargs,
):
    index = _account(account)
    mode = _parse_mode(parse_mode)

    async def edit():
        client = await context.manager().accounts.telethon(index)
        peer = getattr(message_obj, "chat_id", None) or getattr(message_obj, "peer_id", None)
        if peer is None and hasattr(message_obj, "getDialogId") and callable(message_obj.getDialogId):
            peer = message_obj.getDialogId()
        msg_id = _extract_reply_id(message_obj)
        await client.edit_message(peer, msg_id, text, file=file_path, parse_mode=mode)

    return _schedule(edit())


def send_request(request: Any, fn=None, account: Optional[int] = None) -> int:
    """Sends a Telethon request, or a TLRPC one built the exteraGram way,
    and calls fn(result, error)."""
    from komaru_runtime import tl_bridge
    index = _account(account)
    record = context.current_record()
    raw_cb = getattr(fn, "fn", fn)
    callback = bind_request(raw_cb) if callable(raw_cb) else None
    if callback is not None and callback.cancelled:
        return 0

    async def call():
        client = await context.manager().accounts.telethon(index)
        try:
            with context.running(record, index):
                prepared = await tl_bridge.to_telethon(request, client)
                result, error = await client(prepared), None
        except Exception as e:
            result, error = None, e
            if record:
                record.manager._log("warning", "%s: send_request failed: %r" % (record.id, e))
                from komaru_runtime import protocol
                protocol.event(
                    "toast",
                    text="%s: %s" % (
                        record.meta.get("name", record.id),
                        str(e) or e.__class__.__name__,
                    ),
                )
        if callback is not None:
            with context.running(record, index):
                try:
                    callback(result, error)
                except Exception:
                    if record:
                        record.log_exception("send_request callback")

    _schedule(call())
    return 0


class AccountClient:
    __slots__ = ("account",)

    def __init__(self, account: int):
        if account is None:
            raise ValueError(
                "AccountClient requires an account; use the module level helpers for the selected one"
            )
        self.account = int(account)

    def __repr__(self):
        return f"AccountClient(account={self.account})"

    def __eq__(self, other):
        return isinstance(other, AccountClient) and other.account == self.account

    def __hash__(self):
        return hash((AccountClient, self.account))

    def get_account_instance(self):
        return get_account_instance(self.account)

    def get_messages_controller(self):
        return get_messages_controller(self.account)

    def get_contacts_controller(self):
        return get_contacts_controller(self.account)

    def get_media_data_controller(self):
        return get_media_data_controller(self.account)

    def get_connections_manager(self):
        return get_connections_manager(self.account)

    def get_location_controller(self):
        return get_location_controller(self.account)

    def get_notifications_controller(self):
        return get_notifications_controller(self.account)

    def get_messages_storage(self):
        return get_messages_storage(self.account)

    def get_send_messages_helper(self):
        return get_send_messages_helper(self.account)

    def get_file_loader(self):
        return get_file_loader(self.account)

    def get_secret_chat_helper(self):
        return get_secret_chat_helper(self.account)

    def get_download_controller(self):
        return get_download_controller(self.account)

    def get_notifications_settings(self):
        return get_notifications_settings(self.account)

    def get_notification_center(self):
        return get_notification_center(self.account)

    def get_user_config(self):
        return get_user_config(self.account)

    def send_request(self, request: Any, fn=None) -> int:
        return send_request(request, fn, account=self.account)

    def send_message(self, params: dict, parse_mode: Optional[str] = None):
        return send_message(params, parse_mode, account=self.account)

    def send_text(self, peer: int, text: str, *, parse_mode: Optional[str] = None, **kwargs):
        return send_text(peer, text, account=self.account, parse_mode=parse_mode, **kwargs)

    def send_photo(
        self,
        peer: int,
        file_path: str,
        caption: str = "",
        high_quality: bool = False,
        *,
        parse_mode: Optional[str] = None,
        **kwargs,
    ):
        return send_photo(
            peer, file_path, caption, high_quality,
            account=self.account, parse_mode=parse_mode, **kwargs,
        )

    def send_document(
        self, peer: int, file_path: str, caption: str = "", *, parse_mode: Optional[str] = None, **kwargs
    ):
        return send_document(
            peer, file_path, caption, account=self.account, parse_mode=parse_mode, **kwargs
        )

    def send_video(
        self, peer: int, file_path: str, caption: str = "", *, parse_mode: Optional[str] = None, **kwargs
    ):
        return send_video(
            peer, file_path, caption, account=self.account, parse_mode=parse_mode, **kwargs
        )

    def send_audio(
        self, peer: int, file_path: str, caption: str = "", *, parse_mode: Optional[str] = None, **kwargs
    ):
        return send_audio(
            peer, file_path, caption, account=self.account, parse_mode=parse_mode, **kwargs
        )

    def edit_message(
        self,
        message_obj: Any,
        text: Optional[str] = None,
        file_path: Optional[str] = None,
        with_spoiler: bool = False,
        *,
        parse_mode: Optional[str] = None,
        **kwargs,
    ):
        return edit_message(
            message_obj, text, file_path, with_spoiler,
            account=self.account, parse_mode=parse_mode, **kwargs,
        )

    async def telethon(self):
        return await context.manager().accounts.telethon(self.account)

    async def kurigram(self):
        return await context.manager().accounts.kurigram(self.account)


def get_client(account: Optional[int] = None) -> AccountClient:
    return AccountClient(_account(account))


def _none(*args, **kwargs):
    return None


get_last_fragment = _none
get_account_instance = _none


def get_messages_controller(*args, **kwargs):
    from komaru_runtime import tl_bridge
    return tl_bridge.MessagesController()


get_contacts_controller = _none
get_media_data_controller = _none


def get_connections_manager(*args, **kwargs):
    from komaru_runtime import tl_bridge
    return tl_bridge.ConnectionsManager()


get_location_controller = _none
get_notifications_controller = _none
get_messages_storage = _none
get_send_messages_helper = _none
get_file_loader = _none
get_secret_chat_helper = _none
get_download_controller = _none
get_notifications_settings = _none
get_notification_center = _none
get_media_controller = _none
get_user_config = _none


class RequestCallback:
    def __init__(self, fn=None, account: Optional[int] = None):
        self.fn = fn
        self._account = account

    def run(self, response, error):
        if callable(self.fn):
            with account_scope(self._account):
                self.fn(response, error)


class NotificationCenterDelegate:
    def didReceivedNotification(self, id, account, args):
        pass
