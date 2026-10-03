#include "library/tidal/tidalclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QXmlStreamReader>

#include "moc_tidalclient.cpp"
#include "util/logger.h"

namespace mixxx {
namespace tidal {

namespace {

const Logger kLogger("TidalClient");

const ConfigKey kAccessTokenKey = ConfigKey("[Tidal]", "AccessToken");
const ConfigKey kRefreshTokenKey = ConfigKey("[Tidal]", "RefreshToken");
const ConfigKey kTokenTypeKey = ConfigKey("[Tidal]", "TokenType");
const ConfigKey kSessionIdKey = ConfigKey("[Tidal]", "SessionId");
const ConfigKey kCountryCodeKey = ConfigKey("[Tidal]", "CountryCode");
const ConfigKey kUserIdKey = ConfigKey("[Tidal]", "UserId");
const ConfigKey kQualityKey = ConfigKey("[Tidal]", "Quality");

QUrl apiUrl(const QString& path) {
    return QUrl(kApiBaseUrl + path);
}

QString jsonString(const QJsonObject& obj, const char* key) {
    return obj.value(QLatin1String(key)).toString();
}

QString parseYear(const QJsonObject& obj) {
    const QString releaseDate = jsonString(obj, "releaseDate");
    if (releaseDate.size() >= 4) {
        return releaseDate.left(4);
    }
    if (obj.contains(QLatin1String("year"))) {
        return QString::number(obj.value(QLatin1String("year")).toInt());
    }
    return QString();
}

TidalTrack parseTrack(const QJsonObject& obj) {
    TidalTrack track;
    track.id = static_cast<qint64>(obj.value(QLatin1String("id")).toDouble());
    track.title = jsonString(obj, "title");
    track.isrc = jsonString(obj, "isrc");
    track.audioQuality = jsonString(obj, "audioQuality");
    track.durationSec = obj.value(QLatin1String("duration")).toInt();
    track.trackNumber = obj.value(QLatin1String("trackNumber")).toInt();
    track.isExplicit = obj.value(QLatin1String("explicit")).toBool();

    const QJsonArray artists = obj.value(QLatin1String("artists")).toArray();
    QStringList artistNames;
    for (const auto& artistValue : artists) {
        const QString name = artistValue.toObject().value(QLatin1String("name")).toString();
        if (!name.isEmpty()) {
            artistNames.append(name);
        }
    }
    if (artistNames.isEmpty()) {
        artistNames.append(
                obj.value(QLatin1String("artist")).toObject().value(
                        QLatin1String("name")).toString());
    }
    track.artist = artistNames.join(QStringLiteral(", "));

    const QJsonObject albumObj = obj.value(QLatin1String("album")).toObject();
    track.album = albumObj.value(QLatin1String("title")).toString();
    track.year = parseYear(albumObj);
    if (track.year.isEmpty()) {
        track.year = parseYear(obj);
    }
    return track;
}

/// Extracts SegmentTemplate information from an MPD manifest.
struct DashManifest {
    QString mediaTemplate;
    QString initializationTemplate;
    QString codec;
    QString mimeType;
    int startNumber = 1;
    int segmentCount = 0;
    int sampleRate = 44100;
    bool valid = false;

    QList<QUrl> segmentUrls() const {
        QList<QUrl> urls;
        if (initializationTemplate.isEmpty() || mediaTemplate.isEmpty()) {
            return urls;
        }
        urls.append(QUrl(initializationTemplate));
        for (int i = startNumber; i < startNumber + segmentCount; ++i) {
            QString url = mediaTemplate;
            url.replace(QStringLiteral("$Number$"), QString::number(i));
            urls.append(QUrl(url));
        }
        return urls;
    }
};

DashManifest parseDashManifest(const QByteArray& xml) {
    DashManifest manifest;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }
        const auto name = reader.name();
        if (name == QLatin1String("Representation")) {
            const auto attrs = reader.attributes();
            if (manifest.codec.isEmpty()) {
                manifest.codec = attrs.value(QLatin1String("codecs")).toString();
                manifest.sampleRate = attrs.value(QLatin1String("audioSamplingRate")).toInt();
            }
        } else if (name == QLatin1String("SegmentTemplate")) {
            const auto attrs = reader.attributes();
            manifest.mediaTemplate = attrs.value(QLatin1String("media")).toString();
            manifest.initializationTemplate =
                    attrs.value(QLatin1String("initialization")).toString();
            const QString startNumber = attrs.value(QLatin1String("startNumber")).toString();
            if (!startNumber.isEmpty()) {
                manifest.startNumber = startNumber.toInt();
            }
        } else if (name == QLatin1String("S")) {
            const auto attrs = reader.attributes();
            const QString repeat = attrs.value(QLatin1String("r")).toString();
            manifest.segmentCount += repeat.isEmpty() ? 1 : (repeat.toInt() + 1);
        }
    }
    if (reader.hasError()) {
        kLogger.warning() << "Failed to parse DASH manifest:" << reader.errorString();
        return manifest;
    }
    if (manifest.mimeType.isEmpty()) {
        // MIME type has no effect on the raw segment data, but is used to
        // choose a file suffix for the concatenated file.
        manifest.mimeType = QStringLiteral("audio/mp4");
    }
    manifest.valid = !manifest.mediaTemplate.isEmpty() &&
            !manifest.initializationTemplate.isEmpty() && manifest.segmentCount > 0;
    return manifest;
}

} // anonymous namespace

TidalClient::TidalClient(UserSettingsPointer pConfig, QObject* parent)
        : QObject(parent),
          m_pConfig(std::move(pConfig)),
          m_pNetwork(new QNetworkAccessManager(this)) {
    loadSession();
}

TidalClient::~TidalClient() = default;

bool TidalClient::hasSession() const {
    return !m_accessToken.isEmpty();
}

void TidalClient::setQuality(Quality quality) {
    if (m_quality == quality) {
        return;
    }
    m_quality = quality;
    m_pConfig->setValue(kQualityKey, static_cast<int>(quality));
    saveSession();
}

QByteArray TidalClient::authHeader() const {
    if (m_tokenType.isEmpty() || m_accessToken.isEmpty()) {
        return {};
    }
    return (m_tokenType + QLatin1Char(' ') + m_accessToken).toUtf8();
}

void TidalClient::loadSession() {
    m_accessToken = m_pConfig->getValueString(kAccessTokenKey);
    m_refreshToken = m_pConfig->getValueString(kRefreshTokenKey);
    m_tokenType = m_pConfig->getValueString(kTokenTypeKey);
    m_sessionId = m_pConfig->getValueString(kSessionIdKey);
    m_countryCode = m_pConfig->getValueString(kCountryCodeKey);
    m_userId = m_pConfig->getValueString(kUserIdKey);
    const int quality = m_pConfig->getValue(kQualityKey, static_cast<int>(Quality::High));
    m_quality = static_cast<Quality>(quality);
}

void TidalClient::saveSession() {
    m_pConfig->setValue(kAccessTokenKey, m_accessToken);
    m_pConfig->setValue(kRefreshTokenKey, m_refreshToken);
    m_pConfig->setValue(kTokenTypeKey, m_tokenType);
    m_pConfig->setValue(kSessionIdKey, m_sessionId);
    m_pConfig->setValue(kCountryCodeKey, m_countryCode);
    m_pConfig->setValue(kUserIdKey, m_userId);
    m_pConfig->setValue(kQualityKey, static_cast<int>(m_quality));
}

void TidalClient::logout() {
    cancelLogin();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_tokenType.clear();
    m_sessionId.clear();
    m_countryCode.clear();
    m_userId.clear();
    saveSession();
    emit sessionChanged(false);
}

void TidalClient::startDeviceLogin() {
    if (m_deviceLoginRunning) {
        return;
    }
    QNetworkRequest request(QUrl(kAuthBaseUrl + QStringLiteral("device_authorization")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), kClientId);
    form.addQueryItem(QStringLiteral("scope"), kScope);

    QNetworkReply* reply = m_pNetwork->post(request, form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit deviceLoginFinished(false, reply->errorString());
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_deviceCode = obj.value(QLatin1String("deviceCode")).toString();
        m_deviceUserCode = obj.value(QLatin1String("userCode")).toString();
        m_deviceExpiresIn = obj.value(QLatin1String("expiresIn")).toInt();
        m_deviceInterval = qMax(1, obj.value(QLatin1String("interval")).toInt());
        m_devicePollAttempts = 0;
        m_deviceLoginRunning = true;

        emit deviceLoginStarted(m_deviceUserCode,
                obj.value(QLatin1String("verificationUriComplete")).toString(),
                m_deviceExpiresIn);
        pollDeviceToken();
    });
}

void TidalClient::cancelLogin() {
    m_deviceLoginRunning = false;
}

void TidalClient::pollDeviceToken() {
    if (!m_deviceLoginRunning) {
        return;
    }
    if (m_devicePollAttempts * m_deviceInterval > m_deviceExpiresIn) {
        m_deviceLoginRunning = false;
        emit deviceLoginFinished(false, tr("The login code expired."));
        return;
    }
    ++m_devicePollAttempts;

    QNetworkRequest request(QUrl(kAuthBaseUrl + QStringLiteral("token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), kClientId);
    form.addQueryItem(QStringLiteral("client_secret"), kClientSecret);
    form.addQueryItem(QStringLiteral("device_code"), m_deviceCode);
    form.addQueryItem(QStringLiteral("grant_type"),
            QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    form.addQueryItem(QStringLiteral("scope"), kScope);

    QNetworkReply* reply = m_pNetwork->post(request, form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            m_deviceLoginRunning = false;
            applyAuthToken(data);
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        const QString error = obj.value(QLatin1String("error")).toString();
        if (error == QLatin1String("authorization_pending") ||
                error == QLatin1String("slow_down")) {
            QTimer::singleShot(m_deviceInterval * 1000, this, &TidalClient::pollDeviceToken);
            return;
        }
        m_deviceLoginRunning = false;
        if (error == QLatin1String("expired_token")) {
            emit deviceLoginFinished(false, tr("The login code expired."));
        } else {
            emit deviceLoginFinished(false,
                    obj.value(QLatin1String("error_description")).toString(error));
        }
    });
}

void TidalClient::applyAuthToken(const QByteArray& json) {
    const QJsonObject obj = QJsonDocument::fromJson(json).object();
    m_accessToken = obj.value(QLatin1String("access_token")).toString();
    m_refreshToken = obj.value(QLatin1String("refresh_token")).toString();
    m_tokenType = obj.value(QLatin1String("token_type")).toString();
    saveSession();

    // A valid session returns a session id, country code and user id which are
    // required for all subsequent API requests.
    fetchSessionInfo([this](bool ok, const QString& error) {
        if (!ok) {
            kLogger.warning() << "Failed to fetch TIDAL session info:" << error;
            emit sessionChanged(false);
            emit deviceLoginFinished(false,
                    tr("Logged in but could not load the TIDAL session: %1")
                            .arg(error));
            return;
        }
        emit sessionChanged(true);
        emit deviceLoginFinished(true, QString());
    });
}

void TidalClient::fetchSessionInfo(TidalResultCallback callback) {
    sendRequest(
            QStringLiteral("sessions"),
            QUrlQuery(),
            [this, callback](bool ok, const QByteArray& data, const QString& error) {
                if (!ok) {
                    callback(false, error);
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(data).object();
                m_sessionId = obj.value(QLatin1String("sessionId")).toString();
                m_countryCode = obj.value(QLatin1String("countryCode")).toString();
                m_userId = QString::number(static_cast<qint64>(
                        obj.value(QLatin1String("userId")).toDouble()));
                saveSession();
                callback(true, QString());
            });
}

void TidalClient::refreshAccessToken(std::function<void(bool)> callback) {
    if (m_refreshToken.isEmpty()) {
        callback(false);
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    form.addQueryItem(QStringLiteral("client_id"), kClientId);
    form.addQueryItem(QStringLiteral("client_secret"), kClientSecret);
    postForm(
            kAuthBaseUrl + QStringLiteral("token"),
            form,
            [this, callback = std::move(callback)](
                    bool ok, const QByteArray& data, const QString& error) {
                if (!ok) {
                    kLogger.warning() << "Failed to refresh TIDAL token:" << error;
                    callback(false);
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(data).object();
                m_accessToken = obj.value(QLatin1String("access_token")).toString();
                m_tokenType = obj.value(QLatin1String("token_type")).toString();
                saveSession();
                callback(true);
            });
}

void TidalClient::postForm(
        const QString& absoluteUrl,
        const QUrlQuery& query,
        std::function<void(bool, const QByteArray&, const QString&)> callback) {
    QNetworkRequest request{QUrl(absoluteUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    QNetworkReply* reply = m_pNetwork->post(
            request, query.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject obj = QJsonDocument::fromJson(data).object();
            const QString error = obj.value(QLatin1String("error_description")).toString(
                    reply->errorString());
            callback(false, data, error);
            return;
        }
        callback(true, data, QString());
    });
}

void TidalClient::sendRequest(
        const QString& path,
        const QUrlQuery& query,
        std::function<void(bool, const QByteArray&, const QString&)> callback) {
    QUrlQuery fullQuery = query;
    if (!m_sessionId.isEmpty()) {
        fullQuery.addQueryItem(QStringLiteral("sessionId"), m_sessionId);
    }
    if (!m_countryCode.isEmpty()) {
        fullQuery.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    }

    QUrl url = apiUrl(path);
    url.setQuery(fullQuery);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    request.setRawHeader("x-tidal-client-version", kClientVersion.toUtf8());
    const QByteArray auth = authHeader();
    if (!auth.isEmpty()) {
        request.setRawHeader("authorization", auth);
    }

    QNetworkReply* reply = m_pNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, path, query, callback = std::move(callback), reply]() {
                reply->deleteLater();
                const QByteArray data = reply->readAll();

                // Detect an expired token and retry once after refreshing it.
                const QJsonObject errorObj = QJsonDocument::fromJson(data).object();
                const bool tokenExpired = errorObj.value(QLatin1String("userMessage"))
                                                  .toString()
                                                  .startsWith(QLatin1String(
                                                          "The token has expired."));
                if (tokenExpired && !m_refreshToken.isEmpty()) {
                    refreshAccessToken([this, path, query, callback = std::move(callback)](
                                               bool ok) {
                        if (!ok) {
                            callback(false, {}, tr("Session expired, please log in again."));
                            return;
                        }
                        sendRequest(path, query, std::move(callback));
                    });
                    return;
                }

                if (reply->error() != QNetworkReply::NoError) {
                    const QString error = errorObj.value(QLatin1String("userMessage"))
                                                  .toString(reply->errorString());
                    callback(false, data, error);
                    return;
                }
                callback(true, data, QString());
            });
}

void TidalClient::search(const QString& query, TidalSearchCallback callback) {
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("query"), query);
    params.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    params.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    params.addQueryItem(QStringLiteral("types"), QStringLiteral("tracks"));

    QUrl url = apiUrl(QStringLiteral("search"));
    QUrlQuery fullQuery = params;
    if (!m_sessionId.isEmpty()) {
        fullQuery.addQueryItem(QStringLiteral("sessionId"), m_sessionId);
    }
    if (!m_countryCode.isEmpty()) {
        fullQuery.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    }

    QUrl urlWithQuery = url;
    urlWithQuery.setQuery(fullQuery);
    QNetworkRequest request(urlWithQuery);
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    request.setRawHeader("x-tidal-client-version", kClientVersion.toUtf8());
    const QByteArray auth = authHeader();
    if (!auth.isEmpty()) {
        request.setRawHeader("authorization", auth);
    }

    QNetworkReply* reply = m_pNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [reply, callback = std::move(callback)]() {
                reply->deleteLater();
                const QByteArray data = reply->readAll();
                if (reply->error() != QNetworkReply::NoError) {
                    const QJsonObject obj = QJsonDocument::fromJson(data).object();
                    callback(false, {},
                            obj.value(QLatin1String("userMessage")).toString(
                                    reply->errorString()));
                    return;
                }
                const QJsonObject root = QJsonDocument::fromJson(data).object();
                const QJsonArray items = root.value(QLatin1String("tracks"))
                                                 .toObject()
                                                 .value(QLatin1String("items"))
                                                 .toArray();
                QList<TidalTrack> tracks;
                tracks.reserve(items.size());
                for (const auto& item : items) {
                    tracks.append(parseTrack(item.toObject()));
                }
                callback(true, tracks, QString());
            });
}

void TidalClient::resolveStream(
        const TidalTrack& track,
        TidalStreamCallback callback) {
    downloadTrack(track, std::move(callback));
}

void TidalClient::downloadTrack(
        const TidalTrack& track,
        TidalStreamCallback callback) {
    const QString trackId = QString::number(track.id);

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("playbackmode"), QStringLiteral("STREAM"));
    params.addQueryItem(QStringLiteral("audioquality"), qualityToString(m_quality));
    params.addQueryItem(QStringLiteral("assetpresentation"), QStringLiteral("FULL"));

    sendRequest(
            QStringLiteral("tracks/%1/playbackinfopostpaywall").arg(trackId),
            params,
            [this, track, callback = std::move(callback)](
                    bool ok, const QByteArray& data, const QString& error) {
                if (!ok) {
                    callback(false, {}, error);
                    return;
                }
                const QJsonObject obj = QJsonDocument::fromJson(data).object();
                const QString manifestMime = jsonString(obj, "manifestMimeType");
                const QByteArray manifestB64 =
                        obj.value(QLatin1String("manifest")).toString().toUtf8();
                const QByteArray manifest = QByteArray::fromBase64(manifestB64);

                QList<QUrl> segmentUrls;
                QString suffix;
                if (manifestMime.contains(QLatin1String("dash+xml"))) {
                    const DashManifest dash = parseDashManifest(manifest);
                    if (!dash.valid) {
                        callback(false, {}, tr("Unsupported or empty DASH manifest."));
                        return;
                    }
                    segmentUrls = dash.segmentUrls();
                    // DASH segments are always a fragmented MP4 container,
                    // even when the contained codec is FLAC. The .m4a suffix
                    // lets Mixxx pick a suitable decoder (FAAD/FFmpeg).
                    suffix = QStringLiteral("m4a");
                } else if (manifestMime.contains(QLatin1String("vnd.tidal.bts"))) {
                    const QJsonObject bts = QJsonDocument::fromJson(manifest).object();
                    const QJsonArray urls = bts.value(QLatin1String("urls")).toArray();
                    const QString codecs = jsonString(bts, "codecs").toLower();
                    for (const auto& urlValue : urls) {
                        segmentUrls.append(QUrl(urlValue.toString()));
                    }
                    suffix = codecs.contains(QLatin1String("flac"))
                            ? QStringLiteral("flac")
                            : QStringLiteral("m4a");
                } else {
                    callback(false, {},
                            tr("Unsupported manifest type: %1").arg(manifestMime));
                    return;
                }

                downloadSegments(track, segmentUrls, suffix, std::move(callback));
            });
}

QString TidalClient::cacheFilePath(
        const TidalTrack& track,
        const QString& suffix) const {
    QDir cacheDir(QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation));
    cacheDir.mkpath(QStringLiteral("tidal"));
    cacheDir.cd(QStringLiteral("tidal"));
    return cacheDir.filePath(
            QStringLiteral("%1.%2").arg(track.id).arg(suffix));
}

void TidalClient::downloadSegments(
        const TidalTrack& track,
        const QList<QUrl>& segmentUrls,
        const QString& fileSuffix,
        TidalStreamCallback callback) {
    const QString filePath = cacheFilePath(track, fileSuffix);

    // Reuse an already downloaded file (offline cache).
    QFile existing(filePath);
    if (existing.exists() && existing.size() > 0) {
        callback(true, QUrl::fromLocalFile(filePath), QString());
        return;
    }

    if (segmentUrls.isEmpty()) {
        callback(false, {}, tr("The stream manifest did not contain any segments."));
        return;
    }

    // Download all DASH segments concurrently. Segments are small and
    // independent, so downloading them in parallel turns a track load from
    // several seconds of sequential round-trips into a fraction of a second.
    constexpr int kMaxParallelDownloads = 8;

    struct DownloadState {
        QList<QByteArray> results;
        int nextIndex = 0;
        int inFlight = 0;
        int completed = 0;
        bool finished = false;
        QString error;
    };
    auto state = std::make_shared<DownloadState>();
    state->results.resize(segmentUrls.size());

    auto startNext = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakStartNext = startNext;

    auto reportProgress = [this, total = segmentUrls.size()](int completed) {
        emit downloadProgress(completed, total);
    };

    *startNext = [this,
                         segmentUrls,
                         filePath,
                         state,
                         callback,
                         weakStartNext,
                         reportProgress]() {
        if (state->finished) {
            return;
        }
        // Keep the pipe full with up to kMaxParallelDownloads requests.
        while (state->inFlight < kMaxParallelDownloads &&
                state->nextIndex < segmentUrls.size()) {
            const int index = state->nextIndex++;
            ++state->inFlight;

            const QUrl segmentUrl = segmentUrls.at(index);
            QNetworkRequest request(segmentUrl);
            request.setRawHeader("User-Agent", kUserAgent.toUtf8());
            const QByteArray auth = authHeader();
            if (!auth.isEmpty()) {
                request.setRawHeader("authorization", auth);
            }
            QNetworkReply* reply = m_pNetwork->get(request);
            connect(reply, &QNetworkReply::finished, this,
                    [this,
                            reply,
                            index,
                            segmentUrls,
                            filePath,
                            state,
                            callback,
                            weakStartNext,
                            reportProgress]() {
                        reply->deleteLater();
                        if (state->finished) {
                            return;
                        }
                        if (reply->error() != QNetworkReply::NoError) {
                            state->finished = true;
                            callback(false, {},
                                    tr("Failed to download stream segment: %1")
                                            .arg(reply->errorString()));
                            return;
                        }
                        state->results[index] = reply->readAll();
                        --state->inFlight;
                        ++state->completed;
                        reportProgress(state->completed);

                        if (state->completed == segmentUrls.size()) {
                            state->finished = true;
                            QFile file(filePath);
                            if (!file.open(QIODevice::WriteOnly)) {
                                callback(false, {},
                                        tr("Failed to write stream cache file: %1")
                                                .arg(file.errorString()));
                                return;
                            }
                            for (const QByteArray& segment : state->results) {
                                file.write(segment);
                            }
                            file.close();
                            callback(true, QUrl::fromLocalFile(filePath), QString());
                            return;
                        }
                        if (auto startNext = weakStartNext.lock()) {
                            (*startNext)();
                        }
                    });
        }
    };
    (*startNext)();
}

} // namespace tidal
} // namespace mixxx
