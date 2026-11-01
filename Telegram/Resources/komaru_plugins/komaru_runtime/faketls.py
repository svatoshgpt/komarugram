# This is the source code of KomaruGram for Desktop.
#
# MTProto proxies with an ee secret hide inside what looks like TLS. Telethon
# only knows the plain and dd kinds, so this adds the third one for plugins,
# following mtproto_tls_socket.cpp of the client: the same Client Hello, its
# digest signed with the secret, a signed Server Hello, then the usual
# obfuscated stream cut into TLS application data records.
import asyncio
import base64
import hashlib
import hmac
import os
import random
import struct
import time

from telethon.network.connection.connection import Connection, ObfuscatedConnection
from telethon.network.connection.tcpmtproxy import ConnectionTcpMTProxyRandomizedIntermediate

CLIENT_PART_SIZE = 2878
CLIENT_PREFIX = b"\x14\x03\x03\x00\x01\x01"
RECORD_HEADER = b"\x17\x03\x03"
SERVER_HELLO_PART1 = b"\x16\x03\x03"
SERVER_HELLO_PART3 = b"\x14\x03\x03\x00\x01\x01\x17\x03\x03"
DIGEST_POSITION = 11
DIGEST_LENGTH = 32
MAX_SERVER_HELLO = 65536
HANDSHAKE_TIMEOUT = 15


def parse_secret(text):
    """Secrets come as hex or as base64, with the kind in the first byte."""
    text = (text or "").strip()
    try:
        return bytes.fromhex(text)
    except ValueError:
        padded = text + "=" * (-len(text) % 4)
        return base64.urlsafe_b64decode(padded.replace("+", "-").replace("/", "_"))


def _x25519_public_key():
    p = 2 ** 255 - 19
    scalar = bytearray(os.urandom(32))
    scalar[0] &= 248
    scalar[31] = (scalar[31] & 127) | 64
    k = int.from_bytes(scalar, "little")
    x1, x2, z2, x3, z3, swap = 9, 1, 0, 9, 1, 0
    for t in range(254, -1, -1):
        bit = (k >> t) & 1
        if swap ^ bit:
            x2, x3, z2, z3 = x3, x2, z3, z2
        swap = bit
        a, b = (x2 + z2) % p, (x2 - z2) % p
        aa, bb = a * a % p, b * b % p
        e = (aa - bb) % p
        c, d = (x3 + z3) % p, (x3 - z3) % p
        da, cb = d * a % p, c * b % p
        x3 = (da + cb) ** 2 % p
        z3 = x1 * (da - cb) ** 2 % p
        x2 = aa * bb % p
        z2 = e * (aa + 121665 * e) % p
    if swap:
        x2, z2 = x3, z3
    return (x2 * pow(z2, p - 2, p) % p).to_bytes(32, "little")


def _greases():
    result = bytearray((byte & 0xF0) + 0x0A for byte in os.urandom(8))
    for i in range(0, 8, 2):
        if result[i] == result[i + 1]:
            result[i + 1] ^= 0x10
    return result


def _scope(data):
    return struct.pack(">H", len(data)) + data


def _kyber_key():
    # Looks like an ML-KEM public key: 12 bit numbers below 3329, then a seed.
    result = bytearray()
    for _ in range(384):
        a = random.SystemRandom().randrange(3329)
        b = random.SystemRandom().randrange(3329)
        result += bytes((a & 255, (a >> 8) + ((b & 15) << 4), b >> 4))
    return bytes(result) + os.urandom(32)


def client_hello(domain, key):
    """Returns the hello and its digest, as PrepareClientHelloRules lays
    them out. The key share is larger than the 513 bytes the rules pad to,
    so no padding is ever needed."""
    grease = _greases()

    def G(seed):
        return bytes((grease[seed], grease[seed]))

    extensions = [
        b"\x00\x00" + _scope(_scope(b"\x00" + _scope(domain))),
        b"\x00\x05\x00\x05\x01\x00\x00\x00\x00",
        b"\x00\x0a\x00\x0c\x00\x0a" + G(4) + b"\x11\xec\x00\x1d\x00\x17\x00\x18",
        b"\x00\x0b\x00\x02\x01\x00",
        b"\x00\x0d\x00\x18\x00\x16\x09\x04\x09\x05\x09\x06\x04\x03"
        b"\x08\x04\x04\x01\x05\x03\x08\x05\x05\x01\x08\x06\x06\x01",
        b"\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70"
        b"\x2f\x31\x2e\x31",
        b"\x00\x12\x00\x00",
        b"\x00\x17\x00\x00",
        b"\x00\x1b\x00\x03\x02\x00\x02",
        b"\x00\x23\x00\x00",
        b"\x00\x2b\x00\x07\x06" + G(6) + b"\x03\x04\x03\x03",
        b"\x00\x2d\x00\x02\x01\x01",
        b"\x00\x33\x04\xef\x04\xed" + G(4) + b"\x00\x01\x00\x11\xec\x04\xc0"
        + _kyber_key() + _x25519_public_key()
        + b"\x00\x1d\x00\x20" + _x25519_public_key(),
        b"\x44\xcd\x00\x05\x00\x03\x02\x68\x32",
        b"\xfe\x0d" + _scope(
            b"\x00\x00\x01\x00\x01" + os.urandom(1) + b"\x00\x20"
            + _x25519_public_key()
            + _scope(os.urandom(random.choice((144, 176, 208, 240))))),
        b"\xff\x01\x00\x01\x00",
    ]
    random.SystemRandom().shuffle(extensions)
    body = (
        b"\x03\x03" + bytes(DIGEST_LENGTH) + b"\x20" + os.urandom(32)
        + b"\x00\x20" + G(0)
        + b"\x13\x01\x13\x02\x13\x03\xc0\x2b\xc0\x2f\xc0\x2c\xc0\x30\xcc\xa9"
        + b"\xcc\xa8\xc0\x13\xc0\x14\x00\x9c\x00\x9d\x00\x2f\x00\x35\x01\x00"
        + _scope(G(2) + b"\x00\x00" + b"".join(extensions) + G(3) + b"\x00\x01\x00"))
    hello = bytearray(b"\x16\x03\x01" + _scope(b"\x01\x00" + _scope(body)))
    digest = bytearray(hmac.new(key, bytes(hello), hashlib.sha256).digest())
    now = struct.unpack("<I", digest[-4:])[0] ^ (int(time.time()) & 0xFFFFFFFF)
    digest[-4:] = struct.pack("<I", now)
    hello[DIGEST_POSITION:DIGEST_POSITION + DIGEST_LENGTH] = digest
    return bytes(hello), bytes(digest)


class TlsWriter:
    """Cuts the stream into application data records. The obfuscation
    header stays back and goes out with the first packet, as in the client."""

    def __init__(self, writer):
        self._writer = writer
        self._prefix = None
        self._first = True

    def __getattr__(self, name):
        return getattr(self._writer, name)

    def write(self, data):
        if self._first and self._prefix is None:
            self._prefix = bytes(data)
            return
        if self._first:
            self._first = False
            data = self._prefix + bytes(data)
            self._writer.write(CLIENT_PREFIX)
        data = bytes(data)
        for start in range(0, len(data), CLIENT_PART_SIZE):
            part = data[start:start + CLIENT_PART_SIZE]
            self._writer.write(RECORD_HEADER + struct.pack(">H", len(part)) + part)


class TlsReader:
    def __init__(self, reader):
        self._reader = reader
        self._buffer = bytearray()

    def __getattr__(self, name):
        return getattr(self._reader, name)

    async def _next_record(self):
        header = await self._reader.readexactly(5)
        if header[:3] != RECORD_HEADER:
            raise ConnectionError("Bad record from the fake TLS proxy")
        length = struct.unpack(">H", header[3:])[0]
        if length:
            self._buffer += await self._reader.readexactly(length)

    async def readexactly(self, n):
        while len(self._buffer) < n:
            await self._next_record()
        result = bytes(self._buffer[:n])
        del self._buffer[:n]
        return result


class ConnectionTcpMTProxyFakeTLS(ConnectionTcpMTProxyRandomizedIntermediate):
    """Pass the secret as hex in proxy[2], ee byte and domain included."""

    def __init__(self, ip, port, dc_id, *, loggers, proxy=None, local_addr=None):
        secret = parse_secret(proxy[2])
        if len(secret) < 18 or secret[0] != 0xEE:
            raise ValueError("Not a fake TLS proxy secret")
        self._tls_key = secret[1:17]
        self._tls_domain = secret[17:]
        self._tls_ready = False
        super().__init__(ip, port, dc_id, loggers=loggers, proxy=proxy, local_addr=local_addr)

    @staticmethod
    def normalize_secret(secret):
        return parse_secret(secret)[1:17]

    def _init_conn(self):
        # Connection._connect calls this right after the socket connects,
        # which is before the handshake here.
        if self._tls_ready:
            super()._init_conn()

    async def _handshake(self):
        hello, digest = client_hello(self._tls_domain, self._tls_key)
        self._writer.write(hello)
        await self._writer.drain()

        reader = self._reader
        part1 = await reader.readexactly(5)
        if part1[:3] != SERVER_HELLO_PART1:
            raise ConnectionError("Bad Server Hello from the fake TLS proxy")
        part2 = await reader.readexactly(struct.unpack(">H", part1[3:])[0])
        part3 = await reader.readexactly(len(SERVER_HELLO_PART3) + 2)
        if part3[:-2] != SERVER_HELLO_PART3:
            raise ConnectionError("Bad Server Hello from the fake TLS proxy")
        part4 = await reader.readexactly(struct.unpack(">H", part3[-2:])[0])

        reply = bytearray(part1 + part2 + part3 + part4)
        if (len(reply) > MAX_SERVER_HELLO
                or len(reply) < DIGEST_POSITION + DIGEST_LENGTH):
            raise ConnectionError("Bad Server Hello size from the fake TLS proxy")
        received = bytes(reply[DIGEST_POSITION:DIGEST_POSITION + DIGEST_LENGTH])
        reply[DIGEST_POSITION:DIGEST_POSITION + DIGEST_LENGTH] = bytes(DIGEST_LENGTH)
        check = hmac.new(self._tls_key, digest + bytes(reply), hashlib.sha256).digest()
        if not hmac.compare_digest(received, check):
            raise ConnectionError(
                "Bad Server Hello digest, is the proxy secret or the clock wrong?")

    async def _connect(self, timeout=None, ssl=None):
        # Skips TcpMTProxy._connect: it waits for EOF on the plain socket.
        self._tls_ready = False
        await Connection._connect(self, timeout=timeout, ssl=ssl)
        await asyncio.wait_for(self._handshake(), timeout or HANDSHAKE_TIMEOUT)
        self._reader = TlsReader(self._reader)
        self._writer = TlsWriter(self._writer)
        self._tls_ready = True
        ObfuscatedConnection._init_conn(self)
        await self._writer.drain()
