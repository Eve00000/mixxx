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
#include <algorithm>
#include <memory>

#include "moc_soundcloudclient.cpp"
#include "util/logger.h"

namespace mixxx {
namespace soundcloud {

namespace {

const Logger kLogger("SoundCloudClient");

const ConfigKey kClientIdKey = ConfigKey("[SoundCloud]", "ClientId");
const ConfigKey kOAuthTokenKey = ConfigKey("[SoundCloud]", "OAuthToken");

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
            url.contains(QLatin1String("encrypted-hls"));
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

/// Ranks the usable (non-DRM) transcodings best first. Logged-in users may
/// receive hq (256 kbps AAC) transcodings which are preferred. Progressive
/// (single file) beats HLS only when the quality is otherwise equal.
QList<QJsonObject> rankedTranscodings(const QJsonArray& transcodings) {
    QList<QJsonObject> result;
    for (const auto& value : transcodings) {
        const QJsonObject tc = value.toObject();
        if (isEncryptedTranscoding(tc)) {
            continue;
        }
        if (tc.value(QLatin1String("snipped")).toBool()) {
            // A preview is only useful as a last resort.
            continue;
        }
        const QString protocol =
                tc.value(QLatin1String("format")).toObject()
                        .value(QLatin1String("protocol")).toString();
        if (protocol != QLatin1String("progressive") &&
                protocol != QLatin1String("hls")) {
            continue;
        }
        result.append(tc);
    }
    std::stable_sort(result.begin(),
            result.end(),
            [](const QJsonObject& a, const QJsonObject& b) {
                const int qa = a.value(QLatin1String("quality")).toString() ==
                                QLatin1String("hq")
                        ? 2
                        : a.value(QLatin1String("quality")).toString() ==
                                        QLatin1String("sq")
                        ? 1
                        : 0;
                const int qb = b.value(QLatin1String("quality")).toString() ==
                                QLatin1String("hq")
                        ? 2
                        : b.value(QLatin1String("quality")).toString() ==
                                        QLatin1String("sq")
                        ? 1
                        : 0;
                if (qa != qb) {
                    return qa > qb;
                }
                const QString pa =
                        a.value(QLatin1String("format")).toObject()
                                .value(QLatin1String("protocol")).toString();
                const QString pb =
                        b.value(QLatin1String("format")).toObject()
                                .value(QLatin1String("protocol")).toString();
                // Progressive (single file) is slightly preferred.
                return pa == QLatin1String("progressive") &&
                        pb != QLatin1String("progressive");
            });
    return result;
}

} // anonymous namespace

SoundCloudClient::SoundCloudClient(UserSettingsPointer pConfig, QObject* parent)
        : mixxx::streaming::Provider(parent),
          m_pConfig(std::move(pConfig)),
          m_pNetwork(new QNetworkAccessManager(this)) {
    m_clientId = m_pConfig->getValueString(kClientIdKey);
    m_oauthToken = m_pConfig->getValueString(kOAuthTokenKey);
}

SoundCloudClient::~SoundCloudClient() = default;

QString SoundCloudClient::manualTokenPrompt() const {
    return QStringLiteral(
            "<p>Logging in is optional for SoundCloud, but it unlocks the "
            "SoundCloud Go+ catalogue (256 kbps AAC), private tracks and the "
            "original (lossless) file where the artist enabled downloads.</p>"
            "<p>SoundCloud does not offer a third-party login, so paste your "
            "<b>oauth_token</b> cookie:</p>"
            "<ol>"
            "<li>Log in to <a href=\"https://soundcloud.com\">soundcloud.com</a> "
            "in your browser.</li>"
            "<li>Open developer tools (F12) &#8594; "
            "<i>Application &#8594; Cookies &#8594; soundcloud.com</i>.</li>"
            "<li>Copy the value of the cookie named <b>oauth_token</b>.</li>"
            "<li>Paste it below.</li>"
            "</ol>"
            "<p>This grants access to your account, so treat it like a "
            "password.</p>");
}

void SoundCloudClient::submitManualToken(const QString& token) {
    const QString value = token.trimmed();
    if (value.isEmpty()) {
        emit loginFinished(false, tr("The OAuth token is empty."));
        return;
    }
    m_oauthToken = value;
    m_pConfig->setValue(kOAuthTokenKey, m_oauthToken);
    emit sessionChanged(true);
    emit loginFinished(true, QString());
}

void SoundCloudClient::logout() {
    m_oauthToken.clear();
    m_pConfig->setValue(kOAuthTokenKey, m_oauthToken);
    emit sessionChanged(false);
}

QStringList SoundCloudClient::qualityLabels() const {
    // SoundCloud chooses the best stream for the account automatically, so no
    // user-selectable quality tier is offered.
    return {};
}

int SoundCloudClient::currentQualityIndex() const {
    return -1;
}

void SoundCloudClient::setQualityIndex(int index) {
    Q_UNUSED(index);
}

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

void SoundCloudClient::getApiJson(
        const QUrl& url,
        std::function<void(bool, const QByteArray&, const QString&)> callback) {
    QUrl withClient(url);
    QUrlQuery query(withClient);
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    withClient.setQuery(query);

    QNetworkRequest request(withClient);
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    request.setRawHeader("Accept", "application/json");
    if (!m_oauthToken.isEmpty()) {
        request.setRawHeader("Authorization",
                QByteArrayLiteral("OAuth ") + m_oauthToken.toUtf8());
    }
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
    refreshClientId(std::move(callback));
}

void SoundCloudClient::refreshClientId(mixxx::streaming::ResultCallback callback) {
    // Scrape the web app for a client_id. Try the __sc_hydration block first,
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
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
        q.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
        q.addQueryItem(QStringLiteral("linked_partitioning"), QStringLiteral("1"));
        QUrl url(kApiBase + QStringLiteral("search/tracks"));
        url.setQuery(q);
        getApiJson(url,
                [callback](bool ok, const QByteArray& data, const QString& error) {
                    if (!ok) {
                        callback(false, {}, error);
                        return;
                    }
                    const QJsonObject root =
                            QJsonDocument::fromJson(data).object();
                    const QJsonArray items =
                            root.value(QLatin1String("collection")).toArray();
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
        QUrl url(kApiBase + QStringLiteral("tracks/") + track.id);
        getApiJson(url,
                [this, track, callback](
                        bool ok, const QByteArray& data, const QString& error) {
                    if (!ok) {
                        callback(false, {}, error);
                        return;
                    }
                    const QJsonObject root =
                            QJsonDocument::fromJson(data).object();
                    // Try the original (lossless) file first when available,
                    // then fall back to the streamed transcodings.
                    auto fallback = [this, track, root, callback](
                                            bool ok,
                                            const QUrl& url,
                                            const QString& error) {
                        if (ok) {
                            callback(true, url, error);
                        } else {
                            resolveStreamUrl(track, root, callback);
                        }
                    };
                    tryOriginalDownload(track, root, fallback);
                });
    });
}

void SoundCloudClient::tryOriginalDownload(
        const mixxx::streaming::Track& track,
        const QJsonObject& trackObj,
        mixxx::streaming::StreamCallback callback) {
    const bool loggedIn = !m_oauthToken.isEmpty();
    const bool downloadable =
            trackObj.value(QLatin1String("downloadable")).toBool() &&
            trackObj.value(QLatin1String("has_downloads_left")).toBool();
    if (!loggedIn || !downloadable) {
        // Not eligible; hand control to the fallback without downloading.
        callback(false, {}, QString());
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), m_clientId);
    QUrl url(kApiBase + QStringLiteral("tracks/") + track.id +
            QStringLiteral("/download"));
    url.setQuery(q);
    getApiJson(url,
            [this, track, callback](bool ok,
                    const QByteArray& data,
                    const QString& error) {
                Q_UNUSED(error);
                if (!ok) {
                    callback(false, {}, QString());
                    return;
                }
                const QJsonObject root = QJsonDocument::fromJson(data).object();
                const QString redirect =
                        root.value(QLatin1String("redirectUri")).toString();
                if (redirect.isEmpty()) {
                    callback(false, {}, QString());
                    return;
                }
                QNetworkRequest request((QUrl(redirect)));
                request.setRawHeader("User-Agent", kUserAgent.toUtf8());
                if (!m_oauthToken.isEmpty()) {
                    request.setRawHeader("Authorization",
                            QByteArrayLiteral("OAuth ") + m_oauthToken.toUtf8());
                }
                // Follow the redirect manually so the Authorization header is
                // preserved across hosts.
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::ManualRedirectPolicy);
                QNetworkReply* reply = m_pNetwork->get(request);
                connect(reply, &QNetworkReply::finished, this,
                        [this, track, callback, reply]() {
                            reply->deleteLater();
                            const QVariant redirectAttr =
                                    reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
                            if (redirectAttr.isValid()) {
                                // Follow the signed URL without the token.
                                const QUrl location = reply->url().resolved(
                                        redirectAttr.toUrl());
                                QNetworkRequest finalReq(location);
                                finalReq.setRawHeader("User-Agent", kUserAgent.toUtf8());
                                QNetworkReply* finalReply = m_pNetwork->get(finalReq);
                                connect(finalReply, &QNetworkReply::finished, this,
                                        [this, track, callback, finalReply]() {
                                            finalReply->deleteLater();
                                            if (finalReply->error() !=
                                                    QNetworkReply::NoError) {
                                                callback(false, {}, QString());
                                                return;
                                            }
                                            const QString suffix =
                                                    m_oauthToken.isEmpty()
                                                    ? QStringLiteral("bin")
                                                    : QStringLiteral("orig");
                                            const QString filePath =
                                                    cacheFilePath(track, suffix);
                                            QFile file(filePath);
                                            if (!file.open(QIODevice::WriteOnly)) {
                                                callback(false, {}, QString());
                                                return;
                                            }
                                            file.write(finalReply->readAll());
                                            file.close();
                                            if (file.size() == 0) {
                                                callback(false, {}, QString());
                                                return;
                                            }
                                            callback(true,
                                                    QUrl::fromLocalFile(filePath),
                                                    QString());
                                        });
                                return;
                            }
                            if (reply->error() != QNetworkReply::NoError) {
                                callback(false, {}, QString());
                                return;
                            }
                            // Some responses are the file directly (200).
                            const QString filePath =
                                    cacheFilePath(track, QStringLiteral("orig"));
                            QFile file(filePath);
                            if (!file.open(QIODevice::WriteOnly)) {
                                callback(false, {}, QString());
                                return;
                            }
                            file.write(reply->readAll());
                            file.close();
                            if (file.size() == 0) {
                                callback(false, {}, QString());
                                return;
                            }
                            callback(true, QUrl::fromLocalFile(filePath), QString());
                        });
            });
}

void SoundCloudClient::resolveStreamUrl(
        const mixxx::streaming::Track& track,
        const QJsonObject& trackObj,
        mixxx::streaming::StreamCallback callback) {
    const QJsonArray transcodings = trackObj.value(QLatin1String("media"))
                                            .toObject()
                                            .value(QLatin1String("transcodings"))
                                            .toArray();
    const QList<QJsonObject> ranked = rankedTranscodings(transcodings);
    if (ranked.isEmpty()) {
        callback(false, {},
                tr("No playable (unencrypted) SoundCloud stream found. This "
                   "track may require a SoundCloud Go+ login."));
        return;
    }
    const QString trackAuthorization =
            trackObj.value(QLatin1String("track_authorization")).toString();

    auto index = std::make_shared<int>(0);
    auto tryNext = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakTryNext = tryNext;
    *tryNext = [this, track, ranked, trackAuthorization, callback, index, weakTryNext]() {
        if (*index >= ranked.size()) {
            callback(false, {},
                    tr("Could not download a playable SoundCloud stream."));
            return;
        }
        const QJsonObject transcoding = ranked.at((*index)++);
        const QString transcodingUrl =
                transcoding.value(QLatin1String("url")).toString();
        const QString protocol =
                transcoding.value(QLatin1String("format")).toObject()
                        .value(QLatin1String("protocol")).toString();

        QUrlQuery q;
        q.addQueryItem(QStringLiteral("client_id"), m_clientId);
        if (!trackAuthorization.isEmpty()) {
            q.addQueryItem(QStringLiteral("track_authorization"),
                    trackAuthorization);
        }
        QUrl url(transcodingUrl);
        url.setQuery(q);
        getApiJson(url,
                [this, track, protocol, callback, weakTryNext](
                        bool ok, const QByteArray& data, const QString& error) {
                    Q_UNUSED(error);
                    auto next = weakTryNext.lock();
                    if (!ok || !next) {
                        if (next) {
                            (*next)();
                        }
                        return;
                    }
                    const QJsonObject obj =
                            QJsonDocument::fromJson(data).object();
                    const QString signedUrl =
                            obj.value(QLatin1String("url")).toString();
                    if (signedUrl.isEmpty()) {
                        (*next)();
                        return;
                    }
                    auto onDownload = [callback, next](bool done,
                                              const QUrl& resultUrl,
                                              const QString& err) {
                        if (done) {
                            callback(true, resultUrl, err);
                        } else {
                            (*next)();
                        }
                    };
                    if (protocol == QLatin1String("progressive")) {
                        downloadProgressive(track,
                                QUrl(signedUrl),
                                QStringLiteral("mp3"),
                                onDownload);
                    } else {
                        downloadHls(track, QUrl(signedUrl), onDownload);
                    }
                });
    };
    (*tryNext)();
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
        const QString& suffix,
        mixxx::streaming::StreamCallback callback) {
    const QString filePath = cacheFilePath(track, suffix);
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
    connect(reply, &QNetworkReply::finished, this,
            [reply, filePath, callback = std::move(callback)]() {
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
                if (file.size() == 0) {
                    callback(false, {}, QStringLiteral("empty stream"));
                    return;
                }
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

bool SoundCloudClient::finalizeParts(
        const QList<QByteArray>& parts,
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
    return file.size() > 0;
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
                const QString filePath = cacheFilePath(
                        track, isMp4 ? QStringLiteral("m4a") : QStringLiteral("mp3"));
                QFile existing(filePath);
                if (existing.exists() && existing.size() > 0) {
                    callback(true, QUrl::fromLocalFile(filePath), QString());
                    return;
                }

                QList<QUrl> allUrls;
                if (initUrl.isValid()) {
                    allUrls.prepend(initUrl);
                }
                allUrls.append(segmentUrls);
                const int total = allUrls.size();

                // The pump downloads segments sequentially and keeps itself
                // alive through a strong reference held by each reply handler.
                // The body only holds a weak reference to avoid a self
                // reference cycle. Without the strong reference the chain dies
                // after the first segment.
                auto parts = std::make_shared<QList<QByteArray>>();
                auto index = std::make_shared<int>(0);
                auto pump = std::make_shared<std::function<void()>>();
                std::weak_ptr<std::function<void()>> weakPump = pump;
                *pump = [this, track, allUrls, parts, index, filePath, total,
                                callback, weakPump]() {
                    if (*index >= total) {
                        if (finalizeParts(*parts, filePath)) {
                            callback(true, QUrl::fromLocalFile(filePath), QString());
                        } else {
                            callback(false, {},
                                    tr("Failed to write SoundCloud stream file."));
                        }
                        return;
                    }
                    const int current = (*index)++;
                    std::shared_ptr<std::function<void()>> pumpRef = weakPump.lock();
                    QNetworkRequest request(allUrls.at(current));
                    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
                    QNetworkReply* segmentReply = m_pNetwork->get(request);
                    connect(segmentReply, &QNetworkReply::finished, this,
                            [this, segmentReply, parts, filePath, total, current,
                                    callback, pumpRef]() {
                                segmentReply->deleteLater();
                                if (segmentReply->error() !=
                                        QNetworkReply::NoError) {
                                    callback(false, {},
                                            tr("Failed to download SoundCloud "
                                               "stream segment: %1")
                                                    .arg(segmentReply->errorString()));
                                    return;
                                }
                                parts->append(segmentReply->readAll());
                                emit downloadProgress(current + 1, total);
                                if (pumpRef) {
                                    (*pumpRef)();
                                }
                            });
                };
                (*pump)();
            });
}

} // namespace soundcloud
} // namespace mixxx
