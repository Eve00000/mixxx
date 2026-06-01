#include "engine/bufferscalers/rubberbandworkerpool.h"

#include <rubberband/RubberBandStretcher.h>

#include "engine/engine.h"
#include "util/assert.h"


RubberBandWorkerPool::RubberBandWorkerPool(UserSettingsPointer pConfig)
        : QThreadPool() {
    bool multiThreadedOnStereo = pConfig &&
            pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
                                      QStringLiteral("keylock_multithreading")),
                    false);
    m_channelPerWorker = multiThreadedOnStereo
            ? mixxx::audio::ChannelCount::mono()
            : mixxx::audio::ChannelCount::stereo();

    // === FIX: Expand the internal capacity limit to support 5 stereo pairs (10 channels) ===
    // Originally: mixxx::kMaxEngineChannelInputCount (2)
    // Updated to 10 to ensure the QList containers allocate enough slots for all stems.
    const int maxSupportedCustomChannels = 10;
    DEBUG_ASSERT(maxSupportedCustomChannels % m_channelPerWorker == 0);

    int numCore = QThread::idealThreadCount();
    int numRBTasks = qMin(numCore, maxSupportedCustomChannels / m_channelPerWorker);

    qDebug() << "RubberBand will use" << numRBTasks << "tasks to scale the audio signal";

    setThreadPriority(QThread::HighPriority);
    
    // Allow the engine thread to handle 1 task, and distribute the remaining tasks to background workers
    setMaxThreadCount(numRBTasks - 1);

    for (int w = 0; w < maxThreadCount(); w++) {
        reserveThread();
    }
}


// RubberBandWorkerPool::RubberBandWorkerPool(UserSettingsPointer pConfig)
//         : QThreadPool() {
//     bool multiThreadedOnStereo = pConfig &&
//             pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
//                                       QStringLiteral("keylock_multithreading")),
//                     false);
    
//     // === FIX: Enforce uniform stereo tracking to prevent multi-worker collisions ===
//     m_channelPerWorker = mixxx::audio::ChannelCount::stereo();

//     // Base core count detection
//     int numCore = QThread::idealThreadCount();
    
//     // Size tasks safely based on a standard 8-core / 4-task optimization metric
//     int numRBTasks = qMin(numCore, 4);
//     if (numRBTasks < 2) {
//         numRBTasks = 2; // Guarantee at least a dual processing split
//     }

//     qDebug() << "[RubberBandWorkerPool] Linux Multi-Engine Optimization -" 
//              << "Cores:" << numCore 
//              << "Parallel Tasks Allocated:" << numRBTasks;

//     setThreadPriority(QThread::HighPriority);
    
//     // Set thread limits ensuring safe parallel backgrounds
//     setMaxThreadCount(numRBTasks - 1);

//     for (int w = 0; w < maxThreadCount(); w++) {
//         reserveThread();
//     }
// }

// RubberBadWorkerPool::RubberBandWorkerPool(UserSettingsPointer pConfig)
//         : QThreadPool() {
//     bool multiThreadedOnStereo = pConfig &&
//             pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
//                                       QStringLiteral("keylock_multithreading")),
//                     false);
//     m_channelPerWorker = multiThreadedOnStereo
//             ? mixxx::audio::ChannelCount::mono()
//             : mixxx::audio::ChannelCount::stereo();

//     // === FIX: Scale internal task limit to support your 10-channel layout (8 stems + 2 premix) ===
//     const int maxSupportedChannels = 10; 
//     DEBUG_ASSERT(maxSupportedChannels % static_cast<int>(m_channelPerWorker) == 0);

//     int numCore = QThread::idealThreadCount();
//     int numRBTasks = qMin(numCore, maxSupportedChannels / static_cast<int>(m_channelPerWorker));

//     qDebug() << "[RubberBandWorkerPool] Custom 10-ch pool sizing - Cores:" << numCore 
//              << "Allocated tasks:" << numRBTasks;

//     setThreadPriority(QThread::HighPriority);
    
//     // Ensure we have at least 1 background worker thread if multi-coring
//     int threadCountTarget = numRBTasks - 1;
//     if (threadCountTarget < 1 && numCore > 1) {
//         threadCountTarget = 1;
//     }
//     setMaxThreadCount(threadCountTarget);

//     for (int w = 0; w < maxThreadCount(); w++) {
//         reserveThread();
//     }
// }


// #include "engine/bufferscalers/rubberbandworkerpool.h"

// #include <rubberband/RubberBandStretcher.h>

// #include "engine/engine.h"
// #include "util/assert.h"

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

//     int numCore = QThread::idealThreadCount();
//     int numRBTasks = qMin(numCore, mixxx::kMaxEngineChannelInputCount / m_channelPerWorker);

//     qDebug() << "RubberBand will use" << numRBTasks << "tasks to scale the audio signal";

//     setThreadPriority(QThread::HighPriority);
//     // The RB pool will only be used to scale n-1 buffer sample, so the engine
//     // thread takes care of the last buffer and doesn't have to be idle.
//     setMaxThreadCount(numRBTasks - 1);

//     // We allocate one runner less than the total of maximum supported channel,
//     // so the engine thread will also perform a stretching operation, instead of
//     // waiting all workers to complete. During performance testing, this ahas
//     // show better results
//     for (int w = 0; w < maxThreadCount(); w++) {
//         reserveThread();
//     }
// }
