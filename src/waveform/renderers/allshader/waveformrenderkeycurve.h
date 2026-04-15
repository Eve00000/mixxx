#pragma once

#include <QColor>
#include <QImage>
#include <memory>

#include "control/controlproxy.h"
#include "rendergraph/geometrynode.h"
#include "rendergraph/node.h"
#include "waveform/renderers/waveformrenderkeycurvebase.h"

class QDomNode;

namespace rendergraph {
class GeometryNode;
class Context;
} // namespace rendergraph

namespace allshader {
class DigitsRenderNode;
class WaveformRenderKeyCurve;
} // namespace allshader

class WaveformKeyCurveNode;
class ControlProxy;

struct KeySegment {
    double startTime;
    double endTime;
    double duration;
    QString key;
    QString type;
    double confidence;
};

struct LancelotKey {
    QString lancelot;
    double angle;
    bool isMinor;
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

class allshader::WaveformRenderKeyCurve : public WaveformRenderKeyCurveBase,
                                          public rendergraph::Node {
    Q_OBJECT
  public:
    explicit WaveformRenderKeyCurve(WaveformWidgetRenderer* waveformWidget,
            ::WaveformRendererAbstract::PositionSource type =
                    ::WaveformRendererAbstract::Play);
    ~WaveformRenderKeyCurve() override = default;

    void draw(QPainter* painter, QPaintEvent* event) override final;
    void setup(const QDomNode& node, const SkinContext& skinContext) override;
    bool init() override;
    void update() override;
    bool isSubtreeBlocked() const override;

  protected:
    void updateKeyTextures() override;

  private:
    // void createNode();
    void createNode(const QImage& image);
    void updateNode();
    void initLancelotLayout();
    void updateTransposedKey();
    QImage drawKeyCurveTexture();
    QImage drawCamelotWheel();
    void updateCurrentKey();
    QString keyToLancelot(const QString& key) const;
    QString normalizeKeyDisplay(const QString& key) const;
    QString transposeKey(const QString& key, int semitones) const;
    QColor getColorForKey(const QString& key) const;
    void drawCamelotWheelOnImage(QPainter& painter, float width, float height);

    QVector<KeySegment> m_segments;
    QVector<LancelotKey> m_lancelotLayout;

    KeyCurveStyle m_style;

    QElapsedTimer m_animationTimer;

    QString m_currentKey;
    QString m_currentLancelot;
    QString m_baseLancelot;
    QString m_transposedLancelot;
    QString m_transposedMusicalKey;
    QString m_transposedWheelKey;
    QString m_currentWheelKey;

    WaveformKeyCurveNode* m_pKeyCurveNode{};
    rendergraph::Node* m_pKeyCurveNodesParent{};

    std::unique_ptr<ControlProxy> m_pPlayPositionCO;
    std::unique_ptr<ControlProxy> m_pRateRatioCO;
    std::unique_ptr<ControlProxy> m_pKeyControlCO;
    std::unique_ptr<ControlProxy> m_pKeylockCO;

    double m_playPosition;
    double m_currentRateRatio;
    double m_currentKeyShift;
    double m_currentPlayPosition;
    double m_trackLengthSeconds;

    int m_wheelSize;
    int m_wheelMargin;
    int m_currentTotalOffset;

    bool m_visible;
    bool m_keylockEnabled;
    bool m_isSlipRenderer;
    bool m_textureReady{false};
    QImage m_pendingWheelImage;
    bool m_waitingForContext{false};
};
