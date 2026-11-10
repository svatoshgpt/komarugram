// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/crypto_rates.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <rpl/event_stream.h>

#include <cmath>

namespace KomaruCryptoRates {
namespace {

constexpr auto kRefreshAfter = crl::time(5 * 60 * 1000);
constexpr auto kRetryAfter = crl::time(30 * 1000);
constexpr auto kTimeout = 15000;

const auto kUrl = u"https://api.coingecko.com/api/v3/simple/price"
	"?ids=tether,the-open-network,litecoin,bitcoin"
	"&vs_currencies=rub,usd,eur"_q;

struct Coin {
	QString id;
	QStringList names;
};

struct Fiat {
	QString code;
	QString sign;
	QStringList names;
};

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

[[nodiscard]] const std::vector<Coin> &Coins() {
	static const auto result = std::vector<Coin>{
		{ u"tether"_q, { u"usdt"_q, u"юсдт"_q, u"тезер"_q } },
		{ u"the-open-network"_q, {
			u"gram"_q,
			u"grams"_q,
			u"грам"_q,
			u"грамм"_q,
			u"грама"_q,
			u"грамов"_q,
			u"граммов"_q,
			u"ton"_q,
			u"тон"_q,
			u"тонов"_q,
		} },
		{ u"litecoin"_q, { u"ltc"_q, u"лтц"_q, u"лайт"_q, u"лайтов"_q } },
		{ u"bitcoin"_q, {
			u"btc"_q,
			u"бтц"_q,
			u"биток"_q,
			u"битка"_q,
			u"битков"_q,
		} },
	};
	return result;
}

[[nodiscard]] const std::vector<Fiat> &Fiats() {
	static const auto result = std::vector<Fiat>{
		{ u"rub"_q, u"₽"_q, { u"₽"_q, u"rub"_q, u"руб"_q, u"р"_q } },
		{ u"usd"_q, u"$"_q, { u"$"_q, u"usd"_q, u"долл"_q } },
		{ u"eur"_q, u"€"_q, { u"€"_q, u"eur"_q, u"евро"_q } },
	};
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
			return;
		}
		state.rates = object;
		state.loaded = crl::now();
		state.updated.fire({});
	});
}

[[nodiscard]] QString Format(float64 value, const QString &sign) {
	const auto decimals = (value >= 1000.) ? 0 : (value >= 1.) ? 2 : 6;
	auto number = QString::number(value, 'f', decimals);
	if (decimals > 0) {
		while (number.endsWith('0')) {
			number.chop(1);
		}
		if (number.endsWith('.')) {
			number.chop(1);
		}
	}
	auto dot = number.indexOf('.');
	if (dot < 0) {
		dot = number.size();
	}
	for (auto i = dot - 3; i > 0; i -= 3) {
		number.insert(i, QChar(0x202F));
	}
	number.replace('.', ',');
	return number + ' ' + sign;
}

} // namespace

QString Completion(const QString &text) {
	static const auto regex = QRegularExpression(
		u"^\\s*(\\d+(?:[.,]\\d+)?)\\s*([^\\s\\d=]+)\\s*=\\s*([^\\s\\d]*)\\s*$"_q,
		QRegularExpression::UseUnicodePropertiesOption);
	const auto match = regex.match(text);
	if (!match.hasMatch()) {
		return QString();
	}
	const auto amount = match.captured(1).replace(',', '.').toDouble();
	const auto name = match.captured(2).toLower();
	const auto target = match.captured(3).toLower();
	const auto coin = ranges::find_if(Coins(), [&](const Coin &coin) {
		return coin.names.contains(name);
	});
	if (coin == end(Coins())) {
		return QString();
	}
	const auto fiat = target.isEmpty()
		? begin(Fiats())
		: ranges::find_if(Fiats(), [&](const Fiat &fiat) {
			return fiat.names.contains(target);
		});
	if (fiat == end(Fiats())) {
		return QString();
	}
	Load();
	const auto rate = Instance().rates.value(coin->id).toObject().value(
		fiat->code).toDouble();
	if (rate <= 0.) {
		return QString();
	}
	const auto result = Format(amount * rate, fiat->sign);
	return target.isEmpty() ? result : result.mid(0, result.size() - 2);
}

rpl::producer<> Updated() {
	return Instance().updated.events();
}

} // namespace KomaruCryptoRates
