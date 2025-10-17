
#include "oscnotifier.h"

#include "control/controlproxy.h"
#include "moc_oscnotifier.cpp"
#include "oscfunctions.h"
#include "util/parented_ptr.h"

OscNotifier::OscNotifier(QObject* parent)
        : QObject(parent) {
}

OscNotifier::~OscNotifier() = default;

void OscNotifier::setDefault(Category c, Policy p, int intervalMs) {
    m_defaultPolicy[c] = p;
    m_defaultInterval[c] = intervalMs;
}

void OscNotifier::observeControls() {
    qDebug() << "[OSC] [OSCNOTIFIER] -> Start observing controls";

    // List of all desired controls we want to observe
    std::vector<ControlSpec> specs;

    for (int channel = 1; channel <= 4; ++channel) {
        auto G = [channel](const char* fmt) { return QString(fmt).arg(channel); };

        // Toggles
        specs.push_back({G("[Channel%1]"), "play", Category::Toggle, std::nullopt, std::nullopt});
        specs.push_back({G("[Channel%1]"), "pfl", Category::Toggle, std::nullopt, std::nullopt});

        // Continuous
        specs.push_back({G("[Channel%1]"),
                "volume",
                Category::Continuous,
                std::nullopt,
                std::nullopt});
        specs.push_back({G("[Channel%1]"),
                "pregain",
                Category::Continuous,
                std::nullopt,
                std::nullopt});
        specs.push_back({G("[Channel%1]"),
                "rate",
                Category::Continuous,
                std::nullopt,
                std::nullopt});
        specs.push_back({G("[Channel%1]"),
                "bpm",
                Category::Continuous,
                Policy::Throttle,
                100}); // example override: bpm throttle 100ms

        specs.push_back({G("[QuickEffectRack1_[Channel%1]]"),
                "enabled",
                Category::Toggle,
                std::nullopt,
                std::nullopt});
        specs.push_back({G("[QuickEffectRack1_[Channel%1]]"),
                "super1",
                Category::Continuous,
                std::nullopt,
                std::nullopt});

        // EQs
        for (const auto* k : {"parameter1", "parameter2", "parameter3"})
            specs.push_back({G("[EqualizerRack1_[Channel%1]_Effect1]"),
                    k,
                    Category::Continuous,
                    std::nullopt,
                    std::nullopt});
        for (const auto* k : {"button_parameter1", "button_parameter2", "button_parameter3"})
            specs.push_back({G("[EqualizerRack1_[Channel%1]_Effect1]"),
                    k,
                    Category::Toggle,
                    std::nullopt,
                    std::nullopt});

        // Stems
        for (int stem = 1; stem <= 4; ++stem) {
            auto GS = [channel, stem](const char* fmt) {
                return QString(fmt).arg(channel).arg(stem);
            };
            specs.push_back({GS("[Channel%1_Stem%2]"),
                    "mute",
                    Category::Toggle,
                    std::nullopt,
                    std::nullopt});
            specs.push_back({GS("[Channel%1_Stem%2]"),
                    "volume",
                    Category::Continuous,
                    std::nullopt,
                    std::nullopt});
            specs.push_back({GS("[QuickEffectRack1_[Channel%1_Stem%2]]"),
                    "enabled",
                    Category::Toggle,
                    std::nullopt,
                    std::nullopt});
            specs.push_back({GS("[QuickEffectRack1_[Channel%1_Stem%2]]"),
                    "super1",
                    Category::Continuous,
                    std::nullopt,
                    std::nullopt});
        }
    }

    // Master
    specs.push_back({QStringLiteral("[Master]"),
            "crossfader",
            Category::Continuous,
            std::nullopt,
            std::nullopt});

    // Hook up signals with the selected policy (per control or category default)
    //    for (const auto& s : specs) {
    for (const auto& s : std::as_const(specs)) {
        auto proxy = make_parented<ControlProxy>(s.group, s.key, this);
        ControlId id{s.group, s.key};

        const Policy policy = s.policyOverride.value_or(m_defaultPolicy.value(s.category));
        const int ms = s.intervalMsOverride.value_or(m_defaultInterval.value(s.category));

        proxy->connectValueChanged(
                this,
                [this, id, policy, ms](double value) {
                    switch (policy) {
                    case Policy::Immediate:
                        sendOsc(id.group, id.key, value);
                        break;
                    case Policy::Debounce:
                        onValueChanged_Debounce(id, value, ms);
                        break;
                    case Policy::Throttle:
                        onValueChanged_Throttle(id, value, ms);
                        break;
                    }
                },
                Qt::DirectConnection);
    }
}

// static Category inferCategory(const QString& key) {
//     if (key == "play" || key == "pfl" || key == "enabled" || key == "mute" ||
//             key.startsWith("button_"))
//         return Category::Toggle;
//     return Category::Continuous;
// }

// Debounce: send after X ms of inactivity
void OscNotifier::onValueChanged_Debounce(const ControlId& id, double v, int ms) {
    m_debounceLastValues[id] = v;
    QTimer* t = m_debounceTimers.value(id, nullptr);
    if (!t) {
        t = new QTimer(this);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, [this, id]() {
            const double vv = m_debounceLastValues.value(id);
            sendOsc(id.group, id.key, vv);
        });
        m_debounceTimers.insert(id, t);
    }
    t->start(std::max(1, ms));
}

// Throttle (trailing): at most once per X ms; guarantee last value
void OscNotifier::onValueChanged_Throttle(const ControlId& id, double v, int ms) {
    auto& last = m_throttleLastSentAt[id];
    const bool canNow = !last.isValid() || last.elapsed() >= ms;
    if (canNow) {
        sendOsc(id.group, id.key, v);
        last.start();
        if (auto t = m_throttleTrailingTimers.take(id)) {
            t->stop();
            t->deleteLater();
        }
        m_throttleTrailingValues.remove(id);
        return;
    }
    // schedule trailing send
    m_throttleTrailingValues[id] = v;
    QTimer* t = m_throttleTrailingTimers.value(id, nullptr);
    if (!t) {
        t = new QTimer(this);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, [this, id]() {
            const double vv = m_throttleTrailingValues.take(id);
            sendOsc(id.group, id.key, vv);
            m_throttleLastSentAt[id].start();
        });
        m_throttleTrailingTimers.insert(id, t);
    }
    const int remaining = ms - static_cast<int>(last.elapsed());
    t->start(std::max(1, remaining));
}

void OscNotifier::sendOsc(const QString& group, const QString& key, double v) {
    oscFunctionsSendPtrType(group, key, DefOscBodyType::FLOATBODY, "", 0, 0, static_cast<float>(v));
}
