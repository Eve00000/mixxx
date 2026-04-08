#include "analyzer/plugins/analyzerqueenmarybeatsextended.h"

#include <dsp/onsets/DetectionFunction.h>
#include <dsp/tempotracking/TempoTrackV2.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

#include "analyzer/constants.h"
#include "util/math.h"

namespace mixxx {
namespace {

constexpr float kStepSecs = 0.01161f;
constexpr int kMaximumBinSizeHz = 50;

DFConfig makeDetectionFunctionConfig(int stepSizeFrames, int windowSize) {
    DFConfig config;
    config.DFType = DF_COMPLEXSD;
    config.stepSize = stepSizeFrames;
    config.frameLength = windowSize;
    config.dbRise = 3;
    config.adaptiveWhitening = false;
    config.whiteningRelaxCoeff = -1;
    config.whiteningFloor = -1;
    return config;
}

} // namespace

AnalyzerQueenMaryBeatsExtended::AnalyzerQueenMaryBeatsExtended()
        : m_windowSize(0),
          m_stepSizeFrames(0),
          m_trackDuration(0.0) {
    // Initialize musical context with defaults
    m_musicalContext.timeSignature = 4;
    m_musicalContext.beatsPerBar = 4;
    m_musicalContext.halfTimeFeel = false;
    m_musicalContext.perceivedBpm = 120.0;
    m_musicalContext.beatsPerPhrase = 32;
    m_musicalContext.phraseSeconds = 16.0;
}

AnalyzerQueenMaryBeatsExtended::~AnalyzerQueenMaryBeatsExtended() {
}

bool AnalyzerQueenMaryBeatsExtended::initialize(mixxx::audio::SampleRate sampleRate) {
    m_detectionResults.clear();
    m_beatTimes.clear();
    m_bpmSegments.clear();
    m_sampleRate = sampleRate;
    m_stepSizeFrames = static_cast<int>(m_sampleRate * kStepSecs);
    m_windowSize = MathUtilities::nextPowerOfTwo(m_sampleRate / kMaximumBinSizeHz);
    m_pDetectionFunction = std::make_unique<DetectionFunction>(
            makeDetectionFunctionConfig(m_stepSizeFrames, m_windowSize));
    qDebug() << "[QueenMaryBeatsExtended] Sample rate:" << m_sampleRate
             << "Step size:" << m_stepSizeFrames;

    m_helper.initialize(
            m_windowSize, m_stepSizeFrames, [this](double* pWindow, size_t) {
                m_detectionResults.push_back(
                        m_pDetectionFunction->processTimeDomain(pWindow));
                return true;
            });
    return true;
}

bool AnalyzerQueenMaryBeatsExtended::processSamples(const CSAMPLE* pIn, SINT iLen) {
    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
    if (!m_pDetectionFunction) {
        return false;
    }
    return m_helper.processStereoSamples(pIn, iLen);
}

double AnalyzerQueenMaryBeatsExtended::calculateLocalBpm(const std::vector<double>& beatTimes,
        int startIdx,
        int windowSize) {
    if (startIdx + windowSize >= static_cast<int>(beatTimes.size())) {
        return 0.0;
    }

    double totalInterval = beatTimes[startIdx + windowSize] - beatTimes[startIdx];
    double avgInterval = totalInterval / windowSize;
    return 60.0 / avgInterval;
}

double AnalyzerQueenMaryBeatsExtended::calculateConfidence(const std::vector<double>& intervals) {
    if (intervals.size() < 2) {
        return 0.6;
    }

    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    double mean = sum / static_cast<double>(intervals.size());

    double sqSum = std::accumulate(intervals.begin(),
            intervals.end(),
            0.0,
            [mean](double acc, double val) {
                return acc + (val - mean) * (val - mean);
            });
    double stdDev = std::sqrt(sqSum / static_cast<double>(intervals.size()));

    return 1.0 / (1.0 + stdDev * 15.0);
}

void AnalyzerQueenMaryBeatsExtended::smoothBpmData(
        std::vector<double>& bpmValues, int windowLength) {
    if (static_cast<int>(bpmValues.size()) < windowLength) {
        return;
    }

    std::vector<double> smoothed(bpmValues.size());
    int halfWindow = windowLength / 2;

    for (size_t i = 0; i < bpmValues.size(); ++i) {
        int start = std::max(0, static_cast<int>(i) - halfWindow);
        int end = std::min(static_cast<int>(bpmValues.size()) - 1,
                static_cast<int>(i) + halfWindow);
        int count = end - start + 1;

        double sum = 0.0;
        for (int j = start; j <= end; ++j) {
            sum += bpmValues[static_cast<size_t>(j)];
        }
        smoothed[i] = sum / static_cast<double>(count);
    }

    bpmValues = smoothed;
}

void AnalyzerQueenMaryBeatsExtended::detectMusicalContext() {
    if (m_beatTimes.size() < 100) {
        qDebug() << "[QueenMaryBeatsExtended] Not enough beats to detect musical context";
        return;
    }

    // Calculate average BPM
    double totalInterval = m_beatTimes.back() - m_beatTimes.front();
    double avgInterval = totalInterval / (m_beatTimes.size() - 1);
    double avgBpm = 60.0 / avgInterval;

    qDebug() << "[QueenMaryBeatsExtended] Average BPM:" << avgBpm;

    // Detect time signature by analyzing beat intervals
    std::vector<double> intervals;
    for (size_t i = 1; i < m_beatTimes.size(); ++i) {
        intervals.push_back(m_beatTimes[i] - m_beatTimes[i - 1]);
    }

    // Look for 3/4 pattern (strong-weak-weak)
    bool isThreeFour = false;
    if (intervals.size() > 6) {
        // Check if intervals repeat every 3 beats
        double sumDiff1 = 0, sumDiff2 = 0;
        int count = 0;
        for (size_t i = 3; i < intervals.size(); ++i) {
            sumDiff1 += std::abs(intervals[i] - intervals[i - 3]);
            if (i > 3) {
                sumDiff2 += std::abs(intervals[i] - intervals[i - 2]);
            }
            count++;
        }
        double avgDiff1 = sumDiff1 / count;
        double avgDiff2 = sumDiff2 / count;

        // If 3-beat pattern is more consistent than 2-beat, it's likely 3/4
        if (avgDiff1 < avgDiff2 && avgDiff1 < 0.01) {
            isThreeFour = true;
        }
    }

    // Detect half-time feel (dubstep)
    bool halfTimeFeel = false;
    if (avgBpm >= 130 && avgBpm <= 160) {
        // Look for snare pattern (strong beat every 2 beats)
        double perceivedBpmTest = avgBpm / 2;
        qDebug() << "[QueenMaryBeatsExtended] Potential half-time feel, "
                    "perceived BPM:"
                 << perceivedBpmTest;

        // Check if there's a consistent pattern at half the tempo
        double halfTimeInterval = 2.0 * avgInterval;
        int matches = 0;
        for (size_t i = 2; i < m_beatTimes.size(); i += 2) {
            double actualInterval = m_beatTimes[i] - m_beatTimes[i - 2];
            if (std::abs(actualInterval - halfTimeInterval) < 0.02) {
                matches++;
            }
        }
        if (matches > static_cast<int>(m_beatTimes.size()) / 10) {
            halfTimeFeel = true;
        }
    }

    // Set musical context
    if (isThreeFour) {
        m_musicalContext.timeSignature = 3;
        m_musicalContext.beatsPerBar = 3;
        m_musicalContext.beatsPerPhrase = 24; // 8 bars of 3/4
    } else {
        m_musicalContext.timeSignature = 4;
        m_musicalContext.beatsPerBar = 4;
        if (halfTimeFeel) {
            m_musicalContext.beatsPerPhrase = 16; // Half-time feel
            m_musicalContext.halfTimeFeel = true;
            m_musicalContext.perceivedBpm = avgBpm / 2;
        } else {
            m_musicalContext.beatsPerPhrase = 32; // 8 bars of 4/4
            m_musicalContext.perceivedBpm = avgBpm;
        }
    }

    m_musicalContext.phraseSeconds = (m_musicalContext.beatsPerPhrase * 60.0) /
            (m_musicalContext.halfTimeFeel ? avgBpm / 2 : avgBpm);

    qDebug() << "[QueenMaryBeatsExtended] Musical context:"
             << "Time signature:" << m_musicalContext.timeSignature << "/4"
             << "Beats per phrase:" << m_musicalContext.beatsPerPhrase
             << "Phrase seconds:" << m_musicalContext.phraseSeconds
             << "Half-time feel:" << m_musicalContext.halfTimeFeel;
}

double AnalyzerQueenMaryBeatsExtended::getBpmAtTime(double time,
        const std::vector<double>& bpmTimes,
        const std::vector<double>& bpmValues) {
    if (bpmTimes.empty()) {
        return 120.0;
    }
    if (time <= bpmTimes[0]) {
        return bpmValues[0];
    }
    if (time >= bpmTimes.back()) {
        return bpmValues.back();
    }

    for (size_t i = 1; i < bpmTimes.size(); ++i) {
        if (time <= bpmTimes[i]) {
            double t1 = bpmTimes[i - 1];
            double t2 = bpmTimes[i];
            double b1 = bpmValues[i - 1];
            double b2 = bpmValues[i];

            if (t2 > t1) {
                double ratio = (time - t1) / (t2 - t1);
                return b1 + ratio * (b2 - b1);
            }
            return b1;
        }
    }
    return bpmValues.back();
}

// flatpack compiler gave an error on
//     for (double beat : beatTimes)
// replaced with
//     for (auto it = beatTimes.begin(); it != beatTimes.end(); ++it) {
// also error, replaced with
//     for (size_t i = 1; i < beatTimes.size(); ++i) {

// double AnalyzerQueenMaryBeatsExtended::findNearestBeat(
//         double time, const std::vector<double>& beatTimes) {
//     if (beatTimes.empty())
//         return time;
//
//     double nearest = beatTimes[0];
//     double minDist = std::abs(time - nearest);
//
//     for (auto it = beatTimes.begin(); it != beatTimes.end(); ++it) {
//         double dist = std::abs(time - *it);
//         if (dist < minDist) {
//             minDist = dist;
//             nearest = *it;
//         }
//     }
//
//     return nearest;
// }

double AnalyzerQueenMaryBeatsExtended::findNearestBeat(
        double time, const std::vector<double>& beatTimes) {
    if (beatTimes.empty()) {
        return time;
    }

    size_t nearestIndex = 0;
    double minDist = std::abs(time - beatTimes[0]);

    for (size_t i = 1; i < beatTimes.size(); ++i) {
        double dist = std::abs(time - beatTimes[i]);
        if (dist < minDist) {
            minDist = dist;
            nearestIndex = i;
        }
    }

    return beatTimes[nearestIndex];
}

void AnalyzerQueenMaryBeatsExtended::snapSegmentsToBeats() {
    if (m_beatTimes.empty()) {
        return;
    }

    for (auto& seg : m_bpmSegments) {
        // Snap start time to nearest beat (skip time 0)
        if (seg.startTime > 0.001) {
            double nearestStart = findNearestBeat(seg.startTime, m_beatTimes);
            if (std::abs(nearestStart - seg.startTime) < kMaxSnapDistance) {
                seg.startTime = nearestStart;
            }
        }

        // Snap end time to nearest beat
        double nearestEnd = findNearestBeat(seg.endTime, m_beatTimes);
        if (std::abs(nearestEnd - seg.endTime) < kMaxSnapDistance) {
            seg.endTime = nearestEnd;
        }

        seg.duration = seg.endTime - seg.startTime;
    }
}

void AnalyzerQueenMaryBeatsExtended::analyzeBpmChanges() {
    if (m_resultBeats.size() < kWindowSizeBeats) {
        qDebug() << "[QueenMaryBeatsExtended] Not enough beats for BPM analysis";
        return;
    }

    // Convert beat positions from frames to seconds
    m_beatTimes.clear();
    for (const auto& beat : std::as_const(m_resultBeats)) {
        m_beatTimes.push_back(beat.value() / m_sampleRate);
    }

    m_trackDuration = m_beatTimes.back();

    // Detect musical context (time signature, phrase length)
    detectMusicalContext();

    // Calculate BPM at sliding windows
    std::vector<double> bpmTimes;
    std::vector<double> bpmValues;

    for (size_t i = 0; i + kWindowSizeBeats <= m_beatTimes.size(); i += kStepSizeBeats) {
        double bpm = calculateLocalBpm(m_beatTimes, static_cast<int>(i), kWindowSizeBeats);
        if (bpm > 0 && bpm < 300) {
            size_t windowCenter = i + kWindowSizeBeats / 2;
            bpmTimes.push_back(m_beatTimes[windowCenter]);
            bpmValues.push_back(bpm);
        }
    }

    if (bpmValues.empty()) {
        qDebug() << "[QueenMaryBeatsExtended] No BPM values calculated";
        return;
    }

    // Smooth BPM data
    std::vector<double> bpmSmooth = bpmValues;
    smoothBpmData(bpmSmooth, kSmoothWindowLength);

    // Detect change points based on rate of change
    std::vector<double> changeTimes;

    for (size_t i = 2; i < bpmSmooth.size() - 2; ++i) {
        double timeSpan = bpmTimes[i + 2] - bpmTimes[i - 2];
        if (timeSpan <= 0) {
            continue;
        }

        double bpmChange = bpmSmooth[i + 2] - bpmSmooth[i - 2];
        double ratePerSec = std::abs(bpmChange) / timeSpan;

        if (ratePerSec >= kMinRateChange) {
            changeTimes.push_back(bpmTimes[i]);
            i += 3;
        }
    }

    // Add start and end
    changeTimes.insert(changeTimes.begin(), 0.0);
    changeTimes.push_back(m_trackDuration);

    // Remove duplicates
    std::sort(changeTimes.begin(), changeTimes.end());
    changeTimes.erase(
            std::unique(changeTimes.begin(),
                    changeTimes.end(),
                    [](double a, double b) { return std::abs(a - b) < 1.0; }),
            changeTimes.end());

    // Create segments between change points
    m_bpmSegments.clear();

    for (size_t idx = 0; idx < changeTimes.size() - 1; ++idx) {
        double startTime = changeTimes[idx];
        double endTime = changeTimes[idx + 1];
        double duration = endTime - startTime;

        if (duration < 1.0) {
            continue;
        }

        double startBpm = getBpmAtTime(startTime, bpmTimes, bpmSmooth);
        double endBpm = getBpmAtTime(endTime, bpmTimes, bpmSmooth);

        double totalChange = endBpm - startBpm;
        double percentPerMinute = std::abs(totalChange) / startBpm * 100.0 / (duration / 60.0);

        QString type;

        if (percentPerMinute < kStablePercentPerMinute) {
            type = "STABLE";
            double avgBpm = (startBpm + endBpm) / 2.0;
            startBpm = avgBpm;
            endBpm = avgBpm;
        } else if (totalChange > 0) {
            type = "INCREASE";
        } else {
            type = "DECREASE";
        }

        AnalysisBPMSegment seg;
        seg.startTime = startTime;
        seg.endTime = endTime;
        seg.duration = duration;
        seg.type = type;
        seg.startBPM = startBpm;
        seg.endBPM = endBpm;

        m_bpmSegments.append(seg);
    }

    // Merge adjacent stable segments with similar BPM
    if (m_bpmSegments.size() > 1) {
        QVector<AnalysisBPMSegment> merged;
        AnalysisBPMSegment current = m_bpmSegments[0];

        for (int i = 1; i < m_bpmSegments.size(); ++i) {
            const AnalysisBPMSegment& next = m_bpmSegments[i];

            if (current.type == "STABLE" && next.type == "STABLE" &&
                    std::abs(current.startBPM - next.startBPM) < 1.5) {
                current.endTime = next.endTime;
                current.duration += next.duration;
                current.endBPM = next.endBPM;
                current.startBPM = (current.startBPM + next.startBPM) / 2.0;
                current.endBPM = current.startBPM;
            } else {
                merged.append(current);
                current = next;
            }
        }
        merged.append(current);
        m_bpmSegments = merged;
    }

    // Snap to beats
    snapSegmentsToBeats();

    qDebug() << "[QueenMaryBeatsExtended] Created" << m_bpmSegments.size() << "segments";
    for (const auto& seg : std::as_const(m_bpmSegments)) {
        qDebug() << "  " << seg.type << ":" << seg.startTime << "-" << seg.endTime
                 << "BPM:" << seg.startBPM << "->" << seg.endBPM;
    }
}

bool AnalyzerQueenMaryBeatsExtended::finalize() {
    m_helper.finalize();

    size_t nonZeroCount = m_detectionResults.size();
    while (nonZeroCount > 0 && m_detectionResults.at(nonZeroCount - 1) <= 0.0) {
        --nonZeroCount;
    }

    size_t required_size = std::max(static_cast<size_t>(2), nonZeroCount) - 2;

    std::vector<double> df;
    df.reserve(required_size);
    auto beatPeriod = std::vector<int>(required_size / 128 + 1);

    for (size_t i = 2; i < nonZeroCount; ++i) {
        df.push_back(m_detectionResults.at(i));
    }

    TempoTrackV2 tt(m_sampleRate, m_stepSizeFrames);
    tt.calculateBeatPeriod(df, beatPeriod);

    std::vector<double> beats;
    tt.calculateBeats(df, beatPeriod, beats);

    m_resultBeats.reserve(static_cast<int>(beats.size()));
    for (size_t i = 0; i < beats.size(); ++i) {
        const auto result = mixxx::audio::FramePos(
                (beats.at(i) * m_stepSizeFrames) + m_stepSizeFrames / 2);
        m_resultBeats.push_back(result);
    }

    analyzeBpmChanges();

    m_pDetectionFunction.reset();
    return true;
}

QJsonArray AnalyzerQueenMaryBeatsExtended::getBpmSegmentsJson() const {
    QJsonArray segmentsArray;
    int segmentId = 1;

    for (const auto& seg : std::as_const(m_bpmSegments)) {
        if (seg.duration < 0.001) {
            continue;
        }

        QJsonObject segmentObj;
        segmentObj["id"] = segmentId++;
        segmentObj["type"] = seg.type;
        segmentObj["position"] = seg.startTime;
        segmentObj["duration"] = seg.duration;
        segmentObj["bpm_start"] = seg.startBPM;
        segmentObj["bpm_end"] = seg.endBPM;
        segmentObj["range_start"] = seg.startTime;
        segmentObj["range_end"] = seg.endTime;

        segmentsArray.append(segmentObj);
    }

    return segmentsArray;
}

<<<<<<< HEAD
} // namespace mixxx
=======
} // namespace mixxx

// #include "analyzer/plugins/analyzerqueenmarybeatsextended.h"
//
// #include <dsp/onsets/DetectionFunction.h>
// #include <dsp/tempotracking/TempoTrackV2.h>
//
// #include <algorithm>
// #include <cmath>
// #include <numeric>
// #include <set>
// #include <utility>
//
// #include "analyzer/constants.h"
// #include "util/math.h"
//
// namespace mixxx {
// namespace {
//
// constexpr float kStepSecs = 0.01161f;
// constexpr int kMaximumBinSizeHz = 50;
//
//// BPM per second to detect change
// static constexpr double kMinRateChange = 0.5;
//// Max BPM range over window to be STABLE
// static constexpr double kStableMaxRange = 1.0;
//// Window duration for stability check
// static constexpr double kStableWindowSeconds = 10.0;
//
// static constexpr double kBpmWindowSeconds = 30.0;  // Window for average BPM
// (seconds) static constexpr double kBpmChangeThreshold = 2.0; // BPM change to
// create new segment
//
// DFConfig makeDetectionFunctionConfig(int stepSizeFrames, int windowSize) {
//     DFConfig config;
//     config.DFType = DF_COMPLEXSD;
//     config.stepSize = stepSizeFrames;
//     config.frameLength = windowSize;
//     config.dbRise = 3;
//     config.adaptiveWhitening = false;
//     config.whiteningRelaxCoeff = -1;
//     config.whiteningFloor = -1;
//     return config;
// }
//
// } // namespace
//
//// static constexpr int kWindowSizeBeats = 12;
//// static constexpr int kStepSizeBeats = 3;
//// static constexpr int kSmoothWindowLength = 7;
//
// AnalyzerQueenMaryBeatsExtended::AnalyzerQueenMaryBeatsExtended()
//        : m_windowSize(0),
//          m_stepSizeFrames(0),
//          m_trackDuration(0.0) {
//}
//
// AnalyzerQueenMaryBeatsExtended::~AnalyzerQueenMaryBeatsExtended() {
//}
//
// bool AnalyzerQueenMaryBeatsExtended::initialize(mixxx::audio::SampleRate
// sampleRate) {
//    m_detectionResults.clear();
//    m_beatTimes.clear();
//    m_bpmSegments.clear();
//    m_sampleRate = sampleRate;
//    m_stepSizeFrames = static_cast<int>(m_sampleRate * kStepSecs);
//    m_windowSize = MathUtilities::nextPowerOfTwo(m_sampleRate /
//    kMaximumBinSizeHz); m_pDetectionFunction =
//    std::make_unique<DetectionFunction>(
//            makeDetectionFunctionConfig(m_stepSizeFrames, m_windowSize));
//    qDebug() << "[QueenMaryBeatsExtended] Sample rate:" << m_sampleRate
//             << "Step size:" << m_stepSizeFrames;
//
//    m_helper.initialize(
//            m_windowSize, m_stepSizeFrames, [this](double* pWindow, size_t) {
//                m_detectionResults.push_back(
//                        m_pDetectionFunction->processTimeDomain(pWindow));
//                return true;
//            });
//    return true;
//}
//
// bool AnalyzerQueenMaryBeatsExtended::processSamples(const CSAMPLE* pIn, SINT
// iLen) {
//    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
//    if (!m_pDetectionFunction) {
//        return false;
//    }
//    return m_helper.processStereoSamples(pIn, iLen);
//}
//
// double AnalyzerQueenMaryBeatsExtended::calculateLocalBpm(const
// std::vector<double>& beatTimes,
//        int startIdx,
//        int windowSize) {
//    if (startIdx + windowSize >= static_cast<int>(beatTimes.size())) {
//        return 0.0;
//    }
//
//    double totalInterval = beatTimes[startIdx + windowSize] -
//    beatTimes[startIdx]; double avgInterval = totalInterval / windowSize;
//    return 60.0 / avgInterval;
//}
//
// double AnalyzerQueenMaryBeatsExtended::calculateConfidence(const
// std::vector<double>& intervals) {
//    if (intervals.size() < 2)
//        return 0.6;
//
//    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);
//    double mean = sum / static_cast<double>(intervals.size());
//
//    double sqSum = std::accumulate(intervals.begin(),
//            intervals.end(),
//            0.0,
//            [mean](double acc, double val) {
//                return acc + (val - mean) * (val - mean);
//            });
//    double stdDev = std::sqrt(sqSum / static_cast<double>(intervals.size()));
//
//    return 1.0 / (1.0 + stdDev * 15.0);
//}
//
// void AnalyzerQueenMaryBeatsExtended::smoothBpmData(
//        std::vector<double>& bpmValues, int windowLength) {
//    if (static_cast<int>(bpmValues.size()) < windowLength)
//        return;
//
//    std::vector<double> smoothed(bpmValues.size());
//    int halfWindow = windowLength / 2;
//
//    for (size_t i = 0; i < bpmValues.size(); ++i) {
//        int start = std::max(0, static_cast<int>(i) - halfWindow);
//        int end = std::min(static_cast<int>(bpmValues.size()) - 1,
//                static_cast<int>(i) + halfWindow);
//        int count = end - start + 1;
//
//        double sum = 0.0;
//        for (int j = start; j <= end; ++j) {
//            sum += bpmValues[static_cast<size_t>(j)];
//        }
//        smoothed[i] = sum / static_cast<double>(count);
//    }
//
//    bpmValues = smoothed;
//}
//
////void AnalyzerQueenMaryBeatsExtended::analyzeBpmChanges() {
////    if (m_resultBeats.size() < 12) {
////        qDebug() << "[QueenMaryBeatsExtended] Not enough beats for BPM
// analysis"; /        return; /    }
////
////    // Convert beat positions from frames to seconds
////    m_beatTimes.clear();
////    for (const auto& beat : std::as_const(m_resultBeats)) {
////        m_beatTimes.push_back(beat.value() / m_sampleRate);
////    }
////
////    m_trackDuration = m_beatTimes.back();
////
////    // Calculate BPM at sliding windows
////    std::vector<double> bpmTimes;
////    std::vector<double> bpmValues;
////
////    for (size_t i = 0; i + 12 <= m_beatTimes.size(); i += 3) {
////        double totalInterval = m_beatTimes[i + 12] - m_beatTimes[i];
////        double avgInterval = totalInterval / 12;
////        double localBpm = 60.0 / avgInterval;
////
////        size_t windowCenter = i + 6;
////        bpmTimes.push_back(m_beatTimes[windowCenter]);
////        bpmValues.push_back(localBpm);
////    }
////
////    if (bpmValues.empty()) {
////        qDebug() << "[QueenMaryBeatsExtended] No BPM values calculated";
////        return;
////    }
////
////    // Light smoothing
////    std::vector<double> bpmSmooth = bpmValues;
////    int smoothWindow = 5;
////    // if (bpmSmooth.size() >= smoothWindow) {
////    if (static_cast<int>(bpmSmooth.size()) >= smoothWindow) {
////        std::vector<double> smoothed(bpmSmooth.size());
////        for (size_t i = 0; i < bpmSmooth.size(); ++i) {
////            int start = std::max(0, static_cast<int>(i) - 2);
////            int end = std::min(static_cast<int>(bpmSmooth.size()) - 1,
// static_cast<int>(i) + 2); /            double sum = 0; /            int count
//= 0; /            for (int j = start; j <= end; ++j) { /                sum
//+= bpmSmooth[j]; /                count++; /            } / smoothed[i] = sum
//// count; /        } /        bpmSmooth = smoothed; /    }
////
////    // Detect change points based on rate of change (BPM per second)
////    std::vector<double> changeTimes;
////    double minRateChange = 0.5; // 0.3 BPM / sec = change
////    // double minDuration = 5.0;
////
////    for (size_t i = 2; i < bpmSmooth.size() - 2; ++i) {
////        // Calculate rate of change over a window
////        double timeSpan = bpmTimes[i + 2] - bpmTimes[i - 2];
////        if (timeSpan <= 0)
////            continue;
////
////        double bpmChange = bpmSmooth[i + 2] - bpmSmooth[i - 2];
////        double ratePerSec = std::abs(bpmChange) / timeSpan;
////
////        if (ratePerSec >= minRateChange) {
////            // significant change point !!
////            changeTimes.push_back(bpmTimes[i]);
////            i += 3;
////        }
////    }
////
////    // add start and end
////    changeTimes.insert(changeTimes.begin(), 0.0);
////    changeTimes.push_back(m_trackDuration);
////
////    // duplicates remove
////    std::sort(changeTimes.begin(), changeTimes.end());
////    changeTimes.erase(
////            std::unique(changeTimes.begin(),
////                    changeTimes.end(),
////                    [](double a, double b) { return std::abs(a - b) < 1.0;
//}), /            changeTimes.end());
////
////    // get BPM at a specific time
////    auto getBpmAtTime = [&](double time) -> double {
////        if (bpmTimes.empty())
////            return 120.0;
////        if (time <= bpmTimes[0])
////            return bpmSmooth[0];
////        if (time >= bpmTimes.back())
////            return bpmSmooth.back();
////
////        for (size_t i = 1; i < bpmTimes.size(); ++i) {
////            if (time <= bpmTimes[i]) {
////                double t1 = bpmTimes[i - 1];
////                double t2 = bpmTimes[i];
////                double b1 = bpmSmooth[i - 1];
////                double b2 = bpmSmooth[i];
////
////                if (t2 > t1) {
////                    double ratio = (time - t1) / (t2 - t1);
////                    return b1 + ratio * (b2 - b1);
////                }
////                return b1;
////            }
////        }
////        return bpmSmooth.back();
////    };
////
////    // creating segments between change points
////    m_bpmSegments.clear();
////
////    for (size_t idx = 0; idx < changeTimes.size() - 1; ++idx) {
////        double startTime = changeTimes[idx];
////        double endTime = changeTimes[idx + 1];
////        double duration = endTime - startTime;
////
////        if (duration < 1.0)
////            continue;
////
////        double startBpm = getBpmAtTime(startTime);
////        double endBpm = getBpmAtTime(endTime);
////
////        // calc rate of change per second for this segment
////        double totalChange = endBpm - startBpm;
////        double ratePerSec = std::abs(totalChange) / duration;
////
////        // segment type ?
////        QString type;
////
////        // rate is very low: < 0.05 BPM/sec -> STABLE
////        if (ratePerSec < 0.05) {
////            type = "STABLE";
////            double avgBpm = (startBpm + endBpm) / 2.0;
////            startBpm = avgBpm;
////            endBpm = avgBpm;
////        } else if (totalChange > 0) {
////            type = "INCREASE";
////        } else {
////            type = "DECREASE";
////        }
////
////        BPMSegment seg;
////        seg.startTime = startTime;
////        seg.endTime = endTime;
////        seg.duration = duration;
////        seg.type = type;
////        seg.startBPM = startBpm;
////        seg.endBPM = endBpm;
////        seg.changeAmount = totalChange;
////
////        m_bpmSegments.append(seg);
////    }
////
////    // adjacent stable segments? -> merge
////    if (m_bpmSegments.size() > 1) {
////        QVector<BPMSegment> merged;
////        BPMSegment current = m_bpmSegments[0];
////
////        for (int i = 1; i < m_bpmSegments.size(); ++i) {
////            const BPMSegment& next = m_bpmSegments[i];
////
////            if (current.type == "STABLE" && next.type == "STABLE" &&
////                    std::abs(current.startBPM - next.startBPM) < 1.5) {
////                current.endTime = next.endTime;
////                current.duration += next.duration;
////                current.endBPM = next.endBPM;
////                current.startBPM = (current.startBPM + next.startBPM) / 2.0;
////                current.endBPM = current.startBPM;
////            } else {
////                merged.append(current);
////                current = next;
////            }
////        }
////        merged.append(current);
////        m_bpmSegments = merged;
////    }
////
////    qDebug() << "[QueenMaryBeatsExtended] Created" << m_bpmSegments.size()
//<< "segments before snapping them to beats"; /    for (const auto& seg :
// std::as_const(m_bpmSegments)) { /        qDebug() << "  " << seg.type << ":"
//<< seg.startTime << "-" << seg.endTime /                 << "BPM:" <<
// seg.startBPM << "->" << seg.endBPM /                 << "rate:" <<
// std::abs(seg.changeAmount) / seg.duration; /    }
////
////
////    // snap segment boundaries to nearest beats
////    snapSegmentsToBeats();
////
////    qDebug() << "[QueenMaryBeatsExtended] Created" << m_bpmSegments.size()
//<< "segments after snapping to beats"; /    for (const auto& seg :
// std::as_const(m_bpmSegments)) { /        qDebug() << "  " << seg.type << ":"
//<< seg.startTime << "-" << seg.endTime /                 << "BPM:" <<
// seg.startBPM << "->" << seg.endBPM /                 << "rate:" <<
// std::abs(seg.changeAmount) / seg.duration; /    }
////}
//
////void AnalyzerQueenMaryBeatsExtended::analyzeBpmChanges() {
////    if (m_resultBeats.size() < kWindowSizeBeats) {
////        qDebug() << "[QueenMaryBeatsExtended] Not enough beats for BPM
// analysis"; /        return; /    }
////
////    // Convert beat positions from frames to seconds
////    m_beatTimes.clear();
////    for (const auto& beat : std::as_const(m_resultBeats)) {
////        m_beatTimes.push_back(beat.value() / m_sampleRate);
////    }
////
////    m_trackDuration = m_beatTimes.back();
////
////    // Calculate BPM at sliding windows
////    std::vector<double> bpmTimes;
////    std::vector<double> bpmValues;
////
////    for (size_t i = 0; i + kWindowSizeBeats <= m_beatTimes.size(); i +=
// kStepSizeBeats) { /        double totalInterval = m_beatTimes[i +
// kWindowSizeBeats] - m_beatTimes[i]; /        double avgInterval =
// totalInterval / kWindowSizeBeats; /        double localBpm = 60.0 /
// avgInterval;
////
////        if (localBpm > 50 && localBpm < 250) {
////            size_t windowCenter = i + kWindowSizeBeats / 2;
////            bpmTimes.push_back(m_beatTimes[windowCenter]);
////            bpmValues.push_back(localBpm);
////        }
////    }
////
////    if (bpmValues.empty()) {
////        qDebug() << "[QueenMaryBeatsExtended] No BPM values calculated";
////        return;
////    }
////
////    // Light smoothing
////    std::vector<double> bpmSmooth = bpmValues;
////    smoothBpmData(bpmSmooth, kSmoothWindowLength);
////
////    // Detect change points based on rate of change
////    std::vector<double> changeTimes;
////    double minRateChange = 0.5;
////
////    for (size_t i = 2; i < bpmSmooth.size() - 2; ++i) {
////        double timeSpan = bpmTimes[i + 2] - bpmTimes[i - 2];
////        if (timeSpan <= 0)
////            continue;
////
////        double bpmChange = bpmSmooth[i + 2] - bpmSmooth[i - 2];
////        double ratePerSec = std::abs(bpmChange) / timeSpan;
////
////        if (ratePerSec >= minRateChange) {
////            changeTimes.push_back(bpmTimes[i]);
////            i += 3;
////        }
////    }
////
////    // Add start and end
////    changeTimes.insert(changeTimes.begin(), 0.0);
////    changeTimes.push_back(m_trackDuration);
////
////    // Remove duplicates
////    std::sort(changeTimes.begin(), changeTimes.end());
////    changeTimes.erase(
////            std::unique(changeTimes.begin(), changeTimes.end(), [](double a,
// double b) { return std::abs(a - b) < 1.0; }), / changeTimes.end());
////
////    // Helper function to get BPM at a specific time
////    auto getBpmAtTime = [&](double time) -> double {
////        if (bpmTimes.empty())
////            return 120.0;
////        if (time <= bpmTimes[0])
////            return bpmSmooth[0];
////        if (time >= bpmTimes.back())
////            return bpmSmooth.back();
////
////        for (size_t i = 1; i < bpmTimes.size(); ++i) {
////            if (time <= bpmTimes[i]) {
////                double t1 = bpmTimes[i - 1];
////                double t2 = bpmTimes[i];
////                double b1 = bpmSmooth[i - 1];
////                double b2 = bpmSmooth[i];
////
////                if (t2 > t1) {
////                    double ratio = (time - t1) / (t2 - t1);
////                    return b1 + ratio * (b2 - b1);
////                }
////                return b1;
////            }
////        }
////        return bpmSmooth.back();
////    };
////
////    // Helper function to check if a segment is stable
////    auto isSegmentStable = [&](double startTime, double endTime) -> bool {
////        double windowStart = startTime;
////
////        while (windowStart < endTime) {
////            double windowEnd = std::min(windowStart + kStableWindowSeconds,
// endTime);
////
////            // Find min and max BPM within this window
////            double minBpm = 1e9;
////            double maxBpm = -1e9;
////            bool hasData = false;
////
////            for (size_t j = 0; j < bpmTimes.size(); ++j) {
////                if (bpmTimes[j] >= windowStart && bpmTimes[j] <= windowEnd)
//{ /                    hasData = true; /                    if (bpmSmooth[j]
//< minBpm) /                        minBpm = bpmSmooth[j]; / if (bpmSmooth[j]
//> maxBpm) /                        maxBpm = bpmSmooth[j]; /                }
////            }
////
////            if (hasData && (maxBpm - minBpm) > kStableMaxRange) {
////                return false;
////            }
////
////            windowStart = windowEnd;
////        }
////
////        return true;
////    };
////
////    // Create segments between change points
////    m_bpmSegments.clear();
////
////    for (size_t idx = 0; idx < changeTimes.size() - 1; ++idx) {
////        double startTime = changeTimes[idx];
////        double endTime = changeTimes[idx + 1];
////        double duration = endTime - startTime;
////
////        if (duration < 1.0)
////            continue;
////
////        double startBpm = getBpmAtTime(startTime);
////        double endBpm = getBpmAtTime(endTime);
////
////        double totalChange = endBpm - startBpm;
////        double ratePerSec = std::abs(totalChange) / duration;
////
////        // Determine segment type
////        QString type;
////
////        // Check if segment is stable using range-based check
////        if (isSegmentStable(startTime, endTime)) {
////            type = "STABLE";
////            // Calculate average BPM for stable segment
////            double sum = 0;
////            int count = 0;
////            for (size_t j = 0; j < bpmTimes.size(); ++j) {
////                if (bpmTimes[j] >= startTime && bpmTimes[j] <= endTime) {
////                    sum += bpmSmooth[j];
////                    count++;
////                }
////            }
////            double avgBpm = (count > 0) ? sum / count : (startBpm + endBpm)
//// 2.0; /            startBpm = avgBpm; /            endBpm = avgBpm; / } else
// if (totalChange > 0) { /            type = "INCREASE"; /        } else { /
// type = "DECREASE"; /        }
////
////        BPMSegment seg;
////        seg.startTime = startTime;
////        seg.endTime = endTime;
////        seg.duration = duration;
////        seg.type = type;
////        seg.startBPM = startBpm;
////        seg.endBPM = endBpm;
////        seg.changeAmount = totalChange;
////
////        m_bpmSegments.append(seg);
////    }
////
////    // Merge adjacent stable segments with similar BPM
////    if (m_bpmSegments.size() > 1) {
////        QVector<BPMSegment> merged;
////        BPMSegment current = m_bpmSegments[0];
////
////        for (int i = 1; i < m_bpmSegments.size(); ++i) {
////            const BPMSegment& next = m_bpmSegments[i];
////
////            if (current.type == "STABLE" && next.type == "STABLE" &&
////                    std::abs(current.startBPM - next.startBPM) < 1.5) {
////                current.endTime = next.endTime;
////                current.duration += next.duration;
////                current.endBPM = next.endBPM;
////                current.startBPM = (current.startBPM + next.startBPM) / 2.0;
////                current.endBPM = current.startBPM;
////            } else {
////                merged.append(current);
////                current = next;
////            }
////        }
////        merged.append(current);
////        m_bpmSegments = merged;
////    }
////
////    // Snap to beats
////    snapSegmentsToBeats();
////
////    qDebug() << "[QueenMaryBeatsExtended] Created" << m_bpmSegments.size()
//<< "segments"; /    for (const auto& seg : std::as_const(m_bpmSegments)) { /
// qDebug() << "  " << seg.type << ":" << seg.startTime << "-" << seg.endTime /
//<< "BPM:" << seg.startBPM << "->" << seg.endBPM /                 << "rate:"
//<< std::abs(seg.changeAmount) / seg.duration; /    }
////}
//
// void AnalyzerQueenMaryBeatsExtended::analyzeBpmChanges() {
//        if (m_resultBeats.size() < kWindowSizeBeats) {
//            qDebug() << "[QueenMaryBeatsExtended] Not enough beats for BPM
//            analysis"; return;
//        }
//
//        // Convert beat positions from frames to seconds
//        m_beatTimes.clear();
//        for (const auto& beat : std::as_const(m_resultBeats)) {
//            m_beatTimes.push_back(beat.value() / m_sampleRate);
//        }
//
//        m_trackDuration = m_beatTimes.back();
//
//        // Calculate BPM at sliding windows
//        std::vector<double> bpmTimes;
//        std::vector<double> bpmValues;
//
//        for (size_t i = 0; i + kWindowSizeBeats <= m_beatTimes.size(); i +=
//        kStepSizeBeats) {
//            double totalInterval = m_beatTimes[i + kWindowSizeBeats] -
//            m_beatTimes[i]; double avgInterval = totalInterval /
//            kWindowSizeBeats; double localBpm = 60.0 / avgInterval;
//
//            if (localBpm > 50 && localBpm < 250) {
//                size_t windowCenter = i + kWindowSizeBeats / 2;
//                bpmTimes.push_back(m_beatTimes[windowCenter]);
//                bpmValues.push_back(localBpm);
//            }
//        }
//
//        if (bpmValues.empty()) {
//            qDebug() << "[QueenMaryBeatsExtended] No BPM values calculated";
//            return;
//        }
//
//        // Light smoothing
//        std::vector<double> bpmSmooth = bpmValues;
//        smoothBpmData(bpmSmooth, kSmoothWindowLength);
//
//        // Detect change points based on rate of change
//        std::vector<double> changeTimes;
//        double minRateChange = 0.5;
//
//        for (size_t i = 2; i < bpmSmooth.size() - 2; ++i) {
//            double timeSpan = bpmTimes[i + 2] - bpmTimes[i - 2];
//            if (timeSpan <= 0)
//                continue;
//
//            double bpmChange = bpmSmooth[i + 2] - bpmSmooth[i - 2];
//            double ratePerSec = std::abs(bpmChange) / timeSpan;
//
//            if (ratePerSec >= minRateChange) {
//                changeTimes.push_back(bpmTimes[i]);
//                i += 3;
//            }
//        }
//
//        // Add start and end
//        changeTimes.insert(changeTimes.begin(), 0.0);
//        changeTimes.push_back(m_trackDuration);
//
//        // Remove duplicates
//        std::sort(changeTimes.begin(), changeTimes.end());
//        changeTimes.erase(
//                std::unique(changeTimes.begin(), changeTimes.end(), [](double
//                a, double b) { return std::abs(a - b) < 1.0; }),
//                changeTimes.end());
//
//        // Helper function to get BPM at a specific time
//        auto getBpmAtTime = [&](double time) -> double {
//            if (bpmTimes.empty())
//                return 120.0;
//            if (time <= bpmTimes[0])
//                return bpmSmooth[0];
//            if (time >= bpmTimes.back())
//                return bpmSmooth.back();
//
//            for (size_t i = 1; i < bpmTimes.size(); ++i) {
//                if (time <= bpmTimes[i]) {
//                    double t1 = bpmTimes[i - 1];
//                    double t2 = bpmTimes[i];
//                    double b1 = bpmSmooth[i - 1];
//                    double b2 = bpmSmooth[i];
//
//                    if (t2 > t1) {
//                        double ratio = (time - t1) / (t2 - t1);
//                        return b1 + ratio * (b2 - b1);
//                    }
//                    return b1;
//                }
//            }
//            return bpmSmooth.back();
//        };
//
//        // Helper function to check if a segment is stable
//        auto isSegmentStable = [&](double startTime, double endTime) -> bool {
//            double windowStart = startTime;
//
//            while (windowStart < endTime) {
//                double windowEnd = std::min(windowStart +
//                kStableWindowSeconds, endTime);
//
//                // Find min and max BPM within this window
//                double minBpm = 1e9;
//                double maxBpm = -1e9;
//                bool hasData = false;
//
//                for (size_t j = 0; j < bpmTimes.size(); ++j) {
//                    if (bpmTimes[j] >= windowStart && bpmTimes[j] <=
//                    windowEnd) {
//                        hasData = true;
//                        if (bpmSmooth[j] < minBpm)
//                            minBpm = bpmSmooth[j];
//                        if (bpmSmooth[j] > maxBpm)
//                            maxBpm = bpmSmooth[j];
//                    }
//                }
//
//                if (hasData && (maxBpm - minBpm) > kStableMaxRange) {
//                    return false;
//                }
//
//                windowStart = windowEnd;
//            }
//
//            return true;
//        };
//
//        // Create segments between change points
//        // creating segments between change points
//        m_bpmSegments.clear();
//
//        for (size_t idx = 0; idx < changeTimes.size() - 1; ++idx) {
//            double startTime = changeTimes[idx];
//            double endTime = changeTimes[idx + 1];
//            double duration = endTime - startTime;
//
//            if (duration < 1.0)
//                continue;
//
//            double startBpm = getBpmAtTime(startTime);
//            double endBpm = getBpmAtTime(endTime);
//
//            // calc BPM change per minute
//            double totalChange = endBpm - startBpm;
//            double bpmChangePerMinute = std::abs(totalChange) / (duration
//            / 60.0);
//
//            // segment type based on BPM change per minute
//            QString type;
//
//            // STABLE if less than 1 BPM change per minute
//            if (bpmChangePerMinute < 1.0) {
//                type = "STABLE";
//                double avgBpm = (startBpm + endBpm) / 2.0;
//                startBpm = avgBpm;
//                endBpm = avgBpm;
//            } else if (totalChange > 0) {
//                type = "INCREASE";
//            } else {
//                type = "DECREASE";
//            }
//
//            BPMSegment seg;
//            seg.startTime = startTime;
//            seg.endTime = endTime;
//            seg.duration = duration;
//            seg.type = type;
//            seg.startBPM = startBpm;
//            seg.endBPM = endBpm;
//            seg.changeAmount = totalChange;
//
//            m_bpmSegments.append(seg);
//        }
//
//        // Merge adjacent stable segments with similar BPM
//        if (m_bpmSegments.size() > 1) {
//            QVector<BPMSegment> merged;
//            BPMSegment current = m_bpmSegments[0];
//
//            for (int i = 1; i < m_bpmSegments.size(); ++i) {
//                const BPMSegment& next = m_bpmSegments[i];
//
//                if (current.type == "STABLE" && next.type == "STABLE" &&
//                        std::abs(current.startBPM - next.startBPM) < 1.5) {
//                    current.endTime = next.endTime;
//                    current.duration += next.duration;
//                    current.endBPM = next.endBPM;
//                    current.startBPM = (current.startBPM + next.startBPM)
//                    / 2.0; current.endBPM = current.startBPM;
//                } else {
//                    merged.append(current);
//                    current = next;
//                }
//            }
//            merged.append(current);
//            m_bpmSegments = merged;
//        }
//
//        // Snap to beats
//        snapSegmentsToBeats();
//
//        qDebug() << "[QueenMaryBeatsExtended] Created" << m_bpmSegments.size()
//        << "segments"; for (const auto& seg : std::as_const(m_bpmSegments)) {
//            qDebug() << "  " << seg.type << ":" << seg.startTime << "-" <<
//            seg.endTime
//                     << "BPM:" << seg.startBPM << "->" << seg.endBPM
//                     << "rate:" << std::abs(seg.changeAmount) / seg.duration;
//        }
//    }
//
////void AnalyzerQueenMaryBeatsExtended::analyzeBpmChanges() {
////    if (m_resultBeats.size() < 12) {
////        qDebug() << "[QueenMaryBeatsExtended] Not enough beats for BPM
// analysis"; /        return; /    }
////
////    // Convert beat positions from frames to seconds
////    m_beatTimes.clear();
////    for (const auto& beat : std::as_const(m_resultBeats)) {
////        m_beatTimes.push_back(beat.value() / m_sampleRate);
////    }
////
////    m_trackDuration = m_beatTimes.back();
////
////    // Calculate average BPM at each beat position using sliding window
////    std::vector<double> bpmTimes;
////    std::vector<double> bpmValues;
////
////    qDebug() << "[QueenMaryBeatsExtended] Calculating average BPM with" <<
// kBpmWindowSeconds << "second window";
////
////    for (size_t i = 0; i < m_beatTimes.size(); ++i) {
////        double currentTime = m_beatTimes[i];
////
////        // Window centered at current time
////        double windowStart = currentTime - kBpmWindowSeconds / 2.0;
////        double windowEnd = currentTime + kBpmWindowSeconds / 2.0;
////
////        // Collect intervals within this window
////        std::vector<double> intervals;
////        for (size_t j = 0; j < m_beatTimes.size() - 1; ++j) {
////            if (m_beatTimes[j] >= windowStart && m_beatTimes[j + 1] <=
// windowEnd) { /                intervals.push_back(m_beatTimes[j + 1] -
// m_beatTimes[j]); /            } /        }
////
////        if (intervals.size() >= 3) {
////            double sum = std::accumulate(intervals.begin(), intervals.end(),
// 0.0); /            double avgInterval = sum / intervals.size(); / double
// avgBpm = 60.0 / avgInterval;
////
////            bpmTimes.push_back(currentTime);
////            bpmValues.push_back(avgBpm);
////        }
////    }
////
////    if (bpmValues.empty()) {
////        qDebug() << "[QueenMaryBeatsExtended] No BPM values calculated";
////        return;
////    }
////
////    qDebug() << "[QueenMaryBeatsExtended] Calculated" << bpmValues.size() <<
//"average BPM points";
////
////    // Smooth the average BPM values
////    std::vector<double> bpmSmooth = bpmValues;
////    smoothBpmData(bpmSmooth, 5);
////
////    // Create segments when BPM changes significantly
////    m_bpmSegments.clear();
////
////    double segmentStart = 0.0;
////    double segmentBpm = bpmSmooth[0];
////
////    for (size_t i = 1; i < bpmSmooth.size(); ++i) {
////        if (std::abs(bpmSmooth[i] - segmentBpm) > kBpmChangeThreshold) {
////            // End current segment
////            BPMSegment seg;
////            seg.startTime = segmentStart;
////            seg.endTime = bpmTimes[i - 1];
////            seg.duration = seg.endTime - seg.startTime;
////            seg.type = "STABLE";
////            seg.startBPM = segmentBpm;
////            seg.endBPM = segmentBpm;
////            m_bpmSegments.append(seg);
////
////            qDebug() << "  Segment:" << seg.startTime << "-" << seg.endTime
//<< "BPM:" << seg.startBPM;
////
////            // Start new segment
////            segmentStart = bpmTimes[i - 1];
////            segmentBpm = bpmSmooth[i];
////        }
////    }
////
////    // Add final segment
////    BPMSegment finalSeg;
////    finalSeg.startTime = segmentStart;
////    finalSeg.endTime = m_trackDuration;
////    finalSeg.duration = finalSeg.endTime - finalSeg.startTime;
////    finalSeg.type = "STABLE";
////    finalSeg.startBPM = segmentBpm;
////    finalSeg.endBPM = segmentBpm;
////    m_bpmSegments.append(finalSeg);
////
////    qDebug() << "[QueenMaryBeatsExtended] Created" << m_bpmSegments.size()
//<< "segments";
////
////    // Snap to beats
////    snapSegmentsToBeats();
////
////    for (const auto& seg : std::as_const(m_bpmSegments)) {
////        qDebug() << "  " << seg.type << ":" << seg.startTime << "-" <<
// seg.endTime /                 << "BPM:" << seg.startBPM << "->" <<
// seg.endBPM; /    }
////}
//
// bool AnalyzerQueenMaryBeatsExtended::finalize() {
//    m_helper.finalize();
//
//    // original TempoTrackV2 -> beats
//    size_t nonZeroCount = m_detectionResults.size();
//    while (nonZeroCount > 0 && m_detectionResults.at(nonZeroCount - 1) <= 0.0)
//    {
//        --nonZeroCount;
//    }
//
//    size_t required_size = std::max(static_cast<size_t>(2), nonZeroCount) - 2;
//
//    std::vector<double> df;
//    df.reserve(required_size);
//    auto beatPeriod = std::vector<int>(required_size / 128 + 1);
//
//    for (size_t i = 2; i < nonZeroCount; ++i) {
//        df.push_back(m_detectionResults.at(i));
//    }
//
//    TempoTrackV2 tt(m_sampleRate, m_stepSizeFrames);
//    tt.calculateBeatPeriod(df, beatPeriod);
//
//    std::vector<double> beats;
//    tt.calculateBeats(df, beatPeriod, beats);
//
//    m_resultBeats.reserve(static_cast<int>(beats.size()));
//    for (size_t i = 0; i < beats.size(); ++i) {
//        const auto result = mixxx::audio::FramePos(
//                (beats.at(i) * m_stepSizeFrames) + m_stepSizeFrames / 2);
//        m_resultBeats.push_back(result);
//    }
//
//    // analyze BPM changes -> create segments
//    analyzeBpmChanges();
//
//    m_pDetectionFunction.reset();
//    return true;
//}
//
// QJsonArray AnalyzerQueenMaryBeatsExtended::getBpmSegmentsJson() const {
//    QJsonArray segmentsArray;
//    int segmentId = 1;
//
//    for (const auto& segment : m_bpmSegments) {
//        if (segment.duration < 0.001) {
//            continue;
//        }
//
//        QJsonObject segmentObj;
//        segmentObj["id"] = segmentId++;
//        segmentObj["type"] = segment.type;
//        segmentObj["position"] = segment.startTime;
//        segmentObj["duration"] = segment.duration;
//        segmentObj["bpm_start"] = segment.startBPM;
//        segmentObj["bpm_end"] = segment.endBPM;
//        segmentObj["range_start"] = segment.startTime;
//        segmentObj["range_end"] = segment.endTime;
//
//        segmentsArray.append(segmentObj);
//    }
//
//    return segmentsArray;
//}
//
////void AnalyzerQueenMaryBeatsExtended::snapSegmentsToBeats() {
////    if (m_beatTimes.empty()) {
////        return;
////    }
////
////    for (auto& seg : m_bpmSegments) {
////        // Snap start time to nearest beat
////        double nearestStart = findNearestBeat(seg.startTime, m_beatTimes);
////        if (std::abs(nearestStart - seg.startTime) < 0.05) { // within 50ms
////            seg.startTime = nearestStart;
////        }
////
////        // Snap end time to nearest beat
////        double nearestEnd = findNearestBeat(seg.endTime, m_beatTimes);
////        if (std::abs(nearestEnd - seg.endTime) < 0.05) {
////            seg.endTime = nearestEnd;
////        }
////
////        // Recalculate duration
////        seg.duration = seg.endTime - seg.startTime;
////    }
////}
//
////void AnalyzerQueenMaryBeatsExtended::snapSegmentsToBeats() {
////    if (m_beatTimes.empty()) {
////        return;
////    }
////
////    for (auto& seg : m_bpmSegments) {
////        // For start time: snap to the beat that is at or before the current
// time /        // This ensures segments start exactly on a beat / double
// snappedStart = 0.0; /        for (double beat : m_beatTimes) { / if (beat <=
// seg.startTime + 0.001) { // beat at or before startTime / snappedStart =
// beat; /            } else { /                break; /            } /        }
////
////        // Only snap if within reasonable distance (100ms)
////        if (std::abs(snappedStart - seg.startTime) < 0.1) {
////            seg.startTime = snappedStart;
////        }
////
////        // For end time: snap to the nearest beat (can be before or after)
////        double nearestEnd = m_beatTimes[0];
////        double minDist = std::abs(seg.endTime - nearestEnd);
////        for (double beat : m_beatTimes) {
////            double dist = std::abs(seg.endTime - beat);
////            if (dist < minDist) {
////                minDist = dist;
////                nearestEnd = beat;
////            }
////        }
////
////        // Only snap if within reasonable distance (100ms)
////        if (minDist < 0.1) {
////            seg.endTime = nearestEnd;
////        }
////
////        // Recalculate duration after snapping
////        seg.duration = seg.endTime - seg.startTime;
////        if (seg.duration < 0) {
////            seg.duration = 0;
////        }
////    }
////}
//
////void AnalyzerQueenMaryBeatsExtended::snapSegmentsToBeats() {
////    if (m_beatTimes.empty()) {
////        return;
////    }
////
////    for (auto& seg : m_bpmSegments) {
////        // For start time: snap to nearest beat
////        double nearestStart = m_beatTimes[0];
////        double minDistStart = std::abs(seg.startTime - nearestStart);
////        for (double beat : m_beatTimes) {
////            double dist = std::abs(seg.startTime - beat);
////            if (dist < minDistStart) {
////                minDistStart = dist;
////                nearestStart = beat;
////            }
////        }
////
////        // Only snap if within reasonable distance (100ms)
////        if (minDistStart < 0.1) {
////            seg.startTime = nearestStart;
////        }
////
////        // For end time: snap to nearest beat
////        double nearestEnd = m_beatTimes[0];
////        double minDistEnd = std::abs(seg.endTime - nearestEnd);
////        for (double beat : m_beatTimes) {
////            double dist = std::abs(seg.endTime - beat);
////            if (dist < minDistEnd) {
////                minDistEnd = dist;
////                nearestEnd = beat;
////            }
////        }
////
////        if (minDistEnd < 0.1) {
////            seg.endTime = nearestEnd;
////        }
////
////        // Recalculate duration
////        seg.duration = seg.endTime - seg.startTime;
////        if (seg.duration < 0) {
////            seg.duration = 0;
////        }
////    }
////}
//
// void AnalyzerQueenMaryBeatsExtended::snapSegmentsToBeats() {
//    if (m_beatTimes.empty()) {
//        return;
//    }
//
//    // Increase snap threshold to 300ms
//    const double snapThreshold = 0.3;
//
//    for (auto& seg : m_bpmSegments) {
//        // Snap start time to nearest beat
//        double nearestStart = m_beatTimes[0];
//        double minDistStart = std::abs(seg.startTime - nearestStart);
//        for (double beat : m_beatTimes) {
//            double dist = std::abs(seg.startTime - beat);
//            if (dist < minDistStart) {
//                minDistStart = dist;
//                nearestStart = beat;
//            }
//        }
//
//        if (minDistStart < snapThreshold) {
//            seg.startTime = nearestStart;
//        }
//
//        // Snap end time to nearest beat
//        double nearestEnd = m_beatTimes[0];
//        double minDistEnd = std::abs(seg.endTime - nearestEnd);
//        for (double beat : m_beatTimes) {
//            double dist = std::abs(seg.endTime - beat);
//            if (dist < minDistEnd) {
//                minDistEnd = dist;
//                nearestEnd = beat;
//            }
//        }
//
//        if (minDistEnd < snapThreshold) {
//            seg.endTime = nearestEnd;
//        }
//
//        // Recalculate duration
//        seg.duration = seg.endTime - seg.startTime;
//        if (seg.duration < 0) {
//            seg.duration = 0;
//        }
//    }
//}
//
//
// double AnalyzerQueenMaryBeatsExtended::findNearestBeat(double time, const
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
//} // namespace mixxx
>>>>>>> f56f1a9e19 (KeyCurve: extended analyzer 2 segments & wheel in waveform)
