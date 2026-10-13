#pragma once

#include "sources/soundsourceffmpeg.h"
#include "sources/soundsourceprovider.h"
#include "util/samplebuffer.h"

namespace mixxx {

/// @brief Handle a stem file, composed of multiple audio channels.
/// Can open in stereo or in stem mode (5 x stereo: premix + 4 stems).
class SoundSourceSTEM : public SoundSource {
  public:
    explicit SoundSourceSTEM(const QUrl& url);
    ~SoundSourceSTEM() override;

    void close() override;

    // Non-virtual in base class -> do NOT mark override
    audio::StreamInfo getStreamInfo() const;
    IndexRange frameIndexRange() const;

  private:
    std::vector<std::unique_ptr<SoundSourceFFmpeg>> m_pStereoStreams;
    SampleBuffer m_buffer;

    mixxx::audio::ChannelCount m_requestedChannelCount;
    std::vector<bool> m_needsResampling;
    SampleBuffer m_resampleInputBuffer;
    SampleBuffer m_resampleOutputBuffer;

    bool m_premixIncluded = true;
    bool m_upSampleStems = true;
    audio::SampleRate m_targetSampleRate;

    SINT m_referenceStreamIdx = 0;
    IndexRange m_globalFrameRange;
    bool m_streamInfoInitialized = false;

    int getStemSampleRate() const;
    int getReferenceSampleRate() const;

    void processPremixDownsampler(const WritableSampleFrames& globalSampleFrames,
            const CSAMPLE* pBuffer);
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

  protected:
    CSAMPLE cubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, double mu);
    CSAMPLE robustCubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu);
    CSAMPLE safeCubicInterpolate(CSAMPLE y0, CSAMPLE y1, CSAMPLE y2, CSAMPLE y3, CSAMPLE mu);

    // void initializeResamplers(int refSampleRate);
    void initializeResamplers(mixxx::audio::SampleRate targetSampleRate);
    void processWithoutResampler(size_t streamIdx,
            const WritableSampleFrames& globalSampleFrames,
            CSAMPLE* pBuffer);
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

    OpenResult tryOpen(
            OpenMode mode,
            const OpenParams& params) override;

    ReadableSampleFrames readSampleFramesClamped(
            const WritableSampleFrames& sampleFrames) override;
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
// #include "sources/soundsourceprovider.h"
// #include "util/samplebuffer.h"
//
// namespace mixxx {
//
// class SoundSourceFFmpeg;
//
///// @brief Handle a stem file, composed of multiple audio channel. Can open in
///// stereo or in stem (4 x stereo). Use OpenParams to request a maximum number of channels.
///// This allows decks which must not use STEM for performance or usability reason to use the
///// same soundsource.
// class SoundSourceSTEM : public SoundSource {
//   public:
//     explicit SoundSourceSTEM(const QUrl& url);
//     ~SoundSourceSTEM() override;
//
//     void close() override;
//
//   private:
//     // Contains each stem source, or the main mix if opened in stereo mode
//     std::vector<std::unique_ptr<SoundSourceFFmpeg>> m_pStereoStreams;
//     SampleBuffer m_buffer;
//
//     mixxx::audio::ChannelCount m_requestedChannelCount;
//
//   protected:
//     OpenResult tryOpen(
//             OpenMode mode,
//             const OpenParams& params) override;
//
//     ReadableSampleFrames readSampleFramesClamped(
//             const WritableSampleFrames& sampleFrames) override;
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
