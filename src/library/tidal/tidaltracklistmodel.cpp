#include "library/tidal/tidaltracklistmodel.h"

#include <QSqlDatabase>
#include <QTime>
#include <QUuid>

#include "library/tidal/tidalmimedata.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_tidaltracklistmodel.cpp"
#include "track/track.h"

using mixxx::tidal::TidalTrack;

namespace {

QSqlDatabase cloneDatabase(TrackCollectionManager* pTrackCollectionManager) {
    if (!pTrackCollectionManager || !pTrackCollectionManager->internalCollection()) {
        return {};
    }
    const auto connectionName =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto cloned = QSqlDatabase::cloneDatabase(
            pTrackCollectionManager->internalCollection()->database(),
            connectionName);
    if (!cloned.isOpen()) {
        cloned.open();
    }
    return cloned;
}

QString formatDuration(int seconds) {
    if (seconds <= 0) {
        return QString();
    }
    return QTime(0, 0).addSecs(seconds).toString(
            seconds >= 3600 ? QStringLiteral("hh:mm:ss") : QStringLiteral("mm:ss"));
}

QString shortQuality(const QString& audioQuality) {
    if (audioQuality.contains(QLatin1String("HI_RES"), Qt::CaseInsensitive)) {
        return QStringLiteral("HI-RES");
    }
    if (audioQuality.contains(QLatin1String("LOSSLESS"), Qt::CaseInsensitive)) {
        return QStringLiteral("FLAC");
    }
    if (audioQuality.contains(QLatin1String("HIGH"), Qt::CaseInsensitive)) {
        return QStringLiteral("AAC 320");
    }
    if (audioQuality.contains(QLatin1String("LOW"), Qt::CaseInsensitive)) {
        return QStringLiteral("AAC 96");
    }
    return audioQuality;
}

} // anonymous namespace

TidalTrackListModel::TidalTrackListModel(
        QObject* parent,
        TrackCollectionManager* pTrackCollectionManager)
        // The track model settings are stored in a settings namespace.
        : QAbstractTableModel(parent),
          TrackModel(cloneDatabase(pTrackCollectionManager), "mixxx.db.model.tidal"),
          m_pTrackCollectionManager(pTrackCollectionManager) {
}

void TidalTrackListModel::setTracks(const QList<TidalTrack>& tracks) {
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

void TidalTrackListModel::clearTracks() {
    setTracks({});
}

TidalTrack TidalTrackListModel::trackAtRow(int row) const {
    if (row < 0 || row >= m_tracks.size()) {
        return {};
    }
    return m_tracks.at(row);
}

bool TidalTrackListModel::hasTrackAtRow(int row) const {
    return row >= 0 && row < m_tracks.size();
}

int TidalTrackListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_tracks.size();
}

int TidalTrackListModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant TidalTrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) {
        return {};
    }
    const TidalTrack& track = m_tracks.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case Qt::UserRole:
        switch (index.column()) {
        case ColumnArtist:
            return track.artist;
        case ColumnTitle:
            return track.isExplicit ? QStringLiteral("%1  [E]").arg(track.title)
                                    : track.title;
        case ColumnAlbum:
            return track.album;
        case ColumnDuration:
            return formatDuration(track.durationSec);
        case ColumnQuality:
            return shortQuality(track.audioQuality);
        default:
            break;
        }
        break;
    case Qt::ToolTipRole:
        return tr("%1 - %2\nAlbum: %3\nTIDAL ID: %4")
                .arg(track.artist, track.title, track.album)
                .arg(track.id);
    case Qt::TextAlignmentRole:
        if (index.column() == ColumnDuration) {
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        }
        break;
    default:
        break;
    }
    return {};
}

QVariant TidalTrackListModel::headerData(
        int section,
        Qt::Orientation orientation,
        int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case ColumnArtist:
        return tr("Artist");
    case ColumnTitle:
        return tr("Title");
    case ColumnAlbum:
        return tr("Album");
    case ColumnDuration:
        return tr("Duration");
    case ColumnQuality:
        return tr("Quality");
    default:
        return {};
    }
}

Qt::ItemFlags TidalTrackListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QMimeData* TidalTrackListModel::mimeData(const QModelIndexList& indexes) const {
    QList<mixxx::tidal::TidalTrack> tracks;
    for (const QModelIndex& index : indexes) {
        if (index.isValid() && index.column() == ColumnArtist) {
            const TidalTrack track = trackAtRow(index.row());
            if (track.id != 0) {
                tracks.append(track);
            }
        }
    }
    return mixxx::tidal::encodeTracks(tracks);
}

QStringList TidalTrackListModel::mimeTypes() const {
    return {QString::fromLatin1(mixxx::tidal::tidalTracksMimeType())};
}

Qt::DropActions TidalTrackListModel::supportedDragActions() const {
    return Qt::CopyAction;
}

TrackModel::Capabilities TidalTrackListModel::getCapabilities() const {
    return Capability::LoadToDeck | Capability::LoadToPreviewDeck |
            Capability::LoadToSampler | Capability::Sorting;
}

TrackPointer TidalTrackListModel::getTrack(const QModelIndex& index) const {
    if (!index.isValid() || !m_pTrackCollectionManager) {
        return {};
    }
    const TidalTrack track = trackAtRow(index.row());
    if (track.id == 0) {
        return {};
    }
    // A temporary track is used as a placeholder until the stream has been
    // downloaded. The downloader replaces it by a real file before playback.
    // Note: The track must not have a file location, otherwise the cover art
    // and metadata import code would repeatedly try (and fail) to open a
    // non-existent local file. The TIDAL identity is carried in the metadata
    // URL instead.
    TrackPointer pTrack = Track::newTemporary();
    pTrack->setURL(QStringLiteral("tidal://track/%1").arg(track.id));
    pTrack->setArtist(track.artist);
    pTrack->setTitle(track.title);
    pTrack->setAlbum(track.album);
    pTrack->setYear(track.year);
    pTrack->setTrackNumber(track.trackNumber > 0
                    ? QString::number(track.trackNumber)
                    : QString());
    pTrack->setDuration(static_cast<double>(track.durationSec));
    pTrack->setComment(tr("TIDAL stream (quality: %1)").arg(shortQuality(track.audioQuality)));
    return pTrack;
}

TrackPointer TidalTrackListModel::getTrackByRef(const TrackRef& trackRef) const {
    Q_UNUSED(trackRef);
    return {};
}

QUrl TidalTrackListModel::getTrackUrl(const QModelIndex& index) const {
    const TidalTrack track = trackAtRow(index.row());
    if (track.id == 0) {
        return {};
    }
    return QUrl(QStringLiteral("tidal://track/%1").arg(track.id));
}

QString TidalTrackListModel::getTrackLocation(const QModelIndex& index) const {
    const TidalTrack track = trackAtRow(index.row());
    if (track.id == 0) {
        return {};
    }
    return QStringLiteral("tidal://track/%1").arg(track.id);
}

TrackId TidalTrackListModel::getTrackId(const QModelIndex& index) const {
    Q_UNUSED(index);
    // TIDAL tracks are not part of the Mixxx library and have no TrackId.
    return TrackId();
}

CoverInfo TidalTrackListModel::getCoverInfo(const QModelIndex& index) const {
    Q_UNUSED(index);
    return CoverInfo();
}

const QVector<int> TidalTrackListModel::getTrackRows(TrackId trackId) const {
    Q_UNUSED(trackId);
    return {};
}

void TidalTrackListModel::search(const QString& searchText) {
    Q_UNUSED(searchText);
}

const QString TidalTrackListModel::currentSearch() const {
    return QString();
}

bool TidalTrackListModel::isColumnInternal(int column) {
    Q_UNUSED(column);
    return false;
}

bool TidalTrackListModel::isColumnHiddenByDefault(int column) {
    Q_UNUSED(column);
    return false;
}

TrackModel::SortColumnId TidalTrackListModel::sortColumnIdFromColumnIndex(int index) const {
    switch (index) {
    case ColumnArtist:
        return TrackModel::SortColumnId::Artist;
    case ColumnTitle:
        return TrackModel::SortColumnId::Title;
    case ColumnAlbum:
        return TrackModel::SortColumnId::Album;
    case ColumnDuration:
        return TrackModel::SortColumnId::Duration;
    default:
        return TrackModel::SortColumnId::Invalid;
    }
}

int TidalTrackListModel::columnIndexFromSortColumnId(
        TrackModel::SortColumnId sortColumn) const {
    switch (sortColumn) {
    case TrackModel::SortColumnId::Artist:
        return ColumnArtist;
    case TrackModel::SortColumnId::Title:
        return ColumnTitle;
    case TrackModel::SortColumnId::Album:
        return ColumnAlbum;
    case TrackModel::SortColumnId::Duration:
        return ColumnDuration;
    default:
        return -1;
    }
}

QString TidalTrackListModel::modelKey(bool noSearch) const {
    Q_UNUSED(noSearch);
    return QStringLiteral("tidal");
}

bool TidalTrackListModel::updateTrackGenre(Track* pTrack, const QString& genre) const {
    Q_UNUSED(pTrack);
    Q_UNUSED(genre);
    return false;
}
