#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <vector>

#include "analyzer/plugins/analyzerplugin.h"
#include "analyzer/plugins/buffering_utils.h"
#include "track/keys.h"
#include "track/keyutils.h"

using mixxx::track::io::key::ChromaticKey;

class GetKeyMode;

namespace mixxx {

struct KeyDetectionResult {
    ChromaticKey key;
    double timeSeconds;
    double confidence;
};

struct AnalysisKeySegment {
    double startTime;
    double endTime;
    double duration;
    QString key;
    QString type;
    double confidence;

    AnalysisKeySegment()
            : startTime(0),
              endTime(0),
              duration(0),
              confidence(0) {
    }
};

class AnalyzerQueenMaryKeyExtended : public AnalyzerKeyPlugin {
  public:
    static AnalyzerPluginInfo pluginInfo();

    AnalyzerQueenMaryKeyExtended();
    ~AnalyzerQueenMaryKeyExtended() override;

    AnalyzerPluginInfo info() const override;
    bool initialize(mixxx::audio::SampleRate sampleRate) override;
    bool processSamples(const CSAMPLE* pIn, SINT iLen) override;
    bool finalize() override;
    KeyChangeList getKeyChanges() const override;

    QVector<AnalysisKeySegment> getKeySegments() const;
    QJsonArray getKeySegmentsJson() const;

  private:
    void buildKeySegments();
    void smoothKeyResults();
    QString keyToString(int key) const;
    QString keyToStringRaw(int key) const; // For debugging only
    double calculateConfidence(double* keyStrengths, int detectedKeyIndex) const;

    std::unique_ptr<GetKeyMode> m_pKeyMode;
    DownmixAndOverlapHelper m_helper;
    size_t m_currentFrame;
    double m_sampleRate;
    KeyChangeList m_resultKeys;
    std::vector<KeyDetectionResult> m_keyResults;
    double m_trackDuration;
    QVector<AnalysisKeySegment> m_keySegments;
};

} // namespace mixxx

//
// #pragma once
//
// #include <QJsonArray>
// #include <QJsonObject>
// #include <memory>
// #include <vector>
//
// #include "analyzer/plugins/analyzerplugin.h"
// #include "analyzer/plugins/buffering_utils.h"
// #include "track/keys.h"
// #include "track/keyutils.h"
//
//// Add this using statement
// using mixxx::track::io::key::ChromaticKey;
//
// class GetKeyMode;
//
// namespace mixxx {
//
// struct KeyDetectionResult {
//     ChromaticKey key;
//     double timeSeconds;
//     double confidence;
// };
//
// struct AnalysisKeySegment {
//     double startTime;
//     double endTime;
//     double duration;
//     QString key;
//     QString type;
//     double confidence;
//
//     AnalysisKeySegment()
//             : startTime(0),
//               endTime(0),
//               duration(0),
//               confidence(0) {
//     }
// };
//
// class AnalyzerQueenMaryKeyExtended : public AnalyzerKeyPlugin {
//   public:
//     static AnalyzerPluginInfo pluginInfo();
//
//     AnalyzerQueenMaryKeyExtended();
//     ~AnalyzerQueenMaryKeyExtended() override;
//
//     AnalyzerPluginInfo info() const override;
//     bool initialize(mixxx::audio::SampleRate sampleRate) override;
//     bool processSamples(const CSAMPLE* pIn, SINT iLen) override;
//     bool finalize() override;
//     KeyChangeList getKeyChanges() const override;
//
//     QVector<AnalysisKeySegment> getKeySegments() const;
//     QJsonArray getKeySegmentsJson() const;
//
//   private:
//     void buildKeySegments();
//     void smoothKeyResults();
//     QString keyToString(int key) const;
//     double calculateConfidence(double* keyStrengths, int detectedKeyIndex) const;
//     int applyKeyOffset(int keyIndex) const;
//
//     std::unique_ptr<GetKeyMode> m_pKeyMode;
//     DownmixAndOverlapHelper m_helper;
//     size_t m_currentFrame;
//     double m_sampleRate;
//     KeyChangeList m_resultKeys;
//     std::vector<KeyDetectionResult> m_keyResults; // Store results with confidence
//     double m_trackDuration;
//     QVector<AnalysisKeySegment> m_keySegments;
//
//     static constexpr int KEY_OFFSET_CORRECTION = 0; // Shift up by 1 semitone
//     static constexpr int SMOOTHING_WINDOW_SIZE = 5; // Median filter window size
//     void debugKeyMapping();
//     };
//
// } // namespace mixxx

// #pragma once
//
// #include <QJsonArray>
// #include <QJsonObject>
// #include <memory>
// #include <vector>
//
// #include "analyzer/plugins/analyzerplugin.h"
// #include "analyzer/plugins/buffering_utils.h"
// #include "track/keys.h"
// #include "track/keyutils.h"
//
//// Add this using statement
// using mixxx::track::io::key::ChromaticKey;
//
// class GetKeyMode;
//
// namespace mixxx {
//
// struct KeyDetectionResult {
//     ChromaticKey key;
//     double timeSeconds;
//     double confidence;
// };
//
// struct AnalysisKeySegment {
//     double startTime;
//     double endTime;
//     double duration;
//     QString key;
//     QString type;
//     double confidence;
//
//     AnalysisKeySegment()
//             : startTime(0),
//               endTime(0),
//               duration(0),
//               confidence(0) {
//     }
// };
//
// class AnalyzerQueenMaryKeyExtended : public AnalyzerKeyPlugin {
//   public:
//     static AnalyzerPluginInfo pluginInfo();
//
//     AnalyzerQueenMaryKeyExtended();
//     ~AnalyzerQueenMaryKeyExtended() override;
//
//     AnalyzerPluginInfo info() const override;
//     bool initialize(mixxx::audio::SampleRate sampleRate) override;
//     bool processSamples(const CSAMPLE* pIn, SINT iLen) override;
//     bool finalize() override;
//     KeyChangeList getKeyChanges() const override;
//
//     QVector<AnalysisKeySegment> getKeySegments() const;
//     QJsonArray getKeySegmentsJson() const;
//
//   private:
//     void buildKeySegments();
//     QString keyToString(int key) const;
//
//     std::unique_ptr<GetKeyMode> m_pKeyMode;
//     DownmixAndOverlapHelper m_helper;
//     size_t m_currentFrame;
//     double m_sampleRate;
//     KeyChangeList m_resultKeys;
//     std::vector<KeyDetectionResult> m_keyResults; // Store results with confidence
//     double m_trackDuration;
//     QVector<AnalysisKeySegment> m_keySegments;
// };
//
// } // namespace mixxx

// #pragma once
//
// #include <QJsonArray>
// #include <QJsonObject>
// #include <memory>
// #include <vector>
//
// #include "analyzer/plugins/analyzerplugin.h"
// #include "analyzer/plugins/buffering_utils.h"
// #include "track/keys.h"
//
// class GetKeyMode;
//
// namespace mixxx {
//
// struct AnalysisKeySegment {
//     double startTime;
//     double endTime;
//     double duration;
//     QString key;
//     QString type;
//     double confidence;
//
//     AnalysisKeySegment()
//             : startTime(0),
//               endTime(0),
//               duration(0),
//               confidence(0) {
//     }
// };
//
// struct KeyDetectionResult {
//     ChromaticKey key;
//     double timeSeconds;
//     double confidence;
//     double strengths[24]; // Optional: store all strengths for debugging
// };
//
// class AnalyzerQueenMaryKeyExtended : public AnalyzerKeyPlugin {
//   public:
//     static AnalyzerPluginInfo pluginInfo();
//
//     AnalyzerQueenMaryKeyExtended();
//     ~AnalyzerQueenMaryKeyExtended() override;
//
//     AnalyzerPluginInfo info() const override;
//     bool initialize(mixxx::audio::SampleRate sampleRate) override;
//     bool processSamples(const CSAMPLE* pIn, SINT iLen) override;
//     bool finalize() override;
//     KeyChangeList getKeyChanges() const override;
//
//     QVector<AnalysisKeySegment> getKeySegments() const;
//     QJsonArray getKeySegmentsJson() const;
//
//   private:
//     void buildKeySegments();
//     QString keyToString(int key) const;
//
//     std::unique_ptr<GetKeyMode> m_pKeyMode;
//     DownmixAndOverlapHelper m_helper;
//     size_t m_currentFrame;
//     double m_sampleRate;
//     KeyChangeList m_resultKeys;
//     double m_trackDuration;
//     QVector<AnalysisKeySegment> m_keySegments;
//     std::vector<KeyDetectionResult> m_keyResults;
// };
//
// } // namespace mixxx

// #pragma once
//
// #include <QJsonArray>
// #include <QJsonObject>
// #include <QObject>
// #include <memory>
// #include <vector>
//
// #include "analyzer/plugins/analyzerplugin.h"
// #include "analyzer/plugins/buffering_utils.h"
// #include "track/keys.h"
// #include "track/keyutils.h"
//
// class GetKeyMode;
//
// namespace mixxx {
//
// struct AnalysisKeySegment {
//     double startTime;
//     double endTime;
//     double duration;
//     QString key;     // Human readable name (e.g., "C major")
//     QString keyCode; // Open Key code (e.g., "5d")
//     QString type;    // "STABLE" or "MODULATION"
//     QString startKey;
//     QString endKey;
//
//     AnalysisKeySegment()
//             : startTime(0),
//               endTime(0),
//               duration(0) {
//     }
// };
//
// class AnalyzerQueenMaryKeyExtended : public AnalyzerKeyPlugin {
//   public:
//     static AnalyzerPluginInfo pluginInfo() {
//         return AnalyzerPluginInfo(
//                 "qm-keydetector-extended:0",
//                 QObject::tr("Queen Mary University London (Extended)"),
//                 QObject::tr("Queen Mary Key Detector with Key Change Detection"),
//                 false);
//     }
//
//     AnalyzerQueenMaryKeyExtended();
//     ~AnalyzerQueenMaryKeyExtended() override;
//
//     AnalyzerPluginInfo info() const override {
//         return pluginInfo();
//     }
//
//     bool initialize(mixxx::audio::SampleRate sampleRate) override;
//     bool processSamples(const CSAMPLE* pIn, SINT iLen) override;
//     bool finalize() override;
//
//     KeyChangeList getKeyChanges() const override {
//         return m_resultKeys;
//     }
//
//     QVector<AnalysisKeySegment> getKeySegments() const {
//         return m_keySegments;
//     }
//
//     QJsonArray getKeySegmentsJson() const;
//
//   private:
//     void analyzeKeyChanges();
//     void snapSegmentsToBeats();
//     double findNearestBeat(double time, const std::vector<double>& beatTimes);
//     QString keyToString(track::io::key::ChromaticKey key) const;
//
//     std::unique_ptr<GetKeyMode> m_pKeyMode;
//     DownmixAndOverlapHelper m_helper;
//     size_t m_currentFrame;
//     KeyChangeList m_resultKeys; // Original key changes (time, key)
//
//     // Extended key change detection
//     std::vector<double> m_beatTimes;
//     mixxx::audio::SampleRate m_sampleRate;
//     double m_trackDuration;
//     QVector<AnalysisKeySegment> m_keySegments;
//
//     static constexpr int kWindowSizeFrames = 512;
//     static constexpr int kStepSizeFrames = 128;
//     static constexpr double kMinKeyChangeValue = 2.0;
//     static constexpr double kMinChangeDuration = 8.0;
//     static constexpr double kMaxSnapDistance = 0.3;
// };
//
// } // namespace mixxx
