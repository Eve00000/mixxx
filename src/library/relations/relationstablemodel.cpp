#include "library/relations/relationstablemodel.h"

#include "library/dao/trackschema.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "mixer/playermanager.h"
#include "moc_relationstablemodel.cpp"
#include "track/track.h"

namespace {
const QString kModelName = QStringLiteral("relations");

// Builds a SQL condition for the relation views: every word of the search
// text must be found in at least one of the track or relation columns.
// keyword is "WHERE" or "AND", depending on the query it is appended to.
QString relationSearchClause(const QString& searchText, const QString& keyword) {
    const QStringList words =
            searchText.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return QString();
    }
    const QStringList columns = {
            QStringLiteral("library.artist"),
            QStringLiteral("library.title"),
            QStringLiteral("library.album"),
            QStringLiteral("library.album_artist"),
            QStringLiteral("library.genre"),
            QStringLiteral("library.composer"),
            QStringLiteral("library.grouping"),
            QStringLiteral("library.comment"),
            QStringLiteral("library.year"),
            QStringLiteral("r.rel_type"),
            QStringLiteral("g.rg_comment"),
            QStringLiteral("gt.rgt_comment")};

    QStringList wordConditions;
    QStringList columnConditions;
    for (const QString& word : words) {
        QString escaped = word;
        escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
        escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
        escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
        const QString pattern = QStringLiteral("'%") + escaped + QStringLiteral("%'");

        for (const QString& column : columns) {
            columnConditions.append(column + QStringLiteral(" LIKE ") + pattern +
                    QStringLiteral(" ESCAPE '\\'"));
        }
        wordConditions.append(QStringLiteral("(") +
                columnConditions.join(QStringLiteral(" OR ")) + QStringLiteral(")"));
    }
    return keyword + QLatin1Char(' ') + wordConditions.join(QStringLiteral(" AND "));
}
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
            BaseSqlTableModel::isColumnHiddenByDefault(column);
}

DbId RelationsTableModel::getRelationId(const QModelIndex& index) const {
    if (index.isValid()) {
        // -> GroupID = relation identifier
        return DbId(
                index.sibling(index.row(),
                             fieldIndex(ColumnCache::COLUMN_RELATIONGROUPSTABLE_ID_ALIAS))
                        .data());
    } else {
        return DbId();
    }
}

void RelationsTableModel::showRelatedTracks(TrackPointer pTrack) {
    if (!pTrack)
        return;
    m_pDeckTrack = pTrack;
    TrackId trackId = pTrack->getId();
    QString trackTableName = QStringLiteral("relations_track_%1").arg(trackId.toString());

    QSqlQuery dropQuery(m_database);
    dropQuery.exec(QStringLiteral("DROP VIEW IF EXISTS %1").arg(trackTableName));

    QString queryString = QStringLiteral(
            "CREATE TEMPORARY VIEW %1 AS "
            "SELECT "
            "  library.id AS id,"
            "  g.id AS group_id,"
            "  r.rel_type AS rel_type_alias,"
            "  g.rg_comment AS rg_comment_alias,"
            "  g.rg_datetime_added AS relation_date_added_alias,"
            "  gt.rgt_comment AS track_comment,"
            "  '' AS preview,"
            "  library.coverart_digest AS coverart "
            "FROM relation_group_tracks self "
            "JOIN relation_groups g ON g.id = self.rgt_group_id "
            "JOIN relations r ON r.id = g.rg_rel_id "
            "JOIN relation_group_tracks gt ON gt.rgt_group_id = g.id "
            "JOIN library ON library.id = gt.rgt_track_id "
            "WHERE self.rgt_track_id = %2 "
            "AND gt.rgt_track_id != %2 "
            "%3 "
            "ORDER BY g.id, library.artist, library.title")
                                  .arg(trackTableName,
                                          trackId.toString(),
                                          relationSearchClause(m_relationSearch,
                                                  QStringLiteral("AND")));

    QSqlQuery query(m_database);
    query.prepare(queryString);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }

    QStringList columns;
    columns << "id"
            << "group_id"
            << "rel_type_alias"
            << "rg_comment_alias"
            << "relation_date_added_alias"
            << "track_comment"
            << "preview"
            << "coverart";

    setTable(trackTableName,
            "id",
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());
    // qDebug() << "RelationsTableModel::showAllRelations() - Columns and headers:";
    // for (int i = 0; i < columns.size(); ++i) {
    //    qDebug() << "Column" << i << ":" << columns[i]
    //             << "| Header:" << headerData(i, Qt::Horizontal).toString();
    //}
    setSearch("");
    setDefaultSort(fieldIndex("artist"), Qt::AscendingOrder);
}

void RelationsTableModel::showAllRelations() {
    QString tableName = QStringLiteral("all_relations");

    QSqlQuery dropQuery(m_database);
    dropQuery.exec(QStringLiteral("DROP VIEW IF EXISTS %1").arg(tableName));

    QString queryString = QStringLiteral(
            "CREATE TEMPORARY VIEW %1 AS "
            "SELECT "
            "  library.id AS id,"
            "  g.id AS group_id,"
            "  r.rel_type AS rel_type_alias,"
            "  g.rg_comment AS rg_comment_alias,"
            "  g.rg_datetime_added AS relation_date_added_alias,"
            "  gt.rgt_comment AS track_comment,"
            "  '' AS preview,"
            "  library.coverart_digest AS coverart "
            "FROM library "
            "JOIN relation_group_tracks gt ON gt.rgt_track_id = library.id "
            "JOIN relation_groups g ON g.id = gt.rgt_group_id "
            "JOIN relations r ON r.id = g.rg_rel_id "
            "%2 "
            "ORDER BY g.id, library.artist, library.title")
                                  .arg(tableName,
                                          relationSearchClause(m_relationSearch,
                                                  QStringLiteral("WHERE")));

    QSqlQuery query(m_database);
    query.prepare(queryString);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }

    QStringList columns;
    columns << "id"
            << "group_id"
            << "rel_type_alias"
            << "rg_comment_alias"
            << "relation_date_added_alias"
            << "track_comment"
            << "preview"
            << "coverart";

    setTable(tableName,
            "id",
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());
    // qDebug() << "RelationsTableModel::showAllRelations() - Columns and headers:";
    // for (int i = 0; i < columns.size(); ++i) {
    //    qDebug() << "Column" << i << ":" << columns[i]
    //             << "| Header:" << headerData(i, Qt::Horizontal).toString();
    // }
}

void RelationsTableModel::searchRelations(const QString& searchText) {
    if (m_relationSearch == searchText) {
        return;
    }
    m_relationSearch = searchText;
    // The search is part of the SQL of the relation views,
    // rebuild the current view and reload it
    if (m_pDeckTrack) {
        showRelatedTracks(m_pDeckTrack);
    } else {
        showAllRelations();
    }
    select();
}

TrackModel::Capabilities RelationsTableModel::getCapabilities() const {
    Capabilities caps =
            Capability::Sorting |
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
                m_relationSearch;
    } else {
        if (noSearch) {
            return kModelName;
        }
        return kModelName + QChar('#') +
                m_relationSearch;
    }
}
