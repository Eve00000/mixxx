#include "library/streaming/streamingtracklistmodel.h"

#include <QSqlDatabase>
#include <QTime>
#include <QUuid>
#include <deque>
#include <string>

#include "library/streaming/streamingmimedata.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_streamingtracklistmodel.cpp"
#include "track/track.h"

using StreamingTrack = mixxx::streaming::Track;

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

/// Returns a process-lifetime stable namespace string for the given provider,
/// suitable for the TrackModel(const char*) constructor. std::deque never
/// invalidates references to existing elements when growing.
const char* settingsNamespaceForProvider(const QString& providerId) {
    static std::deque<std::string> storage;
    storage.emplace_back("mixxx.db.model." + providerId.toStdString());
    return storage.back().c_str();
}

QString formatDuration(int seconds) {
    if (seconds <= 0) {
        return QString();
    }
    return QTime(0, 0).addSecs(seconds).toString(
            seconds >= 3600 ? QStringLiteral("hh:mm:ss") : QStringLiteral("mm:ss"));
}

/// Human readable short quality derived from the provider-supplied string.
QString shortQuality(const QString& audioQuality) {
    if (audioQuality.isEmpty()) {
        return {};
    }
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

StreamingTrackListModel::StreamingTrackListModel(
        QObject* parent,
        TrackCollectionManager* pTrackCollectionManager,
        const QString& providerId,
        const QString& displayName)
        : QAbstractTableModel(parent),
          TrackModel(cloneDatabase(pTrackCollectionManager),
                  settingsNamespaceForProvider(providerId)),
          m_providerId(providerId),
          m_displayName(displayName),
          m_pTrackCollectionManager(pTrackCollectionManager) {
}

void StreamingTrackListModel::setTracks(const mixxx::streaming::TrackList& tracks) {
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

void StreamingTrackListModel::clearTracks() {
    setTracks({});
}

StreamingTrack StreamingTrackListModel::trackAtRow(int row) const {
    if (row < 0 || row >= m_tracks.size()) {
        return {};
    }
    return m_tracks.at(row);
}

bool StreamingTrackListModel::hasTrackAtRow(int row) const {
    return row >= 0 && row < m_tracks.size();
}

int StreamingTrackListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_tracks.size();
}

int StreamingTrackListModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant StreamingTrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) {
        return {};
    }
    const StreamingTrack& track = m_tracks.at(index.row());

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
        return tr("%1 - %2\nAlbum: %3\n%4 ID: %5")
                .arg(track.artist, track.title, track.album, m_displayName, track.id);
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

QVariant StreamingTrackListModel::headerData(
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

Qt::ItemFlags StreamingTrackListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QMimeData* StreamingTrackListModel::mimeData(const QModelIndexList& indexes) const {
    mixxx::streaming::TrackList tracks;
    QSet<int> seenRows;
    for (const QModelIndex& index : indexes) {
        if (!index.isValid() || seenRows.contains(index.row())) {
            continue;
        }
        seenRows.insert(index.row());
        const StreamingTrack track = trackAtRow(index.row());
        if (!track.id.isEmpty()) {
            tracks.append(track);
        }
    }
    return mixxx::streaming::encodeTracks(tracks);
}

QStringList StreamingTrackListModel::mimeTypes() const {
    return {QString::fromLatin1(mixxx::streaming::tracksMimeType())};
}

Qt::DropActions StreamingTrackListModel::supportedDragActions() const {
    return Qt::CopyAction;
}

TrackModel::Capabilities StreamingTrackListModel::getCapabilities() const {
    return Capability::LoadToDeck | Capability::LoadToPreviewDeck |
            Capability::LoadToSampler | Capability::Sorting;
}

QString StreamingTrackListModel::placeholderUrl(const StreamingTrack& track) const {
    return QStringLiteral("%1://track/%2").arg(m_providerId, track.id);
}

TrackPointer StreamingTrackListModel::getTrack(const QModelIndex& index) const {
    if (!index.isValid() || !m_pTrackCollectionManager) {
        return {};
    }
    const StreamingTrack track = trackAtRow(index.row());
    if (track.id.isEmpty()) {
        return {};
    }
    // A temporary track is used as a placeholder until the stream has been
    // downloaded. The downloader replaces it by a real file before playback.
    // Note: The track must not have a file location, otherwise the cover art
    // and metadata import code would repeatedly try (and fail) to open a
    // non-existent local file. The streaming identity is carried in the
    // metadata URL instead.
    TrackPointer pTrack = Track::newTemporary();
    pTrack->setURL(placeholderUrl(track));
    pTrack->setArtist(track.artist);
    pTrack->setTitle(track.title);
    pTrack->setAlbum(track.album);
    pTrack->setYear(track.year);
    pTrack->setTrackNumber(track.trackNumber > 0
                    ? QString::number(track.trackNumber)
                    : QString());
    pTrack->setDuration(static_cast<double>(track.durationSec));
    pTrack->setComment(tr("%1 stream (quality: %2)")
                               .arg(m_displayName, shortQuality(track.audioQuality)));
    return pTrack;
}

TrackPointer StreamingTrackListModel::getTrackByRef(const TrackRef& trackRef) const {
    Q_UNUSED(trackRef);
    return {};
}

QUrl StreamingTrackListModel::getTrackUrl(const QModelIndex& index) const {
    const StreamingTrack track = trackAtRow(index.row());
    if (track.id.isEmpty()) {
        return {};
    }
    return QUrl(placeholderUrl(track));
}

QString StreamingTrackListModel::getTrackLocation(const QModelIndex& index) const {
    const StreamingTrack track = trackAtRow(index.row());
    if (track.id.isEmpty()) {
        return {};
    }
    return placeholderUrl(track);
}

TrackId StreamingTrackListModel::getTrackId(const QModelIndex& index) const {
    Q_UNUSED(index);
    // Streaming tracks are not part of the Mixxx library and have no TrackId.
    return TrackId();
}

CoverInfo StreamingTrackListModel::getCoverInfo(const QModelIndex& index) const {
    Q_UNUSED(index);
    return CoverInfo();
}

const QVector<int> StreamingTrackListModel::getTrackRows(TrackId trackId) const {
    Q_UNUSED(trackId);
    return {};
}

void StreamingTrackListModel::search(const QString& searchText) {
    Q_UNUSED(searchText);
}

const QString StreamingTrackListModel::currentSearch() const {
    return QString();
}

bool StreamingTrackListModel::isColumnInternal(int column) {
    Q_UNUSED(column);
    return false;
}

bool StreamingTrackListModel::isColumnHiddenByDefault(int column) {
    Q_UNUSED(column);
    return false;
}

TrackModel::SortColumnId StreamingTrackListModel::sortColumnIdFromColumnIndex(
        int index) const {
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

int StreamingTrackListModel::columnIndexFromSortColumnId(
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

QString StreamingTrackListModel::modelKey(bool noSearch) const {
    Q_UNUSED(noSearch);
    return m_providerId;
}

bool StreamingTrackListModel::updateTrackGenre(Track* pTrack, const QString& genre) const {
    Q_UNUSED(pTrack);
    Q_UNUSED(genre);
    return false;
}
