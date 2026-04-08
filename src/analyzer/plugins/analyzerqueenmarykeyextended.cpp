
#include "analyzer/plugins/analyzerqueenmarykeyextended.h"

#include <dsp/keydetection/GetKeyMode.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

#include "analyzer/constants.h"
#include "track/keyutils.h"
#include "util/assert.h"

using mixxx::track::io::key::ChromaticKey;

namespace mixxx {
namespace {

constexpr int kTuningFrequencyHertz = 440;

}

AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::pluginInfo() {
    return AnalyzerPluginInfo(
            "qm-keydetector-extended:0",
            QObject::tr("Queen Mary University London (Extended)"),
            QObject::tr("Queen Mary Key Detector with Key Change Detection"),
            false);
}

AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
        : m_currentFrame(0),
          m_sampleRate(44100.0),
          m_trackDuration(0.0) {
}

AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;

AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::info() const {
    return pluginInfo();
}

bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate sampleRate) {
    m_sampleRate = sampleRate.toDouble();
    m_resultKeys.clear();
    m_keyResults.clear();
    m_keySegments.clear();
    m_currentFrame = 0;

    GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
    m_pKeyMode = std::make_unique<GetKeyMode>(config);
    size_t windowSize = m_pKeyMode->getBlockSize();
    size_t stepSize = m_pKeyMode->getHopSize();

    qDebug() << "[QueenMaryKeyExtended] Initialized with sample rate:" << sampleRate
             << "Window size:" << windowSize << "Step size:" << stepSize;

    // return m_helper.initialize(
    //         windowSize, stepSize, [this](double* pWindow, size_t) {
    //             // Process the window
    //             int iKey = m_pKeyMode->process(pWindow);
    //             double* keyStrengths = m_pKeyMode->getKeyStrengths();

    //            // Find the key with the highest strength (more reliable than process() return)
    //            int bestKeyIndex = 0;
    //            double bestStrength = keyStrengths[0];
    //            for (int j = 1; j < 24; ++j) {
    //                if (keyStrengths[j] > bestStrength) {
    //                    bestStrength = keyStrengths[j];
    //                    bestKeyIndex = j;
    //                }
    //            }

    //            // Fix the key offset: detector outputs +1 semitone too high
    //            // Detector index 6 (F#) should be Mixxx index 5 (F)
    //            int correctedKeyIndex = (bestKeyIndex - 1 + 24) % 24;
    //            ChromaticKey key = static_cast<ChromaticKey>(correctedKeyIndex);
    //            double timeSeconds = static_cast<double>(m_currentFrame) / m_sampleRate;

    //            // Calculate confidence using original key strengths (before correction)
    //            double confidence = calculateConfidence(keyStrengths, bestKeyIndex);

    //            // Debug output for first few frames
    //            static int debugCount = 0;
    //            if (debugCount < 5) {
    //                qDebug() << "[QueenMaryKeyExtended] Frame" << debugCount
    //                         << "Raw key index:" << bestKeyIndex
    //                         << "(" << keyToStringRaw(bestKeyIndex) << ")"
    //                         << "Strength:" << bestStrength
    //                         << "Corrected key index:" << correctedKeyIndex
    //                         << "(" << keyToString(correctedKeyIndex) << ")"
    //                         << "Confidence:" << confidence;
    //                debugCount++;
    //            }

    //            // Store result with confidence
    //            KeyDetectionResult result;
    //            result.key = key;
    //            result.timeSeconds = timeSeconds;
    //            result.confidence = confidence;
    //            m_keyResults.push_back(result);

    //            // Also store in original format for compatibility
    //            m_resultKeys.push_back(qMakePair(key, static_cast<double>(m_currentFrame)));

    //            return true;
    //        });

    return m_helper.initialize(
            windowSize, stepSize, [this](double* pWindow, size_t) {
                int iKey = m_pKeyMode->process(pWindow);
                double* keyStrengths = m_pKeyMode->getKeyStrengths();

                // Find the key with the highest strength
                int bestKeyIndex = 0;
                double bestStrength = keyStrengths[0];
                for (int j = 1; j < 24; ++j) {
                    if (keyStrengths[j] > bestStrength) {
                        bestStrength = keyStrengths[j];
                        bestKeyIndex = j;
                    }
                }

                ChromaticKey key = static_cast<ChromaticKey>(iKey);
                double timeSeconds = static_cast<double>(m_currentFrame) / m_sampleRate;

                // Calculate confidence using original key strengths
                double confidence = calculateConfidence(keyStrengths, bestKeyIndex);

                //// Debug output
                // static int debugCount = 0;
                // if (debugCount < 5) {
                //     qDebug() << "[QueenMaryKeyExtended] Frame" << debugCount
                //              << "Raw key index:" << bestKeyIndex
                //              << "Confidence:" << confidence;
                //     debugCount++;
                // }

                // Store result
                KeyDetectionResult result;
                result.key = key;
                result.timeSeconds = timeSeconds;
                result.confidence = confidence;
                m_keyResults.push_back(result);
                m_resultKeys.push_back(qMakePair(key, static_cast<double>(m_currentFrame)));

                return true;
            });
}

bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT iLen) {
    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
    if (!m_pKeyMode) {
        return false;
    }
    const size_t numInputFrames = iLen / kAnalysisChannels;
    m_currentFrame += numInputFrames;
    return m_helper.processStereoSamples(pIn, iLen);
}

bool AnalyzerQueenMaryKeyExtended::finalize() {
    m_helper.finalize();
    m_trackDuration = static_cast<double>(m_currentFrame) / m_sampleRate;

    // Apply smoothing to reduce noise
    smoothKeyResults();

    buildKeySegments();

    // Log summary statistics
    qDebug() << "[QueenMaryKeyExtended] Finalized. Duration:" << m_trackDuration << "s"
             << "Key results:" << m_keyResults.size()
             << "Segments:" << m_keySegments.size();

    // Calculate key distribution for debugging
    std::map<QString, double> keyDuration;
    for (const auto& seg : m_keySegments) {
        keyDuration[seg.key] += seg.duration;
    }

    if (!keyDuration.empty()) {
        qDebug() << "[QueenMaryKeyExtended] Key distribution:";
        for (const auto& pair : keyDuration) {
            if (pair.second > 0.1) {
                qDebug() << "  " << pair.first << ":" << pair.second << "s";
            }
        }
    }

    return true;
}

KeyChangeList AnalyzerQueenMaryKeyExtended::getKeyChanges() const {
    return m_resultKeys;
}

double AnalyzerQueenMaryKeyExtended::calculateConfidence(
        double* keyStrengths, int detectedKeyIndex) const {
    if (!keyStrengths) {
        return 50.0;
    }

    double best = keyStrengths[detectedKeyIndex];
    double sum = 0.0;
    double secondBest = 0.0;

    for (int i = 0; i < 24; ++i) {
        sum += keyStrengths[i];
        if (i != detectedKeyIndex && keyStrengths[i] > secondBest) {
            secondBest = keyStrengths[i];
        }
    }

    double avg = sum / 24.0;
    double peakToAvg = best / (avg + 0.0001);
    double peakToSecond = best / (secondBest + 0.0001);

    // Combined metric: higher confidence when both ratios are good
    double confidence;
    if (peakToSecond > 1.5 && peakToAvg > 2.0) {
        confidence = 80.0 + std::min(20.0, (peakToSecond - 1.5) * 40.0);
    } else if (peakToSecond > 1.2) {
        confidence = 60.0 + (peakToSecond - 1.2) * 66.7;
    } else if (peakToSecond > 1.05) {
        confidence = 50.0 + (peakToSecond - 1.05) * 200.0;
    } else {
        confidence = 50.0;
    }

    return qBound(0.0, confidence, 100.0);
}

void AnalyzerQueenMaryKeyExtended::smoothKeyResults() {
    if (m_keyResults.size() < 3) {
        return;
    }

    const int SMOOTHING_WINDOW_SIZE = 5;
    std::vector<KeyDetectionResult> smoothed;
    smoothed.reserve(m_keyResults.size());

    for (size_t i = 0; i < m_keyResults.size(); ++i) {
        // Collect keys in window
        std::map<ChromaticKey, int> frequency;
        int start = std::max(0, static_cast<int>(i) - SMOOTHING_WINDOW_SIZE / 2);
        int end = std::min(static_cast<int>(m_keyResults.size()) - 1,
                static_cast<int>(i) + SMOOTHING_WINDOW_SIZE / 2);

        for (int j = start; j <= end; ++j) {
            frequency[m_keyResults[j].key]++;
        }

        // Find most frequent key in window
        ChromaticKey mostFrequentKey = m_keyResults[i].key;
        int maxCount = 0;
        for (const auto& pair : frequency) {
            if (pair.second > maxCount) {
                maxCount = pair.second;
                mostFrequentKey = pair.first;
            }
        }

        // Create smoothed result
        KeyDetectionResult smoothedResult = m_keyResults[i];
        smoothedResult.key = mostFrequentKey;
        smoothed.push_back(smoothedResult);
    }

    m_keyResults = smoothed;
    qDebug() << "[QueenMaryKeyExtended] Applied smoothing with window size:"
             << SMOOTHING_WINDOW_SIZE;
}

void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
    if (m_keyResults.empty()) {
        qDebug() << "[QueenMaryKeyExtended] No key results to build segments";
        return;
    }

    m_keySegments.clear();

    double lastTime = 0.0;
    ChromaticKey lastKey = m_keyResults[0].key;
    double confidenceSum = m_keyResults[0].confidence;
    int confidenceCount = 1;

    for (size_t i = 1; i < m_keyResults.size(); ++i) {
        double currentTime = m_keyResults[i].timeSeconds;
        ChromaticKey currentKey = m_keyResults[i].key;
        double currentConfidence = m_keyResults[i].confidence;

        if (currentKey != lastKey) {
            // End current segment
            AnalysisKeySegment seg;
            seg.startTime = lastTime;
            seg.endTime = currentTime;
            seg.duration = currentTime - lastTime;
            seg.key = keyToString(static_cast<int>(lastKey));
            seg.type = "STABLE";
            seg.confidence = confidenceSum / confidenceCount;

            // Only add segments with minimum duration
            if (seg.duration > 0.1) {
                m_keySegments.append(seg);

                qDebug() << "[QueenMaryKeyExtended] Segment:" << seg.startTime << "-" << seg.endTime
                         << "Key:" << seg.key << "Confidence:" << seg.confidence
                         << "Duration:" << seg.duration;
            }

            // Start new segment
            lastTime = currentTime;
            lastKey = currentKey;
            confidenceSum = currentConfidence;
            confidenceCount = 1;
        } else {
            confidenceSum += currentConfidence;
            confidenceCount++;
        }
    }

    // Add final segment
    if (lastTime < m_trackDuration) {
        AnalysisKeySegment seg;
        seg.startTime = lastTime;
        seg.endTime = m_trackDuration;
        seg.duration = m_trackDuration - lastTime;
        seg.key = keyToString(static_cast<int>(lastKey));
        seg.type = "STABLE";
        seg.confidence = confidenceSum / confidenceCount;

        if (seg.duration > 0.1) {
            m_keySegments.append(seg);
            qDebug() << "[QueenMaryKeyExtended] Final segment:" << seg.startTime
                     << "-" << seg.endTime << "Key:" << seg.key
                     << "Confidence:" << seg.confidence
                     << "Duration:" << seg.duration;
        }
    }
}

// QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
//     if (key < 0 || key >= 24) {
//         qWarning() << "[QueenMaryKeyExtended] Invalid key index:" << key;
//         return QString("INVALID_%1").arg(key);
//     }
//     return KeyUtils::keyToString(static_cast<ChromaticKey>(key));
// }

QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
    if (key < 0 || key >= 24) {
        qWarning() << "[QueenMaryKeyExtended] Invalid key index:" << key;
        return QString("INVALID_%1").arg(key);
    }

    QString result = KeyUtils::keyToString(static_cast<ChromaticKey>(key));

    // Replace Unicode flat (U+266D) with ASCII 'b' (U+0062)
    result.replace(QChar(0x266D), 'b');
    // Replace Unicode sharp (U+266F) with ASCII '#' (U+0023)
    result.replace(QChar(0x266F), '#');
    // Replace Unicode natural (U+266E) with empty string
    result.replace(QChar(0x266E), "");

    return result;
}

// QString AnalyzerQueenMaryKeyExtended::keyToStringRaw(int key) const {
//     // This is for debugging only - shows raw detector key names
//     if (key < 0 || key >= 24) {
//         return QString("INVALID_%1").arg(key);
//     }
//
//     // Temporary mapping for debug output
//     const char* rawKeyNames[24] = {
//             "B", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab",
//             "A", "A#/Bb", "Bm", "Cm", "C#m/Dbm", "Dm", "D#m/Ebm", "Em", "Fm",
//             "F#m/Gbm", "Gm", "G#m/Abm", "Am", "A#m/Bbm"};
//     return QString(rawKeyNames[key]);
// }

QString AnalyzerQueenMaryKeyExtended::keyToStringRaw(int key) const {
    if (key < 0 || key >= 24) {
        return QString("INVALID_%1").arg(key);
    }

    // Use ASCII 'b' and '#'
    const char* rawKeyNames[24] = {"B",
            "C",
            "C#/Db",
            "D",
            "D#/Eb",
            "E",
            "F",
            "F#/Gb",
            "G",
            "G#/Ab",
            "A",
            "A#/Bb",
            "Bm",
            "Cm",
            "C#m/Dbm",
            "Dm",
            "D#m/Ebm",
            "Em",
            "Fm",
            "F#m/Gbm",
            "Gm",
            "G#m/Abm",
            "Am",
            "A#m/Bbm"};

    return QString(rawKeyNames[key]);
}

QVector<AnalysisKeySegment> AnalyzerQueenMaryKeyExtended::getKeySegments() const {
    return m_keySegments;
}

QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
    QJsonArray segmentsArray;
    int segmentId = 1;
    for (const auto& seg : m_keySegments) {
        if (seg.duration < 0.001)
            continue;
        QJsonObject segmentObj;
        segmentObj["id"] = segmentId++;
        segmentObj["type"] = seg.type;
        segmentObj["position"] = seg.startTime;
        segmentObj["duration"] = seg.duration;
        segmentObj["key"] = seg.key;
        segmentObj["confidence"] = seg.confidence;
        segmentObj["range_start"] = seg.startTime;
        segmentObj["range_end"] = seg.endTime;
        segmentsArray.append(segmentObj);
    }
    return segmentsArray;
}

} // namespace mixxx

//
// #include "analyzer/plugins/analyzerqueenmarykeyextended.h"
//
// #include <dsp/keydetection/GetKeyMode.h>
//
// #include <algorithm>
// #include <cmath>
// #include <map>
// #include <numeric>
// #include <utility>
// #include <vector>
//
// #include "analyzer/constants.h"
// #include "track/keyutils.h"
// #include "util/assert.h"
//
// using mixxx::track::io::key::ChromaticKey;
//
// namespace mixxx {
// namespace {
//
// constexpr int kTuningFrequencyHertz = 440;
//
//}
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::pluginInfo() {
//    return AnalyzerPluginInfo(
//            "qm-keydetector-extended:0",
//            QObject::tr("Queen Mary University London (Extended)"),
//            QObject::tr("Queen Mary Key Detector with Key Change Detection"),
//            false);
//}
//
// AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
//        : m_currentFrame(0),
//          m_sampleRate(44100.0),
//          m_trackDuration(0.0) {
//}
//
// AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::info() const {
//    return pluginInfo();
//}
//
////bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
/// sampleRate) { /    m_sampleRate = sampleRate.toDouble(); /
/// m_resultKeys.clear(); /    m_keyResults.clear(); /    m_keySegments.clear();
////    m_currentFrame = 0;
////
////    GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
////    m_pKeyMode = std::make_unique<GetKeyMode>(config);
////    size_t windowSize = m_pKeyMode->getBlockSize();
////    size_t stepSize = m_pKeyMode->getHopSize();
////
////    qDebug() << "[QueenMaryKeyExtended] Initialized with sample rate:" <<
/// sampleRate /             << "Window size:" << windowSize << "Step size:" <<
/// stepSize /             << "Key offset correction:" << KEY_OFFSET_CORRECTION
///<< "semitone(s)";
////
////    return m_helper.initialize(
////            windowSize, stepSize, [this](double* pWindow, size_t) {
////                // Process the window
////                int iKey = m_pKeyMode->process(pWindow);
////                double* keyStrengths = m_pKeyMode->getKeyStrengths();
////
////                // Find the key with the highest strength (more reliable
/// than process() return) /                int bestKeyIndex = 0; / double
/// bestStrength = keyStrengths[0]; /                for (int j = 1; j < 24; ++j)
///{ /                    if (keyStrengths[j] > bestStrength) { / bestStrength =
/// keyStrengths[j]; /                        bestKeyIndex = j; / } / }
////
////                // Apply offset correction to fix the semitone offset issue
////                int correctedKeyIndex = applyKeyOffset(bestKeyIndex);
////                ChromaticKey key =
/// static_cast<ChromaticKey>(correctedKeyIndex); /                double
/// timeSeconds = static_cast<double>(m_currentFrame) / m_sampleRate;
////
////                // Calculate confidence using ratio method
////                double confidence = calculateConfidence(keyStrengths,
/// bestKeyIndex);
////
////                // Debug output for first few frames
////                static int debugCount = 0;
////                if (debugCount < 5) {
////                    qDebug() << "[QueenMaryKeyExtended] Frame" << debugCount
////                             << "Original key index:" << bestKeyIndex
////                             << "(" << keyToString(bestKeyIndex) << ")"
////                             << "Strength:" << bestStrength
////                             << "Corrected key index:" << correctedKeyIndex
////                             << "(" << keyToString(correctedKeyIndex) << ")"
////                             << "Confidence:" << confidence;
////
////                    // Print top 5 strengths for debugging
////                    std::vector<std::pair<double, int>> sorted;
////                    for (int j = 0; j < 24; ++j) {
////                        sorted.push_back(std::make_pair(keyStrengths[j],
/// j)); /                    } /                    std::sort(sorted.begin(),
/// sorted.end(), [](const auto& a, const auto& b) { return a.first > b.first;
///});
////
////                    qDebug() << "  Top 5 strengths:";
////                    for (int k = 0; k < 5 && k < (int)sorted.size(); ++k) {
////                        int correctedIdx = applyKeyOffset(sorted[k].second);
////                        qDebug() << "    " << k + 1 << ": Original index" <<
/// sorted[k].second /                                 << "(" <<
/// keyToString(sorted[k].second) << ")" /                                 << "->
/// Corrected" << correctedIdx /                                 << "(" <<
/// keyToString(correctedIdx) << ")" /                                 << "=" <<
/// sorted[k].first; /                    } /                    debugCount++; /
///}
////
////                // Store result with confidence
////                KeyDetectionResult result;
////                result.key = key;
////                result.timeSeconds = timeSeconds;
////                result.confidence = confidence;
////                m_keyResults.push_back(result);
////
////                // Also store in original format for compatibility
////                m_resultKeys.push_back(qMakePair(key,
/// static_cast<double>(m_currentFrame)));
////
////                return true;
////            });
////}
//
// bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
// sampleRate) {
//    m_sampleRate = sampleRate.toDouble();
//    m_resultKeys.clear();
//    m_keyResults.clear();
//    m_keySegments.clear();
//    m_currentFrame = 0;
//
//    GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
//    m_pKeyMode = std::make_unique<GetKeyMode>(config);
//    size_t windowSize = m_pKeyMode->getBlockSize();
//    size_t stepSize = m_pKeyMode->getHopSize();
//
//    qDebug() << "[QueenMaryKeyExtended] Initialized with sample rate:" <<
//    sampleRate
//             << "Window size:" << windowSize << "Step size:" << stepSize
//             << "Key offset correction:" << KEY_OFFSET_CORRECTION <<
//             "semitone(s)";
//
//    return m_helper.initialize(
//            windowSize, stepSize, [this](double* pWindow, size_t windowSize) {
//                // Process the window
//                int iKey = m_pKeyMode->process(pWindow);
//                double* keyStrengths = m_pKeyMode->getKeyStrengths();
//
//                // Find the key with the highest strength (more reliable than
//                process() return) int bestKeyIndex = 0; double bestStrength =
//                keyStrengths[0]; for (int j = 1; j < 24; ++j) {
//                    if (keyStrengths[j] > bestStrength) {
//                        bestStrength = keyStrengths[j];
//                        bestKeyIndex = j;
//                    }
//                }
//
//                double timeSeconds = static_cast<double>(m_currentFrame) /
//                m_sampleRate;
//
//                //
//                ============================================================
//                // ENHANCED DEBUGGING FOR "HEAL THE WORLD"
//                //
//                ============================================================
//                static int debugFrameCount = 0;
//                debugFrameCount++;
//
//                // Log every 50 frames (about every 1-2 seconds depending on
//                hop size) if (debugFrameCount % 50 == 0 || debugFrameCount <
//                10) {
//                    qDebug() << "========================================";
//                    qDebug() << "[QueenMaryKeyExtended] Frame" <<
//                    debugFrameCount
//                             << "at time:" << timeSeconds << "s";
//                    qDebug() << "  Raw best key index:" << bestKeyIndex
//                             << "(" << keyToString(bestKeyIndex) << ")"
//                             << "Strength:" << bestStrength;
//
//                    // Collect and sort all key strengths
//                    std::vector<std::pair<double, int>> sorted;
//                    for (int j = 0; j < 24; ++j) {
//                        sorted.push_back(std::make_pair(keyStrengths[j], j));
//                    }
//                    std::sort(sorted.begin(), sorted.end(), [](const auto& a,
//                    const auto& b) { return a.first > b.first; });
//
//                    // Show top 8 strengths
//                    qDebug() << "  Top 8 key strengths (original indices):";
//                    for (int k = 0; k < 8 && k < (int)sorted.size(); ++k) {
//                        int idx = sorted[k].second;
//                        double strength = sorted[k].first;
//                        int correctedIdx = applyKeyOffset(idx);
//                        qDebug() << "    #" << (k + 1) << ": Original index"
//                        << idx
//                                 << "(" << keyToString(idx) << ")"
//                                 << "strength=" << QString::number(strength,
//                                 'f', 4)
//                                 << " -> Corrected index" << correctedIdx
//                                 << "(" << keyToString(correctedIdx) << ")";
//                    }
//
//                    // Special check for Db major (index 1) and A major (index
//                    9) qDebug() << "  Specific key strengths:"
//                             << "Db major (idx1)=" <<
//                             QString::number(keyStrengths[1], 'f', 4)
//                             << "A major (idx9)=" <<
//                             QString::number(keyStrengths[9], 'f', 4)
//                             << "D major (idx2)=" <<
//                             QString::number(keyStrengths[2], 'f', 4);
//                }
//
//                // Also log when we see significant strength in Db major but
//                choose something else if (keyStrengths[1] > 0.8 &&
//                bestKeyIndex != 1 && debugFrameCount % 20 == 0) {
//                    qDebug() << "[QueenMaryKeyExtended] WARNING: Db major
//                    strength ="
//                             << keyStrengths[1] << "but best is"
//                             << keyToString(bestKeyIndex) << "(" <<
//                             bestStrength << ")";
//                }
//                //
//                ============================================================
//
//                // Apply offset correction
//                //int correctedKeyIndex = applyKeyOffset(bestKeyIndex);
//                int correctedKeyIndex = (bestKeyIndex + 1) % 24; // +1 for
//                display ChromaticKey key =
//                static_cast<ChromaticKey>(correctedKeyIndex);
//
//                // Calculate confidence using ratio method
//                double confidence = calculateConfidence(keyStrengths,
//                bestKeyIndex);
//
//                // Store result with confidence
//                KeyDetectionResult result;
//                result.key = key;
//                result.timeSeconds = timeSeconds;
//                result.confidence = confidence;
//                m_keyResults.push_back(result);
//
//                // Also store in original format for compatibility
//                m_resultKeys.push_back(qMakePair(key,
//                static_cast<double>(m_currentFrame)));
//
//                debugKeyMapping();
//                return true;
//            });
//}
//
////void AnalyzerQueenMaryKeyExtended::debugKeyMapping() {
////    qDebug() << "[QueenMaryKeyExtended] Key index mapping (0-23):";
////    for (int i = 0; i < 24; ++i) {
////        QString keyName =
/// KeyUtils::keyToString(static_cast<ChromaticKey>(i)); /        qDebug() << "
/// Index" << i << ":" << keyName; /    }
////}
//
// void AnalyzerQueenMaryKeyExtended::debugKeyMapping() {
//    qDebug() << "[QueenMaryKeyExtended] Key index mapping (0-23) with display
//    offset +1:"; for (int i = 0; i < 24; ++i) {
//        int displayKey = (i + 1) % 24;
//        QString keyName =
//        KeyUtils::keyToString(static_cast<ChromaticKey>(displayKey)); qDebug()
//        << "  Detector index" << i << "-> Display index" << displayKey << ":"
//        << keyName;
//    }
//}
//
// bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT
// iLen) {
//    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//    if (!m_pKeyMode) {
//        return false;
//    }
//    const size_t numInputFrames = iLen / kAnalysisChannels;
//    m_currentFrame += numInputFrames;
//    return m_helper.processStereoSamples(pIn, iLen);
//}
//
// bool AnalyzerQueenMaryKeyExtended::finalize() {
//    m_helper.finalize();
//    m_trackDuration = static_cast<double>(m_currentFrame) / m_sampleRate;
//
//    // Apply smoothing to reduce noise
//    smoothKeyResults();
//
//    buildKeySegments();
//
//    // Log summary statistics
//    qDebug() << "[QueenMaryKeyExtended] Finalized. Duration:" <<
//    m_trackDuration << "s"
//             << "Key results:" << m_keyResults.size()
//             << "Segments:" << m_keySegments.size();
//
//    // Calculate key distribution for debugging
//    std::map<QString, double> keyDuration;
//    for (const auto& seg : m_keySegments) {
//        keyDuration[seg.key] += seg.duration;
//    }
//
//    qDebug() << "[QueenMaryKeyExtended] Key distribution:";
//    for (const auto& pair : keyDuration) {
//        if (pair.second > 0.1) {
//            qDebug() << "  " << pair.first << ":" << pair.second << "s";
//        }
//    }
//
//    return true;
//}
//
// KeyChangeList AnalyzerQueenMaryKeyExtended::getKeyChanges() const {
//    return m_resultKeys;
//}
//
// int AnalyzerQueenMaryKeyExtended::applyKeyOffset(int keyIndex) const {
//    if (keyIndex < 0 || keyIndex >= 24) {
//        qWarning() << "[QueenMaryKeyExtended] Invalid key index for offset:"
//        << keyIndex; return keyIndex;
//    }
//    return (keyIndex + KEY_OFFSET_CORRECTION) % 24;
//}
//
// double AnalyzerQueenMaryKeyExtended::calculateConfidence(double*
// keyStrengths, int detectedKeyIndex) const {
//    if (!keyStrengths) {
//        return 50.0;
//    }
//
//    double best = keyStrengths[detectedKeyIndex];
//    double sum = 0.0;
//    double secondBest = 0.0;
//
//    for (int i = 0; i < 24; ++i) {
//        sum += keyStrengths[i];
//        if (i != detectedKeyIndex && keyStrengths[i] > secondBest) {
//            secondBest = keyStrengths[i];
//        }
//    }
//
//    double avg = sum / 24.0;
//    double peakToAvg = best / (avg + 0.0001);
//    double peakToSecond = best / (secondBest + 0.0001);
//
//    // Combined metric: higher confidence when both ratios are good
//    double confidence;
//    if (peakToSecond > 1.5 && peakToAvg > 2.0) {
//        confidence = 80.0 + std::min(20.0, (peakToSecond - 1.5) * 40.0);
//    } else if (peakToSecond > 1.2) {
//        confidence = 60.0 + (peakToSecond - 1.2) * 66.7;
//    } else if (peakToSecond > 1.05) {
//        confidence = 50.0 + (peakToSecond - 1.05) * 200.0;
//    } else {
//        confidence = 50.0;
//    }
//
//    return qBound(0.0, confidence, 100.0);
//}
//
// void AnalyzerQueenMaryKeyExtended::smoothKeyResults() {
//    if (m_keyResults.size() < 3) {
//        return;
//    }
//
//    std::vector<KeyDetectionResult> smoothed;
//    smoothed.reserve(m_keyResults.size());
//
//    for (size_t i = 0; i < m_keyResults.size(); ++i) {
//        // Collect keys in window
//        std::map<ChromaticKey, int> frequency;
//        int start = std::max(0, static_cast<int>(i) - SMOOTHING_WINDOW_SIZE /
//        2); int end = std::min(static_cast<int>(m_keyResults.size()) - 1,
//                static_cast<int>(i) + SMOOTHING_WINDOW_SIZE / 2);
//
//        for (int j = start; j <= end; ++j) {
//            frequency[m_keyResults[j].key]++;
//        }
//
//        // Find most frequent key in window
//        ChromaticKey mostFrequentKey = m_keyResults[i].key;
//        int maxCount = 0;
//        for (const auto& pair : frequency) {
//            if (pair.second > maxCount) {
//                maxCount = pair.second;
//                mostFrequentKey = pair.first;
//            }
//        }
//
//        // Create smoothed result
//        KeyDetectionResult smoothedResult = m_keyResults[i];
//        smoothedResult.key = mostFrequentKey;
//        smoothed.push_back(smoothedResult);
//    }
//
//    m_keyResults = smoothed;
//    qDebug() << "[QueenMaryKeyExtended] Applied smoothing with window size:"
//    << SMOOTHING_WINDOW_SIZE;
//}
//
// void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
//    if (m_keyResults.empty()) {
//        qDebug() << "[QueenMaryKeyExtended] No key results to build segments";
//        return;
//    }
//
//    m_keySegments.clear();
//
//    double lastTime = 0.0;
//    ChromaticKey lastKey = m_keyResults[0].key;
//    double confidenceSum = m_keyResults[0].confidence;
//    int confidenceCount = 1;
//
//    for (size_t i = 1; i < m_keyResults.size(); ++i) {
//        double currentTime = m_keyResults[i].timeSeconds;
//        ChromaticKey currentKey = m_keyResults[i].key;
//        double currentConfidence = m_keyResults[i].confidence;
//
//        if (currentKey != lastKey) {
//            // End current segment
//            AnalysisKeySegment seg;
//            seg.startTime = lastTime;
//            seg.endTime = currentTime;
//            seg.duration = currentTime - lastTime;
//            seg.key = keyToString(static_cast<int>(lastKey));
//            seg.type = "STABLE";
//            seg.confidence = confidenceSum / confidenceCount;
//
//            // Only add segments with minimum duration
//            if (seg.duration > 0.1) {
//                m_keySegments.append(seg);
//
//                qDebug() << "[QueenMaryKeyExtended] Segment:" << seg.startTime
//                << "-" << seg.endTime
//                         << "Key:" << seg.key << "Confidence:" <<
//                         seg.confidence
//                         << "Duration:" << seg.duration;
//            }
//
//            // Start new segment
//            lastTime = currentTime;
//            lastKey = currentKey;
//            confidenceSum = currentConfidence;
//            confidenceCount = 1;
//        } else {
//            confidenceSum += currentConfidence;
//            confidenceCount++;
//        }
//    }
//
//    // Add final segment
//    if (lastTime < m_trackDuration) {
//        AnalysisKeySegment seg;
//        seg.startTime = lastTime;
//        seg.endTime = m_trackDuration;
//        seg.duration = m_trackDuration - lastTime;
//        seg.key = keyToString(static_cast<int>(lastKey));
//        seg.type = "STABLE";
//        seg.confidence = confidenceSum / confidenceCount;
//
//        if (seg.duration > 0.1) {
//            m_keySegments.append(seg);
//            qDebug() << "[QueenMaryKeyExtended] Final segment:" <<
//            seg.startTime << "-" << seg.endTime
//                     << "Key:" << seg.key << "Confidence:" << seg.confidence
//                     << "Duration:" << seg.duration;
//        }
//    }
//}
//
////QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
////    if (key < 0 || key >= 24) {
////        qWarning() << "[QueenMaryKeyExtended] Invalid key index:" << key;
////        return QString("INVALID_%1").arg(key);
////    }
////    return KeyUtils::keyToString(static_cast<ChromaticKey>(key));
////}
//
// QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
//    if (key < 0 || key >= 24) {
//        qWarning() << "[QueenMaryKeyExtended] Invalid key index:" << key;
//        return QString("INVALID_%1").arg(key);
//    }
//
//    // Shift by +1 to match Mixxx's key mapping
//    // Detector index 0 -> ChromaticKey 1 (C)
//    // Detector index 1 -> ChromaticKey 2 (C#/Db)
//    // Detector index 9 -> ChromaticKey 10 (A)
//    int displayKey = (key + 1) % 24;
//
//    return KeyUtils::keyToString(static_cast<ChromaticKey>(displayKey));
//}
//
// QVector<AnalysisKeySegment> AnalyzerQueenMaryKeyExtended::getKeySegments()
// const {
//    return m_keySegments;
//}
//
// QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
//    QJsonArray segmentsArray;
//    int segmentId = 1;
//    for (const auto& seg : m_keySegments) {
//        if (seg.duration < 0.001)
//            continue;
//        QJsonObject segmentObj;
//        segmentObj["id"] = segmentId++;
//        segmentObj["type"] = seg.type;
//        segmentObj["position"] = seg.startTime;
//        segmentObj["duration"] = seg.duration;
//        segmentObj["key"] = seg.key;
//        segmentObj["confidence"] = seg.confidence;
//        segmentObj["range_start"] = seg.startTime;
//        segmentObj["range_end"] = seg.endTime;
//        segmentsArray.append(segmentObj);
//    }
//    return segmentsArray;
//}
//
//} // namespace mixxx

//
// #include "analyzer/plugins/analyzerqueenmarykeyextended.h"
//
// #include <dsp/keydetection/GetKeyMode.h>
//
// #include <algorithm>
// #include <cmath>
// #include <numeric>
// #include <utility>
// #include <vector>
//
// #include "analyzer/constants.h"
// #include "track/keyutils.h"
// #include "util/assert.h"
//
// using mixxx::track::io::key::ChromaticKey;
//
// namespace mixxx {
// namespace {
//
// constexpr int kTuningFrequencyHertz = 440;
//
//// Ratio-based confidence calculation
// double calculateRatioConfidence(double* keyStrengths, int detectedKeyIndex) {
//     if (!keyStrengths) {
//         return 50.0;
//     }
//
//     // Find best and second best
//     double best = 0.0;
//     double secondBest = 0.0;
//     int bestIndex = -1;
//     for (int i = 0; i < 24; ++i) {
//         if (keyStrengths[i] > best) {
//             secondBest = best;
//             best = keyStrengths[i];
//             bestIndex = i;
//         } else if (keyStrengths[i] > secondBest) {
//             secondBest = keyStrengths[i];
//         }
//     }
//
//     // Check if detected key is the best
//     if (bestIndex != detectedKeyIndex) {
//         return 30.0; // Detected key not strongest - low confidence
//     }
//
//     // Calculate ratio between best and second best
//     double ratio = best / (secondBest + 0.0001);
//
//     // Map ratio to confidence:
//     // ratio 1.0 -> 50% (tie)
//     // ratio 1.05 -> 60%
//     // ratio 1.1 -> 70%
//     // ratio 1.2 -> 80%
//     // ratio 1.5 -> 90%
//     // ratio 2.0 -> 95%
//     double confidence;
//     if (ratio < 1.01) {
//         confidence = 50.0;
//     } else if (ratio < 1.05) {
//         confidence = 50.0 + (ratio - 1.0) * 200.0; // 50-60%
//     } else if (ratio < 1.1) {
//         confidence = 60.0 + (ratio - 1.05) * 200.0; // 60-70%
//     } else if (ratio < 1.2) {
//         confidence = 70.0 + (ratio - 1.1) * 100.0; // 70-80%
//     } else if (ratio < 1.5) {
//         confidence = 80.0 + (ratio - 1.2) * 33.3; // 80-90%
//     } else {
//         confidence = 90.0 + std::min(10.0, (ratio - 1.5) * 10.0); // 90-100%
//     }
//
//     return qBound(0.0, confidence, 100.0);
// }
//
// } // namespace
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::pluginInfo() {
//     return AnalyzerPluginInfo(
//             "qm-keydetector-extended:0",
//             QObject::tr("Queen Mary University London (Extended)"),
//             QObject::tr("Queen Mary Key Detector with Key Change Detection"),
//             false);
// }
//
// AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
//         : m_currentFrame(0),
//           m_sampleRate(44100.0),
//           m_trackDuration(0.0) {
// }
//
// AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::info() const {
//     return pluginInfo();
// }
//
// bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
// sampleRate) {
//     m_sampleRate = sampleRate.toDouble();
//     m_resultKeys.clear();
//     m_keyResults.clear();
//     m_keySegments.clear();
//     m_currentFrame = 0;
//
//     GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
//     m_pKeyMode = std::make_unique<GetKeyMode>(config);
//     size_t windowSize = m_pKeyMode->getBlockSize();
//     size_t stepSize = m_pKeyMode->getHopSize();
//
//     qDebug() << "[QueenMaryKeyExtended] Initialized with sample rate:" <<
//     sampleRate
//              << "Window size:" << windowSize << "Step size:" << stepSize;
//
//     return m_helper.initialize(
//             windowSize, stepSize, [this](double* pWindow, size_t) {
//                 // Process the window
//                 int iKey = m_pKeyMode->process(pWindow);
//                 double* keyStrengths = m_pKeyMode->getKeyStrengths();
//
//                 // Find the key with the highest strength (more reliable than
//                 process() return) int bestKeyIndex = 0; double bestStrength =
//                 keyStrengths[0]; for (int j = 1; j < 24; ++j) {
//                     if (keyStrengths[j] > bestStrength) {
//                         bestStrength = keyStrengths[j];
//                         bestKeyIndex = j;
//                     }
//                 }
//
//                 // Use the best key from strengths array
//                 ChromaticKey key = static_cast<ChromaticKey>(bestKeyIndex);
//                 double timeSeconds = static_cast<double>(m_currentFrame) /
//                 m_sampleRate;
//
//                 // Calculate confidence using ratio method
//                 double confidence = calculateRatioConfidence(keyStrengths,
//                 bestKeyIndex);
//
//                 // Debug output for first few frames
//                 static int debugCount = 0;
//                 if (debugCount < 3) {
//                     qDebug() << "[QueenMaryKeyExtended] Frame" << debugCount
//                              << "Process() returned key:" << iKey
//                              << "Best key from strengths:" << bestKeyIndex
//                              << "(" << keyToString(bestKeyIndex) << ")"
//                              << "Confidence:" << confidence;
//
//                     // Print top 5 strengths
//                     std::vector<std::pair<double, int>> sorted;
//                     for (int j = 0; j < 24; ++j) {
//                         sorted.push_back(std::make_pair(keyStrengths[j], j));
//                     }
//                     std::sort(sorted.begin(), sorted.end(), [](const auto& a,
//                     const auto& b) { return a.first > b.first; });
//
//                     qDebug() << "  Top 5 strengths:";
//                     for (int k = 0; k < 5; ++k) {
//                         qDebug() << "    " << k + 1 << ": Key" <<
//                         sorted[k].second
//                                  << "(" << keyToString(sorted[k].second) <<
//                                  ")"
//                                  << "=" << sorted[k].first;
//                     }
//                     debugCount++;
//                 }
//
//                 // Store result with confidence
//                 KeyDetectionResult result;
//                 result.key = key;
//                 result.timeSeconds = timeSeconds;
//                 result.confidence = confidence;
//                 m_keyResults.push_back(result);
//
//                 // Also store in original format for compatibility
//                 m_resultKeys.push_back(qMakePair(key,
//                 static_cast<double>(m_currentFrame)));
//
//                 return true;
//             });
// }
//
// bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT
// iLen) {
//     DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//     if (!m_pKeyMode) {
//         return false;
//     }
//     const size_t numInputFrames = iLen / kAnalysisChannels;
//     m_currentFrame += numInputFrames;
//     return m_helper.processStereoSamples(pIn, iLen);
// }
//
// bool AnalyzerQueenMaryKeyExtended::finalize() {
//     m_helper.finalize();
//     m_trackDuration = static_cast<double>(m_currentFrame) / m_sampleRate;
//     buildKeySegments();
//
//     // Log summary
//     qDebug() << "[QueenMaryKeyExtended] Finalized. Duration:" <<
//     m_trackDuration << "s"
//              << "Key changes:" << m_keyResults.size()
//              << "Segments:" << m_keySegments.size();
//
//     return true;
// }
//
// KeyChangeList AnalyzerQueenMaryKeyExtended::getKeyChanges() const {
//     return m_resultKeys;
// }
//
// void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
//     if (m_keyResults.empty()) {
//         qDebug() << "[QueenMaryKeyExtended] No key results to build
//         segments"; return;
//     }
//
//     m_keySegments.clear();
//
//     double lastTime = 0.0;
//     ChromaticKey lastKey = m_keyResults[0].key;
//     double confidenceSum = m_keyResults[0].confidence;
//     int confidenceCount = 1;
//
//     for (size_t i = 1; i < m_keyResults.size(); ++i) {
//         double currentTime = m_keyResults[i].timeSeconds;
//         ChromaticKey currentKey = m_keyResults[i].key;
//         double currentConfidence = m_keyResults[i].confidence;
//
//         if (currentKey != lastKey) {
//             // End current segment
//             AnalysisKeySegment seg;
//             seg.startTime = lastTime;
//             seg.endTime = currentTime;
//             seg.duration = currentTime - lastTime;
//             seg.key = keyToString(static_cast<int>(lastKey));
//             seg.type = "STABLE";
//             seg.confidence = confidenceSum / confidenceCount;
//             m_keySegments.append(seg);
//
//             qDebug() << "[QueenMaryKeyExtended] Segment:" << seg.startTime <<
//             "-" << seg.endTime
//                      << "Key:" << seg.key << "Confidence:" << seg.confidence;
//
//             // Start new segment
//             lastTime = currentTime;
//             lastKey = currentKey;
//             confidenceSum = currentConfidence;
//             confidenceCount = 1;
//         } else {
//             confidenceSum += currentConfidence;
//             confidenceCount++;
//         }
//     }
//
//     // Add final segment
//     if (lastTime < m_trackDuration) {
//         AnalysisKeySegment seg;
//         seg.startTime = lastTime;
//         seg.endTime = m_trackDuration;
//         seg.duration = m_trackDuration - lastTime;
//         seg.key = keyToString(static_cast<int>(lastKey));
//         seg.type = "STABLE";
//         seg.confidence = confidenceSum / confidenceCount;
//         m_keySegments.append(seg);
//
//         qDebug() << "[QueenMaryKeyExtended] Final segment:" << seg.startTime
//         << "-" << seg.endTime
//                  << "Key:" << seg.key << "Confidence:" << seg.confidence;
//     }
// }
//
// QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
//     return KeyUtils::keyToString(static_cast<ChromaticKey>(key));
// }
//
// QVector<AnalysisKeySegment> AnalyzerQueenMaryKeyExtended::getKeySegments()
// const {
//     return m_keySegments;
// }
//
// QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
//     QJsonArray segmentsArray;
//     int segmentId = 1;
//     for (const auto& seg : m_keySegments) {
//         if (seg.duration < 0.001)
//             continue;
//         QJsonObject segmentObj;
//         segmentObj["id"] = segmentId++;
//         segmentObj["type"] = seg.type;
//         segmentObj["position"] = seg.startTime;
//         segmentObj["duration"] = seg.duration;
//         segmentObj["key"] = seg.key;
//         segmentObj["confidence"] = seg.confidence;
//         segmentObj["range_start"] = seg.startTime;
//         segmentObj["range_end"] = seg.endTime;
//         segmentsArray.append(segmentObj);
//     }
//     return segmentsArray;
// }
//
// } // namespace mixxx

//
// #include "analyzer/plugins/analyzerqueenmarykeyextended.h"
//
// #include <dsp/keydetection/GetKeyMode.h>
//
// #include <algorithm>
// #include <cmath>
// #include <numeric>
// #include <utility>
// #include <vector>
//
// #include "analyzer/constants.h"
// #include "track/keyutils.h"
// #include "util/assert.h"
//
// using mixxx::track::io::key::ChromaticKey;
//
// namespace mixxx {
// namespace {
//
// constexpr int kTuningFrequencyHertz = 440;
//
//// Ratio-based confidence calculation (more robust for compressed key
/// strengths)
// double calculateRatioConfidence(double* keyStrengths, int detectedKeyIndex) {
//     if (!keyStrengths) {
//         return 50.0;
//     }
//
//     // Collect all strengths with their indices
//     std::vector<std::pair<double, int>> strengths;
//     for (int i = 0; i < 24; ++i) {
//         strengths.push_back(std::make_pair(keyStrengths[i], i));
//     }
//
//     // Sort by strength (highest first)
//     std::sort(strengths.begin(), strengths.end(), [](const auto& a, const
//     auto& b) { return a.first > b.first; });
//
//     double best = strengths[0].first;
//     double secondBest = strengths[1].first;
//     double thirdBest = strengths[2].first;
//
//     // Check if detected key is actually the best
//     bool detectedIsBest = (strengths[0].second == detectedKeyIndex);
//     if (!detectedIsBest) {
//         // If the detected key isn't the strongest, confidence is low
//         return 30.0;
//     }
//
//     // Calculate ratio between best and second best
//     double ratio = best / (secondBest + 0.0001);
//
//     // Also check margin to third best
//     double marginToThird = (best - thirdBest) / (best + 0.0001);
//
//     // Map ratio to confidence:
//     // ratio 1.0 -> 50% (tie)
//     // ratio 1.05 -> 60%
//     // ratio 1.1 -> 70%
//     // ratio 1.2 -> 80%
//     // ratio 1.5 -> 90%
//     // ratio 2.0 -> 95%
//     double confidence;
//     if (ratio < 1.01) {
//         confidence = 50.0;
//     } else if (ratio < 1.05) {
//         confidence = 50.0 + (ratio - 1.0) * 200.0; // 50-60%
//     } else if (ratio < 1.1) {
//         confidence = 60.0 + (ratio - 1.05) * 200.0; // 60-70%
//     } else if (ratio < 1.2) {
//         confidence = 70.0 + (ratio - 1.1) * 100.0; // 70-80%
//     } else if (ratio < 1.5) {
//         confidence = 80.0 + (ratio - 1.2) * 33.3; // 80-90%
//     } else {
//         confidence = 90.0 + std::min(10.0, (ratio - 1.5) * 10.0); // 90-100%
//     }
//
//     // Boost confidence if margin to third best is also high
//     if (marginToThird > 0.2) {
//         confidence = std::min(100.0, confidence + 5.0);
//     }
//
//     return qBound(0.0, confidence, 100.0);
// }
//
//// Alternative: Simple ratio-based confidence
// double calculateSimpleRatioConfidence(double* keyStrengths, int
// detectedKeyIndex) {
//     if (!keyStrengths)
//         return 50.0;
//
//     // Find best and second best
//     double best = 0.0;
//     double secondBest = 0.0;
//     for (int i = 0; i < 24; ++i) {
//         if (keyStrengths[i] > best) {
//             secondBest = best;
//             best = keyStrengths[i];
//         } else if (keyStrengths[i] > secondBest) {
//             secondBest = keyStrengths[i];
//         }
//     }
//
//     // If detected key isn't the best, confidence is low
//     if (keyStrengths[detectedKeyIndex] != best) {
//         return 30.0;
//     }
//
//     double ratio = best / (secondBest + 0.0001);
//     // Map ratio 1.0->50%, 1.1->70%, 1.5->90%
//     double confidence = 50.0 + std::min(50.0, (ratio - 1.0) * 100.0);
//
//     return qBound(0.0, confidence, 100.0);
// }
//
// } // namespace
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::pluginInfo() {
//     return AnalyzerPluginInfo(
//             "qm-keydetector-extended:0",
//             QObject::tr("Queen Mary University London (Extended)"),
//             QObject::tr("Queen Mary Key Detector with Key Change Detection"),
//             false);
// }
//
// AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
//         : m_currentFrame(0),
//           m_sampleRate(44100.0),
//           m_trackDuration(0.0) {
// }
//
// AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::info() const {
//     return pluginInfo();
// }
//
// bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
// sampleRate) {
//     m_sampleRate = sampleRate.toDouble();
//     m_resultKeys.clear();
//     m_keyResults.clear();
//     m_keySegments.clear();
//     m_currentFrame = 0;
//
//     GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
//     m_pKeyMode = std::make_unique<GetKeyMode>(config);
//     size_t windowSize = m_pKeyMode->getBlockSize();
//     size_t stepSize = m_pKeyMode->getHopSize();
//
//     qDebug() << "[QueenMaryKeyExtended] Initialized with sample rate:" <<
//     sampleRate
//              << "Window size:" << windowSize << "Step size:" << stepSize;
//
//     return m_helper.initialize(
//             windowSize, stepSize, [this](double* pWindow, size_t) {
//                 int iKey = m_pKeyMode->process(pWindow);
//                 if (iKey < 0) {
//                     iKey = 0;
//                 }
//
//                 ChromaticKey key = static_cast<ChromaticKey>(iKey);
//                 double timeSeconds = static_cast<double>(m_currentFrame) /
//                 m_sampleRate;
//
//                 // Get key strengths and calculate confidence using ratio
//                 method double* keyStrengths = m_pKeyMode->getKeyStrengths();
//
//                 // Use the simple ratio-based confidence for now
//                 double confidence =
//                 calculateSimpleRatioConfidence(keyStrengths, iKey);
//
//                 // Debug output for first few frames
//                 static int debugCount = 0;
//                 if (debugCount < 3) {
//                     qDebug() << "[QueenMaryKeyExtended] Frame" << debugCount
//                              << "Detected key:" << iKey
//                              << "Confidence:" << confidence;
//
//                     // Print top 3 strengths for debugging
//                     std::vector<std::pair<double, int>> sorted;
//                     for (int j = 0; j < 24; ++j) {
//                         sorted.push_back(std::make_pair(keyStrengths[j], j));
//                     }
//                     std::sort(sorted.begin(), sorted.end(), [](const auto& a,
//                     const auto& b) { return a.first > b.first; });
//
//                     qDebug() << "  Top 3 strengths:"
//                              << "1. Key" << sorted[0].second << "=" <<
//                              sorted[0].first
//                              << "2. Key" << sorted[1].second << "=" <<
//                              sorted[1].first
//                              << "3. Key" << sorted[2].second << "=" <<
//                              sorted[2].first;
//                     debugCount++;
//                 }
//
//                 // Store result with confidence
//                 KeyDetectionResult result;
//                 result.key = key;
//                 result.timeSeconds = timeSeconds;
//                 result.confidence = confidence;
//                 m_keyResults.push_back(result);
//
//                 // Also store in original format for compatibility
//                 m_resultKeys.push_back(qMakePair(key,
//                 static_cast<double>(m_currentFrame)));
//
//                 return true;
//             });
// }
//
// bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT
// iLen) {
//     DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//     if (!m_pKeyMode) {
//         return false;
//     }
//     const size_t numInputFrames = iLen / kAnalysisChannels;
//     m_currentFrame += numInputFrames;
//     return m_helper.processStereoSamples(pIn, iLen);
// }
//
// bool AnalyzerQueenMaryKeyExtended::finalize() {
//     m_helper.finalize();
//     m_trackDuration = static_cast<double>(m_currentFrame) / m_sampleRate;
//     buildKeySegments();
//
//     // Log summary
//     qDebug() << "[QueenMaryKeyExtended] Finalized. Duration:" <<
//     m_trackDuration << "s"
//              << "Key changes:" << m_keyResults.size()
//              << "Segments:" << m_keySegments.size();
//
//     return true;
// }
//
// KeyChangeList AnalyzerQueenMaryKeyExtended::getKeyChanges() const {
//     return m_resultKeys;
// }
//
// void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
//     if (m_keyResults.empty()) {
//         qDebug() << "[QueenMaryKeyExtended] No key results to build
//         segments"; return;
//     }
//
//     m_keySegments.clear();
//
//     double lastTime = 0.0;
//     ChromaticKey lastKey = m_keyResults[0].key;
//     double confidenceSum = m_keyResults[0].confidence;
//     int confidenceCount = 1;
//
//     for (size_t i = 1; i < m_keyResults.size(); ++i) {
//         double currentTime = m_keyResults[i].timeSeconds;
//         ChromaticKey currentKey = m_keyResults[i].key;
//         double currentConfidence = m_keyResults[i].confidence;
//
//         if (currentKey != lastKey) {
//             // End current segment
//             AnalysisKeySegment seg;
//             seg.startTime = lastTime;
//             seg.endTime = currentTime;
//             seg.duration = currentTime - lastTime;
//             seg.key = keyToString(static_cast<int>(lastKey));
//             seg.type = "STABLE";
//             seg.confidence = confidenceSum / confidenceCount;
//             m_keySegments.append(seg);
//
//             qDebug() << "[QueenMaryKeyExtended] Segment:" << seg.startTime <<
//             "-" << seg.endTime
//                      << "Key:" << seg.key << "Confidence:" << seg.confidence;
//
//             // Start new segment
//             lastTime = currentTime;
//             lastKey = currentKey;
//             confidenceSum = currentConfidence;
//             confidenceCount = 1;
//         } else {
//             confidenceSum += currentConfidence;
//             confidenceCount++;
//         }
//     }
//
//     // Add final segment
//     if (lastTime < m_trackDuration) {
//         AnalysisKeySegment seg;
//         seg.startTime = lastTime;
//         seg.endTime = m_trackDuration;
//         seg.duration = m_trackDuration - lastTime;
//         seg.key = keyToString(static_cast<int>(lastKey));
//         seg.type = "STABLE";
//         seg.confidence = confidenceSum / confidenceCount;
//         m_keySegments.append(seg);
//
//         qDebug() << "[QueenMaryKeyExtended] Final segment:" << seg.startTime
//         << "-" << seg.endTime
//                  << "Key:" << seg.key << "Confidence:" << seg.confidence;
//     }
// }
//
// QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
//     return KeyUtils::keyToString(static_cast<ChromaticKey>(key));
// }
//
// QVector<AnalysisKeySegment> AnalyzerQueenMaryKeyExtended::getKeySegments()
// const {
//     return m_keySegments;
// }
//
// QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
//     QJsonArray segmentsArray;
//     int segmentId = 1;
//     for (const auto& seg : m_keySegments) {
//         if (seg.duration < 0.001)
//             continue;
//         QJsonObject segmentObj;
//         segmentObj["id"] = segmentId++;
//         segmentObj["type"] = seg.type;
//         segmentObj["position"] = seg.startTime;
//         segmentObj["duration"] = seg.duration;
//         segmentObj["key"] = seg.key;
//         segmentObj["confidence"] = seg.confidence;
//         segmentObj["range_start"] = seg.startTime;
//         segmentObj["range_end"] = seg.endTime;
//         segmentsArray.append(segmentObj);
//     }
//     return segmentsArray;
// }
//
// } // namespace mixxx

//
// #include "analyzer/plugins/analyzerqueenmarykeyextended.h"
//
// #include <dsp/keydetection/GetKeyMode.h>
//
// #include <algorithm>
// #include <cmath>
// #include <numeric>
// #include <utility>
//
// #include "analyzer/constants.h"
// #include "track/keyutils.h"
// #include "util/assert.h"
//
// using mixxx::track::io::key::ChromaticKey;
//
// namespace mixxx {
// namespace {
//
// constexpr int kTuningFrequencyHertz = 440;
//
//// Helper function to normalize confidence from raw key strengths
// double calculateNormalizedConfidence(double* keyStrengths, int
// detectedKeyIndex) {
//     if (!keyStrengths) {
//         return 50.0; // Default fallback
//     }
//
//     // Find min and max to normalize to 0-1 range
//     double minStrength = keyStrengths[0];
//     double maxStrength = keyStrengths[0];
//     for (int i = 0; i < 24; ++i) {
//         if (keyStrengths[i] < minStrength)
//             minStrength = keyStrengths[i];
//         if (keyStrengths[i] > maxStrength)
//             maxStrength = keyStrengths[i];
//     }
//
//     double range = maxStrength - minStrength;
//     if (range < 0.0001) {
//         return 50.0; // All strengths equal - no confidence
//     }
//
//     // Normalize the detected strength
//     double detectedStrength = (keyStrengths[detectedKeyIndex] - minStrength)
//     / range;
//
//     // Find the second best normalized strength
//     double secondBest = 0.0;
//     for (int i = 0; i < 24; ++i) {
//         if (i != detectedKeyIndex) {
//             double normalized = (keyStrengths[i] - minStrength) / range;
//             if (normalized > secondBest) {
//                 secondBest = normalized;
//             }
//         }
//     }
//
//     // Calculate confidence based on margin between best and second best
//     // Margin of 0.0 -> 50%, margin of 1.0 -> 100%
//     double margin = detectedStrength - secondBest;
//     double confidence = 50.0 + (margin * 50.0);
//
//     return qBound(0.0, confidence, 100.0);
// }
//
// } // namespace
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::pluginInfo() {
//     return AnalyzerPluginInfo(
//             "qm-keydetector-extended:0",
//             QObject::tr("Queen Mary University London (Extended)"),
//             QObject::tr("Queen Mary Key Detector with Key Change Detection"),
//             false);
// }
//
// AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
//         : m_currentFrame(0),
//           m_sampleRate(44100.0),
//           m_trackDuration(0.0) {
// }
//
// AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::info() const {
//     return pluginInfo();
// }
//
// bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
// sampleRate) {
//     m_sampleRate = sampleRate.toDouble();
//     m_resultKeys.clear();
//     m_keyResults.clear();
//     m_keySegments.clear();
//     m_currentFrame = 0;
//
//     GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
//     m_pKeyMode = std::make_unique<GetKeyMode>(config);
//     size_t windowSize = m_pKeyMode->getBlockSize();
//     size_t stepSize = m_pKeyMode->getHopSize();
//
//     qDebug() << "[QueenMaryKeyExtended] Initialized with sample rate:" <<
//     sampleRate
//              << "Window size:" << windowSize << "Step size:" << stepSize;
//
//     return m_helper.initialize(
//             windowSize, stepSize, [this](double* pWindow, size_t) {
//                 int iKey = m_pKeyMode->process(pWindow);
//                 if (iKey < 0) {
//                     iKey = 0;
//                 }
//
//                 ChromaticKey key = static_cast<ChromaticKey>(iKey);
//                 double timeSeconds = static_cast<double>(m_currentFrame) /
//                 m_sampleRate;
//
//                 // Get key strengths and calculate normalized confidence
//                 double* keyStrengths = m_pKeyMode->getKeyStrengths();
//                 double confidence =
//                 calculateNormalizedConfidence(keyStrengths, iKey);
//
//                 // Store result with confidence
//                 KeyDetectionResult result;
//                 result.key = key;
//                 result.timeSeconds = timeSeconds;
//                 result.confidence = confidence;
//                 m_keyResults.push_back(result);
//
//                 // Also store in original format for compatibility
//                 m_resultKeys.push_back(qMakePair(key,
//                 static_cast<double>(m_currentFrame)));
//
//                 return true;
//             });
// }
//
// bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT
// iLen) {
//     DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//     if (!m_pKeyMode) {
//         return false;
//     }
//     const size_t numInputFrames = iLen / kAnalysisChannels;
//     m_currentFrame += numInputFrames;
//     return m_helper.processStereoSamples(pIn, iLen);
// }
//
// bool AnalyzerQueenMaryKeyExtended::finalize() {
//     m_helper.finalize();
//     m_trackDuration = static_cast<double>(m_currentFrame) / m_sampleRate;
//     buildKeySegments();
//
//     // Log summary
//     qDebug() << "[QueenMaryKeyExtended] Finalized. Duration:" <<
//     m_trackDuration << "s"
//              << "Key changes:" << m_keyResults.size()
//              << "Segments:" << m_keySegments.size();
//
//     return true;
// }
//
// KeyChangeList AnalyzerQueenMaryKeyExtended::getKeyChanges() const {
//     return m_resultKeys;
// }
//
// void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
//     if (m_keyResults.empty()) {
//         qDebug() << "[QueenMaryKeyExtended] No key results to build
//         segments"; return;
//     }
//
//     m_keySegments.clear();
//
//     double lastTime = 0.0;
//     ChromaticKey lastKey = m_keyResults[0].key;
//     double confidenceSum = m_keyResults[0].confidence;
//     int confidenceCount = 1;
//
//     for (size_t i = 1; i < m_keyResults.size(); ++i) {
//         double currentTime = m_keyResults[i].timeSeconds;
//         ChromaticKey currentKey = m_keyResults[i].key;
//         double currentConfidence = m_keyResults[i].confidence;
//
//         if (currentKey != lastKey) {
//             // End current segment
//             AnalysisKeySegment seg;
//             seg.startTime = lastTime;
//             seg.endTime = currentTime;
//             seg.duration = currentTime - lastTime;
//             seg.key = keyToString(static_cast<int>(lastKey));
//             seg.type = "STABLE";
//             seg.confidence = confidenceSum / confidenceCount;
//             m_keySegments.append(seg);
//
//             qDebug() << "[QueenMaryKeyExtended] Segment:" << seg.startTime <<
//             "-" << seg.endTime
//                      << "Key:" << seg.key << "Confidence:" << seg.confidence;
//
//             // Start new segment
//             lastTime = currentTime;
//             lastKey = currentKey;
//             confidenceSum = currentConfidence;
//             confidenceCount = 1;
//         } else {
//             confidenceSum += currentConfidence;
//             confidenceCount++;
//         }
//     }
//
//     // Add final segment
//     if (lastTime < m_trackDuration) {
//         AnalysisKeySegment seg;
//         seg.startTime = lastTime;
//         seg.endTime = m_trackDuration;
//         seg.duration = m_trackDuration - lastTime;
//         seg.key = keyToString(static_cast<int>(lastKey));
//         seg.type = "STABLE";
//         seg.confidence = confidenceSum / confidenceCount;
//         m_keySegments.append(seg);
//
//         qDebug() << "[QueenMaryKeyExtended] Final segment:" << seg.startTime
//         << "-" << seg.endTime
//                  << "Key:" << seg.key << "Confidence:" << seg.confidence;
//     }
// }
//
// QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
//     return KeyUtils::keyToString(static_cast<ChromaticKey>(key));
// }
//
// QVector<AnalysisKeySegment> AnalyzerQueenMaryKeyExtended::getKeySegments()
// const {
//     return m_keySegments;
// }
//
// QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
//     QJsonArray segmentsArray;
//     int segmentId = 1;
//     for (const auto& seg : m_keySegments) {
//         if (seg.duration < 0.001)
//             continue;
//         QJsonObject segmentObj;
//         segmentObj["id"] = segmentId++;
//         segmentObj["type"] = seg.type;
//         segmentObj["position"] = seg.startTime;
//         segmentObj["duration"] = seg.duration;
//         segmentObj["key"] = seg.key;
//         segmentObj["confidence"] = seg.confidence;
//         segmentObj["range_start"] = seg.startTime;
//         segmentObj["range_end"] = seg.endTime;
//         segmentsArray.append(segmentObj);
//     }
//     return segmentsArray;
// }
//
// } // namespace mixxx

// #include "analyzer/plugins/analyzerqueenmarykeyextended.h"
//
// #include <dsp/keydetection/GetKeyMode.h>
//
// #include <algorithm>
// #include <utility>
//
// #include "analyzer/constants.h"
// #include "track/keyutils.h"
// #include "util/assert.h"
//
// using mixxx::track::io::key::ChromaticKey;
//
// namespace mixxx {
// namespace {
//
// constexpr int kTuningFrequencyHertz = 440;
//
// } // namespace
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::pluginInfo() {
//     return AnalyzerPluginInfo(
//             "qm-keydetector-extended:0",
//             QObject::tr("Queen Mary University London (Extended)"),
//             QObject::tr("Queen Mary Key Detector with Key Change Detection"),
//             false);
// }
//
// AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
//         : m_currentFrame(0),
//           m_sampleRate(44100.0),
//           m_trackDuration(0.0) {
// }
//
// AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::info() const {
//     return pluginInfo();
// }
//
// bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate sampleRate) {
//     m_sampleRate = sampleRate.toDouble();
//     m_resultKeys.clear();
//     m_keyResults.clear();
//     m_keySegments.clear();
//     m_currentFrame = 0;
//
//     GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
//     m_pKeyMode = std::make_unique<GetKeyMode>(config);
//     size_t windowSize = m_pKeyMode->getBlockSize();
//     size_t stepSize = m_pKeyMode->getHopSize();
//
//     return m_helper.initialize(
//             windowSize, stepSize, [this](double* pWindow, size_t) {
//                 int iKey = m_pKeyMode->process(pWindow);
//                 if (iKey < 0) {
//                     iKey = 0;
//                 }
//
//                 ChromaticKey key = static_cast<ChromaticKey>(iKey);
//                 double timeSeconds = static_cast<double>(m_currentFrame) / m_sampleRate;
//
//                 // Get key strengths for confidence calculation
//                 double* keyStrengths = m_pKeyMode->getKeyStrengths();
//
//                 double confidence = 80.0; // Default fallback
//                 if (keyStrengths) {
//                     double detectedStrength = keyStrengths[iKey];
//                     double secondBest = 0.0;
//                     for (int j = 0; j < 24; ++j) {
//                         if (j != iKey && keyStrengths[j] > secondBest) {
//                             secondBest = keyStrengths[j];
//                         }
//                     }
//                     double margin = detectedStrength - secondBest;
//                     confidence = 50.0 + (margin * 50.0);
//                     confidence = qBound(0.0, confidence, 100.0);
//                 }
//
//                 // Store result with confidence
//                 KeyDetectionResult result;
//                 result.key = key;
//                 result.timeSeconds = timeSeconds;
//                 result.confidence = confidence;
//                 m_keyResults.push_back(result);
//
//                 // Also store in original format for compatibility
//                 m_resultKeys.push_back(qMakePair(key, static_cast<double>(m_currentFrame)));
//
//                 return true;
//             });
// }
//
// bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT iLen) {
//     DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//     if (!m_pKeyMode) {
//         return false;
//     }
//     const size_t numInputFrames = iLen / kAnalysisChannels;
//     m_currentFrame += numInputFrames;
//     return m_helper.processStereoSamples(pIn, iLen);
// }
//
// bool AnalyzerQueenMaryKeyExtended::finalize() {
//     m_helper.finalize();
//     m_trackDuration = static_cast<double>(m_currentFrame) / m_sampleRate;
//     buildKeySegments();
//     return true;
// }
//
// KeyChangeList AnalyzerQueenMaryKeyExtended::getKeyChanges() const {
//     return m_resultKeys;
// }
// void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
//     if (m_keyResults.empty()) {
//         return;
//     }
//
//     m_keySegments.clear();
//
//     double lastTime = 0.0;
//     ChromaticKey lastKey = m_keyResults[0].key;
//     double confidenceSum = m_keyResults[0].confidence;
//     int confidenceCount = 1;
//
//     for (size_t i = 1; i < m_keyResults.size(); ++i) {
//         double currentTime = m_keyResults[i].timeSeconds;
//         ChromaticKey currentKey = m_keyResults[i].key;
//         double currentConfidence = m_keyResults[i].confidence;
//
//         if (currentKey != lastKey) {
//             AnalysisKeySegment seg;
//             seg.startTime = lastTime;
//             seg.endTime = currentTime;
//             seg.duration = currentTime - lastTime;
//             seg.key = keyToString(static_cast<int>(lastKey));
//             seg.type = "STABLE";
//             seg.confidence = confidenceSum / confidenceCount;
//             m_keySegments.append(seg);
//
//             lastTime = currentTime;
//             lastKey = currentKey;
//             confidenceSum = currentConfidence;
//             confidenceCount = 1;
//         } else {
//             confidenceSum += currentConfidence;
//             confidenceCount++;
//         }
//     }
//
//     // Add final segment
//     if (lastTime < m_trackDuration) {
//         AnalysisKeySegment seg;
//         seg.startTime = lastTime;
//         seg.endTime = m_trackDuration;
//         seg.duration = m_trackDuration - lastTime;
//         seg.key = keyToString(static_cast<int>(lastKey));
//         seg.type = "STABLE";
//         seg.confidence = confidenceSum / confidenceCount;
//         m_keySegments.append(seg);
//     }
//
//     qDebug() << "[QueenMaryKeyExtended] Built" << m_keySegments.size() << "segments";
//     for (const auto& seg : m_keySegments) {
//         qDebug() << "  Key:" << seg.key << "Confidence:" << seg.confidence << "%";
//     }
// }
//
// void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
//     if (m_keyResults.empty()) {
//         return;
//     }
//
//     m_keySegments.clear();
//
//     double lastTime = 0.0;
//     ChromaticKey lastKey = m_keyResults[0].key;
//     double confidenceSum = m_keyResults[0].confidence;
//     int confidenceCount = 1;
//
//     for (size_t i = 1; i < m_keyResults.size(); ++i) {
//         double currentTime = m_keyResults[i].timeSeconds;
//         ChromaticKey currentKey = m_keyResults[i].key;
//         double currentConfidence = m_keyResults[i].confidence;
//
//         if (currentKey != lastKey) {
//             AnalysisKeySegment seg;
//             seg.startTime = lastTime;
//             seg.endTime = currentTime;
//             seg.duration = currentTime - lastTime;
//             seg.key = keyToString(static_cast<int>(lastKey));
//             seg.type = "STABLE";
//             seg.confidence = confidenceSum / confidenceCount;
//             m_keySegments.append(seg);
//
//             lastTime = currentTime;
//             lastKey = currentKey;
//             confidenceSum = currentConfidence;
//             confidenceCount = 1;
//         } else {
//             confidenceSum += currentConfidence;
//             confidenceCount++;
//         }
//     }
//
//     // Add final segment
//     if (lastTime < m_trackDuration) {
//         AnalysisKeySegment seg;
//         seg.startTime = lastTime;
//         seg.endTime = m_trackDuration;
//         seg.duration = m_trackDuration - lastTime;
//         seg.key = keyToString(static_cast<int>(lastKey));
//         seg.type = "STABLE";
//         seg.confidence = confidenceSum / confidenceCount;
//         m_keySegments.append(seg);
//     }
//
//     qDebug() << "[QueenMaryKeyExtended] Built" << m_keySegments.size() << "segments";
//     for (const auto& seg : m_keySegments) {
//         qDebug() << "  Key:" << seg.key << "Confidence:" << seg.confidence << "%";
//     }
// }
//
// QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
//     return KeyUtils::keyToString(static_cast<ChromaticKey>(key));
// }
//
// QVector<AnalysisKeySegment> AnalyzerQueenMaryKeyExtended::getKeySegments() const {
//     return m_keySegments;
// }
//
// QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
//     QJsonArray segmentsArray;
//     int segmentId = 1;
//     for (const auto& seg : m_keySegments) {
//         if (seg.duration < 0.001)
//             continue;
//         QJsonObject segmentObj;
//         segmentObj["id"] = segmentId++;
//         segmentObj["type"] = seg.type;
//         segmentObj["position"] = seg.startTime;
//         segmentObj["duration"] = seg.duration;
//         segmentObj["key"] = seg.key;
//         segmentObj["confidence"] = seg.confidence;
//         segmentObj["range_start"] = seg.startTime;
//         segmentObj["range_end"] = seg.endTime;
//         segmentsArray.append(segmentObj);
//     }
//     return segmentsArray;
// }
//
// } // namespace mixxx

// #include "analyzer/plugins/analyzerqueenmarykeyextended.h"
//
// #include <dsp/keydetection/GetKeyMode.h>
//
// #include <algorithm>
// #include <utility>
//
// #include "analyzer/constants.h"
// #include "track/keyutils.h"
// #include "util/assert.h"
//
// using mixxx::track::io::key::ChromaticKey;
//
// namespace mixxx {
// namespace {
//
// constexpr int kTuningFrequencyHertz = 440;
//
// } // namespace
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::pluginInfo() {
//     return AnalyzerPluginInfo(
//             "qm-keydetector-extended:0",
//             QObject::tr("Queen Mary University London (Extended)"),
//             QObject::tr("Queen Mary Key Detector with Key Change Detection"),
//             false);
// }
//
// AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
//         : m_currentFrame(0),
//           m_sampleRate(44100.0),
//           m_trackDuration(0.0) {
// }
//
// AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;
//
// AnalyzerPluginInfo AnalyzerQueenMaryKeyExtended::info() const {
//     return pluginInfo();
// }
//
// bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
// sampleRate) {
//     m_sampleRate = sampleRate.toDouble();
//     m_resultKeys.clear();
//     m_keyResults.clear(); // Clear new storage
//     m_keySegments.clear();
//     m_currentFrame = 0;
//
//     GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
//     m_pKeyMode = std::make_unique<GetKeyMode>(config);
//     size_t windowSize = m_pKeyMode->getBlockSize();
//     size_t stepSize = m_pKeyMode->getHopSize();
//
//     return m_helper.initialize(
//             windowSize, stepSize, [this](double* pWindow, size_t) {
//                 int iKey = m_pKeyMode->process(pWindow);
//                 if (iKey < 0) {
//                     iKey = 0;
//                 }
//
//                 ChromaticKey key = static_cast<ChromaticKey>(iKey);
//                 double timeSeconds = static_cast<double>(m_currentFrame) /
//                 m_sampleRate;
//
//                 // Get key strengths for confidence calculation
//                 double* keyStrengths = m_pKeyMode->getKeyStrengths();
//
//                 // Calculate confidence from strengths
//                 double confidence = 80.0; // Default fallback
//                 if (keyStrengths) {
//                     double detectedStrength = keyStrengths[iKey];
//                     double secondBest = 0.0;
//                     for (int j = 0; j < 24; ++j) {
//                         if (j != iKey && keyStrengths[j] > secondBest) {
//                             secondBest = keyStrengths[j];
//                         }
//                     }
//                     // Confidence based on margin between best and second
//                     best double margin = detectedStrength - secondBest;
//                     confidence = 50.0 + (margin * 50.0);
//                     confidence = qBound(0.0, confidence, 100.0);
//                 }
//
//                 // Store both key and confidence
//                 KeyDetectionResult result;
//                 result.key = key;
//                 result.timeSeconds = timeSeconds;
//                 result.confidence = confidence;
//                 m_keyResults.push_back(result);
//
//                 // Also store in original format for compatibility
//                 m_resultKeys.push_back(qMakePair(key,
//                 static_cast<double>(m_currentFrame)));
//
//                 return true;
//             });
// }
//
////bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
/// sampleRate) { /    m_sampleRate = sampleRate.toDouble(); /
/// m_resultKeys.clear(); /    m_keySegments.clear(); /    m_currentFrame = 0;
////
////    GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
////    m_pKeyMode = std::make_unique<GetKeyMode>(config);
////    size_t windowSize = m_pKeyMode->getBlockSize();
////    size_t stepSize = m_pKeyMode->getHopSize();
////
////    return m_helper.initialize(
////            windowSize, stepSize, [this](double* pWindow, size_t) {
////                int iKey = m_pKeyMode->process(pWindow);
////                if (iKey < 0) {
////                    iKey = 0;
////                }
////                // Convert int to ChromaticKey for the KeyChangeList
////                ChromaticKey key = static_cast<ChromaticKey>(iKey);
////                m_resultKeys.push_back(qMakePair(key,
/// static_cast<double>(m_currentFrame))); /                return true; / });
////}
//
// bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT
// iLen) {
//    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//    if (!m_pKeyMode) {
//        return false;
//    }
//    const size_t numInputFrames = iLen / kAnalysisChannels;
//    m_currentFrame += numInputFrames;
//    return m_helper.processStereoSamples(pIn, iLen);
//}
//
// bool AnalyzerQueenMaryKeyExtended::finalize() {
//    m_helper.finalize();
//    m_trackDuration = static_cast<double>(m_currentFrame) / m_sampleRate;
//    buildKeySegments();
//    return true;
//}
//
// KeyChangeList AnalyzerQueenMaryKeyExtended::getKeyChanges() const {
//    return m_resultKeys;
//}
//
////void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
////    if (m_resultKeys.empty()) {
////        return;
////    }
////
////    m_keySegments.clear();
////
////    // Convert frame positions to seconds
////    std::vector<std::pair<ChromaticKey, double>> keyChanges;
////    for (const auto& keyPair : m_resultKeys) {
////        double timeSeconds = static_cast<double>(keyPair.second) /
/// m_sampleRate; /        keyChanges.push_back(std::make_pair(keyPair.first,
/// timeSeconds)); /    }
////
////    double lastTime = 0.0;
////    ChromaticKey lastKey = keyChanges[0].first;
////
////    for (size_t i = 1; i < keyChanges.size(); ++i) {
////        double currentTime = keyChanges[i].second;
////        ChromaticKey currentKey = keyChanges[i].first;
////
////        if (currentKey != lastKey) {
////            AnalysisKeySegment seg;
////            seg.startTime = lastTime;
////            seg.endTime = currentTime;
////            seg.duration = currentTime - lastTime;
////            seg.key = keyToString(static_cast<int>(lastKey));
////            seg.type = "STABLE";
////            m_keySegments.append(seg);
////
////            lastTime = currentTime;
////            lastKey = currentKey;
////        }
////    }
////
////    // Add final segment
////    if (lastTime < m_trackDuration) {
////        AnalysisKeySegment seg;
////        seg.startTime = lastTime;
////        seg.endTime = m_trackDuration;
////        seg.duration = m_trackDuration - lastTime;
////        seg.key = keyToString(static_cast<int>(lastKey));
////        seg.type = "STABLE";
////        m_keySegments.append(seg);
////    }
////}
//
////void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
////    if (m_resultKeys.empty()) {
////        return;
////    }
////
////    m_keySegments.clear();
////
////    // Convert frame positions to seconds with confidence
////    std::vector<std::tuple<ChromaticKey, double, double>> keyChanges; //
/// key, time, confidence
////
////    for (const auto& keyPair : m_resultKeys) {
////        double timeSeconds = static_cast<double>(keyPair.second) /
/// m_sampleRate;
////
////        // Get confidence for this key detection
////        // Note: This requires storing keyStrengths per frame during
/// processing /        // For now, we'll use a placeholder or average confidence
////        double confidence = 80.0; // Placeholder - we'll implement proper
/// confidence later
////
////        keyChanges.push_back(std::make_tuple(keyPair.first, timeSeconds,
/// confidence)); /    }
////
////    double lastTime = 0.0;
////    ChromaticKey lastKey = std::get<0>(keyChanges[0]);
////    double confidenceSum = std::get<2>(keyChanges[0]);
////    int confidenceCount = 1;
////
////    for (size_t i = 1; i < keyChanges.size(); ++i) {
////        double currentTime = std::get<1>(keyChanges[i]);
////        ChromaticKey currentKey = std::get<0>(keyChanges[i]);
////        double currentConfidence = std::get<2>(keyChanges[i]);
////
////        if (currentKey != lastKey) {
////            AnalysisKeySegment seg;
////            seg.startTime = lastTime;
////            seg.endTime = currentTime;
////            seg.duration = currentTime - lastTime;
////            seg.key = keyToString(static_cast<int>(lastKey));
////            seg.type = "STABLE";
////            seg.confidence = confidenceSum / confidenceCount;
////            m_keySegments.append(seg);
////
////            lastTime = currentTime;
////            lastKey = currentKey;
////            confidenceSum = currentConfidence;
////            confidenceCount = 1;
////        } else {
////            confidenceSum += currentConfidence;
////            confidenceCount++;
////        }
////    }
////
////    // Add final segment
////    if (lastTime < m_trackDuration) {
////        AnalysisKeySegment seg;
////        seg.startTime = lastTime;
////        seg.endTime = m_trackDuration;
////        seg.duration = m_trackDuration - lastTime;
////        seg.key = keyToString(static_cast<int>(lastKey));
////        seg.type = "STABLE";
////        seg.confidence = confidenceSum / confidenceCount;
////        m_keySegments.append(seg);
////    }
////}
//
// void AnalyzerQueenMaryKeyExtended::buildKeySegments() {
//    if (m_keyResults.empty()) {
//        return;
//    }
//
//    m_keySegments.clear();
//
//    double lastTime = 0.0;
//    ChromaticKey lastKey = m_keyResults[0].key;
//    double confidenceSum = m_keyResults[0].confidence;
//    int confidenceCount = 1;
//
//    for (size_t i = 1; i < m_keyResults.size(); ++i) {
//        double currentTime = m_keyResults[i].timeSeconds;
//        ChromaticKey currentKey = m_keyResults[i].key;
//        double currentConfidence = m_keyResults[i].confidence;
//
//        if (currentKey != lastKey) {
//            // End current segment
//            AnalysisKeySegment seg;
//            seg.startTime = lastTime;
//            seg.endTime = currentTime;
//            seg.duration = currentTime - lastTime;
//            seg.key = keyToString(static_cast<int>(lastKey));
//            seg.type = "STABLE";
//            seg.confidence = confidenceSum / confidenceCount;
//            m_keySegments.append(seg);
//
//            // Start new segment
//            lastTime = currentTime;
//            lastKey = currentKey;
//            confidenceSum = currentConfidence;
//            confidenceCount = 1;
//        } else {
//            confidenceSum += currentConfidence;
//            confidenceCount++;
//        }
//    }
//
//    // Add final segment
//    if (lastTime < m_trackDuration) {
//        AnalysisKeySegment seg;
//        seg.startTime = lastTime;
//        seg.endTime = m_trackDuration;
//        seg.duration = m_trackDuration - lastTime;
//        seg.key = keyToString(static_cast<int>(lastKey));
//        seg.type = "STABLE";
//        seg.confidence = confidenceSum / confidenceCount;
//        m_keySegments.append(seg);
//    }
//
//    qDebug() << "[QueenMaryKeyExtended] Built" << m_keySegments.size() <<
//    "segments with confidence"; for (const auto& seg : m_keySegments) {
//        qDebug() << "  Key:" << seg.key << "Confidence:" << seg.confidence <<
//        "%";
//    }
//}
//
// QString AnalyzerQueenMaryKeyExtended::keyToString(int key) const {
//    return KeyUtils::keyToString(static_cast<ChromaticKey>(key));
//}
//
// QVector<AnalysisKeySegment> AnalyzerQueenMaryKeyExtended::getKeySegments()
// const {
//    return m_keySegments;
//}
//
// QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
//    QJsonArray segmentsArray;
//    int segmentId = 1;
//
//    for (const auto& seg : m_keySegments) {
//        if (seg.duration < 0.001)
//            continue;
//
//        QJsonObject segmentObj;
//        segmentObj["id"] = segmentId++;
//        segmentObj["type"] = seg.type;
//        segmentObj["position"] = seg.startTime;
//        segmentObj["duration"] = seg.duration;
//        segmentObj["key"] = seg.key;
//        segmentObj["confidence"] = seg.confidence; // Add confidence to JSON
//        segmentObj["range_start"] = seg.startTime;
//        segmentObj["range_end"] = seg.endTime;
//
//        segmentsArray.append(segmentObj);
//    }
//
//    return segmentsArray;
//}
//
////QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
////    QJsonArray segmentsArray;
////    int segmentId = 1;
////    for (const auto& seg : m_keySegments) {
////        if (seg.duration < 0.001)
////            continue;
////        QJsonObject segmentObj;
////        segmentObj["id"] = segmentId++;
////        segmentObj["type"] = seg.type;
////        segmentObj["position"] = seg.startTime;
////        segmentObj["duration"] = seg.duration;
////        segmentObj["key"] = seg.key;
////        segmentObj["range_start"] = seg.startTime;
////        segmentObj["range_end"] = seg.endTime;
////        segmentsArray.append(segmentObj);
////    }
////    return segmentsArray;
////}
//
// double calculateConfidenceFromStrengths(double* keyStrengths, int
// detectedKeyIndex) {
//    if (!keyStrengths)
//        return 0.0;
//
//    double detectedStrength = keyStrengths[detectedKeyIndex];
//    double secondBestStrength = 0.0;
//
//    // Find the second best strength
//    for (int i = 0; i < 24; ++i) {
//        if (i != detectedKeyIndex && keyStrengths[i] > secondBestStrength) {
//            secondBestStrength = keyStrengths[i];
//        }
//    }
//
//    // Calculate confidence based on margin between best and second best
//    double margin = detectedStrength - secondBestStrength;
//    double confidence = 50.0 + (margin * 50.0); // Map margin to 50-100%
//
//    return qBound(0.0, confidence, 100.0);
//}
//
//} // namespace mixxx

// #include "analyzer/plugins/analyzerqueenmarykeyextended.h"
//
// #include <dsp/keydetection/GetKeyMode.h>
//
// #include <algorithm>
// #include <cmath>
// #include <numeric>
// #include <utility>
//
// #include "analyzer/constants.h"
// #include "track/keyutils.h"
// #include "util/assert.h"
//
// using mixxx::track::io::key::ChromaticKey;
//
// namespace mixxx {
// namespace {
//
// constexpr int kTuningFrequencyHertz = 440;
//
// } // namespace
//
// AnalyzerQueenMaryKeyExtended::AnalyzerQueenMaryKeyExtended()
//         : m_currentFrame(0),
//           m_sampleRate(44100),
//           m_trackDuration(0.0) {
// }
//
// AnalyzerQueenMaryKeyExtended::~AnalyzerQueenMaryKeyExtended() = default;
//
////bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
/// sampleRate) { /    m_resultKeys.clear(); /    m_keySegments.clear(); /
/// m_currentFrame = 0;
////
////    GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
////    m_pKeyMode = std::make_unique<GetKeyMode>(config);
////    size_t windowSize = m_pKeyMode->getBlockSize();
////    size_t stepSize = m_pKeyMode->getHopSize();
////
////    return m_helper.initialize(
////            windowSize, stepSize, [this](double* pWindow, size_t) {
////                int iKey = m_pKeyMode->process(pWindow);
////
////                if (iKey < 0) {
////                    qWarning() << "No valid key detected in analyzed
/// window:" << iKey; /                    DEBUG_ASSERT(!"iKey is invalid"); /
/// return false; /                } /                // Convert int to
/// ChromaticKey for the KeyChangeList /                ChromaticKey key =
/// static_cast<ChromaticKey>(iKey); / m_resultKeys.push_back(qMakePair( / key,
/// static_cast<double>(m_currentFrame))); /                return true; / });
////}
//
// bool AnalyzerQueenMaryKeyExtended::initialize(mixxx::audio::SampleRate
// sampleRate) {
//    m_sampleRate = sampleRate; // Store the sample rate
//    m_resultKeys.clear();
//    m_keySegments.clear();
//    m_currentFrame = 0;
//    m_trackDuration = 0.0;
//
//    GetKeyMode::Config config(sampleRate, kTuningFrequencyHertz);
//    m_pKeyMode = std::make_unique<GetKeyMode>(config);
//    size_t windowSize = m_pKeyMode->getBlockSize();
//    size_t stepSize = m_pKeyMode->getHopSize();
//
//    return m_helper.initialize(
//            windowSize, stepSize, [this](double* pWindow, size_t) {
//                int iKey = m_pKeyMode->process(pWindow);
//
//                if (iKey < 0) {
//                    qWarning() << "No valid key detected in analyzed window:"
//                    << iKey; DEBUG_ASSERT(!"iKey is invalid"); return false;
//                }
//                ChromaticKey key = static_cast<ChromaticKey>(iKey);
//                m_resultKeys.push_back(qMakePair(
//                        key, static_cast<double>(m_currentFrame)));
//                return true;
//            });
//}
//
// bool AnalyzerQueenMaryKeyExtended::processSamples(const CSAMPLE* pIn, SINT
// iLen) {
//    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//    if (!m_pKeyMode) {
//        return false;
//    }
//
//    const size_t numInputFrames = iLen / kAnalysisChannels;
//    m_currentFrame += numInputFrames;
//    return m_helper.processStereoSamples(pIn, iLen);
//}
//
////bool AnalyzerQueenMaryKeyExtended::finalize() {
////    m_helper.finalize();
////    m_pKeyMode.reset();
////
////    analyzeKeyChanges();
////
////    return true;
////}
//
// bool AnalyzerQueenMaryKeyExtended::finalize() {
//    m_helper.finalize();
//
//    // Set track duration in seconds
//    m_trackDuration = static_cast<double>(m_currentFrame) /
//    m_sampleRate.toDouble();
//
//    analyzeKeyChanges();
//
//    m_pKeyMode.reset();
//    return true;
//}
//
// QString AnalyzerQueenMaryKeyExtended::keyToString(ChromaticKey key) const {
//    return KeyUtils::keyToString(key);
//}
//
////void AnalyzerQueenMaryKeyExtended::analyzeKeyChanges() {
////    if (m_resultKeys.empty()) {
////        return;
////    }
////
////    // Build segments from key changes
////    m_keySegments.clear();
////
////    double lastTime = 0.0;
////    ChromaticKey lastKey = m_resultKeys[0].first;
////    KeyChangeList::size_type i;
////
////    for (i = 0; i < m_resultKeys.size(); ++i) {
////        double currentTime = m_resultKeys[i].second;
////        ChromaticKey currentKey = m_resultKeys[i].first;
////
////        if (currentKey != lastKey) {
////            AnalysisKeySegment seg;
////            seg.startTime = lastTime;
////            seg.endTime = currentTime;
////            seg.duration = currentTime - lastTime;
////            seg.key = keyToString(lastKey);
////            seg.type = "STABLE";
////            m_keySegments.append(seg);
////
////            lastTime = currentTime;
////            lastKey = currentKey;
////        }
////    }
////
////    // Add final segment
////    if (lastTime < m_trackDuration) {
////        AnalysisKeySegment seg;
////        seg.startTime = lastTime;
////        seg.endTime = m_trackDuration;
////        seg.duration = m_trackDuration - lastTime;
////        seg.key = keyToString(lastKey);
////        seg.type = "STABLE";
////        m_keySegments.append(seg);
////    }
////
////    // Snap to beats
////    snapSegmentsToBeats();
////
////    qDebug() << "[QueenMaryKeyExtended] Created" << m_keySegments.size() <<
///"key segments";
////}
//
// void AnalyzerQueenMaryKeyExtended::analyzeKeyChanges() {
//    if (m_resultKeys.empty()) {
//        return;
//    }
//
//    // Build segments from key changes
//    m_keySegments.clear();
//
//    // Convert frame positions to seconds using stored sample rate
//    double sampleRate = m_sampleRate.toDouble();
//
//    double lastTime = 0.0;
//    ChromaticKey lastKey = m_resultKeys[0].first;
//    KeyChangeList::size_type i;
//
//    for (i = 0; i < m_resultKeys.size(); ++i) {
//        // Convert frame to seconds
//        double currentTime = m_resultKeys[i].second / sampleRate;
//        ChromaticKey currentKey = m_resultKeys[i].first;
//
//        if (currentKey != lastKey) {
//            AnalysisKeySegment seg;
//            seg.startTime = lastTime;
//            seg.endTime = currentTime;
//            seg.duration = currentTime - lastTime;
//            seg.key = keyToString(lastKey);
//            seg.type = "STABLE";
//            m_keySegments.append(seg);
//
//            lastTime = currentTime;
//            lastKey = currentKey;
//        }
//    }
//
//    // Add final segment
//    if (lastTime < m_trackDuration) {
//        AnalysisKeySegment seg;
//        seg.startTime = lastTime;
//        seg.endTime = m_trackDuration;
//        seg.duration = m_trackDuration - lastTime;
//        seg.key = keyToString(lastKey);
//        seg.type = "STABLE";
//        m_keySegments.append(seg);
//    }
//
//    // Snap to beats
//    snapSegmentsToBeats();
//
//    qDebug() << "[QueenMaryKeyExtended] Created" << m_keySegments.size() <<
//    "key segments";
//}
//
// void AnalyzerQueenMaryKeyExtended::snapSegmentsToBeats() {
//    if (m_beatTimes.empty()) {
//        return;
//    }
//
//    for (auto& seg : m_keySegments) {
//        if (seg.startTime > 0.001) {
//            double nearestStart = findNearestBeat(seg.startTime, m_beatTimes);
//            if (std::abs(nearestStart - seg.startTime) < kMaxSnapDistance) {
//                seg.startTime = nearestStart;
//            }
//        }
//
//        double nearestEnd = findNearestBeat(seg.endTime, m_beatTimes);
//        if (std::abs(nearestEnd - seg.endTime) < kMaxSnapDistance) {
//            seg.endTime = nearestEnd;
//        }
//
//        seg.duration = seg.endTime - seg.startTime;
//    }
//}
//
// double AnalyzerQueenMaryKeyExtended::findNearestBeat(double time, const
// std::vector<double>& beatTimes) {
//    if (beatTimes.empty())
//        return time;
//
//    double nearest = beatTimes[0];
//    double minDist = std::abs(time - nearest);
//
//    for (double beat : beatTimes) {
//        double dist = std::abs(time - beat);
//        if (dist < minDist) {
//            minDist = dist;
//            nearest = beat;
//        }
//    }
//
//    return nearest;
//}
//
////QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
////    QJsonArray segmentsArray;
////    int segmentId = 1;
////
////    for (const auto& seg : std::as_const(m_keySegments)) {
////        if (seg.duration < 0.001)
////            continue;
////
////        QJsonObject segmentObj;
////        segmentObj["id"] = segmentId++;
////        segmentObj["type"] = seg.type;
////        segmentObj["position"] = seg.startTime;
////        segmentObj["duration"] = seg.duration;
////        segmentObj["key"] = seg.key;
////        segmentObj["range_start"] = seg.startTime;
////        segmentObj["range_end"] = seg.endTime;
////
////        segmentsArray.append(segmentObj);
////    }
////
////    return segmentsArray;
////}
//
// QJsonArray AnalyzerQueenMaryKeyExtended::getKeySegmentsJson() const {
//    QJsonArray segmentsArray;
//    int segmentId = 1;
//
//    for (const auto& seg : std::as_const(m_keySegments)) {
//        if (seg.duration < 0.001)
//            continue;
//
//        QJsonObject segmentObj;
//        segmentObj["id"] = segmentId++;
//        segmentObj["type"] = seg.type;
//        segmentObj["position"] = seg.startTime;
//        segmentObj["duration"] = seg.duration;
//        segmentObj["key"] = seg.key;
//        segmentObj["range_start"] = seg.startTime;
//        segmentObj["range_end"] = seg.endTime;
//
//        segmentsArray.append(segmentObj);
//    }
//
//    return segmentsArray;
//}
//
//} // namespace mixxx
