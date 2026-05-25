#pragma once

#include "audio/signalinfo.h"

namespace mixxx {

static constexpr audio::ChannelCount kEngineChannelOutputCount =
        audio::ChannelCount::stereo();
static constexpr audio::ChannelCount kMaxEngineChannelInputCount =
        audio::ChannelCount::stem();
// The following constant is always defined as it used for the waveform data
// struct, which must stay consistent, whether the STEM feature is enabled or
// not.
constexpr int kMaxSupportedStems = 5;
#ifdef __STEM__

enum class StemChannel {
    PreMix = 0x1,
    First = 0x2,
    Second = 0x4,
    Third = 0x8,
    Fourth = 0x10,
    None = 0
};

using StemMask = int;

constexpr StemMask kStemMaskAll4 =
        static_cast<int>(StemChannel::First) |
        static_cast<int>(StemChannel::Second) |
        static_cast<int>(StemChannel::Third) |
        static_cast<int>(StemChannel::Fourth);

constexpr StemMask kStemMaskAll5 =
        static_cast<int>(StemChannel::PreMix) |
        kStemMaskAll4;

Q_DECLARE_FLAGS(StemChannelSelection, StemChannel);
#endif

bool includeOriginalMasterStems();
StemChannelSelection getActiveStemSelection();
StemChannelSelection getActiveStemMask();

// StemMask getActiveStemMask();
// StemChannelSelection getActiveStemSelection();

// Contains the information needed to process a buffer of audio
class EngineParameters final {
  public:
    SINT framesPerBuffer() const {
        return m_framesPerBuffer;
    }
    SINT samplesPerBuffer() const {
        return m_outputSignal.frames2samples(framesPerBuffer());
    }

    audio::ChannelCount channelCount() const {
        return m_outputSignal.getChannelCount();
    }

    audio::SampleRate sampleRate() const {
        return m_outputSignal.getSampleRate();
    }

    explicit EngineParameters(
            audio::SampleRate sampleRate,
            SINT framesPerBuffer)
            : m_outputSignal(
                      kEngineChannelOutputCount,
                      sampleRate),
              m_framesPerBuffer(framesPerBuffer) {
        DEBUG_ASSERT(framesPerBuffer > 0);
        DEBUG_ASSERT(sampleRate > 0);
    }

  private:
    const audio::SignalInfo m_outputSignal;
    const SINT m_framesPerBuffer;
};
}

// const mixxx::StemMask stemMask =
//         includeOriginal
//         ? mixxx::kStemMaskAll5
//         : mixxx::kStemMaskAll4;
//
// inline mixxx::StemMask getStemMaskFromConfig(bool includeOriginal) {
//     return includeOriginal
//             ? mixxx::kStemMaskAll5
//             : mixxx::kStemMaskAll4;
// }
//
// const mixxx::StemChannelSelection selection =
//         includeOriginal
//         ?
//         mixxx::StemChannelSelection(mixxx::StemChannel::PreMix) |
//         mixxx::StemChannelSelection(mixxx::StemChannel::First) |
//         mixxx::StemChannelSelection(mixxx::StemChannel::Second) |
//         mixxx::StemChannelSelection(mixxx::StemChannel::Third) |
//         mixxx::StemChannelSelection(mixxx::StemChannel::Fourth)
//         :
//         mixxx::StemChannelSelection(mixxx::StemChannel::First) |
//         mixxx::StemChannelSelection(mixxx::StemChannel::Second) |
//         mixxx::StemChannelSelection(mixxx::StemChannel::Third) |
//         mixxx::StemChannelSelection(mixxx::StemChannel::Fourth);

// #pragma once
//
// #include "audio/signalinfo.h"
// #include "control/controlproxy.h"
// #include "control/pollingcontrolproxy.h"
//
// namespace mixxx {
// static constexpr audio::ChannelCount kEngineChannelOutputCount =
//         audio::ChannelCount::stereo();
// static constexpr audio::ChannelCount kMaxEngineChannelInputCount =
//         audio::ChannelCount::stem();
//// The following constant is always defined as it used for the waveform data
//// struct, which must stay consistent, whether the STEM feature is enabled or
//// not.
// constexpr int kMaxSupportedStems = 5;
// #ifdef __STEM__
//  enum class StemChannel {
//      First = 0x1,
//      Second = 0x2,
//      Third = 0x4,
//      Fourth = 0x8,
//
//      None = 0,
//      All = First | Second | Third | Fourth
//  };
//
//
// Q_DECLARE_FLAGS(StemChannelSelection, StemChannel);
// #endif
//
//// Contains the information needed to process a buffer of audio
// class EngineParameters final {
//   public:
//     SINT framesPerBuffer() const {
//         return m_framesPerBuffer;
//     }
//     SINT samplesPerBuffer() const {
//         return m_outputSignal.frames2samples(framesPerBuffer());
//     }
//
//     audio::ChannelCount channelCount() const {
//         return m_outputSignal.getChannelCount();
//     }
//
//     audio::SampleRate sampleRate() const {
//         return m_outputSignal.getSampleRate();
//     }
//
//     explicit EngineParameters(
//             audio::SampleRate sampleRate,
//             SINT framesPerBuffer)
//             : m_outputSignal(
//                       kEngineChannelOutputCount,
//                       sampleRate),
//               m_framesPerBuffer(framesPerBuffer) {
//         DEBUG_ASSERT(framesPerBuffer > 0);
//         DEBUG_ASSERT(sampleRate > 0);
//     }
//
//   private:
//     const audio::SignalInfo m_outputSignal;
//     const SINT m_framesPerBuffer;
// };
// } // namespace mixxx
