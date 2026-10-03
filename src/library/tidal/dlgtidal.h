#pragma once

#include <QWidget>

#include "library/libraryview.h"
#include "library/tidal/tidalclient.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#ifdef __STEM__
#include "engine/engine.h"
#endif

class Library;
class KeyboardEventFilter;
class QLabel;
class QLineEdit;
class QPushButton;
class ProxyTrackModel;
class WLibrary;
class WTrackTableView;
class TrackCollectionManager;
class TidalTrackListModel;

/// Library view that lets the user log in to TIDAL, search for tracks and load
/// them to a deck. The actual streaming/decoding is delegated to
/// mixxx::tidal::TidalClient which downloads the unencrypted MPEG-DASH
/// segments into a local cache file.
class DlgTidal final : public QWidget, public virtual LibraryView {
    Q_OBJECT

  public:
    DlgTidal(
            WLibrary* parent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            mixxx::tidal::TidalClient* pTidalClient);
    ~DlgTidal() override;

    void onSearch(const QString& text) override;
    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;

    /// Download the given TIDAL track and load it into the given group. An
    /// empty group loads it into the first available deck. Used both by the
    /// load-to-deck actions and by drag & drop.
    void downloadAndLoad(
            const mixxx::tidal::TidalTrack& track,
            const QString& group);

  signals:
    void loadTrack(TrackPointer pTrack);
#ifdef __STEM__
    void loadTrackToPlayer(
            TrackPointer pTrack,
            const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool play);
#else
    void loadTrackToPlayer(TrackPointer pTrack, const QString& group, bool play = false);
#endif

  private slots:
    void slotSearch();
    void slotLoginClicked();
    void slotLogoutClicked();
    void slotDeviceLoginStarted(
            const QString& userCode,
            const QString& verificationUri,
            int expiresInSeconds);
    void slotDeviceLoginFinished(bool success, const QString& error);
    void slotSessionChanged(bool loggedIn);
    void slotDownloadProgress(int completed, int total);
    void slotLoadTrack(TrackPointer pTrack);
    void slotLoadTrackToPlayer(TrackPointer pTrack, const QString& group);

  private:
    void updateLoginUi(bool loggedIn);
    void resolveAndLoad(
            const TrackPointer& pPlaceholder,
            const QString& group);
    TrackPointer trackFromFile(
            const mixxx::tidal::TidalTrack& tidalTrack,
            const QString& filePath) const;

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    mixxx::tidal::TidalClient* m_pTidalClient;
    TrackCollectionManager* m_pTrackCollectionManager;

    QLineEdit* m_pSearchLineEdit;
    QPushButton* m_pSearchButton;
    QPushButton* m_pLoginButton;
    QPushButton* m_pLogoutButton;
    QLabel* m_pStatusLabel;
    WTrackTableView* m_pTrackTableView;
    TidalTrackListModel* m_pTrackModel;
    ProxyTrackModel* m_pProxyModel;
};
