#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "library/streaming/streamingprovider.h"
#include "preferences/usersettings.h"

class QNetworkReply;

namespace mixxx {
namespace deezer {

/// Deezer streaming provider.
///
/// Deezer does not expose a public playback API. Authentication uses the
/// browser session cookie ("arl") that the user copies from their logged-in
/// Deezer session. With it, the private "gw-light" API is used for search and
/// track metadata, the "media.deezer.com/v1/get_url" license endpoint resolves
/// a signed CDN URL, and the resulting Blowfish-CBC ("BF_CBC_STRIPE") stream is
/// decrypted locally before it is written to the cache and loaded into a deck.
///
/// This mirrors the approach used by the deemix/deezer-py projects; the
/// reference implementation was used to derive the endpoint shapes and the
/// Blowfish key schedule.
class DeezerClient final : public mixxx::streaming::Provider {
    Q_OBJECT

  public:
    explicit DeezerClient(UserSettingsPointer pConfig, QObject* parent = nullptr);
    ~DeezerClient() override;

    // Provider interface
    QString id() const override {
        return QStringLiteral("deezer");
    }
    QString displayName() const override {
        return QStringLiteral("Deezer");
    }
    QString iconName() const override {
        return QStringLiteral("deezer");
    }
    bool requiresLogin() const override {
        return true;
    }
    bool hasSession() const override;

    /// Deezer authenticates with a manually copied ARL cookie instead of a
    /// device authorization flow, so the generic dialog asks for it directly.
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
        return QStringLiteral("Search Deezer for tracks...");
    }

  private:
    /// POSTs to the private gw-light endpoint and unwraps the "results" object.
    void gwPost(
            const QString& method,
            const QJsonObject& args,
            const QJsonObject& params,
            const QString& apiToken,
            std::function<void(bool, const QJsonObject&, const QString&)> callback);

    /// Ensures a valid session (ARL + api token + license token) is available.
    void withSession(std::function<void(bool, const QString&)> callback);

    /// Reads deezer.getUserData, refreshing the api and license tokens.
    void fetchUserData(mixxx::streaming::ResultCallback callback);

    void loadSession();
    void saveSession();

    void resolveTrack(
            const mixxx::streaming::Track& track,
            mixxx::streaming::StreamCallback callback);

    /// Asks the license endpoint for a signed URL, falling back to lower
    /// qualities when the requested one is not available for the account.
    void requestLicenseUrl(
            const QString& trackToken,
            int qualityIndex,
            mixxx::streaming::StreamCallback callback);

    void downloadAndDecrypt(
            const mixxx::streaming::Track& track,
            const QUrl& url,
            mixxx::streaming::StreamCallback callback);

    QString cacheFilePath(
            const mixxx::streaming::Track& track,
            const QString& suffix) const;

    UserSettingsPointer m_pConfig;
    QNetworkAccessManager* m_pNetwork;

    // Session state (persisted)
    QString m_arl;
    QString m_apiToken;
    QString m_licenseToken;
    QString m_userId;
    bool m_sessionValid = false;
    int m_qualityIndex = 1;
};

} // namespace deezer
} // namespace mixxx
