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

extern "C" {
#if defined(__FFMPEG__)
#include <libavformat/avformat.h>
#endif
} // extern "C"

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

/// Rewraps a FLAC stream from a fragmented MP4 container into a native FLAC
/// file. The audio frames are copied bit-exactly (lossless), only the
/// container changes. Mixxx decodes native FLAC with its optimized reference
/// decoder, whereas FLAC-in-MP4 falls back to the slower generic FFmpeg
/// decoder, which is not suitable for real-time playback with keylock.
///
/// Returns true on success. This is only attempted when FFmpeg is available.
bool remuxFlacToNative(const QString& inPath, const QString& outPath) {
#if defined(__FFMPEG__)
    AVFormatContext* pIn = nullptr;
    if (avformat_open_input(&pIn, inPath.toUtf8().constData(), nullptr, nullptr) < 0) {
        kLogger.warning() << "Failed to open for remux:" << inPath;
        return false;
    }
    const auto closeInput = [&pIn]() {
        if (pIn) {
            avformat_close_input(&pIn);
        }
    };
    if (avformat_find_stream_info(pIn, nullptr) < 0) {
        kLogger.warning() << "Failed to read stream info for remux:" << inPath;
        closeInput();
        return false;
    }
    const int audioStreamIndex =
            av_find_best_stream(pIn, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStreamIndex < 0) {
        kLogger.warning() << "No audio stream found for remux:" << inPath;
        closeInput();
        return false;
    }
    AVStream* pInStream = pIn->streams[audioStreamIndex];
    if (pInStream->codecpar->codec_id != AV_CODEC_ID_FLAC) {
        // Only FLAC streams can be rewrapped losslessly into a FLAC file.
        closeInput();
        return false;
    }

    AVFormatContext* pOut = nullptr;
    if (avformat_alloc_output_context2(
                &pOut, nullptr, "flac", outPath.toUtf8().constData()) < 0 ||
            !pOut) {
        kLogger.warning() << "Failed to allocate FLAC output context";
        closeInput();
        return false;
    }
    AVStream* pOutStream = avformat_new_stream(pOut, nullptr);
    if (!pOutStream ||
            avcodec_parameters_copy(pOutStream->codecpar, pInStream->codecpar) < 0) {
        kLogger.warning() << "Failed to copy stream parameters for remux";
        avformat_free_context(pOut);
        closeInput();
        return false;
    }
    pOutStream->codecpar->codec_tag = 0;
    pOutStream->time_base = pInStream->time_base;

    if (!(pOut->oformat->flags & AVFMT_NOFILE) &&
            avio_open(&pOut->pb, outPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
        kLogger.warning() << "Failed to open output for remux:" << outPath;
        avformat_free_context(pOut);
        closeInput();
        return false;
    }
    if (avformat_write_header(pOut, nullptr) < 0) {
        kLogger.warning() << "Failed to write FLAC header:" << outPath;
        if (pOut->pb) {
            avio_closep(&pOut->pb);
        }
        avformat_free_context(pOut);
        closeInput();
        return false;
    }

    bool success = true;
    AVPacket* pPacket = av_packet_alloc();
    while (av_read_frame(pIn, pPacket) >= 0) {
        if (pPacket->stream_index == audioStreamIndex) {
            pPacket->stream_index = pOutStream->index;
            av_packet_rescale_ts(pPacket, pInStream->time_base, pOutStream->time_base);
            if (av_interleaved_write_frame(pOut, pPacket) < 0) {
                kLogger.warning() << "Failed to write FLAC packet during remux";
                success = false;
                av_packet_unref(pPacket);
                break;
            }
        }
        av_packet_unref(pPacket);
    }
    av_packet_free(&pPacket);
    av_write_trailer(pOut);
    if (pOut->pb) {
        avio_closep(&pOut->pb);
    }
    avformat_free_context(pOut);
    closeInput();
    if (!success) {
        QFile::remove(outPath);
    }
    return success;
#else
    Q_UNUSED(inPath);
    Q_UNUSED(outPath);
    return false;
#endif
}

/// Derives the file suffix of the concatenated stream from the actual segment
/// URL and the codec reported by the manifest. TIDAL serves FLAC for lossless
/// tiers and MP4/AAC otherwise.
QString suffixForSegmentUrl(const QUrl& segmentUrl, const QString& codec = QString()) {
    const QString path = segmentUrl.path().toLower();
    if (path.endsWith(QLatin1String(".flac"))) {
        return QStringLiteral("flac");
    }
    if (path.endsWith(QLatin1String(".m4a")) ||
            path.endsWith(QLatin1String(".mp4")) ||
            path.endsWith(QLatin1String(".mp4a"))) {
        return QStringLiteral("m4a");
    }
    if (codec.contains(QLatin1String("flac"), Qt::CaseInsensitive)) {
        return QStringLiteral("flac");
    }
    // Default to the MP4 container which also covers AAC.
    return QStringLiteral("m4a");
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

mixxx::streaming::Track parseTrack(const QJsonObject& obj) {
    mixxx::streaming::Track track;
    track.providerId = QStringLiteral("tidal");
    track.id = QString::number(static_cast<qint64>(
            obj.value(QLatin1String("id")).toDouble()));
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
///
/// TIDAL MPDs contain multiple <Representation> entries (HE-AAC, AAC, FLAC).
/// The manifest picks the one with the highest quality among the codecs
/// allowed by the requested quality tier.
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

/// A single audio representation inside an MPD.
struct DashRepresentation {
    QString codec;
    int bandwidth = 0;
    int sampleRate = 44100;
    QString initUrl;
    QString mediaUrl;
    int startNumber = 1;
    int segmentCount = 0;

    bool isLossless() const {
        return codec.contains(QLatin1String("flac"), Qt::CaseInsensitive) ||
                codec.contains(QLatin1String("alac"), Qt::CaseInsensitive);
    }
};

QString decodeXmlEntities(QString value) {
    value.replace(QLatin1String("&amp;"), QLatin1String("&"));
    value.replace(QLatin1String("&lt;"), QLatin1String("<"));
    value.replace(QLatin1String("&gt;"), QLatin1String(">"));
    value.replace(QLatin1String("&quot;"), QLatin1String("\""));
    value.replace(QLatin1String("&apos;"), QLatin1String("'"));
    return value;
}

QList<DashRepresentation> parseRepresentations(const QByteArray& xml) {
    QList<DashRepresentation> representations;
    QXmlStreamReader reader(xml);
    DashRepresentation current;
    bool inRepresentation = false;
    bool inSegmentTemplate = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            if (name == QLatin1String("Representation")) {
                const auto attrs = reader.attributes();
                current = DashRepresentation();
                current.codec = attrs.value(QLatin1String("codecs")).toString();
                current.bandwidth = attrs.value(QLatin1String("bandwidth")).toInt();
                const int sampleRate =
                        attrs.value(QLatin1String("audioSamplingRate")).toInt();
                if (sampleRate > 0) {
                    current.sampleRate = sampleRate;
                }
                inRepresentation = true;
            } else if (inRepresentation && name == QLatin1String("SegmentTemplate")) {
                const auto attrs = reader.attributes();
                current.initUrl = decodeXmlEntities(
                        attrs.value(QLatin1String("initialization")).toString());
                current.mediaUrl = decodeXmlEntities(
                        attrs.value(QLatin1String("media")).toString());
                const QString startNumber =
                        attrs.value(QLatin1String("startNumber")).toString();
                if (!startNumber.isEmpty()) {
                    current.startNumber = startNumber.toInt();
                }
                inSegmentTemplate = true;
            } else if (inSegmentTemplate && name == QLatin1String("S")) {
                const auto attrs = reader.attributes();
                const QString repeat = attrs.value(QLatin1String("r")).toString();
                current.segmentCount += repeat.isEmpty() ? 1 : (repeat.toInt() + 1);
            }
        } else if (reader.isEndElement()) {
            const auto name = reader.name();
            if (name == QLatin1String("Representation")) {
                if (!current.initUrl.isEmpty() && !current.mediaUrl.isEmpty() &&
                        current.segmentCount > 0) {
                    representations.append(current);
                }
                inRepresentation = false;
                inSegmentTemplate = false;
            } else if (name == QLatin1String("SegmentTemplate")) {
                inSegmentTemplate = false;
            }
        }
    }
    if (reader.hasError()) {
        kLogger.warning() << "Failed to parse DASH representations:"
                          << reader.errorString();
        return {};
    }
    return representations;
}

/// Selects the best representation for the requested quality tier.
DashManifest selectDashManifest(
        const QList<DashRepresentation>& representations,
        Quality quality) {
    DashManifest manifest;
    if (representations.isEmpty()) {
        return manifest;
    }

    const DashRepresentation* selected = nullptr;
    auto pickHighestBandwidth = [&selected](const DashRepresentation* candidate) {
        if (!selected || candidate->bandwidth > selected->bandwidth) {
            selected = candidate;
        }
    };
    auto pickLowestBandwidth = [&selected](const DashRepresentation* candidate) {
        if (!selected || candidate->bandwidth < selected->bandwidth) {
            selected = candidate;
        }
    };

    for (const auto& representation : representations) {
        switch (quality) {
        case Quality::Lossless:
            // Only consider lossless representations; if there is none the
            // fallback below picks the best lossy one.
            if (representation.isLossless()) {
                pickHighestBandwidth(&representation);
            }
            break;
        case Quality::High:
        case Quality::Low:
            // Ignore lossless representations for the compressed tiers.
            if (!representation.isLossless()) {
                if (quality == Quality::High) {
                    pickHighestBandwidth(&representation);
                } else {
                    pickLowestBandwidth(&representation);
                }
            }
            break;
        }
    }
    // Fallback: pick the highest bandwidth representation of any codec.
    if (!selected) {
        for (const auto& representation : representations) {
            pickHighestBandwidth(&representation);
        }
    }

    if (!selected) {
        return manifest;
    }
    manifest.codec = selected->codec;
    manifest.sampleRate = selected->sampleRate;
    manifest.initializationTemplate = selected->initUrl;
    manifest.mediaTemplate = selected->mediaUrl;
    manifest.startNumber = selected->startNumber;
    manifest.segmentCount = selected->segmentCount;
    manifest.valid = true;
    return manifest;
}

DashManifest parseDashManifest(const QByteArray& xml, Quality quality) {
    return selectDashManifest(parseRepresentations(xml), quality);
}

} // anonymous namespace

TidalClient::TidalClient(UserSettingsPointer pConfig, QObject* parent)
        : mixxx::streaming::Provider(parent),
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

namespace {
// Display order of the quality tiers. The combo box index maps to this list.
const Quality kQualityOrder[] = {
        Quality::Lossless,
        Quality::High,
        Quality::Low,
};
constexpr int kQualityCount =
        static_cast<int>(sizeof(kQualityOrder) / sizeof(kQualityOrder[0]));
} // anonymous namespace

QStringList TidalClient::qualityLabels() const {
    return {QStringLiteral("Lossless (FLAC)"),
            QStringLiteral("High (AAC 320 kbps)"),
            QStringLiteral("Low (AAC 96 kbps)")};
}

int TidalClient::currentQualityIndex() const {
    for (int i = 0; i < kQualityCount; ++i) {
        if (kQualityOrder[i] == m_quality) {
            return i;
        }
    }
    return 0;
}

void TidalClient::setQualityIndex(int index) {
    if (index < 0 || index >= kQualityCount) {
        return;
    }
    setQuality(kQualityOrder[index]);
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
    const int quality = m_pConfig->getValue(kQualityKey, static_cast<int>(Quality::Lossless));
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

void TidalClient::startLogin() {
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
            emit loginFinished(false, reply->errorString());
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_deviceCode = obj.value(QLatin1String("deviceCode")).toString();
        m_deviceUserCode = obj.value(QLatin1String("userCode")).toString();
        m_deviceExpiresIn = obj.value(QLatin1String("expiresIn")).toInt();
        m_deviceInterval = qMax(1, obj.value(QLatin1String("interval")).toInt());
        m_devicePollAttempts = 0;
        m_deviceLoginRunning = true;

        emit loginStarted(m_deviceUserCode,
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
        emit loginFinished(false, tr("The login code expired."));
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
            emit loginFinished(false, tr("The login code expired."));
        } else {
            emit loginFinished(false,
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
            emit loginFinished(false,
                    tr("Logged in but could not load the TIDAL session: %1")
                            .arg(error));
            return;
        }
        emit sessionChanged(true);
        emit loginFinished(true, QString());
    });
}

void TidalClient::fetchSessionInfo(mixxx::streaming::ResultCallback callback) {
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

void TidalClient::getAbsolute(
        const QUrl& url,
        std::function<void(bool, const QByteArray&, const QString&)> callback) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kAndroidUserAgent.toUtf8());
    // TIDAL's v2 manifest endpoint responds with 404 if a specific JSON Accept
    // header is sent; it must be */* (this matches the reference clients).
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("X-Platform", "android");
    request.setRawHeader("X-Tidal-Platform", "android");
    const QByteArray auth = authHeader();
    if (!auth.isEmpty()) {
        request.setRawHeader("authorization", auth);
    }
    QNetworkReply* reply = m_pNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, url, callback = std::move(callback), reply]() {
                reply->deleteLater();
                const QByteArray data = reply->readAll();
                if (reply->error() != QNetworkReply::NoError) {
                    // A stale token may yield 401; refresh once and retry.
                    const QJsonObject obj = QJsonDocument::fromJson(data).object();
                    const QString message = obj.value(QLatin1String("userMessage"))
                                                    .toString(reply->errorString());
                    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                                        .toInt() == 401 &&
                            !m_refreshToken.isEmpty()) {
                        refreshAccessToken([this, url, callback = std::move(callback)](
                                                   bool ok) {
                            if (!ok) {
                                callback(false, {},
                                        tr("Session expired, please log in again."));
                                return;
                            }
                            getAbsolute(url, std::move(callback));
                        });
                        return;
                    }
                    callback(false, data, message);
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

void TidalClient::search(
        const QString& query,
        mixxx::streaming::SearchCallback callback) {
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
                mixxx::streaming::TrackList tracks;
                tracks.reserve(items.size());
                for (const auto& item : items) {
                    tracks.append(parseTrack(item.toObject()));
                }
                callback(true, tracks, QString());
            });
}

void TidalClient::downloadTrack(
        const mixxx::streaming::Track& track,
        mixxx::streaming::StreamCallback callback) {
    const QString trackId = track.id;

    // The v1 playbackinfopostpaywall endpoint is capped to AAC (HIGH) for our
    // OAuth client, so request the manifest from the v2 endpoint first. It
    // returns an MPEG-DASH MPD that contains a FLAC representation for
    // lossless tiers.
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("adaptive"), QStringLiteral("true"));
    params.addQueryItem(QStringLiteral("manifestType"), QStringLiteral("MPEG_DASH"));
    params.addQueryItem(QStringLiteral("uriScheme"), QStringLiteral("HTTPS"));
    params.addQueryItem(QStringLiteral("usage"), QStringLiteral("PLAYBACK"));
    for (const auto& format : {QStringLiteral("HEAACV1"),
                 QStringLiteral("AACLC"),
                 QStringLiteral("FLAC"),
                 QStringLiteral("FLAC_HIRES")}) {
        params.addQueryItem(QStringLiteral("formats"), format);
    }
    // The v2 manifest endpoint requires both sessionId and countryCode, just
    // like the v1 API; without them it responds with 404.
    if (!m_sessionId.isEmpty()) {
        params.addQueryItem(QStringLiteral("sessionId"), m_sessionId);
    }
    if (!m_countryCode.isEmpty()) {
        params.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    }

    QUrl manifestUrl(kManifestBaseUrl + trackId);
    manifestUrl.setQuery(params);

    getAbsolute(
            manifestUrl,
            [this, track, callback = std::move(callback)](
                    bool ok, const QByteArray& data, const QString& error) {
                if (!ok) {
                    callback(false, {}, error);
                    return;
                }
                const QJsonObject root = QJsonDocument::fromJson(data).object();
                const QJsonObject attributes = root.value(QLatin1String("data"))
                                                       .toObject()
                                                       .value(QLatin1String("attributes"))
                                                       .toObject();
                const QString mpdUrl =
                        attributes.value(QLatin1String("uri")).toString();
                if (mpdUrl.isEmpty()) {
                    callback(false, {},
                            tr("The TIDAL manifest response did not contain a "
                               "manifest URL."));
                    return;
                }

                getAbsolute(
                        QUrl(mpdUrl),
                        [this, track, callback = std::move(callback)](
                                bool mpdOk,
                                const QByteArray& mpd,
                                const QString& mpdError) {
                            if (!mpdOk) {
                                callback(false, {}, mpdError);
                                return;
                            }
                            handleDashManifest(track, mpd, std::move(callback));
                        });
            });
}

void TidalClient::handleDashManifest(
        const mixxx::streaming::Track& track,
        const QByteArray& mpd,
        mixxx::streaming::StreamCallback callback) {
    const DashManifest dash = parseDashManifest(mpd, m_quality);
    if (!dash.valid) {
        callback(false, {}, tr("Unsupported or empty DASH manifest."));
        return;
    }
    const QList<QUrl> segmentUrls = dash.segmentUrls();
    // The segments are always a fragmented MP4 container (even for FLAC), so
    // the .m4a suffix lets Mixxx pick the FFmpeg decoder.
    const QString suffix = suffixForSegmentUrl(
            segmentUrls.isEmpty() ? QUrl() : segmentUrls.first(), dash.codec);
    downloadSegments(track, segmentUrls, suffix, std::move(callback));
}

QString TidalClient::cacheFilePath(
        const mixxx::streaming::Track& track,
        const QString& suffix) const {
    QDir cacheDir(QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation));
    cacheDir.mkpath(QStringLiteral("tidal"));
    cacheDir.cd(QStringLiteral("tidal"));
    // Include the quality tier so that switching between lossless and the
    // compressed tiers does not silently reuse a file of a different quality.
    return cacheDir.filePath(QStringLiteral("%1_%2.%3")
                                     .arg(track.id)
                                     .arg(qualityToString(m_quality).toLower())
                                     .arg(suffix));
}

void TidalClient::downloadSegments(
        const mixxx::streaming::Track& track,
        const QList<QUrl>& segmentUrls,
        const QString& fileSuffix,
        mixxx::streaming::StreamCallback callback) {
    const QString filePath = cacheFilePath(track, fileSuffix);
    // Lossless streams are FLAC wrapped in a fragmented MP4 container. After
    // downloading, the FLAC frames are rewrapped into a native FLAC file so
    // Mixxx can use its optimized decoder.
    const QString nativeFlacPath = cacheFilePath(track, QStringLiteral("flac"));
    const bool mayRemuxToFlac = (fileSuffix == QLatin1String("m4a"));

    // Reuse an already downloaded (and possibly already remuxed) file.
    if (mayRemuxToFlac) {
        QFile remuxed(nativeFlacPath);
        if (remuxed.exists() && remuxed.size() > 0) {
            callback(true, QUrl::fromLocalFile(nativeFlacPath), QString());
            return;
        }
    }
    QFile existing(filePath);
    if (existing.exists() && existing.size() > 0) {
        if (mayRemuxToFlac && remuxFlacToNative(filePath, nativeFlacPath)) {
            callback(true, QUrl::fromLocalFile(nativeFlacPath), QString());
        } else {
            callback(true, QUrl::fromLocalFile(filePath), QString());
        }
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

    // The pump keeps up to kMaxParallelDownloads requests in flight. It is
    // invoked once initially and again from every completed reply. Each reply
    // handler holds a strong reference to it so it stays alive for the whole
    // download; the pump body itself only holds a weak reference to avoid a
    // self-reference cycle.
    auto pump = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakPump = pump;

    auto reportProgress = [this, total = segmentUrls.size()](int completed) {
        emit downloadProgress(completed, total);
    };

    *pump = [this,
                    segmentUrls,
                    filePath,
                    nativeFlacPath,
                    mayRemuxToFlac,
                    state,
                    callback,
                    reportProgress,
                    weakPump]() {
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
            // A strong reference is obtained here, while a strong reference to
            // the pump is still held by the caller, so lock() cannot fail.
            std::shared_ptr<std::function<void()>> pumpRef = weakPump.lock();
            QNetworkReply* reply = m_pNetwork->get(request);
            connect(reply, &QNetworkReply::finished, this,
                    [this,
                            reply,
                            index,
                            segmentUrls,
                            filePath,
                            nativeFlacPath,
                            mayRemuxToFlac,
                            state,
                            callback,
                            reportProgress,
                            pumpRef]() {
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
                            // Rewrap FLAC-in-MP4 into a native FLAC file for
                            // efficient decoding. Falls back to the MP4 file
                            // for lossy (AAC) streams.
                            if (mayRemuxToFlac &&
                                    remuxFlacToNative(filePath, nativeFlacPath)) {
                                callback(true,
                                        QUrl::fromLocalFile(nativeFlacPath),
                                        QString());
                                return;
                            }
                            callback(true, QUrl::fromLocalFile(filePath), QString());
                            return;
                        }
                        if (pumpRef) {
                            (*pumpRef)();
                        }
                    });
        }
    };
    (*pump)();
}

} // namespace tidal
} // namespace mixxx
