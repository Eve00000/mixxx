#include "waveform/waveformfactory.h"

#include "waveform/waveform.h"

// static
Waveform* WaveformFactory::loadWaveformFromAnalysis(
        const AnalysisDao::AnalysisInfo& analysis) {
    Waveform* pWaveform = new Waveform(analysis.data);
    pWaveform->setId(analysis.analysisId);
    pWaveform->setVersion(analysis.version);
    pWaveform->setDescription(analysis.description);
    return pWaveform;
}

// static
WaveformFactory::VersionClass WaveformFactory::waveformVersionToVersionClass(const QString& version) {
    if (version == WAVEFORM_CURRENT_VERSION) {
        // use, since is our version
        return VC_USE;
    }

    if (version == WAVEFORM_4_VERSION) {
        // Used in Mixxx 1.12 beta, suffers Bug #7776
        return VC_REMOVE;
    }

    if (version == WAVEFORM_2_VERSION) {
        // keep for use with old Mixxx versions
        return VC_KEEP;
    }

    if (version == WAVEFORM_3_VERSION) {
        // Used in Mixxx 1.11 beta, suffers Bug #6748
        return VC_REMOVE;
    }

#ifdef __STEM__
    if (version == WAVEFORM_6_0_VERSION) {
        // Used in Mixxx 2.6 beta, introducing stem data but later replaced
        // with the signal scale removal. Also contains phantom stem data.
        return VC_REMOVE;
    }

    if (version == WAVEFORM_6_1_VERSION) {
        // Stem layout counted the premix channel as a stem and read stem
        // channels from the wrong interleaved offset. Contains phantom stem
        // data for tracks whose stem streams end before the reference stream.
        return VC_REMOVE;
    }
#endif

    // possible a future version
    return VC_KEEP;
}

// static
WaveformFactory::VersionClass WaveformFactory::waveformSummaryVersionToVersionClass(const QString& version) {
    if (version == WAVEFORMSUMMARY_CURRENT_VERSION) {
        // use, since is our version
        return VC_USE;
    }

    if (version == WAVEFORMSUMMARY_4_VERSION) {
        // Used in Mixxx 1.12 beta, suffers Bug #7776
        return VC_REMOVE;
    }

    if (version == WAVEFORMSUMMARY_2_VERSION) {
        // keep for use with old Mixxx versions
        return VC_KEEP;
    }

    if (version == WAVEFORMSUMMARY_3_VERSION) {
        // Used in Mixxx 1.11 beta, suffers Bug #6744
        return VC_REMOVE;
    }

#ifdef __STEM__
    if (version == WAVEFORMSUMMARY_6_0_VERSION) {
        // Stem summary counterpart of Waveform-6.0. Contains phantom stem
        // data for the same reason as Waveform-6.0.
        return VC_REMOVE;
    }

    if (version == WAVEFORMSUMMARY_6_1_VERSION) {
        // Stem summary counterpart of Waveform-6.1. Contains phantom stem
        // data for the same reason as Waveform-6.1.
        return VC_REMOVE;
    }
#endif

    // possible a future version
    return VC_KEEP;
}

// static
QString WaveformFactory::currentWaveformVersion() {
    return WAVEFORM_CURRENT_VERSION;
}

// static
QString WaveformFactory::currentWaveformSummaryVersion() {
    return WAVEFORMSUMMARY_CURRENT_VERSION;
}

// static
QString WaveformFactory::currentWaveformDescription() {
    return WAVEFORM_CURRENT_DESCRIPTION;
}

// static
QString WaveformFactory::currentWaveformSummaryDescription() {
    return WAVEFORMSUMMARY_CURRENT_DESCRIPTION;
}

// #include "waveform/waveformfactory.h"
// #include "waveform/waveform.h"
//
//// static
// Waveform* WaveformFactory::loadWaveformFromAnalysis(
//         const AnalysisDao::AnalysisInfo& analysis) {
//     Waveform* pWaveform = new Waveform(analysis.data);
//     pWaveform->setId(analysis.analysisId);
//     pWaveform->setVersion(analysis.version);
//     pWaveform->setDescription(analysis.description);
//     return pWaveform;
// }
//
//// static
// WaveformFactory::VersionClass
// WaveformFactory::waveformVersionToVersionClass(const QString& version) {
//     if (version == WAVEFORM_CURRENT_VERSION) {
//         // use, since is our version
//         return VC_USE;
//     }
//
//     if (version == WAVEFORM_4_VERSION) {
//         // Used in Mixxx 1.12 beta, suffers Bug #7776
//         return VC_REMOVE;
//     }
//
//     if (version == WAVEFORM_2_VERSION) {
//         // keep for use with old Mixxx versions
//         return VC_KEEP;
//     }
//
//     if (version == WAVEFORM_3_VERSION) {
//         // Used in Mixxx 1.11 beta, suffers Bug #6748
//         return VC_REMOVE;
//     }
//
// #ifdef __STEM__
//     if (version == WAVEFORM_6_0_VERSION) {
//         // Used in Mixxx 2.6 beta, introducing stem data but later replaced
//         with
//         // the signal scale removal
//         return VC_REMOVE;
//     }
// #endif
//
//     // possible a future version
//     return VC_KEEP;
// }
//
//// static
// WaveformFactory::VersionClass
// WaveformFactory::waveformSummaryVersionToVersionClass(const QString& version)
// {
//     if (version == WAVEFORMSUMMARY_CURRENT_VERSION) {
//         // use, since is our version
//         return VC_USE;
//     }
//
//     if (version == WAVEFORMSUMMARY_4_VERSION) {
//         // Used in Mixxx 1.12 beta, suffers Bug #7776
//         return VC_REMOVE;
//     }
//
//     if (version == WAVEFORMSUMMARY_2_VERSION) {
//         // keep for use with old Mixxx versions
//         return VC_KEEP;
//     }
//
//     if (version == WAVEFORMSUMMARY_3_VERSION) {
//         // Used in Mixxx 1.11 beta, suffers Bug #6744
//         return VC_REMOVE;
//     }
//
//     // possible a future version
//     return VC_KEEP;
// }
//
//// static
// QString WaveformFactory::currentWaveformVersion() {
//     return WAVEFORM_CURRENT_VERSION;
// }
//
//// static
// QString WaveformFactory::currentWaveformSummaryVersion() {
//     return WAVEFORMSUMMARY_CURRENT_VERSION;
// }
//
//// static
// QString WaveformFactory::currentWaveformDescription() {
//     return WAVEFORM_CURRENT_DESCRIPTION;
// }
//
//// static
// QString WaveformFactory::currentWaveformSummaryDescription() {
//     return WAVEFORMSUMMARY_CURRENT_DESCRIPTION;
// }
