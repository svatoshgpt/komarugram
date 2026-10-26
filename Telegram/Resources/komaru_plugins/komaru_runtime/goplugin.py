# This is the source code of KomaruGram for Desktop.
#
# Go plugins run as child processes and speak the same kind of JSON lines
# with the host. The host sends:
#   {"event": "init", "plugin": meta, "settings": {...}, "accounts": [...]}
#   {"event": "accounts", "accounts": [...]}
#   {"event": "update", "account", "type": "new_message", ...}
#   {"event": "send_message", "id", "account", "peer", "text"}
#   {"event": "setting", "key", "value"}
#   {"event": "stop"}
# and the plugin may send:
#   {"call": "send_message", "id", "account", "peer", "text", "parse_mode"}
#   {"call": "set_setting", "key", "value"}
#   {"call": "subscribe", "what": "updates"}
#   {"call": "log", "text"} / {"call": "toast", "text"}
#   {"reply": id, "action": "keep" | "modify" | "cancel", "text"}
# Each account in "accounts" carries the DC and auth key, so a plugin can
# also open its own MTProto connection with gotd/td.
import asyncio
import json
import logging
import os
import platform
import shutil
import subprocess
import sys

from komaru_runtime import protocol

SEND_HOOK_TIMEOUT = 1.0


def _platform_key():
    system = {"win32": "windows", "darwin": "macos"}.get(sys.platform, "linux")
    machine = platform.machine().lower()
    arch = {"x86_64": "amd64", "amd64": "amd64", "aarch64": "arm64",
            "arm64": "arm64", "i386": "386", "i686": "386", "x86": "386"}.get(machine, machine)
    return system + "-" + arch


class GoPlugin:
    def __init__(self, manager, record):
        self.manager = manager
        self.record = record
        self.log = logging.getLogger("plugin." + record.id)
        self.process = None
        self.send_hook = "send_message" in (record.meta.get("hooks") or [])
        self.wants_updates = False
        self._next_id = 0
        self._waiting = {}
        self._tasks = []

    def _command(self):
        binaries = self.record.meta.get("binaries") or {}
        key = _platform_key()
        relative = binaries.get(key) if isinstance(binaries, dict) else None
        if relative:
            path = os.path.normpath(os.path.join(self.record.dir, relative))
            if not path.startswith(os.path.normpath(self.record.dir)):
                raise RuntimeError("Binary path leaves the plugin folder")
            if not os.path.isfile(path):
                raise RuntimeError("Binary %s is missing" % relative)
            if sys.platform != "win32":
                os.chmod(path, 0o755)
            return [path]
        go = shutil.which("go")
        source = self.record.meta.get("main") or "."
        if go and os.path.exists(os.path.join(self.record.dir, source)):
            return [go, "run", source]
        raise RuntimeError(
            "No binary for %s and Go is not installed to build the source" % key)

    def start(self):
        command = self._command()
        env = dict(os.environ)
        env["KOMARU_PLUGIN_ID"] = self.record.id
        env["KOMARU_PLUGIN_DIR"] = self.record.dir
        self._tasks.append(self.manager.loop.create_task(self._run(command, env)))

    async def _run(self, command, env):
        self.process = await asyncio.create_subprocess_exec(
            *command,
            cwd=self.record.dir,
            env=env,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self._send({
            "event": "init",
            "plugin": self.record.meta,
            "settings": self.record.settings,
            "accounts": self._accounts(),
        })
        self._tasks.append(self.manager.loop.create_task(self._read_stderr()))
        while True:
            line = await self.process.stdout.readline()
            if not line:
                break
            try:
                message = json.loads(line)
            except ValueError:
                self.log.info("%s", line.decode("utf-8", "replace").rstrip())
                continue
            await self._handle(message)
        code = await self.process.wait()
        if self.record.go is self:
            self.record.error = "Go plugin exited with code %s" % code
            self.manager.notify_changed()

    async def _read_stderr(self):
        while True:
            line = await self.process.stderr.readline()
            if not line:
                break
            self.log.info("%s", line.decode("utf-8", "replace").rstrip())

    def _accounts(self):
        result = []
        for index in self.manager.accounts.indices():
            try:
                result.append(self.manager.accounts.session(index))
            except Exception:
                pass
        return result

    def _send(self, message):
        if self.process is None or self.process.stdin is None:
            return
        try:
            self.process.stdin.write((json.dumps(message, default=str) + "\n").encode("utf-8"))
        except (ConnectionError, RuntimeError):
            pass

    def send_accounts(self):
        self._send({"event": "accounts", "accounts": self._accounts()})

    async def _handle(self, message):
        if "reply" in message:
            future = self._waiting.pop(message["reply"], None)
            if future and not future.done():
                future.set_result(message)
            return
        call = message.get("call")
        if call == "log":
            self.log.info("%s", message.get("text", ""))
        elif call == "toast":
            protocol.event("toast", text=str(message.get("text", "")))
        elif call == "set_setting":
            self.record.settings[message.get("key")] = message.get("value")
            self.manager.save_state()
        elif call == "subscribe" and message.get("what") == "updates":
            self.wants_updates = True
            self.manager.ensure_update_listener(self.record)
        elif call == "send_message":
            await self._send_message(message)

    async def _send_message(self, message):
        try:
            client = await self.manager.accounts.telethon(message.get("account"))
            mode = message.get("parse_mode")
            await client.send_message(
                message["peer"], message.get("text", ""),
                parse_mode=("html" if mode == "html" else "md") if mode else None,
                reply_to=message.get("reply_to"))
            self._send({"result": message.get("id"), "ok": True})
        except Exception as e:
            self._send({"result": message.get("id"), "ok": False, "error": str(e)})

    async def transform(self, account, peer, text):
        self._next_id += 1
        request_id = self._next_id
        future = self.manager.loop.create_future()
        self._waiting[request_id] = future
        self._send({"event": "send_message", "id": request_id,
                    "account": account, "peer": peer, "text": text})
        try:
            reply = await asyncio.wait_for(future, SEND_HOOK_TIMEOUT)
        except asyncio.TimeoutError:
            self._waiting.pop(request_id, None)
            return "keep", text
        action = reply.get("action", "keep")
        if action == "modify":
            return "modify", str(reply.get("text", text))
        return action, text

    def forward_update(self, account, update):
        message = getattr(update, "message", None)
        if message is None or not hasattr(message, "message"):
            return
        peer = getattr(message, "peer_id", None)
        chat_id = (getattr(peer, "user_id", None)
                   or getattr(peer, "chat_id", None)
                   or getattr(peer, "channel_id", None))
        sender = getattr(message, "from_id", None)
        self._send({
            "event": "update",
            "type": "new_message",
            "account": account,
            "chat_id": chat_id,
            "chat_type": type(peer).__name__ if peer else "",
            "sender_id": getattr(sender, "user_id", None) if sender else None,
            "message_id": getattr(message, "id", None),
            "text": message.message or "",
            "outgoing": bool(getattr(message, "out", False)),
        })

    def settings_items(self):
        items = []
        for index, entry in enumerate(self.record.meta.get("settings") or []):
            item = dict(entry)
            item["index"] = index
            item.setdefault("type", "switch")
            key = item.get("key")
            if key:
                item["value"] = self.record.settings.get(key, item.get("default"))
            items.append(item)
        return items

    def set_setting(self, index, value):
        entries = self.record.meta.get("settings") or []
        key = entries[index].get("key") if index < len(entries) else None
        if key:
            self.record.settings[key] = value
            self.manager.save_state()
            self._send({"event": "setting", "key": key, "value": value})

    def stop(self):
        self._send({"event": "stop"})
        for task in self._tasks:
            task.cancel()
        if self.process is not None and self.process.returncode is None:
            try:
                self.process.kill()
            except ProcessLookupError:
                pass
        for future in self._waiting.values():
            if not future.done():
                future.cancel()
        self._waiting.clear()
