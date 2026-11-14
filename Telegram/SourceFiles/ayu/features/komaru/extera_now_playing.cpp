// This is the source code of KomaruGram for Desktop.
#include "ayu/features/komaru/extera_now_playing.h"

#include "base/timer.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
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

struct Entry {
	Track track;
	crl::time loaded = 0;
	crl::time unlinked = 0;
	bool loading = false;
	rpl::event_stream<> updated;
};

// The limit is per address, so a pause after 429 covers every user.
crl::time LimitedTill = 0;

[[nodiscard]] std::map<uint64, Entry> &Entries() {
	static auto result = std::map<uint64, Entry>();
	return result;
}

[[nodiscard]] QNetworkAccessManager &Manager() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

[[nodiscard]] Track Parse(const QByteArray &bytes) {
	const auto root = QJsonDocument::fromJson(bytes).object();
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
		.playing = root.value(u"isPlaying"_q).toBool(),
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
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kUrl).arg(userId)));
	request.setTransferTimeout(kTimeout);
	request.setRawHeader("Accept", "application/json");
	request.setHeader(QNetworkRequest::UserAgentHeader, u"KomaruGram"_q);
	const auto reply = Manager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto &entry = Entries()[userId];
		entry.loading = false;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 404) {
			// Not linked in exteraGram, asked again only much later.
			entry.unlinked = crl::now();
			if (!entry.track.empty()) {
				entry.track = Track();
				entry.updated.fire({});
			}
			return;
		} else if (status == 429) {
			LimitedTill = crl::now() + kLimitedFor;
			return;
		} else if (reply->error() != QNetworkReply::NoError) {
			// Keeps what was shown, the next refresh tries again.
			return;
		}
		const auto track = Parse(reply->readAll());
		entry.loaded = crl::now();
		entry.unlinked = 0;
		if (entry.track != track) {
			entry.track = track;
			entry.updated.fire({});
		}
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

} // namespace KomaruExteraNowPlaying
