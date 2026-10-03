#pragma once

#include <QFuture>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

#include "library/tidal/tidaldefs.h"
#include "preferences/usersettings.h"

class QNetworkReply;
class QXmlStreamReader;

namespace mixxx {
namespace tidal {

/// A single track as returned by the TIDAL search API.
struct TidalTrack final {
    qint64 id = 0;
    QString title;
    QString artist;
    QString album;
    QString isrc;
    QString year;
    QString audioQuality;
    int durationSec = 0;
    int trackNumber = 0;
    bool isExplicit = false;
};

using TidalSearchCallback =
        std::function<void(bool ok, const QList<TidalTrack>& tracks, const QString& error)>;
using TidalResultCallback = std::function<void(bool ok, const QString& error)>;
using TidalBytesCallback =
        std::function<void(bool ok, const QByteArray& data, const QString& error)>;
using TidalStreamCallback =
        std::function<void(bool ok, const QUrl& url, const QString& error)>;

/// Minimal asynchronous client for the unofficial TIDAL API.
///
/// This implements the OAuth2 device authorization grant (RFC 8628) used by
/// the TIDAL Android client, plus the search and `playbackinfopostpaywall`
/// endpoints required to resolve an unencrypted MPEG-DASH manifest.
class TidalClient final : public QObject {
    Q_OBJECT

  public:
    explicit TidalClient(UserSettingsPointer pConfig, QObject* parent = nullptr);
    ~TidalClient() override;

    bool hasSession() const;

    /// Begin the OAuth2 device authorization grant. Emits
    /// deviceLoginStarted() with the code and URL the user has to visit and
    /// continues polling in the background until the login succeeds, expires
    /// or is cancelled via cancelLogin().
    void startDeviceLogin();
    void cancelLogin();

    void logout();

    void search(
            const QString& query,
            TidalSearchCallback callback);

    /// Resolve the stream URL. If cached is true the returned URL is a local
    /// file that has already been downloaded; otherwise it is a remote
    /// manifest URL. Only the cached path is currently implemented.
    void resolveStream(
            const TidalTrack& track,
            TidalStreamCallback callback);

    /// Download the MPEG-DASH stream for the given track and write a playable
    /// local file, then invoke the callback with its file URL.
    void downloadTrack(
            const TidalTrack& track,
            TidalStreamCallback callback);

    void setQuality(Quality quality);
    Quality quality() const {
        return m_quality;
    }

  signals:
    void deviceLoginStarted(
            const QString& userCode,
            const QString& verificationUriComplete,
            int expiresInSeconds);
    void deviceLoginFinished(bool success, const QString& error);
    void sessionChanged(bool loggedIn);
    void downloadProgress(int completed, int total);

  private:
    void sendRequest(
            const QString& path,
            const QUrlQuery& query,
            std::function<void(bool, const QByteArray&, const QString&)> callback);
    void postForm(
            const QString& absoluteUrl,
            const QUrlQuery& query,
            std::function<void(bool, const QByteArray&, const QString&)> callback);
    void pollDeviceToken();

    void applyAuthToken(const QByteArray& json);
    void fetchSessionInfo(TidalResultCallback callback);
    void refreshAccessToken(std::function<void(bool)> callback);
    void loadSession();
    void saveSession();

    void downloadSegments(
            const TidalTrack& track,
            const QList<QUrl>& segmentUrls,
            const QString& fileSuffix,
            TidalStreamCallback callback);

    QByteArray authHeader() const;

    QString cacheFilePath(const TidalTrack& track, const QString& suffix) const;

    UserSettingsPointer m_pConfig;
    QNetworkAccessManager* m_pNetwork;

    // Session state (persisted)
    QString m_accessToken;
    QString m_refreshToken;
    QString m_tokenType;
    QString m_sessionId;
    QString m_countryCode;
    QString m_userId;
    Quality m_quality = Quality::High;

    // Device login state
    QString m_deviceCode;
    QString m_deviceUserCode;
    int m_deviceExpiresIn = 0;
    int m_deviceInterval = 5;
    int m_devicePollAttempts = 0;
    bool m_deviceLoginRunning = false;
};

} // namespace tidal
} // namespace mixxx
