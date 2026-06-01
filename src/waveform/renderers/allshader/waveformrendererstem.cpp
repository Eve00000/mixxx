#include "waveform/renderers/allshader/waveformrendererstem.h"

#include <QFont>
#include <QImage>
#include <QOpenGLTexture>

#include "control/controlproxy.h"
#include "engine/channels/enginedeck.h"
#include "engine/engine.h"
#include "rendergraph/material/rgbamaterial.h"
#include "rendergraph/vertexupdaters/rgbavertexupdater.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/math.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"
#include "waveform/waveformwidgetfactory.h"

namespace {
#ifdef __SCENEGRAPH__
// FIXME this is a workaround an issue with waveform only drawing partially in
// SG. The workaround is to reduce the the number of vertices, by reducing the
// precision of waveform strips.
const float kPixelPerStrip = 2;
#else
const float kPixelPerStrip = 1;
#endif
} // namespace

using namespace rendergraph;

namespace allshader {

WaveformRendererStem::WaveformRendererStem(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type,
        ::WaveformRendererSignalBase::Options options)
        : WaveformRendererSignalBase(waveformWidget, options),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
          m_splitStemTracks(false),
          m_outlineOpacity(0.15f),
          m_opacity(0.75f) {
    initForRectangles<RGBAMaterial>(0);
    setUsePreprocess(true);
}

void WaveformRendererStem::onSetup(const QDomNode&) {
}

bool WaveformRendererStem::init() {
    m_pStemGain.clear();
    m_pStemMute.clear();
    if (m_waveformRenderer->getGroup().isEmpty()) {
        return true;
    }
    for (int stemIdx = 0; stemIdx < mixxx::kMaxSupportedStems; stemIdx++) {
        QString stemGroup = EngineDeck::getGroupForStem(m_waveformRenderer->getGroup(), stemIdx);
        // m_pStemGain.emplace_back(
        //         std::make_unique<ControlProxy>(stemGroup,
        //                 QStringLiteral("volume")));
        if (stemIdx == 0) {
            // Push a null placeholder so that stems 1-4 line up at indexes 1-4 perfectly
            m_pStemGain.emplace_back(nullptr);
        } else {
            m_pStemGain.emplace_back(
                    std::make_unique<ControlProxy>(stemGroup, QStringLiteral("volume")));
        }


        m_pStemMute.emplace_back(
                std::make_unique<ControlProxy>(stemGroup,
                        QStringLiteral("mute")));
        auto bringToForeground = [this, stemIdx](double) {
            if (!m_reorderOnChange) {
                return;
            }
            m_stackOrder.removeAll(stemIdx);
            m_stackOrder.append(stemIdx);
        };
        if (m_pStemGain.back()) {
            m_pStemGain.back()->connectValueChanged(this, bringToForeground);
        }
        m_pStemMute.back()->connectValueChanged(this, bringToForeground);
    }

    m_stackOrder.resize(mixxx::kMaxSupportedStems);
    std::iota(m_stackOrder.begin(), m_stackOrder.end(), 0);

#ifndef __SCENEGRAPH__
    auto* pWaveformWidgetFactory = WaveformWidgetFactory::instance();
    setReorderOnChange(pWaveformWidgetFactory->isStemReorderOnChange());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::stemReorderOnChangeChanged,
            this,
            &WaveformRendererStem::setReorderOnChange);
    setOutlineOpacity(pWaveformWidgetFactory->getStemOutlineOpacity());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::stemOutlineOpacityChanged,
            this,
            &WaveformRendererStem::setOutlineOpacity);
    setOpacity(pWaveformWidgetFactory->getStemOpacity());
    connect(pWaveformWidgetFactory,
            &WaveformWidgetFactory::stemOpacityChanged,
            this,
            &WaveformRendererStem::setOpacity);
#endif
    return true;
}

void WaveformRendererStem::preprocess() {
    if (!preprocessInner()) {
        if (geometry().vertexCount() != 0) {
            geometry().allocate(0);
            markDirtyGeometry();
        }
    }
}

bool WaveformRendererStem::preprocessInner() {
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    auto stemInfo = pTrack->getStemInfo();
    // If this track isn't a stem track, skip the rendering
    if (stemInfo.isEmpty()) {
        return false;
    }

    ConstWaveformPointer waveform = pTrack->getWaveform();
    if (waveform.isNull()) {
        return false;
    }

    const WaveformData* data = waveform->data();
    if (!data || waveform->getDataSize() <= 1 || !waveform->hasStem()) {
        return false;
    }

    uint selectedStems = m_waveformRenderer->getSelectedStems();

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const int length = static_cast<int>(m_waveformRenderer->getLength());
    const int pixelLength = static_cast<int>(length * devicePixelRatio);
    const int stripLength = static_cast<int>(static_cast<float>(pixelLength) / kPixelPerStrip);
    const float invDevicePixelRatio = kPixelPerStrip / devicePixelRatio;
    const float halfStripSize = kPixelPerStrip / 2.0f / devicePixelRatio;

    // === FIXED: Kept exactly your historical version's method signature ===
    float allGain = 1.0f;
    getGains(&allGain, nullptr, nullptr, nullptr);

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth());
    const float stemBreadth = m_splitStemTracks ? breadth / 4.0f : 0;
    const float halfBreadth = (m_splitStemTracks ? stemBreadth : breadth) / 2.0f;

    const float heightFactor = allGain * halfBreadth / m_maxValue;

    const int dataSize = waveform->getDataSize();
    const double visualFramesSize = dataSize / 2.0;

    const double firstVisualFrame =
            m_waveformRenderer->getFirstDisplayedPosition(
                    m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                     : ::WaveformRendererAbstract::Play) *
            visualFramesSize;
    const double lastVisualFrame =
            m_waveformRenderer->getLastDisplayedPosition(
                    m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                     : ::WaveformRendererAbstract::Play) *
            visualFramesSize;

    // Represents the # of visual frames per horizontal pixel.
    const double visualIncrementPerPixel = (lastVisualFrame - firstVisualFrame) / stripLength;
    
    // Effective visual frame for x
    double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
            visualIncrementPerPixel;

    const int numVerticesPerLine = 6; // 2 triangles per rectangle
    const int numStemsToDraw = mixxx::audio::ChannelCount::stem() - 1;
    const int layersPerStem = 2;
    
    // Allocate a maximum theoretical ceiling to avoid mid-loop structural re-allocations
    const int reservedCeiling = numVerticesPerLine * layersPerStem * numStemsToDraw * stripLength;

    geometry().setDrawingMode(Geometry::DrawingMode::Triangles);
    geometry().allocate(reservedCeiling);
    markDirtyGeometry();

    RGBAVertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::RGBAColoredPoint2D>()};

    const double maxSamplingRange = visualIncrementPerPixel / 2.0;
    const bool premixMuted =
            (!m_pStemMute.empty() && m_pStemMute.size() > 0) ? m_pStemMute[0]->toBool() : false;

    for (int visualIdx = 0; visualIdx < stripLength; ++visualIdx) {
        int stemLayer = 0;
        for (int stemIdx : std::as_const(m_stackOrder)) {
            // Skip premix entirely; we never draw it
            if (stemIdx == 0) {
                continue;
            }

            // Map to stemInfo index (0-3) from internal layout tracking index (1-4)
            const int colorIdx = stemIdx - 1;
            if (colorIdx < 0 || colorIdx >= static_cast<int>(stemInfo.size())) {
                continue;
            }

            // Colour for this stem
            const QColor stemColor = stemInfo[colorIdx].getColor();
            const float color_r = stemColor.redF();
            const float color_g = stemColor.greenF();
            const float color_b = stemColor.blueF();
            const float color_a_base = stemColor.alphaF();

            // Window of samples contributing to this pixel column
            const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
            const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);
            const int visualIndexStart = std::max(visualFrameStart * 2, 0);
            const int visualIndexStop = std::min(
                    std::max(visualFrameStop, visualFrameStart + 1) * 2,
                    dataSize - 1);

            const float fVisualIdx = static_cast<float>(visualIdx) * invDevicePixelRatio;

            // Find the max values for current stem layer in the waveform byte data.
            uchar u8max = 0;
            for (int chn = 0; chn < 2; ++chn) {
                for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
                    u8max = math_max(u8max, data[i].stems[stemIdx]);
                }
            }

            const float max = static_cast<float>(u8max);

            // Two layers (outline + fill) for stems
            for (int layerIdx = 0; layerIdx < 2; ++layerIdx) {
                float color_a = color_a_base * (layerIdx ? m_opacity : m_outlineOpacity);

                // Apply the gains -> effective gain behavior:
                // - If premix is unmuted -> show all stems at 100% (bright layout)
                // - Else follow this stem’s individual mute/gain status and selection mask
                float effectiveGain = 1.0f;
                if (premixMuted) {
                    const bool isMuted = (m_pStemMute.size() > static_cast<size_t>(stemIdx))
                            ? m_pStemMute[stemIdx]->toBool()
                            : false;
                    const float volume = (m_pStemGain.size() > static_cast<size_t>(stemIdx))
                            ? static_cast<float>(m_pStemGain[stemIdx]->get())
                            : 1.0f;
                    const bool deselected = selectedStems && !(selectedStems & (1 << stemIdx));
                    effectiveGain = (isMuted || deselected) ? 0.0f : volume;
                }

                const float h = heightFactor * (max * effectiveGain);
                
                vertexUpdater.addRectangle(
                        {fVisualIdx - halfStripSize,
                                stemLayer * stemBreadth + halfBreadth - h},
                        {fVisualIdx + halfStripSize,
                                stemLayer * stemBreadth + halfBreadth +
                                        (m_isSlipRenderer ? 0.f : h)},
                        {color_r, color_g, color_b, color_a});
            }
            ++stemLayer;
        }
        xVisualFrame += visualIncrementPerPixel;
    }

    // Explicitly resize geometry allocation block to vertices actually written
    const int actualVerticesWritten = vertexUpdater.index();
    geometry().allocate(actualVerticesWritten);

    markDirtyMaterial();
    return true;
}


// bool WaveformRendererStem::preprocessInner() {
//     TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
//     if (!pTrack || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
//         return false;
//     }

//     auto stemInfo = pTrack->getStemInfo();
//     if (stemInfo.isEmpty()) {
//         return false;
//     }

//     ConstWaveformPointer waveform = pTrack->getWaveform();
//     if (waveform.isNull()) {
//         return false;
//     }

//     const WaveformData* data = waveform->data();
//     if (!data || waveform->getDataSize() <= 1 || !waveform->hasStem()) {
//         return false;
//     }

//     uint selectedStems = m_waveformRenderer->getSelectedStems();

//     const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
//     const int length = static_cast<int>(m_waveformRenderer->getLength());
//     const int pixelLength = static_cast<int>(length * devicePixelRatio);
//     const int stripLength = static_cast<int>(static_cast<float>(pixelLength) / kPixelPerStrip);
//     const float invDevicePixelRatio = kPixelPerStrip / devicePixelRatio;
//     const float halfStripSize = kPixelPerStrip / 2.0f / devicePixelRatio;

//     float allGain = 1.0f;
//     getGains(&allGain, nullptr, nullptr, nullptr);

//     const float breadth = static_cast<float>(m_waveformRenderer->getBreadth());
//     const float stemBreadth = m_splitStemTracks ? breadth / 4.0f : 0;
//     const float halfBreadth = (m_splitStemTracks ? stemBreadth : breadth) / 2.0f;

//     const float heightFactor = allGain * halfBreadth / m_maxValue;

//     const int dataSize = waveform->getDataSize();
//     const double visualFramesSize = dataSize / 2.0;

//     const double firstVisualFrame =
//             m_waveformRenderer->getFirstDisplayedPosition(
//                     m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
//                                      : ::WaveformRendererAbstract::Play) *
//             visualFramesSize;
//     const double lastVisualFrame =
//             m_waveformRenderer->getLastDisplayedPosition(
//                     m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
//                                      : ::WaveformRendererAbstract::Play) *
//             visualFramesSize;

//     const double visualIncrementPerPixel = (lastVisualFrame - firstVisualFrame) / stripLength;
//     double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
//             visualIncrementPerPixel;

//     const int numVerticesPerLine = 6;
//     const int numStemsToDraw = mixxx::audio::ChannelCount::stem(); // Keep full size to include premix
//     const int layersPerStem = 2;
//     const int reservedCeiling = numVerticesPerLine * layersPerStem * numStemsToDraw * stripLength;

//     geometry().setDrawingMode(Geometry::DrawingMode::Triangles);
//     geometry().allocate(reservedCeiling);
//     markDirtyGeometry();

//     RGBAVertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::RGBAColoredPoint2D>()};

//     const double maxSamplingRange = visualIncrementPerPixel / 2.0;
//     const bool premixMuted =
//             (!m_pStemMute.empty() && m_pStemMute.size() > 0) ? m_pStemMute[0]->toBool() : false;

//     for (int visualIdx = 0; visualIdx < stripLength; ++visualIdx) {
//         int stemLayer = 0;
//         for (int stemIdx : std::as_const(m_stackOrder)) {
            
//             float color_r = 0.5f;
//             float color_g = 0.5f;
//             float color_b = 0.5f;
//             float color_a_base = 1.0f;

//             if (stemIdx == 0) {
//                 // === PREMIX COLOR CONFIGURATION ===
//                 // Give the combined background overview a semi-dark gray, 
//                 // or match standard waveform overview tracks (e.g. RGB 70, 70, 70).
//                 color_r = 0.27f;
//                 color_g = 0.27f;
//                 color_b = 0.27f;
//                 color_a_base = 0.40f; // Softly transparent background layer
//             } else {
//                 // === INDIVIDUAL STEMS COLOR CONFIGURATION ===
//                 const int colorIdx = stemIdx - 1;
//                 if (colorIdx < 0 || colorIdx >= static_cast<int>(stemInfo.size())) {
//                     continue;
//                 }
//                 const QColor stemColor = stemInfo[colorIdx].getColor();
//                 color_r = stemColor.redF();
//                 color_g = stemColor.greenF();
//                 color_b = stemColor.blueF();
//                 color_a_base = stemColor.alphaF();
//             }

//             const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
//             const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);
//             const int visualIndexStart = std::max(visualFrameStart * 2, 0);
//             const int visualIndexStop = std::min(
//                     std::max(visualFrameStop, visualFrameStart + 1) * 2,
//                     dataSize - 1);

//             const float fVisualIdx = static_cast<float>(visualIdx) * invDevicePixelRatio;

//             uchar u8max = 0;
//             for (int chn = 0; chn < 2; ++chn) {
//                 for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
//                     if (stemIdx == 0) {
//                         // Read from the unified filtered byte data array for combined display
//                         u8max = math_max(u8max, data[i].filtered.all);
//                     } else {
//                         u8max = math_max(u8max, data[i].stems[stemIdx]);
//                     }
//                 }
//             }

//             const float max = static_cast<float>(u8max);

//             for (int layerIdx = 0; layerIdx < 2; ++layerIdx) {
//                 float color_a = color_a_base * (layerIdx ? m_opacity : m_outlineOpacity);

//                 float effectiveGain = 1.0f;
//                 if (stemIdx > 0 && premixMuted) {
//                     const bool isMuted = (m_pStemMute.size() > static_cast<size_t>(stemIdx))
//                             ? m_pStemMute[stemIdx]->toBool()
//                             : false;
//                     const float volume = (m_pStemGain.size() > static_cast<size_t>(stemIdx))
//                             ? static_cast<float>(m_pStemGain[stemIdx]->get())
//                             : 1.0f;
//                     const bool deselected = selectedStems && !(selectedStems & (1 << stemIdx));
//                     effectiveGain = (isMuted || deselected) ? 0.0f : volume;
//                 }

//                 const float h = heightFactor * (max * effectiveGain);
                
//                 vertexUpdater.addRectangle(
//                         {fVisualIdx - halfStripSize,
//                                 stemLayer * stemBreadth + halfBreadth - h},
//                         {fVisualIdx + halfStripSize,
//                                 stemLayer * stemBreadth + halfBreadth +
//                                         (m_isSlipRenderer ? 0.f : h)},
//                         {color_r, color_g, color_b, color_a});
//             }
//             ++stemLayer;
//         }
//         xVisualFrame += visualIncrementPerPixel;
//     }

//     const int actualVerticesWritten = vertexUpdater.index();
//     geometry().allocate(actualVerticesWritten);

//     markDirtyMaterial();
//     return true;
// }


} // namespace allshader


// #include "waveform/renderers/allshader/waveformrendererstem.h"

// #include <QFont>
// #include <QImage>
// #include <QOpenGLTexture>

// #include "control/controlproxy.h"
// #include "engine/channels/enginedeck.h"
// #include "engine/engine.h"
// #include "rendergraph/material/rgbamaterial.h"
// #include "rendergraph/vertexupdaters/rgbavertexupdater.h"
// #include "track/track.h"
// #include "util/assert.h"
// #include "util/math.h"
// #include "waveform/renderers/waveformwidgetrenderer.h"
// #include "waveform/waveform.h"
// #include "waveform/waveformwidgetfactory.h"

// namespace {
// #ifdef __SCENEGRAPH__
// // FIXME this is a workaround an issue with waveform only drawing partially in
// // SG. The workaround is to reduce the the number of vertices, by reducing the
// // precision of waveform strips.
// const float kPixelPerStrip = 2;
// #else
// const float kPixelPerStrip = 1;
// #endif
// } // namespace

// using namespace rendergraph;

// namespace allshader {

// WaveformRendererStem::WaveformRendererStem(
//         WaveformWidgetRenderer* waveformWidget,
//         ::WaveformRendererAbstract::PositionSource type,
//         ::WaveformRendererSignalBase::Options options)
//         : WaveformRendererSignalBase(waveformWidget, options),
//           m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
//           m_splitStemTracks(false),
//           m_outlineOpacity(0.15f),
//           m_opacity(0.75f) {
//     initForRectangles<RGBAMaterial>(0);
//     setUsePreprocess(true);
// }

// void WaveformRendererStem::onSetup(const QDomNode&) {
// }

// bool WaveformRendererStem::init() {
//     m_pStemGain.clear();
//     m_pStemMute.clear();
//     if (m_waveformRenderer->getGroup().isEmpty()) {
//         return true;
//     }
//     for (int stemIdx = 0; stemIdx < mixxx::kMaxSupportedStems; stemIdx++) {
//         QString stemGroup = EngineDeck::getGroupForStem(m_waveformRenderer->getGroup(), stemIdx);
//         m_pStemGain.emplace_back(
//                 std::make_unique<ControlProxy>(stemGroup,
//                         QStringLiteral("volume")));
//         m_pStemMute.emplace_back(
//                 std::make_unique<ControlProxy>(stemGroup,
//                         QStringLiteral("mute")));
//         auto bringToForeground = [this, stemIdx](double) {
//             if (!m_reorderOnChange) {
//                 return;
//             }
//             m_stackOrder.removeAll(stemIdx);
//             m_stackOrder.append(stemIdx);
//         };
//         m_pStemGain.back()->connectValueChanged(this, bringToForeground);
//         m_pStemMute.back()->connectValueChanged(this, bringToForeground);
//     }

//     m_stackOrder.resize(mixxx::kMaxSupportedStems);
//     std::iota(m_stackOrder.begin(), m_stackOrder.end(), 0);

// #ifndef __SCENEGRAPH__
//     auto* pWaveformWidgetFactory = WaveformWidgetFactory::instance();
//     setReorderOnChange(pWaveformWidgetFactory->isStemReorderOnChange());
//     connect(pWaveformWidgetFactory,
//             &WaveformWidgetFactory::stemReorderOnChangeChanged,
//             this,
//             &WaveformRendererStem::setReorderOnChange);
//     setOutlineOpacity(pWaveformWidgetFactory->getStemOutlineOpacity());
//     connect(pWaveformWidgetFactory,
//             &WaveformWidgetFactory::stemOutlineOpacityChanged,
//             this,
//             &WaveformRendererStem::setOutlineOpacity);
//     setOpacity(pWaveformWidgetFactory->getStemOpacity());
//     connect(pWaveformWidgetFactory,
//             &WaveformWidgetFactory::stemOpacityChanged,
//             this,
//             &WaveformRendererStem::setOpacity);
// #endif
//     return true;
// }

// void WaveformRendererStem::preprocess() {
//     if (!preprocessInner()) {
//         if (geometry().vertexCount() != 0) {
//             geometry().allocate(0);
//             markDirtyGeometry();
//         }
//     }
// }

// bool WaveformRendererStem::preprocessInner() {
//     TrackPointer pTrack = m_waveformRenderer->getTrackInfo();

//     if (!pTrack || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
//         return false;
//     }

//     auto stemInfo = pTrack->getStemInfo();
//     // If this track isn't a stem track, skip the rendering
//     if (stemInfo.isEmpty()) {
//         return false;
//     }
//     auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
//                                          : ::WaveformRendererAbstract::Play;

//     ConstWaveformPointer waveform = pTrack->getWaveform();
//     if (waveform.isNull()) {
//         return false;
//     }

//     const int dataSize = waveform->getDataSize();
//     if (dataSize <= 1) {
//         return false;
//     }

//     const WaveformData* data = waveform->data();
//     if (data == nullptr) {
//         return false;
//     }
//     // If this waveform doesn't contain stem data, skip the rendering
//     if (!waveform->hasStem()) {
//         return false;
//     }

//     uint selectedStems = m_waveformRenderer->getSelectedStems();

//     const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
//     const int length = static_cast<int>(m_waveformRenderer->getLength());
//     const int pixelLength = static_cast<int>(m_waveformRenderer->getLength() * devicePixelRatio);
//     const int stripLength = static_cast<int>(static_cast<float>(pixelLength) / kPixelPerStrip);
//     const float invDevicePixelRatio = kPixelPerStrip / devicePixelRatio;
//     const float halfStripSize = kPixelPerStrip / 2.0f / devicePixelRatio;

//     // See waveformrenderersimple.cpp for a detailed explanation of the frame and index calculation
//     const int visualFramesSize = dataSize / 2;
//     const double firstVisualFrame =
//             m_waveformRenderer->getFirstDisplayedPosition(positionType) * visualFramesSize;
//     const double lastVisualFrame =
//             m_waveformRenderer->getLastDisplayedPosition(positionType) * visualFramesSize;

//     // Represents the # of visual frames per horizontal pixel.
//     const double visualIncrementPerPixel =
//             (lastVisualFrame - firstVisualFrame) / static_cast<double>(stripLength);

//     // Per-band gain from the EQ knobs.
//     float allGain(1.0);
//     getGains(&allGain, nullptr, nullptr, nullptr);

//     const float breadth = static_cast<float>(m_waveformRenderer->getBreadth());
//     const float stemBreadth = m_splitStemTracks ? breadth / 4.0f : 0;
//     const float halfBreadth = (m_splitStemTracks ? stemBreadth : breadth) / 2.0f;

//     const float heightFactor = allGain * halfBreadth / m_maxValue;

//     // Effective visual frame for x
//     double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
//             visualIncrementPerPixel;

//     const int numVerticesPerLine = 6; // 2 triangles

//     const int reserved = numVerticesPerLine *
//             (mixxx::audio::ChannelCount::stem() * stripLength + 1);

//     geometry().setDrawingMode(Geometry::DrawingMode::Triangles);
//     geometry().allocate(reserved);
//     markDirtyGeometry();

//     RGBAVertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::RGBAColoredPoint2D>()};
//     vertexUpdater.addRectangle({0.f,
//                                        halfBreadth - 0.5f},
//             {static_cast<float>(length),
//                     m_isSlipRenderer ? halfBreadth : halfBreadth + 0.5f},
//             {0.f, 0.f, 0.f, 0.f});

//     const double maxSamplingRange = visualIncrementPerPixel / 2.0;

//     for (int visualIdx = 0; visualIdx < stripLength; visualIdx++) {
//         int stemLayer = 0;
//         for (int stemIdx : std::as_const(m_stackOrder)) {
//             // Stem is drawn twice with different opacity level, this allow to
//             // see the maximum signal by transparency
//             for (int layerIdx = 0; layerIdx < 2; layerIdx++) {
//                 QColor stemColor = stemInfo[stemIdx].getColor();
//                 float color_r = stemColor.redF(),
//                       color_g = stemColor.greenF(),
//                       color_b = stemColor.blueF(),
//                       color_a = stemColor.alphaF() * (layerIdx ? m_opacity : m_outlineOpacity);
//                 const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
//                 const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);

//                 const int visualIndexStart = std::max(visualFrameStart * 2, 0);
//                 const int visualIndexStop =
//                         std::min(std::max(visualFrameStop, visualFrameStart + 1) * 2, dataSize - 1);

//                 const float fVisualIdx = static_cast<float>(visualIdx) * invDevicePixelRatio;

//                 // Find the max values for current eq in the waveform data.
//                 // - Max of left and right
//                 uchar u8max{};
//                 for (int chn = 0; chn < 2; chn++) {
//                     // data is interleaved left / right
//                     for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
//                         const WaveformData& waveformData = data[i];

//                         u8max = math_max(u8max, waveformData.stems[stemIdx]);
//                     }
//                 }

//                 // Cast to float
//                 float max = static_cast<float>(u8max) * allGain;

//                 // Apply the gains
//                 if (layerIdx) {
//                     if (selectedStems) {
//                         max *= !(selectedStems & 1 << stemIdx)
//                                 ? 0.f
//                                 : 1.f;
//                     } else if (!m_pStemMute.empty() && m_pStemMute[stemIdx]->toBool()) {
//                         max = 0;
//                     } else {
//                         float volume = m_pStemGain.empty()
//                                 ? 1.f
//                                 : static_cast<float>(m_pStemGain[stemIdx]->get());
//                         max *= volume;
//                     }
//                 }

//                 // Lines are thin rectangles
//                 // shadow
//                 vertexUpdater.addRectangle(
//                         {fVisualIdx - halfStripSize,
//                                 stemLayer * stemBreadth + halfBreadth -
//                                         heightFactor * max},
//                         {fVisualIdx + halfStripSize,
//                                 m_isSlipRenderer
//                                         ? stemLayer * stemBreadth + halfBreadth
//                                         : stemLayer * stemBreadth + halfBreadth +
//                                                 heightFactor * max},
//                         {color_r, color_g, color_b, color_a});
//             }
//             stemLayer++;
//         }

//         xVisualFrame += visualIncrementPerPixel;
//     }

//     DEBUG_ASSERT(reserved == vertexUpdater.index());

//     markDirtyMaterial();

//     return true;
// }

// } // namespace allshader

// // #include "waveform/renderers/allshader/waveformrendererstem.h"
// //
// // #include <QFont>
// // #include <QImage>
// // #include <QOpenGLTexture>
// //
// // #include "control/controlproxy.h"
// // #include "engine/channels/enginedeck.h"
// // #include "engine/engine.h"
// // #include "rendergraph/material/rgbamaterial.h"
// // #include "rendergraph/vertexupdaters/rgbavertexupdater.h"
// // #include "track/track.h"
// // #include "util/assert.h"
// // #include "util/math.h"
// // #include "waveform/renderers/waveformwidgetrenderer.h"
// // #include "waveform/waveform.h"
// // #include "waveform/waveformwidgetfactory.h"
// //
// // namespace {
// // #ifdef __SCENEGRAPH__
// //// FIXME this is a workaround an issue with waveform only drawing partially in
// //// SG. The workaround is to reduce the the number of vertices, by reducing the
// //// precision of waveform strips.
// // const float kPixelPerStrip = 2;
// // #else
// // const float kPixelPerStrip = 1;
// // #endif
// // } // namespace
// //
// // using namespace rendergraph;
// //
// // namespace allshader {
// //
// // WaveformRendererStem::WaveformRendererStem(
// //         WaveformWidgetRenderer* waveformWidget,
// //         ::WaveformRendererAbstract::PositionSource type,
// //         ::WaveformRendererSignalBase::Options options)
// //         : WaveformRendererSignalBase(waveformWidget, options),
// //           m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip),
// //           m_splitStemTracks(false),
// //           m_outlineOpacity(0.15f),
// //           m_opacity(0.75f) {
// //     initForRectangles<RGBAMaterial>(0);
// //     setUsePreprocess(true);
// // }
// //
// // void WaveformRendererStem::onSetup(const QDomNode&) {
// // }
// //
// // bool WaveformRendererStem::init() {
// //     m_pStemGain.clear();
// //     m_pStemMute.clear();
// //     if (m_waveformRenderer->getGroup().isEmpty()) {
// //         return true;
// //     }
// //     for (int stemIdx = 0; stemIdx < mixxx::kMaxSupportedStems; stemIdx++) {
// //         QString stemGroup =
// //         EngineDeck::getGroupForStem(m_waveformRenderer->getGroup(), stemIdx);
// //         m_pStemGain.emplace_back(
// //                 std::make_unique<ControlProxy>(stemGroup,
// //                         QStringLiteral("volume")));
// //         m_pStemMute.emplace_back(
// //                 std::make_unique<ControlProxy>(stemGroup,
// //                         QStringLiteral("mute")));
// //         auto bringToForeground = [this, stemIdx](double) {
// //             if (!m_reorderOnChange) {
// //                 return;
// //             }
// //             m_stackOrder.removeAll(stemIdx);
// //             m_stackOrder.append(stemIdx);
// //         };
// //         m_pStemGain.back()->connectValueChanged(this, bringToForeground);
// //         m_pStemMute.back()->connectValueChanged(this, bringToForeground);
// //     }
// //
// //     m_stackOrder.resize(mixxx::kMaxSupportedStems);
// //     std::iota(m_stackOrder.begin(), m_stackOrder.end(), 0);
// //
// // #ifndef __SCENEGRAPH__
// //     auto* pWaveformWidgetFactory = WaveformWidgetFactory::instance();
// //     setReorderOnChange(pWaveformWidgetFactory->isStemReorderOnChange());
// //     connect(pWaveformWidgetFactory,
// //             &WaveformWidgetFactory::stemReorderOnChangeChanged,
// //             this,
// //             &WaveformRendererStem::setReorderOnChange);
// //     setOutlineOpacity(pWaveformWidgetFactory->getStemOutlineOpacity());
// //     connect(pWaveformWidgetFactory,
// //             &WaveformWidgetFactory::stemOutlineOpacityChanged,
// //             this,
// //             &WaveformRendererStem::setOutlineOpacity);
// //     setOpacity(pWaveformWidgetFactory->getStemOpacity());
// //     connect(pWaveformWidgetFactory,
// //             &WaveformWidgetFactory::stemOpacityChanged,
// //             this,
// //             &WaveformRendererStem::setOpacity);
// // #endif
// //     return true;
// // }
// //
// // void WaveformRendererStem::preprocess() {
// //     if (!preprocessInner()) {
// //         if (geometry().vertexCount() != 0) {
// //             geometry().allocate(0);
// //             markDirtyGeometry();
// //         }
// //     }
// // }
// //
// // bool WaveformRendererStem::preprocessInner() {
// //     TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
// //
// //     if (!pTrack || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive()))
// //     {
// //         return false;
// //     }
// //
// //     auto stemInfo = pTrack->getStemInfo();
// //     // If this track isn't a stem track, skip the rendering
// //     if (stemInfo.isEmpty()) {
// //         return false;
// //     }
// //     auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
// //                                          : ::WaveformRendererAbstract::Play;
// //
// //     ConstWaveformPointer waveform = pTrack->getWaveform();
// //     if (waveform.isNull()) {
// //         return false;
// //     }
// //
// //     const int dataSize = waveform->getDataSize();
// //     if (dataSize <= 1) {
// //         return false;
// //     }
// //
// //     const WaveformData* data = waveform->data();
// //     if (data == nullptr) {
// //         return false;
// //     }
// //     // If this waveform doesn't contain stem data, skip the rendering
// //     if (!waveform->hasStem()) {
// //         return false;
// //     }
// //
// //     uint selectedStems = m_waveformRenderer->getSelectedStems();
// //
// //     const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
// //     const int length = static_cast<int>(m_waveformRenderer->getLength());
// //     const int pixelLength = static_cast<int>(m_waveformRenderer->getLength()
// //     * devicePixelRatio); const int stripLength =
// //     static_cast<int>(static_cast<float>(pixelLength) / kPixelPerStrip); const
// //     float invDevicePixelRatio = kPixelPerStrip / devicePixelRatio; const
// //     float halfStripSize = kPixelPerStrip / 2.0f / devicePixelRatio;
// //
// //     // See waveformrenderersimple.cpp for a detailed explanation of the frame
// //     and index calculation const int visualFramesSize = dataSize / 2; const
// //     double firstVisualFrame =
// //             m_waveformRenderer->getFirstDisplayedPosition(positionType) *
// //             visualFramesSize;
// //     const double lastVisualFrame =
// //             m_waveformRenderer->getLastDisplayedPosition(positionType) *
// //             visualFramesSize;
// //
// //     // Represents the # of visual frames per horizontal pixel.
// //     const double visualIncrementPerPixel =
// //             (lastVisualFrame - firstVisualFrame) /
// //             static_cast<double>(stripLength);
// //
// //     // Per-band gain from the EQ knobs.
// //     float allGain(1.0);
// //     getGains(&allGain, nullptr, nullptr, nullptr);
// //
// //     const float breadth =
// //     static_cast<float>(m_waveformRenderer->getBreadth()); const float
// //     stemBreadth = m_splitStemTracks ? breadth / 4.0f : 0; const float
// //     halfBreadth = (m_splitStemTracks ? stemBreadth : breadth) / 2.0f;
// //
// //     const float heightFactor = allGain * halfBreadth / m_maxValue;
// //
// //     // Effective visual frame for x
// //     double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel)
// //     *
// //             visualIncrementPerPixel;
// //
// //     const int numVerticesPerLine = 6; // 2 triangles
// //
// //     const int reserved = numVerticesPerLine *
// //             (mixxx::audio::ChannelCount::stem() * stripLength + 1);
// //
// //     geometry().setDrawingMode(Geometry::DrawingMode::Triangles);
// //     geometry().allocate(reserved);
// //     markDirtyGeometry();
// //
// //     RGBAVertexUpdater
// //     vertexUpdater{geometry().vertexDataAs<Geometry::RGBAColoredPoint2D>()};
// //     vertexUpdater.addRectangle({0.f,
// //                                        halfBreadth - 0.5f},
// //             {static_cast<float>(length),
// //                     m_isSlipRenderer ? halfBreadth : halfBreadth + 0.5f},
// //             {0.f, 0.f, 0.f, 0.f});
// //
// //     const double maxSamplingRange = visualIncrementPerPixel / 2.0;
// //
// //     for (int visualIdx = 0; visualIdx < stripLength; visualIdx++) {
// //         int stemLayer = 0;
// //         for (int stemIdx : std::as_const(m_stackOrder)) {
// //             // Stem is drawn twice with different opacity level, this allow
// //             to
// //             // see the maximum signal by transparency
// //             for (int layerIdx = 0; layerIdx < 2; layerIdx++) {
// //                 QColor stemColor = stemInfo[stemIdx].getColor();
// //                 float color_r = stemColor.redF(),
// //                       color_g = stemColor.greenF(),
// //                       color_b = stemColor.blueF(),
// //                       color_a = stemColor.alphaF() * (layerIdx ? m_opacity :
// //                       m_outlineOpacity);
// //                 const int visualFrameStart = std::lround(xVisualFrame -
// //                 maxSamplingRange); const int visualFrameStop =
// //                 std::lround(xVisualFrame + maxSamplingRange);
// //
// //                 const int visualIndexStart = std::max(visualFrameStart * 2,
// //                 0); const int visualIndexStop =
// //                         std::min(std::max(visualFrameStop, visualFrameStart +
// //                         1) * 2, dataSize - 1);
// //
// //                 const float fVisualIdx = static_cast<float>(visualIdx) *
// //                 invDevicePixelRatio;
// //
// //                 // Find the max values for current eq in the waveform data.
// //                 // - Max of left and right
// //                 uchar u8max{};
// //                 for (int chn = 0; chn < 2; chn++) {
// //                     // data is interleaved left / right
// //                     for (int i = visualIndexStart + chn; i < visualIndexStop
// //                     + chn; i += 2) {
// //                         const WaveformData& waveformData = data[i];
// //
// //                         u8max = math_max(u8max, waveformData.stems[stemIdx]);
// //                     }
// //                 }
// //
// //                 // Cast to float
// //                 float max = static_cast<float>(u8max) * allGain;
// //
// //                 // Apply the gains
// //                 if (layerIdx) {
// //                     if (selectedStems) {
// //                         max *= !(selectedStems & 1 << stemIdx)
// //                                 ? 0.f
// //                                 : 1.f;
// //                     } else if (!m_pStemMute.empty() &&
// //                     m_pStemMute[stemIdx]->toBool()) {
// //                         max = 0;
// //                     } else {
// //                         float volume = m_pStemGain.empty()
// //                                 ? 1.f
// //                                 :
// //                                 static_cast<float>(m_pStemGain[stemIdx]->get());
// //                         max *= volume;
// //                     }
// //                 }
// //
// //                 // Lines are thin rectangles
// //                 // shadow
// //                 vertexUpdater.addRectangle(
// //                         {fVisualIdx - halfStripSize,
// //                                 stemLayer * stemBreadth + halfBreadth -
// //                                         heightFactor * max},
// //                         {fVisualIdx + halfStripSize,
// //                                 m_isSlipRenderer
// //                                         ? stemLayer * stemBreadth +
// //                                         halfBreadth : stemLayer * stemBreadth
// //                                         + halfBreadth +
// //                                                 heightFactor * max},
// //                         {color_r, color_g, color_b, color_a});
// //             }
// //             stemLayer++;
// //         }
// //
// //         xVisualFrame += visualIncrementPerPixel;
// //     }
// //
// //     DEBUG_ASSERT(reserved == vertexUpdater.index());
// //
// //     markDirtyMaterial();
// //
// //     return true;
// // }
// //
// // } // namespace allshader
