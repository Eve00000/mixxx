#pragma once

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QVector>
#include <memory>

#include "control/controlproxy.h"
#include "waveform/renderers/waveformrendererabstract.h"

struct KeySegment {
    double startTime;
    double endTime;
    double duration;
    QString key;
    QString type; // "STABLE" or "MODULATION"
    double confidence;

    KeySegment()
            : startTime(0),
              endTime(0),
              duration(0),
              confidence(0.0) {
    }

    KeySegment(double start,
            double end,
            double dur,
            const QString& k,
            const QString& t,
            double conf)
            : startTime(start),
              endTime(end),
              duration(dur),
              key(k),
              type(t),
              confidence(conf) {
    }
};

struct KeyCurveStyle {
    // Text styling
    QColor textColor;
    int fontSize;
    int fontOpacity;

    // Background styling
    QColor backgroundColor;
    int backgroundOpacity;

    // Marker styling
    QColor markerColor;
    int markerWidth;
    Qt::PenStyle markerLineStyle;

    // Visibility
    bool showLabels;
    bool showMarkers;
    bool showBackground;
    bool showLancelotWheel;

    // Wheel type
    enum WheelType {
        WHEEL_MIXXX,   // Mixxx keywheel (C/Am at top)
        WHEEL_LANCELOT // Standard Camelot / Lancelot (12B/12A at top & different mapping)
    };
    WheelType wheelType;

    KeyCurveStyle() {
        textColor = QColor(255, 255, 255);
        fontSize = 20;
        fontOpacity = 200;

        backgroundColor = QColor(0, 0, 0);
        backgroundOpacity = 150;

        markerColor = QColor(255, 200, 100);
        markerWidth = 1;
        markerLineStyle = Qt::DashLine;

        showLabels = true;
        showMarkers = true;
        showBackground = true;
        showLancelotWheel = true;

        wheelType = WHEEL_LANCELOT;
    }
};

struct LancelotKey {
    QString lancelot;
    double angle;
    bool isMinor;
};

class WaveformRenderKeyCurve : public WaveformRendererAbstract {
  public:
    explicit WaveformRenderKeyCurve(WaveformWidgetRenderer* renderer);
    ~WaveformRenderKeyCurve() override = default;

    void setup(const QDomNode& node, const SkinContext& context) override;
    void draw(QPainter* painter, QPaintEvent* event) override;
    void onSetTrack() override;

  private slots:
    void onPlayPositionChanged(double value);
    void onRateRatioChanged(double value);

  private:
    void loadKeyCurve();
    void drawKeyLabel(QPainter* painter,
            const QPointF& position,
            const QString& key,
            Qt::Orientation orientation);
    void drawKeyChangeLabel(QPainter* painter,
            const QPointF& position,
            const QString& previousKey,
            const QString& currentKey,
            Qt::Orientation orientation);
    QColor getColorForKey(const QString& key) const;

    // Lancelot wheel
    void drawLancelotWheel(QPainter* painter);
    void drawWheelSlice(QPainter* painter,
            const QRectF& rect,
            double startAngle,
            double endAngle,
            const QString& lancelot,
            bool isMinor);
    void drawHighlightedWheelKey(QPainter* painter,
            const QRectF& outerRect,
            const QRectF& innerRect,
            const QString& lancelot);
    void updateCurrentWheelKey();
    void initPlayPositionControl();
    void initLancelotLayout();
    void initRateRatioControl();
    void initKeyControl();
    void initKeylockControl();
    void updateTransposedKey();

    QString keyToLancelot(const QString& key) const;
    QString normalizeKeyDisplay(const QString& key) const;
    QString transposeKey(const QString& key, int semitones) const;

    std::unique_ptr<ControlProxy> m_pRateRatioCO;
    std::unique_ptr<ControlProxy> m_pKeyControlCO;
    std::unique_ptr<ControlProxy> m_pPlayPositionCO;
    std::unique_ptr<ControlProxy> m_pKeylockCO;

    QVector<KeySegment> m_segments;
    QVector<LancelotKey> m_lancelotLayout;

    KeyCurveStyle m_style;

    QString m_baseLancelot;
    QString m_transposedLancelot;
    QString m_transposedMusicalKey;
    QString m_transposedWheelKey;
    QString m_currentWheelKey;

    QDateTime m_lastLoadTime;
    QElapsedTimer m_animationTimer;

    double m_currentRateRatio;
    double m_currentKeyShift;
    double m_currentPlayPosition;
    double m_trackLengthSeconds;

    int m_wheelSize;
    int m_wheelMargin;
    int m_currentTotalOffset;

    bool m_visible;
    bool m_keylockEnabled;
};
