#include "effects/backends/builtin/autopaneffect.h"

#include "effects/backends/effectmanifest.h"
#include "engine/effects/engineeffectparameter.h"
#include "util/math.h"

constexpr float kPositionRampingThreshold = 0.002f;

// static
QString AutoPanEffect::getId() {
    return "org.mixxx.effects.autopan";
}

// static
EffectManifestPointer AutoPanEffect::getManifest() {
    EffectManifestPointer pManifest(new EffectManifest());
    pManifest->setId(getId());
    pManifest->setName(QObject::tr("Autopan"));
    pManifest->setShortName(QObject::tr("Autopan"));
    pManifest->setAuthor("The Mixxx Team");
    pManifest->setVersion("1.0");
    pManifest->setDescription(QObject::tr(
            "Bounce the sound left and right across the stereo field"));

    // Period
    EffectManifestParameterPointer period = pManifest->addParameter();
    period->setId("period");
    period->setName(QObject::tr("Period"));
    period->setShortName(QObject::tr("Period"));
    period->setDescription(QObject::tr(
            "How fast the sound goes from one side to another\n"
            "1/4 - 4 beats rounded to 1/2 beat if tempo is detected\n"
            "1/4 - 4 seconds if no tempo is detected"));
    period->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    period->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    period->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    period->setDefaultLinkInversion(EffectManifestParameter::LinkInversion::Inverted);
    period->setRange(0.0, 2.0, 4.0);

    EffectManifestParameterPointer smoothing = pManifest->addParameter();
    smoothing->setId("smoothing");
    smoothing->setName(QObject::tr("Smoothing"));
    smoothing->setShortName(QObject::tr("Smooth"));
    smoothing->setDescription(QObject::tr(
            "How smoothly the signal goes from one side to the other"));
    smoothing->setValueScaler(EffectManifestParameter::ValueScaler::Logarithmic);
    smoothing->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    smoothing->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    smoothing->setRange(0.25, 0.50, 0.50); // There are two steps per period so max is half

    // TODO(Ferran Pujol): when KnobComposedMaskedRing branch is merged to main,
    //                     make the scaleStartParameter for this be 1.

    // Width : applied on the channel with gain reducing.
    EffectManifestParameterPointer width = pManifest->addParameter();
    width->setId("width");
    width->setName(QObject::tr("Width"));
    width->setShortName(QObject::tr("Width"));
    width->setDescription(QObject::tr(
            "How far the signal goes to each side"));
    width->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    width->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    width->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    width->setRange(0.0, 0.5, 1.0); // 0.02 * sampleRate => 20ms

    return pManifest;
}

void AutoPanEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pSmoothingParameter = parameters.value("smoothing");
    m_pPeriodParameter = parameters.value("period");
    m_pWidthParameter = parameters.value("width");
}

void AutoPanEffect::processChannel(
        AutoPanGroupState* pGroupState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& engineParameters,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    if (enableState == EffectEnableState::Disabled) {
        return;
    }

    double width = m_pWidthParameter->value();
    double period = m_pPeriodParameter->value();
    const auto smoothing = static_cast<float>(0.5 - m_pSmoothingParameter->value());

    if (groupFeatures.beat_length.has_value()) {
        // period is a number of beats
        double beats = std::max(roundToFraction(period, 2), 0.25);
        period = beats * groupFeatures.beat_length->frames;

        // TODO(xxx) sync phase
        //if (groupFeatures.has_beat_fraction) {

    } else {
        // period is a number of seconds
        period = std::max(period, 0.25) * engineParameters.sampleRate();
    }

    // When the period is changed, the position of the sound shouldn't
    // so time need to be recalculated
    if (pGroupState->m_dPreviousPeriod != -1.0) {
        pGroupState->time = static_cast<unsigned int>(
                pGroupState->time * period / pGroupState->m_dPreviousPeriod);
    }

    pGroupState->m_dPreviousPeriod = period;

    if (pGroupState->time >= period || enableState == EffectEnableState::Enabling) {
        pGroupState->time = 0;
    }

    // Normally, the position goes from 0 to 1 linearly. Here we make steps at
    // 0.25 and 0.75 to have the sound fully on the right or fully on the left.
    // At the end, the "position" value can describe a sinusoid or a square
    // curve depending of the size of those steps.

    // coef of the slope
    // a = (y2 - y1) / (x2 - x1)
    //       1  / ( 1 - 2 * stepfrac)
    float a = smoothing != 0.5f ? 1.0f / (1.0f - smoothing * 2.0f) : 1.0f;

    // size of a segment of slope (controlled by the "smoothing" parameter)
    float u = (0.5f - smoothing) / 2.0f;

    pGroupState->frac.setRampingThreshold(kPositionRampingThreshold);

    double sinusoid = 0;

    // NOTE: Assuming engine is working in stereo.
    for (SINT i = 0; i + 1 < engineParameters.samplesPerBuffer(); i += 2) {
        const auto periodFraction = static_cast<CSAMPLE>(pGroupState->time) /
                static_cast<CSAMPLE>(period);

        // current quarter in the trigonometric circle
        float quarter = floorf(periodFraction * 4.0f);

        // part of the period fraction being a step (not in the slope)
        CSAMPLE stepsFractionPart = floorf((quarter + 1.0f) / 2.0f) * smoothing;

        // float inInterval = std::fmod( periodFraction, (period / 2.0) );
        float inStepInterval = std::fmod(periodFraction, 0.5f);

        CSAMPLE angleFraction;
        if (inStepInterval > u && inStepInterval < (u + smoothing)) {
            // at full left or full right
            angleFraction = quarter < 2.0f ? 0.25f : 0.75f;
        } else {
            // in the slope (linear function)
            angleFraction = (periodFraction - stepsFractionPart) * a;
        }

        // transforms the angleFraction into a sinusoid.
        // The width parameter modulates the two limits. if width values 0.5,
        // the limits will be 0.25 and 0.75. If it's 0, it will be 0.5 and 0.5
        // so the sound will be stuck at the center. If it values 1, the limits
        // will be 0 and 1 (full left and full right).
        sinusoid = sin(M_PI * 2.0f * angleFraction) * width;
        pGroupState->frac.setWithRampingApplied(static_cast<float>((sinusoid + 1.0f) / 2.0f));

        // apply the delay
        pGroupState->pDelay->process(
                &pInput[i],
                &pOutput[i],
                -0.005 *
                        math_clamp(
                                ((pGroupState->frac * 2.0) - 1.0f), -1.0, 1.0) *
                        engineParameters.sampleRate());

        double lawCoef = computeLawCoefficient(sinusoid);
        pOutput[i] *= static_cast<CSAMPLE>(pGroupState->frac * lawCoef);
        pOutput[i + 1] *= static_cast<CSAMPLE>((1.0f - pGroupState->frac) * lawCoef);

        pGroupState->time++;
        while (pGroupState->time >= period) {
            // Click for debug
            //pOutput[i] = 1.0f;
            //pOutput[i+1] = 1.0f;

            // The while loop is required in case period changes the value
            pGroupState->time -= static_cast<unsigned int>(period);
        }
    }
}

double AutoPanEffect::computeLawCoefficient(double position) {
    // position is a result of sin() so between -1 and 1
    // full left/right => 1 + 1 / sqrt(abs(1 or -1) + 1) = 1,707106781
    // center => 1 + 1 / sqrt(abs(0) + 1) = 2
    return 1 + 1 / sqrt(std::abs(position) + 1);
}
