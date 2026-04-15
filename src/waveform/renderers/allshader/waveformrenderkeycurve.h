#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <memory>

#include "control/controlproxy.h"
#include "rendergraph/geometrynode.h"
#include "rendergraph/node.h"
#include "track/trackid.h"
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
    QColor textColor;
    int fontSize;
    int fontOpacity;
    QColor backgroundColor;
    int backgroundOpacity;
    QColor markerColor;
    int markerWidth;
    Qt::PenStyle markerLineStyle;
    bool showLabels;
    bool showMarkers;
    bool showBackground;
    bool showLancelotWheel;
    bool showDiamonds;

    enum WheelType {
        WHEEL_MIXXX,
        WHEEL_LANCELOT
    };
    WheelType wheelType;

    KeyCurveStyle();
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
    void createNode(const QImage& image);
    void updateNode();
    void initLancelotLayout();
    void updateTransposedKey();
    void loadKeyCurve();
    // Drawing procedures
    void drawKeyMarkers(QPainter& painter, float width, float height);
    void drawKeyLabels(QPainter& painter, float width, float height);
    void drawCamelotWheelComponents(QPainter& painter, float width, float height);
    void drawWheelSlices(QPainter& painter, const QRectF& rect, const QRectF& innerRect);
    void drawHighlightedSlice(QPainter& painter, const QRectF& rect, const QRectF& innerRect);
    void drawWheelText(QPainter& painter, int wheelX, int wheelY, int wheelSize);
    QImage createFullImage();

    // Helper to get visible range
    void getVisibleRange(double& startSample, double& endSample, double& width, double& height);

    // Update transposed values (used by both updateTransposedKey and updateNode)
    void calculateTransposedValues(int totalSemitones);

    // QImage drawKeyCurveTexture();
    QImage drawCamelotWheel();
    void updateCurrentKey();
    QString keyToLancelot(const QString& key) const;
    QString normalizeKeyDisplay(const QString& key) const;
    QString transposeKey(const QString& key, int semitones) const;
    QColor getColorForKey(const QString& key) const;

    QVector<KeySegment> m_segments;
    QVector<LancelotKey> m_lancelotLayout;
    KeyCurveStyle m_style;
    QElapsedTimer m_animationTimer;

    TrackId m_lastTrackId;

    QString m_currentKey;
    QString m_currentLancelot;
    QString m_baseLancelot;
    QString m_transposedLancelot;
    QString m_transposedMusicalKey;
    QString m_transposedWheelKey;
    QString m_currentWheelKey;

    WaveformKeyCurveNode* m_pKeyCurveNode;
    rendergraph::Node* m_pKeyCurveNodesParent;

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
    bool m_textureReady;
    bool m_segmentsLoaded{false};
    QImage m_pendingWheelImage;
};

inline KeyCurveStyle::KeyCurveStyle()
        : textColor(255, 255, 255),
          fontSize(20),
          fontOpacity(200),
          backgroundColor(0, 0, 0),
          backgroundOpacity(150),
          markerColor(255, 200, 100),
          markerWidth(1),
          markerLineStyle(Qt::DashLine),
          showLabels(true),
          showMarkers(true),
          showBackground(true),
          showLancelotWheel(true),
          showDiamonds(true),
          wheelType(WHEEL_LANCELOT) {
}