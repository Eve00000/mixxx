#include "sources/soundsourcestem.h"

extern "C" {

#include <libavutil/avutil.h>
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#endif

} // extern "C"

#include <memory>

#include "sources/soundsourceffmpeg.h"
#include "util/assert.h"
#include "util/fpclassify.h"
#include "util/logger.h"
#include "util/sample.h"

#if !defined(VERBOSE_DEBUG_LOG)
#define VERBOSE_DEBUG_LOG false
#endif

namespace mixxx {

namespace {

// STEM constants. We keep the premix stream in m_pStereoStreams[0]
// so we can toggle between premix and stems at runtime.
constexpr int kNumStreams = 5; // premix + 4 stems
constexpr int kRequiredStreamCount = kNumStreams;

const Logger kLogger("SoundSourceSTEM");

struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};
using AVFormatContextPtr =
        std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

} // anonymous namespace

const QString SoundSourceProviderSTEM::kDisplayName = QStringLiteral("STEM with FFmpeg");

QStringList SoundSourceProviderSTEM::getSupportedFileTypes() const {
    return {"stem.mp4", "stem.m4a"};
}

SoundSourceProviderPriority SoundSourceProviderSTEM::getPriorityHint(
        const QString& supportedFileType) const {
    Q_UNUSED(supportedFileType)
    return SoundSourceProviderPriority::Higher;
}

QString SoundSourceProviderSTEM::getVersionString() const {
    return QString::fromUtf8(av_version_info());
}

SoundSourceSTEM::SoundSourceSTEM(const QUrl& url)
        : SoundSource(url) {
}

SoundSourceSTEM::~SoundSourceSTEM() = default;

SoundSource::OpenResult SoundSourceSTEM::tryOpen(
        OpenMode /*mode*/,
        const OpenParams& params) {
    VERIFY_OR_DEBUG_ASSERT(!m_requestedChannelCount.isValid()) {
        return OpenResult::Failed;
    }

    m_upSampleStems = params.getUpSampleStems();
    m_premixIncluded = params.getPremixIncluded();
    m_streamInfoInitialized = false;

    AVFormatContextPtr pavInputFormatContextGuard(
            SoundSourceFFmpeg::openInputFile(getLocalFileName()));
    AVFormatContext* pavInputFormatContext = pavInputFormatContextGuard.get();
    if (pavInputFormatContext == nullptr) {
        kLogger.warning() << "Failed to open input file" << getLocalFileName();
        return OpenResult::Failed;
    }

#if VERBOSE_DEBUG_LOG
    kLogger.debug()
            << "AVFormatContext"
            << "{ nb_streams" << pavInputFormatContext->nb_streams
            << "| start_time" << pavInputFormatContext->start_time
            << "| duration" << pavInputFormatContext->duration
            << "| bit_rate" << pavInputFormatContext->bit_rate
            << '}';
#endif

    const int avformat_find_stream_info_result =
            avformat_find_stream_info(pavInputFormatContext, nullptr);
    if (avformat_find_stream_info_result != 0) {
        DEBUG_ASSERT(avformat_find_stream_info_result < 0);
        kLogger.warning().noquote()
                << "avformat_find_stream_info() failed:"
                << SoundSourceFFmpeg::formatErrorString(avformat_find_stream_info_result);
        return OpenResult::Failed;
    }

    int stemCount = 0;
    const uint selectedStemMask = params.stemMask();
    VERIFY_OR_DEBUG_ASSERT(selectedStemMask <= 1 << mixxx::kMaxSupportedStems) {
        kLogger.warning().noquote()
                << "Invalid selected stem mask" << selectedStemMask;
        return OpenResult::Failed;
    }

    OpenParams stemParam = params;
    stemParam.setChannelCount(mixxx::audio::ChannelCount::stereo());

    // First pass: count & validate audio streams.
    for (unsigned int streamIdx = 0; streamIdx < pavInputFormatContext->nb_streams; ++streamIdx) {
        if (pavInputFormatContext->streams[streamIdx]->codecpar->codec_type !=
                AVMEDIA_TYPE_AUDIO) {
            continue;
        }
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
        if (pavInputFormatContext->streams[streamIdx]->codecpar->ch_layout.nb_channels !=
                mixxx::audio::ChannelCount::stereo()) {
#else
        if (pavInputFormatContext->streams[streamIdx]->codecpar->channels !=
                mixxx::audio::ChannelCount::stereo()) {
#endif
            kLogger.warning().noquote()
                    << "stream at position" << streamIdx << "is not in stereo";
            return OpenResult::Failed;
        }
        ++stemCount;
    }

    if (stemCount != kRequiredStreamCount) {
        kLogger.warning().noquote()
                << "expected to find" << kRequiredStreamCount
                << "streams but found" << stemCount;
        return OpenResult::Failed;
    }

    // Second pass: open all streams.
    // 2.7: SoundSourceFFmpeg takes streamIdx in its constructor.
    for (unsigned int streamIdx = 0; streamIdx < pavInputFormatContext->nb_streams; ++streamIdx) {
        if (pavInputFormatContext->streams[streamIdx]->codecpar->codec_type !=
                AVMEDIA_TYPE_AUDIO) {
            continue;
        }
        m_pStereoStreams.emplace_back(
                std::make_unique<SoundSourceFFmpeg>(getUrl(), streamIdx));
        if (m_pStereoStreams.back()->open(OpenMode::Strict, stemParam) !=
                OpenResult::Succeeded) {
            kLogger.warning() << "Failed to open stream" << streamIdx;
            return OpenResult::Failed;
        }
    }

    VERIFY_OR_DEBUG_ASSERT(!m_pStereoStreams.empty()) {
        close();
        return OpenResult::Failed;
    }

    // Determine requested channel count.
    if (params.getSignalInfo().getChannelCount() ==
                    mixxx::audio::ChannelCount::stereo() ||
            selectedStemMask) {
        m_requestedChannelCount = mixxx::audio::ChannelCount::stereo();
    } else {
        m_requestedChannelCount = mixxx::audio::ChannelCount(
                mixxx::audio::ChannelCount::stereo() *
                static_cast<int>(m_pStereoStreams.size()));
    }

    // Reference stream info.
    const auto& premixFrameRange = m_pStereoStreams[0]->frameIndexRange();
    const auto& premixInfo = m_pStereoStreams[0]->getSignalInfo();
    const auto& firstStemFrameRange = m_pStereoStreams[1]->frameIndexRange();
    const auto& firstStemInfo = m_pStereoStreams[1]->getSignalInfo();

    // === Decide target sample rate ONCE. Never call initSampleRateOnce twice. ===
    if (m_upSampleStems) {
        m_targetSampleRate = premixInfo.getSampleRate();
        m_referenceStreamIdx = 0;
    } else {
        m_targetSampleRate = firstStemInfo.getSampleRate();
        m_referenceStreamIdx = 1;
    }
    if (!m_targetSampleRate.isValid() || m_targetSampleRate <= 0) {
        m_targetSampleRate = audio::SampleRate(44100);
        kLogger.warning() << "Invalid target sample rate, using default 44100";
    }

    // Frame range / duration based on the reference stream at target rate.
    SINT refFrameLength = 0;
    int refSampleRate = 44100;
    if (m_referenceStreamIdx == 0) {
        refFrameLength = premixFrameRange.length();
        refSampleRate = premixInfo.getSampleRate();
    } else {
        refFrameLength = firstStemFrameRange.length();
        refSampleRate = firstStemInfo.getSampleRate();
    }
    const double durationSeconds = static_cast<double>(refFrameLength) /
            std::max(1, refSampleRate);
    const SINT globalFrameLength = static_cast<SINT>(
            durationSeconds * m_targetSampleRate + 0.5);
    m_globalFrameRange = IndexRange::forward(0, globalFrameLength);

    // === Initialize base-class state exactly once each. ===
    if (!initChannelCountOnce(m_requestedChannelCount)) {
        kLogger.warning() << "initChannelCountOnce failed";
        return OpenResult::Failed;
    }
    if (!initSampleRateOnce(m_targetSampleRate)) {
        kLogger.warning() << "initSampleRateOnce failed";
        return OpenResult::Failed;
    }
    if (!initFrameIndexRangeOnce(m_globalFrameRange)) {
        kLogger.warning() << "initFrameIndexRangeOnce failed";
        return OpenResult::Failed;
    }

    // Average bitrate across streams.
    int totalBitrateKbps = 0;
    int bitrateCount = 0;
    int maxBitrateKbps = 0;
    for (const auto& stream : m_pStereoStreams) {
        const auto bitrate = stream->getBitrate();
        if (bitrate.isValid()) {
            const int kbps = static_cast<int>(bitrate.value());
            totalBitrateKbps += kbps;
            maxBitrateKbps = std::max(maxBitrateKbps, kbps);
            ++bitrateCount;
        }
    }
    if (maxBitrateKbps > 0) {
        initBitrateOnce(audio::Bitrate(maxBitrateKbps));
    } else if (bitrateCount > 0) {
        initBitrateOnce(audio::Bitrate(totalBitrateKbps / bitrateCount));
    }

    // Resampling flags.
    m_needsResampling.assign(m_pStereoStreams.size(), false);
    for (std::size_t i = 0; i < m_pStereoStreams.size(); ++i) {
        const auto streamRate = m_pStereoStreams[i]->getSignalInfo().getSampleRate();
        m_needsResampling[i] = (streamRate != m_targetSampleRate);
    }

    // Pre-allocate buffers.
    const SINT maxBufferFrames = 65536;
    const SINT maxBufferSamples = maxBufferFrames * m_requestedChannelCount;
    m_buffer = SampleBuffer(maxBufferSamples);
    m_resampleInputBuffer = SampleBuffer(maxBufferSamples * 2);

    m_streamInfoInitialized = true;

    kLogger.debug() << "STEM opened:"
                    << "upSample =" << m_upSampleStems
                    << "targetRate =" << m_targetSampleRate
                    << "channels =" << m_requestedChannelCount
                    << "frames =" << m_globalFrameRange.length()
                    << "streams =" << m_pStereoStreams.size();

    return OpenResult::Succeeded;
}

void SoundSourceSTEM::close() {
    for (auto& stream : m_pStereoStreams) {
        stream->close();
    }
}

audio::StreamInfo SoundSourceSTEM::getStreamInfo() const {
    if (m_targetSampleRate.isValid() && m_targetSampleRate > 0 &&
            m_requestedChannelCount.isValid()) {
        audio::StreamInfo info;
        info.setSignalInfo(audio::SignalInfo(
                m_requestedChannelCount,
                m_targetSampleRate));
        if (m_referenceStreamIdx >= 0 &&
                m_referenceStreamIdx < static_cast<SINT>(m_pStereoStreams.size())) {
            const auto& refInfo =
                    m_pStereoStreams[m_referenceStreamIdx]->getStreamInfo();
            info.setDuration(refInfo.getDuration());
        } else {
            info.setDuration(Duration::fromSeconds(0));
        }
        return info;
    }
    return SoundSource::getStreamInfo();
}

IndexRange SoundSourceSTEM::frameIndexRange() const {
    if (m_globalFrameRange.length() > 0) {
        return m_globalFrameRange;
    }
    return SoundSource::frameIndexRange();
}

ReadableSampleFrames SoundSourceSTEM::readSampleFramesClamped(
        const WritableSampleFrames& globalSampleFrames) {
    VERIFY_OR_DEBUG_ASSERT(m_requestedChannelCount.isValid()) {
        return ReadableSampleFrames();
    }
    VERIFY_OR_DEBUG_ASSERT(globalSampleFrames.writableLength() %
                    m_requestedChannelCount ==
            0) {
        return ReadableSampleFrames();
    };

    if (m_pStereoStreams.empty()) {
        kLogger.warning() << "No streams available for reading";
        return ReadableSampleFrames();
    }

    const audio::SampleRate targetSampleRate = m_targetSampleRate;
    if (!targetSampleRate.isValid() || targetSampleRate <= 0) {
        kLogger.warning() << "Invalid target sample rate:" << targetSampleRate;
        return ReadableSampleFrames();
    }

    const SINT outputSampleLength = getSignalInfo().frames2samples(
            globalSampleFrames.frameLength());
    if (outputSampleLength <= 0) {
        kLogger.warning() << "Invalid output sample length:" << outputSampleLength;
        return ReadableSampleFrames();
    }

    if (m_needsResampling.empty()) {
        initializeResamplers(targetSampleRate);
    }

    const SINT maxAllowedBufferSize = kMaxBufferSize * m_requestedChannelCount;
    if (outputSampleLength > maxAllowedBufferSize) {
        kLogger.warning() << "Output sample length" << outputSampleLength
                          << "exceeds max buffer size" << maxAllowedBufferSize;
        return ReadableSampleFrames();
    }
    if (outputSampleLength > m_buffer.size()) {
        m_buffer = SampleBuffer(outputSampleLength);
    }

    SINT maxInputSamplesNeeded = outputSampleLength * 5;
    if (maxInputSamplesNeeded > maxAllowedBufferSize * 2) {
        maxInputSamplesNeeded = maxAllowedBufferSize * 2;
    }
    if (maxInputSamplesNeeded > m_resampleInputBuffer.size()) {
        m_resampleInputBuffer = SampleBuffer(maxInputSamplesNeeded);
    }

    ReadableSampleFrames read(
            globalSampleFrames.frameIndexRange(),
            SampleBuffer::ReadableSlice(
                    globalSampleFrames.writableData(),
                    globalSampleFrames.writableLength()));

    const std::size_t stemCount = m_pStereoStreams.size();
    CSAMPLE* pBuffer = globalSampleFrames.writableData();
    if (!pBuffer) {
        kLogger.warning() << "Null buffer pointer";
        return ReadableSampleFrames();
    }

    if (m_requestedChannelCount == mixxx::audio::ChannelCount::stereo() &&
            stemCount != 1) {
        SampleUtil::clear(pBuffer, globalSampleFrames.writableLength());
    }

    if (stemCount == 1) {
        if (!m_pStereoStreams[0]) {
            return ReadableSampleFrames();
        }
        m_pStereoStreams[0]->readSampleFrames(globalSampleFrames);
        return read;
    }

    for (std::size_t streamIdx = 0; streamIdx < stemCount; ++streamIdx) {
        if (streamIdx >= m_pStereoStreams.size() || !m_pStereoStreams[streamIdx]) {
            kLogger.warning() << "Invalid stream at index" << streamIdx;
            continue;
        }
        if (streamIdx >= m_needsResampling.size()) {
            kLogger.warning() << "Missing resampling flag for stream" << streamIdx;
            continue;
        }
        if (m_needsResampling[streamIdx]) {
            processWithResampler(streamIdx, globalSampleFrames, pBuffer);
        } else {
            processWithoutResampler(streamIdx, globalSampleFrames, pBuffer);
        }
    }

    return read;
}

void SoundSourceSTEM::processWithoutResampler(size_t streamIdx,
        const WritableSampleFrames& globalSampleFrames,
        CSAMPLE* pBuffer) {
    const SINT outputSampleLength =
            m_pStereoStreams.front()->getSignalInfo().frames2samples(
                    globalSampleFrames.frameLength());
    if (outputSampleLength <= 0 || outputSampleLength > kMaxBufferSize * 2) {
        return;
    }
    if (outputSampleLength > m_buffer.size()) {
        m_buffer = SampleBuffer(outputSampleLength);
    }

    WritableSampleFrames currentStemFrame(
            globalSampleFrames.frameIndexRange(),
            SampleBuffer::WritableSlice(m_buffer.data(), outputSampleLength));

    const auto readResult =
            m_pStereoStreams[streamIdx]->readSampleFrames(currentStemFrame);
    const SINT framesRead = readResult.frameIndexRange().length();
    if (framesRead == 0) {
        return;
    }

    const std::size_t stemCount = m_pStereoStreams.size();
    const SINT samplesRead = framesRead * 2;
    const SINT outputFrames = outputSampleLength / 2;

    for (SINT i = 0; i < outputFrames && i * 2 < samplesRead; ++i) {
        if (m_requestedChannelCount != mixxx::audio::ChannelCount::stereo()) {
            pBuffer[2 * stemCount * i + 2 * streamIdx] = m_buffer[2 * i];
            pBuffer[2 * stemCount * i + 2 * streamIdx + 1] = m_buffer[2 * i + 1];
        } else {
            pBuffer[2 * i] += m_buffer[2 * i];
            pBuffer[2 * i + 1] += m_buffer[2 * i + 1];
        }
    }
}

void SoundSourceSTEM::processWithResampler(size_t streamIdx,
        const WritableSampleFrames& globalSampleFrames,
        CSAMPLE* pBuffer) {
    const int targetSampleRate = m_targetSampleRate;
    const auto& streamInfo = m_pStereoStreams[streamIdx]->getSignalInfo();
    const int streamSampleRate = streamInfo.getSampleRate();
    const SINT outputFramesNeeded = globalSampleFrames.frameLength();
    const SINT targetStartFrame = globalSampleFrames.frameIndexRange().start();

    if (outputFramesNeeded <= 0 || outputFramesNeeded > 1000000) {
        return;
    }

    const SINT sourceStartFrame = static_cast<SINT>(
            (static_cast<int64_t>(targetStartFrame) * streamSampleRate) /
            targetSampleRate);

    const SINT inputFramesNeeded = std::min<SINT>(
            kMaxBufferSize,
            static_cast<SINT>(((static_cast<int64_t>(outputFramesNeeded) *
                                       streamSampleRate) +
                                      targetSampleRate - 1) /
                    targetSampleRate) +
                    8);

    const SINT inputSamplesNeeded = inputFramesNeeded * 2;
    if (inputSamplesNeeded <= 0 || inputSamplesNeeded > kMaxBufferSize * 2) {
        return;
    }
    if (inputSamplesNeeded + 10 > m_resampleInputBuffer.size()) {
        m_resampleInputBuffer = SampleBuffer(inputSamplesNeeded + 10);
    }

    WritableSampleFrames inputFrames(
            IndexRange::forward(sourceStartFrame, inputFramesNeeded),
            SampleBuffer::WritableSlice(m_resampleInputBuffer.data(),
                    inputSamplesNeeded));

    const auto readResult = m_pStereoStreams[streamIdx]->readSampleFrames(inputFrames);
    const SINT actualFramesRead = readResult.frameIndexRange().length();

    if (actualFramesRead < 4) {
        if (actualFramesRead >= 2) {
            processWithSimpleInterpolation(streamIdx,
                    globalSampleFrames,
                    pBuffer,
                    actualFramesRead,
                    streamSampleRate,
                    targetSampleRate);
        }
        return;
    }

    const std::size_t stemCount = m_pStereoStreams.size();
    const SINT safeOutputFrames = std::min(outputFramesNeeded,
            actualFramesRead * targetSampleRate / streamSampleRate + 4);

    for (SINT i = 0; i < safeOutputFrames; ++i) {
        const int64_t precisePos = static_cast<int64_t>(i) * streamSampleRate;
        const SINT sourceIndex = static_cast<SINT>(precisePos / targetSampleRate);
        const CSAMPLE fraction = static_cast<CSAMPLE>(precisePos % targetSampleRate) /
                static_cast<CSAMPLE>(targetSampleRate);

        if (sourceIndex >= 1 && sourceIndex + 3 < actualFramesRead) {
            interpolateAndMixSafe(streamIdx,
                    i,
                    sourceIndex,
                    fraction,
                    pBuffer,
                    stemCount,
                    actualFramesRead);
        } else if (sourceIndex + 1 < actualFramesRead) {
            linearInterpolateAndMixSafe(streamIdx,
                    i,
                    sourceIndex,
                    fraction,
                    pBuffer,
                    stemCount,
                    actualFramesRead);
        }
    }
}

void SoundSourceSTEM::interpolateAndMixSafe(size_t streamIdx,
        SINT outputIndex,
        SINT sourceIndex,
        CSAMPLE fraction,
        CSAMPLE* pBuffer,
        std::size_t stemCount,
        SINT maxFrames) {
    const CSAMPLE* in = m_resampleInputBuffer.data();
    const SINT baseIdx = sourceIndex * 2;
    const SINT maxSamples = maxFrames * 2;

    if (baseIdx < 2 || baseIdx + 5 >= maxSamples) {
        if (baseIdx >= 0 && baseIdx + 3 < maxSamples) {
            const CSAMPLE left =
                    in[baseIdx] * (1.0f - fraction) + in[baseIdx + 2] * fraction;
            const CSAMPLE right =
                    in[baseIdx + 1] * (1.0f - fraction) + in[baseIdx + 3] * fraction;
            mixToOutput(streamIdx, outputIndex, left, right, pBuffer, stemCount);
        }
        return;
    }

    const CSAMPLE left = safeCubicInterpolate(
            in[baseIdx - 2], in[baseIdx], in[baseIdx + 2], in[baseIdx + 4], fraction);
    const CSAMPLE right = safeCubicInterpolate(
            in[baseIdx - 1], in[baseIdx + 1], in[baseIdx + 3], in[baseIdx + 5], fraction);

    mixToOutput(streamIdx, outputIndex, left, right, pBuffer, stemCount);
}

void SoundSourceSTEM::linearInterpolateAndMixSafe(size_t streamIdx,
        SINT outputIndex,
        SINT sourceIndex,
        CSAMPLE fraction,
        CSAMPLE* pBuffer,
        std::size_t stemCount,
        SINT maxFrames) {
    const CSAMPLE* in = m_resampleInputBuffer.data();
    const SINT baseIdx = sourceIndex * 2;
    const SINT maxSamples = maxFrames * 2;
    if (baseIdx < 0 || baseIdx + 3 >= maxSamples) {
        return;
    }
    const CSAMPLE left = in[baseIdx] * (1.0f - fraction) + in[baseIdx + 2] * fraction;
    const CSAMPLE right = in[baseIdx + 1] * (1.0f - fraction) + in[baseIdx + 3] * fraction;
    mixToOutput(streamIdx, outputIndex, left, right, pBuffer, stemCount);
}

void SoundSourceSTEM::processWithSimpleInterpolation(size_t streamIdx,
        const WritableSampleFrames& globalSampleFrames,
        CSAMPLE* pBuffer,
        SINT availableFrames,
        int streamSampleRate,
        int targetSampleRate) {
    const SINT outputFramesNeeded = globalSampleFrames.frameLength();
    const CSAMPLE* in = m_resampleInputBuffer.data();
    const std::size_t stemCount = m_pStereoStreams.size();

    for (SINT i = 0; i < outputFramesNeeded && i < availableFrames * 2; ++i) {
        const SINT sourcePos = (i * streamSampleRate) / targetSampleRate;
        if (sourcePos + 1 < availableFrames) {
            const CSAMPLE left = in[sourcePos * 2];
            const CSAMPLE right = in[sourcePos * 2 + 1];
            mixToOutput(streamIdx, i, left, right, pBuffer, stemCount);
        }
    }
}

CSAMPLE SoundSourceSTEM::safeCubicInterpolate(
        CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu) {
    const CSAMPLE a0 = y3 - y2 - y0 + y1;
    const CSAMPLE a1 = y0 - y1 - a0;
    const CSAMPLE a2 = y2 - y0;
    return ((a0 * mu + a1) * mu + a2) * mu + y1;
}

CSAMPLE SoundSourceSTEM::robustCubicInterpolate(
        CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu) {
    const CSAMPLE a0 = y3 - y2 - y0 + y1;
    const CSAMPLE a1 = y0 - y1 - a0;
    const CSAMPLE a2 = y2 - y0;
    return (((a0 * mu) + a1) * mu + a2) * mu + y1;
}

CSAMPLE SoundSourceSTEM::cubicInterpolate(
        CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, double mu) {
    const double mu2 = mu * mu;
    const double a0 = y3 - y2 - y0 + y1;
    const double a1 = y0 - y1 - a0;
    const double a2 = y2 - y0;
    const double a3 = y1;
    return static_cast<CSAMPLE>(a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3);
}

void SoundSourceSTEM::interpolateAndMix(size_t streamIdx,
        SINT outputIndex,
        SINT sourceIndex,
        CSAMPLE fraction,
        CSAMPLE* pBuffer,
        std::size_t stemCount) {
    const CSAMPLE* in = m_resampleInputBuffer.data();
    const SINT baseIdx = sourceIndex * 2;
    const CSAMPLE left = safeCubicInterpolate(
            in[baseIdx - 2], in[baseIdx], in[baseIdx + 2], in[baseIdx + 4], fraction);
    const CSAMPLE right = safeCubicInterpolate(
            in[baseIdx - 1], in[baseIdx + 1], in[baseIdx + 3], in[baseIdx + 5], fraction);
    if (m_requestedChannelCount != mixxx::audio::ChannelCount::stereo()) {
        pBuffer[2 * stemCount * outputIndex + 2 * static_cast<SINT>(streamIdx)] = left;
        pBuffer[2 * stemCount * outputIndex + 2 * static_cast<SINT>(streamIdx) + 1] = right;
    } else {
        pBuffer[2 * outputIndex] += left;
        pBuffer[2 * outputIndex + 1] += right;
    }
}

void SoundSourceSTEM::linearInterpolateAndMix(size_t streamIdx,
        SINT outputIndex,
        SINT sourceIndex,
        CSAMPLE fraction,
        CSAMPLE* pBuffer,
        std::size_t stemCount) {
    const CSAMPLE* in = m_resampleInputBuffer.data();
    const SINT baseIdx = sourceIndex * 2;
    const CSAMPLE left = in[baseIdx] * (1.0f - fraction) + in[baseIdx + 2] * fraction;
    const CSAMPLE right = in[baseIdx + 1] * (1.0f - fraction) + in[baseIdx + 3] * fraction;
    if (m_requestedChannelCount != mixxx::audio::ChannelCount::stereo()) {
        pBuffer[2 * stemCount * outputIndex + 2 * static_cast<SINT>(streamIdx)] = left;
        pBuffer[2 * stemCount * outputIndex + 2 * static_cast<SINT>(streamIdx) + 1] = right;
    } else {
        pBuffer[2 * outputIndex] += left;
        pBuffer[2 * outputIndex + 1] += right;
    }
}

void SoundSourceSTEM::mixToOutput(size_t streamIdx,
        SINT outputIndex,
        CSAMPLE left,
        CSAMPLE right,
        CSAMPLE* pBuffer,
        std::size_t stemCount) {
    if (m_requestedChannelCount != mixxx::audio::ChannelCount::stereo()) {
        pBuffer[2 * stemCount * outputIndex + 2 * static_cast<SINT>(streamIdx)] = left;
        pBuffer[2 * stemCount * outputIndex + 2 * static_cast<SINT>(streamIdx) + 1] = right;
    } else {
        pBuffer[2 * outputIndex] += left;
        pBuffer[2 * outputIndex + 1] += right;
    }
}

// void SoundSourceSTEM::initializeResamplers(int targetSampleRate) {
//     if (targetSampleRate <= 0) {
//         return;
//     }
//     const std::size_t stemCount = m_pStereoStreams.size();
//     m_needsResampling.assign(stemCount, false);
//     for (std::size_t i = 0; i < stemCount; ++i) {
//         const auto streamRate = m_pStereoStreams[i]->getSignalInfo().getSampleRate();
//         if (streamRate != targetSampleRate) {
//             m_needsResampling[i] = true;
//         }
//     }
// }

void SoundSourceSTEM::initializeResamplers(mixxx::audio::SampleRate targetSampleRate) {
    if (!targetSampleRate.isValid() || targetSampleRate <= 0) {
        return;
    }
    const std::size_t stemCount = m_pStereoStreams.size();
    m_needsResampling.assign(stemCount, false);
    for (std::size_t i = 0; i < stemCount; ++i) {
        const auto streamRate =
                m_pStereoStreams[i]->getSignalInfo().getSampleRate();
        m_needsResampling[i] = (streamRate != targetSampleRate);
    }
}

// The following helpers are declared in the header but not used by the
// resampling loop above. They are provided so the header stays consistent
// with your original patch; if you don't call them, drop them.
int SoundSourceSTEM::getStemSampleRate() const {
    return m_pStereoStreams.size() > 1
            ? m_pStereoStreams[1]->getSignalInfo().getSampleRate()
            : 44100;
}

int SoundSourceSTEM::getReferenceSampleRate() const {
    if (m_referenceStreamIdx >= 0 &&
            m_referenceStreamIdx < static_cast<SINT>(m_pStereoStreams.size())) {
        return m_pStereoStreams[m_referenceStreamIdx]->getSignalInfo().getSampleRate();
    }
    return 44100;
}

bool SoundSourceSTEM::validateBufferAccess(SINT sourceIndex, SINT inputFramesNeeded) const {
    return sourceIndex >= 0 && sourceIndex < inputFramesNeeded;
}

void SoundSourceSTEM::safeBufferCopy(const CSAMPLE* source,
        CSAMPLE* dest,
        SINT samples,
        SINT sourceOffset,
        SINT destOffset) {
    if (!source || !dest || samples <= 0) {
        return;
    }
    for (SINT i = 0; i < samples; ++i) {
        dest[destOffset + i] = source[sourceOffset + i];
    }
}

void SoundSourceSTEM::processPremixDownsampler(
        const WritableSampleFrames& globalSampleFrames,
        CSAMPLE* pBuffer) {
    // Kept for API compatibility; the current read path uses the generic
    // resampler for the premix stream as well.
    Q_UNUSED(globalSampleFrames);
    Q_UNUSED(pBuffer);
}

} // namespace mixxx

// #include "sources/soundsourcestem.h"
//
// extern "C" {
//
// #include <libavutil/avutil.h>
// #if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
// #include <libavutil/channel_layout.h>
// #endif
//
// } // extern "C"
//
// #include <memory>
//
// #include "sources/soundsourceffmpeg.h"
// #include "util/assert.h"
// #include "util/logger.h"
// #include "util/sample.h"
//
// #if !defined(VERBOSE_DEBUG_LOG)
// #define VERBOSE_DEBUG_LOG false
// #endif
//
// namespace mixxx {
//
// namespace {
//
//// STEM constants
// constexpr int kNumStreams = 5;
// constexpr int kRequiredStreamCount = kNumStreams - 1; // Stem count doesn't
// include the main mix
//
// const Logger kLogger("SoundSourceSTEM");
//
//// Local RAII for AVFormatContext; SoundSourceFFmpeg's wrapper is private.
// struct AVFormatContextDeleter {
//     void operator()(AVFormatContext* ctx) const {
//         if (ctx) {
//             avformat_close_input(&ctx);
//         }
//     }
// };
// using AVFormatContextPtr =
//         std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
//
// } // anonymous namespace
//
// const QString SoundSourceProviderSTEM::kDisplayName = QStringLiteral("STEM
// with FFmpeg");
//
// QStringList SoundSourceProviderSTEM::getSupportedFileTypes() const {
//     return {"stem.mp4", "stem.m4a"};
// }
//
// SoundSourceProviderPriority SoundSourceProviderSTEM::getPriorityHint(
//         const QString& supportedFileType) const {
//     Q_UNUSED(supportedFileType)
//     return SoundSourceProviderPriority::Higher;
// }
//
// QString SoundSourceProviderSTEM::getVersionString() const {
//     return QString::fromUtf8(av_version_info());
// }
//
// SoundSourceSTEM::SoundSourceSTEM(const QUrl& url)
//         : SoundSource(url) {
// }
//
// SoundSourceSTEM::~SoundSourceSTEM() = default;
//
// SoundSource::OpenResult SoundSourceSTEM::tryOpen(
//         OpenMode /*mode*/,
//         const OpenParams& params) {
//     // Ensure that the source isn't yet opened
//     VERIFY_OR_DEBUG_ASSERT(!m_requestedChannelCount.isValid()) {
//         return OpenResult::Failed;
//     }
//     // Open input. RAII handles cleanup on every return path.
//     AVFormatContextPtr pavInputFormatContextGuard(
//             SoundSourceFFmpeg::openInputFile(getLocalFileName()));
//     AVFormatContext* pavInputFormatContext =
//     pavInputFormatContextGuard.get(); if (pavInputFormatContext == nullptr) {
//         kLogger.warning()
//                 << "Failed to open input file"
//                 << getLocalFileName();
//         return OpenResult::Failed;
//     }
// #if VERBOSE_DEBUG_LOG
//     kLogger.debug()
//             << "AVFormatContext"
//             << "{ nb_streams" << pavInputFormatContext->nb_streams
//             << "| start_time" << pavInputFormatContext->start_time
//             << "| duration" << pavInputFormatContext->duration
//             << "| bit_rate" << pavInputFormatContext->bit_rate
//             << "| packet_size" << pavInputFormatContext->packet_size
//             << "| audio_codec_id" << pavInputFormatContext->audio_codec_id
//             << "| output_ts_offset" <<
//             pavInputFormatContext->output_ts_offset
//             << '}';
// #endif
//
//     // Retrieve stream information
//     const int avformat_find_stream_info_result =
//             avformat_find_stream_info(pavInputFormatContext, nullptr);
//     if (avformat_find_stream_info_result != 0) {
//         DEBUG_ASSERT(avformat_find_stream_info_result < 0);
//         kLogger.warning().noquote()
//                 << "avformat_find_stream_info() failed:"
//                 <<
//                 SoundSourceFFmpeg::formatErrorString(avformat_find_stream_info_result);
//         return OpenResult::Failed;
//     }
//
//     bool foundPremixedStream = false;
//     AVStream* firstStem = nullptr;
//     int stemCount = 0;
//     uint selectedStemMask = params.stemMask();
//     VERIFY_OR_DEBUG_ASSERT(selectedStemMask <= 2 <<
//     mixxx::kMaxSupportedStems) {
//         kLogger.warning().noquote()
//                 << "Invalid selected stem mask" << selectedStemMask;
//         return OpenResult::Failed;
//     }
//     OpenParams stemParam = params;
//     stemParam.setChannelCount(mixxx::audio::ChannelCount::stereo());
//     for (unsigned int streamIdx = 0; streamIdx <
//     pavInputFormatContext->nb_streams; streamIdx++) {
//         if (pavInputFormatContext->streams[streamIdx]->codecpar->codec_type
//         !=
//                 AVMEDIA_TYPE_AUDIO) {
//             continue;
//         }
//
// #if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
//         if
//         (pavInputFormatContext->streams[streamIdx]->codecpar->ch_layout.nb_channels
//         !=
//                 mixxx::audio::ChannelCount::stereo()) {
// #else
//         if (pavInputFormatContext->streams[streamIdx]->codecpar->channels !=
//                 mixxx::audio::ChannelCount::stereo()) {
// #endif
//             kLogger.warning().noquote()
//                     << "stream at position" << streamIdx << "is not in
//                     stereo";
//             return OpenResult::Failed;
//         }
//
//         if (!foundPremixedStream) {
//             // We can currently allow this, as we NEVER LOAD the pre-mastered
//             // track, as we do not have analyzer data for it.
//             // This is because we only support one set of metadata for the
//             whole
//             // STEM file, where we determine the track parameters from an
//             // on-the-fly mix of all 4 stems. Especially the replaygain
//             differs
//             // between the on-the-fly mix and the pre-mastered track, because
//             we
//             // do not apply DSP (limiter, equalizer, compressor) to the
//             // on-the-fly mix. If this ever gets changed, we should set
//             // `initSampleRateOnce` and `initBitrateOnce` to match the stem
//             // sample rate and bit rate, such that
//             // SoundSourceFFmpeg::resampleDecodedAVFrame will take care to
//             // resample the main stream, in order to use the same time scale
//             and
//             // keep a working grid/cue definition
//             foundPremixedStream = true;
//             continue;
//         }
//
//         stemCount++;
//
//         if (!firstStem) {
//             // We always keep track of the stem to verify that stem stream
//             properties are matching firstStem =
//             pavInputFormatContext->streams[streamIdx];
//         } else {
//             if (pavInputFormatContext->streams[streamIdx]->codecpar->codec_id
//             !=
//                     firstStem->codecpar->codec_id) {
//                 kLogger.warning().noquote()
//                         << "Stem at position" << streamIdx << "is using a
//                         different codec";
//                 return OpenResult::Failed;
//             }
//
//             if (pavInputFormatContext->streams[streamIdx]
//                             ->codecpar->sample_rate !=
//                     firstStem->codecpar->sample_rate) {
//                 kLogger.warning().noquote()
//                         << "Stem at position" << streamIdx << "is using a
//                         different sample rate";
//                 return OpenResult::Failed;
//             }
//         }
//
//         // StemIdx is equal to StreamIdx -1 (the main mix)
//         if (selectedStemMask && !(selectedStemMask & 1 << (streamIdx - 1))) {
//             continue;
//         }
//
//         m_pStereoStreams.emplace_back(std::make_unique<SoundSourceFFmpeg>(getUrl(),
//         streamIdx)); if (m_pStereoStreams.back()->open(OpenMode::Strict
//         /*Unused*/,
//                     stemParam) != OpenResult::Succeeded) {
//             return OpenResult::Failed;
//         }
//     }
//
//     if (stemCount != kRequiredStreamCount) {
//         kLogger.warning().noquote()
//                 << "expected to find" << kRequiredStreamCount
//                 << "stem but found" << stemCount;
//         close();
//         return OpenResult::Failed;
//     }
//
//     VERIFY_OR_DEBUG_ASSERT(!m_pStereoStreams.empty()) {
//         kLogger.warning().noquote()
//                 << "no stem track were selected";
//         close();
//         return OpenResult::Failed;
//     }
//
//     if (params.getSignalInfo().getChannelCount() ==
//                     mixxx::audio::ChannelCount::stereo() ||
//             selectedStemMask) {
//         // Requesting a stereo stream (used for samplers and preview decks)
//         m_requestedChannelCount = mixxx::audio::ChannelCount::stereo();
//         initChannelCountOnce(mixxx::audio::ChannelCount::stereo());
//     } else {
//         // No special channel format request
//         m_requestedChannelCount = mixxx::audio::ChannelCount::stem();
//         initChannelCountOnce(
//                 static_cast<int>(mixxx::audio::ChannelCount::stereo() *
//                         m_pStereoStreams.size()));
//     }
//
//     initSampleRateOnce(m_pStereoStreams.front()->getSignalInfo().getSampleRate());
//     initBitrateOnce(m_pStereoStreams.front()->getBitrate());
//     initFrameIndexRangeOnce(m_pStereoStreams.front()->frameIndexRange());
//
//     return OpenResult::Succeeded;
// }
//
// void SoundSourceSTEM::close() {
//     for (auto& stream : m_pStereoStreams) {
//         stream->close();
//     }
// }
//
// ReadableSampleFrames SoundSourceSTEM::readSampleFramesClamped(
//         const WritableSampleFrames& globalSampleFrames) {
//     VERIFY_OR_DEBUG_ASSERT(m_requestedChannelCount.isValid()) {
//         return ReadableSampleFrames();
//     }
//
//     VERIFY_OR_DEBUG_ASSERT(globalSampleFrames.writableLength() %
//                     m_requestedChannelCount ==
//             0) {
//         return ReadableSampleFrames();
//     };
//
//     SINT stemSampleLength =
//     m_pStereoStreams.front()->getSignalInfo().frames2samples(
//             globalSampleFrames.frameLength());
//
//     // The same buffer is reused between requests tp prevent reallocation,
//     but
//     // it will be reallocated if a larger chunk is requested and will keep
//     the
//     // new maximum size
//     if (stemSampleLength > m_buffer.size()) {
//         m_buffer = SampleBuffer(stemSampleLength);
//     }
//
//     ReadableSampleFrames read(globalSampleFrames.frameIndexRange(),
//             SampleBuffer::ReadableSlice(
//                     globalSampleFrames.writableData(),
//                     globalSampleFrames.writableLength()));
//     std::size_t stemCount = m_pStereoStreams.size();
//     CSAMPLE* pBuffer = globalSampleFrames.writableData();
//
//     if (m_requestedChannelCount == mixxx::audio::ChannelCount::stereo() &&
//     stemCount != 1) {
//         SampleUtil::clear(pBuffer, globalSampleFrames.writableLength());
//     } else {
//         DEBUG_ASSERT(stemSampleLength * static_cast<SINT>(stemCount) ==
//                 globalSampleFrames.writableLength());
//     }
//
//     if (stemCount == 1) {
//         m_pStereoStreams[0]->readSampleFrames(globalSampleFrames);
//         return read;
//     }
//
//     for (std::size_t streamIdx = 0; streamIdx < stemCount; streamIdx++) {
//         WritableSampleFrames currentStemFrame = WritableSampleFrames(
//                 globalSampleFrames.frameIndexRange(),
//                 SampleBuffer::WritableSlice(
//                         m_buffer.data(),
//                         stemSampleLength));
//         m_pStereoStreams[streamIdx]->readSampleFrames(currentStemFrame);
//
//         // Each m_pStereoStreams[streamIdx] provides a standard stereo signal
//         (L/R).
//         // in stem mode we need to transform the data to an interleaved
//         layout:
//         // 1L1R2L2R3L3R4L4R, 1L1R2L2R3L3R4L4R ...
//         if (m_requestedChannelCount != mixxx::audio::ChannelCount::stereo())
//         {
//             // Change the sample layout to interleave all channels together
//             for (SINT i = 0; i < stemSampleLength / 2; i++) {
//                 pBuffer[2 * stemCount * i + 2 * streamIdx] = m_buffer[2 * i];
//                 pBuffer[2 * stemCount * i + 2 * streamIdx + 1] = m_buffer[2 *
//                 i + 1];
//             }
//         } else {
//             // Change the sample layout to mix all channels together
//             SampleUtil::add(pBuffer, m_buffer.data(), stemSampleLength);
//         }
//     }
//     return read;
// }
//
// } // namespace mixxx
