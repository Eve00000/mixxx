#include "sources/soundsourcestem.h"

#include "sources/readaheadframebuffer.h"

extern "C" {

#include <libavutil/avutil.h>
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#endif

#include <libswresample/swresample.h>

} // extern "C"

#include "util/assert.h"
#include "util/fpclassify.h"
#include "util/logger.h"
#include "util/sample.h"

#if !defined(VERBOSE_DEBUG_LOG)
#define VERBOSE_DEBUG_LOG false
#endif

namespace mixxx {

namespace {

// STEM constants
constexpr int kNumStreams = 5;
// constexpr int kRequiredStreamCount = kNumStreams - 1; // Stem count doesn't include the main mix
constexpr int kRequiredStreamCount = kNumStreams;

const Logger kLogger("SoundSourceSTEM");

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

SoundSourceSingleSTEM::SoundSourceSingleSTEM(const QUrl& url, unsigned int streamIdx)
        : SoundSourceFFmpeg(url),
          m_streamIdx(streamIdx) {
}

SoundSource::OpenResult SoundSourceSingleSTEM::tryOpen(
        OpenMode /*mode*/,
        const OpenParams& params) {
    // Open input
    {
        AVFormatContext* pavInputFormatContext =
                openInputFile(getLocalFileName());
        if (pavInputFormatContext == nullptr) {
            kLogger.warning()
                    << "Failed to open input file"
                    << getLocalFileName();
            return OpenResult::Failed;
        }
        m_pavInputFormatContext.take(&pavInputFormatContext);
    }
#if VERBOSE_DEBUG_LOG
    kLogger.debug()
            << "AVFormatContext"
            << "{ nb_streams" << m_pavInputFormatContext->nb_streams
            << "| start_time" << m_pavInputFormatContext->start_time
            << "| duration" << m_pavInputFormatContext->duration
            << "| bit_rate" << m_pavInputFormatContext->bit_rate
            << "| packet_size" << m_pavInputFormatContext->packet_size
            << "| audio_codec_id" << m_pavInputFormatContext->audio_codec_id
            << "| output_ts_offset" << m_pavInputFormatContext->output_ts_offset
            << '}';
#endif

    // Retrieve stream information
    const int avformat_find_stream_info_result =
            avformat_find_stream_info(m_pavInputFormatContext, nullptr);
    if (avformat_find_stream_info_result != 0) {
        DEBUG_ASSERT(avformat_find_stream_info_result < 0);
        kLogger.warning().noquote()
                << "avformat_find_stream_info() failed:"
                << formatErrorString(avformat_find_stream_info_result);
        return OpenResult::Failed;
    }

    if (m_pavInputFormatContext->nb_streams <= m_streamIdx) {
        kLogger.warning().noquote()
                << "cannot find stream" << m_streamIdx;
        return OpenResult::Failed;
    }

    if (m_pavInputFormatContext->streams[m_streamIdx]->codecpar->codec_type !=
            AVMEDIA_TYPE_AUDIO) {
        kLogger.warning().noquote()
                << "selected stream isn't a valid audio stream";
        return OpenResult::Failed;
    }

    AVStream* selectedAudioStream = m_pavInputFormatContext->streams[m_streamIdx];

    // Open the decoder for these streams
    const AVCodec* pDecoder = avcodec_find_decoder(selectedAudioStream->codecpar->codec_id);
    if (!pDecoder) {
        kLogger.warning()
                << "av_find_best_stream() failed to find a decoder for any audio stream";
        return SoundSource::OpenResult::Aborted;
    }

    DEBUG_ASSERT(pDecoder);

    if (pDecoder->id == AV_CODEC_ID_AAC ||
            pDecoder->id == AV_CODEC_ID_AAC_LATM) {
        // We only allow AAC decoders that pass our seeking tests
        if (std::strcmp(pDecoder->name, "aac") != 0 && std::strcmp(pDecoder->name, "aac_at") != 0) {
            const AVCodec* pAacDecoder = avcodec_find_decoder_by_name("aac");
            if (pAacDecoder) {
                pDecoder = pAacDecoder;
            } else {
                kLogger.warning()
                        << "Internal aac decoder not found in your FFmpeg "
                           "build."
                        << "To enable AAC support, please install an FFmpeg "
                           "version with the internal aac decoder enabled."
                           "Note 1: The libfdk_aac decoder is no working properly "
                           "with Mixxx, FFmpeg's internal AAC decoder does."
                        << "Note 2: AAC decoding may be subject to patent "
                           "restrictions, depending on your country.";
            }
        }
    }

    kLogger.debug() << "using FFmpeg decoder:" << pDecoder->long_name;

    // Select the main mix stream for decoding
    AVStream* pavStream = selectedAudioStream;
    DEBUG_ASSERT(pavStream != nullptr);

    // Allocate decoding context
    AVCodecContextPtr pavCodecContext = AVCodecContextPtr::alloc(pDecoder);
    if (!pavCodecContext) {
        return SoundSource::OpenResult::Aborted;
    }

    // Configure decoding context
    const int avcodec_parameters_to_context_result =
            avcodec_parameters_to_context(pavCodecContext, pavStream->codecpar);
    if (avcodec_parameters_to_context_result != 0) {
        DEBUG_ASSERT(avcodec_parameters_to_context_result < 0);
        kLogger.warning().noquote()
                << "avcodec_parameters_to_context() failed:"
                << formatErrorString(avcodec_parameters_to_context_result);
        return SoundSource::OpenResult::Aborted;
    }

    // Request output format
    pavCodecContext->request_sample_fmt = s_avSampleFormat;
    if (params.getSignalInfo().getChannelCount().isValid()) {
        // A dedicated number of channels for the output signal
        // has been requested. Forward this to FFmpeg to avoid
        // manual resampling or post-processing after decoding.
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
        av_channel_layout_default(&pavCodecContext->ch_layout,
                params.getSignalInfo().getChannelCount());
#else
        pavCodecContext->request_channel_layout =
                av_get_default_channel_layout(params.getSignalInfo().getChannelCount());
#endif
    }

    // Open decoding context
    if (!openDecodingContext(pavCodecContext)) {
        // early exit on any error
        return SoundSource::OpenResult::Failed;
    }

    // Initialize members
    m_pavCodecContext = std::move(pavCodecContext);
    m_pavStream = pavStream;

    if (kLogger.debugEnabled()) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
        AVChannelLayout fixedChannelLayout;
        initChannelLayoutFromStream(&fixedChannelLayout, *m_pavStream);
#endif
        kLogger.debug()
                << "AVStream"
                << "{ index" << m_pavStream->index
                << "| id" << m_pavStream->id
                << "| time_base" << m_pavStream->time_base.num << '/' << m_pavStream->time_base.den
                << "| start_time" << m_pavStream->start_time
                << "| duration" << m_pavStream->duration
                << "| nb_frames" << m_pavStream->nb_frames
                << "| codec_type" << m_pavStream->codecpar->codec_type
                << "| codec_id" << m_pavStream->codecpar->codec_id
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
                << "| ch_layout.nb_channels" << m_pavStream->codecpar->ch_layout.nb_channels
                << "| ch_layout.order" << m_pavStream->codecpar->ch_layout.order
                << "| ch_layout.order (fixed)" << fixedChannelLayout.order
#else
                << "| channels" << m_pavStream->codecpar->channels
                << "| channel_layout" << m_pavStream->codecpar->channel_layout
                << "| channel_layout (fixed)" << getStreamChannelLayout(*m_pavStream)
#endif
                << "| format" << m_pavStream->codecpar->format
                << "| sample_rate" << m_pavStream->codecpar->sample_rate
                << "| bit_rate" << m_pavStream->codecpar->bit_rate
                << "| frame_size" << m_pavStream->codecpar->frame_size
                << "| seek_preroll" << m_pavStream->codecpar->seek_preroll
                << "| initial_padding" << m_pavStream->codecpar->initial_padding
                << "| trailing_padding" << m_pavStream->codecpar->trailing_padding
                << '}';
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
        av_channel_layout_uninit(&fixedChannelLayout);
#endif
    }

    audio::ChannelCount channelCount;
    audio::SampleRate sampleRate;
    if (!initResampling(&channelCount, &sampleRate)) {
        return OpenResult::Failed;
    }
    if (!initChannelCountOnce(channelCount)) {
        kLogger.warning()
                << "Failed to initialize number of channels"
                << channelCount;
        return OpenResult::Aborted;
    }
    if (!initSampleRateOnce(sampleRate)) {
        kLogger.warning()
                << "Failed to initialize sample rate"
                << sampleRate;
        return OpenResult::Aborted;
    }

    const auto streamBitrate =
            audio::Bitrate(m_pavStream->codecpar->bit_rate / 1000); // kbps
    if (streamBitrate.isValid() && !initBitrateOnce(streamBitrate)) {
        kLogger.warning()
                << "Failed to initialize bitrate"
                << streamBitrate;
        return OpenResult::Failed;
    }

    if (m_pavStream->duration == AV_NOPTS_VALUE) {
        // Streams with unknown or unlimited duration are
        // not (yet) supported.
        kLogger.warning()
                << "Unknown or unlimited stream duration";
        return OpenResult::Failed;
    }
    const auto streamFrameIndexRange =
            getStreamFrameIndexRange(*m_pavStream);
    VERIFY_OR_DEBUG_ASSERT(streamFrameIndexRange.start() <= streamFrameIndexRange.end()) {
        kLogger.warning()
                << "Stream with unsupported or invalid frame index range"
                << streamFrameIndexRange;
        return OpenResult::Failed;
    }

    // Decoding MP3/AAC files manually into WAV using the ffmpeg CLI and
    // comparing the audio data revealed that we need to map the nominal
    // range of the stream onto our internal range starting at FrameIndex 0.
    // See also the discussion regarding cue point shift/offset:
    // https://mixxx.zulipchat.com/#narrow/stream/109171-development/topic/Cue.20shift.2Foffset
    const auto frameIndexRange = IndexRange::forward(
            0,
            streamFrameIndexRange.length());
    if (!initFrameIndexRangeOnce(frameIndexRange)) {
        kLogger.warning()
                << "Failed to initialize frame index range"
                << frameIndexRange;
        return OpenResult::Failed;
    }

    DEBUG_ASSERT(!m_pavDecodedFrame);
    m_pavDecodedFrame = av_frame_alloc();

    // FFmpeg does not provide sample-accurate decoding after random seeks
    // in the stream out of the box. Depending on the actual codec we need
    // to account for this and start decoding before the target position.
    m_seekPrerollFrameCount = getStreamSeekPrerollFrameCount(*m_pavStream);
#if VERBOSE_DEBUG_LOG
    kLogger.debug() << "Seek preroll frame count:" << m_seekPrerollFrameCount;
#endif

    m_frameBuffer = ReadAheadFrameBuffer(
            getSignalInfo(),
            frameBufferCapacityForStream(*m_pavStream));
#if VERBOSE_DEBUG_LOG
    kLogger.debug() << "Frame buffer capacity:" << m_frameBuffer.capacity();
#endif

    return OpenResult::Succeeded;
}

SoundSourceSTEM::SoundSourceSTEM(const QUrl& url)
        : SoundSource(url) {
}

SoundSource::OpenResult SoundSourceSTEM::tryOpen(
        OpenMode /*mode*/,
        const OpenParams& params) {
    // Ensure that the source isn't yet opened
    VERIFY_OR_DEBUG_ASSERT(!m_requestedChannelCount.isValid()) {
        return OpenResult::Failed;
    }
    // Open input
    AVFormatContext* pavInputFormatContext =
            SoundSourceFFmpeg::openInputFile(getLocalFileName());
    if (pavInputFormatContext == nullptr) {
        kLogger.warning()
                << "Failed to open input file"
                << getLocalFileName();
        return OpenResult::Failed;
    }
#if VERBOSE_DEBUG_LOG
    kLogger.debug()
            << "AVFormatContext"
            << "{ nb_streams" << pavInputFormatContext->nb_streams
            << "| start_time" << pavInputFormatContext->start_time
            << "| duration" << pavInputFormatContext->duration
            << "| bit_rate" << pavInputFormatContext->bit_rate
            << "| packet_size" << pavInputFormatContext->packet_size
            << "| audio_codec_id" << pavInputFormatContext->audio_codec_id
            << "| output_ts_offset" << pavInputFormatContext->output_ts_offset
            << '}';
#endif

    // Retrieve stream information
    const int avformat_find_stream_info_result =
            avformat_find_stream_info(pavInputFormatContext, nullptr);
    if (avformat_find_stream_info_result != 0) {
        DEBUG_ASSERT(avformat_find_stream_info_result < 0);
        kLogger.warning().noquote()
                << "avformat_find_stream_info() failed:"
                << SoundSourceFFmpeg::formatErrorString(avformat_find_stream_info_result);
        return OpenResult::Failed;
    }

    // bool foundPremixedStream = false;
    // AVStream* firstStem = nullptr;
    int stemCount = 0;
    uint selectedStemMask = params.stemMask();
    VERIFY_OR_DEBUG_ASSERT(selectedStemMask <= 1 << mixxx::kMaxSupportedStems) {
        kLogger.warning().noquote()
                << "Invalid selected stem mask" << selectedStemMask;
        return OpenResult::Failed;
    }
    OpenParams stemParam = params;
    stemParam.setChannelCount(mixxx::audio::ChannelCount::stereo());
    for (unsigned int streamIdx = 0; streamIdx < pavInputFormatContext->nb_streams; streamIdx++) {
        if (pavInputFormatContext->streams[streamIdx]->codecpar->codec_type !=
                AVMEDIA_TYPE_AUDIO) {
            continue;
        }

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
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

        // if (!foundPremixedStream) {
        //     // We can currently allow this, as we NEVER LOAD the pre-mastered
        //     // track, as we do not have analyzer data for it.
        //     // This is because we only support one set of metadata for the whole
        //     // STEM file, where we determine the track parameters from an
        //     // on-the-fly mix of all 4 stems. Especially the replaygain differs
        //     // between the on-the-fly mix and the pre-mastered track, because we
        //     // do not apply DSP (limiter, equalizer, compressor) to the
        //     // on-the-fly mix. If this ever gets changed, we should set
        //     // `initSampleRateOnce` and `initBitrateOnce` to match the stem
        //     // sample rate and bit rate, such that
        //     // SoundSourceFFmpeg::resampleDecodedAVFrame will take care to
        //     // resample the main stream, in order to use the same time scale and
        //     // keep a working grid/cue definition
        //     foundPremixedStream = true;
        //     continue;
        // }

        stemCount++;

        // if (!firstStem) {
        //     // We always keep track of the stem to verify that stem stream
        //     properties are matching firstStem =
        //     pavInputFormatContext->streams[streamIdx];
        // } else {
        //     if (pavInputFormatContext->streams[streamIdx]->codecpar->codec_id
        //     !=
        //             firstStem->codecpar->codec_id) {
        //         kLogger.warning().noquote()
        //                 << "Stem at position" << streamIdx << "is using a
        //                 different codec";
        //         return OpenResult::Failed;
        //     }

        //    if (pavInputFormatContext->streams[streamIdx]
        //                    ->codecpar->sample_rate !=
        //            firstStem->codecpar->sample_rate) {
        //        kLogger.warning().noquote()
        //                << "Stem at position" << streamIdx << "is using a different sample rate";
        //        return OpenResult::Failed;
        //    }
        //}

        //// StemIdx is equal to StreamIdx -1 (the main mix)
        // if (selectedStemMask && !(selectedStemMask & 1 << (streamIdx - 1))) {
        //     continue;
        // }

        m_pStereoStreams.emplace_back(std::make_unique<SoundSourceSingleSTEM>(getUrl(), streamIdx));
        if (m_pStereoStreams.back()->open(OpenMode::Strict /*Unused*/,
                    stemParam) != OpenResult::Succeeded) {
            return OpenResult::Failed;
        }
    }

    if (stemCount != kRequiredStreamCount) {
        kLogger.warning().noquote()
                << "expected to find" << kRequiredStreamCount
                << "stem but found" << stemCount;
        close();
        return OpenResult::Failed;
    }

    VERIFY_OR_DEBUG_ASSERT(!m_pStereoStreams.empty()) {
        kLogger.warning().noquote()
                << "no stem track were selected";
        close();
        return OpenResult::Failed;
    }

    if (params.getSignalInfo().getChannelCount() ==
                    mixxx::audio::ChannelCount::stereo() ||
            selectedStemMask) {
        // Requesting a stereo stream (used for samples and preview decks)
        m_requestedChannelCount = mixxx::audio::ChannelCount::stereo();
        initChannelCountOnce(mixxx::audio::ChannelCount::stereo());
    } else {
        // No special channel format request
        m_requestedChannelCount = mixxx::audio::ChannelCount::stem();
        initChannelCountOnce(
                static_cast<int>(mixxx::audio::ChannelCount::stereo() *
                        m_pStereoStreams.size()));
    }

    /*initSampleRateOnce(m_pStereoStreams.front()->getSignalInfo().getSampleRate());
    initBitrateOnce(m_pStereoStreams.front()->getBitrate());
    initFrameIndexRangeOnce(m_pStereoStreams.front()->frameIndexRange());*/

    m_upSampleStems = params.getUpSampleStems();

    const auto& premixInfo = m_pStereoStreams.front()->getSignalInfo();

    if (m_upSampleStems) {
        // Use premix/original sample rate as target
        m_referenceStreamIdx = 0;

        m_targetSampleRate =
                premixInfo.getSampleRate();
    } else {
        VERIFY_OR_DEBUG_ASSERT(
                m_pStereoStreams.size() > 1) {
            return OpenResult::Failed;
        }

        // Use first stem sample rate as target
        m_referenceStreamIdx = 1;
        const auto& stemInfo = m_pStereoStreams[1]->getSignalInfo();
        m_targetSampleRate = stemInfo.getSampleRate();
    }

    initSampleRateOnce(m_targetSampleRate);
    initBitrateOnce(m_pStereoStreams[m_referenceStreamIdx]->getBitrate());

    const auto premixRange =
            m_pStereoStreams.front()->frameIndexRange();

    IndexRange globalFrameRange;

    if (m_referenceStreamIdx == 0) {
        globalFrameRange = premixRange;
    } else {
        const SINT scaledLength =
                (premixRange.length() * m_targetSampleRate) / premixInfo.getSampleRate();

        globalFrameRange =
                IndexRange::forward(0, scaledLength);
    }

    initFrameIndexRangeOnce(globalFrameRange);

    return OpenResult::Succeeded;
}

void SoundSourceSTEM::close() {
    for (auto& stream : m_pStereoStreams) {
        stream->close();
    }
}
void SoundSourceSTEM::processWithResampler(size_t streamIdx,
        const WritableSampleFrames& globalSampleFrames,
        CSAMPLE* pBuffer) {
    const int targetSampleRate = m_targetSampleRate;

    const auto& streamInfo =
            m_pStereoStreams[streamIdx]->getSignalInfo();

    const int streamSampleRate =
            streamInfo.getSampleRate();

    // Use integer arithmetic for critical calculations to ensure cross-platform consistency
    const SINT outputFramesNeeded = globalSampleFrames.frameLength();

    const SINT targetStartFrame =
            globalSampleFrames.frameIndexRange().start();

    // Integer-based position calculation (avoids floating-point differences)
    const SINT sourceStartFrame =
            (targetStartFrame * streamSampleRate) /
            targetSampleRate;

    // Calculate input frames needed with integer math
    const SINT inputFramesNeeded =
            ((outputFramesNeeded * streamSampleRate) +
                    targetSampleRate - 1) /
                    targetSampleRate +
            4;

    const SINT inputSamplesNeeded = inputFramesNeeded * 2;

    // Ensure input buffer is large enough
    if (inputSamplesNeeded > m_resampleInputBuffer.size()) {
        m_resampleInputBuffer = SampleBuffer(inputSamplesNeeded);
    }

    // Read input data from the correct position in the stream
    WritableSampleFrames inputFrames(
            IndexRange::forward(sourceStartFrame, inputFramesNeeded),
            SampleBuffer::WritableSlice(
                    m_resampleInputBuffer.data(),
                    inputSamplesNeeded));

    auto readResult =
            m_pStereoStreams[streamIdx]->readSampleFrames(inputFrames);

    // Verify we read enough data
    if (readResult.frameIndexRange().length() < inputFramesNeeded) {
        // This can happen near the end of the file, it's normal
        return;
    }

    // Simple audio detection (avoid complex floating-point checks)
    bool hasAudio = false;

    const SINT checkLimit =
            (inputSamplesNeeded < 1000)
            ? inputSamplesNeeded
            : 1000;

    for (SINT i = 0; i < checkLimit; i += 4) {
        // Check every 4th sample
        if (std::fabs(m_resampleInputBuffer[i]) > 0.001f) {
            hasAudio = true;
            break;
        }
    }

    if (!hasAudio) {
        return; // Silence is normal in STEM files
    }

    // Perform resampling - use a robust approach
    std::size_t stemCount = m_pStereoStreams.size();

    for (SINT i = 0; i < outputFramesNeeded; i++) {
        // Calculate source position using precise integer math
        const int64_t precisePos =
                static_cast<int64_t>(i) *
                streamSampleRate;

        const SINT sourceIndex =
                static_cast<SINT>(
                        precisePos /
                        targetSampleRate);

        const CSAMPLE fraction =
                static_cast<CSAMPLE>(
                        precisePos %
                        targetSampleRate) /
                targetSampleRate;

        // Safe bounds checking
        if (sourceIndex >= 1 &&
                sourceIndex + 2 < inputFramesNeeded) {
            // Use a robust interpolation method
            interpolateAndMix(
                    streamIdx,
                    i,
                    sourceIndex,
                    fraction,
                    pBuffer,
                    stemCount);
        } else if (sourceIndex + 1 < inputFramesNeeded) {
            // Linear fallback
            linearInterpolateAndMix(
                    streamIdx,
                    i,
                    sourceIndex,
                    fraction,
                    pBuffer,
                    stemCount);
        }
    }
}

CSAMPLE SoundSourceSTEM::safeCubicInterpolate(
        CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu) {
    // Handle denormals/NaN using the safe math functions
    //    if (!util_isnormal(mu)) {
    //        mu = 0.0f;
    //    }

    // Robust cubic interpolation that works across compilers
    // const CSAMPLE mu2 = mu * mu;
    const CSAMPLE a0 = y3 - y2 - y0 + y1;
    const CSAMPLE a1 = y0 - y1 - a0;
    const CSAMPLE a2 = y2 - y0;

    // Carefully ordered operations to minimize precision issues
    return ((a0 * mu + a1) * mu + a2) * mu + y1;
}

void SoundSourceSTEM::interpolateAndMix(size_t streamIdx,
        SINT outputIndex,
        SINT sourceIndex,
        CSAMPLE fraction,
        CSAMPLE* pBuffer,
        std::size_t stemCount) {
    const CSAMPLE* in = m_resampleInputBuffer.data();
    const SINT baseIdx = sourceIndex * 2;

    // Left channel - use safe interpolation
    CSAMPLE left = safeCubicInterpolate(
            in[baseIdx - 2],
            in[baseIdx],
            in[baseIdx + 2],
            in[baseIdx + 4],
            fraction);

    // Right channel - use safe interpolation
    CSAMPLE right = safeCubicInterpolate(
            in[baseIdx - 1],
            in[baseIdx + 1],
            in[baseIdx + 3],
            in[baseIdx + 5],
            fraction);

    // Mix into output
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

    // Simple linear interpolation (already safe)
    CSAMPLE left = in[baseIdx] * (1.0f - fraction) + in[baseIdx + 2] * fraction;
    CSAMPLE right = in[baseIdx + 1] * (1.0f - fraction) + in[baseIdx + 3] * fraction;

    // Mix into output
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

// More robust cubic interpolation implementation
CSAMPLE SoundSourceSTEM::robustCubicInterpolate(
        CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu) {
    // This formulation is more numerically stable across different compilers
    CSAMPLE a0 = y3 - y2 - y0 + y1;
    CSAMPLE a1 = y0 - y1 - a0;
    CSAMPLE a2 = y2 - y0;

    // Carefully ordered operations to minimize precision issues
    return (((a0 * mu) + a1) * mu + a2) * mu + y1;
}

void SoundSourceSTEM::showResamplingSummary() {
    qDebug() << "=== STEM RESAMPLING FINAL SUMMARY ===";
    for (auto it = m_streamTotalFramesProcessed.begin();
            it != m_streamTotalFramesProcessed.end();
            ++it) {
        int streamIdx = it.key();
        qint64 frames = it.value();
        qint64 timeNs = m_streamTotalResamplingTime[streamIdx];

        qDebug() << "Stream" << streamIdx << ":" << frames << "frames,"
                 << (timeNs / 1000000.0) << "ms,"
                 << (frames > 0 ? (timeNs / (frames * 1000000.0)) : 0) << "ms per frame";
    }
    qDebug() << "=====================================";
}

void SoundSourceSTEM::processWithoutResampler(size_t streamIdx,
        const WritableSampleFrames& globalSampleFrames,
        CSAMPLE* pBuffer) {
    SINT outputSampleLength = m_pStereoStreams.front()->getSignalInfo().frames2samples(
            globalSampleFrames.frameLength());

    // qDebug() << "Processing stream" << streamIdx << "without resampling";
    // qDebug() << "Frames:" << globalSampleFrames.frameLength();

    // Read directly into temp buffer
    WritableSampleFrames currentStemFrame(
            globalSampleFrames.frameIndexRange(),
            SampleBuffer::WritableSlice(m_buffer.data(), outputSampleLength));

    // auto readResult = m_pStereoStreams[streamIdx]->readSampleFrames(currentStemFrame);
    m_pStereoStreams[streamIdx]->readSampleFrames(currentStemFrame);
    // qDebug() << "Read" << readResult.frameIndexRange().length() << "frames";

    // Check audio data
    // bool hasAudio = false;
    SINT checkLimit = outputSampleLength;
    if (checkLimit > 1000) {
        checkLimit = 1000;
    }
    for (SINT i = 0; i < checkLimit; i++) {
        if (std::abs(m_buffer[i]) > 0.0001f) {
            // hasAudio = true;
            break;
        }
    }
    // qDebug() << "Audio detected:" << (hasAudio ? "YES" : "NO");
    // Mix directly from temp buffer
    std::size_t stemCount = m_pStereoStreams.size();

    for (SINT i = 0; i < outputSampleLength / 2; i++) {
        if (m_requestedChannelCount != mixxx::audio::ChannelCount::stereo()) {
            pBuffer[2 * stemCount * i + 2 * streamIdx] = m_buffer[2 * i];
            pBuffer[2 * stemCount * i + 2 * streamIdx + 1] = m_buffer[2 * i + 1];
        } else {
            pBuffer[2 * i] += m_buffer[2 * i];
            pBuffer[2 * i + 1] += m_buffer[2 * i + 1];
        }
    }
}

void SoundSourceSTEM::testCubicInterpolation() {
    // Test cubic interpolation with known values
    CSAMPLE result = cubicInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 0.5f);
    qDebug() << "Cubic interpolation test result:" << result << "(expected: ~1.5)";

    // Test edge cases
    result = cubicInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 0.0f);
    qDebug() << "Cubic interpolation at mu=0.0:" << result << "(expected: 1.0)";

    result = cubicInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 1.0f);
    qDebug() << "Cubic interpolation at mu=1.0:" << result << "(expected: 2.0)";
}

void SoundSourceSTEM::initializeResamplers(int targetSampleRate) {
// Debug output to check compilation flags
#ifdef __FAST_MATH__
    qWarning() << "STEM: WARNING! Compiled with fast-math - audio quality may suffer!";
#else
    qDebug() << "STEM: Compiled with precise math settings";
#endif

    std::size_t stemCount = m_pStereoStreams.size();
    m_needsResampling.resize(stemCount, false);

    for (std::size_t streamIdx = 0; streamIdx < stemCount; streamIdx++) {
        const auto& streamInfo = m_pStereoStreams[streamIdx]->getSignalInfo();
        const int streamSampleRate = streamInfo.getSampleRate();

        if (streamSampleRate != targetSampleRate) {
            m_needsResampling[streamIdx] = true;
        }
    }
}

ReadableSampleFrames SoundSourceSTEM::readSampleFramesClamped(
        const WritableSampleFrames& globalSampleFrames) {
    VERIFY_OR_DEBUG_ASSERT(m_requestedChannelCount.isValid()) {
        return ReadableSampleFrames();
    }

    VERIFY_OR_DEBUG_ASSERT(
            globalSampleFrames.writableLength() %
                    m_requestedChannelCount ==
            0) {
        return ReadableSampleFrames();
    };

    // --- Step 1: Determine target sample rate ---
    const int targetSampleRate = m_targetSampleRate;

    SINT outputSampleLength =
            getSignalInfo().frames2samples(
                    globalSampleFrames.frameLength());

    // --- Step 2: Initialize resamplers on first call if needed ---
    if (m_needsResampling.empty()) {
        initializeResamplers(targetSampleRate);
    }

    // --- Step 3: Reuse buffers if needed ---
    if (outputSampleLength > m_buffer.size()) {
        m_buffer = SampleBuffer(outputSampleLength);
    }

    // Make resample input buffer large enough for worst-case scenario
    SINT maxInputSamplesNeeded =
            outputSampleLength * 5;

    // 5x for large ratio safety
    if (maxInputSamplesNeeded >
            m_resampleInputBuffer.size()) {
        m_resampleInputBuffer =
                SampleBuffer(maxInputSamplesNeeded);
    }

    ReadableSampleFrames read(
            globalSampleFrames.frameIndexRange(),
            SampleBuffer::ReadableSlice(
                    globalSampleFrames.writableData(),
                    globalSampleFrames.writableLength()));

    std::size_t stemCount = m_pStereoStreams.size();

    CSAMPLE* pBuffer = globalSampleFrames.writableData();

    if (m_requestedChannelCount ==
                    mixxx::audio::ChannelCount::stereo() &&
            stemCount != 1) {
        SampleUtil::clear(
                pBuffer,
                globalSampleFrames.writableLength());
    } else {
        DEBUG_ASSERT(
                outputSampleLength *
                        static_cast<SINT>(stemCount) ==
                globalSampleFrames.writableLength());
    }

    if (stemCount == 1) {
        // Only one stem: just read into global buffer
        m_pStereoStreams[0]->readSampleFrames(globalSampleFrames);

        return read;
    }

    // --- Step 4: Process each stream ---
    for (std::size_t streamIdx = 0;
            streamIdx < stemCount;
            streamIdx++) {
        if (m_needsResampling[streamIdx]) {
            // Stream needs resampling - use cubic interpolation
            processWithResampler(
                    streamIdx,
                    globalSampleFrames,
                    pBuffer);
        } else {
            // Stream doesn't need resampling - read directly
            processWithoutResampler(
                    streamIdx,
                    globalSampleFrames,
                    pBuffer);
        }
    }

    return read;
}

CSAMPLE SoundSourceSTEM::cubicInterpolate(
        CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, double mu) {
    // Cubic interpolation formula
    double mu2 = mu * mu;
    double a0 = y3 - y2 - y0 + y1;
    double a1 = y0 - y1 - a0;
    double a2 = y2 - y0;
    double a3 = y1;

    return static_cast<CSAMPLE>(a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3);
}

SoundSourceSTEM::~SoundSourceSTEM() {
    // clean up
    m_needsResampling.clear();
    // showResamplingSummary();
}

} // namespace mixxx
