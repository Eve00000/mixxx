#pragma once

#include "analyzer/analyzertrack.h"
#include "audio/signalinfo.h"
#include "audio/types.h"
#include "util/assert.h"
#include "util/types.h"

/*
 * An Analyzer is an object which wants to process an entire song to
 * calculate some kind of metadata about it. This could be bpm, the
 * summary, key or something else crazy. This is to help consolidate the
 * many different threads currently processing the whole track in Mixxx on load.
 *   -- Adam
 */

#include "track/track_decl.h"

class Analyzer {
  public:
    virtual ~Analyzer() = default;

    // This method is supposed to:
    //  1. Check if the track needs to be analyzed, otherwise return false.
    //  2. Perform the initialization and return true on success.
    //  3. If the initialization failed log the internal error and return false.
    virtual bool initialize(const AnalyzerTrack& track,
            mixxx::audio::SampleRate sampleRate,
            mixxx::audio::ChannelCount channelCount,
            SINT frameLength) = 0;

    // Analyze the next chunk of audio samples and return true if successful.
    // If processing fails the analysis can be aborted early by returning
    // false. After aborting the analysis only cleanup() will be invoked,
    // but not finalize()!
    virtual bool processSamples(const CSAMPLE* pIn, SINT count) = 0;

    // Update the track object with the analysis results after
    // processing finished successfully, i.e. all available audio
    // samples have been processed.
    virtual void storeResults(TrackPointer pTrack) = 0;

    // Discard any temporary results or free allocated memory.
    // This function will be invoked after the results have been
    // stored or if processing aborted preliminary.
    virtual void cleanup() = 0;
};

typedef std::unique_ptr<Analyzer> AnalyzerPtr;

class AnalyzerWithState final {
  public:
    explicit AnalyzerWithState(AnalyzerPtr analyzer)
            : m_analyzer(std::move(analyzer)),
              m_active(false) {
        DEBUG_ASSERT(m_analyzer);
    }
    AnalyzerWithState(const AnalyzerWithState&) = delete;
    AnalyzerWithState(AnalyzerWithState&&) = default;
    ~AnalyzerWithState() {
        VERIFY_OR_DEBUG_ASSERT(!m_active) {
            m_analyzer->cleanup();
        }
    }

    bool isActive() const {
        return m_active;
    }

    bool initialize(const AnalyzerTrack& track,
            mixxx::audio::SampleRate sampleRate,
            mixxx::audio::ChannelCount channelCount,
            SINT frameLength) {
        DEBUG_ASSERT(!m_active);
        return m_active = m_analyzer->initialize(track, sampleRate, channelCount, frameLength);
    }

    void processSamples(const CSAMPLE* pIn, const int count) {
        if (m_active) {
            m_active = m_analyzer->processSamples(pIn, count);
            if (!m_active) {
                // Ensure that cleanup() is invoked after processing
                // failed and the analyzer became inactive!
                m_analyzer->cleanup();
            }
        }
    }

    void finish(const AnalyzerTrack& track) {
        if (m_active) {
            m_analyzer->storeResults(track.getTrack());
            m_analyzer->cleanup();
            m_active = false;
        }
    }

    void cancel() {
        if (m_active) {
            m_analyzer->cleanup();
            m_active = false;
        }
    }

  private:
    AnalyzerPtr m_analyzer;
    bool m_active;
};
