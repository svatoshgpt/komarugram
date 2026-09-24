// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/komaru_badges.h"

#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/style/style_core.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtSvg/QSvgRenderer>

#include <rpl/event_stream.h>

#include <cmath>

namespace KomaruBadges {
namespace {

constexpr auto kGistRaw = "https://gist.githubusercontent.com/"
	"svatoshgpt/300f51e5b67854aa23ab5b592bca0aa8/raw/";
constexpr auto kRefreshInterval = 10 * 60 * 1000;
constexpr auto kFetchTimeout = 15 * 1000;

// Komaru orange, used when a line leaves out its colours.
const auto kDefaultFace = QColor(0xFF, 0xAD, 0x32);

// The face takes this share of the badge; the particles circle around it.
constexpr auto kFaceShare = 0.74;
// The developer art carries its own margin around the rosette.
constexpr auto kDeveloperShare = 0.92;
constexpr auto kRosettePeriod = 12000.;
constexpr auto kParticles = 4;
constexpr auto kOrbitPeriod = 6000.;
constexpr auto kTwinklePeriod = 1400.;

struct State {
	std::unordered_map<uint64, Entry> developers;
	std::unordered_map<uint64, Entry> supporters;
	std::unordered_map<uint64, Entry> partners;
	double rate = 0.;
	rpl::event_stream<> updated;
	bool started = false;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] QNetworkAccessManager &Manager() {
	// Owned by the application, so it goes away with it instead of being
	// torn down after QApplication by static destruction.
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

[[nodiscard]] QNetworkReply *Get(const QString &url) {
	auto request = QNetworkRequest(QUrl(url));
	request.setTransferTimeout(kFetchTimeout);
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		u"KomaruGram"_q);
	return Manager().get(request);
}

[[nodiscard]] std::unordered_map<uint64, Entry> ParseList(
		Kind kind,
		const QByteArray &data) {
	auto result = std::unordered_map<uint64, Entry>();
	for (const auto &raw : QString::fromUtf8(data).split('\n')) {
		const auto line = raw.trimmed();
		if (line.isEmpty() || line.startsWith('#')) {
			continue;
		}
		const auto parts = line.split(',');
		const auto id = parts[0].trimmed().toULongLong();
		if (!id) {
			continue;
		}
		// "#RRGGBB"; anything else falls back.
		const auto color = [&](int index, QColor fallback) {
			const auto text = (parts.size() > index)
				? parts[index].trimmed()
				: QString();
			auto ok = false;
			const auto rgb = text.mid(1).toUInt(&ok, 16);
			return (ok && text.size() == 7 && text.startsWith('#'))
				? QColor(QRgb(0xFF000000U | rgb))
				: fallback;
		};
		// A line with a single colour uses it for both.
		auto particles = color(1, kDefaultFace);
		auto face = color(2, particles);
		auto ok = false;
		const auto alpha = (parts.size() > 3)
			? parts[3].trimmed().toInt(&ok)
			: 255;
		const auto clamped = std::clamp(ok ? alpha : 255, 0, 255);
		face.setAlpha(clamped);
		particles.setAlpha(clamped);
		result.emplace(id, Entry{
			.kind = kind,
			.face = face,
			.particles = particles,
		});
	}
	return result;
}

void FetchList(Kind kind) {
	const auto name = (kind == Kind::Developer)
		? u"badges.txt"_q
		: (kind == Kind::Partner)
		? u"partner.txt"_q
		: u"donates.txt"_q;
	const auto reply = Get(QString::fromLatin1(kGistRaw) + name);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			// Keep the last list we had rather than dropping every badge.
			LOG(("Komaru Badges Error: %1 not loaded, %2"
				).arg(name, reply->errorString()));
			return;
		}
		auto &state = Instance();
		auto parsed = ParseList(kind, reply->readAll());
		auto &list = (kind == Kind::Developer)
			? state.developers
			: (kind == Kind::Partner)
			? state.partners
			: state.supporters;
		list = std::move(parsed);
		state.updated.fire({});
	});
}

// Public tickers that need no key, tried in order. Binance and Bybit refuse
// requests from some countries, so they are not used.
struct RateSource {
	const char *url = nullptr;
	double (*parse)(const QJsonObject &root) = nullptr;
};

[[nodiscard]] double ParseOkx(const QJsonObject &root) {
	const auto data = root.value(u"data"_q).toArray();
	return data.isEmpty()
		? 0.
		: data.first().toObject().value(u"last"_q).toString().toDouble();
}

[[nodiscard]] double ParseCoinGecko(const QJsonObject &root) {
	return root.value(u"the-open-network"_q).toObject()
		.value(u"usd"_q).toDouble();
}

[[nodiscard]] double ParseCoinMarketCap(const QJsonObject &root) {
	return root.value(u"data"_q).toObject()
		.value(u"statistics"_q).toObject()
		.value(u"price"_q).toDouble();
}

const RateSource kRateSources[] = {
	{
		"https://www.okx.com/api/v5/market/ticker?instId=GRAM-USDT",
		ParseOkx,
	},
	{
		"https://api.coingecko.com/api/v3/simple/price"
		"?ids=the-open-network&vs_currencies=usd",
		ParseCoinGecko,
	},
	{
		"https://api.coinmarketcap.com/data-api/v3/cryptocurrency/detail"
		"?slug=gram",
		ParseCoinMarketCap,
	},
};

void FetchRate(int index = 0) {
	if (index >= int(std::size(kRateSources))) {
		LOG(("Komaru Badges Error: no Gram rate from any source."));
		return;
	}
	const auto &source = kRateSources[index];
	const auto reply = Get(QString::fromLatin1(source.url));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto root = (reply->error() == QNetworkReply::NoError)
			? QJsonDocument::fromJson(reply->readAll()).object()
			: QJsonObject();
		const auto rate = kRateSources[index].parse(root);
		if (rate > 0.) {
			auto &state = Instance();
			if (state.rate != rate) {
				state.rate = rate;
				state.updated.fire({});
			}
		} else {
			FetchRate(index + 1);
		}
	});
}

void Refresh() {
	FetchList(Kind::Developer);
	FetchList(Kind::Supporter);
	FetchList(Kind::Partner);
	FetchRate();
}

enum class Art : uchar {
	SupporterFace,
	DeveloperFace,
	DeveloperRosette,
};

[[nodiscard]] QString ArtPath(Art art) {
	switch (art) {
	case Art::SupporterFace: return u":/gui/art/ayu/komaru_badge.svg"_q;
	case Art::DeveloperFace: return u":/gui/art/ayu/komaru_dev_face.svg"_q;
	case Art::DeveloperRosette:
		return u":/gui/art/ayu/komaru_dev_rosette.svg"_q;
	}
	Unexpected("Art in KomaruBadges::ArtPath.");
}

// The art is white; its alpha is kept and the colour comes from the list.
[[nodiscard]] const QImage &Mask(Art art, int size) {
	static auto cache = std::map<std::pair<Art, int>, QImage>();
	const auto key = std::make_pair(art, size);
	if (const auto i = cache.find(key); i != end(cache)) {
		return i->second;
	}
	auto file = QFile(ArtPath(art));
	const auto content = file.open(QIODevice::ReadOnly)
		? file.readAll()
		: QByteArray();
	auto mask = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
	mask.fill(Qt::transparent);
	{
		auto p = QPainter(&mask);
		QSvgRenderer(content).render(&p, QRectF(0, 0, size, size));
	}
	return cache.emplace(key, std::move(mask)).first->second;
}

[[nodiscard]] const QImage &Tinted(Art art, int size, QColor color) {
	using Key = std::tuple<Art, int, QRgb>;
	static auto cache = std::map<Key, QImage>();
	const auto key = Key(art, size, color.rgba());
	if (const auto i = cache.find(key); i != end(cache)) {
		return i->second;
	}
	if (cache.size() > 64) {
		cache.clear();
	}
	auto image = Mask(art, size);
	{
		auto p = QPainter(&image);
		p.setCompositionMode(QPainter::CompositionMode_SourceIn);
		p.fillRect(image.rect(), color);
	}
	return cache.emplace(key, std::move(image)).first->second;
}

// A four-pointed sparkle centred on `center`, `radius` from tip to centre.
[[nodiscard]] QPainterPath Sparkle(QPointF center, double radius) {
	const auto inner = radius * 0.3;
	auto path = QPainterPath();
	path.moveTo(center + QPointF(0, -radius));
	path.lineTo(center + QPointF(inner, -inner));
	path.lineTo(center + QPointF(radius, 0));
	path.lineTo(center + QPointF(inner, inner));
	path.lineTo(center + QPointF(0, radius));
	path.lineTo(center + QPointF(-inner, inner));
	path.lineTo(center + QPointF(-radius, 0));
	path.lineTo(center + QPointF(-inner, -inner));
	path.closeSubpath();
	return path;
}

} // namespace

void Start() {
	auto &state = Instance();
	if (state.started) {
		return;
	}
	state.started = true;
	Refresh();
	const auto timer = new QTimer(QCoreApplication::instance());
	QObject::connect(timer, &QTimer::timeout, [] { Refresh(); });
	timer->start(kRefreshInterval);
}

const Entry *Developer(uint64 peerId) {
	const auto &list = Instance().developers;
	const auto i = list.find(peerId);
	return (i != end(list)) ? &i->second : nullptr;
}

const Entry *Supporter(uint64 peerId) {
	const auto &state = Instance();
	if (const auto i = state.supporters.find(peerId);
		i != end(state.supporters)) {
		return &i->second;
	} else if (const auto j = state.partners.find(peerId);
		j != end(state.partners)) {
		return &j->second;
	}
	return nullptr;
}

bool IsPartner(uint64 peerId) {
	return Instance().partners.contains(peerId);
}

rpl::producer<> Updated() {
	return Instance().updated.events();
}

QString DonateGramAmount() {
	const auto rate = Instance().rate;
	if (rate <= 0.) {
		return QString();
	}
	const auto amount = std::ceil(kDonateUsd / rate * 100.) / 100.;
	return QString::number(amount, 'f', 2);
}

bool Animated() {
	return !PowerSaving::On(PowerSaving::kEmojiStatus);
}

void Paint(QPainter &p, QRect rect, const Entry &entry, crl::time now) {
	const auto side = std::min(rect.width(), rect.height());
	if (side <= 0) {
		return;
	}
	const auto center = QRectF(rect).center();
	const auto ratio = style::DevicePixelRatio();
	const auto time = Animated() ? double(now) : 0.;
	const auto developer = (entry.kind == Kind::Developer);
	const auto square = [&](int size) {
		return QRectF(
			center.x() - size / 2.,
			center.y() - size / 2.,
			size,
			size);
	};

	auto hq = PainterHighQualityEnabler(p);
	if (developer) {
		// The developer face sits on a slowly turning rosette.
		const auto size = int(std::round(side * kDeveloperShare));
		const auto &rosette = Tinted(
			Art::DeveloperRosette,
			size * ratio,
			entry.face);
		p.save();
		p.translate(center);
		p.rotate(360. * std::fmod(time / kRosettePeriod, 1.));
		p.translate(-center);
		p.drawImage(square(size), rosette);
		p.restore();
		p.drawImage(
			square(size),
			Tinted(Art::DeveloperFace, size * ratio, entry.face));
	} else {
		const auto size = int(std::round(side * kFaceShare));
		p.drawImage(
			square(size),
			Tinted(Art::SupporterFace, size * ratio, entry.face));
	}

	p.setPen(Qt::NoPen);
	const auto orbit = side * 0.47;
	const auto turn = time / kOrbitPeriod;
	const auto opacity = p.opacity();
	for (auto i = 0; i != kParticles; ++i) {
		const auto angle = 2. * M_PI * (turn + (i + 0.5) / kParticles);
		const auto twinkle = 0.5 + 0.5 * std::sin(
			2. * M_PI * (time / kTwinklePeriod + i * 0.27));
		const auto radius = side * 0.13 * (0.55 + 0.45 * twinkle);
		const auto position = center + QPointF(
			std::cos(angle) * orbit,
			std::sin(angle) * orbit);
		p.setOpacity(opacity * (0.35 + 0.65 * twinkle));
		p.setBrush(entry.particles);
		p.drawPath(Sparkle(position, radius));
	}
	p.setOpacity(opacity);
}

} // namespace KomaruBadges
