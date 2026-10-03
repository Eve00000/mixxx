#include "library/tidal/tidalmimedata.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

namespace mixxx {
namespace tidal {

namespace {

TidalDropCallback s_dropCallback;

} // anonymous namespace

const char* tidalTracksMimeType() {
    return "application/x-mixxx-tidal-tracks";
}

QMimeData* encodeTracks(const QList<TidalTrack>& tracks) {
    if (tracks.isEmpty()) {
        return nullptr;
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint32>(tracks.size());
    for (const TidalTrack& track : tracks) {
        stream << track.id << track.title << track.artist << track.album
               << track.isrc << track.year << track.audioQuality
               << track.durationSec << track.trackNumber << track.isExplicit;
    }
    auto* mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(tidalTracksMimeType()), payload);
    return mimeData;
}

bool canDecodeTracks(const QMimeData* pMimeData) {
    return pMimeData && pMimeData->hasFormat(QString::fromLatin1(tidalTracksMimeType()));
}

QList<TidalTrack> decodeTracks(const QMimeData* pMimeData) {
    QList<TidalTrack> tracks;
    if (!canDecodeTracks(pMimeData)) {
        return tracks;
    }
    QByteArray payload =
            pMimeData->data(QString::fromLatin1(tidalTracksMimeType()));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 count = 0;
    stream >> count;
    for (quint32 i = 0; i < count && !stream.atEnd(); ++i) {
        TidalTrack track;
        stream >> track.id >> track.title >> track.artist >> track.album
                >> track.isrc >> track.year >> track.audioQuality
                >> track.durationSec >> track.trackNumber >> track.isExplicit;
        tracks.append(track);
    }
    return tracks;
}

void setTidalDropCallback(TidalDropCallback callback) {
    s_dropCallback = std::move(callback);
}

TidalDropCallback tidalDropCallback() {
    return s_dropCallback;
}

} // namespace tidal
} // namespace mixxx
