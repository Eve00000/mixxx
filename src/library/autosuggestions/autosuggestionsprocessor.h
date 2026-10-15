#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <vector>

#include "audio/frame.h"
// #include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "control/pollingcontrolproxy.h"
#include "engine/channels/enginechannel.h"
#include "library/playlisttablemodel.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "util/class.h"
#include "util/parented_ptr.h"

class TrackCollectionManager;
typedef QList<QModelIndex> QModelIndexList;

class AutoSuggestionsProcessor : public QObject {
    Q_OBJECT
  public:
    enum AutoSuggestionsState {
        ASS_IDLE = 0,
        ASS_BLABLABLA1111,
        ASS_DISABLED
    };

    enum AutoSuggestionsError {
        ASS_OK = 0,
        ASS_BLABLABLA2222,
        ASS_IS_INACTIVE,
        ASS_QUEUE_EMPTY,
    };

    AutoSuggestionsProcessor(QObject* pParent,
            UserSettingsPointer pConfig,
            TrackCollectionManager* pTrackCollectionManager,
            int iAutoSuggestionsPlaylistId);
    ~AutoSuggestionsProcessor() override;

    AutoSuggestionsState getState() const {
        return m_eState;
    }

    double getRefreshRate() const {
        return m_refreshRate;
    }

    PlaylistTableModel* getTableModel() const {
        return m_pAutoSuggestionsTableModel;
    }

    bool nextTrackLoaded();

    void setRefreshRate(int seconds);

    AutoSuggestionsError toggleAutoSuggestions(bool enable);

  signals:
#ifdef __STEM__
    void loadTrackToPlayer(TrackPointer pTrack,
            const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool play);
#else
    void loadTrackToPlayer(TrackPointer pTrack, const QString& group, bool play);
#endif
    void autoSuggestionsStateChanged(AutoSuggestionsProcessor::AutoSuggestionsState state);
    void autoSuggestionsError(AutoSuggestionsProcessor::AutoSuggestionsError error);
    void refreshRateChanged(int time);
    void CheckAutoSuggestionsFileNow();
    void ForceCheckAutoSuggestionsFileNow();

  public slots:
    void checkNow();

  private slots:
    void controlEnableChangeRequest(double value);
    void controlCheckNow(double value);
    void onRefreshTimerTimeout();

  protected:
    virtual void emitLoadTrackToPlayer(TrackPointer pTrack, const QString& group, bool play) {
        emit loadTrackToPlayer(pTrack, group,
#ifdef __STEM__
                mixxx::StemChannelSelection(),
#endif
                play);
    }
    virtual void emitAutoSuggestionsStateChanged(
            AutoSuggestionsProcessor::AutoSuggestionsState state) {
        emit autoSuggestionsStateChanged(state);
    }

  private:
    void loadRefreshRateFromConfig();
    void updateTimerState();

    TrackPointer getNextTrackFromQueue();
    UserSettingsPointer m_pConfig;
    parented_ptr<PlaylistTableModel> m_pAutoSuggestionsTableModel;
    AutoSuggestionsState m_eState;
    double m_refreshRate;
    ControlPushButton m_checkNow;
    ControlPushButton m_enabledAutoSuggestions;
    QTimer* m_pRefreshTimer;

    DISALLOW_COPY_AND_ASSIGN(AutoSuggestionsProcessor);
};
