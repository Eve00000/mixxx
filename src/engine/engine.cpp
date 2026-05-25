
#include "engine/engine.h"

#include "control/controlproxy.h"
#include "control/pollingcontrolproxy.h"

namespace mixxx {

bool includeOriginalMasterStems() {
    PollingControlProxy proxy(
            "IncludeOriginalMasterWhenPlayingStems",
            "Enabled");

    return proxy.toBool();
}

// StemMask getActiveStemMask() {
//     return includeOriginalMasterStems()
//             ? kStemMaskAll5
//             : kStemMaskAll4;
// }

// StemChannelSelection getActiveStemSelection() {
//     StemChannelSelection sel;
//
//     sel |= StemChannel::First;
//     sel |= StemChannel::Second;
//     sel |= StemChannel::Third;
//     sel |= StemChannel::Fourth;
//
//     if (includeOriginalMasterStems()) {
//         sel |= StemChannel::PreMix;
//     }
//
//     return sel;
// }

StemChannelSelection getActiveStemMask() {
    StemChannelSelection sel;

    sel |= StemChannel::First;
    sel |= StemChannel::Second;
    sel |= StemChannel::Third;
    sel |= StemChannel::Fourth;

    if (includeOriginalMasterStems()) {
        sel |= StemChannel::PreMix;
    }

    return sel;
}

StemChannelSelection getActiveStemSelection() {
    return getActiveStemMask();
}

} // namespace mixxx
