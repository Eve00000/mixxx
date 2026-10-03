#pragma once

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
/// SoundCloud needs no account for public tracks. It exposes a `client_id`
/// (scraped from the web app) plus a per-track `track_authorization` token.
/// Requesting a track's `media.transcodings` yields a signed URL to a plain,
/// unencrypted progressive MP3 or HLS/AAC stream.
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
    bool requiresLogin() const override {
        return false;
    }
    bool hasSession() const override {
        // Public SoundCloud access only needs a scraped client_id.
        return !m_clientId.isEmpty();
    }

    void search(
            const QString& query,
            mixxx::streaming::SearchCallback callback) override;

    void downloadTrack(
            const mixxx::streaming::Track& track,
            mixxx::streaming::StreamCallback callback) override;

    QString searchPlaceholderText() const override {
        return QStringLiteral("Search SoundCloud for tracks...");
    }

  private:
    /// Fetches (and caches) a client_id by scraping the SoundCloud web app.
    /// Calls the callback once a client_id is available.
    void ensureClientId(mixxx::streaming::ResultCallback callback);

    void getJson(
            const QUrl& url,
            std::function<void(bool, const QByteArray&, const QString&)> callback);

    /// Resolves the chosen transcoding to a signed URL.
    void resolveStreamUrl(
            const mixxx::streaming::Track& track,
            const QByteArray& trackJson,
            mixxx::streaming::StreamCallback callback);

    /// Downloads a progressive (single-file) audio URL to the cache.
    void downloadProgressive(
            const mixxx::streaming::Track& track,
            const QUrl& url,
            mixxx::streaming::StreamCallback callback);

    /// Downloads an HLS playlist (init segment + media segments) and remuxes
    /// it into a local file via FFmpeg.
    void downloadHls(
            const mixxx::streaming::Track& track,
            const QUrl& playlistUrl,
            mixxx::streaming::StreamCallback callback);

    /// Parses an m3u8 playlist into (initUrl, segmentUrls).
    static bool parseM3u8(const QByteArray& playlist,
            QUrl* initUrl,
            QList<QUrl>* segmentUrls);

    /// Concatenates downloaded HLS segments (fMP4 or MP3) into a final file.
    bool finalizeHlsFile(const QList<QByteArray>& parts,
            bool isMp4,
            const QString& filePath) const;

    QString cacheFilePath(
            const mixxx::streaming::Track& track,
            const QString& suffix) const;

    UserSettingsPointer m_pConfig;
    QNetworkAccessManager* m_pNetwork;
    QString m_clientId;
};

} // namespace soundcloud
} // namespace mixxx
