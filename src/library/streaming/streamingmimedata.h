#pragma once

#include <QList>
#include <QString>
#include <functional>

#include "library/streaming/streamingprovider.h"

class QMimeData;

namespace mixxx {
namespace streaming {

/// Single MIME type used to drag tracks from any streaming provider's search
/// results onto a deck. Each track carries its own providerId, so one MIME
/// type can serve all providers.
const char* tracksMimeType();

QMimeData* encodeTracks(const TrackList& tracks);
bool canDecodeTracks(const QMimeData* pMimeData);
TrackList decodeTracks(const QMimeData* pMimeData);

/// Callback invoked when streaming tracks are dropped onto a deck.
using DropHandler =
        std::function<void(const TrackList& tracks, const QString& group)>;

/// Register the drop handler for a provider id. Each StreamingFeature
/// registers itself here; the drag & drop helper looks the handler up by the
/// provider id encoded in the MIME payload.
void registerDropHandler(const QString& providerId, DropHandler handler);
void unregisterDropHandler(const QString& providerId);
DropHandler dropHandler(const QString& providerId);

/// Returns true if any provider has registered a drop handler, i.e. the MIME
/// data can be acted upon.
bool hasDropHandler(const QString& providerId);

} // namespace streaming
} // namespace mixxx

Q_DECLARE_METATYPE(mixxx::streaming::Track)
