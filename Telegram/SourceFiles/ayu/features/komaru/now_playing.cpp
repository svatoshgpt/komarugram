// This file is part of KomaruGram for Desktop, see https://github.com/svatoshgpt/komarugram
#include "ayu/features/komaru/now_playing.h"

#include "apiwrap.h"
#include "ayu/ayu_settings.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_premium_limits.h"
#include "data/data_saved_music.h"
#include "data/data_search_controller.h"
#include "inline_bots/inline_bot_result.h"
#include "main/main_app_config.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QBuffer>
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrlQuery>
#include <QtGui/QImage>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace KomaruNowPlaying {
namespace {

constexpr auto kPollInterval = 5 * crl::time(1000);
constexpr auto kMaxBackoff = 10 * 60 * crl::time(1000);
constexpr auto kRestartDelay = 2 * crl::time(1000);
// A pause this long clears the card, the profile music and the bio.
constexpr auto kStopDelay = 10 * crl::time(1000);
constexpr auto kFetchTimeout = 15000;
constexpr auto kSearchLimit = 20;
constexpr auto kMaxSilence = 480;
constexpr auto kThumbSize = 320;
constexpr auto kFrameBytes = 36;
constexpr auto kFrameSamples = 576;
constexpr auto kSampleRate = 16000;

struct Applied {
	DocumentId music = 0;
	QString musicKey;
	QString bioBackup;
	QString bioText;
	bool bioSaved = false;
	// Tracks this client put into profile music, so a repeat is moved, not kept.
	std::vector<DocumentId> added;
};

constexpr auto kAddedLimit = 100;

void RememberAdded(Applied &applied, DocumentId id) {
	if (ranges::contains(applied.added, id)) {
		return;
	}
	applied.added.push_back(id);
	if (applied.added.size() > kAddedLimit) {
		applied.added.erase(begin(applied.added));
	}
}

[[nodiscard]] QNetworkAccessManager &Network() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

[[nodiscard]] QNetworkReply *Get(const QUrl &url) {
	auto request = QNetworkRequest(url);
	request.setTransferTimeout(kFetchTimeout);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"KomaruGram"_q);
	return Network().get(request);
}

[[nodiscard]] QString StatePath() {
	return cWorkingDir() + u"tdata/komaru_now_playing.json"_q;
}

// The bio as it was before Now Playing, readable and kept across restarts.
[[nodiscard]] QString BioBackupPath() {
	return cWorkingDir() + u"tdata/komaru_bio_backup.md"_q;
}

[[nodiscard]] bool IsNowPlayingBio(const QString &text) {
	return text.startsWith(u"🎧 "_q);
}

[[nodiscard]] std::map<uint64, QString> ReadBioBackups() {
	auto result = std::map<uint64, QString>();
	auto file = QFile(BioBackupPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto text = QString::fromUtf8(file.readAll());
	static const auto re = QRegularExpression(
		u"<!-- user (\\d+) -->\n(.*?)\n<!-- end -->"_q,
		QRegularExpression::DotMatchesEverythingOption);
	auto i = re.globalMatch(text);
	while (i.hasNext()) {
		const auto match = i.next();
		result[match.captured(1).toULongLong()] = match.captured(2);
	}
	return result;
}

void WriteBioBackup(uint64 userId, const QString &bio) {
	if (IsNowPlayingBio(bio)) {
		return;
	}
	auto all = ReadBioBackups();
	const auto i = all.find(userId);
	if (i != end(all) && i->second == bio) {
		return;
	}
	all[userId] = bio;
	auto text = u"# KomaruGram bio backup\n\n"_q
		+ u"The profile bio saved before Now Playing replaced it.\n\n"_q;
	for (const auto &[id, about] : all) {
		text += u"<!-- user "_q + QString::number(id) + u" -->\n"_q
			+ about + u"\n<!-- end -->\n\n"_q;
	}
	auto file = QFile(BioBackupPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(text.toUtf8());
	}
}

[[nodiscard]] QString BioToRestore(uint64 userId, const QString &backup) {
	if (!IsNowPlayingBio(backup)) {
		return backup;
	}
	const auto all = ReadBioBackups();
	const auto i = all.find(userId);
	return (i != end(all)) ? i->second : QString();
}

[[nodiscard]] bool Enabled() {
	const auto &settings = AyuSettings::getInstance();
	switch (settings.nowPlayingService()) {
	case NowPlayingService::None: return false;
	case NowPlayingService::LastFm:
		if (settings.nowPlayingLastFmKey().trimmed().isEmpty()) {
			return false;
		}
		break;
	case NowPlayingService::StatsFm: break;
	}
	return !settings.nowPlayingUsername().trimmed().isEmpty();
}

[[nodiscard]] QString Normalize(const QString &text) {
	auto result = QString();
	result.reserve(text.size());
	for (const auto &ch : text.toLower()) {
		if (ch.isLetterOrNumber()) {
			result.append(ch);
		}
	}
	return result;
}

// The title without "(feat. X)", "[Remastered]" and what follows a dash.
[[nodiscard]] QString TitleCore(const QString &text) {
	static const auto brackets = QRegularExpression(
		u"[\\(\\[][^\\)\\]]*[\\)\\]]"_q);
	auto result = QString(text).remove(brackets);
	if (const auto dash = result.indexOf(u" - "_q); dash > 0) {
		result = result.left(dash);
	}
	return Normalize(result);
}

// The first of several artists, like "A" of "A, B & C feat. D".
[[nodiscard]] QString FirstArtist(const QString &text) {
	static const auto separator = QRegularExpression(
		u"\\s*(,|&|\\bfeat\\b|\\bft\\b|\\bx\\b)"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto at = text.indexOf(separator);
	return Normalize((at > 0) ? text.left(at) : text);
}

[[nodiscard]] bool Matches(
		not_null<DocumentData*> document,
		const Track &track,
		bool loose = false) {
	const auto song = document->song();
	if (!song) {
		return false;
	}
	if (loose) {
		const auto title = TitleCore(song->title);
		const auto performer = Normalize(song->performer);
		const auto artist = FirstArtist(track.artist);
		return !title.isEmpty()
			&& !performer.isEmpty()
			&& !artist.isEmpty()
			&& (title == TitleCore(track.title))
			&& (performer.contains(artist) || artist.contains(performer));
	}
	const auto title = Normalize(song->title);
	const auto wanted = Normalize(track.title);
	const auto performer = Normalize(song->performer);
	const auto artist = Normalize(track.artist);
	if (title.isEmpty() || wanted.isEmpty() || performer.isEmpty()) {
		return false;
	}
	return title.startsWith(wanted)
		&& (performer.contains(artist) || artist.contains(performer));
}

[[nodiscard]] QByteArray Silence(int seconds) {
	const auto frames = (seconds * kSampleRate + kFrameSamples - 1)
		/ kFrameSamples;
	auto result = QByteArray(frames * kFrameBytes, char(0));
	for (auto i = 0; i != frames; ++i) {
		const auto frame = result.data() + i * kFrameBytes;
		frame[0] = char(0xFF);
		frame[1] = char(0xF3);
		frame[2] = char(0x18);
		frame[3] = char(0xC0);
	}
	return result;
}

[[nodiscard]] QByteArray Thumbnail(const QByteArray &bytes) {
	auto image = QImage::fromData(bytes);
	if (image.isNull()) {
		return QByteArray();
	}
	image = image.scaled(
		kThumbSize,
		kThumbSize,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "JPG", 87);
	return result;
}

[[nodiscard]] MTPInputFile InputFile(
		uint64 id,
		const QString &name,
		const QByteArray &bytes) {
	const auto md5 = QCryptographicHash::hash(
		bytes,
		QCryptographicHash::Md5).toHex();
	return MTP_inputFile(
		MTP_long(id),
		MTP_int(1),
		MTP_string(name),
		MTP_string(QString::fromLatin1(md5)));
}

[[nodiscard]] QString BioText(
		not_null<Main::Session*> session,
		const Track &track,
		const QString &bio) {
	const auto limits = Data::PremiumLimits(session);
	const auto limit = session->user()->isPremium()
		? limits.aboutLengthPremium()
		: limits.aboutLengthDefault();
	const auto cut = [](const QString &text, int size) {
		return (text.size() > size)
			? (text.left(std::max(size - 1, 0)).trimmed() + QChar(0x2026))
			: text;
	};
	// "🎧 Artist — Title | bio", the bio shortened first when too long.
	const auto song = cut(
		u"🎧 "_q + track.artist + u" — "_q + track.title,
		limit);
	const auto about = bio.simplified();
	const auto separator = u" | "_q;
	const auto room = limit - song.size() - separator.size();
	return (about.isEmpty() || room < 2)
		? song
		: (song + separator + cut(about, room));
}

[[nodiscard]] std::optional<Track> ParseLastFm(const QJsonObject &root) {
	const auto tracks = root.value(u"recenttracks"_q).toObject()
		.value(u"track"_q);
	const auto first = tracks.isArray()
		? tracks.toArray().at(0).toObject()
		: tracks.toObject();
	const auto playing = first.value(u"@attr"_q).toObject()
		.value(u"nowplaying"_q).toString() == u"true"_q;
	if (!playing) {
		return std::nullopt;
	}
	auto result = Track{
		.title = first.value(u"name"_q).toString().trimmed(),
		.artist = first.value(u"artist"_q).toObject()
			.value(u"#text"_q).toString().trimmed(),
		.url = first.value(u"url"_q).toString(),
	};
	for (const auto image : first.value(u"image"_q).toArray()) {
		const auto url = image.toObject().value(u"#text"_q).toString();
		if (!url.isEmpty()) {
			result.cover = url;
		}
	}
	if (result.title.isEmpty() || result.artist.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<Track> ParseStatsFm(const QJsonObject &root) {
	const auto item = root.value(u"item"_q).toObject();
	if (!item.value(u"isPlaying"_q).toBool()) {
		return std::nullopt;
	}
	const auto track = item.value(u"track"_q).toObject();
	auto artists = QStringList();
	for (const auto artist : track.value(u"artists"_q).toArray()) {
		const auto name = artist.toObject().value(u"name"_q).toString();
		if (!name.trimmed().isEmpty()) {
			artists.push_back(name.trimmed());
		}
	}
	const auto albums = track.value(u"albums"_q).toArray();
	const auto spotify = track.value(u"externalIds"_q).toObject()
		.value(u"spotify"_q).toArray().at(0).toString();
	auto result = Track{
		.title = track.value(u"name"_q).toString().trimmed(),
		.artist = artists.join(u", "_q),
		.url = (spotify.isEmpty()
			? (u"https://stats.fm/track/"_q
				+ QString::number(track.value(u"id"_q).toInteger()))
			: (u"https://open.spotify.com/track/"_q + spotify)),
		.cover = albums.at(0).toObject().value(u"image"_q).toString(),
		.duration = int(track.value(u"durationMs"_q).toInteger() / 1000),
	};
	if (result.title.isEmpty() || result.artist.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

class Manager final : public base::has_weak_ptr {
public:
	Manager();

	[[nodiscard]] std::optional<Track> current() const;
	[[nodiscard]] rpl::producer<std::optional<Track>> value() const;
	[[nodiscard]] rpl::producer<QImage> cover() const;
	[[nodiscard]] bool isQuitPrevent();

private:
	using Done = Fn<void(DocumentData*)>;

	void restart();
	void poll();
	void finish(std::optional<Track> track);
	void fail();
	void requestDuration(Track track);
	void setCurrent(std::optional<Track> track);
	void loadCover(const QString &url, Fn<void(QByteArray)> done);

	void sync();
	void sync(not_null<Main::Session*> session, bool active);
	void syncMusic(
		not_null<Main::Session*> session,
		const std::optional<Track> &track);
	void syncBio(
		not_null<Main::Session*> session,
		const std::optional<Track> &track);
	void findMusic(
		not_null<Main::Session*> session,
		const Track &track,
		Done done);
	void findInBot(
		not_null<Main::Session*> session,
		const Track &track,
		Done done);
	void uploadMusic(
		not_null<Main::Session*> session,
		const Track &track,
		Done done);
	void uploadMusic(
		not_null<Main::Session*> session,
		const Track &track,
		const QByteArray &thumb,
		Done done);
	void removeMusic(not_null<Main::Session*> session, DocumentId id);

	void load();
	void save() const;

	rpl::variable<std::optional<Track>> _current;
	rpl::variable<QImage> _cover;
	QString _coverUrl;
	QByteArray _coverBytes;
	std::vector<Fn<void(QByteArray)>> _coverWaiters;
	base::Timer _timer;
	QPointer<QNetworkReply> _reply;
	uint64 _generation = 0;
	int _failures = 0;
	crl::time _idleSince = 0;
	bool _known = false;
	base::weak_ptr<Main::Session> _session;
	base::weak_ptr<Main::Session> _active;
	std::map<uint64, Applied> _applied;
	base::flat_map<uint64, QString> _musicPending;
	base::flat_set<uint64> _bioLoading;
	base::flat_set<uint64> _staleBioChecked;
	bool _quitStarted = false;
	int _quitRequests = 0;
	rpl::lifetime _lifetime;

};

Manager::Manager()
: _timer([=] { poll(); }) {
	load();
	const auto &settings = AyuSettings::getInstance();
	rpl::merge(
		settings.nowPlayingServiceValue() | rpl::to_empty,
		settings.nowPlayingUsernameValue() | rpl::to_empty,
		settings.nowPlayingLastFmKeyValue() | rpl::to_empty
	) | rpl::on_next([=] {
		restart();
	}, _lifetime);
	Core::App().domain().activeSessionValue(
	) | rpl::on_next([=](Main::Session *session) {
		_active = session
			? base::make_weak(session)
			: base::weak_ptr<Main::Session>();
		sync();
	}, _lifetime);
	rpl::merge(
		settings.nowPlayingProfileMusicValue() | rpl::to_empty,
		settings.nowPlayingBioValue() | rpl::to_empty
	) | rpl::on_next([=] {
		sync();
	}, _lifetime);
}

std::optional<Track> Manager::current() const {
	return _current.current();
}

rpl::producer<std::optional<Track>> Manager::value() const {
	return _current.value();
}

rpl::producer<QImage> Manager::cover() const {
	return _cover.value();
}

bool Manager::isQuitPrevent() {
	if (_quitStarted) {
		return (_quitRequests > 0);
	}
	_quitStarted = true;
	++_generation;
	_timer.cancel();
	const auto keep = AyuSettings::getInstance().nowPlayingKeepMusic();
	const auto finished = [=] {
		if (--_quitRequests == 0) {
			Core::App().quitPreventFinished();
		}
	};
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto session = entry.account->maybeSession();
		if (!session) {
			continue;
		}
		const auto userId = session->userId().bare;
		const auto i = _applied.find(userId);
		if (i == end(_applied)) {
			continue;
		}
		auto &applied = i->second;
		if (applied.bioSaved) {
			const auto about = session->user()->about();
			if (about.isEmpty()
				|| about == applied.bioText
				|| IsNowPlayingBio(about)) {
				++_quitRequests;
				session->api().request(MTPaccount_UpdateProfile(
					MTP_flags(MTPaccount_UpdateProfile::Flag::f_about),
					MTPstring(),
					MTPstring(),
					MTP_string(BioToRestore(userId, applied.bioBackup))
				)).done(finished).fail(finished).send();
			}
		}
		if (const auto music = applied.music; music && !keep) {
			const auto document = session->data().document(music);
			if (document->hasRemoteLocation()) {
				++_quitRequests;
				session->api().request(MTPaccount_SaveMusic(
					MTP_flags(MTPaccount_SaveMusic::Flag::f_unsave),
					document->mtpInput(),
					MTPInputDocument()
				)).done(finished).fail(finished).send();
				applied.music = 0;
				applied.musicKey = QString();
			}
		}
		if (applied.bioSaved) {
			applied = Applied{
				.music = applied.music,
				.musicKey = applied.musicKey,
				.added = std::move(applied.added),
			};
		}
	}
	save();
	return (_quitRequests > 0);
}

void Manager::loadCover(const QString &url, Fn<void(QByteArray)> done) {
	if (url.isEmpty()) {
		done(QByteArray());
		return;
	} else if (_coverUrl == url) {
		if (_coverWaiters.empty()) {
			done(_coverBytes);
		} else {
			_coverWaiters.push_back(std::move(done));
		}
		return;
	}
	_coverUrl = url;
	_coverBytes = QByteArray();
	for (const auto &waiter : base::take(_coverWaiters)) {
		waiter(QByteArray());
	}
	_coverWaiters.push_back(std::move(done));
	const auto reply = Get(QUrl(url));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (_coverUrl != url) {
			return;
		}
		_coverBytes = (reply->error() == QNetworkReply::NoError)
			? reply->readAll()
			: QByteArray();
		for (const auto &waiter : base::take(_coverWaiters)) {
			waiter(_coverBytes);
		}
	});
}

void Manager::restart() {
	++_generation;
	if (_reply) {
		_reply->abort();
	}
	_failures = 0;
	_idleSince = 0;
	_known = false;
	_current = std::nullopt;
	if (Enabled()) {
		_timer.callOnce(kRestartDelay);
	} else {
		_timer.cancel();
	}
	sync();
}

void Manager::poll() {
	if (!Enabled()) {
		return;
	}
	const auto &settings = AyuSettings::getInstance();
	const auto user = settings.nowPlayingUsername().trimmed();
	auto url = QUrl();
	if (settings.nowPlayingService() == NowPlayingService::LastFm) {
		url = QUrl(u"https://ws.audioscrobbler.com/2.0/"_q);
		auto query = QUrlQuery();
		query.addQueryItem(u"method"_q, u"user.getrecenttracks"_q);
		query.addQueryItem(u"user"_q, user);
		query.addQueryItem(u"api_key"_q, settings.nowPlayingLastFmKey().trimmed());
		query.addQueryItem(u"format"_q, u"json"_q);
		query.addQueryItem(u"limit"_q, u"1"_q);
		url.setQuery(query);
	} else {
		url = QUrl(u"https://api.stats.fm/api/v1/users/"_q
			+ QString::fromUtf8(QUrl::toPercentEncoding(user))
			+ u"/streams/current"_q);
	}
	const auto generation = _generation;
	const auto lastFm = (settings.nowPlayingService()
		== NowPlayingService::LastFm);
	_reply = Get(url);
	QObject::connect(_reply, &QNetworkReply::finished, [=, reply = _reply] {
		reply->deleteLater();
		if (generation != _generation) {
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto bytes = reply->readAll();
		const auto root = QJsonDocument::fromJson(bytes).object();
		if (!lastFm && (status == 404 || status == 204)) {
			finish(std::nullopt);
		} else if (reply->error() != QNetworkReply::NoError
			|| root.isEmpty()
			|| root.contains(u"error"_q)) {
			fail();
		} else {
			finish(lastFm ? ParseLastFm(root) : ParseStatsFm(root));
		}
	});
}

void Manager::fail() {
	++_failures;
	const auto delay = std::min(
		kPollInterval << std::min(_failures, 5),
		kMaxBackoff);
	_timer.callOnce(delay);
}

void Manager::finish(std::optional<Track> track) {
	_failures = 0;
	_timer.callOnce(kPollInterval);
	if (!track) {
		const auto now = crl::now();
		if (!_idleSince) {
			_idleSince = now;
		}
		if (_known && (now - _idleSince < kStopDelay)) {
			return;
		}
		const auto wasKnown = std::exchange(_known, true);
		if (!_current.current()) {
			// After a restart nothing changes, yet music and bio may be left over.
			if (!wasKnown) {
				sync();
			}
			return;
		}
		setCurrent(std::nullopt);
		return;
	}
	_idleSince = 0;
	_known = true;
	const auto now = _current.current();
	if (now
		&& now->title == track->title
		&& now->artist == track->artist) {
		return;
	}
	if (!track->duration
		&& AyuSettings::getInstance().nowPlayingService()
			== NowPlayingService::LastFm) {
		requestDuration(std::move(*track));
	} else {
		setCurrent(std::move(track));
	}
}

void Manager::requestDuration(Track track) {
	auto url = QUrl(u"https://ws.audioscrobbler.com/2.0/"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"method"_q, u"track.getInfo"_q);
	query.addQueryItem(u"artist"_q, track.artist);
	query.addQueryItem(u"track"_q, track.title);
	query.addQueryItem(
		u"api_key"_q,
		AyuSettings::getInstance().nowPlayingLastFmKey());
	query.addQueryItem(u"format"_q, u"json"_q);
	url.setQuery(query);
	const auto generation = _generation;
	const auto reply = Get(url);
	QObject::connect(reply, &QNetworkReply::finished, [=]() mutable {
		reply->deleteLater();
		if (generation != _generation) {
			return;
		}
		const auto root = QJsonDocument::fromJson(reply->readAll()).object();
		const auto ms = root.value(u"track"_q).toObject()
			.value(u"duration"_q).toString().toLongLong();
		track.duration = int(ms / 1000);
		setCurrent(std::move(track));
	});
}

void Manager::setCurrent(std::optional<Track> track) {
	if (_current.current() == track) {
		return;
	}
	_cover = QImage();
	const auto url = track ? track->cover : QString();
	_current = std::move(track);
	if (!url.isEmpty()) {
		loadCover(url, crl::guard(this, [=](QByteArray bytes) {
			const auto now = _current.current();
			if (now && now->cover == url) {
				_cover = QImage::fromData(bytes);
			}
		}));
	}
	sync();
}

void Manager::sync() {
	if (_quitStarted) {
		return;
	}
	const auto active = _active.get();
	if (const auto previous = _session.get(); previous && previous != active) {
		sync(previous, false);
	}
	_session = active
		? base::make_weak(active)
		: base::weak_ptr<Main::Session>();
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto session = entry.account->maybeSession();
		if (session && session != active) {
			if (_applied.contains(session->userId().bare)) {
				sync(session, false);
			}
		}
	}
	if (active) {
		sync(active, true);
	}
}

void Manager::sync(not_null<Main::Session*> session, bool active) {
	if (active && Enabled() && !_known) {
		return;
	}
	const auto track = (active && Enabled())
		? _current.current()
		: std::nullopt;
	const auto &settings = AyuSettings::getInstance();
	syncMusic(
		session,
		settings.nowPlayingProfileMusic() ? track : std::nullopt);
	syncBio(session, settings.nowPlayingBio() ? track : std::nullopt);
	const auto i = _applied.find(session->userId().bare);
	if (i != end(_applied)
		&& !i->second.music
		&& i->second.musicKey.isEmpty()
		&& !i->second.bioSaved
		&& i->second.added.empty()) {
		_applied.erase(i);
		save();
	}
}

void Manager::syncMusic(
		not_null<Main::Session*> session,
		const std::optional<Track> &track) {
	const auto userId = session->userId().bare;
	const auto key = track
		? (track->artist + QChar('\n') + track->title)
		: QString();
	auto &applied = _applied[userId];
	const auto pending = _musicPending.find(userId);
	if (pending != end(_musicPending)) {
		if (pending->second == key) {
			return;
		}
		_musicPending.erase(pending);
	}
	if (applied.musicKey == key) {
		return;
	}
	const auto keep = AyuSettings::getInstance().nowPlayingKeepMusic();
	if (!track) {
		if (const auto old = base::take(applied.music); old && !keep) {
			removeMusic(session, old);
		}
		applied.musicKey = QString();
		save();
		return;
	}
	_musicPending[userId] = key;
	const auto weak = base::make_weak(session);
	findMusic(session, *track, crl::guard(this, [=](DocumentData *found) {
		const auto strong = weak.get();
		const auto i = _musicPending.find(userId);
		if (!strong || i == end(_musicPending) || i->second != key) {
			return;
		}
		_musicPending.erase(i);
		auto &applied = _applied[userId];
		const auto old = base::take(applied.music);
		applied.musicKey = key;
		auto &music = strong->data().savedMusic();
		if (found && found->id == old) {
			applied.music = old;
		} else if (found && !music.has(found)) {
			music.save(found, Data::FileOrigin());
			applied.music = found->id;
			RememberAdded(applied, found->id);
		} else if (found && ranges::contains(applied.added, found->id)) {
			// Ours from an earlier play: back to the top of the profile.
			music.remove(found);
			music.save(found, Data::FileOrigin());
			applied.music = found->id;
		}
		if (old && old != applied.music && !keep) {
			removeMusic(strong, old);
		}
		save();
	}));
}

void Manager::findMusic(
		not_null<Main::Session*> session,
		const Track &track,
		Done done) {
	session->data().savedMusic().loadIds();
	const auto weak = base::make_weak(session);
	session->api().request(MTPmessages_SearchGlobal(
		MTP_flags(0),
		MTPint(),
		MTPInputChannel(),
		MTP_string(track.artist + QChar(' ') + track.title),
		MTP_inputMessagesFilterMusic(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_inputPeerEmpty(),
		MTP_int(0),
		MTP_int(kSearchLimit)
	)).done([=](const MTPmessages_Messages &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto parsed = Api::ParseGlobalMediaResult(strong, result, false);
		for (const auto &position : parsed.messageIds) {
			const auto item = strong->data().message(position.fullId);
			const auto media = item ? item->media() : nullptr;
			const auto document = media ? media->document() : nullptr;
			if (document && Matches(document, track)) {
				done(document);
				return;
			}
		}
		findInBot(strong, track, done);
	}).fail([=] {
		if (const auto strong = weak.get()) {
			findInBot(strong, track, done);
		}
	}).send();
}

// Telegram's own music search has the real track far more often than your chats.
void Manager::findInBot(
		not_null<Main::Session*> session,
		const Track &track,
		Done done) {
	const auto username = session->appConfig().get<QString>(
		u"music_search_username"_q,
		QString());
	const auto weak = base::make_weak(session);
	const auto fallback = [=] {
		if (const auto strong = weak.get()) {
			uploadMusic(strong, track, done);
		}
	};
	if (username.isEmpty()) {
		fallback();
		return;
	}
	const auto search = [=](UserData *bot) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		} else if (!bot) {
			fallback();
			return;
		}
		strong->api().request(MTPmessages_GetInlineBotResults(
			MTP_flags(0),
			bot->inputUser(),
			strong->user()->input(),
			MTPInputGeoPoint(),
			MTP_string(track.artist + QChar(' ') + track.title),
			MTP_string()
		)).done([=](const MTPmessages_BotResults &result) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			const auto &data = result.data();
			strong->data().processUsers(data.vusers());
			auto songs = std::vector<not_null<DocumentData*>>();
			for (const auto &item : data.vresults().v) {
				const auto parsed = InlineBots::Result::Create(
					strong,
					data.vquery_id().v,
					item);
				const auto document = parsed ? parsed->document() : nullptr;
				if (document
					&& document->isSong()
					&& parsed->contentUrl().isEmpty()) {
					songs.push_back(document);
				}
			}
			for (const auto loose : { false, true }) {
				for (const auto document : songs) {
					if (Matches(document, track, loose)) {
						done(document);
						return;
					}
				}
			}
			fallback();
		}).fail(fallback).send();
	};
	if (const auto peer = session->data().peerByUsername(username)) {
		search(peer->asUser());
		return;
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto &data = result.data();
		strong->data().processUsers(data.vusers());
		strong->data().processChats(data.vchats());
		const auto peer = strong->data().peerLoaded(peerFromMTP(data.vpeer()));
		search(peer ? peer->asUser() : nullptr);
	}).fail(fallback).send();
}

void Manager::uploadMusic(
		not_null<Main::Session*> session,
		const Track &track,
		Done done) {
	const auto weak = base::make_weak(session);
	loadCover(track.cover, crl::guard(this, [=](QByteArray bytes) {
		if (const auto strong = weak.get()) {
			uploadMusic(strong, track, Thumbnail(bytes), done);
		}
	}));
}

void Manager::uploadMusic(
		not_null<Main::Session*> session,
		const Track &track,
		const QByteArray &thumb,
		Done done) {
	const auto seconds = std::clamp(track.duration, 1, kMaxSilence);
	const auto audio = Silence(seconds);
	const auto audioId = base::RandomValue<uint64>();
	const auto thumbId = thumb.isEmpty() ? 0 : base::RandomValue<uint64>();
	const auto weak = base::make_weak(session);
	const auto media = [=] {
		using Flag = MTPDinputMediaUploadedDocument::Flag;
		using AudioFlag = MTPDdocumentAttributeAudio::Flag;
		const auto name = track.artist + u" - "_q + track.title + u".mp3"_q;
		auto attributes = QVector<MTPDocumentAttribute>{
			MTP_documentAttributeAudio(
				MTP_flags(AudioFlag::f_title | AudioFlag::f_performer),
				MTP_int(std::max(track.duration, seconds)),
				MTP_string(track.title),
				MTP_string(track.artist),
				MTPbytes()),
			MTP_documentAttributeFilename(MTP_string(name)),
		};
		return MTP_inputMediaUploadedDocument(
			MTP_flags(thumbId ? Flag::f_thumb : Flag(0)),
			InputFile(audioId, name, audio),
			(thumbId
				? InputFile(thumbId, u"thumb.jpg"_q, thumb)
				: MTPInputFile()),
			MTP_string("audio/mpeg"),
			MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
			MTPVector<MTPInputDocument>(),
			MTPInputPhoto(),
			MTPint(),
			MTPint());
	};
	const auto send = [=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->api().request(MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			MTP_inputPeerSelf(),
			media()
		)).done([=](const MTPMessageMedia &result) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto document = (DocumentData*)nullptr;
			result.match([&](const MTPDmessageMediaDocument &data) {
				if (const auto received = data.vdocument()) {
					document = strong->data().processDocument(*received);
				}
			}, [](const auto &) {});
			done(document);
		}).fail([=] {
			done(nullptr);
		}).send();
	};
	const auto part = [=](uint64 id, QByteArray bytes, Fn<void()> next) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->api().request(MTPupload_SaveFilePart(
			MTP_long(id),
			MTP_int(0),
			MTP_bytes(bytes)
		)).done(next).fail([=] {
			done(nullptr);
		}).send();
	};
	part(audioId, audio, thumbId ? Fn<void()>([=] {
		part(thumbId, thumb, send);
	}) : Fn<void()>(send));
}

void Manager::removeMusic(not_null<Main::Session*> session, DocumentId id) {
	const auto document = session->data().document(id);
	if (document->hasRemoteLocation()) {
		session->data().savedMusic().remove(document);
		return;
	}
	const auto weak = base::make_weak(session);
	session->api().request(MTPusers_GetSavedMusic(
		MTP_inputUserSelf(),
		MTP_int(0),
		MTP_int(kSearchLimit),
		MTP_long(0)
	)).done([=](const MTPusers_SavedMusic &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		result.match([&](const MTPDusers_savedMusic &data) {
			for (const auto &received : data.vdocuments().v) {
				const auto document = strong->data().processDocument(received);
				if (document->id == id) {
					strong->data().savedMusic().remove(document);
				}
			}
		}, [](const auto &) {});
	}).send();
}

void Manager::syncBio(
		not_null<Main::Session*> session,
		const std::optional<Track> &track) {
	const auto userId = session->userId().bare;
	const auto self = session->user();
	auto &applied = _applied[userId];
	if (!track) {
		if (applied.bioSaved) {
			const auto about = self->about();
			if (about.isEmpty()
				|| about == applied.bioText
				|| IsNowPlayingBio(about)) {
				session->api().saveSelfBio(
					BioToRestore(userId, applied.bioBackup));
			}
			applied = Applied{
				.music = applied.music,
				.musicKey = applied.musicKey,
				.added = std::move(applied.added),
			};
			save();
		} else if (!_staleBioChecked.contains(userId)) {
			// A "🎧" bio left by a build that lost its state.
			_staleBioChecked.emplace(userId);
			const auto weak = base::make_weak(session);
			session->api().request(MTPusers_GetFullUser(
				MTP_inputUserSelf()
			)).done(crl::guard(this, [=](const MTPusers_UserFull &result) {
				const auto strong = weak.get();
				const auto about = result.data().vfull_user().data().vabout();
				const auto bio = about ? qs(*about) : QString();
				if (strong
					&& IsNowPlayingBio(bio)
					&& !_current.current()
					&& !_applied[userId].bioSaved) {
					strong->api().saveSelfBio(BioToRestore(userId, bio));
				}
			})).send();
		}
		return;
	}
	if (!applied.bioSaved) {
		if (_bioLoading.contains(userId)) {
			return;
		}
		_bioLoading.emplace(userId);
		const auto weak = base::make_weak(session);
		session->api().request(MTPusers_GetFullUser(
			MTP_inputUserSelf()
		)).done(crl::guard(this, [=](const MTPusers_UserFull &result) {
			_bioLoading.remove(userId);
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			auto &applied = _applied[userId];
			const auto about = result.data().vfull_user().data().vabout();
			const auto bio = about ? qs(*about) : QString();
			applied.bioBackup = IsNowPlayingBio(bio)
				? BioToRestore(userId, bio)
				: bio;
			applied.bioSaved = true;
			WriteBioBackup(userId, applied.bioBackup);
			save();
			sync();
		})).fail(crl::guard(this, [=] {
			_bioLoading.remove(userId);
		})).send();
		return;
	}
	const auto text = BioText(
		session,
		*track,
		BioToRestore(userId, applied.bioBackup));
	const auto about = self->about();
	if (!about.isEmpty()
		&& !applied.bioText.isEmpty()
		&& about != applied.bioText
		&& about != text
		&& !IsNowPlayingBio(about)) {
		applied.bioBackup = about;
		WriteBioBackup(userId, about);
	}
	if (applied.bioText != text) {
		applied.bioText = text;
		save();
	}
	if (about != text) {
		session->api().saveSelfBio(text);
	}
}

void Manager::load() {
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = root.begin(); i != root.end(); ++i) {
		const auto userId = i.key().toULongLong();
		const auto value = i.value().toObject();
		if (!userId) {
			continue;
		}
		_applied[userId] = Applied{
			.music = value.value(u"music"_q).toString().toULongLong(),
			.musicKey = value.value(u"musicKey"_q).toString(),
			.bioBackup = value.value(u"bioBackup"_q).toString(),
			.bioText = value.value(u"bioText"_q).toString(),
			.bioSaved = value.value(u"bioSaved"_q).toBool(),
		};
		for (const auto id : value.value(u"added"_q).toArray()) {
			_applied[userId].added.push_back(id.toString().toULongLong());
		}
	}
}

void Manager::save() const {
	auto root = QJsonObject();
	for (const auto &[userId, applied] : _applied) {
		root.insert(QString::number(userId), QJsonObject{
			{ u"music"_q, QString::number(applied.music) },
			{ u"musicKey"_q, applied.musicKey },
			{ u"bioBackup"_q, applied.bioBackup },
			{ u"bioText"_q, applied.bioText },
			{ u"bioSaved"_q, applied.bioSaved },
			{ u"added"_q, [&] {
				auto list = QJsonArray();
				for (const auto id : applied.added) {
					list.push_back(QString::number(id));
				}
				return list;
			}() },
		});
	}
	auto file = QFile(StatePath());
	if (root.isEmpty()) {
		file.remove();
	} else if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	}
}

Manager *Instance = nullptr;

}

void Start() {
	if (!Instance) {
		Instance = new Manager();
	}
}

bool IsQuitPrevent() {
	return Instance && Instance->isQuitPrevent();
}

std::optional<Track> Current() {
	return Instance ? Instance->current() : std::nullopt;
}

rpl::producer<std::optional<Track>> Value() {
	return Instance
		? Instance->value()
		: rpl::single(std::optional<Track>());
}

rpl::producer<QImage> CoverValue() {
	return Instance ? Instance->cover() : rpl::single(QImage());
}

QString SiteUrl(NowPlayingService service) {
	return (service == NowPlayingService::StatsFm)
		? u"https://stats.fm/"_q
		: u"https://www.last.fm/"_q;
}

}
