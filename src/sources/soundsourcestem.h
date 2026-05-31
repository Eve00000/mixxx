#pragma once

#include "sources/soundsourceffmpeg.h"
#include "sources/soundsourceprovider.h"
#include "util/samplebuffer.h"

namespace mixxx {

/// @brief Handle a single stem embedded in a stem file
class SoundSourceSingleSTEM : public SoundSourceFFmpeg {
  public:
    // streamIdx is the FFmpeg stream id, which may different than stemIdx + 1
    // because STEM may contain other non audio stream
    explicit SoundSourceSingleSTEM(const QUrl& url, unsigned int streamIdx);

  protected:
    OpenResult tryOpen(
            OpenMode mode,
            const OpenParams& params) override;

  private:
    unsigned int m_streamIdx;
};

/// @brief Handle a stem file, composed of multiple audio channel. Can open in
/// stereo or in stem (4 x stereo). Use OpenParams to request a maximum number of channels.
/// This allows decks which must not use STEM for performance or usability reason to use the
/// same soundsource.
class SoundSourceSTEM : public SoundSource {
  public:
    explicit SoundSourceSTEM(const QUrl& url);
    ~SoundSourceSTEM();

    void close() override;

    void updateConversionMethod();

    // NOTE: These are NOT virtual in the base class, so remove 'override'
    // audio::StreamInfo getStreamInfo() const {
    //    if (m_targetSampleRate.isValid() && m_requestedChannelCount.isValid()) {
    //        audio::StreamInfo info;
    //        info.setSignalInfo(audio::SignalInfo(
    //                m_requestedChannelCount,
    //                m_targetSampleRate));
    //        // Use the actual duration based on reference stream
    //        if (m_referenceStreamIdx >= 0 &&
    //                m_referenceStreamIdx < static_cast<SINT>(m_pStereoStreams.size())) {
    //            const auto& refInfo = m_pStereoStreams[m_referenceStreamIdx]->getStreamInfo();
    //            // Convert Duration to double (seconds)
    //            const double durationSeconds = refInfo.getDuration().toDoubleSeconds();
    //            info.setDuration(mixxx::Duration::fromSeconds(durationSeconds));
    //        }
    //        return info;
    //    }
    //    return SoundSource::getStreamInfo();
    //}
    audio::StreamInfo getStreamInfo() const {
        if (m_targetSampleRate.isValid() && m_targetSampleRate > 0 &&
                m_requestedChannelCount.isValid()) {
            audio::StreamInfo info;
            info.setSignalInfo(audio::SignalInfo(
                    m_requestedChannelCount,
                    m_targetSampleRate));
            // Use the actual duration based on reference stream
            if (m_referenceStreamIdx >= 0 &&
                    m_referenceStreamIdx < static_cast<SINT>(m_pStereoStreams.size())) {
                const auto& refInfo = m_pStereoStreams[m_referenceStreamIdx]->getStreamInfo();
                info.setDuration(refInfo.getDuration());
            } else {
                // Fallback duration
                info.setDuration(mixxx::Duration::fromSeconds(0));
            }
            return info;
        }
        // Fallback to base class
        return SoundSource::getStreamInfo();
    }

    IndexRange frameIndexRange() const {
        if (m_globalFrameRange.start() >= 0 && m_globalFrameRange.length() > 0) {
            return m_globalFrameRange;
        }
        return SoundSource::frameIndexRange();
    }

  private:
    // Contains each stem source, or the main mix if opened in stereo mode
    std::vector<std::unique_ptr<SoundSourceSingleSTEM>> m_pStereoStreams;
    SampleBuffer m_buffer;

    mixxx::audio::ChannelCount m_requestedChannelCount;
    /// // working
    std::vector<bool> m_needsResampling;
    SampleBuffer m_resampleInputBuffer;
    SampleBuffer m_resampleOutputBuffer;

    QMap<int, qint64> m_streamTotalFramesProcessed;
    QMap<int, qint64> m_streamTotalResamplingTime;

    bool m_premixIncluded;
    bool m_upSampleStems;
    audio::SampleRate m_targetSampleRate;

    SINT m_referenceStreamIdx;

    int getStemSampleRate() const;
    int getReferenceSampleRate() const;
    void processPremixDownsampler(const WritableSampleFrames& globalSampleFrames,
            CSAMPLE* pBuffer);
    bool validateBufferAccess(SINT sourceIndex, SINT inputFramesNeeded) const;
    void safeBufferCopy(const CSAMPLE* source,
            CSAMPLE* dest,
            SINT samples,
            SINT sourceOffset,
            SINT destOffset);
    void processWithSimpleInterpolation(size_t streamIdx,
            const WritableSampleFrames& globalSampleFrames,
            CSAMPLE* pBuffer,
            SINT availableFrames,
            int streamSampleRate,
            int targetSampleRate);
    void linearInterpolateAndMixSafe(size_t streamIdx,
            SINT outputIndex,
            SINT sourceIndex,
            CSAMPLE fraction,
            CSAMPLE* pBuffer,
            std::size_t stemCount,
            SINT maxFrames);
    void interpolateAndMixSafe(size_t streamIdx,
            SINT outputIndex,
            SINT sourceIndex,
            CSAMPLE fraction,
            CSAMPLE* pBuffer,
            std::size_t stemCount,
            SINT maxFrames);

    static constexpr SINT kMaxBufferSize = 192000; // Max 2 seconds at 96kHz
    static constexpr SINT kMinBufferSize = 2048;

    IndexRange m_globalFrameRange;
    bool m_streamInfoInitialized;

  protected:
    // Cubic interpolation function
    CSAMPLE cubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, double mu);

    void initializeResamplers(int refSampleRate);
    void processWithoutResampler(size_t streamIdx,
            const WritableSampleFrames& globalSampleFrames,
            CSAMPLE* pBuffer);
    void testCubicInterpolation();
    void showResamplingSummary();

    OpenResult tryOpen(
            OpenMode mode,
            const OpenParams& params) override;

    ReadableSampleFrames readSampleFramesClamped(
            const WritableSampleFrames& sampleFrames) override;

    void processWithResampler(size_t streamIdx,
            const WritableSampleFrames& globalSampleFrames,
            CSAMPLE* pBuffer);
    void interpolateAndMix(size_t streamIdx,
            SINT outputIndex,
            SINT sourceIndex,
            CSAMPLE fraction,
            CSAMPLE* pBuffer,
            std::size_t stemCount);
    void linearInterpolateAndMix(size_t streamIdx,
            SINT outputIndex,
            SINT sourceIndex,
            CSAMPLE fraction,
            CSAMPLE* pBuffer,
            std::size_t stemCount);
    void mixToOutput(size_t streamIdx,
            SINT outputIndex,
            CSAMPLE left,
            CSAMPLE right,
            CSAMPLE* pBuffer,
            std::size_t stemCount);
    CSAMPLE robustCubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu);
    CSAMPLE safeCubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu);
};

class SoundSourceProviderSTEM : public SoundSourceProvider {
  public:
    static const QString kDisplayName;

    ~SoundSourceProviderSTEM() override = default;

    QString getDisplayName() const override {
        return kDisplayName + QChar(' ') + getVersionString();
    }

    QStringList getSupportedFileTypes() const override;

    SoundSourceProviderPriority getPriorityHint(
            const QString& supportedFileType) const override;

    SoundSourcePointer newSoundSource(const QUrl& url) override {
        return newSoundSourceFromUrl<SoundSourceSTEM>(url);
    }

    QString getVersionString() const;
};

} // namespace mixxx

// #pragma once
//
// #include "sources/soundsourceffmpeg.h"
// #include "sources/soundsourceprovider.h"
// #include "util/samplebuffer.h"
//
// namespace mixxx {
//
///// @brief Handle a single stem embedded in a stem file
// class SoundSourceSingleSTEM : public SoundSourceFFmpeg {
//   public:
//     // streamIdx is the FFmpeg stream id, which may different than stemIdx +
//     1
//     // because STEM may contain other non audio stream
//     explicit SoundSourceSingleSTEM(const QUrl& url, unsigned int streamIdx);
//
//   protected:
//     OpenResult tryOpen(
//             OpenMode mode,
//             const OpenParams& params) override;
//
//   private:
//     unsigned int m_streamIdx;
// };
//
///// @brief Handle a stem file, composed of multiple audio channel. Can open in
///// stereo or in stem (4 x stereo). Use OpenParams to request a maximum number
/// of channels.
///// This allows decks which must not use STEM for performance or usability
/// reason to use the
///// same soundsource.
// class SoundSourceSTEM : public SoundSource {
//   public:
//     explicit SoundSourceSTEM(const QUrl& url);
//     ~SoundSourceSTEM();
//
//     void close() override;
//
//     void updateConversionMethod();
//
//     audio::StreamInfo getStreamInfo() const override {
//         if (m_targetSampleRate.isValid() &&
//         m_requestedChannelCount.isValid()) {
//             audio::StreamInfo info;
//             info.setSignalInfo(audio::SignalInfo(
//                     m_requestedChannelCount,
//                     m_targetSampleRate));
//             // Use the actual duration based on reference stream
//             if (m_referenceStreamIdx >= 0 &&
//                     m_referenceStreamIdx <
//                     static_cast<SINT>(m_pStereoStreams.size())) {
//                 const auto& refInfo =
//                 m_pStereoStreams[m_referenceStreamIdx]->getStreamInfo();
//                 const double durationSeconds =
//                 static_cast<double>(refInfo.getFrameLength()) /
//                         refInfo.getSignalInfo().getSampleRate();
//                 info.setDuration(mixxx::Duration::fromSeconds(durationSeconds));
//             }
//             return info;
//         }
//         return AudioSource::getStreamInfo();
//     }
//
//     IndexRange frameIndexRange() const override {
//         if (m_globalFrameRange.isValid()) {
//             return m_globalFrameRange;
//         }
//         return AudioSource::frameIndexRange();
//     }
//
//   private:
//     // Contains each stem source, or the main mix if opened in stereo mode
//     std::vector<std::unique_ptr<SoundSourceSingleSTEM>> m_pStereoStreams;
//     SampleBuffer m_buffer;
//
//     mixxx::audio::ChannelCount m_requestedChannelCount;
//     /// // working
//     std::vector<bool> m_needsResampling;
//     SampleBuffer m_resampleInputBuffer;
//     SampleBuffer m_resampleOutputBuffer;
//
//     QMap<int, qint64> m_streamTotalFramesProcessed;
//     QMap<int, qint64> m_streamTotalResamplingTime;
//
//     bool m_premixIncluded;
//     bool m_upSampleStems;
//     int m_referenceSampleRate;
//     audio::SampleRate m_targetSampleRate;
//
//     std::size_t m_referenceStreamIdx{0};
//
//     int getStemSampleRate() const;
//     int getReferenceSampleRate() const;
//     void processPremixDownsampler(const WritableSampleFrames&
//     globalSampleFrames,
//             CSAMPLE* pBuffer);
//     bool validateBufferAccess(SINT sourceIndex, SINT inputFramesNeeded)
//     const; void safeBufferCopy(const CSAMPLE* source, CSAMPLE* dest, SINT
//     samples, SINT sourceOffset, SINT destOffset); void
//     processWithSimpleInterpolation(size_t streamIdx,
//             const WritableSampleFrames& globalSampleFrames,
//             CSAMPLE* pBuffer,
//             SINT availableFrames,
//             int streamSampleRate,
//             int targetSampleRate);
//     void linearInterpolateAndMixSafe(size_t streamIdx,
//             SINT outputIndex,
//             SINT sourceIndex,
//             CSAMPLE fraction,
//             CSAMPLE* pBuffer,
//             std::size_t stemCount,
//             SINT maxFrames);
//     void interpolateAndMixSafe(size_t streamIdx,
//             SINT outputIndex,
//             SINT sourceIndex,
//             CSAMPLE fraction,
//             CSAMPLE* pBuffer,
//             std::size_t stemCount,
//             SINT maxFrames);
//
//
//
//     static constexpr SINT kMaxBufferSize = 192000; // Max 2 seconds at 96kHz
//     static constexpr SINT kMinBufferSize = 2048;
//
//     IndexRange m_globalFrameRange;
//     SINT m_referenceStreamIdx;
//     bool m_streamInfoInitialized;
//
//   protected:
//     // Cubic interpolation function
//     CSAMPLE cubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3,
//     double mu);
//
//     void initializeResamplers(int refSampleRate);
//     void processWithoutResampler(size_t streamIdx,
//             const WritableSampleFrames& globalSampleFrames,
//             CSAMPLE* pBuffer);
//     void testCubicInterpolation();
//     void showResamplingSummary();
//
//     OpenResult tryOpen(
//             OpenMode mode,
//             const OpenParams& params) override;
//
//     ReadableSampleFrames readSampleFramesClamped(
//             const WritableSampleFrames& sampleFrames) override;
//
//     void processWithResampler(size_t streamIdx,
//             const WritableSampleFrames& globalSampleFrames,
//             CSAMPLE* pBuffer);
//     void interpolateAndMix(size_t streamIdx,
//             SINT outputIndex,
//             SINT sourceIndex,
//             CSAMPLE fraction,
//             CSAMPLE* pBuffer,
//             std::size_t stemCount);
//     void linearInterpolateAndMix(size_t streamIdx,
//             SINT outputIndex,
//             SINT sourceIndex,
//             CSAMPLE fraction,
//             CSAMPLE* pBuffer,
//             std::size_t stemCount);
//     void mixToOutput(size_t streamIdx,
//             SINT outputIndex,
//             CSAMPLE left,
//             CSAMPLE right,
//             CSAMPLE* pBuffer,
//             std::size_t stemCount);
//     CSAMPLE robustCubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2,
//     CSAMPLE y3, CSAMPLE mu); CSAMPLE safeCubicInterpolate(CSAMPLE y0, CSAMPLE
//     y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu);
// };
//
// class SoundSourceProviderSTEM : public SoundSourceProvider {
//   public:
//     static const QString kDisplayName;
//
//     ~SoundSourceProviderSTEM() override = default;
//
//     QString getDisplayName() const override {
//         return kDisplayName + QChar(' ') + getVersionString();
//     }
//
//     QStringList getSupportedFileTypes() const override;
//
//     SoundSourceProviderPriority getPriorityHint(
//             const QString& supportedFileType) const override;
//
//     SoundSourcePointer newSoundSource(const QUrl& url) override {
//         return newSoundSourceFromUrl<SoundSourceSTEM>(url);
//     }
//
//     QString getVersionString() const;
// };
//
// } // namespace mixxx
