
#include "oscnotifier.h"

#include "control/controlproxy.h"
#include "moc_oscnotifier.cpp"
#include "oscfunctions.h"
#include "util/parented_ptr.h"

OscNotifier::OscNotifier(QObject* parent)
        : QObject(parent) {
}

OscNotifier::~OscNotifier() = default;

void OscNotifier::observeControls() {
    qDebug() << "[OSC] [OSCNOTIFIER] -> Start observing controls";

    // List of all desired controls we want to observe
    std::vector<std::pair<QString, QString>> controlSpecs;

    // Channels
    for (int channel = 1; channel <= 4; channel++) {
        // Channel play
        controlSpecs.emplace_back(
                QString("[Channel%1]").arg(channel),
                QStringLiteral("play"));

        // Channel volume
        controlSpecs.emplace_back(
                QString("[Channel%1]").arg(channel),
                QStringLiteral("volume"));

        // Channel pregain
        controlSpecs.emplace_back(
                QString("[Channel%1]").arg(channel),
                QStringLiteral("pregain"));

        // Channel pfl
        controlSpecs.emplace_back(
                QString("[Channel%1]").arg(channel),
                QStringLiteral("pfl"));

        // Channel rate
        controlSpecs.emplace_back(
                QString("[Channel%1]").arg(channel),
                QStringLiteral("rate"));

        // Channel bpm
        controlSpecs.emplace_back(
                QString("[Channel%1]").arg(channel),
                QStringLiteral("bpm"));

        // Channel Fx enabled
        controlSpecs.emplace_back(
                QString("[QuickEffectRack1_[Channel%1]]").arg(channel),
                QStringLiteral("enabled"));

        // Channel Fx SuperKnob
        controlSpecs.emplace_back(
                QString("[QuickEffectRack1_[Channel%1]]").arg(channel),
                QStringLiteral("super1"));

        // EQ Low
        controlSpecs.emplace_back(
                QString("[EqualizerRack1_[Channel%1]_Effect1]").arg(channel),
                QStringLiteral("parameter1"));

        // EQ Low Kill
        controlSpecs.emplace_back(
                QString("[EqualizerRack1_[Channel%1]_Effect1]").arg(channel),
                QStringLiteral("button_parameter1"));

        // EQ Mid
        controlSpecs.emplace_back(
                QString("[EqualizerRack1_[Channel%1]_Effect1]").arg(channel),
                QStringLiteral("parameter2"));

        // EQ Mid Kill
        controlSpecs.emplace_back(
                QString("[EqualizerRack1_[Channel%1]_Effect1]").arg(channel),
                QStringLiteral("button_parameter2"));

        // EQ High
        controlSpecs.emplace_back(
                QString("[EqualizerRack1_[Channel%1]_Effect1]").arg(channel),
                QStringLiteral("parameter3"));

        // EQ High Kill
        controlSpecs.emplace_back(
                QString("[EqualizerRack1_[Channel%1]_Effect1]").arg(channel),
                QStringLiteral("button_parameter3"));

        // Stems
        for (int stem = 1; stem <= 4; stem++) {
            // Stems mute
            controlSpecs.emplace_back(
                    QString("[Channel%1_Stem%2]").arg(channel).arg(stem),
                    QStringLiteral("mute"));

            // Stems volume
            controlSpecs.emplace_back(
                    QString("[Channel%1_Stem%2]").arg(channel).arg(stem),
                    QStringLiteral("volume"));

            // Stems Fx enabled
            controlSpecs.emplace_back(
                    QString("[QuickEffectRack1_[Channel%1_Stem%2]]").arg(channel).arg(stem),
                    QStringLiteral("enabled"));

            // Stems Fx SuperKnob
            controlSpecs.emplace_back(
                    QString("[QuickEffectRack1_[Channel%1_Stem%2]]").arg(channel).arg(stem),
                    QStringLiteral("super1"));
        }
    }

    // Master crossfader
    controlSpecs.emplace_back(QStringLiteral("[Master]"), QStringLiteral("crossfader"));

    // Iterate over all controls to observe value changes and send OSC messages
    for (const auto& [group, key] : controlSpecs) {
        auto proxy = make_parented<ControlProxy>(group, key, this);
        proxy->connectValueChanged(
                this,
                [group, key](double value) {
                    oscFunctionsSendPtrType(
                            group,
                            key,
                            DefOscBodyType::FLOATBODY,
                            "",
                            0,
                            0,
                            static_cast<float>(value));
                },
                Qt::DirectConnection);
    }
}
