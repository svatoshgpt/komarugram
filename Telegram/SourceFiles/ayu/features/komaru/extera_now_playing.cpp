// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/extera_now_playing.h"

#include "base/timer.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtGui/QImage>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace KomaruExteraNowPlaying {
namespace {

constexpr auto kUrl = "https://api.exteragram.app/api/v1/profiles/%1/now-playing";
constexpr auto kRefresh = crl::time(30 * 1000);
constexpr auto kUnlinkedFor = crl::time(10 * 60 * 1000);
constexpr auto kLimitedFor = crl::time(60 * 1000);
constexpr auto kTimeout = 15000;
constexpr auto kCoversKept = 50;

struct Entry {
	Track track;
	crl::time loaded = 0;
	crl::time unlinked = 0;
	bool loading = false;
	rpl::event_stream<> updated;
};

struct Cover {
	QImage image;
	bool loading = false;
	rpl::event_stream<> updated;
};

// The limit is per address, so a pause after 429 covers every user.
crl::time LimitedTill = 0;

[[nodiscard]] std::map<uint64, Entry> &Entries() {
	static auto result = std::map<uint64, Entry>();
	return result;
}

[[nodiscard]] std::map<QString, Cover> &Covers() {
	static auto result = std::map<QString, Cover>();
	return result;
}

[[nodiscard]] QNetworkAccessManager &Manager() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

[[nodiscard]] QNetworkReply *Get(const QUrl &url) {
	auto request = QNetworkRequest(url);
	request.setTransferTimeout(kTimeout);
	request.setRawHeader("Accept", "application/json");
	request.setHeader(QNetworkRequest::UserAgentHeader, u"KomaruGram"_q);
	return Manager().get(request);
}

[[nodiscard]] Track Parse(const QByteArray &bytes) {
	const auto root = QJsonDocument::fromJson(bytes).object();
	if (!root.value(u"isPlaying"_q).toBool()) {
		return Track();
	}
	auto artists = QStringList();
	for (const auto &artist : root.value(u"artists"_q).toArray()) {
		if (const auto name = artist.toString().trimmed(); !name.isEmpty()) {
			artists.push_back(name);
		}
	}
	return {
		.title = root.value(u"trackName"_q).toString().trimmed(),
		.artist = artists.join(u", "_q),
		.url = root.value(u"songUrl"_q).toString(),
		.cover = root.value(u"coverUrl"_q).toString(),
	};
}

void Load(uint64 userId) {
	auto &entry = Entries()[userId];
	const auto now = crl::now();
	if (entry.loading
		|| (now < LimitedTill)
		|| (entry.unlinked && now - entry.unlinked < kUnlinkedFor)
		|| (entry.loaded && now - entry.loaded < kRefresh)) {
		return;
	}
	entry.loading = true;
	const auto reply = Get(QUrl(QString::fromLatin1(kUrl).arg(userId)));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &entry = Entries()[userId];
		entry.loading = false;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		auto track = Track();
		if (status == 404) {
			// Not linked in exteraGram, asked again only much later.
			entry.unlinked = crl::now();
		} else if (status == 429) {
			LimitedTill = crl::now() + kLimitedFor;
			return;
		} else if (reply->error() != QNetworkReply::NoError) {
			// Keeps what was shown, the next refresh tries again.
			return;
		} else {
			track = Parse(reply->readAll());
			entry.loaded = crl::now();
			entry.unlinked = 0;
		}
		if (entry.track != track) {
			entry.track = track;
			entry.updated.fire({});
		}
	});
}

[[nodiscard]] QString Simple(const QString &text) {
	return text.toLower().simplified();
}

// The first result with the same title and a shared artist, else the first.
[[nodiscard]] QString ArtworkUrl(
		const QByteArray &bytes,
		const QString &title,
		const QString &artist) {
	const auto results = QJsonDocument::fromJson(bytes).object()
		.value(u"results"_q).toArray();
	auto found = QString();
	for (const auto &value : results) {
		const auto item = value.toObject();
		const auto url = item.value(u"artworkUrl100"_q).toString();
		if (url.isEmpty()) {
			continue;
		} else if (found.isEmpty()) {
			found = url;
		}
		const auto name = Simple(item.value(u"artistName"_q).toString());
		const auto wanted = Simple(artist);
		if (Simple(item.value(u"trackName"_q).toString()) == Simple(title)
			&& !name.isEmpty()
			&& (name.contains(wanted) || wanted.contains(name))) {
			found = url;
			break;
		}
	}
	return found.replace(u"100x100bb"_q, u"300x300bb"_q);
}

void LoadImage(const QString &key, const QString &url) {
	const auto reply = Get(QUrl(url));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &cover = Covers()[key];
		cover.loading = false;
		if (reply->error() == QNetworkReply::NoError) {
			cover.image = QImage::fromData(reply->readAll());
			cover.updated.fire({});
		}
	});
}

void LoadCover(
		const QString &key,
		const QString &title,
		const QString &artist,
		const QString &known) {
	auto &covers = Covers();
	auto &cover = covers[key];
	if (cover.loading || !cover.image.isNull()) {
		return;
	}
	if (covers.size() > kCoversKept) {
		for (auto i = covers.begin(); i != covers.end();) {
			const auto busy = (i->first == key)
				|| i->second.loading
				|| i->second.updated.has_consumers();
			i = busy ? std::next(i) : covers.erase(i);
		}
	}
	covers[key].loading = true;
	if (!known.isEmpty()) {
		LoadImage(key, known);
		return;
	}
	auto query = QUrlQuery();
	query.addQueryItem(u"term"_q, title + u" - "_q + artist);
	query.addQueryItem(u"entity"_q, u"song"_q);
	query.addQueryItem(u"limit"_q, u"5"_q);
	auto url = QUrl(u"https://itunes.apple.com/search"_q);
	url.setQuery(query);
	const auto reply = Get(url);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto artwork = (reply->error() == QNetworkReply::NoError)
			? ArtworkUrl(reply->readAll(), title, artist)
			: QString();
		if (artwork.isEmpty()) {
			Covers()[key].loading = false;
			return;
		}
		LoadImage(key, artwork);
	});
}

} // namespace

rpl::producer<Track> Value(uint64 userId) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto push = [=] {
			consumer.put_next_copy(Entries()[userId].track);
		};
		Entries()[userId].updated.events(
		) | rpl::on_next(push, lifetime);
		push();
		Load(userId);
		const auto timer = lifetime.make_state<base::Timer>([=] {
			Load(userId);
		});
		timer->callEach(kRefresh);
		return lifetime;
	};
}

rpl::producer<QImage> CoverValue(
		const QString &title,
		const QString &artist,
		const QString &known) {
	if (title.isEmpty()) {
		return rpl::single(QImage());
	}
	const auto key = Simple(artist) + QChar('\n') + Simple(title);
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto push = [=] {
			consumer.put_next_copy(Covers()[key].image);
		};
		Covers()[key].updated.events(
		) | rpl::on_next(push, lifetime);
		push();
		LoadCover(key, title, artist, known);
		return lifetime;
	};
}

} // namespace KomaruExteraNowPlaying
