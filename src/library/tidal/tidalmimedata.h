#pragma once

#include <QList>
#include <QString>
#include <functional>

#include "library/tidal/tidalclient.h"

class QMimeData;

namespace mixxx {
namespace tidal {

/// Custom MIME type used to drag TIDAL tracks from the search results onto a
/// deck. Since the tracks are not present on the local file system yet, the
/// regular file-URL based drag & drop handling cannot be used.
const char* tidalTracksMimeType();

/// Serialize/deserialize a list of TIDAL tracks for drag & drop.
QMimeData* encodeTracks(const QList<TidalTrack>& tracks);
bool canDecodeTracks(const QMimeData* pMimeData);
QList<TidalTrack> decodeTracks(const QMimeData* pMimeData);

/// Callback invoked when TIDAL tracks are dropped onto a deck.
using TidalDropCallback =
        std::function<void(const QList<TidalTrack>& tracks, const QString& group)>;

/// Register/clear the global drop callback. Set by TidalFeature while the
/// feature is alive.
void setTidalDropCallback(TidalDropCallback callback);
TidalDropCallback tidalDropCallback();

} // namespace tidal
} // namespace mixxx

Q_DECLARE_METATYPE(mixxx::tidal::TidalTrack)
