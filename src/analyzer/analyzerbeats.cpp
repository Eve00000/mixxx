#include "analyzer/analyzerbeats.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QVector>
#include <QtDebug>

#include "analyzer/analyzertrack.h"
#include "analyzer/constants.h"
#include "analyzer/plugins/analyzerqueenmarybeats.h"
#include "analyzer/plugins/analyzerqueenmarybeatsextended.h"
#include "analyzer/plugins/analyzersoundtouchbeats.h"
#include "library/rekordbox/rekordboxconstants.h"
#include "track/beatfactory.h"
#include "track/track.h"

// static
QList<mixxx::AnalyzerPluginInfo> AnalyzerBeats::availablePlugins() {
    QList<mixxx::AnalyzerPluginInfo> plugins;
    // First one below is the default
    // eve added extended as default
    plugins.append(mixxx::AnalyzerQueenMaryBeatsExtended::pluginInfo());
    plugins.append(mixxx::AnalyzerQueenMaryBeats::pluginInfo());
    plugins.append(mixxx::AnalyzerSoundTouchBeats::pluginInfo());
    return plugins;
}

// static
mixxx::AnalyzerPluginInfo AnalyzerBeats::defaultPlugin() {
    const auto plugins = availablePlugins();
    DEBUG_ASSERT(!plugins.isEmpty());
    return plugins.at(0);
}

AnalyzerBeats::AnalyzerBeats(UserSettingsPointer pConfig, bool enforceBpmDetection)
        : m_bpmSettings(pConfig),
          m_enforceBpmDetection(enforceBpmDetection),
          m_bPreferencesReanalyzeOldBpm(false),
          m_bPreferencesReanalyzeImported(false),
          m_bPreferencesFixedTempo(true),
          m_bPreferencesFastAnalysis(false),
          m_maxFramesToProcess(0),
          m_currentFrame(0) {
}

bool AnalyzerBeats::initialize(const AnalyzerTrack& track,
        mixxx::audio::SampleRate sampleRate,
        mixxx::audio::ChannelCount channelCount,
        SINT frameLength) {
    if (frameLength <= 0) {
        return false;
    }

    bool bPreferencesBeatDetectionEnabled =
            m_enforceBpmDetection || m_bpmSettings.getBpmDetectionEnabled();
    if (!bPreferencesBeatDetectionEnabled) {
        qDebug() << "Beat calculation is deactivated";
        return false;
    }

    bool bpmLock = track.getTrack()->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }

    m_bPreferencesFixedTempo = track.getOptions().useFixedTempo.value_or(
            m_bpmSettings.getFixedTempoAssumption());
    m_bPreferencesReanalyzeOldBpm = m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bPreferencesReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bPreferencesFastAnalysis = m_bpmSettings.getFastAnalysis();

    const auto plugins = availablePlugins();
    if (!plugins.isEmpty()) {
        m_pluginId = defaultPlugin().id();
        QString pluginId = m_bpmSettings.getBeatPluginId();
        for (const auto& info : plugins) {
            if (info.id() == pluginId) {
                m_pluginId = pluginId; // configured Plug-In available
                break;
            }
        }
    }

    qDebug() << "AnalyzerBeats preference settings:"
             << "\nPlugin:" << m_pluginId
             << "\nFixed tempo assumption:" << m_bPreferencesFixedTempo
             << "\nRe-analyze when settings change:" << m_bPreferencesReanalyzeOldBpm
             << "\nRe-analyze imported from other software:" << m_bPreferencesReanalyzeImported
             << "\nFast analysis:" << m_bPreferencesFastAnalysis;

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;
    // In fast analysis mode, skip processing after
    // kFastAnalysisSecondsToAnalyze seconds are analyzed.
    if (m_bPreferencesFastAnalysis) {
        m_maxFramesToProcess =
                mixxx::kFastAnalysisSecondsToAnalyze * m_sampleRate;
    } else {
        m_maxFramesToProcess = frameLength;
    }
    m_currentFrame = 0;

    // if we can load a stored track don't reanalyze it
    bool bShouldAnalyze = shouldAnalyze(track.getTrack());

    DEBUG_ASSERT(!m_pPlugin);
    if (bShouldAnalyze) {
        // Eve for BPM segments -> extended QM
        if (m_pluginId == mixxx::AnalyzerQueenMaryBeatsExtended::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerQueenMaryBeatsExtended>();
        } else if (m_pluginId == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerQueenMaryBeats>();
        } else if (m_pluginId == mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerSoundTouchBeats>();
        } else {
            // This must not happen, because we have already verified above
            // that the PlugInId is valid
            DEBUG_ASSERT(false);
        }

        if (m_pPlugin) {
            if (m_pPlugin->initialize(m_sampleRate)) {
                qDebug() << "Beat calculation started with plugin" << m_pluginId;
            } else {
                qDebug() << "Beat calculation will not start.";
                m_pPlugin.reset();
                bShouldAnalyze = false;
            }
        } else {
            bShouldAnalyze = false;
        }
    }
    return bShouldAnalyze;
}

bool AnalyzerBeats::shouldAnalyze(TrackPointer pTrack) const {
    bool bpmLock = pTrack->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }

    QString pluginID = m_bpmSettings.getBeatPluginId();
    if (pluginID.isEmpty()) {
        pluginID = defaultPlugin().id();
    }

    // If the track already has a Beats object then we need to decide whether to
    // analyze this track or not.
    const mixxx::BeatsPointer pBeats = pTrack->getBeats();
    if (!pBeats) {
        return true;
    }
    if (!pBeats->getBpmInRange(mixxx::audio::kStartFramePos,
                       mixxx::audio::FramePos{
                               pTrack->getDuration() * pBeats->getSampleRate()})
                    .isValid()) {
        // Tracks with an invalid bpm <= 0 should be re-analyzed,
        // independent of the preference settings. We expect that
        // all tracks have a bpm > 0 when analyzed. Users that want
        // to keep their zero bpm tracks could lock them to prevent
        // this re-analysis (see the check above).
        qDebug() << "Re-analyzing track with invalid BPM despite preference settings.";
        return true;
    }

    QString subVersion = pBeats->getSubVersion();
    if (subVersion == mixxx::rekordboxconstants::beatsSubversion) {
        return m_bPreferencesReanalyzeImported;
    }

    if (subVersion.isEmpty() && pBeats->firstBeat() <= mixxx::audio::kStartFramePos &&
            m_pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
        // This happens if the beat grid was created from the metadata BPM value.
        qDebug() << "First beat is 0 for grid so analyzing track to find first beat.";
        return true;
    }

    QString version = pBeats->getVersion();
    QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
            pluginID,
            m_bPreferencesFastAnalysis);
    QString newVersion = BeatFactory::getPreferredVersion(
            m_bPreferencesFixedTempo);
    QString newSubVersion = BeatFactory::getPreferredSubVersion(
            extraVersionInfo);

    if (version == newVersion && subVersion == newSubVersion) {
        // If the version and settings have not changed then if the world is
        // sane, re-analyzing will do nothing.
        return false;
    }
    // Beat grid exists but version and settings differ
    if (!m_bPreferencesReanalyzeOldBpm) {
        qDebug() << "Beat calculation skips analyzing because the track has"
                << "a BPM computed by a previous Mixxx version and user"
                << "preferences indicate we should not change it.";
        return false;
    }

    return true;
}

bool AnalyzerBeats::processSamples(const CSAMPLE* pIn, SINT count) {
    VERIFY_OR_DEBUG_ASSERT(m_pPlugin) {
        return false;
    }

    SINT numFrames = count / m_channelCount;
    const CSAMPLE* pBeatInput = pIn;
    CSAMPLE* pDrumChannel = nullptr;

    if (m_channelCount == mixxx::audio::ChannelCount::stem()) {
        // We have an 8 channel soundsource. The only implemented soundsource with
        // 8ch is the NI STEM file format.
        // TODO: If we add other soundsources with 8ch, we need to rework this condition.
        //
        // For NI STEM we mix all the stems together except the first one,
        // which contains drums or beats by convention.
        count = numFrames * mixxx::audio::ChannelCount::stereo();
        pDrumChannel = SampleUtil::alloc(count);

        VERIFY_OR_DEBUG_ASSERT(pDrumChannel) {
            return false;
        }

        if (m_bpmSettings.getStemStrategy() == BeatDetectionSettings::StemStrategy::Enforced) {
            SampleUtil::copyOneStereoFromMulti(pDrumChannel, pIn, numFrames, m_channelCount, 0);
        } else {
            SampleUtil::mixMultichannelToStereo(pDrumChannel, pIn, numFrames, m_channelCount);
        }

        pBeatInput = pDrumChannel;
    } else if (m_channelCount > mixxx::audio::ChannelCount::stereo()) {
        DEBUG_ASSERT(!"Unsupported channel count");
        return false;
    }

    m_currentFrame += numFrames;
    if (m_currentFrame > m_maxFramesToProcess) {
        return true; // silently ignore all remaining samples
    }

    bool ret = m_pPlugin->processSamples(pBeatInput, count);
    if (pDrumChannel) {
        SampleUtil::free(pDrumChannel);
    }
    return ret;
}

void AnalyzerBeats::cleanup() {
    m_pPlugin.reset();
}

void AnalyzerBeats::storeResults(TrackPointer pTrack) {
    VERIFY_OR_DEBUG_ASSERT(m_pPlugin) {
        return;
    }

    if (!m_pPlugin->finalize()) {
        qWarning() << "Beat/BPM analysis failed";
        return;
    }

    mixxx::BeatsPointer pBeats;

    if (m_pPlugin->supportsBeatTracking()) {
        QVector<mixxx::audio::FramePos> beats = m_pPlugin->getBeats();
        QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
                m_pluginId, m_bPreferencesFastAnalysis);
        pBeats = BeatFactory::makePreferredBeats(
                beats,
                extraVersionInfo,
                m_bPreferencesFixedTempo,
                m_sampleRate);
        qDebug() << "AnalyzerBeats plugin detected" << beats.size()
                 << "beats. Predominant BPM:"
                 << (pBeats ? pBeats->getBpmInRange(
                                      mixxx::audio::kStartFramePos,
                                      mixxx::audio::FramePos{
                                              pTrack->getDuration() *
                                              pBeats->getSampleRate()})
                            : mixxx::Bpm());
    } else {
        mixxx::Bpm bpm = m_pPlugin->getBpm();
        qDebug() << "AnalyzerBeats plugin detected constant BPM: " << bpm;
        pBeats = mixxx::Beats::fromConstTempo(m_sampleRate, mixxx::audio::kStartFramePos, bpm);
    }

    pTrack->trySetBeats(pBeats);

    // BPM SEGMENTS -> JSON
    auto* pExtendedPlugin = dynamic_cast<mixxx::AnalyzerQueenMaryBeatsExtended*>(m_pPlugin.get());
    if (pExtendedPlugin) {
        QJsonArray segmentsArray = pExtendedPlugin->getBpmSegmentsJson();

        if (!segmentsArray.isEmpty()) {
            saveBpmSegmentsJson(pTrack, segmentsArray);
            // saveBpmSegments2DB(pTrack, segmentsArray);
        }
    }
}

// Save the segments to JSON
bool AnalyzerBeats::saveBpmSegmentsJson(TrackPointer pTrack, const QJsonArray& segmentsArray) {
    QString trackIdStr = pTrack->getId().toString();
    if (trackIdStr.isEmpty()) {
        qDebug() << "[AnalyzerBeats] No valid track ID, skipping JSON save";
        return false;
    }

    QJsonObject root;
    root["bpm_curve"] = segmentsArray;

    QJsonObject trackInfo;
    trackInfo["title"] = pTrack->getTitle();
    trackInfo["artist"] = pTrack->getArtist();
    trackInfo["album"] = pTrack->getAlbum();
    trackInfo["duration_seconds"] = pTrack->getDuration();
    trackInfo["offset_seconds"] = 0.0;
    trackInfo["track_id"] = trackIdStr;

    root["track"] = trackInfo;

    QString bpmDir = QStandardPaths::writableLocation(
                             QStandardPaths::AppLocalDataLocation) +
            "/bpmcurve/";

    QDir dir;
    if (!dir.exists(bpmDir)) {
        dir.mkpath(bpmDir);
    }

    QString jsonPath = bpmDir + trackIdStr + ".json";

    QFile file(jsonPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "[AnalyzerBeats] Saved BPM curve JSON to:" << jsonPath;
        qDebug() << "[AnalyzerBeats] Segments:" << segmentsArray.size();
        return true;
    } else {
        qDebug() << "[AnalyzerBeats] Failed to save BPM curve JSON to:" << jsonPath;
        return false;
    }
}

// static
QHash<QString, QString> AnalyzerBeats::getExtraVersionInfo(
        const QString& pluginId, bool bPreferencesFastAnalysis) {
    QHash<QString, QString> extraVersionInfo;
    extraVersionInfo["vamp_plugin_id"] = pluginId;
    if (bPreferencesFastAnalysis) {
        extraVersionInfo["fast_analysis"] = "1";
    }
    return extraVersionInfo;
}

// bool AnalyzerBeats::saveBpmSegments2DB(TrackPointer pTrack, const QJsonArray&
// segmentsArray) {
//     if (!pTrack) {
//         qDebug() << "[AnalyzerBeats] No track to save BPM segments to
//         database"; return false;
//     }
//
//     TrackId trackId = pTrack->getId();
//     if (!trackId.isValid()) {
//         qDebug() << "[AnalyzerBeats] No valid track ID";
//         return false;
//     }
//
//     if (segmentsArray.isEmpty()) {
//         qDebug() << "[AnalyzerBeats] No segments to save";
//         return false;
//     }
//
//     qDebug() << "[AnalyzerBeats] Saving" << segmentsArray.size() << "BPM
//     segments to database for track:" << pTrack->getTitle();
//
//     // Convert JSON to BPMSegmentData list (for database)
//     QVector<mixxx::BPMSegmentData> dbSegments;
//     double sampleRate = m_sampleRate;
//
//     for (const QJsonValue& val : segmentsArray) {
//         if (!val.isObject())
//             continue;
//
//         QJsonObject obj = val.toObject();
//
//         mixxx::BPMSegmentData seg;
//         seg.startPosition = mixxx::audio::FramePos(obj["position"].toDouble()
//         * sampleRate); seg.endPosition =
//         mixxx::audio::FramePos(obj["range_end"].toDouble() * sampleRate);
//         seg.startBpm = obj["bpm_start"].toDouble();
//         seg.endBpm = obj["bpm_end"].toDouble();
//         seg.type = obj["type"].toString();
//
//         dbSegments.append(seg);
//     }
//
//     // TODO: Save to database via TrackCollection
//     // When we have access to TrackCollection, we'll do:
//     // TrackCollection* pCollection = ...; // Get from somewhere
//     // BPMSegmentDAO& dao = pCollection->getBPMSegmentDAO();
//     // dao.saveTrackSegments(trackId, dbSegments);
//
//     qDebug() << "[AnalyzerBeats] Converted" << dbSegments.size() << "segments
//     for database storage";
//
//     return true;
// }
