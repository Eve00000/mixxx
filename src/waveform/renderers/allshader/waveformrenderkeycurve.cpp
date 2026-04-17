
#include "waveform/renderers/allshader/waveformrenderkeycurve.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
#include <QStringView>
#include <cmath>

#include "moc_waveformrenderkeycurve.cpp"
#include "rendergraph/context.h"
#include "rendergraph/geometry.h"
#include "rendergraph/geometrynode.h"
#include "rendergraph/material/texturematerial.h"
#include "rendergraph/texture.h"
#include "rendergraph/vertexupdaters/texturedvertexupdater.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"

using namespace rendergraph;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class WaveformKeyCurveNode : public rendergraph::GeometryNode {
  public:
    WaveformKeyCurveNode(rendergraph::Context* pContext,
            const QImage& image) {
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

constexpr double WHEEL_START_ANGLE = 45.0;
constexpr double SLICE_ANGLE = 30.0;
constexpr int NUM_SLICES = 12;
constexpr bool showDebugAllshaderWaveformRenderKeyCurve = false;

WaveformRenderKeyCurve::WaveformRenderKeyCurve(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : WaveformRenderKeyCurveBase(waveformWidget, false),
          m_segments(),
          m_lancelotLayout(),
          m_style(),
          m_animationTimer(),
          m_currentKey(),
          m_currentLancelot(),
          m_baseLancelot(),
          m_transposedLancelot(),
          m_transposedMusicalKey(),
          m_transposedWheelKey(),
          m_currentWheelKey(),
          m_pKeyCurveNode(nullptr),
          m_pKeyCurveNodesParent(nullptr),
          m_pPlayPositionCO(nullptr),
          m_pRateRatioCO(nullptr),
          m_pKeyControlCO(nullptr),
          m_pKeylockCO(nullptr),
          m_playPosition(0.0),
          m_currentRateRatio(1.0),
          m_currentKeyShift(0.0),
          m_currentPlayPosition(0.0),
          m_trackLengthSeconds(0.0),
          m_wheelSize(120),
          m_wheelMargin(10),
          m_currentTotalOffset(0),
          m_visible(true),
          m_keylockEnabled(false),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
          m_textureReady(false),
          m_lastTrackId() {
    m_animationTimer.start();
    m_style = KeyCurveStyle();

    auto pNode = std::make_unique<rendergraph::Node>();
    m_pKeyCurveNodesParent = pNode.get();
    appendChildNode(std::move(pNode));

    if (!m_waveformRenderer->getGroup().isEmpty()) {
        m_pPlayPositionCO = std::make_unique<ControlProxy>(
                m_waveformRenderer->getGroup(), "playposition");
        m_playPosition = m_pPlayPositionCO->get();
        m_pKeylockCO = std::make_unique<ControlProxy>(
                m_waveformRenderer->getGroup(), "keylock");
        m_pRateRatioCO = std::make_unique<ControlProxy>(
                m_waveformRenderer->getGroup(), "rate_ratio");
        m_pKeyControlCO = std::make_unique<ControlProxy>(
                m_waveformRenderer->getGroup(), "pitch");
    }
    initLancelotLayout();
}

void WaveformRenderKeyCurve::initLancelotLayout() {
    m_lancelotLayout.clear();

    QStringList majorLancelots = {"1B",
            "2B",
            "3B",
            "4B",
            "5B",
            "6B",
            "7B",
            "8B",
            "9B",
            "10B",
            "11B",
            "12B"};
    QStringList minorLancelots = {"1A",
            "2A",
            "3A",
            "4A",
            "5A",
            "6A",
            "7A",
            "8A",
            "9A",
            "10A",
            "11A",
            "12A"};

    for (int i = 0; i < NUM_SLICES; ++i) {
        LancelotKey key;
        key.lancelot = majorLancelots[i];
        double angle = WHEEL_START_ANGLE - (i * SLICE_ANGLE);
        while (angle < 0.0)
            angle += 360.0;
        while (angle >= 360.0)
            angle -= 360.0;
        key.angle = angle;
        key.isMinor = false;
        m_lancelotLayout.append(key);
    }

    for (int i = 0; i < NUM_SLICES; ++i) {
        LancelotKey key;
        key.lancelot = minorLancelots[i];
        double angle = WHEEL_START_ANGLE - (i * SLICE_ANGLE);
        while (angle < 0.0)
            angle += 360.0;
        while (angle >= 360.0)
            angle -= 360.0;
        key.angle = angle;
        key.isMinor = true;
        m_lancelotLayout.append(key);
    }
}

void WaveformRenderKeyCurve::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderKeyCurve::setup(const QDomNode& node, const SkinContext& skinContext) {
    Q_UNUSED(node);
    Q_UNUSED(skinContext);
    WaveformRenderKeyCurveBase::setup(node, skinContext);
}

bool WaveformRenderKeyCurve::init() {
    if (showDebugAllshaderWaveformRenderKeyCurve) {
        qDebug() << "[WaveformRenderKeyCurve - Allshader] init called";
    }
    return true;
}

bool WaveformRenderKeyCurve::isSubtreeBlocked() const {
    return m_isSlipRenderer && !m_waveformRenderer->isSlipActive();
}

QString WaveformRenderKeyCurve::keyToLancelot(const QString& key) const {
    QString normalizedKey = key;

    // combined keys (e.g., "D#m/Ebm") -> take first part
    if (normalizedKey.contains('/')) {
        QStringList parts = normalizedKey.split('/');
        if (!parts.isEmpty()) {
            normalizedKey = parts[0];
        }
    }

    // Convert Unicode to ASCII
    normalizedKey.replace(QChar(0x266D), 'b');
    normalizedKey.replace(QChar(0x266F), '#');
    normalizedKey.replace(QChar(0x266E), "");

    if (m_style.wheelType == KeyCurveStyle::WHEEL_MIXXX) {
        // Mixxx keywheel mapping
        static QMap<QString, QString> majorToLancelot = {{"C", "12B"},
                {"G", "1B"},
                {"D", "2B"},
                {"A", "3B"},
                {"E", "4B"},
                {"B", "5B"},
                {"F#", "6B"},
                {"Gb", "6B"},
                {"Db", "7B"},
                {"Ab", "8B"},
                {"Eb", "9B"},
                {"Bb", "10B"},
                {"F", "11B"}};

        static QMap<QString, QString> minorToLancelot = {{"Am", "12A"},
                {"Em", "1A"},
                {"Bm", "2A"},
                {"F#m", "3A"},
                {"C#m", "4A"},
                {"G#m", "5A"},
                {"D#m", "6A"},
                {"Ebm", "6A"},
                {"Bbm", "7A"},
                {"Fm", "8A"},
                {"Cm", "9A"},
                {"Gm", "10A"},
                {"Dm", "11A"}};

        if (normalizedKey.contains('m')) {
            return minorToLancelot.value(normalizedKey);
        } else {
            return majorToLancelot.value(normalizedKey);
        }
    } else {
        // Standard Camelot wheel mapping
        static QMap<QString, QString> majorToLancelot = {{"B", "1B"},
                {"F#", "2B"},
                {"Gb", "2B"},
                {"C#", "3B"},
                {"Db", "3B"},
                {"Ab", "4B"},
                {"G#", "4B"},
                {"Eb", "5B"},
                {"D#", "5B"},
                {"Bb", "6B"},
                {"A#", "6B"},
                {"F", "7B"},
                {"C", "8B"},
                {"G", "9B"},
                {"D", "10B"},
                {"A", "11B"},
                {"E", "12B"}};

        static QMap<QString, QString> minorToLancelot = {{"G#m", "1A"},
                {"Abm", "1A"},
                {"D#m", "2A"},
                {"Ebm", "2A"},
                {"A#m", "3A"},
                {"Bbm", "3A"},
                {"Fm", "4A"},
                {"Cm", "5A"},
                {"Gm", "6A"},
                {"Dm", "7A"},
                {"Am", "8A"},
                {"Em", "9A"},
                {"Bm", "10A"},
                {"F#m", "11A"},
                {"C#m", "12A"}};

        if (normalizedKey.contains('m')) {
            return minorToLancelot.value(normalizedKey);
        } else {
            return majorToLancelot.value(normalizedKey);
        }
    }
}

void WaveformRenderKeyCurve::updateCurrentKey() {
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack) {
        if (showDebugAllshaderWaveformRenderKeyCurve) {
            qDebug() << "[WaveformRenderKeyCurve - Allshader] No track info";
        }
        return;
    }

    // Load key curve if not loaded yet
    // if (m_segments.isEmpty()) {
    if (!m_segmentsLoaded && m_segments.isEmpty()) {
        if (showDebugAllshaderWaveformRenderKeyCurve) {
            qDebug() << "[WaveformRenderKeyCurve - Allshader] Loading key curve...";
        }
        QString trackIdStr = pTrack->getId().toString();
        QString keyDir = QStandardPaths::writableLocation(
                                 QStandardPaths::AppLocalDataLocation) +
                "/keycurve/";
        QString jsonPath = keyDir + trackIdStr + ".json";

        QFile file(jsonPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray jsonData = file.readAll();
            file.close();

            QJsonDocument doc = QJsonDocument::fromJson(jsonData);
            if (doc.isObject()) {
                QJsonObject rootObj = doc.object();
                QJsonArray keyArray = rootObj["key_curve"].toArray();

                for (const QJsonValue& val : keyArray) {
                    if (!val.isObject()) {
                        continue;
                    }
                    QJsonObject obj = val.toObject();
                    KeySegment seg;
                    seg.startTime = obj["position"].toDouble();
                    seg.endTime = obj["range_end"].toDouble();
                    seg.key = obj["key"].toString();
                    seg.confidence = obj["confidence"].toDouble();
                    m_segments.append(seg);
                }
                if (showDebugAllshaderWaveformRenderKeyCurve) {
                    qDebug() << "[WaveformRenderKeyCurve - Allshader] Loaded"
                             << m_segments.size() << "segments";
                }
            }
        } else {
            if (showDebugAllshaderWaveformRenderKeyCurve) {
                qDebug() << "[WaveformRenderKeyCurve - Allshader] Cannot open file:" << jsonPath;
            }
        }
        m_segmentsLoaded = true;
    }

    // Update track duration
    if (m_trackLengthSeconds == 0.0) {
        m_trackLengthSeconds = pTrack->getDuration();
        if (showDebugAllshaderWaveformRenderKeyCurve) {
            qDebug() << "[WaveformRenderKeyCurve - Allshader] Track duration:"
                     << m_trackLengthSeconds;
        }
    }

    // Update play position
    if (m_pPlayPositionCO) {
        m_playPosition = m_pPlayPositionCO->get();
    }

    // Find current key based on play position
    double positionSeconds = m_playPosition * m_trackLengthSeconds;
    for (const auto& seg : std::as_const(m_segments)) {
        if (positionSeconds >= seg.startTime && positionSeconds <= seg.endTime) {
            if (m_currentKey != seg.key) {
                if (showDebugAllshaderWaveformRenderKeyCurve) {
                    qDebug() << "[WaveformRenderKeyCurve - Allshader] Key changed from"
                             << m_currentKey << "to" << seg.key << "at"
                             << positionSeconds << "s";
                }
                m_currentKey = seg.key;
                m_currentLancelot = keyToLancelot(seg.key);

                m_currentWheelKey = seg.key;
                m_baseLancelot = keyToLancelot(seg.key);

                updateTransposedKey();
            }
            break;
        }
    }

    // set initial values if this is the first time
    if (!m_segments.isEmpty() && m_currentWheelKey.isEmpty()) {
        m_currentWheelKey = m_segments[0].key;
        m_baseLancelot = keyToLancelot(m_currentWheelKey);
        updateTransposedKey();
    }
}

QString WaveformRenderKeyCurve::transposeKey(const QString& key, int semitones) const {
    static QStringList majorKeys =
            {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static QStringList minorKeys =
            {"Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm"};

    // Normalize enharmonic equivalents
    QString normalizedKey = key;
    if (normalizedKey == "Bbm") {
        normalizedKey = "A#m";
    }
    if (normalizedKey == "Ebm") {
        normalizedKey = "D#m";
    }
    if (normalizedKey == "Abm") {
        normalizedKey = "G#m";
    }
    if (normalizedKey == "Db") {
        normalizedKey = "C#";
    }
    if (normalizedKey == "Gb") {
        normalizedKey = "F#";
    }
    if (normalizedKey == "Bb") {
        normalizedKey = "A#";
    }
    if (normalizedKey == "Eb") {
        normalizedKey = "D#";
    }
    if (normalizedKey == "Ab") {
        normalizedKey = "G#";
    }

    bool isMinor = normalizedKey.contains('m');
    const QStringList& keys = isMinor ? minorKeys : majorKeys;

    int index = keys.indexOf(normalizedKey);
    if (index == -1) {
        if (showDebugAllshaderWaveformRenderKeyCurve) {
            qDebug() << "[WaveformRenderKeyCurve - Allshader] transposeKey - "
                        "key not found:"
                     << normalizedKey;
        }
        return key;
    }

    int newIndex = (index + semitones) % 12;
    if (newIndex < 0) {
        newIndex += 12;
    }

    QString result = keys[newIndex];
    if (showDebugAllshaderWaveformRenderKeyCurve) {
        qDebug() << "[WaveformRenderKeyCurve - Allshader] transposeKey:" << normalizedKey
                 << "+" << semitones << "=" << result;
    }

    return result;
}

void WaveformRenderKeyCurve::updateTransposedKey() {
    if (m_baseLancelot.isEmpty() || m_currentWheelKey.isEmpty())
        return;

    if (m_pKeylockCO)
        m_keylockEnabled = (m_pKeylockCO->get() > 0.5);

    double pitchSemitones = m_keylockEnabled ? 0.0 : 12.0 * log2(m_currentRateRatio);
    int totalSemitones = static_cast<int>(std::round(pitchSemitones + m_currentKeyShift));
    m_currentTotalOffset = totalSemitones;

    calculateTransposedValues(totalSemitones);

    if (showDebugAllshaderWaveformRenderKeyCurve) {
        qDebug() << "[WaveformRenderKeyCurve] Keylock:" << m_keylockEnabled
                 << "Semitones:" << totalSemitones
                 << "Original:" << m_currentWheelKey << "(" << m_baseLancelot << ")"
                 << "-> Flashing:" << m_transposedLancelot
                 << "Display:" << m_transposedMusicalKey;
    }
}

void WaveformRenderKeyCurve::getVisibleRange(
        double& startSample, double& endSample, double& width, double& height) {
    width = static_cast<float>(m_waveformRenderer->getWidth());
    height = static_cast<float>(m_waveformRenderer->getBreadth());

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (pTrack) {
        double trackSamples = m_waveformRenderer->getTrackSamples();
        double firstDisplayedPosition = m_waveformRenderer->getFirstDisplayedPosition();
        double lastDisplayedPosition = m_waveformRenderer->getLastDisplayedPosition();

        startSample = firstDisplayedPosition * trackSamples;
        endSample = lastDisplayedPosition * trackSamples;
    }
}

QImage WaveformRenderKeyCurve::createFullImage() {
    float width = static_cast<float>(m_waveformRenderer->getWidth());
    float height = static_cast<float>(m_waveformRenderer->getBreadth());

    QImage image(static_cast<int>(width),
            static_cast<int>(height),
            QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    drawKeyMarkers(painter, width, height);
    drawKeyLabels(painter, width, height);
    drawCamelotWheelComponents(painter, width, height);

    painter.end();

    return image;
}

void WaveformRenderKeyCurve::drawKeyMarkers(QPainter& painter, float width, float height) {
    if (!m_style.showMarkers)
        return;
    if (m_segments.isEmpty())
        return;

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack)
        return;

    double trackLengthSeconds = pTrack->getDuration();
    double trackSamples = m_waveformRenderer->getTrackSamples();
    double startSample, endSample, dummy;
    getVisibleRange(startSample, endSample, dummy, dummy);

    painter.setPen(QPen(m_style.markerColor, m_style.markerWidth, m_style.markerLineStyle));

    for (const auto& seg : std::as_const(m_segments)) {
        double startPos = seg.startTime * (trackSamples / trackLengthSeconds);

        if (startPos >= startSample && startPos <= endSample) {
            double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                    startPos, ::WaveformRendererAbstract::Play);

            if (x >= 0 && x <= width) {
                painter.drawLine(QLineF(x, 0, x, height));

                if (m_style.showDiamonds) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(m_style.markerColor);
                    QPolygonF diamond;
                    diamond << QPointF(x, 4) << QPointF(x + 4, 0)
                            << QPointF(x, -4) << QPointF(x - 4, 0);
                    painter.drawPolygon(diamond);
                    painter.setPen(QPen(m_style.markerColor,
                            m_style.markerWidth,
                            m_style.markerLineStyle));
                }
            }
        }
    }
}

void WaveformRenderKeyCurve::drawKeyLabels(QPainter& painter, float width, float height) {
    if (!m_style.showLabels)
        return;
    if (m_segments.isEmpty())
        return;

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack)
        return;

    double trackLengthSeconds = pTrack->getDuration();
    double trackSamples = m_waveformRenderer->getTrackSamples();
    double startSample, endSample, dummy;
    getVisibleRange(startSample, endSample, dummy, dummy);

    QFont labelFont = painter.font();
    labelFont.setPointSize(m_style.fontSize);
    labelFont.setBold(true);
    painter.setFont(labelFont);

    const double minLabelSpacing = 400;
    double lastLabelX = -minLabelSpacing;
    const double labelY = 5;

    for (const auto& seg : std::as_const(m_segments)) {
        if (seg.confidence < 50.0)
            continue;

        double startPos = seg.startTime * (trackSamples / trackLengthSeconds);

        if (startPos < startSample || startPos > endSample)
            continue;

        double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                startPos, ::WaveformRendererAbstract::Play);

        if (x >= 0 && x <= width && (x - lastLabelX) >= minLabelSpacing) {
            QString transposedKey = (m_currentTotalOffset == 0)
                    ? seg.key
                    : transposeKey(seg.key, m_currentTotalOffset);

            // Convert to preferred spelling
            if (transposedKey == "A#m") {
                transposedKey = "Bbm";
            }
            if (transposedKey == "D#m") {
                transposedKey = "Ebm";
            }
            if (transposedKey == "G#m") {
                transposedKey = "Abm";
            }
            if (transposedKey == "C#") {
                transposedKey = "Db";
            }
            if (transposedKey == "F#") {
                transposedKey = "Gb";
            }
            if (transposedKey == "A#") {
                transposedKey = "Bb";
            }
            if (transposedKey == "D#") {
                transposedKey = "Eb";
            }
            if (transposedKey == "G#") {
                transposedKey = "Ab";
            }

            QRect textRect = painter.fontMetrics().boundingRect(transposedKey);
            int padding = 4;

            painter.setPen(QPen(getColorForKey(transposedKey), 1));
            painter.drawText(static_cast<int>(x + 5),
                    static_cast<int>(labelY + textRect.height()),
                    transposedKey);

            lastLabelX = x;
        }
    }
}

void WaveformRenderKeyCurve::drawWheelSlices(
        QPainter& painter, const QRectF& rect, const QRectF& innerRect) {
    // Draw outer slices (Majors)
    for (const auto& key : std::as_const(m_lancelotLayout)) {
        if (!key.isMinor) {
            double startAngle = key.angle;
            double endAngle = startAngle + 30;
            double spanAngle = endAngle - startAngle;
            int start = static_cast<int>(startAngle * 16);
            int span = static_cast<int>(spanAngle * 16);

            painter.setPen(QPen(QColor(200, 200, 200, 150), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPie(rect, start, span);
        }
    }

    // Draw inner slices (Minors)
    for (const auto& key : std::as_const(m_lancelotLayout)) {
        if (key.isMinor) {
            double startAngle = key.angle;
            double endAngle = startAngle + 30;
            double spanAngle = endAngle - startAngle;
            int start = static_cast<int>(startAngle * 16);
            int span = static_cast<int>(spanAngle * 16);

            painter.setPen(QPen(QColor(200, 200, 200, 150), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPie(innerRect, start, span);
        }
    }
}

void WaveformRenderKeyCurve::drawHighlightedSlice(
        QPainter& painter, const QRectF& rect, const QRectF& innerRect) {
    QString currentLancelotForDisplay = m_transposedLancelot.isEmpty()
            ? m_baseLancelot
            : m_transposedLancelot;
    if (currentLancelotForDisplay.isEmpty())
        return;

    for (const auto& key : std::as_const(m_lancelotLayout)) {
        if (key.lancelot == currentLancelotForDisplay) {
            double startAngle = key.angle;
            int start = static_cast<int>(startAngle * 16);
            int span = static_cast<int>(30 * 16);

            QRectF drawRect = key.isMinor ? innerRect : rect;

            double time = m_animationTimer.elapsed() / 1000.0;
            int alpha = 100 + static_cast<int>(155 * sin(time * 8));

            painter.setPen(QPen(QColor(255, 255, 255, alpha), 2));
            painter.setBrush(QBrush(QColor(255, 255, 255, alpha / 2)));
            painter.drawPie(drawRect, start, span);
            break;
        }
    }
}

void WaveformRenderKeyCurve::drawWheelText(
        QPainter& painter, int wheelX, int wheelY, int wheelSize) {
    QString displayKey;
    QStringList parts;
    if (!m_transposedMusicalKey.isEmpty() && !m_transposedLancelot.isEmpty()) {
        if (normalizeKeyDisplay(m_transposedMusicalKey).contains('/')) {
            parts = normalizeKeyDisplay(m_transposedMusicalKey).split('/');
            if (!parts.isEmpty()) {
                displayKey = parts[0] + " (" + m_transposedLancelot + ")";
            }
        } else {
            displayKey = normalizeKeyDisplay(m_transposedMusicalKey) + " (" +
                    m_transposedLancelot + ")";
        }
    } else if (!m_currentWheelKey.isEmpty() && !m_baseLancelot.isEmpty()) {
        if (normalizeKeyDisplay(m_currentWheelKey).contains('/')) {
            parts = normalizeKeyDisplay(m_currentWheelKey).split('/');
            if (!parts.isEmpty()) {
                displayKey = parts[0] + " (" + m_baseLancelot + ")";
            }
        } else {
            displayKey = normalizeKeyDisplay(m_currentWheelKey) + " (" + m_baseLancelot + ")";
        }
    }

    QFont font = painter.font();
    font.setPointSize(static_cast<int>(m_waveformRenderer->getHeight() * 0.1));
    painter.setFont(font);
    painter.setPen(QPen(QColor(255, 255, 255, 200), 1));
    painter.drawText(QRect(wheelX, wheelY + wheelSize + 5, wheelSize, 20),
            Qt::AlignCenter,
            displayKey);
}

void WaveformRenderKeyCurve::drawCamelotWheelComponents(
        QPainter& painter, float width, float height) {
    int wheelSize = static_cast<int>(height * 0.7);
    if (wheelSize < 60)
        wheelSize = 60;

    int wheelX = static_cast<int>(width - wheelSize - m_wheelMargin);
    int wheelY = m_wheelMargin;

    QRectF rect(wheelX, wheelY, wheelSize, wheelSize);
    QPointF center = rect.center();

    // Draw outer circle
    painter.setPen(QPen(QColor(150, 150, 150, 200), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(rect);

    // Draw inner circle
    double innerRadius = rect.width() * 0.35;
    QRectF innerRect(center.x() - innerRadius,
            center.y() - innerRadius,
            innerRadius * 2,
            innerRadius * 2);
    painter.setPen(QPen(QColor(150, 150, 150, 150), 1, Qt::DashLine));
    painter.drawEllipse(innerRect);

    drawWheelSlices(painter, rect, innerRect);
    drawHighlightedSlice(painter, rect, innerRect);
    drawWheelText(painter, wheelX, wheelY, wheelSize);
}

void WaveformRenderKeyCurve::calculateTransposedValues(int totalSemitones) {
    if (m_baseLancelot.isEmpty() || m_currentWheelKey.isEmpty())
        return;

    // Convert semitones to Camelot wheel steps
    int wheelSteps = (totalSemitones * 7) % 12;
    if (wheelSteps < 0)
        wheelSteps += 12;

    // Apply to Lancelot number
    int currentNumber = QStringView(m_baseLancelot).left(m_baseLancelot.length() - 1).toInt();
    bool isMinor = m_baseLancelot.endsWith("A");

    int newNumber = currentNumber + wheelSteps;
    while (newNumber < 1)
        newNumber += 12;
    while (newNumber > 12)
        newNumber -= 12;

    m_transposedLancelot = QString::number(newNumber) + (isMinor ? "A" : "B");
    m_transposedMusicalKey = transposeKey(m_currentWheelKey, totalSemitones);

    // Convert back to preferred spelling
    if (m_transposedMusicalKey == "A#m") {
        m_transposedMusicalKey = "Bbm";
    }
    if (m_transposedMusicalKey == "D#m") {
        m_transposedMusicalKey = "Ebm";
    }
    if (m_transposedMusicalKey == "G#m") {
        m_transposedMusicalKey = "Abm";
    }
    if (m_transposedMusicalKey == "C#") {
        m_transposedMusicalKey = "Db";
    }
    if (m_transposedMusicalKey == "F#") {
        m_transposedMusicalKey = "Gb";
    }
    if (m_transposedMusicalKey == "A#") {
        m_transposedMusicalKey = "Bb";
    }
    if (m_transposedMusicalKey == "D#") {
        m_transposedMusicalKey = "Eb";
    }
    if (m_transposedMusicalKey == "G#") {
        m_transposedMusicalKey = "Ab";
    }
}

QString WaveformRenderKeyCurve::normalizeKeyDisplay(const QString& key) const {
    QString normalized = key;
    normalized.replace(QChar(0x266D), 'b');
    normalized.replace(QChar(0x266F), '#');
    return normalized;
}

void WaveformRenderKeyCurve::updateKeyTextures() {
    // not used
}

QColor WaveformRenderKeyCurve::getColorForKey(const QString& key) const {
    int hash = 0;
    for (QChar ch : key) {
        hash += ch.unicode();
    }
    return QColor::fromHsv(hash % 360, 200, 200);
}

void WaveformRenderKeyCurve::createNode(const QImage& image) {
    if (m_pKeyCurveNode) {
        return;
    }

    m_pendingWheelImage = image;
    if (showDebugAllshaderWaveformRenderKeyCurve) {
        qDebug() << "[WaveformRenderKeyCurve - Allshader] Waiting for context to create node";
    }
}

void WaveformRenderKeyCurve::updateNode() {
    if (!m_pendingWheelImage.isNull()) {
        if (showDebugAllshaderWaveformRenderKeyCurve) {
            qDebug() << "[WaveformRenderKeyCurve - Allshader] Context available, creating node";
        }
        auto pNode = std::make_unique<WaveformKeyCurveNode>(
                m_waveformRenderer->getContext(), m_pendingWheelImage);
        m_pKeyCurveNode = pNode.get();
        m_pKeyCurveNodesParent->appendChildNode(std::move(pNode));
        m_pendingWheelImage = QImage();
    }

    if (!m_pKeyCurveNode)
        return;

    // Poll values
    if (m_pRateRatioCO) {
        m_currentRateRatio = m_pRateRatioCO->get();
    }
    if (m_pKeyControlCO) {
        m_currentKeyShift = m_pKeyControlCO->get();
    }
    if (m_pKeylockCO) {
        m_keylockEnabled = (m_pKeylockCO->get() > 0.5);
    }

    float width = static_cast<float>(m_waveformRenderer->getWidth());
    float height = static_cast<float>(m_waveformRenderer->getBreadth());
    if (width <= 0 || height <= 0)
        return;

    // Calculate current offset
    double pitchSemitones = m_keylockEnabled ? 0.0 : 12.0 * log2(m_currentRateRatio);
    int totalSemitones = static_cast<int>(std::round(pitchSemitones + m_currentKeyShift));
    m_currentTotalOffset = totalSemitones;

    calculateTransposedValues(totalSemitones);

    // Create image and draw
    QImage fullImage(static_cast<int>(width),
            static_cast<int>(height),
            QImage::Format_ARGB32_Premultiplied);
    fullImage.fill(Qt::transparent);

    QPainter painter(&fullImage);
    painter.setRenderHint(QPainter::Antialiasing);

    drawKeyMarkers(painter, width, height);
    drawKeyLabels(painter, width, height);
    drawCamelotWheelComponents(painter, width, height);

    painter.end();

    // Update texture
    auto texture = std::make_unique<rendergraph::Texture>(
            m_waveformRenderer->getContext(), fullImage);
    dynamic_cast<rendergraph::TextureMaterial&>(m_pKeyCurveNode->material())
            .setTexture(std::move(texture));
    m_pKeyCurveNode->markDirtyMaterial();

    // Update position
    float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    m_pKeyCurveNode->updatePosition(0, 0, devicePixelRatio);
    m_pKeyCurveNode->markDirtyGeometry();
}

void WaveformRenderKeyCurve::update() {
    if (isSubtreeBlocked()) {
        return;
    }

    // Check if track changed
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    TrackId currentTrackId = pTrack ? pTrack->getId() : TrackId();

    if (currentTrackId != m_lastTrackId) {
        m_lastTrackId = currentTrackId;

        // Clear all track-specific data
        m_segments.clear();
        m_currentKey.clear();
        m_currentWheelKey.clear();
        m_baseLancelot.clear();
        m_transposedLancelot.clear();
        m_transposedMusicalKey.clear();
        m_segmentsLoaded = false;

        if (pTrack) {
            // Force reload of new track's key curve
            updateCurrentKey(); // This will reload segments and set initial key values

            // Force immediate wheel update
            if (m_pKeyCurveNode) {
                // QImage wheelImage = drawCamelotWheel();
                QImage wheelImage = createFullImage();
                rendergraph::Context* pContext = m_waveformRenderer->getContext();
                if (pContext) {
                    m_pKeyCurveNode->updateTexture(pContext, wheelImage);
                    m_pKeyCurveNode->markDirtyMaterial();
                }
            }
        }
    }

    // Update play position and current key (only if track is loaded)
    if (pTrack && !m_segments.isEmpty() && m_pPlayPositionCO) {
        double newPosition = m_pPlayPositionCO->get();
        if (std::abs(newPosition - m_playPosition) > 0.001) {
            m_playPosition = newPosition;
            updateCurrentKey();
        }
    }

    // Create initial pending image if needed
    if (!m_pKeyCurveNode && m_pendingWheelImage.isNull() && !m_segments.isEmpty()) {
        // QImage wheelImage = drawCamelotWheel();
        QImage wheelImage = createFullImage();
        createNode(wheelImage);
    }

    updateNode();
}

} // namespace allshader
