# This is the source code of KomaruGram for Desktop.
#
# Telethon clients for the accounts logged into KomaruGram. They have no
# network connection of their own: every request goes through the client's
# connection (see bridge.py), so plugins act in the client's own session
# and the account's key never leaves KomaruGram. Clients are created on
# first use and shared by every plugin.
import asyncio


class AccountsError(Exception):
    pass


class Accounts:
    def __init__(self, log):
        self._log = log
        self._config = {}
        self._accounts = {}
        self._telethon = {}
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
        stale = [
            index for index, entry in self._accounts.items()
            if accounts.get(index) != entry
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

    async def close(self):
        for index in list(self._telethon):
            await self._drop(index)

    def _device(self):
        return self._config.get("device", {})

    def resolve(self, message):
        client = self._telethon.get(message.get("account"))
        if client is not None:
            client._sender.resolve(message)

    def feed_updates(self, message):
        client = self._telethon.get(message.get("account"))
        if client is not None:
            client._sender.feed_updates(message.get("data") or "")

    async def warm(self):
        """Gets the clients ready ahead of the first plugin call."""
        try:
            await self.wait_ready()
            for index in self.indices():
                await self.telethon(index)
        except Exception as error:
            self._log("warning", "Could not prepare plugin accounts: %r" % (error,))

    async def telethon(self, index=None):
        await self.wait_ready()
        index, entry = self.account(index)
        async with self._lock:
            client = self._telethon.get(index)
            if client is not None:
                return client
            from komaru_runtime import bridge
            client = bridge.make_client(
                index, entry, self._config, self._device(), self._log)
            self._telethon[index] = client
            try:
                await client.connect()
            except Exception:
                self._telethon.pop(index, None)
                raise
            self._log("info", "Telethon ready for account %s" % index)
            return client

    async def kurigram(self, index=None):
        raise AccountsError(
            "Kurigram needs a connection of its own with the account key, "
            "which would get the session revoked, so it is off. Use Telethon "
            "through self.telethon().")

    def session(self, index=None):
        """Account data for Go plugins. The key stays in KomaruGram: they
        call the host, which sends through the client."""
        index, entry = self.account(index)
        return {
            "index": index,
            "user_id": int(entry["user_id"]),
            "dc_id": int(entry["dc_id"]),
            "test": bool(entry.get("test")),
            "api_id": int(self._config["api_id"]),
            "api_hash": self._config["api_hash"],
            "device": self._device(),
        }
