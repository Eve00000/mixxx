#include "library/soundcloud/soundcloudclient.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

#include "moc_soundcloudclient.cpp"
#include "util/logger.h"

namespace mixxx {
namespace soundcloud {

namespace {

const Logger kLogger("SoundCloudClient");

const ConfigKey kClientIdKey = ConfigKey("[SoundCloud]", "ClientId");

// A current desktop browser UA is required by some SoundCloud endpoints.
const QString kUserAgent = QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

const QString kApiBase = QStringLiteral("https://api-v2.soundcloud.com/");

// Formats whose protocol indicates DRM/encryption; always skipped.
bool isEncryptedTranscoding(const QJsonObject& transcoding) {
    const QString protocol =
            transcoding.value(QLatin1String("format")).toObject()
                    .value(QLatin1String("protocol")).toString();
    const QString url = transcoding.value(QLatin1String("url")).toString();
    return protocol.startsWith(QLatin1String("ctr-")) ||
            protocol.startsWith(QLatin1String("cbc-")) ||
            url.contains(QLatin1String("/encrypted-hls/"));
}

mixxx::streaming::Track parseTrack(const QJsonObject& obj) {
    mixxx::streaming::Track track;
    track.providerId = QStringLiteral("soundcloud");
    track.id = QString::number(static_cast<qint64>(
            obj.value(QLatin1String("id")).toDouble()));
    track.title = obj.value(QLatin1String("title")).toString();
    track.artist = obj.value(QLatin1String("user")).toObject()
                           .value(QLatin1String("username")).toString();
    track.album.clear();
    // duration is in milliseconds.
    track.durationSec = obj.value(QLatin1String("duration")).toInt() / 1000;
    track.isExplicit = obj.value(QLatin1String("policy")).toString() ==
            QLatin1String("MONETIZE");
    return track;
}

/// Chooses the best non-DRM transcoding. Prefers progressive (single file),
/// then HLS AAC, then any HLS. Returns an empty object if none is usable.
QJsonObject chooseTranscoding(const QJsonArray& transcodings) {
    QJsonObject bestHlsAac;
    QJsonObject bestHlsOther;
    for (const auto& value : transcodings) {
        const QJsonObject tc = value.toObject();
        if (isEncryptedTranscoding(tc)) {
            continue;
        }
        const QString protocol =
                tc.value(QLatin1String("format")).toObject()
                        .value(QLatin1String("protocol")).toString();
        const QString preset = tc.value(QLatin1String("preset")).toString();
        if (protocol == QLatin1String("progressive")) {
            // Progressive is a complete, seekable file: always preferred.
            return tc;
        }
        if (protocol == QLatin1String("hls")) {
            if (preset.contains(QLatin1String("aac"))) {
                if (bestHlsAac.isEmpty()) {
                    bestHlsAac = tc;
                }
            } else if (bestHlsOther.isEmpty()) {
                bestHlsOther = tc;
            }
        }
    }
    if (!bestHlsAac.isEmpty()) {
        return bestHlsAac;
    }
    return bestHlsOther;
}

} // anonymous namespace

SoundCloudClient::SoundCloudClient(UserSettingsPointer pConfig, QObject* parent)
        : mixxx::streaming::Provider(parent),
          m_pConfig(std::move(pConfig)),
          m_pNetwork(new QNetworkAccessManager(this)) {
    m_clientId = m_pConfig->getValueString(kClientIdKey);
}

SoundCloudClient::~SoundCloudClient() = default;

void SoundCloudClient::getJson(
        const QUrl& url,
        std::function<void(bool, const QByteArray&, const QString&)> callback) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    request.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = m_pNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [reply, callback = std::move(callback)]() {
                reply->deleteLater();
                const QByteArray data = reply->readAll();
                if (reply->error() != QNetworkReply::NoError) {
                    callback(false, data, reply->errorString());
                    return;
                }
                callback(true, data, QString());
            });
}

void SoundCloudClient::ensureClientId(mixxx::streaming::ResultCallback callback) {
    if (!m_clientId.isEmpty()) {
        callback(true, QString());
        return;
    }
    // Scrape the web app for a client_id. First try the __sc_hydration block,
    // then fall back to scanning the JS assets.
    QNetworkRequest request(QUrl(QStringLiteral("https://soundcloud.com/")));
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    QNetworkReply* reply = m_pNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, callback, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(false, reply->errorString());
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());
        const QStringList scriptUrls = [&html]() {
            QStringList urls;
            const QRegularExpression re(
                    QStringLiteral("<script[^>]+src=\"([^\"]+)\""));
            auto it = re.globalMatch(html);
            while (it.hasNext()) {
                urls.append(it.next().captured(1));
            }
            return urls;
        }();

        // Fetch JS assets in reverse order and look for the client_id.
        auto tryNext = std::make_shared<std::function<void(int)>>();
        std::weak_ptr<std::function<void(int)>> weakTryNext = tryNext;
        *tryNext = [this, scriptUrls, callback, weakTryNext](int index) {
            if (index < 0) {
                callback(false, tr("Could not determine a SoundCloud client id."));
                return;
            }
            QNetworkRequest req(QUrl(scriptUrls.at(index)));
            req.setRawHeader("User-Agent", kUserAgent.toUtf8());
            QNetworkReply* r = m_pNetwork->get(req);
            connect(r, &QNetworkReply::finished, this,
                    [this, r, index, callback, weakTryNext]() {
                        r->deleteLater();
                        const QString js = QString::fromUtf8(r->readAll());
                        static const QRegularExpression idRe(
                                QStringLiteral("client_id\\s*:\\s*\"([0-9a-zA-Z]{32})\""));
                        const auto match = idRe.match(js);
                        if (match.hasMatch()) {
                            m_clientId = match.captured(1);
                            m_pConfig->setValue(kClientIdKey, m_clientId);
                            callback(true, QString());
                            return;
                        }
                        if (auto next = weakTryNext.lock()) {
                            (*next)(index - 1);
                        }
                    });
        };
        (*tryNext)(scriptUrls.size() - 1);
    });
}

void SoundCloudClient::search(
        const QString& query,
        mixxx::streaming::SearchCallback callback) {
    ensureClientId([this, query, callback](bool ok, const QString& error) {
        if (!ok) {
            callback(false, {}, error);
            return;
        }
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), query);
        q.addQueryItem(QStringLiteral("client_id"), m_clientId);
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
        q.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
        q.addQueryItem(QStringLiteral("linked_partitioning"), QStringLiteral("1"));
        QUrl url(kApiBase + QStringLiteral("search/tracks"));
        url.setQuery(q);
        getJson(url, [callback](bool ok, const QByteArray& data, const QString& error) {
            if (!ok) {
                callback(false, {}, error);
                return;
            }
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            const QJsonArray items = root.value(QLatin1String("collection")).toArray();
            mixxx::streaming::TrackList tracks;
            tracks.reserve(items.size());
            for (const auto& item : items) {
                tracks.append(parseTrack(item.toObject()));
            }
            callback(true, tracks, QString());
        });
    });
}

void SoundCloudClient::downloadTrack(
        const mixxx::streaming::Track& track,
        mixxx::streaming::StreamCallback callback) {
    ensureClientId([this, track, callback](bool ok, const QString& error) {
        if (!ok) {
            callback(false, {}, error);
            return;
        }
        // Re-fetch the track to obtain media.transcodings and
        // track_authorization.
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("client_id"), m_clientId);
        QUrl url(kApiBase + QStringLiteral("tracks/") + track.id);
        url.setQuery(q);
        getJson(url,
                [this, track, callback](bool ok, const QByteArray& data, const QString& error) {
                    if (!ok) {
                        callback(false, {}, error);
                        return;
                    }
                    resolveStreamUrl(track, data, std::move(callback));
                });
    });
}

void SoundCloudClient::resolveStreamUrl(
        const mixxx::streaming::Track& track,
        const QByteArray& trackJson,
        mixxx::streaming::StreamCallback callback) {
    const QJsonObject root = QJsonDocument::fromJson(trackJson).object();
    const QJsonArray transcodings = root.value(QLatin1String("media")).toObject()
                                            .value(QLatin1String("transcodings"))
                                            .toArray();
    const QJsonObject transcoding = chooseTranscoding(transcodings);
    if (transcoding.isEmpty()) {
        callback(false, {}, tr("No playable (unencrypted) SoundCloud stream found."));
        return;
    }
    const QString transcodingUrl = transcoding.value(QLatin1String("url")).toString();
    const QString trackAuthorization =
            root.value(QLatin1String("track_authorization")).toString();

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), m_clientId);
    if (!trackAuthorization.isEmpty()) {
        q.addQueryItem(QStringLiteral("track_authorization"), trackAuthorization);
    }
    QUrl url(transcodingUrl);
    url.setQuery(q);
    getJson(url,
            [this, track, transcoding, callback](
                    bool ok, const QByteArray& data, const QString& error) {
                if (!ok) {
                    callback(false, {}, error);
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(data).object();
                const QString signedUrl = obj.value(QLatin1String("url")).toString();
                if (signedUrl.isEmpty()) {
                    callback(false, {}, tr("SoundCloud did not return a stream URL."));
                    return;
                }
                const QString protocol =
                        transcoding.value(QLatin1String("format")).toObject()
                                .value(QLatin1String("protocol")).toString();
                if (protocol == QLatin1String("progressive")) {
                    downloadProgressive(track, QUrl(signedUrl), std::move(callback));
                } else {
                    downloadHls(track, QUrl(signedUrl), std::move(callback));
                }
            });
}

QString SoundCloudClient::cacheFilePath(
        const mixxx::streaming::Track& track,
        const QString& suffix) const {
    QDir cacheDir(QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation));
    cacheDir.mkpath(QStringLiteral("soundcloud"));
    cacheDir.cd(QStringLiteral("soundcloud"));
    return cacheDir.filePath(QStringLiteral("%1.%2").arg(track.id, suffix));
}

void SoundCloudClient::downloadProgressive(
        const mixxx::streaming::Track& track,
        const QUrl& url,
        mixxx::streaming::StreamCallback callback) {
    const QString filePath = cacheFilePath(track, QStringLiteral("mp3"));
    QFile existing(filePath);
    if (existing.exists() && existing.size() > 0) {
        callback(true, QUrl::fromLocalFile(filePath), QString());
        return;
    }
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    QNetworkReply* reply = m_pNetwork->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0) {
                    emit downloadProgress(static_cast<int>(received / 1024),
                            static_cast<int>(total / 1024));
                }
            });
    connect(reply, &QNetworkReply::finished, this, [reply, filePath, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(false, {}, reply->errorString());
            return;
        }
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            callback(false, {}, file.errorString());
            return;
        }
        file.write(reply->readAll());
        file.close();
        callback(true, QUrl::fromLocalFile(filePath), QString());
    });
}

bool SoundCloudClient::parseM3u8(
        const QByteArray& playlist,
        QUrl* initUrl,
        QList<QUrl>* segmentUrls) {
    const QString text = QString::fromUtf8(playlist);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1String("#EXT-X-KEY"))) {
            // Encrypted HLS is not supported.
            return false;
        }
        if (line.startsWith(QLatin1String("#EXT-X-MAP:URI=\""))) {
            const int start = line.indexOf(QLatin1Char('"')) + 1;
            const int end = line.indexOf(QLatin1Char('"'), start);
            if (start > 0 && end > start) {
                *initUrl = QUrl(line.mid(start, end - start));
            }
        } else if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
            segmentUrls->append(QUrl(line));
        }
    }
    return !segmentUrls->isEmpty();
}

bool SoundCloudClient::finalizeHlsFile(
        const QList<QByteArray>& parts,
        bool isMp4,
        const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        kLogger.warning() << "Failed to write" << filePath << file.errorString();
        return false;
    }
    for (const QByteArray& part : parts) {
        file.write(part);
    }
    file.close();
    Q_UNUSED(isMp4);
    return true;
}

void SoundCloudClient::downloadHls(
        const mixxx::streaming::Track& track,
        const QUrl& playlistUrl,
        mixxx::streaming::StreamCallback callback) {
    QNetworkRequest request(playlistUrl);
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    QNetworkReply* reply = m_pNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, track, callback = std::move(callback), reply]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    callback(false, {}, reply->errorString());
                    return;
                }
                QUrl initUrl;
                QList<QUrl> segmentUrls;
                if (!parseM3u8(reply->readAll(), &initUrl, &segmentUrls)) {
                    callback(false, {},
                            tr("Unsupported or encrypted SoundCloud HLS stream."));
                    return;
                }
                const bool isMp4 = segmentUrls.first().path()
                                           .endsWith(QLatin1String(".m4s")) ||
                        segmentUrls.first().path().endsWith(QLatin1String(".mp4"));
                const QString suffix = isMp4 ? QStringLiteral("m4a")
                                             : QStringLiteral("mp3");
                const QString filePath = cacheFilePath(track, suffix);
                QFile existing(filePath);
                if (existing.exists() && existing.size() > 0) {
                    callback(true, QUrl::fromLocalFile(filePath), QString());
                    return;
                }

                // Download init (if any) plus all segments sequentially.
                QList<QUrl> allUrls;
                if (initUrl.isValid()) {
                    allUrls.append(initUrl);
                }
                allUrls.append(segmentUrls);

                auto parts = std::make_shared<QList<QByteArray>>();
                auto index = std::make_shared<int>(0);
                auto pump = std::make_shared<std::function<void()>>();
                std::weak_ptr<std::function<void()>> weakPump = pump;
                *pump = [this, allUrls, parts, index, filePath, isMp4,
                                callback, weakPump]() {
                    if (*index >= allUrls.size()) {
                        if (finalizeHlsFile(*parts, isMp4, filePath)) {
                            callback(true, QUrl::fromLocalFile(filePath), QString());
                        } else {
                            callback(false, {},
                                    tr("Failed to write SoundCloud stream file."));
                        }
                        return;
                    }
                    const int current = (*index)++;
                    QNetworkRequest request(allUrls.at(current));
                    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
                    QNetworkReply* segmentReply = m_pNetwork->get(request);
                    connect(segmentReply, &QNetworkReply::finished, this,
                            [segmentReply, parts, callback, weakPump, current,
                                    total = allUrls.size()]() {
                                segmentReply->deleteLater();
                                if (segmentReply->error() != QNetworkReply::NoError) {
                                    callback(false, {},
                                            segmentReply->errorString());
                                    return;
                                }
                                parts->append(segmentReply->readAll());
                                if (auto pump = weakPump.lock()) {
                                    (*pump)();
                                }
                            });
                };
                (*pump)();
            });
}

} // namespace soundcloud
} // namespace mixxx
