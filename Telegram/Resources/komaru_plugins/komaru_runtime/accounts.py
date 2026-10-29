# This is the source code of KomaruGram for Desktop.
#
# Telethon and Kurigram clients for the accounts logged into KomaruGram.
# The client hands over each account's main DC and auth key, so plugins act
# as that account without a separate login. The connection presents the same
# device, system and app version as KomaruGram itself, so the authorization
# in Settings > Devices keeps its name. Clients are created on first use and
# shared by every plugin, and go through the proxy KomaruGram uses. When
# KomaruGram uses a proxy a plugin never connects directly: Telegram is
# blocked in places, and a direct connection would only hang or reveal the
# user, so a proxy the libraries cannot use is an error instead.
import asyncio
import base64
import struct


class AccountsError(Exception):
    pass


class Accounts:
    def __init__(self, log):
        self._log = log
        self._config = {}
        self._accounts = {}
        self._telethon = {}
        self._kurigram = {}
        self._lock = asyncio.Lock()
        self._ready = asyncio.Event()

    async def wait_ready(self, timeout=60):
        await asyncio.wait_for(self._ready.wait(), timeout)

    def indices(self):
        return sorted(self._accounts)

    def account(self, index):
        if index is None:
            index = self.indices()[0] if self._accounts else 0
        if index not in self._accounts:
            raise AccountsError("Account %s is not available" % index)
        return index, self._accounts[index]

    async def configure(self, config):
        accounts = {}
        for entry in config.get("accounts", []):
            accounts[int(entry["index"])] = entry
        proxy_changed = (config.get("proxy") != self._config.get("proxy"))
        stale = [
            index for index, entry in self._accounts.items()
            if proxy_changed or accounts.get(index) != entry
        ]
        self._config = config
        self._accounts = accounts
        for index in stale:
            await self._drop(index)
        self._ready.set()

    async def _drop(self, index):
        client = self._telethon.pop(index, None)
        if client is not None:
            try:
                await client.disconnect()
            except Exception:
                pass
        client = self._kurigram.pop(index, None)
        if client is not None:
            try:
                await client.stop()
            except Exception:
                pass

    async def close(self):
        for index in list(set(self._telethon) | set(self._kurigram)):
            await self._drop(index)

    def _device(self):
        return self._config.get("device", {})

    def _proxy(self):
        return self._config.get("proxy") or {}

    def _require(self, supported, library):
        proxy = self._proxy()
        if proxy and not supported:
            raise AccountsError(
                "%s cannot use the %s proxy KomaruGram is set to, and "
                "plugins never connect without it. Use a SOCKS5 or "
                "MTProto (not fake TLS) proxy." % (
                    library, proxy.get("label") or proxy.get("type")))

    def _telethon_connection(self):
        """Connection and proxy arguments for TelegramClient."""
        proxy = self._proxy()
        kind = proxy.get("type")
        if kind in ("socks5", "http"):
            return {"proxy": {
                "proxy_type": kind,
                "addr": proxy["host"],
                "port": int(proxy["port"]),
                "username": proxy.get("user") or None,
                "password": proxy.get("password") or None,
                "rdns": True,
            }}
        if kind == "mtproto":
            from telethon import connection
            secret = (proxy.get("password") or "").lower()
            if secret.startswith("ee"):
                return None
            kind = (connection.ConnectionTcpMTProxyRandomizedIntermediate
                    if secret.startswith("dd")
                    else connection.ConnectionTcpMTProxyAbridged)
            return {
                "connection": kind,
                "proxy": (proxy["host"], int(proxy["port"]), secret),
            }
        return {} if not proxy else None

    def _kurigram_proxy(self):
        proxy = self._proxy()
        if not proxy:
            return {}
        if proxy.get("type") not in ("socks5", "http"):
            return None
        return {
            "scheme": proxy["type"],
            "hostname": proxy["host"],
            "port": int(proxy["port"]),
            "username": proxy.get("user") or None,
            "password": proxy.get("password") or None,
        }

    async def telethon(self, index=None):
        await self.wait_ready()
        index, entry = self.account(index)
        async with self._lock:
            client = self._telethon.get(index)
            if client is not None:
                return client
            from telethon import TelegramClient
            from telethon.crypto import AuthKey
            from telethon.sessions import StringSession

            connection = self._telethon_connection()
            self._require(connection is not None, "Telethon")

            session = StringSession()
            session.set_dc(int(entry["dc_id"]), entry["ip"], int(entry["port"]))
            session.auth_key = AuthKey(bytes.fromhex(entry["auth_key"]))
            device = self._device()
            client = TelegramClient(
                session,
                int(self._config["api_id"]),
                self._config["api_hash"],
                device_model=device.get("model") or "KomaruGram",
                system_version=device.get("system") or "",
                app_version=device.get("app") or "",
                lang_code=device.get("lang") or "en",
                system_lang_code=device.get("system_lang") or "en",
                **connection,
            )
            await client.connect()
            if not await client.is_user_authorized():
                await client.disconnect()
                raise AccountsError("Account %s is not authorized" % index)
            self._telethon[index] = client
            self._log("info", "Telethon connected for account %s" % index)
            return client

    async def kurigram(self, index=None):
        await self.wait_ready()
        index, entry = self.account(index)
        async with self._lock:
            client = self._kurigram.get(index)
            if client is not None:
                return client
            from pyrogram import Client

            proxy = self._kurigram_proxy()
            self._require(proxy is not None, "Kurigram")

            # Pyrogram 2 session string: dc, api id, test mode, auth key,
            # user id, is bot.
            packed = struct.pack(
                ">BI?256sQ?",
                int(entry["dc_id"]),
                int(self._config["api_id"]),
                bool(entry.get("test")),
                bytes.fromhex(entry["auth_key"]),
                int(entry["user_id"]),
                False,
            )
            session_string = base64.urlsafe_b64encode(packed).decode().rstrip("=")
            device = self._device()
            client = Client(
                "komaru_%s" % index,
                api_id=int(self._config["api_id"]),
                api_hash=self._config["api_hash"],
                session_string=session_string,
                in_memory=True,
                device_model=device.get("model") or "KomaruGram",
                system_version=device.get("system") or "",
                app_version=device.get("app") or "",
                lang_code=device.get("lang") or "en",
                proxy=proxy or None,
            )
            await client.start()
            self._kurigram[index] = client
            self._log("info", "Kurigram connected for account %s" % index)
            return client

    def session(self, index=None):
        """Raw session data for runtimes that bring their own MTProto
        library, such as Go plugins built on gotd."""
        index, entry = self.account(index)
        return {
            "index": index,
            "user_id": int(entry["user_id"]),
            "dc_id": int(entry["dc_id"]),
            "ip": entry["ip"],
            "port": int(entry["port"]),
            "auth_key": entry["auth_key"],
            "test": bool(entry.get("test")),
            "api_id": int(self._config["api_id"]),
            "api_hash": self._config["api_hash"],
            "device": self._device(),
            "proxy": self._proxy() or None,
        }
