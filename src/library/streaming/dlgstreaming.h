#pragma once

#include <QWidget>

#include "library/libraryview.h"
#include "library/streaming/streamingprovider.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#ifdef __STEM__
#include "engine/engine.h"
#endif

class Library;
class KeyboardEventFilter;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class ProxyTrackModel;
class WLibrary;
class WTrackTableView;
class TrackCollectionManager;
class StreamingTrackListModel;

/// Generic library view for any streaming provider. It exposes an optional
/// login button, a search box, an optional quality selector and a results
/// table. The provider handles authentication, search and downloading; this
/// view is provider-agnostic.
class DlgStreaming final : public QWidget, public virtual LibraryView {
    Q_OBJECT

  public:
    DlgStreaming(
            WLibrary* parent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            mixxx::streaming::Provider* pProvider);
    ~DlgStreaming() override;

    void onSearch(const QString& text) override;
    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;

    /// Download the given track and load it into the given group. An empty
    /// group loads it into the first available deck. Used by the
    /// load-to-deck actions and by drag & drop.
    void downloadAndLoad(
            const mixxx::streaming::Track& track,
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
    void slotQualityChanged(int index);
    void slotLoginClicked();
    void slotLogoutClicked();
    void slotLoginStarted(
            const QString& userCode,
            const QString& verificationUri,
            int expiresInSeconds);
    void slotLoginFinished(bool success, const QString& error);
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
            const mixxx::streaming::Track& track,
            const QString& filePath) const;

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    mixxx::streaming::Provider* m_pProvider;
    TrackCollectionManager* m_pTrackCollectionManager;

    QLineEdit* m_pSearchLineEdit;
    QPushButton* m_pSearchButton;
    QComboBox* m_pQualityComboBox;
    QPushButton* m_pLoginButton;
    QPushButton* m_pLogoutButton;
    QLabel* m_pStatusLabel;
    WTrackTableView* m_pTrackTableView;
    StreamingTrackListModel* m_pTrackModel;
    ProxyTrackModel* m_pProxyModel;
};
