# This is the source code of KomaruGram for Desktop.
#
# exteraGram client helpers over Telethon. Sending goes through the account
# the current hook belongs to, or the first account outside a hook. Android
# controllers do not exist on desktop and come back as None.
import threading

from komaru_runtime import context

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
    return context.manager().spawn(context.current_record(), coroutine)


def run_on_queue(fn, queue_name=PLUGINS_QUEUE, delay=0):
    record = context.current_record()
    account = context.hook_account()

    def work():
        with context.running(record, account):
            try:
                fn()
            except Exception:
                if record:
                    record.log_exception("run_on_queue")
    timer = threading.Timer(max(delay, 0) / 1000.0, work)
    timer.daemon = True
    timer.start()


def get_queue_by_name(name):
    return None


def get_selected_account():
    manager = context.manager()
    indices = manager.accounts.indices() if manager else []
    return indices[0] if indices else 0


def get_hook_account():
    return context.hook_account()


def _parse_mode(parse_mode):
    if not parse_mode:
        return None
    return "html" if str(parse_mode).lower() == "html" else "md"


def send_message(params, parse_mode=None, account=None):
    params = dict(params or {})
    peer = params.get("peer")
    text = params.get("message") or params.get("caption") or ""
    path = params.get("path")
    reply_to = params.get("replyToMsg")
    schedule = params.get("scheduleDate")
    silent = params.get("notify") is False
    mode = _parse_mode(parse_mode)
    index = _account(account)

    async def send():
        client = await context.manager().accounts.telethon(index)
        kwargs = {"reply_to": reply_to, "parse_mode": mode, "silent": silent}
        if schedule:
            import datetime
            kwargs["schedule"] = datetime.datetime.fromtimestamp(schedule)
        if path:
            await client.send_file(peer, path, caption=text, **kwargs)
        else:
            await client.send_message(peer, text, **kwargs)
    return _schedule(send())


def send_text(peer_id, text, replyToMsg=None, parse_mode=None, account=None, **kwargs):
    return send_message(
        {"peer": peer_id, "message": text, "replyToMsg": replyToMsg},
        parse_mode=parse_mode,
        account=account)


def _send_file(peer_id, path, caption=None, replyToMsg=None, parse_mode=None, account=None, **kwargs):
    return send_message(
        {"peer": peer_id, "path": path, "caption": caption or "", "replyToMsg": replyToMsg},
        parse_mode=parse_mode,
        account=account)


send_photo = _send_file
send_document = _send_file
send_video = _send_file
send_audio = _send_file


def edit_message(message_obj, text=None, file_path=None, parse_mode=None, account=None, **kwargs):
    index = _account(account)
    mode = _parse_mode(parse_mode)

    async def edit():
        client = await context.manager().accounts.telethon(index)
        await client.edit_message(
            getattr(message_obj, "chat_id", None) or getattr(message_obj, "peer_id", None),
            getattr(message_obj, "id", message_obj),
            text,
            file=file_path,
            parse_mode=mode)
    return _schedule(edit())


def send_request(request, fn=None, account=None):
    """Sends a Telethon request, or a TLRPC one built the exteraGram way,
    and calls fn(result, error)."""
    from komaru_runtime import tl_bridge
    index = _account(account)
    record = context.current_record()
    callback = getattr(fn, "fn", fn)

    async def call():
        client = await context.manager().accounts.telethon(index)
        try:
            prepared = await tl_bridge.to_telethon(request, client)
            result, error = await client(prepared), None
        except Exception as e:
            result, error = None, e
            if record:
                record.manager._log("warning", "%s: send_request failed: %r" % (record.id, e))
        if callback:
            with context.running(record, index):
                try:
                    callback(result, error)
                except Exception:
                    if record:
                        record.log_exception("send_request callback")
    _schedule(call())
    return 0


class AccountClient:
    def __init__(self, account):
        self.account = account

    def __getattr__(self, name):
        target = globals().get(name)
        if not callable(target):
            raise AttributeError(name)

        def bound(*args, **kwargs):
            kwargs.setdefault("account", self.account)
            return target(*args, **kwargs)
        return bound

    async def telethon(self):
        return await context.manager().accounts.telethon(self.account)

    async def kurigram(self):
        return await context.manager().accounts.kurigram(self.account)


def get_client(account=None):
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
    def __init__(self, fn=None):
        self.fn = fn


class NotificationCenterDelegate:
    def didReceivedNotification(self, id, account, args):
        pass
