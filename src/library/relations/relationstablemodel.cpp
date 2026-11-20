#include "library/relations/relationstablemodel.h"

#include "library/dao/trackschema.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "mixer/playermanager.h"
#include "moc_relationstablemodel.cpp"

namespace {
const QString kModelName = QStringLiteral("relations");
} // anonymous namespace

RelationsTableModel::RelationsTableModel(
        QObject* parent,
        TrackCollectionManager* pTrackCollectionManager)
        : BaseSqlTableModel(
                  parent,
                  pTrackCollectionManager,
                  "mixxx.db.model.relations") {
    showAllRelations();
}

bool RelationsTableModel::isColumnInternal(int column) {
    return column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ID) ||
            column == fieldIndex(ColumnCache::COLUMN_RELATIONGROUPSTABLE_ID) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_PLAYED) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_MIXXXDELETED) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM_LOCK) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_KEY_ID) ||
            column == fieldIndex(ColumnCache::COLUMN_TRACKLOCATIONSTABLE_FSDELETED) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_PREVIEW) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_SOURCE) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_TYPE) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_LOCATION) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_COLOR) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_DIGEST) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART_HASH);
}

bool RelationsTableModel::isColumnHiddenByDefault(int column) {
    return column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DATETIMEADDED) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COMMENT) ||
            column == fieldIndex(ColumnCache::COLUMN_RELATIONGROUPSTABLE_DATETIMEADDED) ||
            BaseSqlTableModel::isColumnHiddenByDefault(column);
}

DbId RelationsTableModel::getRelationId(const QModelIndex& index) const {
    if (index.isValid()) {
        // Return the Group ID, since that identifies the relation group
        return DbId(
                index.sibling(index.row(),
                             fieldIndex(ColumnCache::COLUMN_RELATIONGROUPSTABLE_ID))
                        .data());
    } else {
        return DbId();
    }
}

void RelationsTableModel::showRelatedTracks(TrackPointer pTrack) {
    if (!pTrack) {
        return;
    }
    m_pDeckTrack = pTrack;
    TrackId trackId = pTrack->getId();
    QString trackTableName = QStringLiteral("relations_track_%1").arg(trackId.toString());

    QStringList columns;
    columns << "library." + LIBRARYTABLE_ID
            << "g." + RELATIONGROUPSTABLE_ID
            << "r." + RELATIONSTABLE_TYPE
            << "g." + RELATIONGROUPSTABLE_COMMENT
            << "g." + RELATIONGROUPSTABLE_DATETIMEADDED
            << "gt." + RELATIONGROUPTRACKSTABLE_COMMENT
            << "'' AS " + LIBRARYTABLE_PREVIEW
            << LIBRARYTABLE_COVERART_DIGEST + " AS " + LIBRARYTABLE_COVERART;

    // Fetch the OTHER tracks in every group that this track belongs to
    QString queryString = QStringLiteral(
            "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
            "SELECT %2 "
            "FROM relation_group_tracks self "
            "JOIN relation_groups g ON g.id = self.rgt_group_id "
            "JOIN relations r ON r.id = g.rg_rel_id "
            "JOIN relation_group_tracks gt ON gt.rgt_group_id = g.id "
            "JOIN library ON library.id = gt.rgt_track_id "
            "WHERE self.rgt_track_id = %3 "
            "AND gt.rgt_track_id != %3 "
            "ORDER BY g.id, library.artist, library.title")
                                  .arg(trackTableName, columns.join(','), trackId.toString());

    QSqlQuery query(m_database);
    query.prepare(queryString);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }

    // Strip table prefixes so BaseSqlTableModel can parse the column names
    columns[0] = LIBRARYTABLE_ID;
    columns[1] = RELATIONGROUPSTABLE_ID;
    columns[2] = RELATIONSTABLE_TYPE;
    columns[3] = RELATIONGROUPSTABLE_COMMENT;
    columns[4] = RELATIONGROUPSTABLE_DATETIMEADDED;
    columns[5] = RELATIONGROUPTRACKSTABLE_COMMENT;
    columns[6] = LIBRARYTABLE_PREVIEW;
    columns[7] = LIBRARYTABLE_COVERART;

    setTable(trackTableName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());
    setSearch("");
    setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST), Qt::AscendingOrder);
}

void RelationsTableModel::showAllRelations() {
    QString tableName = QStringLiteral("all_relations");

    QStringList columns;
    columns << "library." + LIBRARYTABLE_ID
            << "g." + RELATIONGROUPSTABLE_ID
            << "r." + RELATIONSTABLE_TYPE
            << "g." + RELATIONGROUPSTABLE_COMMENT
            << "g." + RELATIONGROUPSTABLE_DATETIMEADDED
            << "gt." + RELATIONGROUPTRACKSTABLE_COMMENT
            << "'' AS " + LIBRARYTABLE_PREVIEW
            << LIBRARYTABLE_COVERART_DIGEST + " AS " + LIBRARYTABLE_COVERART;

    // Fetch ALL tracks in ALL groups
    QString queryString = QStringLiteral(
            "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
            "SELECT %2 "
            "FROM library "
            "JOIN relation_group_tracks gt ON gt.rgt_track_id = library.id "
            "JOIN relation_groups g ON g.id = gt.rgt_group_id "
            "JOIN relations r ON r.id = g.rg_rel_id "
            "ORDER BY g.id, library.artist, library.title")
                                  .arg(tableName, columns.join(','));

    QSqlQuery query(m_database);
    query.prepare(queryString);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }

    columns[0] = LIBRARYTABLE_ID;
    columns[1] = RELATIONGROUPSTABLE_ID;
    columns[2] = RELATIONSTABLE_TYPE;
    columns[3] = RELATIONGROUPSTABLE_COMMENT;
    columns[4] = RELATIONGROUPSTABLE_DATETIMEADDED;
    columns[5] = RELATIONGROUPTRACKSTABLE_COMMENT;
    columns[6] = LIBRARYTABLE_PREVIEW;
    columns[7] = LIBRARYTABLE_COVERART;

    setTable(tableName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());
}

TrackModel::Capabilities RelationsTableModel::getCapabilities() const {
    Capabilities caps =
            Capability::AddToTrackSet |
            Capability::EditMetadata |
            Capability::LoadToDeck |
            Capability::LoadToSampler |
            Capability::LoadToPreviewDeck |
            Capability::ResetPlayed |
            Capability::Hide |
            Capability::Analyze |
            Capability::Properties;

    return caps;
}

QString RelationsTableModel::modelKey(bool noSearch) const {
    if (m_pDeckTrack) {
        if (noSearch) {
            return kModelName + QChar(':') + m_pDeckTrack->getId().toString();
        }
        return kModelName + QChar(':') +
                m_pDeckTrack->getId().toString() +
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
