// This is the source code of KomaruGram for Desktop.
#include "ayu/features/analytics/komaru_analytics.h"

#include "ayu/ayu_settings.h"
#include "base/unixtime.h"
#include "core/version.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSysInfo>
#include <QtCore/QUrlQuery>
#include <QtCore/QUuid>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#ifndef KOMARUGRAM_GA_MEASUREMENT_ID
#define KOMARUGRAM_GA_MEASUREMENT_ID ""
#endif // KOMARUGRAM_GA_MEASUREMENT_ID

#ifndef KOMARUGRAM_GA_API_SECRET
#define KOMARUGRAM_GA_API_SECRET ""
#endif // KOMARUGRAM_GA_API_SECRET

namespace KomaruAnalytics {
namespace {

constexpr auto kMeasurementId = KOMARUGRAM_GA_MEASUREMENT_ID;
constexpr auto kApiSecret = KOMARUGRAM_GA_API_SECRET;

// GA4 drops parameter values longer than this.
constexpr auto kMaxValueLength = 100;

struct State {
	QString clientId;
	int lastVersion = 0;
};

[[nodiscard]] QString StatePath() {
	return cWorkingDir() + u"tdata/komaru_analytics"_q;
}

[[nodiscard]] State ReadState() {
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	return {
		.clientId = object.value(u"client_id"_q).toString(),
		.lastVersion = object.value(u"last_version"_q).toInt(),
	};
}

void WriteState(const State &state) {
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LOG(("Analytics Error: could not write %1").arg(StatePath()));
		return;
	}
	file.write(QJsonDocument(QJsonObject{
		{ u"client_id"_q, state.clientId },
		{ u"last_version"_q, state.lastVersion },
	}).toJson(QJsonDocument::Compact));
}

// A random id per installation, generated here and stored next to the
// rest of tdata. It is not derived from the account or the machine.
[[nodiscard]] State LoadOrCreateState() {
	auto state = ReadState();
	if (state.clientId.isEmpty()) {
		state.clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		WriteState(state);
	}
	return state;
}

[[nodiscard]] bool Enabled() {
	return Available()
		&& AyuSettings::getInstance().sendAnonymousStatistics();
}

[[nodiscard]] QString Clip(QString value) {
	return (value.size() > kMaxValueLength)
		? value.left(kMaxValueLength)
		: value;
}

[[nodiscard]] QJsonObject CommonParams() {
	// One session per process: GA4 needs session_id and a non-zero
	// engagement time to count the event towards active users.
	static const auto SessionId = QString::number(base::unixtime::now());
	return {
		{ u"session_id"_q, SessionId },
		{ u"engagement_time_msec"_q, 1 },
		{ u"app_version"_q, QString::fromLatin1(AppVersionStr) },
		{ u"os"_q, Clip(QSysInfo::productType()) },
		{ u"os_version"_q, Clip(QSysInfo::productVersion()) },
		{ u"arch"_q, Clip(QSysInfo::buildCpuArchitecture()) },
	};
}

[[nodiscard]] QNetworkAccessManager &Manager() {
	// Owned by the application, so it goes away with it instead of being
	// torn down after QApplication by static destruction.
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return *result;
}

void Send(
		const QString &clientId,
		const QString &name,
		QJsonObject params) {
	for (auto i = params.begin(); i != params.end(); ++i) {
		if (i.value().isString()) {
			i.value() = Clip(i.value().toString());
		}
	}
	auto merged = CommonParams();
	for (auto i = params.begin(); i != params.end(); ++i) {
		merged.insert(i.key(), i.value());
	}
	const auto body = QJsonDocument(QJsonObject{
		{ u"client_id"_q, clientId },
		{ u"non_personalized_ads"_q, true },
		{ u"events"_q, QJsonArray{ QJsonObject{
			{ u"name"_q, name },
			{ u"params"_q, merged },
		} } },
	}).toJson(QJsonDocument::Compact);

	auto query = QUrlQuery();
	query.addQueryItem(u"measurement_id"_q, QString::fromLatin1(kMeasurementId));
	query.addQueryItem(u"api_secret"_q, QString::fromLatin1(kApiSecret));
	auto url = QUrl(u"https://www.google-analytics.com/mp/collect"_q);
	url.setQuery(query);

	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	const auto reply = Manager().post(request, body);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Analytics Error: %1 not sent, %2"
				).arg(name, reply->errorString()));
		}
		reply->deleteLater();
	});
}

} // namespace

bool Available() {
	return (kMeasurementId[0] != '\0') && (kApiSecret[0] != '\0');
}

void TrackLaunch() {
	auto state = LoadOrCreateState();
	const auto previous = state.lastVersion;
	if (previous != AppVersion) {
		// Keep the record current even with statistics off, so turning them
		// on later does not report an update that happened long ago.
		state.lastVersion = AppVersion;
		WriteState(state);
	}
	if (!Enabled()) {
		return;
	}
	// first_seen marks the first launch that statistics saw; installs that
	// predate this build report it once too.
	Send(state.clientId, u"app_launch"_q, {
		{ u"first_seen"_q, previous ? 0 : 1 },
	});
	if (previous && previous < AppVersion) {
		Send(state.clientId, u"app_update"_q, {
			{ u"from_version"_q, QString::number(previous) },
			{ u"to_version"_q, QString::number(AppVersion) },
		});
	}
}

void TrackCrash(const QByteArray &report) {
	if (!Enabled()) {
		return;
	}
	const auto text = QString::fromUtf8(report);
	const auto field = [&](const QString &prefix) {
		for (const auto &line : text.split('\n')) {
			const auto trimmed = line.trimmed();
			if (trimmed.startsWith(prefix)) {
				return trimmed.mid(prefix.size()).trimmed();
			}
		}
		return QString();
	};
	// "Caught signal 11 (SIGSEGV) ..." on Linux, a Breakpad line on Windows.
	const auto kind = [&] {
		static const auto Signal = QRegularExpression(
			u"Caught signal (\\d+)(?: \\(([A-Z]+)\\))?"_q);
		const auto match = Signal.match(text);
		if (match.hasMatch()) {
			return match.captured(2).isEmpty()
				? (u"signal "_q + match.captured(1))
				: match.captured(2);
		} else if (text.contains(u"Google Breakpad caught a crash"_q)) {
			return u"breakpad"_q;
		}
		return u"unknown"_q;
	}();
	Send(LoadOrCreateState().clientId, u"app_crash"_q, {
		{ u"crashed_version"_q, field(u"Version:"_q) },
		{ u"crash_kind"_q, kind },
	});
}

} // namespace KomaruAnalytics
