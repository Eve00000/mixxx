#include "library/trackset/searchcrate/searchcratetablemodel.h"

#include <QtDebug>

#include "library/dao/trackschema.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/searchcrate/searchcrate.h"
#include "moc_searchcratetablemodel.cpp"
#include "track/track.h"
#include "util/db/fwdsqlquery.h"

namespace {

const QString kModelName = QStringLiteral("searchCrate");

} // anonymous namespace

SearchCrateTableModel::SearchCrateTableModel(
        QObject* pParent,
        TrackCollectionManager* pTrackCollectionManager)
        : TrackSetTableModel(
                  pParent,
                  pTrackCollectionManager,
                  "mixxx.db.model.searchCrate") {
}

void SearchCrateTableModel::selectSearchCrate(SearchCrateId searchCrateId) {
    // qDebug() << "SearchCrateTableModel::setSearchCrate()" << searchCrateId;
    if (searchCrateId == m_selectedSearchCrate) {
        qDebug() << "Already focused on searchCrate " << searchCrateId;
        return;
    }
    // Store search text
    QString currSearch = currentSearch();
    if (m_selectedSearchCrate.isValid()) {
        if (!currSearch.trimmed().isEmpty()) {
            m_searchTexts.insert(m_selectedSearchCrate, currSearch);
        } else {
            m_searchTexts.remove(m_selectedSearchCrate);
        }
    }

    m_selectedSearchCrate = searchCrateId;

    QString tableName = QStringLiteral("searchCrate_%1").arg(m_selectedSearchCrate.toString());
    QStringList columns;
    columns << LIBRARYTABLE_ID
            << "'' AS " + LIBRARYTABLE_PREVIEW
            // For sorting the cover art column we give LIBRARYTABLE_COVERART
            // the same value as the cover digest.
            << LIBRARYTABLE_COVERART_DIGEST + " AS " + LIBRARYTABLE_COVERART;
    // We hide files that have been explicitly deleted in the library
    // (mixxx_deleted = 0) from the view.
    // They are kept in the database, because we treat searchCrate membership as a
    // track property, which persist over a hide / unhide cycle.
    QString queryString =
            QString("CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
                    "SELECT %2 FROM %3 "
                    "WHERE %4 IN (%5) "
                    "AND %6=0")
                    .arg(tableName,
                            columns.join(","),
                            LIBRARY_TABLE,
                            LIBRARYTABLE_ID,
                            SearchCrateStorage::formatSubselectQueryForSearchCrateTrackIds(
                                    searchCrateId),
                            LIBRARYTABLE_MIXXXDELETED);
    FwdSqlQuery(m_database, queryString).execPrepared();

    columns[0] = LIBRARYTABLE_ID;
    columns[1] = LIBRARYTABLE_PREVIEW;
    columns[2] = LIBRARYTABLE_COVERART;
    setTable(tableName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());

    // Restore search text
    setSearch(m_searchTexts.value(m_selectedSearchCrate));
    setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST), Qt::AscendingOrder);
}

bool SearchCrateTableModel::addTrack(const QModelIndex& index, const QString& location) {
    Q_UNUSED(index);

    // This will only succeed if the file actually exist.
    mixxx::FileInfo fileInfo(location);
    if (!fileInfo.checkFileExists()) {
        qDebug() << "SearchCrateTableModel::addTrack:"
                 << "File" << location << "not found";
        return false;
    }

    // If a track is dropped but it isn't in the library, then add it because
    // the user probably dropped a file from outside Mixxx into this searchCrate.
    // If the track is already contained in the library it will not insert
    // a duplicate. It also handles unremoving logic if the track has been
    // removed from the library recently and re-adds it.
    const TrackPointer pTrack = m_pTrackCollectionManager->getOrAddTrack(
            TrackRef::fromFileInfo(fileInfo));
    if (!pTrack) {
        qDebug() << "SearchCrateTableModel::addTrack:"
                 << "Failed to add track" << location << "to library";
        return false;
    }

    QList<TrackId> trackIds;
    trackIds.append(pTrack->getId());
    if (!m_pTrackCollectionManager->internalCollection()->addSearchCrateTracks(
                m_selectedSearchCrate, trackIds)) {
        qDebug() << "SearchCrateTableModel::addTrack:"
                 << "Failed to add track" << location << "to searchCrate"
                 << m_selectedSearchCrate;
        return false;
    }

    // TODO(rryan) just add the track don't select
    select();
    return true;
}

TrackModel::Capabilities SearchCrateTableModel::getCapabilities() const {
    Capabilities caps =
            Capability::ReceiveDrops |
            Capability::AddToTrackSet |
            Capability::AddToAutoDJ |
            Capability::EditMetadata |
            Capability::LoadToDeck |
            Capability::LoadToSampler |
            Capability::LoadToPreviewDeck |
            // Capability::RemoveSearchCrate |
            Capability::ResetPlayed |
            Capability::Hide |
            Capability::RemoveFromDisk |
            Capability::Analyze |
            Capability::Properties |
            Capability::Sorting;

    if (m_selectedSearchCrate.isValid()) {
        SearchCrate searchCrate;
        if (m_pTrackCollectionManager->internalCollection()
                        ->searchCrates()
                        .readSearchCrateById(m_selectedSearchCrate, &searchCrate)) {
            if (searchCrate.isLocked()) {
                caps |= Capability::Locked;
            }
        } else {
            qWarning() << "Failed to read create" << m_selectedSearchCrate;
        }
    }

    return caps;
}

int SearchCrateTableModel::addTracksWithTrackIds(
        const QModelIndex& index, const QList<TrackId>& trackIds, int* pOutInsertionPos) {
    Q_UNUSED(index);

    if (pOutInsertionPos != nullptr) {
        // searchCrate insertion is not done by position, and no duplicates will be added,.
        // 0 indicates this to the caller.
        *pOutInsertionPos = 0;
    }

    // If a track is dropped but it isn't in the library, then add it because
    // the user probably dropped a file from outside Mixxx into this searchCrate.
    if (!m_pTrackCollectionManager->internalCollection()->addSearchCrateTracks(
                m_selectedSearchCrate, trackIds)) {
        qWarning() << "SearchCrateTableModel::addTracks could not add"
                   << trackIds.size() << "tracks to searchCrate" << m_selectedSearchCrate;
        return 0;
    }

    select();
    return trackIds.size();
}

bool SearchCrateTableModel::isLocked() {
    SearchCrate searchCrate;
    if (!m_pTrackCollectionManager->internalCollection()
                    ->searchCrates()
                    .readSearchCrateById(m_selectedSearchCrate, &searchCrate)) {
        qWarning() << "Failed to read create" << m_selectedSearchCrate;
        return false;
    }
    return searchCrate.isLocked();
}

void SearchCrateTableModel::removeTracks(const QModelIndexList& indices) {
    VERIFY_OR_DEBUG_ASSERT(m_selectedSearchCrate.isValid()) {
        return;
    }
    if (indices.empty()) {
        return;
    }

    SearchCrate searchCrate;
    if (!m_pTrackCollectionManager->internalCollection()
                    ->searchCrates()
                    .readSearchCrateById(m_selectedSearchCrate, &searchCrate)) {
        qWarning() << "Failed to read create" << m_selectedSearchCrate;
        return;
    }

    VERIFY_OR_DEBUG_ASSERT(!searchCrate.isLocked()) {
        return;
    }

    QList<TrackId> trackIds;
    trackIds.reserve(indices.size());
    for (const QModelIndex& index : indices) {
        trackIds.append(getTrackId(index));
    }
    if (!m_pTrackCollectionManager->internalCollection()->removeSearchCrateTracks(
                searchCrate.getId(), trackIds)) {
        qWarning() << "Failed to remove tracks from searchCrate" << searchCrate;
        return;
    }

    select();
}

QString SearchCrateTableModel::modelKey(bool noSearch) const {
    if (m_selectedSearchCrate.isValid()) {
        if (noSearch) {
            return kModelName + QChar(':') + m_selectedSearchCrate.toString();
        }
        return kModelName + QChar(':') +
                m_selectedSearchCrate.toString() +
                QChar('#') +
                currentSearch();
    } else {
        if (noSearch) {
            return kModelName;
        }
        return kModelName + QChar('#') +
                currentSearch();
    }
}
