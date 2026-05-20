#include "library/autosuggestions/autosuggestionsprocessor.h"

#include <QTimer>

#include "engine/channels/enginedeck.h"
#include "mixer/basetrackplayer.h"
#include "mixer/playermanager.h"
#include "moc_autosuggestionsprocessor.cpp"
#include "track/track.h"
#include "util/math.h"

namespace {
const QString kPreferenceGroup = QStringLiteral("[AutoSuggestions]");
const QString kControlGroup = QStringLiteral("[AutoSuggestions]");
const char* kRefreshRatePreferenceName = "RefreshRate";
constexpr double kRefreshRatePreferenceDefault = 10.0;
constexpr bool sDebug = false;
} // anonymous namespace

AutoSuggestionsProcessor::AutoSuggestionsProcessor(
        QObject* pParent,
        UserSettingsPointer pConfig,
        TrackCollectionManager* pTrackCollectionManager,
        int iAutoSuggestionsPlaylistId)
        : QObject(pParent),
          m_pConfig(pConfig),
          m_pAutoSuggestionsTableModel(nullptr),
          m_eState(ASS_DISABLED),
          m_refreshRate(kRefreshRatePreferenceDefault),
          m_checkNow(ConfigKey(kControlGroup, QStringLiteral("check_now"))),
          m_enabledAutoSuggestions(ConfigKey(kControlGroup, QStringLiteral("enabled"))),
          m_pRefreshTimer(nullptr) {
    m_pAutoSuggestionsTableModel = make_parented<PlaylistTableModel>(
            this, pTrackCollectionManager, pConfig, "mixxx.db.model.autosuggestion");
    m_pAutoSuggestionsTableModel->selectPlaylist(iAutoSuggestionsPlaylistId);
    m_pAutoSuggestionsTableModel->select();

    m_pRefreshTimer = new QTimer();
    m_pRefreshTimer->setSingleShot(false);
    connect(m_pRefreshTimer,
            &QTimer::timeout,
            this,
            &AutoSuggestionsProcessor::onRefreshTimerTimeout);

    connect(&m_checkNow,
            &ControlObject::valueChanged,
            this,
            &AutoSuggestionsProcessor::controlCheckNow);
    m_enabledAutoSuggestions.setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_enabledAutoSuggestions.connectValueChangeRequest(this,
            &AutoSuggestionsProcessor::controlEnableChangeRequest);

    loadRefreshRateFromConfig();
}

AutoSuggestionsProcessor::~AutoSuggestionsProcessor() {
    if (m_pRefreshTimer) {
        m_pRefreshTimer->stop();
    }
}

void AutoSuggestionsProcessor::checkNow() {
    if (m_eState != ASS_IDLE) {
        return;
    }
    // qDebug() << "AutoSuggestionsProcessor::slotImportPlaylistFile --
    // Enabled?" << m_eState; qDebug() <<
    // "AutoSuggestionsProcessor::slotImportPlaylistFile -- RefreshRate" <<
    // m_refreshRate;
    emit ForceCheckAutoSuggestionsFileNow();
}

AutoSuggestionsProcessor::AutoSuggestionsError
AutoSuggestionsProcessor::toggleAutoSuggestions(bool enable) {
    if (enable) {
        m_eState = ASS_IDLE;
        emitAutoSuggestionsStateChanged(m_eState);
        updateTimerState();
    } else {
        m_enabledAutoSuggestions.setAndConfirm(0.0);
        qDebug() << "Auto Suggestions disabled";
        m_eState = ASS_DISABLED;
        emitAutoSuggestionsStateChanged(m_eState);
        updateTimerState();
    }
    return ASS_OK;
}

void AutoSuggestionsProcessor::controlEnableChangeRequest(double value) {
    toggleAutoSuggestions(value > 0.0);
}

void AutoSuggestionsProcessor::controlCheckNow(double value) {
    if (value > 0.0) {
        checkNow();
    }
}

TrackPointer AutoSuggestionsProcessor::getNextTrackFromQueue() {
    while (true) {
        TrackPointer pNextTrack = m_pAutoSuggestionsTableModel->getTrack(
                m_pAutoSuggestionsTableModel->index(0, 0));

        if (pNextTrack) {
            if (pNextTrack->getFileInfo().checkFileExists()) {
                return pNextTrack;
            } else {
                qWarning() << "Auto Suggestions: Skip missing track" << pNextTrack->getLocation();
                m_pAutoSuggestionsTableModel->removeTrack(
                        m_pAutoSuggestionsTableModel->index(0, 0));
            }
        } else {
            return pNextTrack;
        }
    }
}

void AutoSuggestionsProcessor::setRefreshRate(int time) {
    if constexpr (sDebug) {
        qDebug() << "AutoSuggestionsProcessor::slotImportPlaylistFile -- Old "
                    "RefreshRate"
                 << m_refreshRate;
    }

    m_pConfig->setValue(ConfigKey(kPreferenceGroup, kRefreshRatePreferenceName),
            time);
    m_refreshRate = time;

    if constexpr (sDebug) {
        qDebug() << "AutoSuggestionsProcessor::slotImportPlaylistFile -- New "
                    "RefreshRate"
                 << m_refreshRate;
    }
    updateTimerState();
}

bool AutoSuggestionsProcessor::nextTrackLoaded() {
    return true;
}

void AutoSuggestionsProcessor::loadRefreshRateFromConfig() {
    m_refreshRate = m_pConfig->getValue(
            ConfigKey(kPreferenceGroup, kRefreshRatePreferenceName),
            kRefreshRatePreferenceDefault);
    updateTimerState();
}

void AutoSuggestionsProcessor::updateTimerState() {
    if (!m_pRefreshTimer) {
        return;
    }

    if (m_eState == ASS_IDLE && m_refreshRate > 0) {
        int intervalMs = static_cast<int>(m_refreshRate * 1000);
        m_pRefreshTimer->start(intervalMs);

        if constexpr (sDebug) {
            qDebug() << "AutoSuggestionsProcessor: Timer started with interval"
                     << intervalMs << "ms";
        }
    } else {
        m_pRefreshTimer->stop();

        if constexpr (sDebug) {
            qDebug() << "AutoSuggestionsProcessor: Timer stopped (state="
                     << m_eState << ", rate=" << m_refreshRate << ")";
        }
    }
}

void AutoSuggestionsProcessor::onRefreshTimerTimeout() {
    if constexpr (sDebug) {
        qDebug() << "AutoSuggestionsProcessor: Timer timeout - calling checkNow()";
    }
    if (m_eState != ASS_IDLE) {
        return;
    }
    emit CheckAutoSuggestionsFileNow();
}
