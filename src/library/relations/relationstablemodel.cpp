#include "library/relations/relationstablemodel.h"

#include <QTextStream>

#include "library/dao/trackschema.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "mixer/playermanager.h"
#include "moc_relationstablemodel.cpp"
#include "track/track.h"
#include "util/db/dbconnection.h"

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

constexpr int kMaxSortColumns = 3;
const QString kColumnsSortingSetting = QStringLiteral("ColumnsSorting");

// Returns the SQL expressions to sort the relation views on a column.
// Empty if the column cannot be sorted.
QStringList relationSortExpressions(const QString& field) {
    const auto textSort = [](const QString& sqlColumn) {
        return mixxx::DbConnection::collateLexicographically(
                QStringLiteral("lower(") + sqlColumn + QStringLiteral(")"));
    };
    // Comments often start with a number (a BPM value, a sequence number):
    // sort on that number first, then on the text
    const auto numberFirstSort = [&textSort](const QString& sqlColumn) {
        return QStringList{
                QStringLiteral("CAST(") + sqlColumn + QStringLiteral(" AS REAL)"),
                textSort(sqlColumn)};
    };

    // Columns of the relation views
    if (field == RELATIONGROUPSTABLE_ID_ALIAS) {
        return QStringList{QStringLiteral("g.id")};
    }
    if (field == RELATIONSTABLE_TYPE_ALIAS) {
        return QStringList{textSort(QStringLiteral("r.rel_type"))};
    }
    if (field == RELATIONGROUPSTABLE_COMMENT_ALIAS) {
        return numberFirstSort(QStringLiteral("g.rg_comment"));
    }
    if (field == RELATIONGROUPSTABLE_DATETIMEADDED_ALIAS) {
        return QStringList{QStringLiteral("g.rg_datetime_added")};
    }
    if (field == RELATIONGROUPTRACKSTABLE_COMMENT_ALIAS) {
        return numberFirstSort(QStringLiteral("gt.rgt_comment"));
    }

    // Track columns, from the joined library table
    static const QStringList kTextFields = {
            LIBRARYTABLE_ARTIST,
            LIBRARYTABLE_TITLE,
            LIBRARYTABLE_ALBUM,
            LIBRARYTABLE_ALBUMARTIST,
            LIBRARYTABLE_GENRE,
            LIBRARYTABLE_COMPOSER,
            LIBRARYTABLE_GROUPING,
            LIBRARYTABLE_COMMENT,
            LIBRARYTABLE_FILETYPE,
            LIBRARYTABLE_YEAR};
    static const QStringList kIntegerFields = {
            LIBRARYTABLE_TRACKNUMBER,
            LIBRARYTABLE_BITRATE,
            LIBRARYTABLE_SAMPLERATE,
            LIBRARYTABLE_TIMESPLAYED};
    static const QStringList kPlainFields = {
            LIBRARYTABLE_DURATION,
            LIBRARYTABLE_BPM,
            LIBRARYTABLE_REPLAYGAIN,
            LIBRARYTABLE_DATETIMEADDED,
            LIBRARYTABLE_LAST_PLAYED_AT,
            LIBRARYTABLE_RATING,
            LIBRARYTABLE_TUNING_FREQUENCY,
            LIBRARYTABLE_COLOR};
    if (kTextFields.contains(field)) {
        return QStringList{textSort(QStringLiteral("library.") + field)};
    }
    if (kIntegerFields.contains(field)) {
        return QStringList{QStringLiteral("CAST(library.") + field +
                QStringLiteral(" AS INTEGER)")};
    }
    if (kPlainFields.contains(field)) {
        return QStringList{QStringLiteral("library.") + field};
    }
    if (field == LIBRARYTABLE_KEY) {
        return QStringList{QStringLiteral("library.key_id")};
    }
    if (field == TRACKLOCATIONSTABLE_LOCATION) {
        return QStringList{textSort(QStringLiteral(
                "(SELECT tl.location FROM track_locations tl WHERE tl.id = library.location)"))};
    }
    return QStringList();
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
            //"ORDER BY g.id, library.artist, library.title")
            "%4")
                                  .arg(trackTableName,
                                          trackId.toString(),
                                          relationSearchClause(m_relationSearch,
                                                  // QStringLiteral("AND")));
                                                  QStringLiteral("AND")),
                                          relationSortClause());

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
            //"ORDER BY g.id, library.artist, library.title")
            "%3")
                                  .arg(tableName,
                                          relationSearchClause(m_relationSearch,
                                                  //       QStringLiteral("WHERE")));
                                                  QStringLiteral("WHERE")),
                                          relationSortClause());

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

QString RelationsTableModel::relationSortClause() const {
    QStringList terms;
    for (int i = 0; i < m_sortFields.size(); ++i) {
        const QString direction = m_sortOrders.at(i) == Qt::AscendingOrder
                ? QStringLiteral(" ASC")
                : QStringLiteral(" DESC");
        const QStringList expressions = relationSortExpressions(m_sortFields.at(i));
        for (const QString& expression : expressions) {
            terms.append(expression + direction);
        }
    }
    // Default order after the chosen columns: keeps the groups together
    terms.append(QStringLiteral("g.id"));
    terms.append(QStringLiteral("library.artist"));
    terms.append(QStringLiteral("library.title"));
    return QStringLiteral("ORDER BY ") + terms.join(QStringLiteral(", "));
}

void RelationsTableModel::loadSortSettings() {
    m_sortFields.clear();
    m_sortOrders.clear();
    QString value = getModelSetting(kColumnsSortingSetting);
    QTextStream in(&value);
    while (!in.atEnd()) {
        QString name;
        int orderValue = -1;
        in >> name >> orderValue;
        if (name.isEmpty() ||
                m_sortFields.contains(name) ||
                m_sortFields.size() >= kMaxSortColumns ||
                relationSortExpressions(name).isEmpty()) {
            continue;
        }
        m_sortFields.append(name);
        m_sortOrders.append(orderValue > 0 ? Qt::AscendingOrder : Qt::DescendingOrder);
    }
}

bool RelationsTableModel::sortOverride(int column, Qt::SortOrder order) {
    const QString field =
            headerData(column, Qt::Horizontal, TrackModel::kHeaderNameRole).toString();
    if (field.isEmpty() || relationSortExpressions(field).isEmpty()) {
        // This column cannot be sorted: nothing to do, but the base class
        // must not sort on it either
        return true;
    }

    if (m_sortFields.isEmpty()) {
        // No sort history yet: load the last sort from the settings
        loadSortSettings();
    }

    if (!m_sortFields.isEmpty() && m_sortFields.first() == field) {
        // Only the order has changed
        m_sortOrders.replace(0, order);
    } else {
        // Remove the column if it is already in the history,
        // then make it the primary sort column
        const int existingIndex = m_sortFields.indexOf(field);
        if (existingIndex >= 0) {
            m_sortFields.removeAt(existingIndex);
            m_sortOrders.removeAt(existingIndex);
        }
        m_sortFields.prepend(field);
        m_sortOrders.prepend(order);
        while (m_sortFields.size() > kMaxSortColumns) {
            m_sortFields.removeLast();
            m_sortOrders.removeLast();
        }
    }

    // Write the sort history to the settings
    QString value;
    QTextStream out(&value);
    for (int i = 0; i < m_sortFields.size(); ++i) {
        out << m_sortFields.at(i) << " "
            << (m_sortOrders.at(i) == Qt::AscendingOrder ? 1 : -1) << " ";
    }
    out.flush();
    setModelSetting(kColumnsSortingSetting, value);

    // The sort order is part of the SQL of the relation views,
    // rebuild the current view and reload it
    if (m_pDeckTrack) {
        showRelatedTracks(m_pDeckTrack);
    } else {
        showAllRelations();
    }
    select();
    // the base class sorting must not run if sort on relationfields
    return true;
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
