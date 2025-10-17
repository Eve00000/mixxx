#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <optional>

struct ControlId {
    QString group;
    QString key;
    bool operator==(const ControlId& o) const {
        return group == o.group && key == o.key;
    }
};
inline size_t qHash(const ControlId& id, size_t seed = 0) noexcept {
    return qHashMulti(seed, id.group, id.key);
}

enum class Policy { Immediate,
    Debounce,
    Throttle };
enum class Category { Toggle,
    Continuous };

struct ControlSpec {
    QString group;
    QString key;
    // If policy/interval not set, we’ll use category defaults
    Category category;
    std::optional<Policy> policyOverride;
    std::optional<int> intervalMsOverride;
};

class OscNotifier : public QObject {
    Q_OBJECT

  public:
    explicit OscNotifier(QObject* parent = nullptr);
    ~OscNotifier() override;

    void observeControls();

    // Category defaults (what you asked for):
    //  - Toggle => Immediate
    //  - Continuous (knobs/faders) => Throttle(200ms)
    void setDefault(Category c, Policy p, int intervalMs = 0);

  private:
    // Defaults
    QHash<Category, Policy> m_defaultPolicy{{Category::Toggle, Policy::Immediate},
            {Category::Continuous, Policy::Debounce}};
    QHash<Category, int> m_defaultInterval{{Category::Toggle, 0},
            {Category::Continuous, 150}}; // ms

    // Debounce state
    QHash<ControlId, QTimer*> m_debounceTimers;
    QHash<ControlId, double> m_debounceLastValues;

    // Throttle (trailing) state
    QHash<ControlId, QElapsedTimer> m_throttleLastSentAt;
    QHash<ControlId, QTimer*> m_throttleTrailingTimers;
    QHash<ControlId, double> m_throttleTrailingValues;

    // Helpers
    void sendOsc(const QString& group, const QString& key, double v);
    void onValueChanged_Debounce(const ControlId& id, double v, int ms);
    void onValueChanged_Throttle(const ControlId& id, double v, int ms);
};
