#include "waveform/renderers/allshader/waveformrenderbpmcurve.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
#include <cmath>

#include "moc_waveformrenderbpmcurve.cpp"
#include "rendergraph/context.h"
#include "rendergraph/geometry.h"
#include "rendergraph/geometrynode.h"
#include "rendergraph/material/texturematerial.h"
#include "rendergraph/texture.h"
#include "rendergraph/vertexupdaters/texturedvertexupdater.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"

using namespace rendergraph;

class WaveformBpmCurveNode : public rendergraph::GeometryNode {
  public:
    WaveformBpmCurveNode(rendergraph::Context* pContext, const QImage& image) {
        initForRectangles<TextureMaterial>(1);
        updateTexture(pContext, image);
    }

    void updateTexture(rendergraph::Context* pContext, const QImage& image) {
        dynamic_cast<TextureMaterial&>(material())
                .setTexture(std::make_unique<Texture>(pContext, image));
        m_textureWidth = static_cast<float>(image.width());
        m_textureHeight = static_cast<float>(image.height());
    }

    void updatePosition(float x, float y, float devicePixelRatio) {
        TexturedVertexUpdater vertexUpdater{
                geometry().vertexDataAs<Geometry::TexturedPoint2D>()};
        vertexUpdater.addRectangle({x, y},
                {x + m_textureWidth / devicePixelRatio,
                        y + m_textureHeight / devicePixelRatio},
                {0.f, 0.f},
                {1.f, 1.f});
        markDirtyGeometry();
        markDirtyMaterial();
    }

    float textureWidth() const {
        return m_textureWidth;
    }
    float textureHeight() const {
        return m_textureHeight;
    }

  private:
    float m_textureWidth{};
    float m_textureHeight{};
};

namespace allshader {

constexpr bool showDebugAllshaderWaveformRenderBPMCurve = false;

WaveformRenderBpmCurve::WaveformRenderBpmCurve(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : WaveformRenderBpmCurveBase(waveformWidget, false),
          m_visible(true),
          m_minBpm(0),
          m_maxBpm(0),
          m_yMinBpm(0),
          m_yMaxBpm(0),
          m_offsetSeconds(0.0),
          m_currentRateRatio(1.0),
          m_trackLengthSeconds(0.0),
          m_trackSamples(0.0),
          m_pBpmCurveNode(nullptr),
          m_pBpmCurveNodesParent(nullptr),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
          m_textureReady(false),
          m_needsTextureUpdate(true),
          m_needsRangeRecalculation(true),
          m_lastTrackId() {
    auto pNode = std::make_unique<rendergraph::Node>();
    m_pBpmCurveNodesParent = pNode.get();
    appendChildNode(std::move(pNode));

    if (!m_waveformRenderer->getGroup().isEmpty()) {
        m_pRateRatioCO = std::make_unique<ControlProxy>(
                m_waveformRenderer->getGroup(), "rate_ratio");
        m_currentRateRatio = m_pRateRatioCO->get();
    }
}

void WaveformRenderBpmCurve::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderBpmCurve::setup(const QDomNode& node, const SkinContext& skinContext) {
    Q_UNUSED(node);
    Q_UNUSED(skinContext);
    WaveformRenderBpmCurveBase::setup(node, skinContext);
}

bool WaveformRenderBpmCurve::init() {
    if (showDebugAllshaderWaveformRenderBPMCurve) {
        qDebug() << "[WaveformRenderBpmCurve - Allshader] init called";
    }
    return true;
}

bool WaveformRenderBpmCurve::isSubtreeBlocked() const {
    return m_isSlipRenderer && !m_waveformRenderer->isSlipActive();
}

void WaveformRenderBpmCurve::setOffset(double offsetSeconds) {
    m_offsetSeconds = offsetSeconds;
}

double WaveformRenderBpmCurve::getOffset() const {
    return m_offsetSeconds;
}

void WaveformRenderBpmCurve::loadBpmCurve() {
    m_segments.clear();

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack || !pTrack->getId().isValid()) {
        return;
    }
    if (showDebugAllshaderWaveformRenderBPMCurve) {
        qDebug() << "[WaveformRenderKeyCurve - Allshader] Loading key curve...";
    }

    m_trackLengthSeconds = pTrack->getDuration();
    m_trackSamples = m_waveformRenderer->getTrackSamples();

    QString trackIdStr = pTrack->getId().toString();
    QString bpmDir = QStandardPaths::writableLocation(
                             QStandardPaths::AppLocalDataLocation) +
            "/bpmcurve/";
    QString jsonPath = bpmDir + trackIdStr + ".json";

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull() || !doc.isObject()) {
        return;
    }

    QJsonObject rootObj = doc.object();
    QJsonArray bpmArray;

    if (rootObj.contains("bpm_curve") && rootObj["bpm_curve"].isArray()) {
        bpmArray = rootObj["bpm_curve"].toArray();
    } else {
        return;
    }

    for (const QJsonValue& val : std::as_const(bpmArray)) {
        if (!val.isObject()) {
            continue;
        }
        QJsonObject obj = val.toObject();
        SegmentPoint seg;
        seg.position = obj["position"].toDouble();
        seg.duration = obj["duration"].toDouble();
        seg.bpm_start = obj["bpm_start"].toDouble();
        seg.bpm_end = obj["bpm_end"].toDouble();
        seg.range_start = obj["range_start"].toDouble();
        seg.range_end = obj["range_end"].toDouble();
        seg.type = obj["type"].toString();
        m_segments.append(seg);
    }

    // Load offset from JSON if present
    if (rootObj.contains("track") && rootObj["track"].isObject()) {
        QJsonObject trackObj = rootObj["track"].toObject();
        if (trackObj.contains("offset_seconds")) {
            m_offsetSeconds = trackObj["offset_seconds"].toDouble();
        }
    }

    m_needsRangeRecalculation = true;
    m_needsTextureUpdate = true;
    calculateBpmRange();
}

void WaveformRenderBpmCurve::calculateBpmRange() {
    if (!m_needsRangeRecalculation && !m_segments.isEmpty()) {
        return;
    }
    m_needsRangeRecalculation = false;
    if (m_segments.isEmpty()) {
        m_minBpm = 0;
        m_maxBpm = 0;
        m_yMinBpm = 0;
        m_yMaxBpm = 0;
        return;
    }

    double minBpm = m_segments[0].bpm_start * m_currentRateRatio;
    double maxBpm = m_segments[0].bpm_start * m_currentRateRatio;

    for (const auto& seg : std::as_const(m_segments)) {
        double startBpmAdj = seg.bpm_start * m_currentRateRatio;
        double endBpmAdj = seg.bpm_end * m_currentRateRatio;

        minBpm = qMin(minBpm, qMin(startBpmAdj, endBpmAdj));
        maxBpm = qMax(maxBpm, qMax(startBpmAdj, endBpmAdj));
    }

    m_minBpm = minBpm;
    m_maxBpm = maxBpm;

    double bpmRange = m_maxBpm - m_minBpm;
    double minRange = 40.0;

    if (bpmRange < minRange) {
        double midBpm = (m_minBpm + m_maxBpm) / 2.0;
        m_yMinBpm = midBpm - (minRange / 2.0);
        m_yMaxBpm = midBpm + (minRange / 2.0);
    } else {
        double padding = bpmRange * 0.1;
        if (padding < 0.5) {
            padding = 0.5;
        }
        m_yMinBpm = m_minBpm - padding;
        m_yMaxBpm = m_maxBpm + padding;
    }

    if (m_yMinBpm < 0) {
        m_yMinBpm = 0;
    }

    if (showDebugAllshaderWaveformRenderBPMCurve) {
        qDebug() << "[WaveformRenderBpmCurve - Allshader] BPM range - min:"
                 << m_minBpm << "max:" << m_maxBpm << "yMin:" << m_yMinBpm
                 << "yMax:" << m_yMaxBpm;
    }
}

double WaveformRenderBpmCurve::mapBpmToY(double bpm, double height) const {
    double adjustedBpm = bpm * m_currentRateRatio;
    double bpmRange = m_yMaxBpm - m_yMinBpm;
    if (bpmRange <= 0.001) {
        return height / 2;
    }
    double normalized = (adjustedBpm - m_yMinBpm) / bpmRange;
    normalized = qBound(0.0, normalized, 1.0);
    return height - (normalized * height);
}

double WaveformRenderBpmCurve::getFullImageWidth() const {
    double trackSamples = m_waveformRenderer->getTrackSamples();
    double visibleWidth = m_waveformRenderer->getWidth();

    if (trackSamples <= 0 || visibleWidth <= 0) {
        return 0;
    }

    double samplesPerPixel = trackSamples / visibleWidth;
    if (samplesPerPixel <= 0) {
        return 0;
    }

    return trackSamples / samplesPerPixel;
}

QImage WaveformRenderBpmCurve::drawBpmTexture() {
    float width = static_cast<float>(m_waveformRenderer->getWidth());
    float height = static_cast<float>(m_waveformRenderer->getBreadth());

    if (width <= 0 || height <= 0) {
        return QImage();
    }

    QImage image(static_cast<int>(width),
            static_cast<int>(height),
            QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_pRateRatioCO) {
        m_currentRateRatio = m_pRateRatioCO->get();
        calculateBpmRange();
    }

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (pTrack && !m_segments.isEmpty()) {
        double trackLengthSeconds = pTrack->getDuration();
        double trackSamples = m_waveformRenderer->getTrackSamples();
        double firstDisplayedPosition = m_waveformRenderer->getFirstDisplayedPosition();
        double lastDisplayedPosition = m_waveformRenderer->getLastDisplayedPosition();

        double startSample = firstDisplayedPosition * trackSamples;
        double endSample = lastDisplayedPosition * trackSamples;

        // Draw BPM curve
        if (m_style.showCurve) {
            painter.setPen(QPen(m_style.curveColor, m_style.curveWidth));

            for (const auto& seg : std::as_const(m_segments)) {
                double startTime = seg.position + m_offsetSeconds;
                double endTime = seg.range_end + m_offsetSeconds;

                startTime = qMax(0.0, qMin(startTime, trackLengthSeconds));
                endTime = qMax(0.0, qMin(endTime, trackLengthSeconds));

                if (endTime <= startTime) {
                    continue;
                }

                double startPos = startTime * (trackSamples / trackLengthSeconds);
                double endPos = endTime * (trackSamples / trackLengthSeconds);

                if (endPos < startSample || startPos > endSample) {
                    continue;
                }

                double visibleStart = qMax(startPos, startSample);
                double visibleEnd = qMin(endPos, endSample);

                double tStart = (visibleStart - startPos) / (endPos - startPos);
                double tEnd = (visibleEnd - startPos) / (endPos - startPos);
                double bpmStart = seg.bpm_start + (seg.bpm_end - seg.bpm_start) * tStart;
                double bpmEnd = seg.bpm_start + (seg.bpm_end - seg.bpm_start) * tEnd;

                double xStart = m_waveformRenderer->transformSamplePositionInRendererWorld(
                        visibleStart, ::WaveformRendererAbstract::Play);
                double xEnd = m_waveformRenderer->transformSamplePositionInRendererWorld(
                        visibleEnd, ::WaveformRendererAbstract::Play);

                double yStart = mapBpmToY(bpmStart, height);
                double yEnd = mapBpmToY(bpmEnd, height);

                if (showDebugAllshaderWaveformRenderBPMCurve) {
                    qDebug() << "[WaveformRenderBpmCurve - Allshader] BPM:"
                             << bpmStart << "->" << bpmEnd << "Y:" << yStart
                             << "->" << yEnd;
                }

                painter.drawLine(QLineF(xStart, yStart, xEnd, yEnd));
            }
        }

        // Draw markers
        if (m_style.showMarkers) {
            painter.setPen(QPen(m_style.markerColor, m_style.markerWidth, m_style.markerLineStyle));

            for (const auto& seg : std::as_const(m_segments)) {
                double boundaryTime = seg.position + m_offsetSeconds;
                double boundaryPos = boundaryTime * (trackSamples / trackLengthSeconds);

                if (boundaryPos >= startSample && boundaryPos <= endSample) {
                    double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                            boundaryPos, ::WaveformRendererAbstract::Play);

                    if (x >= 0 && x <= width) {
                        painter.drawLine(QLineF(x, 0, x, height));

                        if (m_style.showDiamonds) {
                            painter.setPen(Qt::NoPen);
                            painter.setBrush(m_style.markerColor);
                            QPolygonF diamond;
                            diamond << QPointF(x, 4) << QPointF(x + 4, 0)
                                    << QPointF(x, -4) << QPointF(x - 4, 0);
                            painter.drawPolygon(diamond);
                        }
                    }
                }
            }
        }

        // Draw markerlabels
        if (m_style.showLabels) {
            QFont labelFont = painter.font();
            labelFont.setPointSize(m_style.labelFontSize);
            labelFont.setBold(true);
            painter.setFont(labelFont);

            double minLabelSpacing = 400;
            double lastLabelX = -minLabelSpacing;
            double labelY = 5;

            for (const auto& seg : std::as_const(m_segments)) {
                double boundaryTime = seg.position + m_offsetSeconds;
                double boundaryPos = boundaryTime * (trackSamples / trackLengthSeconds);

                if (boundaryPos >= startSample && boundaryPos <= endSample) {
                    double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                            boundaryPos, ::WaveformRendererAbstract::Play);

                    if (x >= 0 && x <= width && (x - lastLabelX) >= minLabelSpacing) {
                        double adjustedBpm = seg.bpm_start * m_currentRateRatio;
                        QString labelText = QString::number(
                                adjustedBpm, 'f', m_style.labelDecimalPlaces);
                        QRect textRect = painter.fontMetrics().boundingRect(labelText);
                        int padding = 4;
                        int labelX = static_cast<int>(x + m_style.labelOffset);
                        int labelYint = static_cast<int>(labelY);

                        QRect bgRect(labelX - padding,
                                labelYint - padding,
                                textRect.width() + padding * 2,
                                textRect.height() + padding * 2);
                        // if background rectangle needed
                        // painter.fillRect(bgRect, m_style.labelBackgroundColor);
                        painter.setPen(QPen(m_style.labelTextColor, 1));
                        painter.drawText(labelX, labelYint + textRect.height(), labelText);
                        lastLabelX = x;
                    }
                }
            }
        }
    }

    // Draw track start
    if (m_style.showTrackStart) {
        painter.setPen(QPen(m_style.trackStartColor, m_style.trackStartWidth, Qt::SolidLine));
        double startX = m_waveformRenderer->transformSamplePositionInRendererWorld(
                0.0, ::WaveformRendererAbstract::Play);
        if (startX >= 0 && startX <= width) {
            painter.drawLine(QLineF(startX, 0, startX, height));
        }
    }

    // Draw offset indicator
    if (m_style.showOffsetIndicator && !m_segments.isEmpty() && std::abs(m_offsetSeconds) > 0.001) {
        painter.setPen(QPen(m_style.offsetColor, m_style.offsetWidth, m_style.offsetLineStyle));
        double firstOffsetTime = m_segments[0].position + m_offsetSeconds;
        double firstOffsetPos = firstOffsetTime * (m_trackSamples / m_trackLengthSeconds);
        double offsetX = m_waveformRenderer->transformSamplePositionInRendererWorld(
                firstOffsetPos, ::WaveformRendererAbstract::Play);
        if (offsetX >= 0 && offsetX <= width) {
            painter.drawLine(QLineF(offsetX, 0, offsetX, height));
        }
    }

    painter.end();

    return image;
}

void WaveformRenderBpmCurve::updateBpmTextures() {
    if (showDebugAllshaderWaveformRenderBPMCurve) {
        qDebug() << "[WaveformRenderBpmCurve - Allshader] updateBpmTextures called";
    }

    if (m_segments.isEmpty()) {
        loadBpmCurve();
    }

    if (m_segments.isEmpty()) {
        return;
    }

    QImage bpmImage = drawBpmTexture();

    if (m_pBpmCurveNode) {
        rendergraph::Context* pContext = m_waveformRenderer->getContext();
        if (pContext) {
            m_pBpmCurveNode->updateTexture(pContext, bpmImage);
            m_textureReady = true;
        }
    } else {
        createNode(bpmImage);
        m_textureReady = true;
    }
}

void WaveformRenderBpmCurve::createNode(const QImage& image) {
    if (m_pBpmCurveNode) {
        return;
    }

    m_pendingBpmImage = image;
    if (showDebugAllshaderWaveformRenderBPMCurve) {
        qDebug() << "[WaveformRenderBpmCurve - Allshader] Waiting for context to create node";
    }
}

void WaveformRenderBpmCurve::updateNode() {
    if (!m_pendingBpmImage.isNull()) {
        if (showDebugAllshaderWaveformRenderBPMCurve) {
            qDebug() << "[WaveformRenderBpmCurve - Allshader] Context available, creating node";
        }
        auto pNode = std::make_unique<WaveformBpmCurveNode>(
                m_waveformRenderer->getContext(), m_pendingBpmImage);
        m_pBpmCurveNode = pNode.get();
        m_pBpmCurveNodesParent->appendChildNode(std::move(pNode));
        m_pendingBpmImage = QImage();
        m_needsTextureUpdate = true;
    }

    if (!m_pBpmCurveNode) {
        return;
    }

    if (m_pRateRatioCO) {
        double newRate = m_pRateRatioCO->get();
        if (std::abs(newRate - m_currentRateRatio) > 0.001) {
            m_currentRateRatio = newRate;
            m_needsRangeRecalculation = true;
            m_needsTextureUpdate = true;
        }
    }

    // Update track samples in case of resize
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (pTrack) {
        m_trackSamples = m_waveformRenderer->getTrackSamples();
    }

    QImage fullImage = drawBpmTexture();

    if (fullImage.isNull()) {
        return;
    }

    auto texture = std::make_unique<rendergraph::Texture>(
            m_waveformRenderer->getContext(), fullImage);
    dynamic_cast<rendergraph::TextureMaterial&>(m_pBpmCurveNode->material())
            .setTexture(std::move(texture));
    m_pBpmCurveNode->markDirtyMaterial();

    // Position to cover the entire waveform area
    float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    m_pBpmCurveNode->updatePosition(0, 0, devicePixelRatio);
    m_pBpmCurveNode->markDirtyGeometry();
}

void WaveformRenderBpmCurve::update() {
    if (isSubtreeBlocked()) {
        return;
    }

    // Check if track changed
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    TrackId currentTrackId = pTrack ? pTrack->getId() : TrackId();

    if (currentTrackId != m_lastTrackId) {
        m_lastTrackId = currentTrackId;
        m_segments.clear();
        m_needsTextureUpdate = true;
        if (pTrack) {
            loadBpmCurve();
        }
    }

    // Load BPM curve if not loaded yet
    if (m_segments.isEmpty() && pTrack) {
        loadBpmCurve();
    }

    // Update track samples
    if (pTrack) {
        m_trackSamples = m_waveformRenderer->getTrackSamples();
    }

    // Create initial pending image if needed
    if (!m_pBpmCurveNode && m_pendingBpmImage.isNull() && !m_segments.isEmpty()) {
        QImage bpmImage = drawBpmTexture();
        if (!bpmImage.isNull()) {
            createNode(bpmImage);
        }
    }

    updateNode();
}

} // namespace allshader
