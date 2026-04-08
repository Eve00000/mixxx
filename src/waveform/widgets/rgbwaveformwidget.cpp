
#include "rgbwaveformwidget.h"

#include <QPainter>

#include "moc_rgbwaveformwidget.cpp"
#include "waveform/renderers/waveformrenderbackground.h"
#include "waveform/renderers/waveformrenderbeat.h"
#include "waveform/renderers/waveformrenderbpmcurve.h"
#include "waveform/renderers/waveformrendererendoftrack.h"
#include "waveform/renderers/waveformrendererpreroll.h"
#include "waveform/renderers/waveformrendererrgb.h"
#include "waveform/renderers/waveformrenderkeycurve.h"
#include "waveform/renderers/waveformrendermark.h"
#include "waveform/renderers/waveformrendermarkrange.h"

RGBWaveformWidget::RGBWaveformWidget(const QString& group,
        QWidget* parent,
        WaveformRendererSignalBase::Options options)
        : NonGLWaveformWidgetAbstract(group, parent) {
    addRenderer<WaveformRenderBackground>();
    addRenderer<WaveformRendererEndOfTrack>();
    addRenderer<WaveformRendererPreroll>();
    addRenderer<WaveformRenderMarkRange>();
    addRenderer<WaveformRendererRGB>(options);
    m_rendererStack.push_back(new WaveformRenderBpmCurve(this));
    m_rendererStack.push_back(new WaveformRenderKeyCurve(this));
    addRenderer<WaveformRenderBeat>();
    addRenderer<WaveformRenderMark>();

    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_initSuccess = init();
}

RGBWaveformWidget::~RGBWaveformWidget() {
}

void RGBWaveformWidget::castToQWidget() {
    m_widget = this;
}

void RGBWaveformWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    draw(&painter, event);
}

// #include "rgbwaveformwidget.h"
//
// #include <QPainter>
// #include <QTimer>
//
// #include "moc_rgbwaveformwidget.cpp"
// #include "track/track.h"
// #include "waveform/renderers/camelotwheeloverlay.h"
// #include "waveform/renderers/waveformrenderbackground.h"
// #include "waveform/renderers/waveformrenderbeat.h"
// #include "waveform/renderers/waveformrenderbpmcurve.h"
// #include "waveform/renderers/waveformrendererendoftrack.h"
// #include "waveform/renderers/waveformrendererpreroll.h"
// #include "waveform/renderers/waveformrendererrgb.h"
// #include "waveform/renderers/waveformrenderkeycurve.h"
// #include "waveform/renderers/waveformrendermark.h"
// #include "waveform/renderers/waveformrendermarkrange.h"
// #include "waveform/visualplayposition.h"
//
// RGBWaveformWidget::RGBWaveformWidget(const QString& group,
//         QWidget* parent,
//         WaveformRendererSignalBase::Options options)
//         : NonGLWaveformWidgetAbstract(group, parent) {
//     addRenderer<WaveformRenderBackground>();
//     addRenderer<WaveformRendererEndOfTrack>();
//     addRenderer<WaveformRendererPreroll>();
//     addRenderer<WaveformRenderMarkRange>();
//     addRenderer<WaveformRendererRGB>(options);
//     m_rendererStack.push_back(new WaveformRenderBpmCurve(this));
//     m_rendererStack.push_back(new WaveformRenderKeyCurve(this));
//     addRenderer<WaveformRenderBeat>();
//     addRenderer<WaveformRenderMark>();
//
//     setAttribute(Qt::WA_NoSystemBackground);
//     setAttribute(Qt::WA_OpaquePaintEvent);
//
//     // Create the Camelot wheel overlay
//     m_pCamelotWheel = std::make_unique<CamelotWheelOverlay>(this);
//     // m_pCamelotWheel->move(width() - 190, 20);
//     m_pCamelotWheel->init(group);
//     //m_pCamelotWheel->setVisible(false);
//     m_pCamelotWheel->setVisible(true);
//     //m_pCamelotWheel->setVisible(true);    // Temporarily force visible for
//     testing
//     // m_pCamelotWheel->setCurrentKey("Am"); // Set a test key
//     // qDebug() << "[RGBWaveformWidget] Camelot wheel created, size:"
//     //         << m_pCamelotWheel->size() << "position:" <<
//     m_pCamelotWheel->pos();
//     //  Create timer to update Camelot wheel position
//     //m_positionUpdateTimer = new QTimer(this);
//     //m_positionUpdateTimer->setInterval(100); // Update 10 times per second
//     //connect(m_positionUpdateTimer, &QTimer::timeout, this,
//     &RGBWaveformWidget::updateCamelotWheelPosition);
//     //m_positionUpdateTimer->start();
//
//     m_initSuccess = init();
// }
//
// RGBWaveformWidget::~RGBWaveformWidget() {
// }
//
// void RGBWaveformWidget::castToQWidget() {
//     m_widget = this;
// }
//
// void RGBWaveformWidget::paintEvent(QPaintEvent* event) {
//     QPainter painter(this);
//     draw(&painter, event);
//
//     if (m_pCamelotWheel && m_pCamelotWheel->isVisible()) {
//         m_pCamelotWheel->raise();
//     }
// }
//
////void RGBWaveformWidget::onTrackLoaded(TrackPointer pTrack) {
////    if (!pTrack) {
////        if (m_pCamelotWheel) {
////            m_pCamelotWheel->setVisible(false);
////        }
////        return;
////    }
////
////    if (m_pCamelotWheel) {
////        m_pCamelotWheel->loadKeyCurveData(pTrack);
////        m_pCamelotWheel->setVisible(true);
////    }
////}
//
// void RGBWaveformWidget::onTrackLoaded(TrackPointer pTrack) {
//    qDebug() << "[RGBWaveformWidget] onTrackLoaded called, track:" << (pTrack
//    ? pTrack->getTitle() : "null");
//
//    if (!pTrack) {
//        if (m_pCamelotWheel) {
//            m_pCamelotWheel->setVisible(false);
//        }
//        return;
//    }
//
//    if (m_pCamelotWheel) {
//        // First try to get segments from the key curve renderer
//        for (auto* renderer : m_rendererStack) {
//            WaveformRenderKeyCurve* keyCurve =
//            dynamic_cast<WaveformRenderKeyCurve*>(renderer); if (keyCurve) {
//                const QVector<KeySegment>& segments =
//                keyCurve->getKeySegments(); if (!segments.isEmpty()) {
//                    qDebug() << "[RGBWaveformWidget] Got" << segments.size()
//                    << "segments from key curve";
//                    m_pCamelotWheel->setKeySegments(segments);
//                    m_pCamelotWheel->setVisible(true);
//                    return;
//                }
//            }
//        }
//
//        // If no segments from renderer, load directly from JSON
//        qDebug() << "[RGBWaveformWidget] Loading key curve data directly";
//        m_pCamelotWheel->loadKeyCurveData(pTrack);
//        m_pCamelotWheel->setVisible(true);
//    }
//}
//
// void RGBWaveformWidget::resizeEvent(QResizeEvent* event) {
//    NonGLWaveformWidgetAbstract::resizeEvent(event);
//
//    if (m_pCamelotWheel) {
//        // Position at top-right corner with some margin
//        int margin = 10;
//        int x = width() - m_pCamelotWheel->width() - margin;
//        int y = margin;
//        m_pCamelotWheel->move(x, y);
//        m_pCamelotWheel->raise();
//    }
//}
//
////void RGBWaveformWidget::paintEvent(QPaintEvent* event) {
////    QPainter painter(this);
////    draw(&painter, event);
////
////    //if (m_pCamelotWheel && m_pCamelotWheel->isVisible()) {
////    //    m_pCamelotWheel->raise();
////    //}
////    if (m_pCamelotWheel) {
////        m_pCamelotWheel->raise();
//////        m_pCamelotWheel->show(); // Ensure it's shown
//////        qDebug() << "[RGBWaveformWidget] Camelot wheel visible:" <<
/// m_pCamelotWheel->isVisible(); /    }
////}
////
////void RGBWaveformWidget::onTrackLoaded(TrackPointer pTrack) {
////    if (!pTrack) {
////        if (m_pCamelotWheel) {
////            m_pCamelotWheel->setVisible(false);
////        }
////        return;
////    }
////
////    if (m_pCamelotWheel) {
////        // Load key segments directly from JSON file
////        // m_pCamelotWheel->loadKeyCurveData();
////        m_pCamelotWheel->setVisible(true);
////    }
////}
////
////void RGBWaveformWidget::updateCamelotWheelPosition() {
////    if (!m_pCamelotWheel || !m_pCamelotWheel->isVisible()) {
////        return;
////    }
////
////    // Get the current play position from VisualPlayPosition
////    QSharedPointer<VisualPlayPosition> pVisPlayPos =
////            VisualPlayPosition::getVisualPlayPosition(m_group);
////
////    if (pVisPlayPos) {
////        double playPosition = pVisPlayPos->getEnginePlayPos();
////        if (playPosition >= 0) {
////            // Get track duration to convert to seconds if needed
////            TrackPointer pTrack = getTrackInfo();
////            if (pTrack) {
////                double duration = pTrack->getDuration();
////                if (duration > 0) {
////                    // playPosition is typically 0-1 (percentage), convert
/// to seconds /                    double positionSeconds = playPosition *
/// duration; / m_pCamelotWheel->updateFromKeyCurve(positionSeconds); / } / } / }
////    }
////}
////
////void RGBWaveformWidget::onPlayPositionChanged(double positionSeconds) {
////    qDebug() << "[RGBWaveformWidget] Play position changed:" <<
/// positionSeconds; /    if (m_pCamelotWheel && m_pCamelotWheel->isVisible()) {
////        m_pCamelotWheel->updateFromKeyCurve(positionSeconds);
////    }
////}
////
////void RGBWaveformWidget::resizeEvent(QResizeEvent* event) {
////    NonGLWaveformWidgetAbstract::resizeEvent(event);
////
////    if (m_pCamelotWheel) {
////        // Reposition to top right corner
////        m_pCamelotWheel->move(width() - 190, 20);
////        m_pCamelotWheel->raise();
////        m_pCamelotWheel->show();
////    }
////}
//
//
////void RGBWaveformWidget::onTrackLoaded(TrackPointer pTrack) {
////    if (!pTrack) {
////        if (m_pCamelotWheel) {
////            m_pCamelotWheel->setVisible(false);
////        }
////        return;
////    }
////
////    // Get key segments from the key curve renderer
////    if (m_pCamelotWheel) {
////        for (auto* renderer : m_rendererStack) {
////            WaveformRenderKeyCurve* keyCurve =
/// dynamic_cast<WaveformRenderKeyCurve*>(renderer); /            if (keyCurve) {
////                // Pass the segments to Camelot wheel
////                m_pCamelotWheel->setKeySegments(keyCurve->getKeySegments());
////                break;
////            }
////        }
////        m_pCamelotWheel->setVisible(true);
////    }
////    m_currentKey.clear();
////}
//
////void RGBWaveformWidget::onTrackLoaded(TrackPointer pTrack) {
////    if (!pTrack) {
////        if (m_pCamelotWheel) {
////            m_pCamelotWheel->setVisible(false);
////        }
////        return;
////    }
////
////    if (m_pCamelotWheel) {
////        // Get key segments from the key curve renderer
////        for (auto* renderer : m_rendererStack) {
////            WaveformRenderKeyCurve* keyCurve =
/// dynamic_cast<WaveformRenderKeyCurve*>(renderer); /            if (keyCurve) {
////                const QVector<KeySegment>& segments =
/// keyCurve->getKeySegments(); /                qDebug() << "[RGBWaveformWidget]
/// Found" << segments.size() << "key segments";
////
////                m_pCamelotWheel->setKeySegments(segments);
////
////                // Set initial key to first segment
////                if (!segments.isEmpty()) {
////                    m_pCamelotWheel->setCurrentKey(segments[0].key);
////                }
////                break;
////            }
////        }
////        m_pCamelotWheel->setVisible(true);
////    }
////}
//
//
////#include "rgbwaveformwidget.h"
////
////#include <QPainter>
////
////#include "moc_rgbwaveformwidget.cpp"
////#include "waveform/renderers/camelotwheeloverlay.h"
////#include "waveform/renderers/waveformrenderbackground.h"
////#include "waveform/renderers/waveformrenderbeat.h"
////#include "waveform/renderers/waveformrenderbpmcurve.h"
////#include "waveform/renderers/waveformrenderkeycurve.h"
////#include "waveform/renderers/waveformrendererendoftrack.h"
////#include "waveform/renderers/waveformrendererpreroll.h"
////#include "waveform/renderers/waveformrendererrgb.h"
////#include "waveform/renderers/waveformrendermark.h"
////#include "waveform/renderers/waveformrendermarkrange.h"
////
////RGBWaveformWidget::RGBWaveformWidget(const QString& group,
////        QWidget* parent,
////        WaveformRendererSignalBase::Options options)
////        : NonGLWaveformWidgetAbstract(group, parent) {
////    addRenderer<WaveformRenderBackground>();
////    addRenderer<WaveformRendererEndOfTrack>();
////    addRenderer<WaveformRendererPreroll>();
////    addRenderer<WaveformRenderMarkRange>();
////    addRenderer<WaveformRendererRGB>(options);
////    m_rendererStack.push_back(new WaveformRenderBpmCurve(this));
////    m_rendererStack.push_back(new WaveformRenderKeyCurve(this));
////    //addRenderer<WaveformRenderKeyCurve>();
////    addRenderer<WaveformRenderBeat>();
////    addRenderer<WaveformRenderMark>();
////
////    setAttribute(Qt::WA_NoSystemBackground);
////    setAttribute(Qt::WA_OpaquePaintEvent);
////
////     // Create the Camelot wheel overlay
////    m_pCamelotWheel = std::make_unique<CamelotWheelOverlay>(this);
////    m_pCamelotWheel->move(width() - 190, 20); // Top right corner
////
////    // Initially hidden until track is loaded and analyzed
////    m_pCamelotWheel->setVisible(false);
////
////    m_initSuccess = init();
////}
////
////RGBWaveformWidget::~RGBWaveformWidget() {
////}
////
////void RGBWaveformWidget::castToQWidget() {
////    m_widget = this;
////}
////
////void RGBWaveformWidget::paintEvent(QPaintEvent* event) {
////    QPainter painter(this);
////    draw(&painter,event);
////
////        // Ensure the Camelot wheel is on top
////    if (m_pCamelotWheel && m_pCamelotWheel->isVisible()) {
////        m_pCamelotWheel->raise();
////    }
////}
////
//////void RGBWaveformWidget::onPlayPositionChanged(double positionSeconds) {
//////    updateCurrentKeyFromPosition(positionSeconds);
//////}
////
////void RGBWaveformWidget::onPlayPositionChanged(double positionSeconds) {
////    if (m_pCamelotWheel && m_pCamelotWheel->isVisible()) {
////        m_pCamelotWheel->updateFromKeyCurve(positionSeconds);
////    }
////}
////
////void RGBWaveformWidget::onTrackLoaded(TrackPointer pTrack) {
////    if (!pTrack) {
////        m_pCamelotWheel->setVisible(false);
////        return;
////    }
////
////    // Get key segments from the key curve renderer
////    for (auto* renderer : m_rendererStack) {
////        WaveformRenderKeyCurve* keyCurve =
/// dynamic_cast<WaveformRenderKeyCurve*>(renderer); /        if (keyCurve) { /
///// Pass the segments to Camelot wheel /
/// m_pCamelotWheel->setKeySegments(keyCurve->getKeySegments()); / break; / } / }
////
////    m_pCamelotWheel->setVisible(true);
////    m_currentKey.clear();
////}
////
////void RGBWaveformWidget::updateCurrentKeyFromPosition(double positionSeconds)
///{ /    QString newKey = getKeyAtPosition(positionSeconds);
////
////    if (newKey != m_currentKey && !newKey.isEmpty()) {
////        m_currentKey = newKey;
////        if (m_pCamelotWheel) {
////            m_pCamelotWheel->setCurrentKey(m_currentKey);
////        }
////    }
////}
////
////QString RGBWaveformWidget::getKeyAtPosition(double positionSeconds) const {
////    // Get the key curve renderer to access the segments
////    for (auto* renderer : m_rendererStack) {
////        WaveformRenderKeyCurve* keyCurve =
/// dynamic_cast<WaveformRenderKeyCurve*>(renderer); /        if (keyCurve) { /
///// We need to add a method to WaveformRenderKeyCurve to get segments / // For
/// now, we'll return empty /            // TODO: Add getKeySegments() method to
/// WaveformRenderKeyCurve /            break; /        } /    }
////
////    // Alternative: Get from track metadata
////    TrackPointer pTrack = getTrackInfo();
////    if (pTrack) {
////        // This would need to load from the JSON file
////        // For simplicity, we'll rely on the renderer to provide this info
////    }
////
////    return QString();
////}
//
