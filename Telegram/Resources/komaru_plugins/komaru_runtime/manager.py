# This is the source code of KomaruGram for Desktop.
#
# The plugin manager inside the host: installs plugins into
# <data>/installed/<id>, keeps their enabled state and settings in
# <data>/state.json, loads Python plugins as komaru_plugins.<id> modules and
# runs Go plugins as child processes.
import asyncio
import importlib.util
import json
import logging
import os
import shutil
import subprocess
import sys
import traceback
import types
import zipfile

from komaru_runtime import HOST_VERSION, context, meta as metamod, protocol, stubs
from komaru_runtime.accounts import Accounts
from komaru_runtime.goplugin import GoPlugin

log = logging.getLogger("komaru.host")


def _lower_first(name):
    return name[:1].lower() + name[1:]


class Record:
    def __init__(self, manager, plugin_id, directory, info):
        self.manager = manager
        self.id = plugin_id
        self.dir = directory
        self.info = info
        self.meta = info["meta"]
        self.instance = None
        self.go = None
        self.error = ""
        self.send_hook = False
        self.update_hooks = []
        self.tasks = set()
        self.telethon_handlers = []
        self.sys_paths = []
        self.pages = {}
        self.menu_items = {}
        self._unsupported = set()

    @property
    def enabled(self):
        return bool(self.manager.state["enabled"].get(self.id))

    @property
    def settings(self):
        return self.manager.state["settings"].setdefault(self.id, {})

    @property
    def runtime(self):
        return self.meta.get("runtime", "python")

    @property
    def loaded(self):
        return self.instance is not None or self.go is not None

    def assets_dir(self):
        refmap = self.meta.get("refmap", {})
        name = refmap.get("assets") or "assets"
        path = os.path.join(self.dir, name)
        return path if os.path.isdir(path) else ""

    def strings(self):
        refmap = self.meta.get("refmap", {})
        name = refmap.get("strings")
        if not name:
            return {}
        path = os.path.join(self.dir, name)
        candidates = []
        if os.path.isdir(path):
            for lang in ("en", "ru"):
                for ext in (".yml", ".yaml", ".json"):
                    candidates.append(os.path.join(path, "strings_" + lang + ext))
        else:
            candidates.append(path)
        result = {}
        for candidate in reversed(candidates):
            if os.path.isfile(candidate):
                with open(candidate, "rb") as f:
                    result.update(metamod.parse_mapping(candidate, f.read()))
        return result

    def unsupported(self, what):
        if what not in self._unsupported:
            self._unsupported.add(what)
            logging.getLogger("plugin." + self.id).info(
                "%s is an Android feature and is ignored on desktop", what)

    def log_exception(self, where):
        text = traceback.format_exc()
        logging.getLogger("plugin." + self.id).error("%s failed:\n%s", where, text)
        protocol.event("log", level="error", plugin=self.id,
                       text=text.strip().splitlines()[-1])

    def has_settings(self):
        if self.go is not None:
            return bool(self.meta.get("settings"))
        if self.instance is None:
            return False
        from base_plugin import BasePlugin
        return type(self.instance).create_settings is not BasePlugin.create_settings

    def describe(self):
        return {
            "id": self.id,
            "name": self.meta.get("name", self.id),
            "description": self.meta.get("description", ""),
            "author": self.meta.get("author", ""),
            "version": self.meta.get("version", ""),
            "icon": self.meta.get("icon", ""),
            "format": self.info.get("format", ""),
            "runtime": self.runtime,
            "enabled": self.enabled,
            "loaded": self.loaded,
            "error": self.error,
            "has_settings": self.has_settings(),
            "send_hook": self.send_hook or bool(self.go and self.go.send_hook),
            "menu": self.describe_menu(),
        }

    def describe_menu(self):
        result = []
        items = sorted(
            self.menu_items.values(),
            key=lambda item: -int(getattr(item, "priority", 0) or 0))
        for item in items:
            kind = getattr(item.menu_type, "value", item.menu_type)
            result.append({
                "id": item.item_id,
                "type": str(kind or ""),
                "text": str(item.text or item.item_id),
                "subtext": str(item.subtext or ""),
            })
        return result


class Manager:
    def __init__(self, data_dir):
        self.data_dir = data_dir
        self.installed_dir = os.path.join(data_dir, "installed")
        self.libs_dir = os.path.join(data_dir, "libs")
        self.state_path = os.path.join(data_dir, "state.json")
        self.loop = asyncio.get_running_loop()
        self.accounts = Accounts(self._log)
        self.records = {}
        self.state = {"enabled": {}, "settings": {}}
        self._update_clients = set()
        self._changed_pending = False
        os.makedirs(self.installed_dir, exist_ok=True)
        os.makedirs(self.libs_dir, exist_ok=True)

    def _log(self, level, text):
        getattr(log, level, log.info)(text)

    # State.
    def load_state(self):
        try:
            with open(self.state_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.state["enabled"] = dict(data.get("enabled", {}))
            self.state["settings"] = dict(data.get("settings", {}))
        except (OSError, ValueError):
            pass

    def save_state(self):
        temp = self.state_path + ".tmp"
        with open(temp, "w", encoding="utf-8") as f:
            json.dump(self.state, f, ensure_ascii=False, indent=1, default=str)
        os.replace(temp, self.state_path)

    def notify_changed(self):
        if self._changed_pending:
            return
        self._changed_pending = True

        def fire():
            self._changed_pending = False
            protocol.event("changed")
        self.loop.call_soon_threadsafe(fire)

    def notify_settings(self, record):
        protocol.event("settings_changed", plugin=record.id)

    # Tasks.
    def spawn(self, record, coroutine):
        async def guarded():
            try:
                if record is None:
                    return await coroutine
                with context.running(record, context.hook_account()):
                    return await coroutine
            except asyncio.CancelledError:
                raise
            except Exception:
                if record is not None:
                    record.log_exception("task")
                else:
                    log.exception("task failed")

        def start():
            task = self.loop.create_task(guarded())
            if record is not None:
                record.tasks.add(task)
                task.add_done_callback(record.tasks.discard)
            return task

        try:
            running = asyncio.get_running_loop()
        except RuntimeError:
            running = None
        if running is self.loop:
            return start()
        future = asyncio.run_coroutine_threadsafe(guarded(), self.loop)
        return future

    # Discovery.
    def scan(self):
        for name in sorted(os.listdir(self.installed_dir)):
            directory = os.path.join(self.installed_dir, name)
            info_path = os.path.join(directory, "komaru.json")
            if not os.path.isfile(info_path):
                continue
            try:
                with open(info_path, "r", encoding="utf-8") as f:
                    info = json.load(f)
                self.records[info["meta"]["id"]] = Record(
                    self, info["meta"]["id"], directory, info)
            except (OSError, ValueError, KeyError):
                log.exception("Broken plugin folder %s", directory)

    # Install.
    def install(self, path):
        if metamod.is_archive(path):
            meta = metamod.from_archive(path)
            fmt = "Go" if meta["runtime"] == "go" else "Elyx"
        else:
            fallback = None
            if path.lower().endswith(".py"):
                fallback = metamod.fallback_id(path)
            with open(path, "r", encoding="utf-8-sig") as f:
                meta = metamod.from_source(f.read(), fallback)
            fmt = "exteraGram" if path.lower().endswith(".plugin") else "Python"
        plugin_id = meta["id"]
        old = self.records.get(plugin_id)
        was_enabled = bool(old and old.enabled)
        if old is not None:
            self.unload(old)
        directory = os.path.join(self.installed_dir, plugin_id)
        shutil.rmtree(directory, ignore_errors=True)
        os.makedirs(directory)
        if fmt in ("Elyx", "Go"):
            self._extract(path, directory)
        else:
            shutil.copyfile(path, os.path.join(directory, "plugin.py"))
            meta["main"] = "plugin.py"
        info = {"format": fmt, "source": os.path.basename(path), "meta": meta}
        with open(os.path.join(directory, "komaru.json"), "w", encoding="utf-8") as f:
            json.dump(info, f, ensure_ascii=False, indent=1, default=str)
        record = Record(self, plugin_id, directory, info)
        self.records[plugin_id] = record
        if was_enabled:
            self.load(record)
        self.notify_changed()
        return record

    @staticmethod
    def _extract(path, directory):
        root = os.path.normpath(directory)
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                target = os.path.normpath(os.path.join(root, member.filename))
                if target != root and not target.startswith(root + os.sep):
                    raise metamod.MetaError("Archive entry leaves the plugin folder")
            archive.extractall(root)

    def uninstall(self, record):
        self.unload(record)
        shutil.rmtree(record.dir, ignore_errors=True)
        shutil.rmtree(os.path.join(self.libs_dir, record.id), ignore_errors=True)
        self.records.pop(record.id, None)
        self.state["enabled"].pop(record.id, None)
        self.state["settings"].pop(record.id, None)
        self.save_state()
        self.notify_changed()

    # Dependencies.
    def _pip_command(self):
        wheel = os.path.join(self.data_dir, "runtime", "pip.whl")
        if os.path.isfile(wheel):
            return [sys.executable, os.path.join(wheel, "pip")]
        return [sys.executable, "-m", "pip"]

    def _prepare_dependencies(self, record):
        libs = os.path.join(self.libs_dir, record.id)
        paths = []
        wheels_name = record.meta.get("refmap", {}).get("wheels")
        if wheels_name:
            wheels_dir = os.path.join(record.dir, wheels_name)
            wanted = set()
            if os.path.isdir(wheels_dir):
                for name in sorted(os.listdir(wheels_dir)):
                    if not name.endswith(".whl"):
                        continue
                    stem = name[:-4]
                    wanted.add(stem)
                    target = os.path.join(libs, "wheels", stem)
                    if not os.path.isdir(target):
                        self._extract(os.path.join(wheels_dir, name), target)
                    paths.append(target)
            existing = os.path.join(libs, "wheels")
            if os.path.isdir(existing):
                for stem in os.listdir(existing):
                    if stem not in wanted:
                        shutil.rmtree(os.path.join(existing, stem), ignore_errors=True)
        requirements = record.meta.get("requirements") or []
        if isinstance(requirements, str):
            requirements = [r.strip() for r in requirements.split(",") if r.strip()]
        if requirements:
            target = os.path.join(libs, "pip")
            marker = os.path.join(target, ".komaru-requirements")
            wanted = "\n".join(sorted(requirements))
            current = ""
            if os.path.isfile(marker):
                with open(marker, "r", encoding="utf-8") as f:
                    current = f.read()
            if current != wanted:
                os.makedirs(target, exist_ok=True)
                command = self._pip_command() + [
                    "install", "--disable-pip-version-check",
                    "--no-warn-script-location", "--upgrade",
                    "--target", target] + requirements
                result = subprocess.run(
                    command, capture_output=True, text=True,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                if result.returncode != 0:
                    raise RuntimeError("pip failed: " + (result.stderr or result.stdout)[-500:])
                with open(marker, "w", encoding="utf-8") as f:
                    f.write(wanted)
            paths.append(target)
        for path in paths:
            if path not in sys.path:
                sys.path.insert(0, path)
        record.sys_paths = paths

    # Load and unload.
    def load(self, record):
        if record.loaded:
            return
        record.error = ""
        try:
            if record.runtime == "go":
                record.go = GoPlugin(self, record)
                record.go.start()
            else:
                self._load_python(record)
        except Exception as e:
            record.instance = None
            record.go = None
            record.error = str(e) or e.__class__.__name__
            record.log_exception("load")
        self.notify_changed()

    def _load_python(self, record):
        self._prepare_dependencies(record)
        package = "komaru_plugins." + record.id
        main = os.path.join(record.dir, record.meta.get("main") or "plugin.py")
        if record.info.get("format") in ("Elyx",):
            parent = types.ModuleType(package)
            parent.__path__ = [os.path.dirname(main)]
            parent.__package__ = package
            sys.modules[package] = parent
            name = package + "." + os.path.splitext(os.path.basename(main))[0]
        else:
            name = package
        spec = importlib.util.spec_from_file_location(name, main)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        with context.running(record):
            spec.loader.exec_module(module)
            plugin_class = self._find_plugin_class(module)
            if plugin_class is None:
                raise RuntimeError("The plugin has no BasePlugin subclass")
            instance = plugin_class()
            record.instance = instance
            instance.on_plugin_load()
            from base_plugin import AppEvent
            instance.on_app_event(AppEvent.START)

    @staticmethod
    def _find_plugin_class(module):
        # exteraGram metadata assigns the module's own __name__, so classes
        # defined in the plugin carry the display name as their module.
        from base_plugin import BasePlugin
        found = None
        for value in vars(module).values():
            if (not isinstance(value, type)
                    or not issubclass(value, BasePlugin)
                    or value.__module__ in ("base_plugin", "komaru")):
                continue
            if value.__module__ == vars(module).get("__name__"):
                return value
            found = found or value
        return found

    def unload(self, record):
        if record.instance is not None:
            with context.running(record):
                try:
                    from base_plugin import AppEvent
                    record.instance.on_app_event(AppEvent.STOP)
                    record.instance.on_plugin_unload()
                except Exception:
                    record.log_exception("unload")
        if record.go is not None:
            record.go.stop()
        for task in list(record.tasks):
            task.cancel()
        for client, callback, builder in record.telethon_handlers:
            try:
                client.remove_event_handler(callback, builder)
            except Exception:
                pass
        record.telethon_handlers.clear()
        record.update_hooks.clear()
        record.menu_items.clear()
        record.send_hook = False
        record.instance = None
        record.go = None
        record.pages.clear()
        prefix = "komaru_plugins." + record.id
        for name in list(sys.modules):
            if name == prefix or name.startswith(prefix + "."):
                del sys.modules[name]
        for path in record.sys_paths:
            if path in sys.path:
                sys.path.remove(path)
        record.sys_paths = []
        self.notify_changed()

    def set_enabled(self, record, enabled):
        self.state["enabled"][record.id] = bool(enabled)
        self.save_state()
        if enabled:
            self.load(record)
        else:
            self.unload(record)
        self.notify_changed()

    # Settings pages.
    def _page_items(self, record, page):
        if record.go is not None:
            return record.go.settings_items()
        nodes = record.instance.create_settings() if record.instance else []
        for part in [p for p in page.split("/") if p]:
            node = nodes[int(part)]
            nodes = node.create_sub_fragment() or []
        return nodes or []

    def settings_page(self, record, page=""):
        if not record.loaded:
            return []
        with context.running(record):
            nodes = self._page_items(record, page)
        record.pages[page] = nodes
        if record.go is not None:
            return nodes
        return [self._describe_node(record, index, node) for index, node in enumerate(nodes)]

    def _describe_node(self, record, index, node):
        kind = type(node).__name__.lower()
        item = {"index": index, "type": kind}
        for attr in ("text", "subtext", "key", "hint", "icon"):
            value = getattr(node, attr, None)
            if value is not None:
                item[attr] = str(value)
        item["accent"] = bool(getattr(node, "accent", False))
        item["red"] = bool(getattr(node, "red", False))
        item["clickable"] = bool(getattr(node, "on_click", None)
                                 or getattr(node, "create_sub_fragment", None))
        item["opens_page"] = bool(getattr(node, "create_sub_fragment", None))
        if kind == "selector":
            item["items"] = [str(x) for x in getattr(node, "items", [])]
        key = getattr(node, "key", None)
        if key:
            item["value"] = record.settings.get(key, getattr(node, "default", None))
        return item

    def set_setting(self, record, page, index, value):
        if record.go is not None:
            record.go.set_setting(index, value)
            return
        node = record.pages.get(page, [])[index]
        key = getattr(node, "key", None)
        if key:
            record.settings[key] = value
            self.save_state()
        callback = getattr(node, "on_change", None)
        if callback:
            with context.running(record):
                try:
                    callback(value)
                except Exception:
                    record.log_exception("on_change")

    def click(self, record, page, index):
        node = record.pages.get(page, [])[index]
        callback = getattr(node, "on_click", None)
        if callback:
            with context.running(record):
                try:
                    callback(None)
                except Exception:
                    record.log_exception("on_click")
        if getattr(node, "create_sub_fragment", None):
            return (page + "/" if page else "") + str(index)
        return None

    def menu_click(self, record, item_id, message):
        item = record.menu_items.get(item_id)
        if item is None or not callable(item.on_click):
            raise KeyError("Unknown menu item")
        account = message.get("account")
        dialog_id = int(message.get("dialog_id") or 0)
        from komaru_runtime.tl_bridge import TLObject
        menu_context = {"account": account, "dialog_id": dialog_id}
        if dialog_id > 0:
            user = TLObject(id=dialog_id, access_hash=0)
            menu_context.update(user=user, userId=dialog_id)
        elif dialog_id < 0:
            chat = TLObject(id=-dialog_id)
            menu_context.update(chat=chat, chatId=-dialog_id)
        with context.running(record, account):
            try:
                item.on_click(menu_context)
            except Exception:
                record.log_exception("menu item " + item_id)
        return True

    # Hooks.
    async def transform_outgoing(self, account, peer, text, reply_to):
        from base_plugin import HookStrategy
        for record in list(self.records.values()):
            if record.go is not None and record.go.send_hook:
                action, text = await record.go.transform(account, peer, text)
                if action == "cancel":
                    return {"action": "cancel"}
                continue
            if record.instance is None or not record.send_hook:
                continue
            params = types.SimpleNamespace(
                message=text, peer=peer, replyToMsg=reply_to, caption=None,
                entities=[], account=account)
            with context.running(record, account):
                try:
                    result = record.instance.on_send_message_hook(account, params)
                except Exception:
                    record.log_exception("on_send_message_hook")
                    continue
            strategy = getattr(result, "strategy", HookStrategy.DEFAULT)
            if strategy == HookStrategy.CANCEL:
                return {"action": "cancel"}
            if strategy in (HookStrategy.MODIFY, HookStrategy.MODIFY_FINAL):
                changed = getattr(getattr(result, "params", None) or params, "message", text)
                if isinstance(changed, str):
                    text = changed
                if strategy == HookStrategy.MODIFY_FINAL:
                    break
        return {"action": "modify", "text": text}

    def ensure_update_listener(self, record):
        async def attach():
            from telethon import events
            await self.accounts.wait_ready()
            for index in self.accounts.indices():
                client = await self.accounts.telethon(index)
                if id(client) in self._update_clients:
                    continue
                self._update_clients.add(id(client))

                async def handler(update, index=index):
                    self.dispatch_update(index, update)
                client.add_event_handler(handler, events.Raw)
        self.spawn(None, attach())

    def dispatch_update(self, account, update):
        name = "TL_" + _lower_first(type(update).__name__)
        for record in list(self.records.values()):
            if record.go is not None and record.go.wants_updates:
                if name in ("TL_updateNewMessage", "TL_updateNewChannelMessage"):
                    record.go.forward_update(account, update)
                continue
            if record.instance is None:
                continue
            for hook, substring in record.update_hooks:
                if hook == name or (substring and hook in name):
                    with context.running(record, account):
                        try:
                            record.instance.on_update_hook(name, account, update)
                        except Exception:
                            record.log_exception("on_update_hook")
                    break

    # Commands.
    async def handle(self, message):
        command = message.get("cmd")
        record = self.records.get(message.get("plugin", ""))
        if command == "hello":
            return {"version": HOST_VERSION, "python": sys.version.split()[0]}
        if command == "list":
            return [r.describe() for r in self.records.values()]
        if command == "install":
            return self.install(message["path"]).describe()
        if command == "accounts":
            await self.accounts.configure(message)
            self._update_clients.clear()
            for r in self.records.values():
                if r.go is not None:
                    r.go.send_accounts()
                if r.update_hooks or (r.go is not None and r.go.wants_updates):
                    self.ensure_update_listener(r)
            return True
        if command == "transform_outgoing":
            peer = message.get("peer")
            try:
                peer = int(peer)
            except (TypeError, ValueError):
                pass
            return await self.transform_outgoing(
                message.get("account"), peer,
                message.get("text", ""), message.get("reply_to"))
        if record is None:
            raise KeyError("Unknown plugin")
        if command == "uninstall":
            self.uninstall(record)
            return True
        if command == "set_enabled":
            self.set_enabled(record, message.get("enabled"))
            return record.describe()
        if command == "settings":
            return self.settings_page(record, message.get("page", ""))
        if command == "set_setting":
            self.set_setting(record, message.get("page", ""), int(message["index"]), message.get("value"))
            return self.settings_page(record, message.get("page", ""))
        if command == "menu_click":
            return self.menu_click(record, message.get("item", ""), message)
        if command == "click":
            return {"open": self.click(record, message.get("page", ""), int(message["index"]))}
        raise KeyError("Unknown command %s" % command)

    def dispatch(self, message):
        if message.get("cmd") == "shutdown":
            self.loop.create_task(self.shutdown())
            return

        async def run_command():
            request_id = message.get("id")
            try:
                result = await self.handle(message)
                reply = {"id": request_id, "ok": True, "result": result}
            except Exception as e:
                log.exception("Command %s failed", message.get("cmd"))
                reply = {"id": request_id, "ok": False, "error": str(e) or e.__class__.__name__}
            if request_id is not None:
                protocol.send(reply)
        self.loop.create_task(run_command())

    async def shutdown(self):
        for record in list(self.records.values()):
            self.unload(record)
        await self.accounts.close()
        self.loop.stop()


def _setup_logging(data_dir):
    handler = logging.FileHandler(os.path.join(data_dir, "host.log"), mode="w", encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s"))
    root = logging.getLogger()
    root.handlers[:] = [handler]
    root.setLevel(logging.INFO)


def run(data_dir):
    os.makedirs(data_dir, exist_ok=True)
    _setup_logging(data_dir)
    stubs.install()
    sys.modules.setdefault("komaru_plugins", types.ModuleType("komaru_plugins")).__path__ = []
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    async def start():
        manager = Manager(data_dir)
        context.set_manager(manager)
        manager.load_state()
        manager.scan()
        protocol.start_reader(loop, manager.dispatch)
        for record in list(manager.records.values()):
            if record.enabled:
                manager.load(record)
        protocol.event("ready", version=HOST_VERSION)

    loop.run_until_complete(start())
    try:
        loop.run_forever()
    finally:
        loop.close()
