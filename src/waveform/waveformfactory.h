#pragma once

#include "library/dao/analysisdao.h"

class Waveform;

#define WAVEFORM_2_VERSION "Waveform-2.0"
#define WAVEFORMSUMMARY_2_VERSION "WaveformSummary-2.0"
#define WAVEFORM_2_DESCRIPTION "Waveform 2.0"
#define WAVEFORMSUMMARY_2_DESCRIPTION "WaveformSummary 2.0"

// Used in Mixxx 1.11 beta
#define WAVEFORM_3_VERSION "Waveform-3.0"
#define WAVEFORMSUMMARY_3_VERSION "WaveformSummary-3.0"
#define WAVEFORM_3_DESCRIPTION "Waveform 3.0"
#define WAVEFORMSUMMARY_3_DESCRIPTION "WaveformSummary 3.0"

// Used from Mixxx 1.11 pre
#define WAVEFORM_4_VERSION "Waveform-4.0"
#define WAVEFORMSUMMARY_4_VERSION "WaveformSummary-4.0"
#define WAVEFORM_4_DESCRIPTION "Waveform 4.0"
#define WAVEFORMSUMMARY_4_DESCRIPTION "WaveformSummary 4.0"

// Used from Mixxx 1.12 alpha
#define WAVEFORM_5_VERSION "Waveform-5.0"
#define WAVEFORMSUMMARY_5_VERSION "WaveformSummary-5.0"
#define WAVEFORM_5_DESCRIPTION "Waveform 5.0"
#define WAVEFORMSUMMARY_5_DESCRIPTION "WaveformSummary 5.0"

#ifdef __STEM__
// Used from Mixxx 2.6-pre-alpha with stem data (6.0) and without
// analyzer/analyzerwaveform.h:scaleSignal (6.1). Both versions contain
// phantom stem data for tracks whose stem streams end before the reference
// stream, so they are removed and re-analyzed.
#define WAVEFORM_6_0_VERSION "Waveform-6.0" // Superseded by 6.1
#define WAVEFORMSUMMARY_6_0_VERSION "WaveformSummary-6.0"
#define WAVEFORM_6_0_DESCRIPTION "Waveform 6.0"
#define WAVEFORMSUMMARY_6_0_DESCRIPTION "WaveformSummary 6.0"

#define WAVEFORM_6_1_VERSION "Waveform-6.1"
#define WAVEFORMSUMMARY_6_1_VERSION "WaveformSummary-6.1"
#define WAVEFORM_6_1_DESCRIPTION "Waveform 6.1"
#define WAVEFORMSUMMARY_6_1_DESCRIPTION "WaveformSummary 6.1"

// Stem waveform layout fix: premix channel is no longer counted as a stem,
// and stem channels are read from the correct interleaved offset.
#define WAVEFORM_7_VERSION "Waveform-7.0"
#define WAVEFORMSUMMARY_7_VERSION "WaveformSummary-7.0"
#define WAVEFORM_7_DESCRIPTION "Waveform 7.0"
#define WAVEFORMSUMMARY_7_DESCRIPTION "WaveformSummary 7.0"

#define WAVEFORM_CURRENT_VERSION WAVEFORM_7_VERSION
#define WAVEFORM_CURRENT_DESCRIPTION WAVEFORM_7_DESCRIPTION
#define WAVEFORMSUMMARY_CURRENT_VERSION WAVEFORMSUMMARY_7_VERSION
#define WAVEFORMSUMMARY_CURRENT_DESCRIPTION WAVEFORMSUMMARY_7_DESCRIPTION
#else
#define WAVEFORM_CURRENT_VERSION WAVEFORM_5_VERSION
#define WAVEFORM_CURRENT_DESCRIPTION WAVEFORM_5_DESCRIPTION
#define WAVEFORMSUMMARY_CURRENT_VERSION WAVEFORMSUMMARY_5_VERSION
#define WAVEFORMSUMMARY_CURRENT_DESCRIPTION WAVEFORMSUMMARY_5_DESCRIPTION
#endif

class WaveformFactory {
  public:
    enum VersionClass {
        VC_USE,
        VC_KEEP,
        VC_REMOVE
    };

    static Waveform* loadWaveformFromAnalysis(
            const AnalysisDao::AnalysisInfo& analysis);
    static VersionClass waveformVersionToVersionClass(const QString& version);
    static VersionClass waveformSummaryVersionToVersionClass(const QString& version);
    static QString currentWaveformVersion();
    static QString currentWaveformDescription();
    static QString currentWaveformSummaryVersion();
    static QString currentWaveformSummaryDescription();
};

// #pragma once
//
// #include "library/dao/analysisdao.h"
//
// class Waveform;
//
// #define WAVEFORM_2_VERSION "Waveform-2.0"
// #define WAVEFORMSUMMARY_2_VERSION "WaveformSummary-2.0"
// #define WAVEFORM_2_DESCRIPTION "Waveform 2.0"
// #define WAVEFORMSUMMARY_2_DESCRIPTION "WaveformSummary 2.0"
//
//// Used in Mixxx 1.11 beta
// #define WAVEFORM_3_VERSION "Waveform-3.0"
// #define WAVEFORMSUMMARY_3_VERSION "WaveformSummary-3.0"
// #define WAVEFORM_3_DESCRIPTION "Waveform 3.0"
// #define WAVEFORMSUMMARY_3_DESCRIPTION "WaveformSummary 3.0"
//
//// Used from Mixxx 1.11 pre
// #define WAVEFORM_4_VERSION "Waveform-4.0"
// #define WAVEFORMSUMMARY_4_VERSION "WaveformSummary-4.0"
// #define WAVEFORM_4_DESCRIPTION "Waveform 4.0"
// #define WAVEFORMSUMMARY_4_DESCRIPTION "WaveformSummary 4.0"
//
//// Used from Mixxx 1.12 alpha
// #define WAVEFORM_5_VERSION "Waveform-5.0"
// #define WAVEFORMSUMMARY_5_VERSION "WaveformSummary-5.0"
// #define WAVEFORM_5_DESCRIPTION "Waveform 5.0"
// #define WAVEFORMSUMMARY_5_DESCRIPTION "WaveformSummary 5.0"
//
// #ifdef __STEM__
//// Used from Mixxx 2.6-pre-alpha with Stem data (6.0) and without
//// analyzer/analyzerwaveform.h:scaleSignal (6.1)
// #define WAVEFORM_6_0_VERSION "Waveform-6.0" // Superseded by 6.1
// #define WAVEFORM_6_VERSION "Waveform-6.1"
// #define WAVEFORMSUMMARY_6_VERSION "WaveformSummary-6.1"
// #define WAVEFORM_6_DESCRIPTION "Waveform 6.1"
// #define WAVEFORMSUMMARY_6_DESCRIPTION "WaveformSummary 6.1"
//
// #define WAVEFORM_CURRENT_VERSION WAVEFORM_6_VERSION
// #define WAVEFORM_CURRENT_DESCRIPTION WAVEFORM_6_DESCRIPTION
// #else
// #define WAVEFORM_CURRENT_VERSION WAVEFORM_5_VERSION
// #define WAVEFORM_CURRENT_DESCRIPTION WAVEFORM_5_DESCRIPTION
// #endif
// #define WAVEFORMSUMMARY_CURRENT_VERSION WAVEFORMSUMMARY_5_VERSION
// #define WAVEFORMSUMMARY_CURRENT_DESCRIPTION WAVEFORMSUMMARY_5_DESCRIPTION
//
// class WaveformFactory {
//   public:
//     enum VersionClass {
//         VC_USE,
//         VC_KEEP,
//         VC_REMOVE
//     };
//
//     static Waveform* loadWaveformFromAnalysis(
//             const AnalysisDao::AnalysisInfo& analysis);
//     static VersionClass waveformVersionToVersionClass(const QString& version);
//     static VersionClass waveformSummaryVersionToVersionClass(const QString& version);
//     static QString currentWaveformVersion();
//     static QString currentWaveformDescription();
//     static QString currentWaveformSummaryVersion();
//     static QString currentWaveformSummaryDescription();
// };
