#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include "library/streaming/streamingprovider.h"
#include "preferences/usersettings.h"

class QNetworkReply;

namespace mixxx {
namespace soundcloud {

/// SoundCloud streaming provider.
///
/// Public tracks need no account: a `client_id` is scraped from the web app
/// and the public v2 API is queried. Logging in with an OAuth token additionally
/// unlocks the SoundCloud Go+ catalogue (256 kbps AAC, which is otherwise only
/// offered as DRM-encrypted HLS to guests), private tracks and, for tracks that
/// allow it, the original (lossless) upload via the download endpoint.
///
/// Streams are either a progressive (single-file) MP3, or HLS (AAC fMP4 or MP3)
/// whose segments are concatenated. DRM-encrypted transcodings (ctr-/cbc- hls)
/// are never used.
class SoundCloudClient final : public mixxx::streaming::Provider {
    Q_OBJECT

  public:
    explicit SoundCloudClient(UserSettingsPointer pConfig, QObject* parent = nullptr);
    ~SoundCloudClient() override;

    // Provider interface
    QString id() const override {
        return QStringLiteral("soundcloud");
    }
    QString displayName() const override {
        return QStringLiteral("SoundCloud");
    }
    QString iconName() const override {
        return QStringLiteral("soundcloud");
    }
    /// Searching and playing public tracks works without an account, so login
    /// is optional rather than required.
    bool requiresLogin() const override {
        return false;
    }
    /// A login button is still shown so users can unlock the Go+ catalogue.
    bool showLoginButton() const override {
        return true;
    }
    bool hasSession() const override {
        return !m_oauthToken.isEmpty();
    }

    /// SoundCloud login cannot be done interactively by a third-party client
    /// (OAuth requires a registered app), so the user pastes an oauth_token.
    bool requiresManualToken() const override {
        return true;
    }
    QString manualTokenPrompt() const override;
    void submitManualToken(const QString& token) override;
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
        return QStringLiteral("Search SoundCloud for tracks...");
    }

  private:
    /// Fetches (and caches) a client_id by scraping the SoundCloud web app.
    void ensureClientId(mixxx::streaming::ResultCallback callback);
    /// Refreshes the client_id after a 401/403.
    void refreshClientId(mixxx::streaming::ResultCallback callback);

    void getJson(
            const QUrl& url,
            std::function<void(bool, const QByteArray&, const QString&)> callback);
    /// GET that attaches the OAuth token when logged in.
    void getApiJson(
            const QUrl& url,
            std::function<void(bool, const QByteArray&, const QString&)> callback);

    /// Resolves the chosen transcoding to a signed URL, trying the preferred
    /// transcodings in order and falling back to lower quality on failure.
    void resolveStreamUrl(
            const mixxx::streaming::Track& track,
            const QJsonObject& trackObj,
            mixxx::streaming::StreamCallback callback);

    /// Tries the original (lossless) download format when logged in and the
    /// track allows it. Calls `callback` only on success.
    void tryOriginalDownload(
            const mixxx::streaming::Track& track,
            const QJsonObject& trackObj,
            mixxx::streaming::StreamCallback callback);

    /// Downloads a progressive (single-file) audio URL to the cache.
    void downloadProgressive(
            const mixxx::streaming::Track& track,
            const QUrl& url,
            const QString& suffix,
            mixxx::streaming::StreamCallback callback);

    /// Downloads an HLS playlist (init segment + media segments), concatenates
    /// the parts and writes them to the cache.
    void downloadHls(
            const mixxx::streaming::Track& track,
            const QUrl& playlistUrl,
            mixxx::streaming::StreamCallback callback);

    /// Parses an m3u8 playlist into (initUrl, segmentUrls). Returns false when
    /// the playlist is encrypted.
    static bool parseM3u8(const QByteArray& playlist,
            QUrl* initUrl,
            QList<QUrl>* segmentUrls);

    /// Concatenates downloaded parts into the final cache file.
    bool finalizeParts(const QList<QByteArray>& parts, const QString& filePath) const;

    QString cacheFilePath(
            const mixxx::streaming::Track& track,
            const QString& suffix) const;

    UserSettingsPointer m_pConfig;
    QNetworkAccessManager* m_pNetwork;
    QString m_clientId;
    QString m_oauthToken;
    int m_qualityIndex = 0;
};

} // namespace soundcloud
} // namespace mixxx
