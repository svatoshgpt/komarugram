// This is the source code of KomaruGram for Desktop.
//
// Inline calculator, currency/crypto/Stars converter, unit converter, and time
// zone converter ported from official exteraGram (com.exteragram.messenger.inline).
#include "ayu/features/komaru/crypto_rates.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QTimeZone>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <rpl/event_stream.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace KomaruCryptoRates {
namespace {

constexpr auto kRefreshAfter = crl::time(5 * 60 * 1000);
constexpr auto kRetryAfter = crl::time(30 * 1000);
constexpr auto kTimeout = 15000;
constexpr auto kPi = 3.14159265358979323846;
constexpr auto kE = 2.71828182845904523536;
constexpr auto kStarsUsdRate = 0.013; // $13 per 1000 Telegram Stars
constexpr auto kCelsiusOffset = 273.15;
constexpr auto kFahrenheitScale = 5.0 / 9.0;
constexpr auto kMaxFactorial = 170;

const auto kCoinbaseUrl =
	u"https://api.coinbase.com/v2/exchange-rates?currency=USD"_q;

// ============================================================================
// Exchange Rates (Coinbase API — 150+ fiat currencies + crypto + Stars)
// ============================================================================

struct RatesState {
	QHash<QString, double> usdRates; // code -> USD price of 1 unit
	crl::time loaded = 0;
	crl::time requested = 0;
	bool loading = false;
	rpl::event_stream<> updated;
};

[[nodiscard]] RatesState &Instance() {
	static auto result = RatesState();
	return result;
}

[[nodiscard]] QNetworkAccessManager &Manager() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

void RequestRates() {
	auto &state = Instance();
	const auto now = crl::now();
	if (state.loading
		|| (state.loaded && now - state.loaded < kRefreshAfter)
		|| (state.requested && now - state.requested < kRetryAfter)) {
		return;
	}
	state.loading = true;
	state.requested = now;
	LOG(("Komaru Rates: requesting Coinbase exchange rates."));

	auto request = QNetworkRequest(QUrl(kCoinbaseUrl));
	request.setTransferTimeout(kTimeout);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"KomaruGram"_q);
	const auto reply = Manager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &st = Instance();
		st.loading = false;
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Komaru Rates Error: %1").arg(reply->errorString()));
			return;
		}
		const auto root = QJsonDocument::fromJson(reply->readAll()).object();
		const auto ratesObj = root.value(u"data"_q).toObject()
			.value(u"rates"_q).toObject();
		if (ratesObj.isEmpty()) {
			LOG(("Komaru Rates Error: empty Coinbase rates."));
			return;
		}
		QHash<QString, double> parsed;
		parsed.reserve(ratesObj.size() + 4);
		parsed.insert(u"USD"_q, 1.0);
		for (auto it = ratesObj.constBegin(); it != ratesObj.constEnd(); ++it) {
			bool ok = false;
			const auto perUsd = it.value().toString().toDouble(&ok);
			if (ok && perUsd > 0.0 && std::isfinite(perUsd)) {
				parsed.insert(it.key().toUpper(), 1.0 / perUsd);
			}
		}
		if (parsed.contains(u"TON"_q)) {
			parsed.insert(u"GRAM"_q, parsed.value(u"TON"_q));
		}
		parsed.insert(u"XTR"_q, kStarsUsdRate);
		st.usdRates = std::move(parsed);
		st.loaded = crl::now();
		LOG(("Komaru Rates: loaded %1 currencies.").arg(st.usdRates.size()));
		st.updated.fire({});
	});
}

[[nodiscard]] QString NormalizeRateCode(const QString &code) {
	return (code == u"GRAM"_q) ? u"TON"_q : code;
}

[[nodiscard]] bool IsKnownCurrencyCode(const QString &code) {
	static const QSet<QString> kSupported = {
		u"USD"_q, u"EUR"_q, u"RUB"_q, u"GBP"_q, u"KZT"_q, u"TRY"_q, u"UAH"_q,
		u"PLN"_q, u"AED"_q, u"CNY"_q, u"JPY"_q, u"BYN"_q, u"ILS"_q, u"CZK"_q,
		u"INR"_q, u"CHF"_q, u"CAD"_q, u"AUD"_q, u"UZS"_q, u"KGS"_q, u"AZN"_q,
		u"AMD"_q, u"GEL"_q, u"KRW"_q, u"THB"_q, u"VND"_q, u"SGD"_q, u"HKD"_q,
		u"BRL"_q, u"MXN"_q, u"SEK"_q, u"NOK"_q, u"DKK"_q, u"HUF"_q, u"RON"_q,
		u"BGN"_q, u"RSD"_q, u"MDL"_q, u"TJS"_q, u"TMT"_q, u"PHP"_q, u"IDR"_q,
		u"MYR"_q, u"NZD"_q, u"ZAR"_q, u"ARS"_q, u"CLP"_q, u"COP"_q, u"PEN"_q,
		u"EGP"_q, u"SAR"_q, u"QAR"_q, u"KWD"_q, u"NGN"_q, u"PKR"_q, u"BDT"_q,
		u"TWD"_q, u"ISK"_q, u"MAD"_q,
		u"TON"_q, u"GRAM"_q, u"BTC"_q, u"ETH"_q, u"SOL"_q, u"USDT"_q,
		u"USDC"_q, u"LTC"_q, u"DOGE"_q, u"XRP"_q, u"BNB"_q, u"TRX"_q,
		u"ADA"_q, u"NOT"_q, u"XTR"_q,
	};
	if (kSupported.contains(code)) {
		return true;
	}
	return Instance().usdRates.contains(NormalizeRateCode(code));
}

[[nodiscard]] std::optional<double> UsdRate(const QString &code) {
	if (code == u"XTR"_q) {
		return kStarsUsdRate;
	}
	const auto &st = Instance();
	const auto key = NormalizeRateCode(code);
	const auto it = st.usdRates.constFind(key);
	if (it != st.usdRates.constEnd() && *it > 0.0) {
		RequestRates(); // background refresh if TTL expired
		return *it;
	}
	RequestRates();
	return std::nullopt;
}

[[nodiscard]] std::optional<double> ExchangeRate(
		const QString &from,
		const QString &to) {
	if (from == to) {
		return 1.0;
	}
	const auto fromUsd = UsdRate(from);
	const auto toUsd = UsdRate(to);
	if (!fromUsd || !toUsd || *toUsd == 0.0) {
		return std::nullopt;
	}
	return (*fromUsd) / (*toUsd);
}

[[nodiscard]] bool HasCyrillic(const QString &text) {
	for (const auto ch : text) {
		const auto u = ch.unicode();
		if ((u >= 0x0400 && u <= 0x04FF) || u == 0x20BD) { // Cyrillic or ₽
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString LocalCurrency(bool russianHint) {
	const auto territory = QLocale::system().territory();
	switch (territory) {
	case QLocale::Russia: return u"RUB"_q;
	case QLocale::Ukraine: return u"UAH"_q;
	case QLocale::Kazakhstan: return u"KZT"_q;
	case QLocale::Belarus: return u"BYN"_q;
	case QLocale::Uzbekistan: return u"UZS"_q;
	case QLocale::Kyrgyzstan: return u"KGS"_q;
	case QLocale::Georgia: return u"GEL"_q;
	case QLocale::Armenia: return u"AMD"_q;
	case QLocale::Azerbaijan: return u"AZN"_q;
	case QLocale::Turkey: return u"TRY"_q;
	case QLocale::Poland: return u"PLN"_q;
	case QLocale::Czechia: return u"CZK"_q;
	case QLocale::UnitedKingdom: return u"GBP"_q;
	case QLocale::Switzerland: return u"CHF"_q;
	case QLocale::UnitedArabEmirates: return u"AED"_q;
	case QLocale::Israel: return u"ILS"_q;
	case QLocale::China: return u"CNY"_q;
	case QLocale::Japan: return u"JPY"_q;
	case QLocale::SouthKorea: return u"KRW"_q;
	case QLocale::India: return u"INR"_q;
	case QLocale::Thailand: return u"THB"_q;
	case QLocale::UnitedStates: return u"USD"_q;
	case QLocale::Germany:
	case QLocale::France:
	case QLocale::Italy:
	case QLocale::Spain:
	case QLocale::Netherlands:
	case QLocale::Austria:
	case QLocale::Finland:
	case QLocale::Portugal:
	case QLocale::Belgium:
	case QLocale::Ireland:
	case QLocale::Greece:
	case QLocale::Estonia:
	case QLocale::Latvia:
	case QLocale::Lithuania:
		return u"EUR"_q;
	default:
		break;
	}
	return russianHint ? u"RUB"_q : u"USD"_q;
}

[[nodiscard]] bool IsImperialSystem() {
	const auto t = QLocale::system().territory();
	return (t == QLocale::UnitedStates)
		|| (t == QLocale::Liberia)
		|| (t == QLocale::Myanmar);
}

// ============================================================================
// Number Formatting (matching exteraGram MathFormat.kt)
// ============================================================================

[[nodiscard]] QString FormatNumber(
		double value,
		QChar decimalSep,
		int maxDecimals = 6,
		bool trimZeros = true) {
	if (!std::isfinite(value) || std::abs(value) >= 1e15) {
		return QString();
	}
	if (std::abs(value) < 1e-12) {
		value = 0.0;
	}
	const auto negative = (value < 0.0);
	const auto absVal = std::abs(value);
	auto raw = QString::number(absVal, 'f', maxDecimals);
	if (trimZeros && raw.contains('.')) {
		while (raw.endsWith('0')) {
			raw.chop(1);
		}
		if (raw.endsWith('.')) {
			raw.chop(1);
		}
	}
	const auto dot = raw.indexOf('.');
	const auto whole = (dot >= 0) ? raw.left(dot) : raw;
	const auto frac = (dot >= 0) ? raw.mid(dot + 1) : QString();
	const auto isZero = (whole == u"0"_q) && frac.isEmpty();

	QString result;
	if (negative && !isZero) {
		result.append(QChar(0x2212)); // Unicode minus
	}
	for (qsizetype i = 0; i < whole.size(); ++i) {
		if (i > 0 && (whole.size() - i) % 3 == 0) {
			result.append(QChar(0x202F)); // narrow no-break space
		}
		result.append(whole[i]);
	}
	if (!frac.isEmpty()) {
		result.append(decimalSep);
		result.append(frac);
	}
	return result;
}

[[nodiscard]] int CurrencyDecimals(const QString &code, double absVal) {
	static const QSet<QString> kZeroDec = {
		u"XTR"_q, u"JPY"_q, u"KRW"_q, u"VND"_q, u"CLP"_q, u"ISK"_q,
	};
	if (kZeroDec.contains(code)) {
		return 0;
	}
	static const QSet<QString> kCrypto = {
		u"BTC"_q, u"ETH"_q, u"TON"_q, u"GRAM"_q, u"SOL"_q, u"LTC"_q,
		u"DOGE"_q, u"XRP"_q, u"BNB"_q, u"TRX"_q, u"ADA"_q, u"NOT"_q,
	};
	if (kCrypto.contains(code)) {
		if (absVal >= 1000.0) return 2;
		if (absVal >= 1.0) return 4;
		return 6;
	}
	if (absVal >= 10000.0) return 0;
	if (absVal >= 0.01 || absVal == 0.0) return 2;
	return 6;
}

// ============================================================================
// Units & Currencies Registry (Currencies.kt + Measures.kt)
// ============================================================================

enum class UnitKind {
	None,
	Currency,
	Length,
	Mass,
	Volume,
	Area,
	Speed,
	Temperature,
	Duration,
};

struct MathUnit {
	QString code;
	QString labelEn;
	QString labelRu;
	UnitKind kind = UnitKind::None;
	double scale = 1.0;
	double offset = 0.0;

	[[nodiscard]] QString label(bool russian) const {
		return (russian && !labelRu.isEmpty()) ? labelRu : labelEn;
	}
	[[nodiscard]] double toBase(double v) const {
		return v * scale + offset;
	}
	[[nodiscard]] double fromBase(double b) const {
		return (b - offset) / scale;
	}
};

struct CurrencyRegistry {
	QHash<QChar, QString> symbols;
	QHash<QString, QString> labels;
	QHash<QString, QString> words;
	QSet<QString> lowercaseCodes;

	CurrencyRegistry() {
		symbols = {
			{ QChar('$'), u"USD"_q },
			{ QChar(0x20AC), u"EUR"_q }, // €
			{ QChar(0x20BD), u"RUB"_q }, // ₽
			{ QChar(0x00A3), u"GBP"_q }, // £
			{ QChar(0x00A5), u"JPY"_q }, // ¥
			{ QChar(0x20B8), u"KZT"_q }, // ₸
			{ QChar(0x20B4), u"UAH"_q }, // ₴
			{ QChar(0x20BA), u"TRY"_q }, // ₺
			{ QChar(0x20B9), u"INR"_q }, // ₹
			{ QChar(0x20AA), u"ILS"_q }, // ₪
			{ QChar(0x20A9), u"KRW"_q }, // ₩
			{ QChar(0x20BE), u"GEL"_q }, // ₾
			{ QChar(0x20BC), u"AZN"_q }, // ₼
			{ QChar(0x058F), u"AMD"_q }, // ֏
			{ QChar(0x20AB), u"VND"_q }, // ₫
			{ QChar(0x0E3F), u"THB"_q }, // ฿
			{ QChar(0x20B1), u"PHP"_q }, // ₱
			{ QChar(0x20A6), u"NGN"_q }, // ₦
			{ QChar(0x20BF), u"BTC"_q }, // ₿
			{ QChar(0x2B50), u"XTR"_q }, // ⭐
			{ QChar(0x2605), u"XTR"_q }, // ★
		};
		for (auto it = symbols.constBegin(); it != symbols.constEnd(); ++it) {
			if (it.value() != u"BTC"_q && it.key() != QChar(0x2605)) {
				labels.insert(it.value(), QString(it.key()));
			}
		}

		lowercaseCodes = {
			u"usd"_q, u"eur"_q, u"rub"_q, u"gbp"_q, u"kzt"_q, u"uah"_q,
			u"byn"_q, u"cny"_q, u"jpy"_q, u"pln"_q, u"czk"_q, u"aed"_q,
			u"inr"_q, u"ils"_q, u"chf"_q, u"cad"_q, u"aud"_q, u"uzs"_q,
			u"kgs"_q, u"azn"_q, u"krw"_q, u"thb"_q, u"vnd"_q, u"sgd"_q,
			u"hkd"_q, u"brl"_q, u"mxn"_q, u"sek"_q, u"huf"_q, u"try"_q,
			u"gel"_q, u"amd"_q, u"btc"_q, u"eth"_q, u"usdt"_q, u"usdc"_q,
			u"ton"_q, u"sol"_q, u"ltc"_q, u"doge"_q, u"xrp"_q, u"bnb"_q,
			u"trx"_q, u"not"_q, u"xtr"_q,
		};

		const auto add = [&](const QString &code, std::initializer_list<const char16_t*> forms) {
			for (const auto *f : forms) {
				words.insert(QString::fromUtf16(f), code);
			}
		};
		add(u"USD"_q, { u"доллар", u"доллара", u"долларов", u"доллары", u"долларах", u"долл",
			u"бакс", u"бакса", u"баксов", u"баксы", u"баксах", u"dollar", u"dollars", u"buck", u"bucks" });
		add(u"EUR"_q, { u"евро", u"euro", u"euros" });
		add(u"RUB"_q, { u"рубль", u"рубля", u"рублей", u"рубли", u"рублях", u"руб", u"р",
			u"ruble", u"rubles", u"rouble", u"roubles" });
		add(u"GBP"_q, { u"фунт", u"фунта", u"фунтов", u"фунты", u"фунтах" });
		add(u"KZT"_q, { u"тенге", u"tenge" });
		add(u"UAH"_q, { u"гривна", u"гривны", u"гривен", u"гривнах", u"грн", u"hryvnia", u"hryvnias" });
		add(u"CNY"_q, { u"юань", u"юаня", u"юаней", u"юани", u"юанях", u"yuan" });
		add(u"JPY"_q, { u"иена", u"иены", u"иен", u"иенах", u"йена", u"йены", u"йен", u"йенах", u"yen" });
		add(u"TRY"_q, { u"лира", u"лиры", u"лир", u"лирах", u"lira", u"liras" });
		add(u"PLN"_q, { u"злотый", u"злотого", u"злотых", u"злотые", u"zloty", u"zlotys", u"zł" });
		add(u"CZK"_q, { u"kč", u"крон", u"крона", u"кроны", u"кронах" });
		add(u"INR"_q, { u"рупия", u"рупии", u"рупий", u"рупиях", u"rupee", u"rupees" });
		add(u"ILS"_q, { u"шекель", u"шекеля", u"шекелей", u"шекели", u"шекелях", u"shekel", u"shekels" });
		add(u"CHF"_q, { u"франк", u"франка", u"франков", u"франки", u"франках", u"franc", u"francs" });
		add(u"AED"_q, { u"дирхам", u"дирхама", u"дирхамов", u"дирхамы", u"дирхамах", u"dirham", u"dirhams" });
		add(u"THB"_q, { u"бат", u"бата", u"батов", u"баты", u"батах", u"baht" });
		add(u"GEL"_q, { u"лари", u"lari" });
		add(u"AMD"_q, { u"драм", u"драмов", u"драмы", u"драмах", u"dram", u"drams" });
		add(u"UZS"_q, { u"сум", u"сумов", u"сумы", u"сумах" });
		add(u"KGS"_q, { u"сом", u"сомов", u"сомах", u"som" });
		add(u"BTC"_q, { u"биткоин", u"биткоина", u"биткоинов", u"биткоины", u"биткоинах",
			u"биткойн", u"биткойна", u"биткойнов", u"биткойны", u"биткойнах",
			u"биток", u"битка", u"битков", u"бтц", u"bitcoin", u"bitcoins" });
		add(u"ETH"_q, { u"эфириум", u"эфириума", u"эфириумов", u"эфириумах",
			u"эфир", u"эфира", u"эфиров", u"ethereum", u"ether" });
		add(u"GRAM"_q, { u"gram", u"grams", u"грам", u"грама", u"грамов", u"грамы", u"грамах",
			u"ton", u"тон", u"тона", u"тонов", u"тонах", u"toncoin", u"toncoins",
			u"тонкоин", u"тонкоина", u"тонкоинов" });
		add(u"USDT"_q, { u"tether", u"юсдт", u"тезер" });
		add(u"LTC"_q, { u"лтц", u"лайт", u"лайтов", u"лайткоин", u"litecoin" });
		add(u"SOL"_q, { u"солана", u"соланы", u"солан", u"solana" });
		add(u"XTR"_q, { u"звезда", u"звезды", u"звёзды", u"звезд", u"звёзд",
			u"звездах", u"звёздах", u"звездами", u"звёздами", u"звезду", u"star", u"stars" });
	}

	[[nodiscard]] QString labelFor(const QString &code) const {
		return labels.value(code, code);
	}

	[[nodiscard]] std::optional<MathUnit> fromSymbol(QChar ch) const {
		const auto it = symbols.constFind(ch);
		if (it == symbols.constEnd()) {
			return std::nullopt;
		}
		const auto code = *it;
		return MathUnit{ code, labelFor(code), labelFor(code), UnitKind::Currency, 1.0, 0.0 };
	}

	[[nodiscard]] std::optional<MathUnit> fromWord(const QString &word) const {
		const auto lower = word.toLower();
		const auto it = words.constFind(lower);
		QString code;
		bool isAlias = false;
		if (it != words.constEnd()) {
			code = *it;
			isAlias = true;
		} else {
			bool allUpperAscii = !word.isEmpty() && word.size() <= 5;
			for (const auto ch : word) {
				if (ch < 'A' || ch > 'Z') {
					allUpperAscii = false;
					break;
				}
			}
			if (allUpperAscii) {
				code = word;
			} else if (lowercaseCodes.contains(lower)) {
				code = lower.toUpper();
			} else {
				return std::nullopt;
			}
		}
		if (!IsKnownCurrencyCode(code)) {
			return std::nullopt;
		}
		const auto lbl = isAlias ? labelFor(code) : code;
		return MathUnit{ code, lbl, lbl, UnitKind::Currency, 1.0, 0.0 };
	}
};

[[nodiscard]] const CurrencyRegistry &Currencies() {
	static const auto reg = CurrencyRegistry();
	return reg;
}

struct MeasureRegistry {
	struct Ladder {
		bool imperial = false;
		std::vector<QString> codes;
	};

	QHash<QString, MathUnit> units;
	QHash<QString, QString> words;
	QHash<QString, QString> counterparts;
	QHash<QString, QString> compounds;
	QHash<QString, Ladder> ladders;

	MeasureRegistry() {
		const auto add = [&](
				const QString &code,
				const QString &en,
				const QString &ru,
				UnitKind kind,
				double scale,
				const QString &counterpart,
				std::initializer_list<const char16_t*> forms,
				double offset = 0.0) {
			units.insert(code, MathUnit{ code, en, ru, kind, scale, offset });
			if (!counterpart.isEmpty()) {
				counterparts.insert(code, counterpart);
			}
			for (const auto *f : forms) {
				words.insert(QString::fromUtf16(f), code);
			}
		};

		// Length
		add(u"mm"_q, u"mm"_q, u"мм"_q, UnitKind::Length, 0.001, u"in"_q,
			{ u"mm", u"мм", u"миллиметр", u"миллиметра", u"миллиметров", u"миллиметрах",
			  u"millimeter", u"millimeters", u"millimetre", u"millimetres" });
		add(u"cm"_q, u"cm"_q, u"см"_q, UnitKind::Length, 0.01, u"in"_q,
			{ u"cm", u"см", u"сантиметр", u"сантиметра", u"сантиметров", u"сантиметрах",
			  u"centimeter", u"centimeters", u"centimetre", u"centimetres" });
		add(u"m"_q, u"m"_q, u"м"_q, UnitKind::Length, 1.0, u"ft"_q,
			{ u"m", u"м", u"метр", u"метра", u"метров", u"метрах", u"meter", u"meters", u"metre", u"metres" });
		add(u"km"_q, u"km"_q, u"км"_q, UnitKind::Length, 1000.0, u"mi"_q,
			{ u"km", u"км", u"километр", u"километра", u"километров", u"километрах",
			  u"kilometer", u"kilometers", u"kilometre", u"kilometres" });
		add(u"in"_q, u"in"_q, u"in"_q, UnitKind::Length, 0.0254, u"cm"_q,
			{ u"inch", u"inches", u"дюйм", u"дюйма", u"дюймов", u"дюймах" });
		add(u"ft"_q, u"ft"_q, u"ft"_q, UnitKind::Length, 0.3048, u"m"_q,
			{ u"ft", u"foot", u"feet", u"фут", u"фута", u"футов", u"футах" });
		add(u"yd"_q, u"yd"_q, u"yd"_q, UnitKind::Length, 0.9144, u"m"_q,
			{ u"yd", u"yard", u"yards", u"ярд", u"ярда", u"ярдов", u"ярдах" });
		add(u"mi"_q, u"mi"_q, u"mi"_q, UnitKind::Length, 1609.344, u"km"_q,
			{ u"mi", u"mile", u"miles", u"миля", u"мили", u"миль", u"милях" });

		// Mass
		add(u"mg"_q, u"mg"_q, u"мг"_q, UnitKind::Mass, 0.000001, QString(),
			{ u"mg", u"мг", u"миллиграмм", u"миллиграмма", u"миллиграммов", u"milligram", u"milligrams" });
		add(u"g"_q, u"g"_q, u"г"_q, UnitKind::Mass, 0.001, u"oz"_q,
			{ u"g", u"г", u"гр", u"грамм", u"грамма", u"граммов", u"граммах", u"gramme", u"grammes" });
		add(u"kg"_q, u"kg"_q, u"кг"_q, UnitKind::Mass, 1.0, u"lb"_q,
			{ u"kg", u"кг", u"килограмм", u"килограмма", u"килограммов", u"килограммах",
			  u"kilogram", u"kilograms", u"kilo", u"kilos" });
		add(u"t"_q, u"t"_q, u"т"_q, UnitKind::Mass, 1000.0, u"lb"_q,
			{ u"t", u"т", u"тонна", u"тонны", u"тонн", u"тоннах", u"tonne", u"tonnes" });
		add(u"oz"_q, u"oz"_q, u"oz"_q, UnitKind::Mass, 0.028349523125, u"g"_q,
			{ u"oz", u"ounce", u"ounces", u"унция", u"унции", u"унций", u"унциях" });
		add(u"lb"_q, u"lb"_q, u"lb"_q, UnitKind::Mass, 0.45359237, u"kg"_q,
			{ u"lb", u"lbs", u"pound", u"pounds", u"фунт", u"фунта", u"фунтов", u"фунтах" });

		// Volume
		add(u"ml"_q, u"ml"_q, u"мл"_q, UnitKind::Volume, 0.001, u"floz"_q,
			{ u"ml", u"мл", u"миллилитр", u"миллилитра", u"миллилитров", u"миллилитрах",
			  u"milliliter", u"milliliters", u"millilitre", u"millilitres" });
		add(u"l"_q, u"l"_q, u"л"_q, UnitKind::Volume, 1.0, u"gal"_q,
			{ u"l", u"л", u"литр", u"литра", u"литров", u"литрах", u"liter", u"liters", u"litre", u"litres" });
		add(u"floz"_q, u"fl oz"_q, u"fl oz"_q, UnitKind::Volume, 0.0295735295625, u"ml"_q,
			{ u"floz" });
		add(u"gal"_q, u"gal"_q, u"gal"_q, UnitKind::Volume, 3.785411784, u"l"_q,
			{ u"gal", u"gallon", u"gallons", u"галлон", u"галлона", u"галлонов", u"галлонах" });

		// Area
		add(u"m2"_q, u"m²"_q, u"м²"_q, UnitKind::Area, 1.0, u"ft2"_q,
			{ u"m2", u"m²", u"м2", u"м²", u"sqm" });
		add(u"km2"_q, u"km²"_q, u"км²"_q, UnitKind::Area, 1000000.0, u"mi2"_q,
			{ u"km2", u"km²", u"км2", u"км²" });
		add(u"ha"_q, u"ha"_q, u"га"_q, UnitKind::Area, 10000.0, u"acre"_q,
			{ u"ha", u"га", u"гектар", u"гектара", u"гектаров", u"гектарах", u"hectare", u"hectares" });
		add(u"ft2"_q, u"ft²"_q, u"ft²"_q, UnitKind::Area, 0.09290304, u"m2"_q,
			{ u"ft2", u"ft²", u"sqft" });
		add(u"mi2"_q, u"mi²"_q, u"mi²"_q, UnitKind::Area, 2589988.110336, u"km2"_q,
			{ u"mi2", u"mi²" });
		add(u"acre"_q, u"ac"_q, u"ac"_q, UnitKind::Area, 4046.8564224, u"ha"_q,
			{ u"ac", u"acre", u"acres", u"акр", u"акра", u"акров", u"акрах" });

		// Speed
		add(u"kmh"_q, u"km/h"_q, u"км/ч"_q, UnitKind::Speed, 1.0 / 3.6, u"mph"_q,
			{ u"kmh", u"kph" });
		add(u"ms"_q, u"m/s"_q, u"м/с"_q, UnitKind::Speed, 1.0, u"kmh"_q,
			{ u"mps" });
		add(u"mph"_q, u"mph"_q, u"mph"_q, UnitKind::Speed, 0.44704, u"kmh"_q,
			{ u"mph" });
		add(u"knot"_q, u"kn"_q, u"уз"_q, UnitKind::Speed, 1852.0 / 3600.0, u"kmh"_q,
			{ u"knot", u"knots", u"узел", u"узла", u"узлов", u"узлах" });

		// Temperature
		add(u"C"_q, u"°C"_q, u"°C"_q, UnitKind::Temperature, 1.0, u"F"_q,
			{ u"celsius", u"цельсий", u"цельсия", u"цельсию", u"цельсиях" },
			kCelsiusOffset);
		add(u"F"_q, u"°F"_q, u"°F"_q, UnitKind::Temperature, kFahrenheitScale, u"C"_q,
			{ u"fahrenheit", u"фаренгейт", u"фаренгейта", u"фаренгейту", u"фаренгейтах" },
			kCelsiusOffset - 32.0 * kFahrenheitScale);
		add(u"K"_q, u"K"_q, u"K"_q, UnitKind::Temperature, 1.0, u"C"_q,
			{ u"kelvin", u"kelvins", u"кельвин", u"кельвина", u"кельвинов", u"кельвинах" },
			0.0);

		// Duration
		add(u"msec"_q, u"ms"_q, u"мс"_q, UnitKind::Duration, 0.001, QString(),
			{ u"ms", u"мс", u"millisecond", u"milliseconds", u"миллисекунда", u"миллисекунды", u"миллисекунд" });
		add(u"s"_q, u"s"_q, u"с"_q, UnitKind::Duration, 1.0, QString(),
			{ u"s", u"sec", u"secs", u"second", u"seconds", u"с", u"сек",
			  u"секунда", u"секунды", u"секунд", u"секундах", u"секунду" });
		add(u"min"_q, u"min"_q, u"мин"_q, UnitKind::Duration, 60.0, QString(),
			{ u"min", u"mins", u"minute", u"minutes", u"мин",
			  u"минута", u"минуты", u"минут", u"минутах", u"минуту" });
		add(u"h"_q, u"h"_q, u"ч"_q, UnitKind::Duration, 3600.0, QString(),
			{ u"h", u"hr", u"hrs", u"hour", u"hours", u"ч", u"час", u"часа", u"часов", u"часах" });
		add(u"d"_q, u"d"_q, u"д"_q, UnitKind::Duration, 86400.0, QString(),
			{ u"d", u"day", u"days", u"д", u"дн", u"день", u"дня", u"дней", u"днях", u"сут", u"сутки", u"суток" });
		add(u"w"_q, u"w"_q, u"нед"_q, UnitKind::Duration, 604800.0, QString(),
			{ u"w", u"wk", u"week", u"weeks", u"нед", u"неделя", u"недели", u"недель", u"неделях", u"неделю" });
		add(u"mo"_q, u"mo"_q, u"мес"_q, UnitKind::Duration, 2592000.0, QString(),
			{ u"mo", u"month", u"months", u"мес", u"месяц", u"месяца", u"месяцев", u"месяцах" });
		add(u"y"_q, u"y"_q, u"г."_q, UnitKind::Duration, 31536000.0, QString(),
			{ u"y", u"yr", u"yrs", u"year", u"years", u"год", u"года", u"лет", u"годах" });

		compounds.insert(u"km/h"_q, u"kmh"_q);
		compounds.insert(u"км/ч"_q, u"kmh"_q);
		compounds.insert(u"m/s"_q, u"ms"_q);
		compounds.insert(u"м/с"_q, u"ms"_q);
		compounds.insert(u"fl oz"_q, u"floz"_q);

		const auto addLadder = [&](bool imperial, std::vector<QString> codes) {
			const auto l = Ladder{ imperial, codes };
			for (const auto &c : codes) {
				ladders.insert(c, l);
			}
			return l;
		};
		addLadder(false, { u"mm"_q, u"cm"_q, u"m"_q, u"km"_q });
		addLadder(false, { u"mg"_q, u"g"_q, u"kg"_q, u"t"_q });
		addLadder(false, { u"ml"_q, u"l"_q });
		addLadder(false, { u"m2"_q, u"ha"_q, u"km2"_q });
		ladders.insert(u"yd"_q, addLadder(true, { u"in"_q, u"ft"_q, u"mi"_q }));
		addLadder(true, { u"oz"_q, u"lb"_q });
		addLadder(true, { u"floz"_q, u"gal"_q });
		addLadder(true, { u"ft2"_q, u"acre"_q, u"mi2"_q });
	}

	[[nodiscard]] std::optional<MathUnit> fromWord(const QString &word) const {
		const auto it = words.constFind(word.toLower());
		if (it == words.constEnd()) {
			return std::nullopt;
		}
		return units.value(*it);
	}

	[[nodiscard]] std::optional<MathUnit> fromCompound(const QString &key) const {
		const auto it = compounds.constFind(key.toLower());
		if (it == compounds.constEnd()) {
			return std::nullopt;
		}
		return units.value(*it);
	}

	[[nodiscard]] std::optional<MathUnit> temperature(QChar scale) const {
		switch (scale.unicode()) {
		case 'C': case 'c': case 0x0421: case 0x0441: return units.value(u"C"_q); // C / С
		case 'F': case 'f': case 0x0424: case 0x0444: return units.value(u"F"_q); // F / Ф
		case 'K': case 'k': case 0x041A: case 0x043A: return units.value(u"K"_q); // K / К
		default: return std::nullopt;
		}
	}

	[[nodiscard]] std::optional<MathUnit> fromSymbol(QChar ch) const {
		if (ch.unicode() == 0x2103) return units.value(u"C"_q); // ℃
		if (ch.unicode() == 0x2109) return units.value(u"F"_q); // ℉
		return std::nullopt;
	}

	[[nodiscard]] std::optional<MathUnit> implicitTarget(
			const MathUnit &unit,
			double value,
			bool imperial) const {
		const auto lIt = ladders.constFind(unit.code);
		if (lIt != ladders.constEnd() && lIt->imperial == imperial) {
			const auto base = std::abs(unit.toBase(value));
			for (auto i = int(lIt->codes.size()) - 1; i >= 0; --i) {
				const auto &code = lIt->codes[i];
				const auto &target = units.value(code);
				const auto amount = target.fromBase(base);
				if (code != unit.code && amount >= 0.1 && amount < 1000.0) {
					return target;
				}
			}
		}
		const auto cIt = counterparts.constFind(unit.code);
		if (cIt != counterparts.constEnd()) {
			return units.value(*cIt);
		}
		return std::nullopt;
	}

	[[nodiscard]] QString formatDuration(
			double seconds,
			QChar decimalSep,
			bool russian) const {
		if (!std::isfinite(seconds) || std::abs(seconds) >= 1e12) {
			return QString();
		}
		const auto absSec = std::abs(seconds);
		if (absSec < 60.0) {
			const auto num = FormatNumber(seconds, decimalSep, 3, true);
			if (num.isEmpty()) return QString();
			return num + QChar(0x00A0) + units.value(u"s"_q).label(russian);
		}
		auto rest = std::llround(absSec);
		static const QString kBreakdown[] = { u"d"_q, u"h"_q, u"min"_q, u"s"_q };
		QString out;
		if (seconds < 0.0) {
			out.append(QChar(0x2212));
		}
		bool first = true;
		for (const auto &code : kBreakdown) {
			const auto &u = units.value(code);
			const auto step = static_cast<long long>(u.scale);
			const auto count = rest / step;
			if (count > 0) {
				if (!first) out.append(' ');
				first = false;
				out.append(QString::number(count));
				out.append(QChar(0x00A0));
				out.append(u.label(russian));
				rest -= count * step;
			}
		}
		if (first) {
			out.append(u"0"_q).append(QChar(0x00A0)).append(units.value(u"s"_q).label(russian));
		}
		return out;
	}
};

[[nodiscard]] const MeasureRegistry &Measures() {
	static const auto reg = MeasureRegistry();
	return reg;
}

// ============================================================================
// Time Zones (TimeZones.kt)
// ============================================================================

[[nodiscard]] QString NormalizeZoneName(QString name) {
	name = name.toLower();
	name.replace(QChar(0x0451), QChar(0x0435)); // ё -> е
	name.replace('-', ' ');
	return name;
}

[[nodiscard]] const QHash<QString, QByteArray> &ZoneMap() {
	static const auto map = [] {
		QHash<QString, QByteArray> m;
		const auto add = [&](const char *iana, std::initializer_list<const char16_t*> names) {
			const auto id = QByteArray(iana);
			for (const auto *n : names) {
				m.insert(NormalizeZoneName(QString::fromUtf16(n)), id);
			}
		};
		add("UTC", { u"utc" });
		add("GMT", { u"gmt" });
		add("Europe/Moscow", { u"msk", u"мск", u"москва", u"москве", u"moscow", u"питер", u"питере",
			u"спб", u"петербург", u"петербурге", u"санкт-петербург", u"санкт-петербурге",
			u"st petersburg", u"saint petersburg" });
		add("Europe/Kaliningrad", { u"калининград", u"калининграде", u"kaliningrad" });
		add("Europe/Samara", { u"самара", u"самаре", u"samara" });
		add("Asia/Yekaterinburg", { u"екб", u"екатеринбург", u"екатеринбурге", u"yekaterinburg" });
		add("Asia/Omsk", { u"омск", u"омске", u"omsk" });
		add("Asia/Novosibirsk", { u"нск", u"новосибирск", u"новосибирске", u"novosibirsk" });
		add("Asia/Krasnoyarsk", { u"красноярск", u"красноярске", u"krasnoyarsk" });
		add("Asia/Irkutsk", { u"иркутск", u"иркутске", u"irkutsk" });
		add("Asia/Yakutsk", { u"якутск", u"якутске", u"yakutsk" });
		add("Asia/Vladivostok", { u"владивосток", u"владивостоке", u"vladivostok" });
		add("Asia/Magadan", { u"магадан", u"магадане", u"magadan" });
		add("Asia/Kamchatka", { u"камчатка", u"камчатке", u"kamchatka" });
		add("Europe/Kiev", { u"киев", u"киеве", u"київ", u"києві", u"kyiv", u"kiev" });
		add("Europe/Minsk", { u"минск", u"минске", u"minsk" });
		add("Asia/Almaty", { u"алматы", u"almaty", u"астана", u"астане", u"astana" });
		add("Asia/Tashkent", { u"ташкент", u"ташкенте", u"tashkent" });
		add("Asia/Bishkek", { u"бишкек", u"бишкеке", u"bishkek" });
		add("Asia/Dushanbe", { u"душанбе", u"dushanbe" });
		add("Asia/Tbilisi", { u"тбилиси", u"tbilisi" });
		add("Asia/Yerevan", { u"ереван", u"ереване", u"yerevan" });
		add("Asia/Baku", { u"баку", u"baku" });
		add("Europe/Chisinau", { u"кишинев", u"кишиневе", u"chisinau" });
		add("Europe/Riga", { u"рига", u"риге", u"riga" });
		add("Europe/Vilnius", { u"вильнюс", u"вильнюсе", u"vilnius" });
		add("Europe/Tallinn", { u"таллин", u"таллине", u"tallinn" });
		add("Europe/Helsinki", { u"eet", u"eest", u"хельсинки", u"helsinki" });
		add("Europe/Warsaw", { u"варшава", u"варшаве", u"warsaw" });
		add("Europe/Prague", { u"прага", u"праге", u"prague" });
		add("Europe/Berlin", { u"cet", u"cest", u"берлин", u"берлине", u"berlin" });
		add("Europe/Paris", { u"париж", u"париже", u"paris" });
		add("Europe/Rome", { u"рим", u"риме", u"rome" });
		add("Europe/Madrid", { u"мадрид", u"мадриде", u"madrid", u"барселона", u"барселоне", u"barcelona" });
		add("Europe/Amsterdam", { u"амстердам", u"амстердаме", u"amsterdam" });
		add("Europe/London", { u"bst", u"лондон", u"лондоне", u"london" });
		add("Europe/Lisbon", { u"wet", u"west", u"лиссабон", u"лиссабоне", u"lisbon" });
		add("Europe/Istanbul", { u"стамбул", u"стамбуле", u"istanbul", u"анталья", u"анталье", u"antalya" });
		add("Asia/Jerusalem", { u"тель-авив", u"тель-авиве", u"tel aviv", u"иерусалим", u"иерусалиме", u"jerusalem" });
		add("Asia/Dubai", { u"дубай", u"дубае", u"dubai" });
		add("Africa/Cairo", { u"каир", u"каире", u"cairo" });
		add("Asia/Kolkata", { u"ist", u"дели", u"delhi", u"new delhi", u"мумбаи", u"mumbai" });
		add("Asia/Bangkok", { u"бангкок", u"бангкоке", u"bangkok", u"пхукет", u"пхукете", u"phuket" });
		add("Asia/Makassar", { u"бали", u"bali" });
		add("Asia/Singapore", { u"sgt", u"сингапур", u"сингапуре", u"singapore" });
		add("Asia/Hong_Kong", { u"hkt", u"гонконг", u"гонконге", u"hong kong" });
		add("Asia/Shanghai", { u"пекин", u"пекине", u"beijing", u"шанхай", u"шанхае", u"shanghai" });
		add("Asia/Seoul", { u"kst", u"сеул", u"сеуле", u"seoul" });
		add("Asia/Tokyo", { u"jst", u"токио", u"tokyo" });
		add("Australia/Perth", { u"awst", u"перт", u"перте", u"perth" });
		add("Australia/Sydney", { u"aest", u"aedt", u"сидней", u"сиднее", u"sydney" });
		add("Australia/Melbourne", { u"мельбурн", u"мельбурне", u"melbourne" });
		add("Pacific/Auckland", { u"nzst", u"nzdt", u"окленд", u"окленде", u"auckland" });
		add("America/New_York", { u"est", u"edt", u"нью-йорк", u"нью-йорке", u"new york", u"nyc",
			u"вашингтон", u"вашингтоне", u"washington", u"майами", u"miami" });
		add("America/Toronto", { u"торонто", u"toronto" });
		add("America/Chicago", { u"cst", u"cdt", u"чикаго", u"chicago" });
		add("America/Denver", { u"mst", u"mdt", u"денвер", u"денвере", u"denver" });
		add("America/Los_Angeles", { u"pst", u"pdt", u"лос-анджелес", u"лос-анджелесе", u"los angeles",
			u"сан-франциско", u"san francisco", u"сиэтл", u"сиэтле", u"seattle" });
		add("America/Vancouver", { u"ванкувер", u"ванкувере", u"vancouver" });
		add("America/Anchorage", { u"akst", u"akdt", u"анкоридж", u"анкоридже", u"anchorage" });
		add("Pacific/Honolulu", { u"hst", u"гонолулу", u"honolulu" });
		add("America/Mexico_City", { u"мехико", u"mexico city" });
		add("America/Sao_Paulo", { u"сан-паулу", u"sao paulo" });
		add("America/Argentina/Buenos_Aires", { u"буэнос-айрес", u"буэнос-айресе", u"buenos aires" });
		return m;
	}();
	return map;
}

[[nodiscard]] std::optional<QTimeZone> LookupTimeZone(
		const QStringList &words,
		int from,
		int to) {
	QString key;
	for (int i = from; i < to; ++i) {
		if (i > from) key.append(' ');
		key.append(words[i]);
	}
	const auto norm = NormalizeZoneName(key);
	const auto &zmap = ZoneMap();
	const auto tryLookup = [&](const QString &candidate) -> std::optional<QTimeZone> {
		const auto it = zmap.constFind(candidate);
		if (it != zmap.constEnd()) {
			const auto tz = QTimeZone(*it);
			if (tz.isValid()) {
				return tz;
			}
		}
		return std::nullopt;
	};
	if (auto tz = tryLookup(norm)) {
		return tz;
	}
	if (norm.size() > 4 && norm.endsWith(u"е"_q)) {
		const auto stem = norm.chopped(1);
		for (const auto &rep : { u""_q, u"а"_q, u"я"_q, u"ь"_q, u"й"_q }) {
			if (auto tz = tryLookup(stem + rep)) {
				return tz;
			}
		}
	}

	static const QRegularExpression kOffsetRe(
		u"^(utc|gmt|msk|мск) ?([+-]) ?(\\d{1,2})(?::?(\\d{2}))?$"_q);
	const auto match = kOffsetRe.match(key);
	if (match.hasMatch()) {
		const auto prefix = match.captured(1);
		const auto baseMin = (prefix == u"msk"_q || prefix == u"мск"_q) ? 180 : 0;
		const auto hours = match.captured(3).toInt();
		const auto mins = match.captured(4).isEmpty() ? 0 : match.captured(4).toInt();
		if (hours <= 18 && mins <= 59) {
			const auto sign = (match.captured(2) == u"-"_q) ? -1 : 1;
			const auto totalSec = (baseMin + sign * (hours * 60 + mins)) * 60;
			if (std::abs(totalSec) <= 18 * 3600) {
				return QTimeZone::fromSecondsAheadOfUtc(totalSec);
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]] QString FormatClockTime(const QTime &t, bool useMeridiem) {
	const auto minStr = QString::number(t.minute()).rightJustified(2, '0');
	if (!useMeridiem) {
		return QString::number(t.hour()).rightJustified(2, '0') + ':' + minStr;
	}
	const auto h12 = (t.hour() + 11) % 12 + 1;
	const auto suffix = (t.hour() < 12) ? u"am"_q : u"pm"_q;
	return QString::number(h12) + ':' + minStr + ' ' + suffix;
}

[[nodiscard]] QString EvaluateTimeZone(const QString &beforeEquals) {
	const auto slice = (beforeEquals.size() > 64)
		? beforeEquals.right(64)
		: beforeEquals;
	auto lowered = slice.toLower();
	lowered.replace(QChar(0x0451), QChar(0x0435)); // ё -> е
	lowered.replace(QChar(0x2212), QChar('-'));    // − -> -
	lowered.replace(QChar(0x00A0), QChar(' '));
	const auto words = lowered.split(QRegularExpression(u"[ \\t]+"_q), Qt::SkipEmptyParts);
	if (words.isEmpty()) {
		return QString();
	}

	static const QSet<QString> kTargetMarkers = { u"в"_q, u"во"_q, u"на"_q, u"to"_q, u"in"_q };
	static const QSet<QString> kSourceMarkers = { u"по"_q };
	static const QSet<QString> kNowWords = { u"сейчас"_q, u"now"_q };
	static const QSet<QString> kMeridiems = { u"am"_q, u"pm"_q, u"a.m."_q, u"p.m."_q };

	int end = int(words.size());
	std::optional<QTimeZone> target;
	for (int count = 3; count >= 1; --count) {
		if (end - count - 1 < 0 || !kTargetMarkers.contains(words[end - count - 1])) {
			continue;
		}
		if (auto tz = LookupTimeZone(words, end - count, end)) {
			target = tz;
			end -= count + 1;
			break;
		}
	}

	std::optional<QTimeZone> source;
	for (int count = 3; count >= 1; --count) {
		if (end - count < 1) {
			continue;
		}
		if (auto tz = LookupTimeZone(words, end - count, end)) {
			source = tz;
			end -= count;
			if (end > 1 && kSourceMarkers.contains(words[end - 1])) {
				--end;
			}
			break;
		}
	}

	if (end < 1) {
		return QString();
	}
	auto timeToken = words[end - 1];
	if (kMeridiems.contains(timeToken) && end >= 2) {
		timeToken = words[end - 2] + timeToken;
	}
	const auto hasMeridiem = timeToken.endsWith(u"am"_q)
		|| timeToken.endsWith(u"pm"_q)
		|| timeToken.endsWith(u"a.m."_q)
		|| timeToken.endsWith(u"p.m."_q);
	if (!source && !target && !hasMeridiem) {
		return QString();
	}

	const auto localTz = QTimeZone::systemTimeZone();
	const auto nowUtc = QDateTime::currentDateTimeUtc();
	if (kNowWords.contains(timeToken)) {
		if (!target || source) {
			return QString();
		}
		const auto converted = nowUtc.toTimeZone(*target);
		return FormatClockTime(converted.time(), false);
	}

	static const QRegularExpression kTimeRe(
		u"^(\\d{1,2})(?:[:.](\\d{2}))?(am|pm|a\\.m\\.|p\\.m\\.)?$"_q);
	const auto m = kTimeRe.match(timeToken);
	if (!m.hasMatch()) {
		return QString();
	}
	auto hour = m.captured(1).toInt();
	const auto minStr = m.captured(2);
	const auto meridiem = m.captured(3);
	if (minStr.isEmpty() && meridiem.isEmpty() && !source) {
		return QString();
	}
	const auto minute = minStr.isEmpty() ? 0 : minStr.toInt();
	if (minute > 59) {
		return QString();
	}
	if (!meridiem.isEmpty()) {
		if (hour < 1 || hour > 12) {
			return QString();
		}
		hour = (hour % 12) + (meridiem.startsWith('p') ? 12 : 0);
	} else if (hour > 23) {
		return QString();
	}

	if (!source && !target) {
		return FormatClockTime(QTime(hour, minute), false);
	}

	const auto srcTz = source ? *source : localTz;
	const auto dstTz = target ? *target : localTz;
	const auto srcDate = nowUtc.toTimeZone(srcTz).date();
	const auto startDt = QDateTime(srcDate, QTime(hour, minute), srcTz);
	const auto converted = startDt.toTimeZone(dstTz);
	if (converted.date() == startDt.date() && converted.time() == startDt.time()) {
		return QString();
	}
	const auto dayShift = startDt.date().daysTo(converted.date());
	auto result = FormatClockTime(converted.time(), !meridiem.isEmpty());
	if (dayShift > 0) {
		result += u" (+%1)"_q.arg(dayShift);
	} else if (dayShift < 0) {
		result += u" (%1)"_q.arg(dayShift);
	}
	return result;
}

// ============================================================================
// Math + Currency + Units Expression Evaluator (MathExpression.kt + MathOperators.kt)
// ============================================================================

struct Value {
	double num = 0.0;
	std::optional<MathUnit> unit;
	bool isPercent = false;
};

[[nodiscard]] double Factorial(double v) {
	if (v < 0.0 || v != std::floor(v) || v > kMaxFactorial) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double res = 1.0;
	const auto n = static_cast<int>(v);
	for (int i = 2; i <= n; ++i) {
		res *= double(i);
	}
	return res;
}

[[nodiscard]] std::optional<double> LookupMultiplier(const QString &word) {
	static const QHash<QString, double> kMult = {
		{ u"к"_q, 1e3 }, { u"k"_q, 1e3 }, { u"тыс"_q, 1e3 },
		{ u"тысяча"_q, 1e3 }, { u"тысячи"_q, 1e3 }, { u"тысяч"_q, 1e3 },
		{ u"thousand"_q, 1e3 }, { u"thousands"_q, 1e3 },
		{ u"кк"_q, 1e6 }, { u"kk"_q, 1e6 }, { u"млн"_q, 1e6 },
		{ u"миллион"_q, 1e6 }, { u"миллиона"_q, 1e6 }, { u"миллионов"_q, 1e6 },
		{ u"million"_q, 1e6 }, { u"millions"_q, 1e6 },
		{ u"ккк"_q, 1e9 }, { u"kkk"_q, 1e9 }, { u"млрд"_q, 1e9 },
		{ u"миллиард"_q, 1e9 }, { u"миллиарда"_q, 1e9 }, { u"миллиардов"_q, 1e9 },
		{ u"billion"_q, 1e9 }, { u"billions"_q, 1e9 },
	};
	const auto it = kMult.constFind(word.toLower());
	return (it != kMult.constEnd()) ? std::optional<double>(*it) : std::nullopt;
}

[[nodiscard]] bool IsConversionWord(const QString &w) {
	const auto l = w.toLower();
	return (l == u"в"_q) || (l == u"to"_q) || (l == u"in"_q);
}

[[nodiscard]] bool IsPercentOfWord(const QString &w) {
	const auto l = w.toLower();
	return (l == u"от"_q) || (l == u"of"_q);
}

[[nodiscard]] std::optional<MathUnit> LookupAnyUnit(
		const QString &word,
		std::optional<UnitKind> preferKind = std::nullopt) {
	if (preferKind && *preferKind == UnitKind::Currency) {
		if (auto c = Currencies().fromWord(word)) return c;
	} else if (preferKind && *preferKind != UnitKind::None) {
		if (auto m = Measures().fromWord(word)) return m;
	}
	if (auto c = Currencies().fromWord(word)) return c;
	if (auto m = Measures().fromWord(word)) return m;
	return std::nullopt;
}

[[nodiscard]] std::optional<double> ConvertValue(
		double amount,
		const MathUnit &from,
		const MathUnit &to) {
	if (from.kind != to.kind) {
		return std::nullopt;
	}
	if (from.code == to.code) {
		return amount;
	}
	if (from.kind == UnitKind::Currency) {
		const auto rate = ExchangeRate(from.code, to.code);
		return rate ? std::optional<double>(amount * (*rate)) : std::nullopt;
	}
	return to.fromBase(from.toBase(amount));
}

// Base conversion helper: hex(255), bin(10), oct(64), dec(ff, 16)
[[nodiscard]] QString TryBaseConversion(const QString &expr) {
	static const QRegularExpression kBaseRe(
		u"^\\s*(hex|bin|oct|dec)\\s*\\(\\s*([^,;\\)]+)(?:\\s*[,;]\\s*(\\d+))?\\s*\\)\\s*$"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto m = kBaseRe.match(expr);
	if (!m.hasMatch()) {
		return QString();
	}
	const auto fn = m.captured(1).toLower();
	auto arg = m.captured(2).trimmed();
	const auto radixArg = m.captured(3).trimmed();

	if (fn == u"dec"_q) {
		int radix = 10;
		if (!radixArg.isEmpty()) {
			radix = radixArg.toInt();
		} else if (arg.startsWith(u"0x"_q, Qt::CaseInsensitive)) {
			radix = 16;
			arg = arg.mid(2);
		} else if (arg.startsWith(u"0b"_q, Qt::CaseInsensitive)) {
			radix = 2;
			arg = arg.mid(2);
		} else if (arg.startsWith(u"0o"_q, Qt::CaseInsensitive)) {
			radix = 8;
			arg = arg.mid(2);
		}
		if (radix < 2 || radix > 36) {
			return QString();
		}
		if (radix == 16 && arg.startsWith(u"0x"_q, Qt::CaseInsensitive)) arg = arg.mid(2);
		if (radix == 2 && arg.startsWith(u"0b"_q, Qt::CaseInsensitive)) arg = arg.mid(2);
		if (radix == 8 && arg.startsWith(u"0o"_q, Qt::CaseInsensitive)) arg = arg.mid(2);
		bool ok = false;
		const auto val = arg.toLongLong(&ok, radix);
		return ok ? QString::number(val) : QString();
	}

	bool ok = false;
	const auto val = arg.toLongLong(&ok, 0);
	if (!ok || val < 0) {
		return QString();
	}
	if (fn == u"hex"_q) return u"0x"_q + QString::number(val, 16);
	if (fn == u"bin"_q) return u"0b"_q + QString::number(val, 2);
	if (fn == u"oct"_q) return u"0o"_q + QString::number(val, 8);
	return QString();
}

class ExprParser {
public:
	explicit ExprParser(const QString &input) : _s(input) {}

	int operators = 0;
	bool hasBaseLiteral = false;
	std::optional<MathUnit> explicitTarget;

	[[nodiscard]] std::optional<Value> parse() {
		auto val = parseAddSub(0);
		if (!val) {
			return std::nullopt;
		}
		skipSpaces();
		// Optional trailing conversion: "в <unit>" / "to <unit>" / "in <unit>"
		if (_pos < _s.size()) {
			const auto saved = _pos;
			const auto w = readWord();
			if (IsConversionWord(w)) {
				skipSpaces();
				auto target = readUnitOrSymbol(val->unit ? std::optional(val->unit->kind) : std::nullopt);
				if (target) {
					explicitTarget = *target;
					++operators;
				} else {
					_pos = saved;
				}
			} else {
				_pos = saved;
			}
		}
		skipSpaces();
		if (_pos != _s.size()) {
			return std::nullopt;
		}
		return val;
	}

private:
	void skipSpaces() {
		while (_pos < _s.size() && (_s[_pos].isSpace() || _s[_pos].unicode() == 0x00A0)) {
			++_pos;
		}
	}

	[[nodiscard]] QChar peek() {
		skipSpaces();
		return (_pos < _s.size()) ? _s[_pos] : QChar();
	}

	bool eat(QChar c) {
		skipSpaces();
		if (_pos < _s.size() && _s[_pos] == c) {
			++_pos;
			return true;
		}
		return false;
	}

	[[nodiscard]] QString readWord() {
		skipSpaces();
		const auto start = _pos;
		while (_pos < _s.size()
			&& (_s[_pos].isLetter()
				|| _s[_pos].unicode() == 0x00B2 // ²
				|| (_pos > start && _s[_pos].isDigit() && _s[_pos] == '2'))) {
			++_pos;
		}
		return _s.mid(start, _pos - start);
	}

	[[nodiscard]] std::optional<MathUnit> readUnitOrSymbol(
			std::optional<UnitKind> preferKind) {
		skipSpaces();
		if (_pos >= _s.size()) {
			return std::nullopt;
		}
		const auto ch = _s[_pos];
		// Degree sign: °C / °F / °K
		if (ch.unicode() == 0x00B0 && _pos + 1 < _s.size()) {
			if (auto t = Measures().temperature(_s[_pos + 1])) {
				_pos += 2;
				return t;
			}
		}
		if (auto u = Measures().fromSymbol(ch)) {
			++_pos;
			return u;
		}
		if (auto c = Currencies().fromSymbol(ch)) {
			++_pos;
			return c;
		}
		const auto saved = _pos;
		const auto w1 = readWord();
		if (w1.isEmpty()) {
			return std::nullopt;
		}
		// Compound units: km/h, км/ч, m/s, м/с, fl oz
		if (_pos < _s.size() && _s[_pos] == '/') {
			const auto afterSlash = _pos + 1;
			_pos = afterSlash;
			const auto w2 = readWord();
			if (!w2.isEmpty()) {
				if (auto comp = Measures().fromCompound(w1 + '/' + w2)) {
					return comp;
				}
			}
			_pos = afterSlash - 1;
		} else if (w1.toLower() == u"fl"_q) {
			const auto afterW1 = _pos;
			const auto w2 = readWord();
			if (w2.toLower() == u"oz"_q) {
				if (auto comp = Measures().fromCompound(u"fl oz"_q)) {
					return comp;
				}
			}
			_pos = afterW1;
		}
		if (auto u = LookupAnyUnit(w1, preferKind)) {
			return u;
		}
		_pos = saved;
		return std::nullopt;
	}

	[[nodiscard]] std::optional<Value> parseAddSub(int depth) {
		auto left = parseMulDiv(depth);
		if (!left) return std::nullopt;
		while (true) {
			const auto ch = peek();
			const auto isAdd = (ch == '+');
			const auto isSub = (ch == '-' || ch.unicode() == 0x2212);
			if (!isAdd && !isSub) {
				return left;
			}
			++_pos;
			auto right = parseMulDiv(depth);
			if (!right) return std::nullopt;
			++operators;
			if (right->isPercent && !right->unit) {
				left->num = isAdd
					? (left->num + left->num * right->num)
					: (left->num - left->num * right->num);
				left->isPercent = false;
			} else if (left->unit && right->unit) {
				if (left->unit->kind != right->unit->kind) {
					return std::nullopt;
				}
				const auto conv = ConvertValue(right->num, *right->unit, *left->unit);
				if (!conv) return std::nullopt;
				left->num = isAdd ? (left->num + *conv) : (left->num - *conv);
			} else if (!left->unit && !right->unit) {
				left->num = isAdd ? (left->num + right->num) : (left->num - right->num);
			} else {
				return std::nullopt;
			}
		}
	}

	[[nodiscard]] std::optional<Value> parseMulDiv(int depth) {
		auto left = parsePower(depth);
		if (!left) return std::nullopt;
		while (true) {
			// "12% от 1200" / "12% of 1200"
			if (left->isPercent && !left->unit) {
				const auto saved = _pos;
				const auto w = readWord();
				if (IsPercentOfWord(w)) {
					auto right = parsePower(depth);
					if (!right) return std::nullopt;
					++operators;
					left = Value{ left->num * right->num, right->unit, false };
					continue;
				}
				_pos = saved;
			}

			const auto ch = peek();
			const auto isMul = (ch == '*' || ch.unicode() == 0x00D7 || ch.unicode() == 0x00B7);
			const auto isDiv = (ch == '/' || ch.unicode() == 0x00F7);
			if (!isMul && !isDiv) {
				// Implicit multiplication before '(' or '√' or constant 'π'
				if (ch == '(' || ch.unicode() == 0x221A || ch.unicode() == 0x03C0) {
					auto right = parsePower(depth);
					if (!right) return std::nullopt;
					++operators;
					if (left->unit && right->unit) return std::nullopt;
					left->num *= right->num;
					if (!left->unit) left->unit = right->unit;
					left->isPercent = false;
					continue;
				}
				return left;
			}
			++_pos;
			auto right = parsePower(depth);
			if (!right) return std::nullopt;
			++operators;
			if (isMul) {
				if (left->unit && right->unit) return std::nullopt;
				left->num *= right->num;
				if (!left->unit) left->unit = right->unit;
			} else {
				if (right->num == 0.0) return std::nullopt;
				if (left->unit && right->unit) {
					if (left->unit->kind != right->unit->kind) return std::nullopt;
					const auto conv = ConvertValue(right->num, *right->unit, *left->unit);
					if (!conv || *conv == 0.0) return std::nullopt;
					left->num /= *conv;
					left->unit.reset();
				} else if (!right->unit) {
					left->num /= right->num;
				} else {
					return std::nullopt;
				}
			}
			left->isPercent = false;
		}
	}

	[[nodiscard]] std::optional<Value> parsePower(int depth) {
		const auto ch = peek();
		if (ch == '-' || ch.unicode() == 0x2212) {
			++_pos;
			auto v = parsePower(depth);
			if (!v) return std::nullopt;
			v->num = -v->num;
			return v;
		}
		if (ch == '+') {
			++_pos;
			return parsePower(depth);
		}
		if (ch.unicode() == 0x221A) { // √
			++_pos;
			auto v = parsePower(depth);
			if (!v || v->unit || v->num < 0.0) return std::nullopt;
			++operators;
			v->num = std::sqrt(v->num);
			return v;
		}

		auto base = parsePostfix(depth);
		if (!base) return std::nullopt;
		if (eat('^')) {
			auto exp = parsePower(depth);
			if (!exp || base->unit || exp->unit) return std::nullopt;
			++operators;
			base->num = std::pow(base->num, exp->num);
			base->isPercent = false;
		}
		return base;
	}

	[[nodiscard]] std::optional<Value> parsePostfix(int depth) {
		auto v = parsePrimary(depth);
		if (!v) return std::nullopt;
		while (true) {
			skipSpaces();
			if (_pos >= _s.size()) break;
			const auto ch = _s[_pos];
			if (ch == '%') {
				++_pos;
				++operators;
				v->num /= 100.0;
				v->isPercent = true;
				continue;
			}
			if (ch == '!') {
				++_pos;
				++operators;
				if (v->unit) return std::nullopt;
				v->num = Factorial(v->num);
				v->isPercent = false;
				continue;
			}
			// Multiplier word after number (e.g. "300к", "5k", "2 млн")
			const auto saved = _pos;
			const auto w = readWord();
			if (!w.isEmpty()) {
				if (auto mult = LookupMultiplier(w)) {
					v->num *= *mult;
					++operators;
					continue;
				}
				_pos = saved;
			}
			// Unit / currency suffix (if not already attached)
			if (!v->unit) {
				if (auto u = readUnitOrSymbol(std::nullopt)) {
					v->unit = *u;
					continue;
				}
			}
			break;
		}
		return v;
	}

	[[nodiscard]] std::optional<Value> parsePrimary(int depth) {
		if (depth > 32) return std::nullopt;
		skipSpaces();
		if (_pos >= _s.size()) return std::nullopt;

		// Prefix currency symbol: $100, €50, ₽500
		if (auto cur = Currencies().fromSymbol(_s[_pos])) {
			++_pos;
			auto inner = parsePrimary(depth + 1);
			if (!inner || inner->unit) return std::nullopt;
			// Optional glued multiplier right after prefix-currency number (e.g. $5k)
			const auto saved = _pos;
			const auto w = readWord();
			if (!w.isEmpty()) {
				if (auto mult = LookupMultiplier(w)) {
					inner->num *= *mult;
					++operators;
				} else {
					_pos = saved;
				}
			}
			inner->unit = *cur;
			return inner;
		}

		// Parentheses
		if (eat('(')) {
			auto v = parseAddSub(depth + 1);
			if (!v || !eat(')')) return std::nullopt;
			return v;
		}

		// Unicode π
		if (_s[_pos].unicode() == 0x03C0) {
			++_pos;
			return Value{ kPi, std::nullopt, false };
		}

		// Number literal (including 0x / 0b / 0o)
		if (_s[_pos].isDigit() || _s[_pos] == '.' || _s[_pos] == ',') {
			return parseNumber();
		}

		// Identifier: constant or function call
		const auto saved = _pos;
		const auto name = readWord();
		if (name.isEmpty()) return std::nullopt;
		const auto lower = name.toLower();

		if (peek() == '(') {
			return parseFunction(lower, depth);
		}
		if (lower == u"pi"_q) {
			return Value{ kPi, std::nullopt, false };
		}
		if (lower == u"e"_q) {
			return Value{ kE, std::nullopt, false };
		}
		_pos = saved;
		return std::nullopt;
	}

	[[nodiscard]] std::optional<Value> parseNumber() {
		skipSpaces();
		// Base literals: 0x..., 0b..., 0o...
		if (_pos + 2 < _s.size() && _s[_pos] == '0') {
			const auto p = _s[_pos + 1].toLower();
			int radix = 0;
			if (p == 'x') radix = 16;
			else if (p == 'b') radix = 2;
			else if (p == 'o') radix = 8;
			if (radix != 0) {
				_pos += 2;
				const auto start = _pos;
				while (_pos < _s.size()) {
					const auto c = _s[_pos].toLower();
					const auto okDigit = (radix == 2 && (c == '0' || c == '1'))
						|| (radix == 8 && (c >= '0' && c <= '7'))
						|| (radix == 16 && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
					if (!okDigit) break;
					++_pos;
				}
				if (_pos == start) return std::nullopt;
				bool ok = false;
				const auto val = _s.mid(start, _pos - start).toLongLong(&ok, radix);
				if (!ok) return std::nullopt;
				hasBaseLiteral = true;
				return Value{ double(val), std::nullopt, false };
			}
		}

		QString num;
		bool hasDot = false;
		while (_pos < _s.size()) {
			const auto c = _s[_pos];
			if (c.isDigit()) {
				num.append(c);
				++_pos;
			} else if ((c == '.' || c == ',') && !hasDot) {
				// Don't consume comma if it's a function argument separator followed by space
				if (c == ',' && _pos + 1 < _s.size() && !_s[_pos + 1].isDigit()) {
					break;
				}
				hasDot = true;
				num.append('.');
				++_pos;
			} else {
				break;
			}
		}
		if (num.isEmpty() || num == u"."_q) {
			return std::nullopt;
		}
		bool ok = false;
		const auto val = num.toDouble(&ok);
		if (!ok) return std::nullopt;
		return Value{ val, std::nullopt, false };
	}

	[[nodiscard]] std::optional<Value> parseFunction(const QString &fn, int depth) {
		if (!eat('(')) return std::nullopt;
		std::vector<double> args;
		if (peek() != ')') {
			while (true) {
				auto arg = parseAddSub(depth + 1);
				if (!arg || arg->unit) return std::nullopt;
				args.push_back(arg->num);
				if (eat(';') || eat(',')) {
					continue;
				}
				break;
			}
		}
		if (!eat(')') || args.empty()) return std::nullopt;
		++operators;

		const auto rad = [](double deg) { return deg * kPi / 180.0; };
		const auto deg = [](double r) { return r * 180.0 / kPi; };

		double res = std::numeric_limits<double>::quiet_NaN();
		if (args.size() == 1) {
			const auto x = args[0];
			if (fn == u"sqrt"_q) res = std::sqrt(x);
			else if (fn == u"cbrt"_q) res = std::cbrt(x);
			else if (fn == u"abs"_q) res = std::abs(x);
			else if (fn == u"sign"_q) res = (x > 0.0) ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
			else if (fn == u"round"_q) res = std::round(x);
			else if (fn == u"floor"_q) res = std::floor(x);
			else if (fn == u"ceil"_q) res = std::ceil(x);
			else if (fn == u"fact"_q) res = Factorial(x);
			else if (fn == u"log"_q) res = std::log10(x);
			else if (fn == u"log2"_q) res = std::log2(x);
			else if (fn == u"ln"_q) res = std::log(x);
			else if (fn == u"exp"_q) res = std::exp(x);
			else if (fn == u"sin"_q) res = std::sin(rad(x));
			else if (fn == u"cos"_q) res = std::cos(rad(x));
			else if (fn == u"tan"_q) res = std::tan(rad(x));
			else if (fn == u"asin"_q) res = deg(std::asin(x));
			else if (fn == u"acos"_q) res = deg(std::acos(x));
			else if (fn == u"atan"_q) res = deg(std::atan(x));
			else if (fn == u"sinh"_q) res = std::sinh(x);
			else if (fn == u"cosh"_q) res = std::cosh(x);
			else if (fn == u"tanh"_q) res = std::tanh(x);
			else if (fn == u"min"_q || fn == u"max"_q) res = x;
			else return std::nullopt;
		} else if (fn == u"atan2"_q && args.size() == 2) {
			res = deg(std::atan2(args[0], args[1]));
		} else if (fn == u"log"_q && args.size() == 2) {
			// log(value, base)
			res = std::log(args[0]) / std::log(args[1]);
		} else if (fn == u"min"_q) {
			res = *std::min_element(args.begin(), args.end());
		} else if (fn == u"max"_q) {
			res = *std::max_element(args.begin(), args.end());
		} else {
			return std::nullopt;
		}
		if (!std::isfinite(res)) return std::nullopt;
		return Value{ res, std::nullopt, false };
	}

	QString _s;
	qsizetype _pos = 0;
};

[[nodiscard]] QString EvaluateExpression(
		const QString &expr,
		const QString &afterEquals,
		bool russian) {
	const auto decimalSep = russian ? QChar(',') : QChar('.');

	// 1. Base conversion functions: hex(255), dec(ff, 16), bin(42), oct(64)
	if (afterEquals.isEmpty()) {
		if (const auto baseRes = TryBaseConversion(expr); !baseRes.isEmpty()) {
			return baseRes;
		}
	}

	// 2. General math / currency / unit parser
	auto parser = ExprParser(expr);
	auto parsed = parser.parse();
	if (!parsed || !std::isfinite(parsed->num)) {
		return QString();
	}

	// Optional legacy KomaruGram syntax: "1.5 btc = $" or "1 usdt = евро"
	std::optional<MathUnit> trailingTarget;
	if (!afterEquals.isEmpty()) {
		const auto trimmedAfter = afterEquals.trimmed();
		if (trimmedAfter.size() == 1) {
			trailingTarget = Currencies().fromSymbol(trimmedAfter[0]);
		}
		if (!trailingTarget) {
			trailingTarget = LookupAnyUnit(
				trimmedAfter,
				parsed->unit ? std::optional(parsed->unit->kind) : std::nullopt);
		}
		if (!trailingTarget) {
			return QString();
		}
	}

	const auto target = trailingTarget ? trailingTarget : parser.explicitTarget;

	// Case A: Currency value
	if (parsed->unit && parsed->unit->kind == UnitKind::Currency) {
		MathUnit dstUnit;
		if (target && target->kind == UnitKind::Currency) {
			dstUnit = *target;
		} else if (target) {
			return QString();
		} else {
			auto dstCode = LocalCurrency(russian);
			if (dstCode == parsed->unit->code) {
				dstCode = (dstCode == u"USD"_q) ? u"EUR"_q : u"USD"_q;
			}
			const auto lbl = Currencies().labelFor(dstCode);
			dstUnit = MathUnit{ dstCode, lbl, lbl, UnitKind::Currency, 1.0, 0.0 };
		}
		const auto converted = ConvertValue(parsed->num, *parsed->unit, dstUnit);
		if (!converted) {
			return QString();
		}
		const auto decs = CurrencyDecimals(dstUnit.code, std::abs(*converted));
		const auto formatted = FormatNumber(*converted, decimalSep, decs, decs > 2);
		if (formatted.isEmpty()) {
			return QString();
		}
		if (trailingTarget) {
			// User already typed the currency after "=" (e.g. "1.5 btc = $")
			return formatted;
		}
		return formatted + QChar(0x00A0) + dstUnit.label(russian);
	}

	// Case B: Physical unit / Duration
	if (parsed->unit && parsed->unit->kind != UnitKind::None) {
		if (target) {
			if (target->kind != parsed->unit->kind) {
				return QString();
			}
			const auto converted = ConvertValue(parsed->num, *parsed->unit, *target);
			if (!converted) return QString();
			const auto formatted = FormatNumber(*converted, decimalSep, 4, true);
			if (formatted.isEmpty()) return QString();
			return trailingTarget
				? formatted
				: (formatted + QChar(0x00A0) + target->label(russian));
		}
		if (parsed->unit->kind == UnitKind::Duration) {
			const auto totalSec = parsed->unit->toBase(parsed->num);
			return Measures().formatDuration(totalSec, decimalSep, russian);
		}
		const auto implicit = Measures().implicitTarget(
			*parsed->unit,
			parsed->num,
			IsImperialSystem());
		if (implicit) {
			const auto converted = ConvertValue(parsed->num, *parsed->unit, *implicit);
			if (!converted) return QString();
			const auto formatted = FormatNumber(*converted, decimalSep, 2, true);
			if (formatted.isEmpty()) return QString();
			return formatted + QChar(0x00A0) + implicit->label(russian);
		}
		if (parser.operators == 0) {
			return QString();
		}
		const auto formatted = FormatNumber(parsed->num, decimalSep, 4, true);
		return formatted.isEmpty()
			? QString()
			: (formatted + QChar(0x00A0) + parsed->unit->label(russian));
	}

	// Case C: Plain math expression (must have at least 1 operator or base literal)
	if (target || (parser.operators == 0 && !parser.hasBaseLiteral)) {
		return QString();
	}
	return FormatNumber(parsed->num, decimalSep, 6, true);
}

} // namespace

QString Completion(const QString &text) {
	const auto eqIdx = text.lastIndexOf('=');
	if (eqIdx <= 0) {
		return QString();
	}
	const auto afterEquals = text.mid(eqIdx + 1).trimmed();
	// Only allow empty text after "=" or a single target currency/unit token (legacy "1 btc = $")
	if (afterEquals.contains(' ') || afterEquals.contains('\n')) {
		return QString();
	}

	// Extract current line before '='
	const auto lineStart = text.lastIndexOf('\n', eqIdx - 1) + 1;
	const auto prevEq = text.lastIndexOf('=', eqIdx - 1);
	const auto segStart = std::max(lineStart, prevEq + 1);
	const auto segment = text.mid(segStart, eqIdx - segStart).trimmed();
	if (segment.isEmpty()) {
		return QString();
	}

	const auto russian = HasCyrillic(segment) || HasCyrillic(afterEquals);

	// 1. Time zones (only when nothing follows '=')
	if (afterEquals.isEmpty()) {
		if (const auto tzRes = EvaluateTimeZone(segment); !tzRes.isEmpty()) {
			return tzRes;
		}
	}

	// 2. Full segment as expression/currency/unit
	if (const auto full = EvaluateExpression(segment, afterEquals, russian); !full.isEmpty()) {
		return full;
	}

	// 3. Suffix scan so expressions at the end of a sentence also work
	//    (e.g. "привет, скинь 100$ в ₽ =" or "итого 2300 - 15% =")
	for (qsizetype i = 0; i < segment.size(); ++i) {
		if (segment[i].isSpace()) {
			while (i + 1 < segment.size() && segment[i + 1].isSpace()) {
				++i;
			}
			const auto suffix = segment.mid(i + 1);
			if (!suffix.isEmpty()) {
				if (const auto res = EvaluateExpression(suffix, afterEquals, russian); !res.isEmpty()) {
					return res;
				}
			}
		}
	}

	return QString();
}

rpl::producer<> Updated() {
	return Instance().updated.events();
}

} // namespace KomaruCryptoRates
