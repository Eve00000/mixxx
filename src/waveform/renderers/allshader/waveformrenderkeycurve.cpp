
#include "waveform/renderers/allshader/waveformrenderkeycurve.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
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

// Define the node class FIRST, outside any namespace
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

WaveformRenderKeyCurve::WaveformRenderKeyCurve(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : WaveformRenderKeyCurveBase(waveformWidget, false),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
          m_pKeyCurveNode(nullptr),
          m_trackLengthSeconds(0.0),
          m_playPosition(0.0),
          m_pRateRatioCO(nullptr),
          m_pKeyControlCO(nullptr),
          m_pKeylockCO(nullptr),
          m_segments(),
          m_lancelotLayout(),
          m_baseLancelot(),
          m_transposedLancelot(),
          m_transposedMusicalKey(),
          m_transposedWheelKey(),
          m_currentWheelKey(),
          m_animationTimer(),
          m_currentRateRatio(1.0),
          m_currentKeyShift(0.0),
          m_currentPlayPosition(0.0),
          m_wheelSize(120),
          m_wheelMargin(10),
          m_currentTotalOffset(0),
          m_visible(true),
          m_keylockEnabled(false) {
    m_animationTimer.start();

    // Create parent node for key curve nodes (like WaveformRenderMark does)
    auto pNode = std::make_unique<rendergraph::Node>();
    m_pKeyCurveNodesParent = pNode.get();
    appendChildNode(std::move(pNode));

    // Initialize control proxy for play position
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
    qDebug() << "[WaveformRenderKeyCurve - Allshader] init called";
    return true;
}

bool WaveformRenderKeyCurve::isSubtreeBlocked() const {
    return m_isSlipRenderer && !m_waveformRenderer->isSlipActive();
}

QString WaveformRenderKeyCurve::keyToLancelot(const QString& key) const {
    QString normalizedKey = key;

    // Handle compound keys (e.g., "D#m/Ebm") -> take first part
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
    // Get track info
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack) {
        qDebug() << "[WaveformRenderKeyCurve - Allshader] No track info";
        return;
    }

    // Load key curve if not loaded yet
    if (m_segments.isEmpty()) {
        qDebug() << "[WaveformRenderKeyCurve - Allshader] Loading key curve...";
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
                    if (!val.isObject())
                        continue;
                    QJsonObject obj = val.toObject();
                    KeySegment seg;
                    seg.startTime = obj["position"].toDouble();
                    seg.endTime = obj["range_end"].toDouble();
                    seg.key = obj["key"].toString();
                    seg.confidence = obj["confidence"].toDouble();
                    m_segments.append(seg);
                }
                qDebug() << "[WaveformRenderKeyCurve - Allshader] Loaded"
                         << m_segments.size() << "segments";
            }
        } else {
            qDebug() << "[WaveformRenderKeyCurve - Allshader] Cannot open file:" << jsonPath;
        }
    }

    // Update track duration
    if (m_trackLengthSeconds == 0.0) {
        m_trackLengthSeconds = pTrack->getDuration();
        qDebug() << "[WaveformRenderKeyCurve - Allshader] Track duration:" << m_trackLengthSeconds;
    }

    // Update play position
    if (m_pPlayPositionCO) {
        m_playPosition = m_pPlayPositionCO->get();
    }

    // Find current key based on play position
    double positionSeconds = m_playPosition * m_trackLengthSeconds;
    for (const auto& seg : m_segments) {
        if (positionSeconds >= seg.startTime && positionSeconds <= seg.endTime) {
            if (m_currentKey != seg.key) {
                qDebug() << "[WaveformRenderKeyCurve - Allshader] Key changed "
                            "from"
                         << m_currentKey << "to" << seg.key << "at"
                         << positionSeconds << "s";
                m_currentKey = seg.key;
                m_currentLancelot = keyToLancelot(seg.key);

                // CRITICAL: Also set these for the wheel display
                m_currentWheelKey = seg.key;
                m_baseLancelot = keyToLancelot(seg.key);

                // Update transposed key for rate/pitch changes
                updateTransposedKey();
            }
            break;
        }
    }

    // Also set initial values if this is the first time
    if (!m_segments.isEmpty() && m_currentWheelKey.isEmpty()) {
        m_currentWheelKey = m_segments[0].key;
        m_baseLancelot = keyToLancelot(m_currentWheelKey);
        updateTransposedKey();
    }
}

QString WaveformRenderKeyCurve::transposeKey(const QString& key, int semitones) const {
    static QStringList majorKeys = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static QStringList minorKeys = {"Cm",
            "C#m",
            "Dm",
            "D#m",
            "Em",
            "Fm",
            "F#m",
            "Gm",
            "G#m",
            "Am",
            "A#m",
            "Bm"};

    // Normalize enharmonic equivalents
    QString normalizedKey = key;
    if (normalizedKey == "Bbm")
        normalizedKey = "A#m";
    if (normalizedKey == "Ebm")
        normalizedKey = "D#m";
    if (normalizedKey == "Abm")
        normalizedKey = "G#m";
    if (normalizedKey == "Db")
        normalizedKey = "C#";
    if (normalizedKey == "Gb")
        normalizedKey = "F#";
    if (normalizedKey == "Bb")
        normalizedKey = "A#";
    if (normalizedKey == "Eb")
        normalizedKey = "D#";
    if (normalizedKey == "Ab")
        normalizedKey = "G#";

    bool isMinor = normalizedKey.contains('m');
    const QStringList& keys = isMinor ? minorKeys : majorKeys;

    int index = keys.indexOf(normalizedKey);
    if (index == -1) {
        qDebug() << "[WaveformRenderKeyCurve] transposeKey - key not found:" << normalizedKey;
        return key;
    }

    int newIndex = (index + semitones) % 12;
    if (newIndex < 0)
        newIndex += 12;

    QString result = keys[newIndex];
    qDebug() << "[WaveformRenderKeyCurve] transposeKey:" << normalizedKey
             << "+" << semitones << "=" << result;

    return result;
}

void WaveformRenderKeyCurve::updateTransposedKey() {
    if (m_baseLancelot.isEmpty() || m_currentWheelKey.isEmpty()) {
        qDebug() << "[WaveformRenderKeyCurve] updateTransposedKey - missing data";
        return;
    }

    // Poll keylock status
    if (m_pKeylockCO) {
        m_keylockEnabled = (m_pKeylockCO->get() > 0.5);
    }

    // Calculate semitone offset from rate ratio (only if keylock is OFF)
    double pitchSemitones = 0.0;
    if (!m_keylockEnabled) {
        pitchSemitones = 12.0 * log2(m_currentRateRatio);
    }

    int totalSemitones = static_cast<int>(std::round(pitchSemitones + m_currentKeyShift));
    m_currentTotalOffset = totalSemitones;

    // Convert semitones to Camelot wheel steps
    int wheelSteps = (totalSemitones * 7) % 12;
    if (wheelSteps < 0)
        wheelSteps += 12;

    // Apply to Lancelot number
    // int currentNumber = m_baseLancelot.left(m_baseLancelot.length() - 1).toInt();
    int currentNumber = QStringView(m_baseLancelot).left(m_baseLancelot.length() - 1).toInt();
    bool isMinor = m_baseLancelot.endsWith("A");

    int newNumber = currentNumber + wheelSteps;
    while (newNumber < 1)
        newNumber += 12;
    while (newNumber > 12)
        newNumber -= 12;

    m_transposedLancelot = QString::number(newNumber) + (isMinor ? "A" : "B");

    // Transpose the musical key by totalSemitones (chromatic)
    qDebug() << "[WaveformRenderKeyCurve] Before transpose - key:" << m_currentWheelKey
             << "semitones:" << totalSemitones;

    m_transposedMusicalKey = transposeKey(m_currentWheelKey, totalSemitones);

    qDebug() << "[WaveformRenderKeyCurve] After transpose - result:" << m_transposedMusicalKey;

    // Convert back to preferred spelling
    if (m_transposedMusicalKey == "A#m")
        m_transposedMusicalKey = "Bbm";
    if (m_transposedMusicalKey == "D#m")
        m_transposedMusicalKey = "Ebm";
    if (m_transposedMusicalKey == "G#m")
        m_transposedMusicalKey = "Abm";
    if (m_transposedMusicalKey == "C#")
        m_transposedMusicalKey = "Db";
    if (m_transposedMusicalKey == "F#")
        m_transposedMusicalKey = "Gb";
    if (m_transposedMusicalKey == "A#")
        m_transposedMusicalKey = "Bb";
    if (m_transposedMusicalKey == "D#")
        m_transposedMusicalKey = "Eb";
    if (m_transposedMusicalKey == "G#")
        m_transposedMusicalKey = "Ab";

    qDebug() << "[WaveformRenderKeyCurve] Keylock:" << m_keylockEnabled
             << "Semitones:" << totalSemitones
             << "Wheel steps:" << wheelSteps
             << "Original:" << m_currentWheelKey << "(" << m_baseLancelot << ")"
             << "-> Flashing:" << m_transposedLancelot
             << "Display:" << m_transposedMusicalKey;
}

QImage WaveformRenderKeyCurve::drawCamelotWheel() {
    float width = static_cast<float>(m_waveformRenderer->getWidth());
    float height = static_cast<float>(m_waveformRenderer->getBreadth());

    // Create image for the entire waveform area
    QImage image(static_cast<int>(width),
            static_cast<int>(height),
            QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    // FIRST: Draw key markers and labels (if they should appear on the waveform)
    // This code is copied from your working non-accelerated version
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (pTrack && !m_segments.isEmpty()) {
        double trackLengthSeconds = pTrack->getDuration();
        double trackSamples = m_waveformRenderer->getTrackSamples();
        double firstDisplayedPosition = m_waveformRenderer->getFirstDisplayedPosition();
        double lastDisplayedPosition = m_waveformRenderer->getLastDisplayedPosition();

        double startSample = firstDisplayedPosition * trackSamples;
        double endSample = lastDisplayedPosition * trackSamples;

        int totalOffset = m_currentTotalOffset;

        // Draw key markers (vertical lines)
        if (m_style.showMarkers) {
            painter.setPen(QPen(m_style.markerColor, m_style.markerWidth, m_style.markerLineStyle));
            for (const auto& seg : m_segments) {
                double startTime = seg.startTime;
                double startPos = startTime * (trackSamples / trackLengthSeconds);

                if (startPos >= startSample && startPos <= endSample) {
                    double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                            startPos, ::WaveformRendererAbstract::Play);

                    if (x >= 0 && x <= width) {
                        painter.drawLine(QLineF(x, 0, x, height));
                    }
                }
            }
        }

        // Draw key labels
        if (m_style.showLabels) {
            QFont labelFont = painter.font();
            labelFont.setPointSize(m_style.fontSize);
            labelFont.setBold(true);
            painter.setFont(labelFont);

            double minLabelSpacing = 400;
            double lastLabelX = -minLabelSpacing;
            double labelY = 5;

            for (int i = 0; i < m_segments.size(); ++i) {
                const auto& seg = m_segments[i];

                if (seg.confidence < 50.0) {
                    continue;
                }

                double startTime = seg.startTime;
                double startPos = startTime * (trackSamples / trackLengthSeconds);

                if (startPos < startSample || startPos > endSample) {
                    continue;
                }

                double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                        startPos, ::WaveformRendererAbstract::Play);

                if (x >= 0 && x <= width) {
                    if (x - lastLabelX >= minLabelSpacing) {
                        QString originalKey = seg.key;
                        QString transposedKey = transposeKey(originalKey, totalOffset);

                        // Convert to preferred spelling
                        if (transposedKey == "A#m")
                            transposedKey = "Bbm";
                        if (transposedKey == "D#m")
                            transposedKey = "Ebm";
                        if (transposedKey == "G#m")
                            transposedKey = "Abm";
                        if (transposedKey == "C#")
                            transposedKey = "Db";
                        if (transposedKey == "F#")
                            transposedKey = "Gb";
                        if (transposedKey == "A#")
                            transposedKey = "Bb";
                        if (transposedKey == "D#")
                            transposedKey = "Eb";
                        if (transposedKey == "G#")
                            transposedKey = "Ab";

                        // Draw label background
                        QRect textRect = painter.fontMetrics().boundingRect(transposedKey);
                        int padding = 4;
                        QRect bgRect(static_cast<int>(x + 5 - padding),
                                static_cast<int>(labelY - padding),
                                textRect.width() + padding * 2,
                                textRect.height() + padding * 2);
                        painter.fillRect(bgRect, m_style.backgroundColor);

                        // Draw label text
                        painter.setPen(QPen(getColorForKey(transposedKey), 1));
                        painter.drawText(static_cast<int>(x + 5),
                                static_cast<int>(labelY + textRect.height()),
                                transposedKey);

                        lastLabelX = x;
                    }
                }
            }
        }
    }

    // SECOND: Draw the Camelot wheel (your working code)
    // Calculate wheel size based on waveform height
    int wheelSize = static_cast<int>(height * 0.7);
    if (wheelSize < 60)
        wheelSize = 60;
    if (wheelSize > 150)
        wheelSize = 150;
    m_wheelSize = wheelSize;

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

    // Draw outer slices (Majors)
    for (const auto& key : m_lancelotLayout) {
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
    for (const auto& key : m_lancelotLayout) {
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

    // Draw highlighted key
    QString currentLancelotForDisplay = m_transposedLancelot.isEmpty()
            ? m_baseLancelot
            : m_transposedLancelot;
    if (!currentLancelotForDisplay.isEmpty()) {
        for (const auto& key : m_lancelotLayout) {
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

    // Draw text below wheel
    QString displayKey;
    if (!m_transposedMusicalKey.isEmpty() && !m_transposedLancelot.isEmpty()) {
        displayKey = normalizeKeyDisplay(m_transposedMusicalKey) + " (" +
                m_transposedLancelot + ")";
    } else if (!m_currentWheelKey.isEmpty() && !m_baseLancelot.isEmpty()) {
        displayKey = normalizeKeyDisplay(m_currentWheelKey) + " (" + m_baseLancelot + ")";
    }

    if (displayKey.contains('/')) {
        QStringList parts = displayKey.split('/');
        if (!parts.isEmpty()) {
            displayKey = parts[0];
        }
    }

    QFont font = painter.font();
    font.setPointSize(static_cast<int>(height * 0.1));
    painter.setFont(font);
    painter.setPen(QPen(QColor(255, 255, 255, 200), 1));
    painter.drawText(QRect(wheelX, wheelY + wheelSize + 5, wheelSize, 20),
            Qt::AlignCenter,
            displayKey);

    painter.end();

    return image;
}

QString WaveformRenderKeyCurve::normalizeKeyDisplay(const QString& key) const {
    QString normalized = key;
    normalized.replace(QChar(0x266D), 'b');
    normalized.replace(QChar(0x266F), '#');
    return normalized;
}

void WaveformRenderKeyCurve::updateKeyTextures() {
    qDebug() << "[WaveformRenderKeyCurve - Allshader] updateKeyTextures called "
                "- currentKey:"
             << m_currentKey;

    // Update current key based on play position
    updateCurrentKey();

    // Draw the Camelot wheel
    QImage wheelImage = drawCamelotWheel();

    if (m_pKeyCurveNode) {
        // Update existing node texture
        rendergraph::Context* pContext = m_waveformRenderer->getContext();
        if (pContext) {
            m_pKeyCurveNode->updateTexture(pContext, wheelImage);
            m_textureReady = true;
        }
    } else {
        // Create new node
        createNode(wheelImage);
        // Texture will be set in createNode, but mark that we need to update position
        m_textureReady = true;
    }

    // Force a position update
    updateNode();
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
        // Node already exists, no need to store pending
        return;
    }

    // Store the image and wait for context
    m_pendingWheelImage = image;
    m_waitingForContext = true;
    qDebug() << "[WaveformRenderKeyCurve - Allshader] Waiting for context to create node";
}

void WaveformRenderKeyCurve::updateNode() {
    if (!m_pendingWheelImage.isNull()) {
        qDebug() << "[WaveformRenderKeyCurve - Allshader] Context available, creating node";
        auto pNode = std::make_unique<WaveformKeyCurveNode>(
                m_waveformRenderer->getContext(), m_pendingWheelImage);
        m_pKeyCurveNode = pNode.get();
        m_pKeyCurveNodesParent->appendChildNode(std::move(pNode));
        m_waitingForContext = false;
        m_pendingWheelImage = QImage(); // Clear pending image
    }

    if (!m_pKeyCurveNode)
        return;

    // Poll current values for rate, key shift, keylock
    if (m_pRateRatioCO) {
        m_currentRateRatio = m_pRateRatioCO->get();
    }
    if (m_pKeyControlCO) {
        m_currentKeyShift = m_pKeyControlCO->get();
    }
    if (m_pKeylockCO) {
        m_keylockEnabled = (m_pKeylockCO->get() > 0.5);
    }

    // Create a full-size image for the entire waveform
    float width = static_cast<float>(m_waveformRenderer->getWidth());
    float height = static_cast<float>(m_waveformRenderer->getBreadth());

    if (width <= 0 || height <= 0)
        return;

    // Create image for the entire waveform area
    QImage fullImage(static_cast<int>(width),
            static_cast<int>(height),
            QImage::Format_ARGB32_Premultiplied);
    fullImage.fill(Qt::transparent);

    QPainter painter(&fullImage);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw key markers and labels
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (pTrack && !m_segments.isEmpty()) {
        double trackLengthSeconds = pTrack->getDuration();
        double trackSamples = m_waveformRenderer->getTrackSamples();
        double firstDisplayedPosition = m_waveformRenderer->getFirstDisplayedPosition();
        double lastDisplayedPosition = m_waveformRenderer->getLastDisplayedPosition();

        double startSample = firstDisplayedPosition * trackSamples;
        double endSample = lastDisplayedPosition * trackSamples;

        // Calculate semitone offset from rate ratio (only if keylock is OFF)
        double pitchSemitones = 0.0;
        if (!m_keylockEnabled) {
            pitchSemitones = 12.0 * log2(m_currentRateRatio);
        }

        int totalSemitones = static_cast<int>(std::round(pitchSemitones + m_currentKeyShift));
        m_currentTotalOffset = totalSemitones;

        int totalOffset = m_currentTotalOffset;

        // UPDATE TRANSPOSED VALUES FOR WHEEL AND DISPLAY KEY
        if (!m_baseLancelot.isEmpty() && !m_currentWheelKey.isEmpty()) {
            // Convert semitones to Camelot wheel steps
            int wheelSteps = (totalSemitones * 7) % 12;
            if (wheelSteps < 0)
                wheelSteps += 12;

            // Apply to Lancelot number
            int currentNumber = QStringView(m_baseLancelot)
                                        .left(m_baseLancelot.length() - 1)
                                        .toInt();
            bool isMinor = m_baseLancelot.endsWith("A");

            int newNumber = currentNumber + wheelSteps;
            while (newNumber < 1)
                newNumber += 12;
            while (newNumber > 12)
                newNumber -= 12;

            m_transposedLancelot = QString::number(newNumber) + (isMinor ? "A" : "B");

            // Transpose the musical key
            m_transposedMusicalKey = transposeKey(m_currentWheelKey, totalSemitones);

            // Convert back to preferred spelling
            if (m_transposedMusicalKey == "A#m")
                m_transposedMusicalKey = "Bbm";
            if (m_transposedMusicalKey == "D#m")
                m_transposedMusicalKey = "Ebm";
            if (m_transposedMusicalKey == "G#m")
                m_transposedMusicalKey = "Abm";
            if (m_transposedMusicalKey == "C#")
                m_transposedMusicalKey = "Db";
            if (m_transposedMusicalKey == "F#")
                m_transposedMusicalKey = "Gb";
            if (m_transposedMusicalKey == "A#")
                m_transposedMusicalKey = "Bb";
            if (m_transposedMusicalKey == "D#")
                m_transposedMusicalKey = "Eb";
            if (m_transposedMusicalKey == "G#")
                m_transposedMusicalKey = "Ab";
        }

        // Draw key markers (vertical lines)
        if (m_style.showMarkers) {
            painter.setPen(QPen(m_style.markerColor, m_style.markerWidth, m_style.markerLineStyle));
            for (const auto& seg : m_segments) {
                double startTime = seg.startTime;
                double startPos = startTime * (trackSamples / trackLengthSeconds);

                if (startPos >= startSample && startPos <= endSample) {
                    double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                            startPos, ::WaveformRendererAbstract::Play);

                    if (x >= 0 && x <= width) {
                        painter.drawLine(QLineF(x, 0, x, height));
                    }
                }
            }
        }

        // Draw key labels
        if (m_style.showLabels) {
            QFont labelFont = painter.font();
            labelFont.setPointSize(m_style.fontSize);
            labelFont.setBold(true);
            painter.setFont(labelFont);

            double minLabelSpacing = 400;
            double lastLabelX = -minLabelSpacing;
            double labelY = 5;

            for (int i = 0; i < m_segments.size(); ++i) {
                const auto& seg = m_segments[i];

                if (seg.confidence < 50.0) {
                    continue;
                }

                double startTime = seg.startTime;
                double startPos = startTime * (trackSamples / trackLengthSeconds);

                if (startPos < startSample || startPos > endSample) {
                    continue;
                }

                double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                        startPos, ::WaveformRendererAbstract::Play);

                if (x >= 0 && x <= width) {
                    if (x - lastLabelX >= minLabelSpacing) {
                        QString originalKey = seg.key;
                        QString transposedKey = transposeKey(originalKey, totalOffset);

                        // Convert to preferred spelling
                        if (transposedKey == "A#m")
                            transposedKey = "Bbm";
                        if (transposedKey == "D#m")
                            transposedKey = "Ebm";
                        if (transposedKey == "G#m")
                            transposedKey = "Abm";
                        if (transposedKey == "C#")
                            transposedKey = "Db";
                        if (transposedKey == "F#")
                            transposedKey = "Gb";
                        if (transposedKey == "A#")
                            transposedKey = "Bb";
                        if (transposedKey == "D#")
                            transposedKey = "Eb";
                        if (transposedKey == "G#")
                            transposedKey = "Ab";

                        // Draw label background
                        QRect textRect = painter.fontMetrics().boundingRect(transposedKey);
                        int padding = 4;
                        QRect bgRect(static_cast<int>(x + 5 - padding),
                                static_cast<int>(labelY - padding),
                                textRect.width() + padding * 2,
                                textRect.height() + padding * 2);
                        painter.fillRect(bgRect, m_style.backgroundColor);

                        // Draw label text
                        painter.setPen(QPen(getColorForKey(transposedKey), 1));
                        painter.drawText(static_cast<int>(x + 5),
                                static_cast<int>(labelY + textRect.height()),
                                transposedKey);

                        lastLabelX = x;
                    }
                }
            }
        }
    }

    // Draw the Camelot wheel
    int wheelSize = static_cast<int>(height * 0.7);
    if (wheelSize < 60)
        wheelSize = 60;
    // if (wheelSize > 150)
    //     wheelSize = 150;

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

    // Draw outer slices (Majors)
    for (const auto& key : m_lancelotLayout) {
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
    for (const auto& key : m_lancelotLayout) {
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

    // Draw highlighted key using updated transposed values
    QString currentLancelotForDisplay = m_transposedLancelot.isEmpty()
            ? m_baseLancelot
            : m_transposedLancelot;
    if (!currentLancelotForDisplay.isEmpty()) {
        for (const auto& key : m_lancelotLayout) {
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

    // Draw text below wheel using updated transposed musical key
    QString displayKey;
    if (!m_transposedMusicalKey.isEmpty() && !m_transposedLancelot.isEmpty()) {
        displayKey = normalizeKeyDisplay(m_transposedMusicalKey) + " (" +
                m_transposedLancelot + ")";
    } else if (!m_currentWheelKey.isEmpty() && !m_baseLancelot.isEmpty()) {
        displayKey = normalizeKeyDisplay(m_currentWheelKey) + " (" + m_baseLancelot + ")";
    }

    QFont font = painter.font();
    font.setPointSize(static_cast<int>(height * 0.1));
    painter.setFont(font);
    painter.setPen(QPen(QColor(255, 255, 255, 200), 1));
    painter.drawText(QRect(wheelX, wheelY + wheelSize + 5, wheelSize, 20),
            Qt::AlignCenter,
            displayKey);
    painter.end();

    // Update the texture with the full image
    auto texture = std::make_unique<rendergraph::Texture>(
            m_waveformRenderer->getContext(), fullImage);
    dynamic_cast<rendergraph::TextureMaterial&>(m_pKeyCurveNode->material())
            .setTexture(std::move(texture));
    m_pKeyCurveNode->markDirtyMaterial();

    // Update position to cover the entire waveform area
    float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    m_pKeyCurveNode->updatePosition(0, 0, devicePixelRatio);
    m_pKeyCurveNode->markDirtyGeometry();
}

void WaveformRenderKeyCurve::update() {
    if (isSubtreeBlocked())
        return;

    // Update play position and current key
    if (m_pPlayPositionCO) {
        double newPosition = m_pPlayPositionCO->get();
        if (std::abs(newPosition - m_playPosition) > 0.001) {
            m_playPosition = newPosition;
            updateCurrentKey();
        }
    }

    // Create initial pending image if needed
    if (!m_pKeyCurveNode && m_pendingWheelImage.isNull() && !m_segments.isEmpty()) {
        QImage wheelImage = drawCamelotWheel();
        createNode(wheelImage);
    }

    updateNode();
}

} // namespace allshader
