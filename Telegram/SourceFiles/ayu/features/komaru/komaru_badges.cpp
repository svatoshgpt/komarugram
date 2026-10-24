// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/komaru_badges.h"

#include "apiwrap.h"
#include "core/application.h"
#include "data/data_types.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/style/style_core.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QApplication>

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

// An emoji status from statuses.txt: either a custom emoji id, or a
// 1-based position in an emoji pack that is looked up once per pack.
struct StatusRef {
	QString set;
	int index = 0;
	DocumentId id = 0;
	QString text;
};

struct State {
	std::unordered_map<uint64, Entry> developers;
	std::unordered_map<uint64, Entry> supporters;
	std::map<Kind, QByteArray> raw;
	std::unordered_map<uint64, StatusRef> statuses;
	QByteArray statusesRaw;
	base::flat_map<QString, std::vector<DocumentId>> sets;
	base::flat_set<QString> requestedSets;
	Main::Session *session = nullptr;
	rpl::lifetime sessionLifetime;
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
		struct ParsedColor {
			QColor value;
			bool theme = false;
		};
		const auto color = [&](int index, ParsedColor fallback) {
			const auto text = (parts.size() > index)
				? parts[index].trimmed()
				: QString();
			if (text.compare(u"#md3"_q, Qt::CaseInsensitive) == 0) {
				return ParsedColor{ fallback.value, true };
			}
			auto ok = false;
			const auto rgb = text.mid(1).toUInt(&ok, 16);
			return (ok && text.size() == 7 && text.startsWith('#'))
				? ParsedColor{ QColor(QRgb(0xFF000000U | rgb)), false }
				: fallback;
		};
		auto particles = color(1, { kDefaultFace, false });
		auto face = color(2, particles);
		auto ok = false;
		const auto alpha = (parts.size() > 3)
			? parts[3].trimmed().toInt(&ok)
			: 255;
		const auto clamped = std::clamp(ok ? alpha : 255, 0, 255);
		face.value.setAlpha(clamped);
		particles.value.setAlpha(clamped);
		result.emplace(id, Entry{
			.kind = kind,
			.face = face.value,
			.particles = particles.value,
			.faceTheme = face.theme,
			.particlesTheme = particles.theme,
		});
	}
	return result;
}

[[nodiscard]] StatusRef ParseStatusRef(const QString &text) {
	auto ok = false;
	if (const auto id = text.toULongLong(&ok); ok && id) {
		return { .id = id };
	}
	static const auto expression = QRegularExpression(
		u"^(?:(?:https?://)?(?:t|telegram)\\.me/addemoji/)?"
		"([A-Za-z0-9_]+)/(\\d+)/?$"_q);
	const auto match = expression.match(text);
	const auto index = match.hasMatch() ? match.captured(2).toInt() : 0;
	return (index > 0)
		? StatusRef{ .set = match.captured(1), .index = index }
		: StatusRef();
}

[[nodiscard]] std::unordered_map<uint64, StatusRef> ParseStatuses(
		const QByteArray &data) {
	auto result = std::unordered_map<uint64, StatusRef>();
	for (const auto &raw : QString::fromUtf8(data).split('\n')) {
		const auto line = raw.trimmed();
		if (line.isEmpty() || line.startsWith('#')) {
			continue;
		}
		const auto parts = line.split(',');
		const auto id = parts[0].trimmed().toULongLong();
		if (!id || parts.size() < 2) {
			continue;
		}
		auto status = ParseStatusRef(parts[1].trimmed());
		if (!status.id && !status.index) {
			continue;
		}
		if (parts.size() > 2) {
			status.text = parts.mid(2).join(',').trimmed();
		}
		result.emplace(id, std::move(status));
	}
	return result;
}

[[nodiscard]] QString ListName(Kind kind) {
	switch (kind) {
	case Kind::Developer: return u"badges.txt"_q;
	case Kind::Supporter: return u"donates.txt"_q;
	}
	Unexpected("Kind in KomaruBadges::ListName.");
}

// The last lists received, so faces show from the first frame instead of
// popping in once the gist answers.
[[nodiscard]] QString CachePath(Kind kind) {
	return cWorkingDir() + u"tdata/komaru_badges/"_q + ListName(kind);
}

// Chat lists paint badges only when something else repaints them, so new
// faces would otherwise not show until the next scroll or hover.
void RepaintAll() {
	for (const auto widget : QApplication::allWidgets()) {
		widget->update();
	}
}

// Returns whether the list changed.
bool ApplyList(Kind kind, const QByteArray &data) {
	auto &state = Instance();
	auto &raw = state.raw[kind];
	if (raw == data) {
		return false;
	}
	raw = data;
	auto &list = (kind == Kind::Developer)
		? state.developers
		: state.supporters;
	list = ParseList(kind, data);
	return true;
}

[[nodiscard]] QString StatusesPath() {
	return cWorkingDir() + u"tdata/komaru_badges/statuses.txt"_q;
}

void RequestSet(not_null<Main::Session*> session, const QString &name) {
	auto &state = Instance();
	if (state.sets.contains(name) || state.requestedSets.contains(name)) {
		return;
	}
	state.requestedSets.emplace(name);
	session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetShortName(MTP_string(name)),
		MTP_int(0)
	)).done([=](const MTPmessages_StickerSet &result) {
		auto ids = std::vector<DocumentId>();
		result.match([&](const MTPDmessages_stickerSet &data) {
			for (const auto &document : data.vdocuments().v) {
				ids.push_back((document.type() == mtpc_document)
					? document.c_document().vid().v
					: DocumentId());
			}
		}, [](const MTPDmessages_stickerSetNotModified &) {
		});
		auto &state = Instance();
		state.sets[name] = std::move(ids);
		state.updated.fire({});
		RepaintAll();
	}).fail([=](const MTP::Error &error) {
		LOG(("Komaru Badges Error: emoji pack %1 not loaded, %2"
			).arg(name, error.type()));
	}).send();
}

void ResolveStatusSets() {
	auto &state = Instance();
	if (!state.session) {
		return;
	}
	for (const auto &[peerId, status] : state.statuses) {
		if (!status.set.isEmpty()) {
			RequestSet(state.session, status.set);
		}
	}
}

// Returns whether the list changed.
bool ApplyStatuses(const QByteArray &data) {
	auto &state = Instance();
	if (state.statusesRaw == data) {
		return false;
	}
	state.statusesRaw = data;
	state.statuses = ParseStatuses(data);
	state.requestedSets.clear();
	ResolveStatusSets();
	return true;
}

void FetchStatuses() {
	const auto reply = Get(QString::fromLatin1(kGistRaw) + u"statuses.txt"_q);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Komaru Badges Error: statuses.txt not loaded, %1"
				).arg(reply->errorString()));
			return;
		}
		const auto data = reply->readAll();
		if (!ApplyStatuses(data)) {
			return;
		}
		QDir().mkpath(cWorkingDir() + u"tdata/komaru_badges"_q);
		auto file = QFile(StatusesPath());
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(data);
		}
		Instance().updated.fire({});
		RepaintAll();
	});
}

void LoadCachedLists() {
	auto any = false;
	for (const auto kind : { Kind::Developer, Kind::Supporter }) {
		auto file = QFile(CachePath(kind));
		if (file.open(QIODevice::ReadOnly)) {
			any = ApplyList(kind, file.readAll()) || any;
		}
	}
	if (auto file = QFile(StatusesPath()); file.open(QIODevice::ReadOnly)) {
		any = ApplyStatuses(file.readAll()) || any;
	}
	if (any) {
		Instance().updated.fire({});
	}
}

void FetchList(Kind kind) {
	const auto name = ListName(kind);
	const auto reply = Get(QString::fromLatin1(kGistRaw) + name);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			// Keep the last list we had rather than dropping every badge.
			LOG(("Komaru Badges Error: %1 not loaded, %2"
				).arg(name, reply->errorString()));
			return;
		}
		const auto data = reply->readAll();
		if (!ApplyList(kind, data)) {
			return;
		}
		QDir().mkpath(cWorkingDir() + u"tdata/komaru_badges"_q);
		auto file = QFile(CachePath(kind));
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(data);
		}
		Instance().updated.fire({});
		RepaintAll();
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
	FetchStatuses();
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
	LoadCachedLists();
	Core::App().domain().activeSessionValue(
	) | rpl::on_next([](Main::Session *session) {
		Instance().session = session;
		ResolveStatusSets();
	}, state.sessionLifetime);
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
	const auto &list = Instance().supporters;
	const auto i = list.find(peerId);
	return (i != end(list)) ? &i->second : nullptr;
}

uint64 Status(uint64 peerId) {
	const auto &state = Instance();
	const auto i = state.statuses.find(peerId);
	if (i == end(state.statuses)) {
		return 0;
	} else if (i->second.id) {
		return i->second.id;
	}
	const auto j = state.sets.find(i->second.set);
	if (j == end(state.sets) || i->second.index > int(j->second.size())) {
		return 0;
	}
	return j->second[i->second.index - 1];
}

QString StatusText(uint64 peerId) {
	const auto &state = Instance();
	const auto i = state.statuses.find(peerId);
	return (i != end(state.statuses)) ? i->second.text : QString();
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

void Paint(
		QPainter &p,
		QRect rect,
		const Entry &entry,
		crl::time now,
		QColor themeColor) {
	const auto side = std::min(rect.width(), rect.height());
	if (side <= 0) {
		return;
	}
	const auto center = QRectF(rect).center();
	const auto ratio = style::DevicePixelRatio();
	const auto time = Animated() ? double(now) : 0.;
	const auto developer = (entry.kind == Kind::Developer);
	const auto resolve = [&](QColor color, bool theme) {
		if (!theme) {
			return color;
		}
		auto result = themeColor;
		result.setAlphaF(result.alphaF() * color.alphaF());
		return result;
	};
	const auto face = resolve(entry.face, entry.faceTheme);
	const auto particles = resolve(entry.particles, entry.particlesTheme);
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
			face);
		p.save();
		p.translate(center);
		p.rotate(360. * std::fmod(time / kRosettePeriod, 1.));
		p.translate(-center);
		p.drawImage(square(size), rosette);
		p.restore();
		p.drawImage(
			square(size),
			Tinted(Art::DeveloperFace, size * ratio, face));
	} else {
		const auto size = int(std::round(side * kFaceShare));
		p.drawImage(
			square(size),
			Tinted(Art::SupporterFace, size * ratio, face));
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
		p.setBrush(particles);
		p.drawPath(Sparkle(position, radius));
	}
	p.setOpacity(opacity);
}

} // namespace KomaruBadges
