#include "analyzer/plugins/analyzerqueenmarybeatsextended.h"

#include <dsp/onsets/DetectionFunction.h>
#include <dsp/tempotracking/TempoTrackV2.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <utility>

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

// static constexpr int kWindowSizeBeats = 12;
// static constexpr int kStepSizeBeats = 3;
// static constexpr int kSmoothWindowLength = 7;

AnalyzerQueenMaryBeatsExtended::AnalyzerQueenMaryBeatsExtended()
        : m_windowSize(0),
          m_stepSizeFrames(0),
          m_trackDuration(0.0) {
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
    if (intervals.size() < 2)
        return 0.6;

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
    if (static_cast<int>(bpmValues.size()) < windowLength)
        return;

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

void AnalyzerQueenMaryBeatsExtended::analyzeBpmChanges() {
    if (m_resultBeats.size() < 12) {
        qDebug() << "[QueenMaryBeatsExtended] Not enough beats for BPM analysis";
        return;
    }

    // Convert beat positions from frames to seconds
    m_beatTimes.clear();
    for (const auto& beat : std::as_const(m_resultBeats)) {
        m_beatTimes.push_back(beat.value() / m_sampleRate);
    }

    m_trackDuration = m_beatTimes.back();

    // Calculate BPM at sliding windows
    std::vector<double> bpmTimes;
    std::vector<double> bpmValues;

    for (size_t i = 0; i + 12 <= m_beatTimes.size(); i += 3) {
        double totalInterval = m_beatTimes[i + 12] - m_beatTimes[i];
        double avgInterval = totalInterval / 12;
        double localBpm = 60.0 / avgInterval;

        size_t windowCenter = i + 6;
        bpmTimes.push_back(m_beatTimes[windowCenter]);
        bpmValues.push_back(localBpm);
    }

    if (bpmValues.empty()) {
        qDebug() << "[QueenMaryBeatsExtended] No BPM values calculated";
        return;
    }

    // Light smoothing
    std::vector<double> bpmSmooth = bpmValues;
    int smoothWindow = 5;
    // if (bpmSmooth.size() >= smoothWindow) {
    if (static_cast<int>(bpmSmooth.size()) >= smoothWindow) {
        std::vector<double> smoothed(bpmSmooth.size());
        for (size_t i = 0; i < bpmSmooth.size(); ++i) {
            int start = std::max(0, static_cast<int>(i) - 2);
            int end = std::min(static_cast<int>(bpmSmooth.size()) - 1, static_cast<int>(i) + 2);
            double sum = 0;
            int count = 0;
            for (int j = start; j <= end; ++j) {
                sum += bpmSmooth[j];
                count++;
            }
            smoothed[i] = sum / count;
        }
        bpmSmooth = smoothed;
    }

    // Detect change points based on rate of change (BPM per second)
    std::vector<double> changeTimes;
    double minRateChange = 0.5; // 0.3 BPM / sec = change
    // double minDuration = 5.0;

    for (size_t i = 2; i < bpmSmooth.size() - 2; ++i) {
        // Calculate rate of change over a window
        double timeSpan = bpmTimes[i + 2] - bpmTimes[i - 2];
        if (timeSpan <= 0)
            continue;

        double bpmChange = bpmSmooth[i + 2] - bpmSmooth[i - 2];
        double ratePerSec = std::abs(bpmChange) / timeSpan;

        if (ratePerSec >= minRateChange) {
            // significant change point !!
            changeTimes.push_back(bpmTimes[i]);
            i += 3;
        }
    }

    // add start and end
    changeTimes.insert(changeTimes.begin(), 0.0);
    changeTimes.push_back(m_trackDuration);

    // duplicates remove
    std::sort(changeTimes.begin(), changeTimes.end());
    changeTimes.erase(
            std::unique(changeTimes.begin(),
                    changeTimes.end(),
                    [](double a, double b) { return std::abs(a - b) < 1.0; }),
            changeTimes.end());

    // get BPM at a specific time
    auto getBpmAtTime = [&](double time) -> double {
        if (bpmTimes.empty())
            return 120.0;
        if (time <= bpmTimes[0])
            return bpmSmooth[0];
        if (time >= bpmTimes.back())
            return bpmSmooth.back();

        for (size_t i = 1; i < bpmTimes.size(); ++i) {
            if (time <= bpmTimes[i]) {
                double t1 = bpmTimes[i - 1];
                double t2 = bpmTimes[i];
                double b1 = bpmSmooth[i - 1];
                double b2 = bpmSmooth[i];

                if (t2 > t1) {
                    double ratio = (time - t1) / (t2 - t1);
                    return b1 + ratio * (b2 - b1);
                }
                return b1;
            }
        }
        return bpmSmooth.back();
    };

    // creating segments between change points
    m_bpmSegments.clear();

    for (size_t idx = 0; idx < changeTimes.size() - 1; ++idx) {
        double startTime = changeTimes[idx];
        double endTime = changeTimes[idx + 1];
        double duration = endTime - startTime;

        if (duration < 1.0)
            continue;

        double startBpm = getBpmAtTime(startTime);
        double endBpm = getBpmAtTime(endTime);

        // calc rate of change per second for this segment
        double totalChange = endBpm - startBpm;
        double ratePerSec = std::abs(totalChange) / duration;

        // segment type ?
        QString type;

        // rate is very low: < 0.05 BPM/sec -> STABLE
        if (ratePerSec < 0.05) {
            type = "STABLE";
            double avgBpm = (startBpm + endBpm) / 2.0;
            startBpm = avgBpm;
            endBpm = avgBpm;
        } else if (totalChange > 0) {
            type = "INCREASE";
        } else {
            type = "DECREASE";
        }

        BPMSegment seg;
        seg.startTime = startTime;
        seg.endTime = endTime;
        seg.duration = duration;
        seg.type = type;
        seg.startBPM = startBpm;
        seg.endBPM = endBpm;
        seg.changeAmount = totalChange;

        m_bpmSegments.append(seg);
    }

    // adjacent stable segments? -> merge
    if (m_bpmSegments.size() > 1) {
        QVector<BPMSegment> merged;
        BPMSegment current = m_bpmSegments[0];

        for (int i = 1; i < m_bpmSegments.size(); ++i) {
            const BPMSegment& next = m_bpmSegments[i];

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

    qDebug() << "[QueenMaryBeatsExtended] Created" << m_bpmSegments.size() << "segments";
    for (const auto& seg : std::as_const(m_bpmSegments)) {
        qDebug() << "  " << seg.type << ":" << seg.startTime << "-" << seg.endTime
                 << "BPM:" << seg.startBPM << "->" << seg.endBPM
                 << "rate:" << std::abs(seg.changeAmount) / seg.duration;
    }
}

bool AnalyzerQueenMaryBeatsExtended::finalize() {
    m_helper.finalize();

    // original TempoTrackV2 -> beats
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

    // analyze BPM changes -> create segments
    analyzeBpmChanges();

    m_pDetectionFunction.reset();
    return true;
}

QJsonArray AnalyzerQueenMaryBeatsExtended::getBpmSegmentsJson() const {
    QJsonArray segmentsArray;
    int segmentId = 1;

    for (const auto& segment : m_bpmSegments) {
        if (segment.duration < 0.001) {
            continue;
        }

        QJsonObject segmentObj;
        segmentObj["id"] = segmentId++;
        segmentObj["type"] = segment.type;
        segmentObj["position"] = segment.startTime;
        segmentObj["duration"] = segment.duration;
        segmentObj["bpm_start"] = segment.startBPM;
        segmentObj["bpm_end"] = segment.endBPM;
        segmentObj["range_start"] = segment.startTime;
        segmentObj["range_end"] = segment.endTime;

        segmentsArray.append(segmentObj);
    }

    return segmentsArray;
}

} // namespace mixxx
