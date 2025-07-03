#include "library/trackset/searchcrate/searchcratestorage.h"

#include "library/dao/trackschema.h"
#include "library/queryutil.h"
#include "library/trackset/searchcrate/searchcrate.h"
#include "library/trackset/searchcrate/searchcrateschema.h"
#include "library/trackset/searchcrate/searchcratesummary.h"
#include "util/db/dbconnection.h"
#include "util/db/fwdsqlquery.h"
#include "util/db/sqllikewildcards.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("SearchCrateStorage");

const QString SEARCHCRATETABLE_LOCKED = "locked";

const QString SEARCHCRATE_SUMMARY_VIEW = "searchCrate_summary";

const QString SEARCHCRATESUMMARY_TRACK_COUNT = "track_count";
const QString SEARCHCRATESUMMARY_TRACK_DURATION = "track_duration";

const QString kSearchCrateTracksJoin =
        QStringLiteral("LEFT JOIN %3 ON %3.%4=%1.%2")
                .arg(SEARCHCRATE_TABLE,
                        SEARCHCRATETABLE_ID,
                        SEARCHCRATE_TRACKS_TABLE,
                        SEARCHCRATETRACKSTABLE_SEARCHCRATEID);

const QString kLibraryTracksJoin = kSearchCrateTracksJoin +
        QStringLiteral(" LEFT JOIN %3 ON %3.%4=%1.%2")
                .arg(SEARCHCRATE_TRACKS_TABLE,
                        SEARCHCRATETRACKSTABLE_TRACKID,
                        LIBRARY_TABLE,
                        LIBRARYTABLE_ID);

const QString kSearchCrateSummaryViewSelect =
        QStringLiteral(
                "SELECT %1.*,"
                "COUNT(CASE %2.%4 WHEN 0 THEN 1 ELSE NULL END) AS %5,"
                "SUM(CASE %2.%4 WHEN 0 THEN %2.%3 ELSE 0 END) AS %6 "
                "FROM %1")
                .arg(
                        SEARCHCRATE_TABLE,
                        LIBRARY_TABLE,
                        LIBRARYTABLE_DURATION,
                        LIBRARYTABLE_MIXXXDELETED,
                        SEARCHCRATESUMMARY_TRACK_COUNT,
                        SEARCHCRATESUMMARY_TRACK_DURATION);

const QString kSearchCrateSummaryViewQuery =
        QStringLiteral(
                "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS %2 %3 "
                "GROUP BY %4.%5")
                .arg(
                        SEARCHCRATE_SUMMARY_VIEW,
                        kSearchCrateSummaryViewSelect,
                        kLibraryTracksJoin,
                        SEARCHCRATE_TABLE,
                        SEARCHCRATETABLE_ID);

class SearchCrateQueryBinder final {
  public:
    explicit SearchCrateQueryBinder(FwdSqlQuery& query)
            : m_query(query) {
    }

    void bindId(const QString& placeholder, const SearchCrate& searchCrate) const {
        m_query.bindValue(placeholder, searchCrate.getId());
    }
    void bindName(const QString& placeholder, const SearchCrate& searchCrate) const {
        m_query.bindValue(placeholder, searchCrate.getName());
    }
    void bindLocked(const QString& placeholder, const SearchCrate& searchCrate) const {
        m_query.bindValue(placeholder, QVariant(searchCrate.isLocked()));
    }
    void bindAutoDjSource(const QString& placeholder, const SearchCrate& searchCrate) const {
        m_query.bindValue(placeholder, QVariant(searchCrate.isAutoDjSource()));
    }

  protected:
    FwdSqlQuery& m_query;
};

const QChar kSqlListSeparator(',');

// It is not possible to bind multiple values as a list to a query.
// The list of track ids has to be transformed into a single list
// string before it can be used in an SQL query.
QString joinSqlStringList(const QList<TrackId>& trackIds) {
    QString joinedTrackIds;
    // Reserve memory up front to prevent reallocation. Here we
    // assume that all track ids fit into 6 decimal digits and
    // add 1 character for the list separator.
    joinedTrackIds.reserve((6 + 1) * trackIds.size());
    for (const auto& trackId : trackIds) {
        if (!joinedTrackIds.isEmpty()) {
            joinedTrackIds += kSqlListSeparator;
        }
        joinedTrackIds += trackId.toString();
    }
    return joinedTrackIds;
}

} // anonymous namespace

SearchCrateQueryFields::SearchCrateQueryFields(const FwdSqlQuery& query)
        : m_iId(query.fieldIndex(SEARCHCRATETABLE_ID)),
          m_iName(query.fieldIndex(SEARCHCRATETABLE_NAME)),
          m_iLocked(query.fieldIndex(SEARCHCRATETABLE_LOCKED)),
          m_iAutoDjSource(query.fieldIndex(SEARCHCRATETABLE_AUTODJ_SOURCE)) {
}

void SearchCrateQueryFields::populateFromQuery(
        const FwdSqlQuery& query,
        SearchCrate* pSearchCrate) const {
    pSearchCrate->setId(getId(query));
    pSearchCrate->setName(getName(query));
    pSearchCrate->setLocked(isLocked(query));
    pSearchCrate->setAutoDjSource(isAutoDjSource(query));
}

SearchCrateTrackQueryFields::SearchCrateTrackQueryFields(const FwdSqlQuery& query)
        : m_iSearchCrateId(query.fieldIndex(SEARCHCRATETRACKSTABLE_SEARCHCRATEID)),
          m_iTrackId(query.fieldIndex(SEARCHCRATETRACKSTABLE_TRACKID)) {
}

// TrackQueryFields::TrackQueryFields(const FwdSqlQuery& query)
//         : m_iTrackId(query.fieldIndex(SEARCHCRATETRACKSTABLE_TRACKID)) {
// }

SearchCrateSummaryQueryFields::SearchCrateSummaryQueryFields(const FwdSqlQuery& query)
        : SearchCrateQueryFields(query),
          m_iTrackCount(query.fieldIndex(SEARCHCRATESUMMARY_TRACK_COUNT)),
          m_iTrackDuration(query.fieldIndex(SEARCHCRATESUMMARY_TRACK_DURATION)) {
}

void SearchCrateSummaryQueryFields::populateFromQuery(
        const FwdSqlQuery& query,
        SearchCrateSummary* pSearchCrateSummary) const {
    SearchCrateQueryFields::populateFromQuery(query, pSearchCrateSummary);
    pSearchCrateSummary->setTrackCount(getTrackCount(query));
    pSearchCrateSummary->setTrackDuration(getTrackDuration(query));
}

void SearchCrateStorage::repairDatabase(const QSqlDatabase& database) {
    // NOTE(uklotzde): No transactions
    // All queries are independent so there is no need to enclose some
    // or all of them in a transaction. Grouping into transactions would
    // improve the overall performance at the cost of increased resource
    // utilization. Since performance is not an issue for a maintenance
    // operation the decision was not to use any transactions.

    // NOTE(uklotzde): Nested scopes
    // Each of the following queries is enclosed in a nested scope.
    // When leaving this scope all resources allocated while executing
    // the query are released implicitly and before executing the next
    // query.

    // SearchCrates
    {
        // Delete searchCrates with empty names
        FwdSqlQuery query(database,
                QStringLiteral("DELETE FROM %1 WHERE %2 IS NULL OR TRIM(%2)=''")
                        .arg(SEARCHCRATE_TABLE, SEARCHCRATETABLE_NAME));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning()
                    << "Deleted" << query.numRowsAffected()
                    << "searchCrates with empty names";
        }
    }
    {
        // Fix invalid values in the "locked" column
        FwdSqlQuery query(database,
                QStringLiteral("UPDATE %1 SET %2=0 WHERE %2 NOT IN (0,1)")
                        .arg(SEARCHCRATE_TABLE, SEARCHCRATETABLE_LOCKED));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning()
                    << "Fixed boolean values in table" << SEARCHCRATE_TABLE
                    << "column" << SEARCHCRATETABLE_LOCKED
                    << "for" << query.numRowsAffected() << "searchCrates";
        }
    }
    {
        // Fix invalid values in the "autodj_source" column
        FwdSqlQuery query(database,
                QStringLiteral("UPDATE %1 SET %2=0 WHERE %2 NOT IN (0,1)")
                        .arg(SEARCHCRATE_TABLE, SEARCHCRATETABLE_AUTODJ_SOURCE));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning()
                    << "Fixed boolean values in table" << SEARCHCRATE_TABLE
                    << "column" << SEARCHCRATETABLE_AUTODJ_SOURCE
                    << "for" << query.numRowsAffected() << "searchCrates";
        }
    }

    // SearchCrate tracks
    {
        // Remove tracks from non-existent searchCrates
        FwdSqlQuery query(database,
                QStringLiteral(
                        "DELETE FROM %1 WHERE %2 NOT IN (SELECT %3 FROM %4)")
                        .arg(SEARCHCRATE_TRACKS_TABLE,
                                SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                                SEARCHCRATETABLE_ID,
                                SEARCHCRATE_TABLE));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning() << "Removed" << query.numRowsAffected()
                              << "searchCrate tracks from non-existent searchCrates";
        }
    }
    {
        // Remove library purged tracks from searchCrates
        FwdSqlQuery query(database,
                QStringLiteral(
                        "DELETE FROM %1 WHERE %2 NOT IN (SELECT %3 FROM %4)")
                        .arg(SEARCHCRATE_TRACKS_TABLE,
                                SEARCHCRATETRACKSTABLE_TRACKID,
                                LIBRARYTABLE_ID,
                                LIBRARY_TABLE));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning() << "Removed" << query.numRowsAffected()
                              << "library purged tracks from searchCrates";
        }
    }
}

void SearchCrateStorage::connectDatabase(const QSqlDatabase& database) {
    m_database = database;
    createViews();
}

void SearchCrateStorage::disconnectDatabase() {
    // Ensure that we don't use the current database connection
    // any longer.
    m_database = QSqlDatabase();
}

void SearchCrateStorage::createViews() {
    VERIFY_OR_DEBUG_ASSERT(
            FwdSqlQuery(m_database, kSearchCrateSummaryViewQuery).execPrepared()) {
        kLogger.critical()
                << "Failed to create database view for searchCrate summaries!";
    }
}

uint SearchCrateStorage::countSearchCrates() const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT COUNT(*) FROM %1").arg(SEARCHCRATE_TABLE));
    if (query.execPrepared() && query.next()) {
        uint result = query.fieldValue(0).toUInt();
        DEBUG_ASSERT(!query.next());
        return result;
    } else {
        return 0;
    }
}

bool SearchCrateStorage::readSearchCrateById(SearchCrateId id, SearchCrate* pSearchCrate) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:id")
                    .arg(SEARCHCRATE_TABLE, SEARCHCRATETABLE_ID));
    query.bindValue(":id", id);
    if (query.execPrepared()) {
        SearchCrateSelectResult searchCrates(std::move(query));
        if ((pSearchCrate != nullptr) ? searchCrates.populateNext(pSearchCrate)
                                      : searchCrates.next()) {
            VERIFY_OR_DEBUG_ASSERT(!searchCrates.next()) {
                kLogger.warning() << "Ambiguous searchCrate id:" << id;
            }
            return true;
        } else {
            kLogger.warning() << "SearchCrate not found by id:" << id;
        }
    }
    return false;
}

bool SearchCrateStorage::readSearchCrateByName(
        const QString& name, SearchCrate* pSearchCrate) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:name")
                    .arg(SEARCHCRATE_TABLE, SEARCHCRATETABLE_NAME));
    query.bindValue(":name", name);
    if (query.execPrepared()) {
        SearchCrateSelectResult searchCrates(std::move(query));
        if ((pSearchCrate != nullptr) ? searchCrates.populateNext(pSearchCrate)
                                      : searchCrates.next()) {
            VERIFY_OR_DEBUG_ASSERT(!searchCrates.next()) {
                kLogger.warning() << "Ambiguous searchCrate name:" << name;
            }
            return true;
        } else {
            if (kLogger.debugEnabled()) {
                kLogger.debug() << "SearchCrate not found by name:" << name;
            }
        }
    }
    return false;
}

SearchCrateSelectResult SearchCrateStorage::selectSearchCrates() const {
    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT * FROM %1 ORDER BY %2")
                            .arg(SEARCHCRATE_TABLE, SEARCHCRATETABLE_NAME)));

    if (query.execPrepared()) {
        return SearchCrateSelectResult(std::move(query));
    } else {
        return SearchCrateSelectResult();
    }
}

SearchCrateSelectResult SearchCrateStorage::selectSearchCratesByIds(
        const QString& subselectForSearchCrateIds,
        SqlSubselectMode subselectMode) const {
    QString subselectPrefix;
    switch (subselectMode) {
    case SQL_SUBSELECT_IN:
        if (subselectForSearchCrateIds.isEmpty()) {
            // edge case: no searchCrates
            return SearchCrateSelectResult();
        }
        subselectPrefix = "IN";
        break;
    case SQL_SUBSELECT_NOT_IN:
        if (subselectForSearchCrateIds.isEmpty()) {
            // edge case: all searchCrates
            return selectSearchCrates();
        }
        subselectPrefix = "NOT IN";
        break;
    }
    DEBUG_ASSERT(!subselectPrefix.isEmpty());
    DEBUG_ASSERT(!subselectForSearchCrateIds.isEmpty());

    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT * FROM %1 "
                                   "WHERE %2 %3 (%4) "
                                   "ORDER BY %5")
                            .arg(SEARCHCRATE_TABLE,
                                    SEARCHCRATETABLE_ID,
                                    subselectPrefix,
                                    subselectForSearchCrateIds,
                                    SEARCHCRATETABLE_NAME)));

    if (query.execPrepared()) {
        return SearchCrateSelectResult(std::move(query));
    } else {
        return SearchCrateSelectResult();
    }
}

// SearchCrateSelectResult SearchCrateStorage::selectAutoDjSearchCrates(bool autoDjSource) const {
//     FwdSqlQuery query(m_database,
//             mixxx::DbConnection::collateLexicographically(
//                     QStringLiteral("SELECT * FROM %1 WHERE %2=:autoDjSource "
//                                    "ORDER BY %3")
//                             .arg(SEARCHCRATE_TABLE,
//                                     SEARCHCRATETABLE_AUTODJ_SOURCE,
//                                     SEARCHCRATETABLE_NAME)));
//     query.bindValue(":autoDjSource", QVariant(autoDjSource));
//     if (query.execPrepared()) {
//         return SearchCrateSelectResult(std::move(query));
//     } else {
//         return SearchCrateSelectResult();
//     }
// }

SearchCrateSummarySelectResult SearchCrateStorage::selectSearchCrateSummaries() const {
    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT * FROM %1 ORDER BY %2")
                            .arg(SEARCHCRATE_SUMMARY_VIEW, SEARCHCRATETABLE_NAME)));
    if (query.execPrepared()) {
        return SearchCrateSummarySelectResult(std::move(query));
    } else {
        return SearchCrateSummarySelectResult();
    }
}

bool SearchCrateStorage::readSearchCrateSummaryById(
        SearchCrateId id, SearchCrateSummary* pSearchCrateSummary) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:id")
                    .arg(SEARCHCRATE_SUMMARY_VIEW, SEARCHCRATETABLE_ID));
    query.bindValue(":id", id);
    if (query.execPrepared()) {
        SearchCrateSummarySelectResult searchCrateSummaries(std::move(query));
        if ((pSearchCrateSummary != nullptr)
                        ? searchCrateSummaries.populateNext(pSearchCrateSummary)
                        : searchCrateSummaries.next()) {
            VERIFY_OR_DEBUG_ASSERT(!searchCrateSummaries.next()) {
                kLogger.warning() << "Ambiguous searchCrate id:" << id;
            }
            return true;
        } else {
            kLogger.warning() << "SearchCrate summary not found by id:" << id;
        }
    }
    return false;
}

uint SearchCrateStorage::countSearchCrateTracks(SearchCrateId searchCrateId) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2=:searchCrateId")
                    .arg(SEARCHCRATE_TRACKS_TABLE, SEARCHCRATETRACKSTABLE_SEARCHCRATEID));
    query.bindValue(":searchCrateId", searchCrateId);
    if (query.execPrepared() && query.next()) {
        uint result = query.fieldValue(0).toUInt();
        DEBUG_ASSERT(!query.next());
        return result;
    } else {
        return 0;
    }
}

// static
QString SearchCrateStorage::formatSubselectQueryForSearchCrateTrackIds(
        SearchCrateId searchCrateId) {
    return QStringLiteral("SELECT %1 FROM %2 WHERE %3=%4")
            .arg(SEARCHCRATETRACKSTABLE_TRACKID,
                    SEARCHCRATE_TRACKS_TABLE,
                    SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                    searchCrateId.toString());
}

QString SearchCrateStorage::formatQueryForTrackIdsBySearchCrateNameLike(
        const QString& searchCrateNameLike) const {
    FieldEscaper escaper(m_database);
    QString escapedSearchCrateNameLike = escaper.escapeString(
            kSqlLikeMatchAll + searchCrateNameLike + kSqlLikeMatchAll);
    return QString(
            "SELECT DISTINCT %1 FROM %2 "
            "JOIN %3 ON %4=%5 WHERE %6 LIKE %7 "
            "ORDER BY %1")
            .arg(SEARCHCRATETRACKSTABLE_TRACKID,
                    SEARCHCRATE_TRACKS_TABLE,
                    SEARCHCRATE_TABLE,
                    SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                    SEARCHCRATETABLE_ID,
                    SEARCHCRATETABLE_NAME,
                    escapedSearchCrateNameLike);
}

// static
QString SearchCrateStorage::formatQueryForTrackIdsWithSearchCrate() {
    return QStringLiteral(
            "SELECT DISTINCT %1 FROM %2 JOIN %3 ON %4=%5 ORDER BY %1")
            .arg(SEARCHCRATETRACKSTABLE_TRACKID,
                    SEARCHCRATE_TRACKS_TABLE,
                    SEARCHCRATE_TABLE,
                    SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                    SEARCHCRATETABLE_ID);
}

SearchCrateTrackSelectResult SearchCrateStorage::selectSearchCrateTracksSorted(
        SearchCrateId searchCrateId) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:searchCrateId ORDER BY %3")
                    .arg(SEARCHCRATE_TRACKS_TABLE,
                            SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                            SEARCHCRATETRACKSTABLE_TRACKID));
    query.bindValue(":searchCrateId", searchCrateId);
    if (query.execPrepared()) {
        return SearchCrateTrackSelectResult(std::move(query));
    } else {
        return SearchCrateTrackSelectResult();
    }
}

SearchCrateTrackSelectResult SearchCrateStorage::selectTrackSearchCratesSorted(
        TrackId trackId) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:trackId ORDER BY %3")
                    .arg(SEARCHCRATE_TRACKS_TABLE,
                            SEARCHCRATETRACKSTABLE_TRACKID,
                            SEARCHCRATETRACKSTABLE_SEARCHCRATEID));
    query.bindValue(":trackId", trackId);
    if (query.execPrepared()) {
        return SearchCrateTrackSelectResult(std::move(query));
    } else {
        return SearchCrateTrackSelectResult();
    }
}

SearchCrateSummarySelectResult SearchCrateStorage::selectSearchCratesWithTrackCount(
        const QList<TrackId>& trackIds) const {
    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT *, "
                                   "(SELECT COUNT(*) FROM %1 WHERE %2.%3 = %1.%4 and "
                                   "%1.%5 in (%9)) AS %6, "
                                   "0 as %7 FROM %2 ORDER BY %8")
                            .arg(
                                    SEARCHCRATE_TRACKS_TABLE,
                                    SEARCHCRATE_TABLE,
                                    SEARCHCRATETABLE_ID,
                                    SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                                    SEARCHCRATETRACKSTABLE_TRACKID,
                                    SEARCHCRATESUMMARY_TRACK_COUNT,
                                    SEARCHCRATESUMMARY_TRACK_DURATION,
                                    SEARCHCRATETABLE_NAME,
                                    joinSqlStringList(trackIds))));

    if (query.execPrepared()) {
        return SearchCrateSummarySelectResult(std::move(query));
    } else {
        return SearchCrateSummarySelectResult();
    }
}

SearchCrateTrackSelectResult SearchCrateStorage::selectTracksSortedBySearchCrateNameLike(
        const QString& searchCrateNameLike) const {
    // TODO: Do SQL LIKE wildcards in searchCrateNameLike need to be escaped?
    // Previously we used SqlLikeWildcardEscaper in the past for this
    // purpose. This utility class has become obsolete but could be
    // restored from the 2.3 branch if ever needed again.
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT %1,%2 FROM %3 "
                           "JOIN %4 ON %5 = %6 "
                           "WHERE %7 LIKE :searchCrateNameLike "
                           "ORDER BY %1")
                    .arg(SEARCHCRATETRACKSTABLE_TRACKID,
                            SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                            SEARCHCRATE_TRACKS_TABLE,
                            SEARCHCRATE_TABLE,
                            SEARCHCRATETABLE_ID,
                            SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                            SEARCHCRATETABLE_NAME));
    query.bindValue(":searchCrateNameLike",
            QVariant(kSqlLikeMatchAll + searchCrateNameLike + kSqlLikeMatchAll));

    if (query.execPrepared()) {
        return SearchCrateTrackSelectResult(std::move(query));
    } else {
        return SearchCrateTrackSelectResult();
    }
}

// TrackSelectResult SearchCrateStorage::selectAllTracksSorted() const {
//     FwdSqlQuery query(m_database,
//             QStringLiteral("SELECT DISTINCT %1 FROM %2 ORDER BY %1")
//                     .arg(SEARCHCRATETRACKSTABLE_TRACKID, SEARCHCRATE_TRACKS_TABLE));
//     if (query.execPrepared()) {
//         return TrackSelectResult(std::move(query));
//     } else {
//         return TrackSelectResult();
//     }
// }

QSet<SearchCrateId> SearchCrateStorage::collectSearchCrateIdsOfTracks(
        const QList<TrackId>& trackIds) const {
    // NOTE(uklotzde): One query per track id. This could be optimized
    // by querying for chunks of track ids and collecting the results.
    QSet<SearchCrateId> trackSearchCrates;
    for (const auto& trackId : trackIds) {
        // NOTE(uklotzde): The query result does not need to be sorted by searchCrate id
        // here. But since the corresponding FK column is indexed the impact on the
        // performance should be negligible. By reusing an existing query we reduce
        // the amount of code and the number of prepared SQL queries.
        SearchCrateTrackSelectResult searchCrateTracks(selectTrackSearchCratesSorted(trackId));
        while (searchCrateTracks.next()) {
            DEBUG_ASSERT(searchCrateTracks.trackId() == trackId);
            trackSearchCrates.insert(searchCrateTracks.searchCrateId());
        }
    }
    return trackSearchCrates;
}

bool SearchCrateStorage::onInsertingSearchCrate(
        const SearchCrate& searchCrate,
        SearchCrateId* pSearchCrateId) {
    VERIFY_OR_DEBUG_ASSERT(!searchCrate.getId().isValid()) {
        kLogger.warning()
                << "Cannot insert searchCrate with a valid id:" << searchCrate.getId();
        return false;
    }
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "INSERT INTO %1 (%2,%3,%4) "
                    "VALUES (:name,:locked,:autoDjSource)")
                    .arg(
                            SEARCHCRATE_TABLE,
                            SEARCHCRATETABLE_NAME,
                            SEARCHCRATETABLE_LOCKED,
                            SEARCHCRATETABLE_AUTODJ_SOURCE));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return false;
    }
    SearchCrateQueryBinder queryBinder(query);
    queryBinder.bindName(":name", searchCrate);
    queryBinder.bindLocked(":locked", searchCrate);
    queryBinder.bindAutoDjSource(":autoDjSource", searchCrate);
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return false;
    }
    if (query.numRowsAffected() > 0) {
        DEBUG_ASSERT(query.numRowsAffected() == 1);
        if (pSearchCrateId != nullptr) {
            *pSearchCrateId = SearchCrateId(query.lastInsertId());
            DEBUG_ASSERT(pSearchCrateId->isValid());
        }
        return true;
    } else {
        return false;
    }
}

bool SearchCrateStorage::onUpdatingSearchCrate(
        const SearchCrate& searchCrate) {
    VERIFY_OR_DEBUG_ASSERT(searchCrate.getId().isValid()) {
        kLogger.warning()
                << "Cannot update searchCrate without a valid id";
        return false;
    }
    FwdSqlQuery query(m_database,
            QString(
                    "UPDATE %1 "
                    "SET %2=:name,%3=:locked,%4=:autoDjSource "
                    "WHERE %5=:id")
                    .arg(
                            SEARCHCRATE_TABLE,
                            SEARCHCRATETABLE_NAME,
                            SEARCHCRATETABLE_LOCKED,
                            SEARCHCRATETABLE_AUTODJ_SOURCE,
                            SEARCHCRATETABLE_ID));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return false;
    }
    SearchCrateQueryBinder queryBinder(query);
    queryBinder.bindId(":id", searchCrate);
    queryBinder.bindName(":name", searchCrate);
    queryBinder.bindLocked(":locked", searchCrate);
    queryBinder.bindAutoDjSource(":autoDjSource", searchCrate);
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return false;
    }
    if (query.numRowsAffected() > 0) {
        VERIFY_OR_DEBUG_ASSERT(query.numRowsAffected() <= 1) {
            kLogger.warning()
                    << "Updated multiple searchCrates with the same id" << searchCrate.getId();
        }
        return true;
    } else {
        kLogger.warning()
                << "Cannot update non-existent searchCrate with id" << searchCrate.getId();
        return false;
    }
}

bool SearchCrateStorage::onDeletingSearchCrate(
        SearchCrateId searchCrateId) {
    VERIFY_OR_DEBUG_ASSERT(searchCrateId.isValid()) {
        kLogger.warning()
                << "Cannot delete searchCrate without a valid id";
        return false;
    }
    {
        FwdSqlQuery query(m_database,
                QStringLiteral("DELETE FROM %1 WHERE %2=:id")
                        .arg(SEARCHCRATE_TRACKS_TABLE, SEARCHCRATETRACKSTABLE_SEARCHCRATEID));
        VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
            return false;
        }
        query.bindValue(":id", searchCrateId);
        VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
            return false;
        }
        if (query.numRowsAffected() <= 0) {
            if (kLogger.debugEnabled()) {
                kLogger.debug()
                        << "Deleting empty searchCrate with id"
                        << searchCrateId;
            }
        }
    }
    {
        FwdSqlQuery query(m_database,
                QStringLiteral("DELETE FROM %1 WHERE %2=:id")
                        .arg(SEARCHCRATE_TABLE, SEARCHCRATETABLE_ID));
        VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
            return false;
        }
        query.bindValue(":id", searchCrateId);
        VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
            return false;
        }
        if (query.numRowsAffected() > 0) {
            VERIFY_OR_DEBUG_ASSERT(query.numRowsAffected() <= 1) {
                kLogger.warning()
                        << "Deleted multiple searchCrates with the same id" << searchCrateId;
            }
            return true;
        } else {
            kLogger.warning()
                    << "Cannot delete non-existent searchCrate with id" << searchCrateId;
            return false;
        }
    }
}

bool SearchCrateStorage::onAddingSearchCrateTracks(
        SearchCrateId searchCrateId,
        const QList<TrackId>& trackIds) {
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "INSERT OR IGNORE INTO %1 (%2, %3) "
                    "VALUES (:searchCrateId,:trackId)")
                    .arg(
                            SEARCHCRATE_TRACKS_TABLE,
                            SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                            SEARCHCRATETRACKSTABLE_TRACKID));
    if (!query.isPrepared()) {
        return false;
    }
    query.bindValue(":searchCrateId", searchCrateId);
    for (const auto& trackId : trackIds) {
        query.bindValue(":trackId", trackId);
        if (!query.execPrepared()) {
            return false;
        }
        if (query.numRowsAffected() == 0) {
            // track is already in searchCrate
            if (kLogger.debugEnabled()) {
                kLogger.debug()
                        << "Track" << trackId
                        << "not added to searchCrate" << searchCrateId;
            }
        } else {
            DEBUG_ASSERT(query.numRowsAffected() == 1);
        }
    }
    return true;
}

bool SearchCrateStorage::onRemovingSearchCrateTracks(
        SearchCrateId searchCrateId,
        const QList<TrackId>& trackIds) {
    // NOTE(uklotzde): We remove tracks in a loop
    // analogously to adding tracks (see above).
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "DELETE FROM %1 "
                    "WHERE %2=:searchCrateId AND %3=:trackId")
                    .arg(
                            SEARCHCRATE_TRACKS_TABLE,
                            SEARCHCRATETRACKSTABLE_SEARCHCRATEID,
                            SEARCHCRATETRACKSTABLE_TRACKID));
    if (!query.isPrepared()) {
        return false;
    }
    query.bindValue(":searchCrateId", searchCrateId);
    for (const auto& trackId : trackIds) {
        query.bindValue(":trackId", trackId);
        if (!query.execPrepared()) {
            return false;
        }
        if (query.numRowsAffected() == 0) {
            // track not found in searchCrate
            if (kLogger.debugEnabled()) {
                kLogger.debug()
                        << "Track" << trackId
                        << "not removed from searchCrate" << searchCrateId;
            }
        } else {
            DEBUG_ASSERT(query.numRowsAffected() == 1);
        }
    }
    return true;
}

bool SearchCrateStorage::onPurgingTracks(
        const QList<TrackId>& trackIds) {
    // NOTE(uklotzde): Remove tracks from searchCrates one-by-one.
    // This might be optimized by deleting multiple track ids
    // at once in chunks with a maximum size.
    FwdSqlQuery query(m_database,
            QStringLiteral("DELETE FROM %1 WHERE %2=:trackId")
                    .arg(SEARCHCRATE_TRACKS_TABLE, SEARCHCRATETRACKSTABLE_TRACKID));
    if (!query.isPrepared()) {
        return false;
    }
    for (const auto& trackId : trackIds) {
        query.bindValue(":trackId", trackId);
        if (!query.execPrepared()) {
            return false;
        }
    }
    return true;
}
