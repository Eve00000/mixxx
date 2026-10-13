#include "library/deezer/deezerclient.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrlQuery>
#include <memory>

#include "library/deezer/deezerdefs.h"
#include "moc_deezerclient.cpp"
#include "util/blowfish.h"
#include "util/logger.h"

namespace mixxx {
namespace deezer {

namespace {

const Logger kLogger("DeezerClient");

const ConfigKey kArlKey = ConfigKey("[Deezer]", "Arl");
const ConfigKey kApiTokenKey = ConfigKey("[Deezer]", "ApiToken");
const ConfigKey kLicenseTokenKey = ConfigKey("[Deezer]", "LicenseToken");
const ConfigKey kUserIdKey = ConfigKey("[Deezer]", "UserId");
const ConfigKey kQualityKey = ConfigKey("[Deezer]", "Quality");

const QString kGwUrl = QStringLiteral("https://www.deezer.com/ajax/gw-light.php");
const QString kLicenseUrl = QStringLiteral("https://media.deezer.com/v1/get_url");

// 2048-byte cipher blocks are the unit Deezer's "BF_CBC_STRIPE" scheme
// encrypts; only the first 2048 bytes of every 6144-byte stripe are encrypted.
constexpr int kStripeSize = 2048;
constexpr int kStripeBlock = 3 * kStripeSize;

const uint8_t kDeezerIv[8] = {0, 1, 2, 3, 4, 5, 6, 7};

/// Renders a JSON value as the textual form the gw-light query string expects.
QString jsonParamToString(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d))) {
            return QString::number(static_cast<qint64>(d));
        }
        return QString::number(d);
    }
    if (value.isArray()) {
        return QString::fromUtf8(
                QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (value.isObject()) {
        return QString::fromUtf8(
                QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

QString trackIdToString(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    return QString::number(static_cast<qint64>(value.toDouble()));
}

mixxx::streaming::Track parseTrack(const QJsonObject& obj) {
    mixxx::streaming::Track track;
    track.providerId = QStringLiteral("deezer");
    track.id = trackIdToString(obj.value(QLatin1String("SNG_ID")));
    if (track.id.isEmpty() || track.id == QStringLiteral("0")) {
        // Public API style payloads use "id" instead of "SNG_ID".
        track.id = trackIdToString(obj.value(QLatin1String("id")));
    }
    track.title = obj.value(QLatin1String("SNG_TITLE")).toString();
    if (track.title.isEmpty()) {
        track.title = obj.value(QLatin1String("title")).toString();
    }
    track.artist = obj.value(QLatin1String("ART_NAME")).toString();
    if (track.artist.isEmpty()) {
        track.artist = obj.value(QLatin1String("artist")).toObject()
                               .value(QLatin1String("name"))
                               .toString();
    }
    track.album = obj.value(QLatin1String("ALB_TITLE")).toString();
    if (track.album.isEmpty()) {
        track.album = obj.value(QLatin1String("album")).toObject()
                              .value(QLatin1String("title"))
                              .toString();
    }
    track.durationSec = obj.value(QLatin1String("DURATION")).toInt();
    track.isrc = obj.value(QLatin1String("ISRC")).toString();
    track.trackNumber = obj.value(QLatin1String("TRACK_NUMBER")).toInt();
    track.isExplicit = obj.value(QLatin1String("EXPLICIT_LYRICS")).toInt() == 1;
    return track;
}

} // anonymous namespace

DeezerClient::DeezerClient(UserSettingsPointer pConfig, QObject* parent)
        : mixxx::streaming::Provider(parent),
          m_pConfig(std::move(pConfig)),
          m_pNetwork(new QNetworkAccessManager(this)) {
    loadSession();
}

DeezerClient::~DeezerClient() = default;

bool DeezerClient::hasSession() const {
    return !m_arl.isEmpty();
}

QString DeezerClient::manualTokenPrompt() const {
    return QStringLiteral(
            "<p>Deezer does not offer a public login API for third-party "
            "players. To use Deezer in Mixxx you need the <b>arl</b> cookie "
            "from your browser session:</p>"
            "<ol>"
            "<li>Log in to <a href=\"https://www.deezer.com\">deezer.com</a> "
            "in your browser.</li>"
            "<li>Open the developer tools (F12) and go to "
            "<i>Application &#8594; Cookies &#8594; deezer.com</i>.</li>"
            "<li>Copy the value of the cookie named <b>arl</b>.</li>"
            "<li>Paste it below.</li>"
            "</ol>"
            "<p>This value grants access to your Deezer account, so treat it "
            "like a password.</p>");
}

void DeezerClient::submitManualToken(const QString& token) {
    const QString arl = token.trimmed();
    if (arl.isEmpty()) {
        emit loginFinished(false, tr("The ARL token is empty."));
        return;
    }
    m_arl = arl;
    m_sessionValid = false;
    fetchUserData([this](bool ok, const QString& error) {
        if (ok) {
            saveSession();
            emit sessionChanged(true);
            emit loginFinished(true, QString());
        } else {
            m_arl.clear();
            emit sessionChanged(false);
            emit loginFinished(false, error);
        }
    });
}

void DeezerClient::logout() {
    m_arl.clear();
    m_apiToken.clear();
    m_licenseToken.clear();
    m_userId.clear();
    m_sessionValid = false;
    saveSession();
    emit sessionChanged(false);
}

void DeezerClient::loadSession() {
    m_arl = m_pConfig->getValueString(kArlKey);
    m_apiToken = m_pConfig->getValueString(kApiTokenKey);
    m_licenseToken = m_pConfig->getValueString(kLicenseTokenKey);
    m_userId = m_pConfig->getValueString(kUserIdKey);
    m_qualityIndex = m_pConfig->getValue(kQualityKey, 1);
}

void DeezerClient::saveSession() {
    m_pConfig->setValue(kArlKey, m_arl);
    m_pConfig->setValue(kApiTokenKey, m_apiToken);
    m_pConfig->setValue(kLicenseTokenKey, m_licenseToken);
    m_pConfig->setValue(kUserIdKey, m_userId);
    m_pConfig->setValue(kQualityKey, m_qualityIndex);
}

QStringList DeezerClient::qualityLabels() const {
    return {tr("FLAC (lossless)"), tr("MP3 320 kbps"), tr("MP3 128 kbps")};
}

int DeezerClient::currentQualityIndex() const {
    return m_qualityIndex;
}

void DeezerClient::setQualityIndex(int index) {
    if (index < 0 || index > 2 || index == m_qualityIndex) {
        return;
    }
    m_qualityIndex = index;
    saveSession();
}

void DeezerClient::gwPost(
        const QString& method,
        const QJsonObject& args,
        const QJsonObject& params,
        const QString& apiToken,
        std::function<void(bool, const QJsonObject&, const QString&)> callback) {
    QUrl url(kGwUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("method"), method);
    query.addQueryItem(QStringLiteral("input"), QStringLiteral("3"));
    query.addQueryItem(QStringLiteral("api_version"), QStringLiteral("1.0"));
    query.addQueryItem(QStringLiteral("api_token"),
            apiToken.isEmpty() ? QStringLiteral("null") : apiToken);
    for (auto it = params.begin(); it != params.end(); ++it) {
        query.addQueryItem(it.key(), jsonParamToString(it.value()));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    if (!m_arl.isEmpty()) {
        request.setRawHeader("Cookie", QByteArrayLiteral("arl=") + m_arl.toUtf8());
    }

    QNetworkReply* reply = m_pNetwork->post(
            request, QJsonDocument(args).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [reply, callback = std::move(callback)]() {
                reply->deleteLater();
                const QByteArray data = reply->readAll();
                if (reply->error() != QNetworkReply::NoError) {
                    callback(false, {}, reply->errorString());
                    return;
                }
                const QJsonObject root = QJsonDocument::fromJson(data).object();
                const QJsonValue error = root.value(QLatin1String("error"));
                const bool hasError = (error.isObject() && !error.toObject().isEmpty()) ||
                        (error.isArray() && !error.toArray().isEmpty()) ||
                        (error.isString() && !error.toString().isEmpty());
                if (hasError) {
                    callback(false,
                            {},
                            QString::fromUtf8(
                                    QJsonDocument(error.toObject()).toJson(
                                            QJsonDocument::Compact)));
                    return;
                }
                callback(true, root.value(QLatin1String("results")).toObject(),
                        QString());
            });
}

void DeezerClient::fetchUserData(mixxx::streaming::ResultCallback callback) {
    gwPost(QStringLiteral("deezer.getUserData"),
            {},
            {},
            QString(),
            [this, callback](bool ok, const QJsonObject& results, const QString& error) {
                if (!ok) {
                    callback(false, error);
                    return;
                }
                const QJsonObject user = results.value(QLatin1String("USER")).toObject();
                const int userId = user.value(QLatin1String("USER_ID")).toInt();
                if (userId == 0) {
                    callback(false,
                            tr("Deezer did not accept the ARL token (not logged in)."));
                    return;
                }
                m_apiToken = results.value(QLatin1String("checkForm")).toString();
                if (m_apiToken.isEmpty()) {
                    m_apiToken = results.value(QLatin1String("checkFormLogin")).toString();
                }
                m_userId = QString::number(userId);
                m_licenseToken = user.value(QLatin1String("OPTIONS"))
                                         .toObject()
                                         .value(QLatin1String("license_token"))
                                         .toString();
                m_sessionValid = true;
                callback(true, QString());
            });
}

void DeezerClient::withSession(std::function<void(bool, const QString&)> callback) {
    if (m_arl.isEmpty()) {
        callback(false, tr("Not logged in to Deezer."));
        return;
    }
    if (m_sessionValid && !m_apiToken.isEmpty() && !m_licenseToken.isEmpty()) {
        callback(true, QString());
        return;
    }
    fetchUserData([callback = std::move(callback)](bool ok, const QString& error) {
        callback(ok, error);
    });
}

void DeezerClient::search(
        const QString& query,
        mixxx::streaming::SearchCallback callback) {
    withSession([this, query, callback](bool ok, const QString& error) {
        if (!ok) {
            callback(false, {}, error);
            return;
        }
        QJsonObject params;
        params.insert(QStringLiteral("query"), query);
        params.insert(QStringLiteral("start"), 0);
        params.insert(QStringLiteral("nb"), 50);
        params.insert(QStringLiteral("suggest"), true);
        params.insert(QStringLiteral("artist_suggest"), true);
        params.insert(QStringLiteral("top_tracks"), true);
        gwPost(QStringLiteral("deezer.pageSearch"),
                {},
                params,
                m_apiToken,
                [callback](bool ok, const QJsonObject& results, const QString& error) {
                    if (!ok) {
                        callback(false, {}, error);
                        return;
                    }
                    QJsonArray items = results.value(QLatin1String("TRACK"))
                                               .toObject()
                                               .value(QLatin1String("data"))
                                               .toArray();
                    if (items.isEmpty()) {
                        items = results.value(QLatin1String("data"))
                                        .toObject()
                                        .value(QLatin1String("TRACK"))
                                        .toObject()
                                        .value(QLatin1String("data"))
                                        .toArray();
                    }
                    mixxx::streaming::TrackList tracks;
                    tracks.reserve(items.size());
                    for (const auto& item : items) {
                        tracks.append(parseTrack(item.toObject()));
                    }
                    callback(true, tracks, QString());
                });
    });
}

void DeezerClient::downloadTrack(
        const mixxx::streaming::Track& track,
        mixxx::streaming::StreamCallback callback) {
    withSession([this, track, callback](bool ok, const QString& error) {
        if (!ok) {
            callback(false, {}, error);
            return;
        }
        resolveTrack(track, std::move(callback));
    });
}

void DeezerClient::resolveTrack(
        const mixxx::streaming::Track& track,
        mixxx::streaming::StreamCallback callback) {
    QJsonObject args;
    args.insert(QStringLiteral("SNG_ID"), track.id);
    gwPost(QStringLiteral("song.getData"),
            args,
            {},
            m_apiToken,
            [this, track, callback](bool ok,
                    const QJsonObject& results,
                    const QString& error) {
                if (!ok) {
                    callback(false, {}, error);
                    return;
                }
                const QString trackToken =
                        results.value(QLatin1String("TRACK_TOKEN")).toString();
                if (trackToken.isEmpty()) {
                    callback(false, {},
                            tr("Deezer did not return a track token. The track "
                               "may be unavailable in your region."));
                    return;
                }
                requestLicenseUrl(trackToken,
                        m_qualityIndex,
                        [this, track, callback](
                                bool ok, const QUrl& url, const QString& error) {
                            if (!ok) {
                                callback(false, {}, error);
                                return;
                            }
                            downloadAndDecrypt(track, url, std::move(callback));
                        });
            });
}

void DeezerClient::requestLicenseUrl(
        const QString& trackToken,
        int qualityIndex,
        mixxx::streaming::StreamCallback callback) {
    if (qualityIndex > 2) {
        callback(false, {},
                tr("No playable Deezer stream is available for this track and "
                   "account."));
        return;
    }
    const QString format = kQualityFormats[qualityIndex];

    QJsonArray formats;
    QJsonObject formatObj;
    formatObj.insert(QStringLiteral("cipher"), QStringLiteral("BF_CBC_STRIPE"));
    formatObj.insert(QStringLiteral("format"), format);
    formats.append(formatObj);

    QJsonArray media;
    QJsonObject mediaObj;
    mediaObj.insert(QStringLiteral("type"), QStringLiteral("FULL"));
    mediaObj.insert(QStringLiteral("formats"), formats);
    media.append(mediaObj);

    QJsonArray trackTokens;
    trackTokens.append(trackToken);

    QJsonObject body;
    body.insert(QStringLiteral("license_token"), m_licenseToken);
    body.insert(QStringLiteral("media"), media);
    body.insert(QStringLiteral("track_tokens"), trackTokens);

    QNetworkRequest request((QUrl(kLicenseUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", kUserAgent.toUtf8());
    if (!m_arl.isEmpty()) {
        request.setRawHeader("Cookie", QByteArrayLiteral("arl=") + m_arl.toUtf8());
    }

    QNetworkReply* reply = m_pNetwork->post(
            request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, trackToken, qualityIndex, callback = std::move(callback), reply]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    callback(false, {}, reply->errorString());
                    return;
                }
                const QJsonObject root =
                        QJsonDocument::fromJson(reply->readAll()).object();
                const QJsonArray data = root.value(QLatin1String("data")).toArray();
                if (data.isEmpty()) {
                    callback(false, {},
                            tr("Deezer returned no stream for this track."));
                    return;
                }
                const QJsonObject first = data.first().toObject();
                const QJsonArray mediaArray =
                        first.value(QLatin1String("media")).toArray();
                QString signedUrl;
                if (!mediaArray.isEmpty()) {
                    const QJsonArray sources = mediaArray.first()
                                                       .toObject()
                                                       .value(QLatin1String("sources"))
                                                       .toArray();
                    if (!sources.isEmpty()) {
                        signedUrl = sources.first()
                                            .toObject()
                                            .value(QLatin1String("url"))
                                            .toString();
                    }
                }
                if (signedUrl.isEmpty()) {
                    // Quality not available for this account; try the next
                    // lower quality before giving up.
                    requestLicenseUrl(trackToken, qualityIndex + 1, std::move(callback));
                    return;
                }
                callback(true, QUrl(signedUrl), QString());
            });
}

QString DeezerClient::cacheFilePath(
        const mixxx::streaming::Track& track,
        const QString& suffix) const {
    QDir cacheDir(QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation));
    cacheDir.mkpath(QStringLiteral("deezer"));
    cacheDir.cd(QStringLiteral("deezer"));
    return cacheDir.filePath(QStringLiteral("%1.%2").arg(track.id, suffix));
}

void DeezerClient::downloadAndDecrypt(
        const mixxx::streaming::Track& track,
        const QUrl& url,
        mixxx::streaming::StreamCallback callback) {
    const bool isLossless = m_qualityIndex == 0;
    const QString suffix = isLossless ? QStringLiteral("flac") : QStringLiteral("mp3");
    const QString filePath = cacheFilePath(track, suffix);
    QFile existing(filePath);
    if (existing.exists() && existing.size() > 0) {
        callback(true, QUrl::fromLocalFile(filePath), QString());
        return;
    }

    // The stream is Blowfish-CBC encrypted in 2048-byte stripes: only the
    // first 2048 bytes of each 6144-byte stripe are encrypted. A dedicated
    // Blowfish instance is required because the key depends on the track id.
    uint8_t key[16];
    mixxx::crypto::generateDeezerBlowfishKey(track.id.toLongLong(), key);
    auto cipher = std::make_shared<mixxx::crypto::Blowfish>(key, 16);

    auto output = std::make_shared<QFile>(filePath);
    if (!output->open(QIODevice::WriteOnly)) {
        callback(false, {}, output->errorString());
        return;
    }

    // Buffers the not-yet-decrypted bytes and the current offset within the
    // 6144-byte stripe pattern.
    struct State {
        QByteArray pending;
        int stripeOffset = 0;
        bool depadPending = true;
        qint64 total = 1;
        qint64 received = 0;
    };
    auto state = std::make_shared<State>();

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
    connect(reply, &QNetworkReply::readyRead, this,
            [reply, state, cipher, output]() {
                state->pending.append(reply->readAll());
                while (state->pending.size() >= kStripeBlock) {
                    // Decrypt the encrypted 2048-byte head of the stripe.
                    QByteArray head = state->pending.left(kStripeSize);
                    QByteArray tail = state->pending.mid(
                            kStripeSize, kStripeBlock - kStripeSize);
                    state->pending.remove(0, kStripeBlock);

                    QByteArray decrypted(head.size(), Qt::Uninitialized);
                    cipher->decryptCbc(
                            reinterpret_cast<const uint8_t*>(head.constData()),
                            reinterpret_cast<uint8_t*>(decrypted.data()),
                            head.size(),
                            kDeezerIv);

                    QByteArray chunk = decrypted + tail;
                    if (state->depadPending &&
                            !chunk.isEmpty() && chunk.at(0) == '\0') {
                        int i = 0;
                        while (i < chunk.size() && chunk.at(i) == '\0') {
                            ++i;
                        }
                        chunk.remove(0, i);
                        state->depadPending = false;
                    }
                    output->write(chunk);
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [reply, state, cipher, output, filePath, callback]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    output->close();
                    QFile::remove(filePath);
                    callback(false, {}, reply->errorString());
                    return;
                }
                // Flush any remaining bytes (the final partial stripe is left
                // unencrypted by Deezer's stripe scheme).
                if (!state->pending.isEmpty()) {
                    QByteArray chunk = state->pending;
                    state->pending.clear();
                    if (state->depadPending && !chunk.isEmpty() && chunk.at(0) == '\0') {
                        int i = 0;
                        while (i < chunk.size() && chunk.at(i) == '\0') {
                            ++i;
                        }
                        chunk.remove(0, i);
                    }
                    output->write(chunk);
                }
                output->close();
                if (output->size() == 0) {
                    QFile::remove(filePath);
                    callback(false, {},
                            tr("Deezer returned an empty or unreadable stream."));
                    return;
                }
                callback(true, QUrl::fromLocalFile(filePath), QString());
            });
}

} // namespace deezer
} // namespace mixxx
