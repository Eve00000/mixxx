#pragma once

#include <QFuture>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

#include "library/streaming/streamingprovider.h"
#include "library/tidal/tidaldefs.h"
#include "preferences/usersettings.h"

class QNetworkReply;
class QXmlStreamReader;

namespace mixxx {
namespace tidal {

using TidalStreamCallback =
        std::function<void(bool ok, const QUrl& url, const QString& error)>;

/// TIDAL streaming provider.
///
/// This implements the OAuth2 device authorization grant (RFC 8628) used by
/// the TIDAL Android client, plus the search and v2 trackManifests endpoints
/// required to resolve an unencrypted MPEG-DASH manifest.
class TidalClient final : public mixxx::streaming::Provider {
    Q_OBJECT

  public:
    explicit TidalClient(UserSettingsPointer pConfig, QObject* parent = nullptr);
    ~TidalClient() override;

    // Provider interface
    QString id() const override {
        return QStringLiteral("tidal");
    }
    QString displayName() const override {
        return QStringLiteral("TIDAL");
    }
    QString iconName() const override {
        return QStringLiteral("tidal");
    }
    bool requiresLogin() const override {
        return true;
    }
    bool hasSession() const override;

    void startLogin() override;
    void cancelLogin() override;
    void logout() override;

    void search(
            const QString& query,
            mixxx::streaming::SearchCallback callback) override;

    void downloadTrack(
            const mixxx::streaming::Track& track,
            mixxx::streaming::StreamCallback callback) override;

    QStringList qualityLabels() const override;
    int currentQualityIndex() const override;
    void setQualityIndex(int index) override;
    QString searchPlaceholderText() const override {
        return QStringLiteral("Search TIDAL for tracks...");
    }

  private:
    void sendRequest(
            const QString& path,
            const QUrlQuery& query,
            std::function<void(bool, const QByteArray&, const QString&)> callback);
    /// Performs an authenticated GET on an absolute URL (e.g. the manifest
    /// endpoint) and returns the raw response body.
    void getAbsolute(
            const QUrl& url,
            std::function<void(bool, const QByteArray&, const QString&)> callback);
    void postForm(
            const QString& absoluteUrl,
            const QUrlQuery& query,
            std::function<void(bool, const QByteArray&, const QString&)> callback);
    void pollDeviceToken();

    void applyAuthToken(const QByteArray& json);
    void fetchSessionInfo(mixxx::streaming::ResultCallback callback);
    void refreshAccessToken(std::function<void(bool)> callback);
    void loadSession();
    void saveSession();

    void downloadSegments(
            const mixxx::streaming::Track& track,
            const QList<QUrl>& segmentUrls,
            const QString& fileSuffix,
            mixxx::streaming::StreamCallback callback);

    void handleDashManifest(
            const mixxx::streaming::Track& track,
            const QByteArray& mpd,
            mixxx::streaming::StreamCallback callback);

    void setQuality(Quality quality);
    Quality quality() const {
        return m_quality;
    }

    QByteArray authHeader() const;

    QString cacheFilePath(
            const mixxx::streaming::Track& track,
            const QString& suffix) const;

    UserSettingsPointer m_pConfig;
    QNetworkAccessManager* m_pNetwork;

    // Session state (persisted)
    QString m_accessToken;
    QString m_refreshToken;
    QString m_tokenType;
    QString m_sessionId;
    QString m_countryCode;
    QString m_userId;
    Quality m_quality = Quality::Lossless;

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
