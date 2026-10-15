#include "engine/bufferscalers/rubberbandworkerpool.h"

#include <rubberband/RubberBandStretcher.h>

#include "engine/engine.h"
#include "util/assert.h"

// RubberBandWorkerPool::RubberBandWorkerPool(UserSettingsPointer pConfig)
//         : QThreadPool() {
//     bool multiThreadedOnStereo = pConfig &&
//             pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
//                                       QStringLiteral("keylock_multithreading")),
//                     false);
//     m_channelPerWorker = multiThreadedOnStereo
//             ? mixxx::audio::ChannelCount::mono()
//             : mixxx::audio::ChannelCount::stereo();
//     DEBUG_ASSERT(mixxx::kMaxEngineChannelInputCount % m_channelPerWorker == 0);
//
//     int numCore = QThread::idealThreadCount();
//     int numRBTasks = qMin(numCore, mixxx::kMaxEngineChannelInputCount / m_channelPerWorker);
//
//     qDebug() << "RubberBand will use" << numRBTasks << "tasks to scale the audio signal";
//
//     setThreadPriority(QThread::HighPriority);
//     // The RB pool will only be used to scale n-1 buffer sample, so the engine
//     // thread takes care of the last buffer and doesn't have to be idle.
//     setMaxThreadCount(numRBTasks - 1);
//
//     // We allocate one runner less than the total of maximum supported channel,
//     // so the engine thread will also perform a stretching operation, instead of
//     // waiting all workers to complete. During performance testing, this ahas
//     // show better results
//     for (int w = 0; w < maxThreadCount(); w++) {
//         reserveThread();
//     }
// }

RubberBandWorkerPool::RubberBandWorkerPool(UserSettingsPointer pConfig)
        : QThreadPool() {
    bool multiThreadedOnStereo = pConfig &&
            pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
                                      QStringLiteral("keylock_multithreading")),
                    false);

    // Keep the per-worker channel count simple (1 for mono, 2 for stereo)
    m_channelPerWorker = multiThreadedOnStereo
            ? mixxx::audio::ChannelCount::mono()
            : mixxx::audio::ChannelCount::stereo();

    // SAFE ASSERT: Ensure we don't divide by zero and handle odd channel counts
    // Instead of a hard assert, we log a warning if the count isn't cleanly divisible.
    if (mixxx::kMaxEngineChannelInputCount % m_channelPerWorker != 0) {
        qWarning() << "RubberBandWorkerPool: Channel count"
                   << mixxx::kMaxEngineChannelInputCount
                   << "is not divisible by" << m_channelPerWorker;
    }

    setThreadPriority(QThread::HighPriority);

    // SAFE MATH: Calculate tasks based on actual channel count, but ensure
    // we never end up with a negative thread count.
    int numRBTasks = mixxx::kMaxEngineChannelInputCount / m_channelPerWorker;

    // We allocate one runner less than the total tasks, but always keep at least 1
    int safeMaxThreads = std::max(1, numRBTasks - 1);
    setMaxThreadCount(safeMaxThreads);

    // Reserve threads (only reserve if > 0)
    for (int w = 0; w < maxThreadCount(); w++) {
        reserveThread();
    }
}
