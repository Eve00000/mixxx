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

class WaveformRenderKeyCurve : public WaveformRendererAbstract {
  public:
    explicit WaveformRenderKeyCurve(WaveformWidgetRenderer* renderer);
    ~WaveformRenderKeyCurve() override = default;

    void setup(const QDomNode& node, const SkinContext& context) override;
    void draw(QPainter* painter, QPaintEvent* event) override;
    void onSetTrack() override;

  private slots:
    void onPlayPositionChanged(double value);

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
    QString keyToLancelot(const QString& key) const;
    QString normalizeKeyDisplay(const QString& key) const;
    void updateCurrentWheelKey();
    void initPlayPositionControl();
    void initLancelotLayout();

    QVector<KeySegment> m_segments;
    KeyCurveStyle m_style;
    bool m_visible;
    double m_trackLengthSeconds;
    QDateTime m_lastLoadTime;

    std::unique_ptr<ControlProxy> m_pRateRatioCO;
    std::unique_ptr<ControlProxy> m_pPlayPositionCO;
    double m_currentRateRatio;
    double m_currentPlayPosition;
    QString m_currentWheelKey;
    QElapsedTimer m_animationTimer;

    int m_wheelSize;
    int m_wheelMargin;

    struct LancelotKey {
        QString lancelot;
        double angle;
        bool isMinor;
    };
    QVector<LancelotKey> m_lancelotLayout;
};
