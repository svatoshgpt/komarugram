// This is the source code of KomaruGram for Desktop.
//
// Native built-in MTProto-over-WebSocket proxy: accepts local Obfuscated2
// connections from Telegram Desktop on 127.0.0.1 and bridges them directly to
// Telegram's own web client endpoints (wss://kws{dc}.web.telegram.org/apiws)
// using Core::WebSocketClient + OpenSSL AES-256-CTR. Zero Python required.
#include "ayu/features/komaru/komaru_ws_proxy.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/websocket_client.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace KomaruWsProxy {
namespace {

constexpr auto kPingUrl = "https://kws2.web.telegram.org/apiws";
constexpr auto kPingTimeoutMs = 8000;

class AesCtr final {
public:
	AesCtr() = default;
	~AesCtr() {
		reset();
	}

	AesCtr(const AesCtr &) = delete;
	AesCtr &operator=(const AesCtr &) = delete;

	bool init(const uchar *key32, const uchar *iv16) {
		reset();
		_ctx = EVP_CIPHER_CTX_new();
		if (!_ctx) {
			return false;
		}
		if (EVP_EncryptInit_ex(_ctx, EVP_aes_256_ctr(), nullptr, key32, iv16) != 1) {
			reset();
			return false;
		}
		return true;
	}

	QByteArray process(const QByteArray &in) {
		if (!_ctx || in.isEmpty()) {
			return QByteArray();
		}
		auto out = QByteArray(in.size(), Qt::Uninitialized);
		auto outLen = 0;
		EVP_EncryptUpdate(
			_ctx,
			reinterpret_cast<uchar*>(out.data()),
			&outLen,
			reinterpret_cast<const uchar*>(in.constData()),
			in.size());
		out.resize(outLen);
		return out;
	}

private:
	void reset() {
		if (_ctx) {
			EVP_CIPHER_CTX_free(_ctx);
			_ctx = nullptr;
		}
	}

	EVP_CIPHER_CTX *_ctx = nullptr;
};

[[nodiscard]] bool IsForbiddenPrefix(const uchar *b) {
	if (b[0] == 0xef) {
		return true;
	}
	const auto eq4 = [&](uchar a0, uchar a1, uchar a2, uchar a3) {
		return b[0] == a0 && b[1] == a1 && b[2] == a2 && b[3] == a3;
	};
	if (eq4(0x48, 0x45, 0x41, 0x44) // HEAD
		|| eq4(0x50, 0x4f, 0x53, 0x54) // POST
		|| eq4(0x47, 0x45, 0x54, 0x20) // GET 
		|| eq4(0xee, 0xee, 0xee, 0xee)
		|| eq4(0xdd, 0xdd, 0xdd, 0xdd)
		|| eq4(0x16, 0x03, 0x01, 0x02)) {
		return true;
	}
	return (b[4] == 0 && b[5] == 0 && b[6] == 0 && b[7] == 0);
}

class ProxySession final : public QObject {
public:
	ProxySession(QTcpSocket *client, const QByteArray &secret16, QObject *parent)
	: QObject(parent)
	, _client(client)
	, _secret(secret16) {
		_client->setParent(this);
		_client->setSocketOption(QAbstractSocket::LowDelayOption, 1);

		connect(_client, &QTcpSocket::readyRead, this, [=] {
			onClientData();
		});
		connect(_client, &QTcpSocket::disconnected, this, [=] {
			closeAll();
		});
		connect(_client, &QAbstractSocket::errorOccurred, this, [=] {
			closeAll();
		});

		if (_client->bytesAvailable() > 0) {
			onClientData();
		}
	}

private:
	void onClientData() {
		if (_closed || !_client) {
			return;
		}
		const auto chunk = _client->readAll();
		if (chunk.isEmpty()) {
			return;
		}
		if (!_handshakeDone) {
			_initBuf.append(chunk);
			if (_initBuf.size() < 64) {
				return;
			}
			const auto init = _initBuf.left(64);
			const auto extra = _initBuf.mid(64);
			_initBuf.clear();
			if (!handleClientInit(init)) {
				closeAll();
				return;
			}
			_handshakeDone = true;
			if (!extra.isEmpty()) {
				forwardClientToUpstream(extra);
			}
			return;
		}
		forwardClientToUpstream(chunk);
	}

	bool handleClientInit(const QByteArray &init) {
		// Derive client->proxy decryption key/iv and proxy->client encryption key/iv.
		auto decSeed = init.mid(8, 32);
		decSeed.append(_secret);
		const auto decKey = QCryptographicHash::hash(
			decSeed,
			QCryptographicHash::Sha256);
		const auto decIv = init.mid(40, 16);

		auto rev = init.mid(8, 48);
		std::reverse(rev.begin(), rev.end());
		auto encSeed = rev.left(32);
		encSeed.append(_secret);
		const auto encKey = QCryptographicHash::hash(
			encSeed,
			QCryptographicHash::Sha256);
		const auto encIv = rev.mid(32, 16);

		if (!_clientDec.init(
				reinterpret_cast<const uchar*>(decKey.constData()),
				reinterpret_cast<const uchar*>(decIv.constData()))
			|| !_clientEnc.init(
				reinterpret_cast<const uchar*>(encKey.constData()),
				reinterpret_cast<const uchar*>(encIv.constData()))) {
			return false;
		}

		// Advance clientDec past the 64-byte obfuscated2 header.
		const auto decrypted = _clientDec.process(init);
		if (decrypted.size() != 64) {
			return false;
		}
		const auto tag = decrypted.mid(56, 4);
		const auto rawDc = qint16(
			uchar(decrypted[60]) | (uchar(decrypted[61]) << 8));
		auto absDc = std::abs(int(rawDc));
		if (absDc == 203) {
			absDc = 2;
		}
		if (absDc < 1 || absDc > 5) {
			absDc = 2;
		}
		const auto isMedia = (rawDc < 0);

		// Build fresh 64-byte upstream Obfuscated2 init packet for web.telegram.org.
		auto upInit = QByteArray(64, Qt::Uninitialized);
		do {
			RAND_bytes(reinterpret_cast<uchar*>(upInit.data()), 64);
		} while (IsForbiddenPrefix(reinterpret_cast<const uchar*>(upInit.constData())));
		std::memcpy(upInit.data() + 56, tag.constData(), 4);
		upInit[60] = char(rawDc & 0xFF);
		upInit[61] = char((rawDc >> 8) & 0xFF);

		const auto upEncKey = upInit.mid(8, 32);
		const auto upEncIv = upInit.mid(40, 16);
		auto upRev = upInit.mid(8, 48);
		std::reverse(upRev.begin(), upRev.end());
		const auto upDecKey = upRev.left(32);
		const auto upDecIv = upRev.mid(32, 16);

		if (!_upEnc.init(
				reinterpret_cast<const uchar*>(upEncKey.constData()),
				reinterpret_cast<const uchar*>(upEncIv.constData()))
			|| !_upDec.init(
				reinterpret_cast<const uchar*>(upDecKey.constData()),
				reinterpret_cast<const uchar*>(upDecIv.constData()))) {
			return false;
		}

		const auto upEncrypted = _upEnc.process(upInit);
		if (upEncrypted.size() != 64) {
			return false;
		}
		_upInitPacket = upInit;
		std::memcpy(_upInitPacket.data() + 56, upEncrypted.constData() + 56, 8);

		const auto wsHost = isMedia
			? u"kws%1-1.web.telegram.org"_q.arg(absDc)
			: u"kws%1.web.telegram.org"_q.arg(absDc);

		_ws = new Core::WebSocketClient(this);
		_ws->onConnected = [=] {
			if (_closed || !_ws) {
				return;
			}
			_wsConnected = true;
			_ws->sendBinary(_upInitPacket);
			_upInitPacket.clear();
			for (const auto &pending : _pendingToWs) {
				_ws->sendBinary(pending);
			}
			_pendingToWs.clear();
		};
		_ws->onBinary = [=](QByteArray payload) {
			if (_closed || !_client || payload.isEmpty()) {
				return;
			}
			const auto plain = _upDec.process(payload);
			const auto out = _clientEnc.process(plain);
			if (!out.isEmpty()) {
				_client->write(out);
			}
		};
		_ws->onClosed = [=] {
			closeAll();
		};
		_ws->connectTo(wsHost, 443, u"/apiws"_q, u"binary"_q);
		return true;
	}

	void forwardClientToUpstream(const QByteArray &chunk) {
		const auto plain = _clientDec.process(chunk);
		const auto encrypted = _upEnc.process(plain);
		if (encrypted.isEmpty()) {
			return;
		}
		if (_wsConnected && _ws) {
			_ws->sendBinary(encrypted);
		} else {
			_pendingToWs.push_back(encrypted);
		}
	}

	void closeAll() {
		if (_closed) {
			return;
		}
		_closed = true;
		if (_ws) {
			_ws->onConnected = nullptr;
			_ws->onBinary = nullptr;
			_ws->onClosed = nullptr;
			_ws->close();
		}
		if (_client) {
			_client->disconnect(this);
			_client->close();
		}
		deleteLater();
	}

	QPointer<QTcpSocket> _client;
	QPointer<Core::WebSocketClient> _ws;
	QByteArray _secret;
	QByteArray _initBuf;
	QByteArray _upInitPacket;
	std::vector<QByteArray> _pendingToWs;
	AesCtr _clientDec;
	AesCtr _clientEnc;
	AesCtr _upEnc;
	AesCtr _upDec;
	bool _handshakeDone = false;
	bool _wsConnected = false;
	bool _closed = false;
};

struct State {
	QPointer<QTcpServer> server;
	QString host;
	uint32 port = 0;
	QByteArray secret16;
	rpl::event_stream<> changed;
};

State &Instance() {
	static auto result = State();
	return result;
}

QNetworkAccessManager &Network() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

} // namespace

bool IsActive() {
	const auto &state = Instance();
	if (!state.server || !state.port || state.host.isEmpty()) {
		return false;
	}
	const auto &proxySettings = Core::App().settings().proxy();
	if (!proxySettings.isEnabled()) {
		return false;
	}
	const auto &selected = proxySettings.selected();
	return (selected.type == MTP::ProxyData::Type::Mtproto)
		&& (selected.host == state.host)
		&& (selected.port == state.port);
}

bool IsConnecting() {
	return false;
}

void Enable(Fn<void(QString error)> done) {
	auto &state = Instance();
	if (!state.server) {
		state.secret16 = QByteArray(16, Qt::Uninitialized);
		RAND_bytes(reinterpret_cast<uchar*>(state.secret16.data()), 16);

		auto server = new QTcpServer(QCoreApplication::instance());
		if (!server->listen(QHostAddress::LocalHost, 0)) {
			const auto err = server->errorString();
			server->deleteLater();
			if (done) {
				done(err.isEmpty() ? u"Failed to bind local WebSocket proxy"_q : err);
			}
			return;
		}
		QObject::connect(server, &QTcpServer::newConnection, server, [server] {
			const auto secret = Instance().secret16;
			while (const auto client = server->nextPendingConnection()) {
				new ProxySession(client, secret, server);
			}
		});
		state.server = server;
		state.host = u"127.0.0.1"_q;
		state.port = uint32(server->serverPort());
	}

	auto proxy = MTP::ProxyData();
	proxy.type = MTP::ProxyData::Type::Mtproto;
	proxy.host = state.host;
	proxy.port = state.port;
	proxy.password = u"dd"_q + QString::fromLatin1(state.secret16.toHex());

	Core::App().setCurrentProxy(
		proxy,
		MTP::ProxyData::Settings::Enabled);
	Core::App().saveSettingsDelayed();
	state.changed.fire({});
	if (done) {
		done(QString());
	}
}

void Disable() {
	auto &state = Instance();
	if (!state.server && !state.port) {
		return;
	}
	if (state.server) {
		state.server->close();
		state.server->deleteLater();
		state.server = nullptr;
	}
	state.port = 0;
	state.host.clear();
	state.secret16.clear();
	state.changed.fire({});
}

void Ping(Fn<void(int ms)> done) {
	if (!done) {
		return;
	}
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kPingUrl)));
	request.setTransferTimeout(kPingTimeoutMs);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"KomaruGram"_q);

	auto timer = std::make_shared<QElapsedTimer>();
	timer->start();

	const auto reply = Network().head(request);
	QObject::connect(reply, &QNetworkReply::finished, [reply, timer, done = std::move(done)] {
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto elapsed = int(timer->elapsed());
		const auto reachable = (status > 0)
			|| (reply->error() == QNetworkReply::NoError);
		reply->deleteLater();
		done(reachable ? std::max(1, elapsed) : -1);
	});
}

rpl::producer<> StateChanged() {
	return Instance().changed.events();
}

} // namespace KomaruWsProxy
