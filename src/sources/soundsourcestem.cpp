#include "sources/soundsourcestem.h"

extern "C" {

#include <libavutil/avutil.h>
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
#include <libavutil/channel_layout.h>
#endif

} // extern "C"

#include <memory>

#include "sources/soundsourceffmpeg.h"
#include "util/assert.h"
#include "util/logger.h"
#include "util/sample.h"

#if !defined(VERBOSE_DEBUG_LOG)
#define VERBOSE_DEBUG_LOG false
#endif

namespace mixxx {

namespace {

// Stream 0 is the premix, streams 1..4 are the stems. All 5 are streamed
// on stem decks; EngineDeck switches between premix and stems with mutes.
constexpr std::size_t kNumStreams = 5;
constexpr std::size_t kPremixStreamIdx = 0;
constexpr std::size_t kFirstStemStreamIdx = 1;

const Logger kLogger("SoundSourceSTEM");

// Local RAII for AVFormatContext; SoundSourceFFmpeg's wrapper is private.
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
    // Ensure that the source isn't yet opened
    VERIFY_OR_DEBUG_ASSERT(!m_requestedChannelCount.isValid()) {
        return OpenResult::Failed;
    }
    // Open input. RAII handles cleanup on every return path.
    AVFormatContextPtr pavInputFormatContextGuard(
            SoundSourceFFmpeg::openInputFile(getLocalFileName()));
    AVFormatContext* pavInputFormatContext = pavInputFormatContextGuard.get();
    if (pavInputFormatContext == nullptr) {
        kLogger.warning() << "Failed to open input file" << getLocalFileName();
        return OpenResult::Failed;
    }

    const int avformat_find_stream_info_result =
            avformat_find_stream_info(pavInputFormatContext, nullptr);
    if (avformat_find_stream_info_result != 0) {
        DEBUG_ASSERT(avformat_find_stream_info_result < 0);
        kLogger.warning().noquote()
                << "avformat_find_stream_info() failed:"
                << SoundSourceFFmpeg::formatErrorString(avformat_find_stream_info_result);
        return OpenResult::Failed;
    }

    // Collect the audio streams: premix first, then the 4 stems.
    std::vector<AVStream*> audioStreams;
    for (unsigned int streamIdx = 0; streamIdx < pavInputFormatContext->nb_streams; ++streamIdx) {
        AVStream* pavStream = pavInputFormatContext->streams[streamIdx];
        if (pavStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1
        if (pavStream->codecpar->ch_layout.nb_channels !=
                mixxx::audio::ChannelCount::stereo()) {
#else
        if (pavStream->codecpar->channels !=
                mixxx::audio::ChannelCount::stereo()) {
#endif
            kLogger.warning().noquote()
                    << "stream at position" << streamIdx << "is not in stereo";
            return OpenResult::Failed;
        }
        audioStreams.push_back(pavStream);
    }
    if (audioStreams.size() != kNumStreams) {
        kLogger.warning().noquote()
                << "expected to find" << kNumStreams
                << "audio streams but found" << audioStreams.size();
        return OpenResult::Failed;
    }

    // Common sample rate: either the premix rate (stems are upsampled) or
    // the stem rate (premix is downsampled). The premix in the file is
    // never modified, FFmpeg resamples while decoding.
    const std::size_t referenceStreamIdx =
            params.getUpSampleStems() ? kPremixStreamIdx : kFirstStemStreamIdx;
    AVStream* pavReferenceStream = audioStreams[referenceStreamIdx];
    const auto targetSampleRate =
            audio::SampleRate(pavReferenceStream->codecpar->sample_rate);
    if (!targetSampleRate.isValid()) {
        kLogger.warning() << "Invalid sample rate" << targetSampleRate
                          << "of reference stream" << referenceStreamIdx;
        return OpenResult::Failed;
    }

    // Stem mask: bit n selects stream n (PreMix = 0x1, First = 0x2, ...)
    const uint selectedStemMask = params.stemMask();
    VERIFY_OR_DEBUG_ASSERT((selectedStemMask &
                                   ~static_cast<uint>(mixxx::StemChannel::All)) == 0) {
        kLogger.warning().noquote()
                << "Invalid selected stem mask" << selectedStemMask;
        return OpenResult::Failed;
    }
    const bool stereoRequested =
            params.getSignalInfo().getChannelCount() ==
                    mixxx::audio::ChannelCount::stereo() ||
            selectedStemMask;

    OpenParams stemParam = params;
    stemParam.setChannelCount(mixxx::audio::ChannelCount::stereo());

    for (std::size_t i = 0; i < kNumStreams; ++i) {
        if (stereoRequested) {
            // Samplers / preview decks have no stem mutes, so only open
            // what should be heard: the selected streams, or the premix.
            const bool selected = selectedStemMask
                    ? (selectedStemMask & (1u << i)) != 0
                    : i == kPremixStreamIdx;
            if (!selected) {
                continue;
            }
        }
        const int streamIdx = audioStreams[i]->index;
        m_pStereoStreams.emplace_back(
                std::make_unique<SoundSourceFFmpeg>(
                        getUrl(),
                        streamIdx,
                        targetSampleRate));
        if (m_pStereoStreams.back()->open(OpenMode::Strict, stemParam) !=
                OpenResult::Succeeded) {
            kLogger.warning() << "Failed to open stream" << streamIdx;
            close();
            return OpenResult::Failed;
        }
        DEBUG_ASSERT(m_pStereoStreams.back()->getSignalInfo().getSampleRate() ==
                targetSampleRate);
    }

    VERIFY_OR_DEBUG_ASSERT(!m_pStereoStreams.empty()) {
        kLogger.warning().noquote() << "no stem track were selected";
        close();
        return OpenResult::Failed;
    }

    if (stereoRequested) {
        m_requestedChannelCount = mixxx::audio::ChannelCount::stereo();
    } else {
        m_requestedChannelCount = mixxx::audio::ChannelCount(
                mixxx::audio::ChannelCount::stereo() *
                static_cast<int>(m_pStereoStreams.size()));
    }

    // Length of the reference stream at the common rate. The other streams
    // may end a few frames earlier or later (encoder padding); missing
    // frames are padded with silence while reading.
    const auto referenceFrameRange = SoundSourceFFmpeg::getStreamFrameIndexRange(
            *pavReferenceStream, targetSampleRate);

    if (!initChannelCountOnce(m_requestedChannelCount) ||
            !initSampleRateOnce(targetSampleRate) ||
            !initFrameIndexRangeOnce(IndexRange::forward(0, referenceFrameRange.length()))) {
        close();
        return OpenResult::Failed;
    }
    const auto bitrate = m_pStereoStreams.front()->getBitrate();
    if (bitrate.isValid()) {
        initBitrateOnce(bitrate);
    }

    kLogger.debug() << "STEM opened:"
                    << "targetRate =" << targetSampleRate
                    << "channels =" << m_requestedChannelCount
                    << "frames =" << frameIndexRange().length()
                    << "streams =" << m_pStereoStreams.size();

    return OpenResult::Succeeded;
}

void SoundSourceSTEM::close() {
    for (auto& stream : m_pStereoStreams) {
        stream->close();
    }
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

    const SINT frameCount = globalSampleFrames.frameLength();
    const SINT stereoSampleCount =
            mixxx::audio::SignalInfo(mixxx::audio::ChannelCount::stereo(),
                    getSignalInfo().getSampleRate())
                    .frames2samples(frameCount);
    const std::size_t streamCount = m_pStereoStreams.size();
    CSAMPLE* pOutput = globalSampleFrames.writableData();

    const ReadableSampleFrames read(
            globalSampleFrames.frameIndexRange(),
            SampleBuffer::ReadableSlice(
                    pOutput,
                    globalSampleFrames.writableLength()));

    const bool stereoOutput =
            m_requestedChannelCount == mixxx::audio::ChannelCount::stereo();

    if (stereoOutput && streamCount == 1) {
        // Single stream (premix or one selected stem): decode directly
        const auto stemRead = m_pStereoStreams[0]->readSampleFrames(globalSampleFrames);
        const SINT framesRead = stemRead.frameIndexRange().length();
        if (framesRead < frameCount) {
            SampleUtil::clear(pOutput + framesRead * 2, (frameCount - framesRead) * 2);
        }
        return read;
    }

    // The same buffer is reused between requests to prevent reallocation
    if (stereoSampleCount > m_buffer.size()) {
        m_buffer = SampleBuffer(stereoSampleCount);
    }
    if (stereoOutput) {
        SampleUtil::clear(pOutput, globalSampleFrames.writableLength());
    }

    for (std::size_t streamIdx = 0; streamIdx < streamCount; ++streamIdx) {
        WritableSampleFrames stemFrames(
                globalSampleFrames.frameIndexRange(),
                SampleBuffer::WritableSlice(m_buffer.data(), stereoSampleCount));
        const auto stemRead = m_pStereoStreams[streamIdx]->readSampleFrames(stemFrames);
        const SINT framesRead = stemRead.frameIndexRange().length();
        if (framesRead < frameCount) {
            // This stream ends before the reference stream
            SampleUtil::clear(m_buffer.data() + framesRead * 2,
                    (frameCount - framesRead) * 2);
        }

        if (stereoOutput) {
            // Mix the selected streams together
            SampleUtil::add(pOutput, m_buffer.data(), stereoSampleCount);
        } else {
            // Interleave: 1L1R2L2R3L3R4L4R5L5R, 1L1R2L2R...
            for (SINT i = 0; i < frameCount; ++i) {
                pOutput[2 * streamCount * i + 2 * streamIdx] = m_buffer[2 * i];
                pOutput[2 * streamCount * i + 2 * streamIdx + 1] = m_buffer[2 * i + 1];
            }
        }
    }
    return read;
}

} // namespace mixxx
