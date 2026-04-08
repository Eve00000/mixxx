#include "waveform/renderers/waveformrenderkeycurve.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
#include <cmath>

#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Constants for Lancelot/Camelot wheel
namespace {
constexpr double WHEEL_START_ANGLE = 45.0; // Slice 1 starts at 45° (Qt counter-clockwise)
constexpr double SLICE_ANGLE = 30.0;       // 30 degrees per slice
constexpr int NUM_SLICES = 12;             // 12 slices for the wheel
} // namespace

WaveformRenderKeyCurve::WaveformRenderKeyCurve(WaveformWidgetRenderer* renderer)
        : WaveformRendererAbstract(renderer),
          m_visible(true),
          m_trackLengthSeconds(0.0),
          m_currentRateRatio(1.0),
          m_currentPlayPosition(0.0),
          m_wheelSize(120),
          m_wheelMargin(10),
          m_pPlayPositionCO(nullptr) {
    m_animationTimer.start();
    initLancelotLayout();
}

void WaveformRenderKeyCurve::initPlayPositionControl() {
    if (!m_waveformRenderer || m_waveformRenderer->getGroup().isEmpty()) {
        qDebug() << "[WaveformRenderKeyCurve] Cannot init playposition control - no group";
        return;
    }

    qDebug() << "[WaveformRenderKeyCurve] Creating playposition control for group:"
             << m_waveformRenderer->getGroup();

    m_pPlayPositionCO = std::make_unique<ControlProxy>(
            m_waveformRenderer->getGroup(), "playposition");

    m_currentPlayPosition = m_pPlayPositionCO->get();
    qDebug() << "[WaveformRenderBpmCurve] Initial playposition:" << m_currentPlayPosition;
}

void WaveformRenderKeyCurve::setup(const QDomNode& node, const SkinContext& skinContext) {
    // Text styling
    QString textColorStr = skinContext.selectString(node, QStringLiteral("KeyTextColor"));
    if (!textColorStr.isEmpty()) {
        m_style.textColor = QColor(textColorStr);
    }

    QString fontSizeStr = skinContext.selectString(node, QStringLiteral("KeyFontSize"));
    if (!fontSizeStr.isEmpty()) {
        m_style.fontSize = fontSizeStr.toInt();
    }

    QString fontOpacityStr = skinContext.selectString(node, QStringLiteral("KeyFontOpacity"));
    if (!fontOpacityStr.isEmpty()) {
        m_style.fontOpacity = fontOpacityStr.toInt();
    }
    m_style.textColor.setAlpha(m_style.fontOpacity);

    // Background styling
    QString bgColorStr = skinContext.selectString(node, QStringLiteral("KeyBackgroundColor"));
    if (!bgColorStr.isEmpty()) {
        m_style.backgroundColor = QColor(bgColorStr);
    }

    QString bgOpacityStr = skinContext.selectString(node, QStringLiteral("KeyBackgroundOpacity"));
    if (!bgOpacityStr.isEmpty()) {
        m_style.backgroundOpacity = bgOpacityStr.toInt();
    }
    m_style.backgroundColor.setAlpha(m_style.backgroundOpacity);

    // Marker styling
    QString markerColorStr = skinContext.selectString(node, QStringLiteral("KeyMarkerColor"));
    if (!markerColorStr.isEmpty()) {
        m_style.markerColor = QColor(markerColorStr);
    }

    QString markerWidthStr = skinContext.selectString(node, QStringLiteral("KeyMarkerWidth"));
    if (!markerWidthStr.isEmpty()) {
        m_style.markerWidth = markerWidthStr.toInt();
    }

    // Lancelot wheel visibility
    QString showWheelStr = skinContext.selectString(node, QStringLiteral("ShowLancelotWheel"));
    if (!showWheelStr.isEmpty()) {
        m_style.showLancelotWheel = (showWheelStr.compare("true", Qt::CaseInsensitive) == 0);
    }

    QString wheelSizeStr = skinContext.selectString(node, QStringLiteral("LancelotWheelSize"));
    if (!wheelSizeStr.isEmpty()) {
        m_wheelSize = wheelSizeStr.toInt();
    }
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

    constexpr double START_ANGLE = 45.0; // Start of Slice 1
    constexpr double SLICE_WIDTH = 30.0; // Each slice is 30 degrees

    for (int i = 0; i < NUM_SLICES; ++i) {
        LancelotKey key;
        key.lancelot = majorLancelots[i];
        // Decrease angle for clockwise numbering
        double angle = START_ANGLE - (i * SLICE_WIDTH);
        // Normalize to 0-360 degrees
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
        double angle = START_ANGLE - (i * SLICE_WIDTH);
        // Normalize to 0-360 degrees
        while (angle < 0.0)
            angle += 360.0;
        while (angle >= 360.0)
            angle -= 360.0;
        key.angle = angle;
        key.isMinor = true;
        m_lancelotLayout.append(key);
    }

    // qDebug() << "[WaveformRenderKeyCurve] Lancelot layout initialized:";
    // for (int i = 0; i < m_lancelotLayout.size(); ++i) {
    // qDebug() << "  Index" << i << ":" << m_lancelotLayout[i].lancelot
    //         << "angle:" << m_lancelotLayout[i].angle
    //         << "isMinor:" << m_lancelotLayout[i].isMinor;
    //}
}

void WaveformRenderKeyCurve::onPlayPositionChanged(double value) {
    m_currentPlayPosition = value;
    updateCurrentWheelKey();
}

void WaveformRenderKeyCurve::updateCurrentWheelKey() {
    if (m_segments.isEmpty() || m_trackLengthSeconds <= 0) {
        return;
    }

    double positionSeconds = m_currentPlayPosition * m_trackLengthSeconds;

    for (const auto& seg : m_segments) {
        if (positionSeconds >= seg.startTime && positionSeconds <= seg.endTime) {
            if (m_currentWheelKey != seg.key) {
                m_currentWheelKey = seg.key;
            }
            break;
        }
    }
}

void WaveformRenderKeyCurve::onSetTrack() {
    m_segments.clear();
    loadKeyCurve();
    initPlayPositionControl();
}

void WaveformRenderKeyCurve::loadKeyCurve() {
    m_segments.clear();

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack || !pTrack->getId().isValid()) {
        return;
    }

    m_trackLengthSeconds = pTrack->getDuration();

    QString trackIdStr = pTrack->getId().toString();
    QString keyDir = QStandardPaths::writableLocation(
                             QStandardPaths::AppLocalDataLocation) +
            "/keycurve/";
    QString jsonPath = keyDir + trackIdStr + ".json";

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
    QJsonArray keyArray;

    if (rootObj.contains("key_curve") && rootObj["key_curve"].isArray()) {
        keyArray = rootObj["key_curve"].toArray();
    } else {
        return;
    }

    for (const QJsonValue& val : std::as_const(keyArray)) {
        if (!val.isObject()) {
            continue;
        }

        QJsonObject obj = val.toObject();
        KeySegment seg;

        seg.startTime = obj["position"].toDouble();
        seg.endTime = obj["range_end"].toDouble();
        seg.duration = obj["duration"].toDouble();
        seg.key = obj["key"].toString();
        seg.type = obj["type"].toString();
        seg.confidence = obj["confidence"].toDouble();

        m_segments.append(seg);
    }

    // Set initial wheel key
    if (!m_segments.isEmpty()) {
        m_currentWheelKey = m_segments[0].key;
    }
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
        static QMap<QString, QString> majorToLancelot = {
                {"C", "12B"},
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

        static QMap<QString, QString> minorToLancelot = {
                {"Am", "12A"},
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
        static QMap<QString, QString> majorToLancelot = {
                {"B", "1B"},
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

QString WaveformRenderKeyCurve::normalizeKeyDisplay(const QString& key) const {
    QString normalized = key;
    // Unicode flat to ASCII b
    normalized.replace(QChar(0x266D), 'b');
    // Unicode sharp to ASCII #
    normalized.replace(QChar(0x266F), '#');
    return normalized;
}

void WaveformRenderKeyCurve::drawWheelSlice(
        QPainter* painter,
        const QRectF& rect,
        double startAngle,
        double endAngle,
        const QString& lancelot,
        bool isMinor) {
    double spanAngle = endAngle - startAngle;
    if (spanAngle <= 0) {
        return;
    }

    int start = static_cast<int>(startAngle * 16);
    int span = static_cast<int>(spanAngle * 16);

    // outline
    painter->setPen(QPen(QColor(200, 200, 200, 150), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawPie(rect, start, span);
}

void WaveformRenderKeyCurve::drawHighlightedWheelKey(
        QPainter* painter,
        const QRectF& outerRect,
        const QRectF& innerRect,
        const QString& lancelot) {
    if (lancelot.isEmpty()) {
        return;
    }

    // qDebug() << "[WaveformRenderKeyCurve-Wheel] Looking for slice:" << lancelot;

    for (const auto& key : m_lancelotLayout) {
        if (key.lancelot == lancelot) {
            double startAngle = key.angle;
            int start = static_cast<int>(startAngle * 16);
            int span = static_cast<int>(30 * 16);

            // Choose correct rectangle
            QRectF drawRect = key.isMinor ? innerRect : outerRect;

            // Flashing white fill
            double time = m_animationTimer.elapsed() / 1000.0;
            int alpha = 100 + static_cast<int>(155 * sin(time * 8));

            painter->setPen(QPen(QColor(255, 255, 255, alpha), 2));
            painter->setBrush(QBrush(QColor(255, 255, 255, alpha / 2)));
            painter->drawPie(drawRect, start, span);
            break;
        }
    }
}

void WaveformRenderKeyCurve::drawLancelotWheel(QPainter* painter) {
    if (!m_style.showLancelotWheel) {
        return;
    }
    if (m_segments.isEmpty()) {
        return;
    }

    m_wheelSize = m_waveformRenderer->getHeight() * 0.7;

    int x = m_waveformRenderer->getWidth() - m_wheelSize - m_wheelMargin;
    int y = m_wheelMargin;
    QRectF rect(x, y, m_wheelSize, m_wheelSize);
    QPointF center = rect.center();

    // Draw outer circle
    painter->setPen(QPen(QColor(150, 150, 150, 200), 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(rect);

    // Draw inner circle (smaller)
    // with 0.45 the outer parts are a bit small
    double innerRadius = rect.width() * 0.35;
    QRectF innerRect(center.x() - innerRadius,
            center.y() - innerRadius,
            innerRadius * 2,
            innerRadius * 2);
    painter->setPen(QPen(QColor(150, 150, 150, 150), 1, Qt::DashLine));
    painter->drawEllipse(innerRect);

    // draw outer slices = Majors
    for (const auto& key : m_lancelotLayout) {
        if (!key.isMinor) {
            drawWheelSlice(painter, rect, key.angle, key.angle + 30, key.lancelot, false);
        }
    }

    // draw inner slices = Minors
    for (const auto& key : m_lancelotLayout) {
        if (key.isMinor) {
            drawWheelSlice(painter, innerRect, key.angle, key.angle + 30, key.lancelot, true);
        }
    }

    // draw highlighted key as flashing white slice
    QString currentLancelot = keyToLancelot(m_currentWheelKey);
    if (!currentLancelot.isEmpty()) {
        drawHighlightedWheelKey(painter, rect, innerRect, currentLancelot);
    }

    QString displayKey = normalizeKeyDisplay(m_currentWheelKey);
    if (!currentLancelot.isEmpty()) {
        displayKey += " (" + currentLancelot + ")";
    }

    QFont font = painter->font();
    // font.setPointSize(15);
    // adapt to height
    font.setPointSize(m_waveformRenderer->getHeight() * 0.1);
    painter->setFont(font);
    painter->setPen(QPen(QColor(255, 255, 255, 200), 1));
    painter->drawText(QRect(x, y + m_wheelSize + 5, m_wheelSize, 20),
            Qt::AlignCenter,
            displayKey);
}

QColor WaveformRenderKeyCurve::getColorForKey(const QString& key) const {
    int hash = 0;
    for (QChar ch : key) {
        hash += ch.unicode();
    }
    return QColor::fromHsv(hash % 360, 200, 200);
}

void WaveformRenderKeyCurve::drawKeyLabel(
        QPainter* painter,
        const QPointF& position,
        const QString& key,
        Qt::Orientation orientation) {
    QColor keyColor = getColorForKey(key);
    keyColor.setAlpha(m_style.fontOpacity);

    QFont labelFont = painter->font();
    labelFont.setPointSize(m_style.fontSize);
    labelFont.setBold(true);
    painter->setFont(labelFont);

    QRect textRect = painter->fontMetrics().boundingRect(key);
    int padding = 4;
    int labelHeight = textRect.height();

    QPointF labelPos;
    if (orientation == Qt::Horizontal) {
        labelPos = QPointF(position.x() + 5, position.y());

        if (labelPos.x() + textRect.width() > painter->device()->width()) {
            labelPos.setX(position.x() - textRect.width() - 5);
        }

        if (labelPos.x() < 0) {
            labelPos.setX(0);
        }

        if (m_style.showBackground) {
            QRect bgRect(static_cast<int>(labelPos.x() - padding),
                    static_cast<int>(labelPos.y() - padding),
                    textRect.width() + padding * 2,
                    labelHeight + padding * 2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_style.backgroundColor);
            painter->drawRoundedRect(bgRect, 3, 3);
        }

        painter->setPen(QPen(keyColor, 1));
        painter->drawText(static_cast<int>(labelPos.x()),
                static_cast<int>(labelPos.y() + labelHeight),
                key);
    }
}

void WaveformRenderKeyCurve::drawKeyChangeLabel(
        QPainter* painter,
        const QPointF& position,
        const QString& previousKey,
        const QString& currentKey,
        Qt::Orientation orientation) {
    drawKeyLabel(painter, position, currentKey, orientation);
}

void WaveformRenderKeyCurve::draw(QPainter* painter, QPaintEvent* /*event*/) {
    if (!m_visible || m_segments.isEmpty()) {
        drawLancelotWheel(painter);
        return;
    }

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack) {
        drawLancelotWheel(painter);
        return;
    }

    if (m_pPlayPositionCO) {
        double newPlayPosition = m_pPlayPositionCO->get();
        if (std::abs(newPlayPosition - m_currentPlayPosition) > 0.001) {
            qDebug() << "[WaveformRenderKeyCurve] PlayPosition changed (polled):"
                     << m_currentPlayPosition << "->" << newPlayPosition;
            m_currentPlayPosition = newPlayPosition;
            onPlayPositionChanged(m_currentPlayPosition);
        }
    }

    double trackLengthSeconds = pTrack->getDuration();
    double trackSamples = m_waveformRenderer->getTrackSamples();

    if (trackLengthSeconds <= 0 || trackSamples <= 0) {
        drawLancelotWheel(painter);
        return;
    }

    double firstDisplayedPosition = m_waveformRenderer->getFirstDisplayedPosition();
    double lastDisplayedPosition = m_waveformRenderer->getLastDisplayedPosition();

    firstDisplayedPosition = qMax(0.0, firstDisplayedPosition);
    lastDisplayedPosition = qMin(1.0, lastDisplayedPosition);

    if (firstDisplayedPosition >= lastDisplayedPosition) {
        firstDisplayedPosition = 0.0;
        lastDisplayedPosition = 1.0;
    }

    double startSample = firstDisplayedPosition * trackSamples;
    double endSample = lastDisplayedPosition * trackSamples;

    const float rendererWidth = m_waveformRenderer->getWidth();
    const float rendererHeight = m_waveformRenderer->getHeight();
    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();

    PainterScope scope(painter);
    painter->setRenderHint(QPainter::Antialiasing);

    const double labelY = 5;
    const double minLabelSpacing = 400;

    // draw key markers and labels
    if (m_style.showMarkers) {
        painter->setPen(QPen(m_style.markerColor, m_style.markerWidth, m_style.markerLineStyle));

        for (const auto& seg : m_segments) {
            double startTime = seg.startTime;
            double startPos = startTime * (trackSamples / trackLengthSeconds);

            if (startPos >= startSample && startPos <= endSample) {
                double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                        startPos, ::WaveformRendererAbstract::Play);

                if (x >= 0 && x <= rendererWidth) {
                    if (orientation == Qt::Horizontal) {
                        painter->drawLine(QLineF(x, 0, x, rendererHeight));
                    }
                }
            }
        }
    }

    if (m_style.showLabels) {
        QFont labelFont = painter->font();
        labelFont.setPointSize(m_style.fontSize);
        labelFont.setBold(true);
        painter->setFont(labelFont);

        double lastLabelX = -minLabelSpacing;

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

            if (x >= 0 && x <= rendererWidth) {
                if (x - lastLabelX >= minLabelSpacing) {
                    if (orientation == Qt::Horizontal) {
                        drawKeyLabel(painter, QPointF(x, labelY), seg.key, orientation);
                        lastLabelX = x;
                    }
                }
            }
        }
    }

    drawLancelotWheel(painter);
}
