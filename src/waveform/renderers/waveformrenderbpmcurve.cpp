
#include "waveform/renderers/waveformrenderbpmcurve.h"

#include <QDebug>
#include <QDir>
#include <QDomNode>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
#include <algorithm>

#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

WaveformRenderBpmCurve::WaveformRenderBpmCurve(WaveformWidgetRenderer* renderer)
        : WaveformRendererAbstract(renderer),
          m_visible(true),
          m_minBpm(0),
          m_maxBpm(0),
          m_yMinBpm(0),
          m_yMaxBpm(0),
          m_offsetSeconds(0.0) {
}

void WaveformRenderBpmCurve::setup(const QDomNode& node, const SkinContext& skinContext) {
    // Curve styling
    QString curveColorStr = skinContext.selectString(node, QStringLiteral("CurveColor"));
    if (!curveColorStr.isEmpty()) {
        m_style.curveColor = QColor(curveColorStr);
        m_style.curveColor = WSkinColor::getCorrectColor(m_style.curveColor).toRgb();
    }

    QString curveWidthStr = skinContext.selectString(node, QStringLiteral("CurveWidth"));
    if (!curveWidthStr.isEmpty()) {
        m_style.curveWidth = curveWidthStr.toInt();
    }

    QString curveOpacityStr = skinContext.selectString(node, QStringLiteral("CurveOpacity"));
    if (!curveOpacityStr.isEmpty()) {
        m_style.curveOpacity = curveOpacityStr.toInt();
    }
    m_style.curveColor.setAlpha(m_style.curveOpacity);

    // Marker styling
    QString markerColorStr = skinContext.selectString(node, QStringLiteral("MarkerColor"));
    if (!markerColorStr.isEmpty()) {
        m_style.markerColor = QColor(markerColorStr);
        m_style.markerColor = WSkinColor::getCorrectColor(m_style.markerColor).toRgb();
    }

    QString markerWidthStr = skinContext.selectString(node, QStringLiteral("MarkerWidth"));
    if (!markerWidthStr.isEmpty()) {
        m_style.markerWidth = markerWidthStr.toInt();
    }

    QString markerOpacityStr = skinContext.selectString(node, QStringLiteral("MarkerOpacity"));
    if (!markerOpacityStr.isEmpty()) {
        m_style.markerOpacity = markerOpacityStr.toInt();
    }
    m_style.markerColor.setAlpha(m_style.markerOpacity);

    QString markerLineStyleStr = skinContext.selectString(node, QStringLiteral("MarkerLineStyle"));
    if (!markerLineStyleStr.isEmpty()) {
        if (markerLineStyleStr == "solid")
            m_style.markerLineStyle = Qt::SolidLine;
        else if (markerLineStyleStr == "dash")
            m_style.markerLineStyle = Qt::DashLine;
        else if (markerLineStyleStr == "dot")
            m_style.markerLineStyle = Qt::DotLine;
        else if (markerLineStyleStr == "dashdot")
            m_style.markerLineStyle = Qt::DashDotLine;
    }

    // label styling
    QString labelTextColorStr = skinContext.selectString(node, QStringLiteral("LabelTextColor"));
    if (!labelTextColorStr.isEmpty()) {
        m_style.labelTextColor = QColor(labelTextColorStr);
        m_style.labelTextColor = WSkinColor::getCorrectColor(m_style.labelTextColor).toRgb();
    }

    QString labelBgColorStr = skinContext.selectString(
            node, QStringLiteral("LabelBackgroundColor"));
    if (!labelBgColorStr.isEmpty()) {
        m_style.labelBackgroundColor = QColor(labelBgColorStr);
        m_style.labelBackgroundColor =
                WSkinColor::getCorrectColor(m_style.labelBackgroundColor)
                        .toRgb();
    }

    QString labelBgOpacityStr = skinContext.selectString(
            node, QStringLiteral("LabelBackgroundOpacity"));
    if (!labelBgOpacityStr.isEmpty()) {
        m_style.labelBackgroundOpacity = labelBgOpacityStr.toInt();
    }
    m_style.labelBackgroundColor.setAlpha(m_style.labelBackgroundOpacity);

    QString labelFontSizeStr = skinContext.selectString(node, QStringLiteral("LabelFontSize"));
    if (!labelFontSizeStr.isEmpty()) {
        m_style.labelFontSize = labelFontSizeStr.toInt();
    }

    QString labelDecimalPlacesStr = skinContext.selectString(
            node, QStringLiteral("LabelDecimalPlaces"));
    if (!labelDecimalPlacesStr.isEmpty()) {
        m_style.labelDecimalPlaces = labelDecimalPlacesStr.toInt();
    }

    QString labelOffsetStr = skinContext.selectString(node, QStringLiteral("LabelOffset"));
    if (!labelOffsetStr.isEmpty()) {
        m_style.labelOffset = labelOffsetStr.toInt();
    }

    // track start styling
    QString trackStartColorStr = skinContext.selectString(node, QStringLiteral("TrackStartColor"));
    if (!trackStartColorStr.isEmpty()) {
        m_style.trackStartColor = QColor(trackStartColorStr);
        m_style.trackStartColor = WSkinColor::getCorrectColor(m_style.trackStartColor).toRgb();
    }

    QString trackStartWidthStr = skinContext.selectString(node, QStringLiteral("TrackStartWidth"));
    if (!trackStartWidthStr.isEmpty()) {
        m_style.trackStartWidth = trackStartWidthStr.toInt();
    }

    // offset styling
    QString offsetColorStr = skinContext.selectString(node, QStringLiteral("OffsetColor"));
    if (!offsetColorStr.isEmpty()) {
        m_style.offsetColor = QColor(offsetColorStr);
        m_style.offsetColor = WSkinColor::getCorrectColor(m_style.offsetColor).toRgb();
    }

    QString offsetWidthStr = skinContext.selectString(node, QStringLiteral("OffsetWidth"));
    if (!offsetWidthStr.isEmpty()) {
        m_style.offsetWidth = offsetWidthStr.toInt();
    }

    QString offsetLineStyleStr = skinContext.selectString(node, QStringLiteral("OffsetLineStyle"));
    if (!offsetLineStyleStr.isEmpty()) {
        if (offsetLineStyleStr == "solid")
            m_style.offsetLineStyle = Qt::SolidLine;
        else if (offsetLineStyleStr == "dash")
            m_style.offsetLineStyle = Qt::DashLine;
        else if (offsetLineStyleStr == "dot")
            m_style.offsetLineStyle = Qt::DotLine;
        else if (offsetLineStyleStr == "dashdot")
            m_style.offsetLineStyle = Qt::DashDotLine;
    }

    // visibility -> future
    QString showCurveStr = skinContext.selectString(node, QStringLiteral("ShowCurve"));
    if (!showCurveStr.isEmpty()) {
        m_style.showCurve = (showCurveStr.compare("true", Qt::CaseInsensitive) == 0);
    }

    QString showMarkersStr = skinContext.selectString(node, QStringLiteral("ShowMarkers"));
    if (!showMarkersStr.isEmpty()) {
        m_style.showMarkers = (showMarkersStr.compare("true", Qt::CaseInsensitive) == 0);
    }

    QString showLabelsStr = skinContext.selectString(node, QStringLiteral("ShowLabels"));
    if (!showLabelsStr.isEmpty()) {
        m_style.showLabels = (showLabelsStr.compare("true", Qt::CaseInsensitive) == 0);
    }

    QString showTrackStartStr = skinContext.selectString(node, QStringLiteral("ShowTrackStart"));
    if (!showTrackStartStr.isEmpty()) {
        m_style.showTrackStart = (showTrackStartStr.compare("true", Qt::CaseInsensitive) == 0);
    }

    QString showOffsetIndicatorStr = skinContext.selectString(
            node, QStringLiteral("ShowOffsetIndicator"));
    if (!showOffsetIndicatorStr.isEmpty()) {
        m_style.showOffsetIndicator = (showOffsetIndicatorStr.compare("true",
                                               Qt::CaseInsensitive) == 0);
    }

    QString showDiamondsStr = skinContext.selectString(node, QStringLiteral("ShowDiamonds"));
    if (!showDiamondsStr.isEmpty()) {
        m_style.showDiamonds = (showDiamondsStr.compare("true", Qt::CaseInsensitive) == 0);
    }

    // offset from skin -< future
    QString offsetStr = skinContext.selectString(node, QStringLiteral("OffsetSeconds"));
    if (!offsetStr.isEmpty()) {
        m_offsetSeconds = offsetStr.toDouble();
    }

    // BPM curve data
    loadBpmCurve();

    // Calculate BPM range
    calculateBpmRange();
}

void WaveformRenderBpmCurve::loadBpmCurve() {
    // current track from the renderer
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();

    if (!pTrack) {
        qDebug() << "[WaveformRenderBpmCurve] No track loaded";
        return;
    }

    // track ID
    if (!pTrack->getId().isValid()) {
        qDebug() << "[WaveformRenderBpmCurve] Track has no valid ID";
        return;
    }

    QString trackIdStr = pTrack->getId().toString();
    if (trackIdStr.isEmpty()) {
        qDebug() << "[WaveformRenderBpmCurve] Track ID string is empty";
        return;
    }

    // path to JSON file
    QString bpmDir = QStandardPaths::writableLocation(
                             QStandardPaths::AppLocalDataLocation) +
            "/bpmcurve/";
    QString jsonPath = bpmDir + trackIdStr + ".json";

    qDebug() << "[WaveformRenderBpmCurve] Loading BPM curve from:" << jsonPath;

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[WaveformRenderBpmCurve] Cannot open BPM JSON:" << jsonPath;
        return;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        qDebug() << "[WaveformRenderBpmCurve] Invalid JSON document";
        return;
    }

    if (!doc.isObject()) {
        qDebug() << "[WaveformRenderBpmCurve] JSON is not an object";
        return;
    }

    QJsonObject rootObj = doc.object();
    QJsonArray bpmArray;

    if (rootObj.contains("bpm_curve") && rootObj["bpm_curve"].isArray()) {
        bpmArray = rootObj["bpm_curve"].toArray();
    } else {
        qDebug() << "[WaveformRenderBpmCurve] No BPM array found in JSON";
        return;
    }

    m_segments.clear();

    // 1st element: "bpm_start" or "bpm"
    bool isSegmentFormat = false;
    if (!bpmArray.isEmpty() && bpmArray[0].isObject()) {
        QJsonObject firstObj = bpmArray[0].toObject();
        isSegmentFormat = firstObj.contains("bpm_start") && firstObj.contains("bpm_end");
    }

    if (isSegmentFormat) {
        // Load as segments directly
        for (const QJsonValue& val : std::as_const(bpmArray)) {
            if (!val.isObject())
                continue;

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
        qDebug() << "[WaveformRenderBpmCurve] Loaded" << m_segments.size() << "BPM segments";
    }

    // offset from JSON if present
    if (rootObj.contains("track") && rootObj["track"].isObject()) {
        QJsonObject trackObj = rootObj["track"].toObject();
        if (trackObj.contains("offset_seconds")) {
            m_offsetSeconds = trackObj["offset_seconds"].toDouble();
            qDebug() << "[WaveformRenderBpmCurve] Loaded offset from JSON:"
                     << m_offsetSeconds << "s";
        }
    }

    calculateBpmRange();
}

void WaveformRenderBpmCurve::onSetTrack() {
    qDebug() << "[WaveformRenderBpmCurve] onSetTrack called";
    loadBpmCurve();
    calculateBpmRange();
}

void WaveformRenderBpmCurve::calculateBpmRange() {
    if (m_segments.isEmpty()) {
        m_minBpm = 0;
        m_maxBpm = 0;
        m_yMinBpm = 0;
        m_yMaxBpm = 0;
        return;
    }

    m_minBpm = m_segments[0].bpm_start;
    m_maxBpm = m_segments[0].bpm_start;

    for (const auto& seg : std::as_const(m_segments)) {
        if (seg.bpm_start < m_minBpm)
            m_minBpm = seg.bpm_start;
        if (seg.bpm_start > m_maxBpm)
            m_maxBpm = seg.bpm_start;
        if (seg.bpm_end < m_minBpm)
            m_minBpm = seg.bpm_end;
        if (seg.bpm_end > m_maxBpm)
            m_maxBpm = seg.bpm_end;
    }

    // 10% extra Y-axis
    double bpmRange = m_maxBpm - m_minBpm;
    double padding = bpmRange * 0.1;
    if (padding < 0.5)
        padding = 0.5;

    m_yMinBpm = m_minBpm - padding;
    m_yMaxBpm = m_maxBpm + padding;

    qDebug() << "[WaveformRenderBpmCurve] BPM range:" << m_minBpm << "-" << m_maxBpm;
    qDebug() << "[WaveformRenderBpmCurve] Y-axis range:" << m_yMinBpm << "-" << m_yMaxBpm;
}

void WaveformRenderBpmCurve::setOffset(double offsetSeconds) {
    m_offsetSeconds = offsetSeconds;
}

double WaveformRenderBpmCurve::getPositionWithOffset(double positionSeconds) const {
    return positionSeconds + m_offsetSeconds;
}

double WaveformRenderBpmCurve::mapBpmToY(
        double bpm, double yMinBpm, double yMaxBpm, double height) {
    double bpmRange = yMaxBpm - yMinBpm;
    if (bpmRange <= 0)
        return height / 2;

    double normalized = (bpm - yMinBpm) / bpmRange;
    normalized = qBound(0.0, normalized, 1.0);
    return height - (normalized * height);
}

void WaveformRenderBpmCurve::drawLabel(QPainter* painter,
        const QPointF& position,
        double bpm,
        Qt::Orientation orientation) {
    QString labelText = QString::number(bpm, 'f', m_style.labelDecimalPlaces);
    QRect textRect = painter->fontMetrics().boundingRect(labelText);
    int padding = 4;

    QPointF labelPos;
    if (orientation == Qt::Horizontal) {
        labelPos = QPointF(position.x() - textRect.width() / 2 - padding,
                position.y() + m_style.labelOffset);

        if (labelPos.x() < 0) {
            labelPos.setX(0);
        } else if (labelPos.x() + textRect.width() + padding * 2 > painter->device()->width()) {
            labelPos.setX(painter->device()->width() - textRect.width() - padding * 2);
        }

        // Draw background - explicit int casts
        QRect bgRect(static_cast<int>(labelPos.x() - padding),
                static_cast<int>(labelPos.y() - padding),
                textRect.width() + padding * 2,
                textRect.height() + padding * 2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_style.labelBackgroundColor);
        painter->drawRoundedRect(bgRect, 3, 3);

        // Draw text - explicit int casts
        painter->setPen(QPen(m_style.labelTextColor, 1));
        painter->drawText(static_cast<int>(labelPos.x()),
                static_cast<int>(labelPos.y() + textRect.height()),
                labelText);
    }
}

void WaveformRenderBpmCurve::draw(QPainter* painter, QPaintEvent* /*event*/) {
    if (!m_visible || m_segments.isEmpty()) {
        return;
    }

    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack) {
        return;
    }

    double trackLengthSeconds = pTrack->getDuration();
    double trackSamples = m_waveformRenderer->getTrackSamples();

    if (trackLengthSeconds <= 0 || trackSamples <= 0) {
        return;
    }

    // Get visible range
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

    // Set up font for labels
    QFont labelFont = painter->font();
    labelFont.setPointSize(m_style.labelFontSize);
    labelFont.setBold(true);
    painter->setFont(labelFont);

    // draw curve STABLE / INCREASE / DECREASE
    painter->setPen(QPen(m_style.curveColor, m_style.curveWidth));

    QVector<QLineF> curveLines;

    for (const auto& seg : std::as_const(m_segments)) {
        double startTime = seg.position + m_offsetSeconds;
        double endTime = seg.range_end + m_offsetSeconds;

        double startBpm = seg.bpm_start;
        double endBpm = seg.bpm_end;

        // sample positions
        double startPos = startTime * (trackSamples / trackLengthSeconds);
        double endPos = endTime * (trackSamples / trackLengthSeconds);

        // outside visible range
        if (endPos < startSample || startPos > endSample) {
            continue;
        }

        double x1 = m_waveformRenderer->transformSamplePositionInRendererWorld(
                startPos, ::WaveformRendererAbstract::Play);
        double x2 = m_waveformRenderer->transformSamplePositionInRendererWorld(
                endPos, ::WaveformRendererAbstract::Play);
        double y1 = mapBpmToY(startBpm, m_yMinBpm, m_yMaxBpm, rendererHeight);
        double y2 = mapBpmToY(endBpm, m_yMinBpm, m_yMaxBpm, rendererHeight);

        if (orientation == Qt::Vertical) {
            std::swap(x1, y1);
            std::swap(x2, y2);
        }

        x1 = qBound(0.0, x1, (double)rendererWidth);
        x2 = qBound(0.0, x2, (double)rendererWidth);
        y1 = qBound(0.0, y1, (double)rendererHeight);
        y2 = qBound(0.0, y2, (double)rendererHeight);

        curveLines.append(QLineF(x1, y1, x2, y2));
    }

    if (!curveLines.isEmpty()) {
        painter->drawLines(curveLines);
    }

    // draw markers
    if (m_style.showMarkers) {
        painter->setPen(QPen(m_style.markerColor, m_style.markerWidth, m_style.markerLineStyle));

        QVector<QLineF> markerLines;
        QVector<std::pair<QPointF, double>> labelPositions;

        for (const auto& seg : std::as_const(m_segments)) {
            double boundaryTime = seg.position + m_offsetSeconds;
            double boundaryPos = boundaryTime * (trackSamples / trackLengthSeconds);

            if (boundaryPos >= startSample && boundaryPos <= endSample) {
                double x = m_waveformRenderer->transformSamplePositionInRendererWorld(
                        boundaryPos, ::WaveformRendererAbstract::Play);

                if (x >= 0 && x <= rendererWidth) {
                    if (orientation == Qt::Horizontal) {
                        markerLines.append(QLineF(x, 0, x, rendererHeight));
                        labelPositions.append({QPointF(x, 0), seg.bpm_start});
                    } else {
                        markerLines.append(QLineF(0, x, rendererWidth, x));
                        labelPositions.append({QPointF(0, x), seg.bpm_start});
                    }
                }
            }
        }

        if (!markerLines.isEmpty()) {
            painter->drawLines(markerLines);
        }

        // draw marker symbols
        if (m_style.showDiamonds && !markerLines.isEmpty()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_style.markerColor);
            for (const QLineF& line : std::as_const(markerLines)) {
                QPointF topPoint = line.p1();
                QPolygonF diamond;
                diamond << QPointF(topPoint.x(), topPoint.y() - 4)
                        << QPointF(topPoint.x() + 4, topPoint.y())
                        << QPointF(topPoint.x(), topPoint.y() + 4)
                        << QPointF(topPoint.x() - 4, topPoint.y());
                painter->drawPolygon(diamond);
            }
        }

        // draw labels
        if (m_style.showLabels) {
            for (const auto& labelInfo : labelPositions) {
                drawLabel(painter, labelInfo.first, labelInfo.second, orientation);
            }
        }
    }

    // draw track start
    if (m_style.showTrackStart) {
        painter->setPen(QPen(m_style.trackStartColor, m_style.trackStartWidth, Qt::SolidLine));
        double trackStartX = m_waveformRenderer->transformSamplePositionInRendererWorld(
                0.0, ::WaveformRendererAbstract::Play);
        if (trackStartX >= 0 && trackStartX <= rendererWidth) {
            if (orientation == Qt::Horizontal) {
                // painter->drawLine(trackStartX, 0, trackStartX, rendererHeight);
                painter->drawLine(static_cast<int>(trackStartX),
                        0,
                        static_cast<int>(trackStartX),
                        static_cast<int>(rendererHeight));
            } else {
                // painter->drawLine(0, trackStartX, rendererWidth, trackStartX);
                painter->drawLine(0,
                        static_cast<int>(trackStartX),
                        static_cast<int>(rendererWidth),
                        static_cast<int>(trackStartX));
            }
        }
    }

    // draw offset
    if (m_style.showOffsetIndicator && !m_segments.isEmpty() && std::abs(m_offsetSeconds) > 0.001) {
        painter->setPen(QPen(m_style.offsetColor, m_style.offsetWidth, m_style.offsetLineStyle));

        double firstOffsetPosition = m_segments[0].position + m_offsetSeconds;
        double offsetSample = firstOffsetPosition * (trackSamples / trackLengthSeconds);
        double offsetX = m_waveformRenderer->transformSamplePositionInRendererWorld(
                offsetSample, ::WaveformRendererAbstract::Play);

        if (offsetX >= 0 && offsetX <= rendererWidth) {
            if (orientation == Qt::Horizontal) {
                // painter->drawLine(offsetX, 0, offsetX, rendererHeight);
                painter->drawLine(static_cast<int>(offsetX),
                        0,
                        static_cast<int>(offsetX),
                        static_cast<int>(rendererHeight));
            } else {
                // painter->drawLine(0, offsetX, rendererWidth, offsetX);
                painter->drawLine(0,
                        static_cast<int>(offsetX),
                        static_cast<int>(rendererWidth),
                        static_cast<int>(offsetX));
            }
        }
    }
}
