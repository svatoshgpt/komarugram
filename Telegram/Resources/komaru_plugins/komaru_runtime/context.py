# This is the source code of KomaruGram for Desktop.
#
# Which plugin is running right now and which account it acts for. Helpers
# such as client_utils.send_text use this to pick the account the current
# hook belongs to, as exteraGram does.
import contextlib
import contextvars

_manager = None
_record = contextvars.ContextVar("komaru_plugin_record", default=None)
_account = contextvars.ContextVar("komaru_hook_account", default=None)


def set_manager(manager):
    global _manager
    _manager = manager


def manager():
    return _manager


def current_record():
    return _record.get()


def hook_account():
    return _account.get()


@contextlib.contextmanager
def running(record, account=None):
    record_token = _record.set(record)
    account_token = _account.set(account)
    try:
        yield
    finally:
        _account.reset(account_token)
        _record.reset(record_token)


def record_for_module(name):
    """Finds the plugin that owns a module by its komaru_plugins.<id>
    prefix, for code that runs outside a hook."""
    if not name or not name.startswith("komaru_plugins."):
        return None
    plugin_id = name.split(".")[1]
    return _manager.records.get(plugin_id) if _manager else None
