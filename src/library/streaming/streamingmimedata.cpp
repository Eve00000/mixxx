#include "library/streaming/streamingmimedata.h"

#include <QDataStream>
#include <QHash>
#include <QIODevice>
#include <QMimeData>

namespace mixxx {
namespace streaming {

namespace {

QHash<QString, DropHandler> s_dropHandlers;

} // anonymous namespace

const char* tracksMimeType() {
    return "application/x-mixxx-streaming-tracks";
}

QMimeData* encodeTracks(const TrackList& tracks) {
    if (tracks.isEmpty()) {
        return nullptr;
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint32>(tracks.size());
    for (const Track& track : tracks) {
        stream << track.providerId << track.id << track.title << track.artist
               << track.album << track.isrc << track.year << track.audioQuality
               << track.durationSec << track.trackNumber << track.isExplicit;
    }
    auto* mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(tracksMimeType()), payload);
    return mimeData;
}

bool canDecodeTracks(const QMimeData* pMimeData) {
    return pMimeData && pMimeData->hasFormat(QString::fromLatin1(tracksMimeType()));
}

TrackList decodeTracks(const QMimeData* pMimeData) {
    TrackList tracks;
    if (!canDecodeTracks(pMimeData)) {
        return tracks;
    }
    QByteArray payload = pMimeData->data(QString::fromLatin1(tracksMimeType()));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 count = 0;
    stream >> count;
    for (quint32 i = 0; i < count && !stream.atEnd(); ++i) {
        Track track;
        stream >> track.providerId >> track.id >> track.title >> track.artist
                >> track.album >> track.isrc >> track.year >> track.audioQuality
                >> track.durationSec >> track.trackNumber >> track.isExplicit;
        tracks.append(track);
    }
    return tracks;
}

void registerDropHandler(const QString& providerId, DropHandler handler) {
    s_dropHandlers.insert(providerId, std::move(handler));
}

void unregisterDropHandler(const QString& providerId) {
    s_dropHandlers.remove(providerId);
}

DropHandler dropHandler(const QString& providerId) {
    return s_dropHandlers.value(providerId);
}

bool hasDropHandler(const QString& providerId) {
    return s_dropHandlers.contains(providerId);
}

} // namespace streaming
} // namespace mixxx
