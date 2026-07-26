#include "waveform/renderers/allshader/waveformrendererstem.h"

#include <QFont>
#include <QImage>
#include <QOpenGLTexture>

#include "control/controlproxy.h"
#include "control/pollingcontrolproxy.h"
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
          m_showOriginalPremix(false),
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
    const QString kWaveformGroup(QStringLiteral("[Waveform]"));

    m_pStemControlsExpanded = std::make_unique<ControlProxy>(
            m_waveformRenderer->getGroup(),
            QStringLiteral("stem_controls_expanded"));
    m_pUseStemSplitTracks = std::make_unique<ControlProxy>(
            kWaveformGroup,
            QStringLiteral("stem_split_tracks"));

    if (m_waveformRenderer->getGroup().isEmpty()) {
        return true;
    }

    for (int stemIdx = 0; stemIdx < mixxx::kMaxSupportedStems; stemIdx++) {
        QString stemGroup = EngineDeck::getGroupForStem(m_waveformRenderer->getGroup(), stemIdx);
        if (stemIdx == 0) {
            m_pStemGain.emplace_back(nullptr);
        } else {
            m_pStemGain.emplace_back(
                    std::make_unique<ControlProxy>(stemGroup, QStringLiteral("volume")));
        }

        m_pStemMute.emplace_back(
                std::make_unique<ControlProxy>(stemGroup,
                        QStringLiteral("mute")));

        // Reordering works in COMBINED mode (when split is false)
        auto bringToForeground = [this, stemIdx](double) {
            // Check if we're currently in split mode (combined split state)
            bool expanded = m_pStemControlsExpanded ? m_pStemControlsExpanded->get() > 0.5 : false;
            bool useSplit = m_pUseStemSplitTracks ? m_pUseStemSplitTracks->get() > 0.5 : false;
            bool currentlySplit = expanded && useSplit;

            if (!m_reorderOnChange || currentlySplit) {
                return;
            }
            // Remove the stem from its current position and move to end (will
            // be drawn on top/front)
            m_stackOrder.removeAll(stemIdx);
            m_stackOrder.append(stemIdx);
            markDirtyGeometry();
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

    updateSplitState();

    m_pStemControlsExpanded->connectValueChanged(
            this, &WaveformRendererStem::onStemControlsExpandedChanged);
    m_pUseStemSplitTracks->connectValueChanged(
            this, &WaveformRendererStem::onUseStemSplitTracksChanged);

    return true;
}

void WaveformRendererStem::onStemControlsExpandedChanged(double value) {
    bool expanded = value > 0.5;
    bool useSplit = m_pUseStemSplitTracks ? m_pUseStemSplitTracks->get() > 0.5 : false;
    bool shouldSplit = expanded && useSplit;

    if (m_splitStemTracks != shouldSplit) {
        m_splitStemTracks = shouldSplit;
        markDirtyGeometry();
    }
}

void WaveformRendererStem::onUseStemSplitTracksChanged(double value) {
    bool useSplit = value > 0.5;
    bool expanded = m_pStemControlsExpanded ? m_pStemControlsExpanded->get() > 0.5 : false;
    bool shouldSplit = expanded && useSplit;

    if (m_splitStemTracks != shouldSplit) {
        m_splitStemTracks = shouldSplit;
        markDirtyGeometry();
    }
}

void WaveformRendererStem::updateSplitState() {
    if (m_pStemControlsExpanded && m_pUseStemSplitTracks) {
        bool expanded = m_pStemControlsExpanded->get() > 0.5;
        bool useSplit = m_pUseStemSplitTracks->get() > 0.5;
        bool shouldSplit = expanded && useSplit;

        if (m_splitStemTracks != shouldSplit) {
            m_splitStemTracks = shouldSplit;
            markDirtyGeometry();
        }
    }
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

    float allGain = 1.0f;
    getGains(&allGain, nullptr, nullptr, nullptr);

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth());

    // Determine if we need to show premix based on skin preference
    PollingControlProxy proxyShowOriginalPremix(QStringLiteral("[Skin]"),
            QStringLiteral("show_original_premix"),
            ControlFlag::AllowMissingOrInvalid);

    m_showOriginalPremix = proxyShowOriginalPremix.get() > 0 ? true : false;

    const bool premixMuted =
            (!m_pStemMute.empty() && m_pStemMute.size() > 0) ? m_pStemMute[0]->toBool() : false;

    // Show premix only if the skin preference is enabled AND premix is NOT muted
    const bool showPremix = m_showOriginalPremix && !premixMuted;

    // Check if all 4 stems are muted
    bool allStemsMuted = true;
    for (int stemIdx = 1; stemIdx < mixxx::kMaxSupportedStems; ++stemIdx) {
        const bool isMuted = (m_pStemMute.size() > static_cast<size_t>(stemIdx))
                ? m_pStemMute[stemIdx]->toBool()
                : true; // If control is missing, treat as muted
        if (!isMuted) {
            allStemsMuted = false;
            break;
        }
    }

    // Determine if we should use split mode
    // OVERRULE: If premix is unmuted AND all stems are muted -> force combined mode
    bool useSplitMode = false;
    if (showPremix && allStemsMuted) {
        // Force combined mode to show just the premix
        useSplitMode = false;
    } else {
        // Normal logic: use the combined split state from preferences
        bool expanded = m_pStemControlsExpanded ? m_pStemControlsExpanded->get() > 0.5 : false;
        bool useSplit = m_pUseStemSplitTracks ? m_pUseStemSplitTracks->get() > 0.5 : false;
        useSplitMode = expanded && useSplit;
    }

    // Update m_splitStemTracks if it changed
    if (m_splitStemTracks != useSplitMode) {
        m_splitStemTracks = useSplitMode;
        markDirtyGeometry();
    }

    // Calculate stem breadth based on what we're showing
    const int numStripsToShow = m_splitStemTracks ? (showPremix ? 5 : 4) : 1;

    const float stemBreadth = m_splitStemTracks
            ? breadth / static_cast<float>(numStripsToShow)
            : breadth;
    const float halfBreadth = stemBreadth / 2.0f;

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

    for (int visualIdx = 0; visualIdx < stripLength; ++visualIdx) {
        int stemLayer = 0;

        // Draw premix first if show_premix is enabled
        if (showPremix) {
            const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
            const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);
            const int visualIndexStart = std::max(visualFrameStart * 2, 0);
            const int visualIndexStop = std::min(
                    std::max(visualFrameStop, visualFrameStart + 1) * 2,
                    dataSize - 1);

            // Determine vertical position for premix
            float yCenter = m_splitStemTracks ? stemLayer * stemBreadth + halfBreadth : halfBreadth;

            // Draw stems layered from bottom to top (stem 1 at bottom, stem 4 on top)
            for (int stemIdx = 1; stemIdx < mixxx::kMaxSupportedStems; ++stemIdx) {
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

                // Find the max values for current stem
                uchar u8max = 0;
                for (int chn = 0; chn < 2; ++chn) {
                    for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
                        u8max = math_max(u8max, data[i].stems[stemIdx]);
                    }
                }

                const float max = static_cast<float>(u8max);
                const float h = heightFactor * max;
                const float fVisualIdx = static_cast<float>(visualIdx) * invDevicePixelRatio;

                // Draw this stem's contribution to the premix
                for (int layerIdx = 0; layerIdx < 2; ++layerIdx) {
                    float color_a = color_a_base * (layerIdx ? m_opacity : m_outlineOpacity);

                    vertexUpdater.addRectangle(
                            {fVisualIdx - halfStripSize,
                                    yCenter - h},
                            {fVisualIdx + halfStripSize,
                                    yCenter + h},
                            {color_r, color_g, color_b, color_a});
                }
            }

            if (m_splitStemTracks) {
                stemLayer++;
            }
        }

        if (!m_splitStemTracks) {
            // COMBINED/OVERLAPPING MODE: Draw stems in stacking order (reorderable)
            // The last stem in m_stackOrder will be drawn on top (front)
            for (int stemIdx : std::as_const(m_stackOrder)) {
                // Skip premix
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

                // Find the max values for current stem in the waveform byte data.
                uchar u8max = 0;
                for (int chn = 0; chn < 2; ++chn) {
                    for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
                        u8max = math_max(u8max, data[i].stems[stemIdx]);
                    }
                }

                const float max = static_cast<float>(u8max);

                // Get volume and mute state for this stem
                const bool isMuted = (m_pStemMute.size() > static_cast<size_t>(stemIdx))
                        ? m_pStemMute[stemIdx]->toBool()
                        : false;
                const float volume = (m_pStemGain.size() > static_cast<size_t>(stemIdx))
                        ? static_cast<float>(m_pStemGain[stemIdx]->get())
                        : 1.0f;
                const bool deselected = selectedStems && !(selectedStems & (1 << stemIdx));

                float effectiveGain = (isMuted || deselected) ? 0.0f : volume;
                const float h = heightFactor * (max * effectiveGain);

                // Draw this stem centered in the full breadth
                // Each stem is drawn in the same position, but layered based on m_stackOrder
                for (int layerIdx = 0; layerIdx < 2; ++layerIdx) {
                    float color_a = color_a_base * (layerIdx ? m_opacity : m_outlineOpacity);

                    vertexUpdater.addRectangle(
                            {fVisualIdx - halfStripSize,
                                    halfBreadth - h},
                            {fVisualIdx + halfStripSize,
                                    halfBreadth + (m_isSlipRenderer ? 0.f : h)},
                            {color_r, color_g, color_b, color_a});
                }
            }
        } else {
            // SPLIT MODE: Draw each stem in its own layer (fixed order 1,2,3,4)
            for (int stemIdx = 1; stemIdx < mixxx::kMaxSupportedStems; ++stemIdx) {
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

                // Get volume and mute state for this stem
                const bool isMuted = (m_pStemMute.size() > static_cast<size_t>(stemIdx))
                        ? m_pStemMute[stemIdx]->toBool()
                        : false;
                const float volume = (m_pStemGain.size() > static_cast<size_t>(stemIdx))
                        ? static_cast<float>(m_pStemGain[stemIdx]->get())
                        : 1.0f;
                const bool deselected = selectedStems && !(selectedStems & (1 << stemIdx));

                float effectiveGain = (isMuted || deselected) ? 0.0f : volume;
                const float h = heightFactor * (max * effectiveGain);

                // Two layers (outline + fill) for stems
                for (int layerIdx = 0; layerIdx < 2; ++layerIdx) {
                    float color_a = color_a_base * (layerIdx ? m_opacity : m_outlineOpacity);

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
        }
        xVisualFrame += visualIncrementPerPixel;
    }

    // Explicitly resize geometry allocation block to vertices actually written
    const int actualVerticesWritten = vertexUpdater.index();
    geometry().allocate(actualVerticesWritten);

    markDirtyMaterial();
    return true;
}

} // namespace allshader
