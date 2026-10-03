#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

namespace mixxx {
namespace streaming {

/// A track provided by a streaming service. It is not backed by a local file
/// until it has been downloaded by the provider.
struct Track final {
    QString providerId; // e.g. "tidal", "soundcloud"
    QString id;         // provider-specific track id (as string)
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

using TrackList = QList<Track>;

using SearchCallback =
        std::function<void(bool ok, const TrackList& tracks, const QString& error)>;
using ResultCallback = std::function<void(bool ok, const QString& error)>;
using StreamCallback =
        std::function<void(bool ok, const QUrl& localFileUrl, const QString& error)>;

/// Abstract interface implemented by each streaming service integration
/// (TIDAL, SoundCloud, ...).
///
/// Providers expose search, an optional interactive login and a download step
/// that resolves the (unencrypted) stream and writes a playable local file.
/// The generic UI (StreamingDlg/StreamingFeature) and the generic track model
/// are driven purely through this interface.
class Provider : public QObject {
    Q_OBJECT

  public:
    explicit Provider(QObject* parent = nullptr)
            : QObject(parent) {
    }
    ~Provider() override = default;

    /// Stable lowercase identifier, also used as the cache subdirectory and
    /// in the placeholder URL scheme ("<id>://track/<trackId>").
    virtual QString id() const = 0;
    /// Human readable name shown in the sidebar and status messages.
    virtual QString displayName() const = 0;
    /// Icon base name, resolved to ":/images/library/ic_library_<iconName>.svg".
    virtual QString iconName() const = 0;

    /// Whether the provider needs an interactive login before searching.
    virtual bool requiresLogin() const = 0;
    virtual bool hasSession() const = 0;

    /// Interactive login (device authorization / cookie etc.). Providers that
    /// do not need a login may leave these as no-ops.
    virtual void startLogin() {}
    virtual void cancelLogin() {}
    virtual void logout() {}

    /// Some providers (e.g. Deezer) cannot perform an interactive login and
    /// instead require the user to paste a session token/cookie. When
    /// `requiresManualToken()` is true the generic UI asks for a token and
    /// calls `submitManualToken()` instead of `startLogin()`.
    virtual bool requiresManualToken() const {
        return false;
    }
    virtual QString manualTokenPrompt() const {
        return {};
    }
    virtual void submitManualToken(const QString& token) {
        Q_UNUSED(token);
    }

    /// Search the service.
    virtual void search(
            const QString& query,
            SearchCallback callback) = 0;

    /// Download the given track and return the local file URL. The actual
    /// audio is expected to be unencrypted.
    virtual void downloadTrack(
            const Track& track,
            StreamCallback callback) = 0;

    /// Optional quality selection support for the UI. Return an empty list if
    /// not applicable.
    virtual QStringList qualityLabels() const {
        return {};
    }
    virtual int currentQualityIndex() const {
        return -1;
    }
    virtual void setQualityIndex(int index) {
        Q_UNUSED(index);
    }

    virtual QString searchPlaceholderText() const {
        return tr("Search...");
    }

  signals:
    void sessionChanged(bool loggedIn);
    void loginStarted(
            const QString& userCode,
            const QString& verificationUriComplete,
            int expiresInSeconds);
    void loginFinished(bool success, const QString& error);
    void downloadProgress(int completed, int total);
};

} // namespace streaming
} // namespace mixxx
