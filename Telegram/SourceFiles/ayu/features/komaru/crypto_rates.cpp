// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/crypto_rates.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <rpl/event_stream.h>

#include <komaru_core.h>

namespace KomaruCryptoRates {
namespace {

constexpr auto kRefreshAfter = crl::time(5 * 60 * 1000);
constexpr auto kRetryAfter = crl::time(30 * 1000);
constexpr auto kTimeout = 15000;

const auto kUrl = u"https://api.coingecko.com/api/v3/simple/price"
	"?ids=tether,the-open-network,litecoin,bitcoin"
	"&vs_currencies=rub,usd,eur"_q;

struct State {
	QJsonObject rates;
	crl::time loaded = 0;
	crl::time requested = 0;
	bool loading = false;
	rpl::event_stream<> updated;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] QNetworkAccessManager &Manager() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

void Load() {
	auto &state = Instance();
	const auto now = crl::now();
	if (state.loading
		|| (state.loaded && now - state.loaded < kRefreshAfter)
		|| (state.requested && now - state.requested < kRetryAfter)) {
		return;
	}
	state.loading = true;
	state.requested = now;
	auto request = QNetworkRequest(QUrl(kUrl));
	request.setTransferTimeout(kTimeout);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"KomaruGram"_q);
	const auto reply = Manager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &state = Instance();
		state.loading = false;
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Komaru Rates Error: %1").arg(reply->errorString()));
			return;
		}
		const auto object = QJsonDocument::fromJson(reply->readAll()).object();
		if (object.isEmpty()) {
			LOG(("Komaru Rates Error: empty answer."));
			return;
		}
		state.rates = object;
		state.loaded = crl::now();
		state.updated.fire({});
	});
}

} // namespace

QString Completion(const QString &text) {
	const auto lookup = [](
			void*,
			const char *coin,
			size_t coinSize,
			const char *fiat,
			size_t fiatSize) {
		Load();
		return Instance().rates.value(
			QString::fromUtf8(coin, qsizetype(coinSize))
		).toObject().value(QString::fromUtf8(fiat, qsizetype(fiatSize))).toDouble();
	};
	const auto utf = text.toUtf8();
	char buffer[128];
	const auto size = komaru_rate_completion(
		utf.constData(),
		utf.size(),
		lookup,
		nullptr,
		buffer,
		sizeof(buffer));
	return QString::fromUtf8(buffer, qsizetype(size));
}

rpl::producer<> Updated() {
	return Instance().updated.events();
}

} // namespace KomaruCryptoRates
