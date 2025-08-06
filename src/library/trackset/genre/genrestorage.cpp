#include "library/trackset/genre/genrestorage.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

#include "library/dao/trackschema.h"
#include "library/queryutil.h"
#include "library/trackset/genre/genre.h"
#include "library/trackset/genre/genreschema.h"
#include "library/trackset/genre/genresummary.h"
#include "util/db/dbconnection.h"
#include "util/db/fwdsqlquery.h"
#include "util/db/sqllikewildcards.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("GenreStorage");

const QString GENRETABLE_LOCKED = "locked";

const QString GENRE_SUMMARY_VIEW = "genre_summary";

const QString GENRESUMMARY_TRACK_COUNT = "track_count";
const QString GENRESUMMARY_TRACK_DURATION = "track_duration";

const QString kGenreSummaryViewSelect =
        QStringLiteral(
                "SELECT %1.*,"
                "COUNT(CASE %2.%4 WHEN 0 THEN 1 ELSE NULL END) AS %5,"
                "SUM(CASE %2.%4 WHEN 0 THEN %2.%3 ELSE 0 END) AS %6 "
                "FROM %1")
                .arg(
                        GENRE_TABLE,
                        LIBRARY_TABLE,
                        LIBRARYTABLE_DURATION,
                        LIBRARYTABLE_MIXXXDELETED,
                        GENRESUMMARY_TRACK_COUNT,
                        GENRESUMMARY_TRACK_DURATION);

const QString kGenreSummaryViewQuery = QStringLiteral(
        "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
        "SELECT g.*, "
        "  (SELECT COUNT(*) FROM library l "
        "   WHERE l.mixxx_deleted = 0 "
        "     AND (l.genre LIKE '%%##' || g.id || '##%%' "
        "          OR EXISTS ("
        "            SELECT 1 FROM genres g2 "
        "            WHERE g2.display_group = g.id "
        "              AND l.genre LIKE '%%##' || g2.id || '##%%'"
        "          ))"
        "  ) AS %2, "
        "  (SELECT SUM(l.duration) FROM library l "
        "   WHERE l.mixxx_deleted = 0 "
        "     AND (l.genre LIKE '%%##' || g.id || '##%%' "
        "          OR EXISTS ("
        "            SELECT 1 FROM genres g2 "
        "            WHERE g2.display_group = g.id "
        "              AND l.genre LIKE '%%##' || g2.id || '##%%'"
        "          ))"
        "  ) AS %3 "
        "FROM genres g "
        "WHERE g.is_visible != 0")
                                               .arg(
                                                       GENRE_SUMMARY_VIEW,
                                                       GENRESUMMARY_TRACK_COUNT,
                                                       GENRESUMMARY_TRACK_DURATION);

class GenreQueryBinder final {
  public:
    explicit GenreQueryBinder(FwdSqlQuery& query)
            : m_query(query) {
    }

    void bindId(const QString& placeholder, const Genre& genre) const {
        m_query.bindValue(placeholder, genre.getId());
    }
    void bindName(const QString& placeholder, const Genre& genre) const {
        m_query.bindValue(placeholder, genre.getName());
    }
    void bindLocked(const QString& placeholder, const Genre& genre) const {
        m_query.bindValue(placeholder, QVariant(genre.isLocked()));
    }
    void bindAutoDjSource(const QString& placeholder, const Genre& genre) const {
        m_query.bindValue(placeholder, QVariant(genre.isAutoDjSource()));
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

GenreQueryFields::GenreQueryFields(const FwdSqlQuery& query)
        : m_iId(query.fieldIndex(GENRETABLE_ID)),
          m_iName(query.fieldIndex(GENRETABLE_NAME)),
          m_iLocked(query.fieldIndex(GENRETABLE_LOCKED)),
          m_iAutoDjSource(query.fieldIndex(GENRETABLE_AUTODJ_SOURCE)),
          m_iNameLevel1(query.fieldIndex(GENRETABLE_NAMELEVEL1)),
          m_iNameLevel2(query.fieldIndex(GENRETABLE_NAMELEVEL2)),
          m_iNameLevel3(query.fieldIndex(GENRETABLE_NAMELEVEL3)),
          m_iNameLevel4(query.fieldIndex(GENRETABLE_NAMELEVEL4)),
          m_iNameLevel5(query.fieldIndex(GENRETABLE_NAMELEVEL5)),
          m_iDisplayGroup(query.fieldIndex(GENRETABLE_DISPLAYGROUP)),
          m_iIsModelDefined(query.fieldIndex(GENRETABLE_ISMODELDEFINED)),
          m_iIsVisible(query.fieldIndex(GENRETABLE_ISVISIBLE)) {
}

void GenreQueryFields::populateFromQuery(
        const FwdSqlQuery& query,
        Genre* pGenre) const {
    pGenre->setId(getId(query));
    pGenre->setName(getName(query));
    pGenre->setLocked(isLocked(query));
    pGenre->setAutoDjSource(isAutoDjSource(query));

    pGenre->setNameLevel1(getNameLevel1(query));
    pGenre->setNameLevel2(getNameLevel2(query));
    pGenre->setNameLevel3(getNameLevel3(query));
    pGenre->setNameLevel4(getNameLevel4(query));
    pGenre->setNameLevel5(getNameLevel5(query));
    pGenre->setDisplayGroup(getDisplayGroup(query));
    pGenre->setVisible(isVisible(query));
    pGenre->setModelDefined(isModelDefined(query));
}

GenreTrackQueryFields::GenreTrackQueryFields(const FwdSqlQuery& query)
        : m_iGenreId(query.fieldIndex(kGenreIdField)),
          m_iTrackId(query.fieldIndex(kTrackIdField)) {
    if (m_iGenreId == -1 || m_iTrackId == -1) {
        qWarning() << "[GenreTrackQueryFields] Missing required columns in query result!";
    }
}

GenreSummaryQueryFields::GenreSummaryQueryFields(const FwdSqlQuery& query)
        : GenreQueryFields(query),
          m_iTrackCount(query.fieldIndex(GENRESUMMARY_TRACK_COUNT)),
          m_iTrackDuration(query.fieldIndex(GENRESUMMARY_TRACK_DURATION)) {
}

void GenreSummaryQueryFields::populateFromQuery(
        const FwdSqlQuery& query,
        GenreSummary* pGenreSummary) const {
    GenreQueryFields::populateFromQuery(query, pGenreSummary);
    pGenreSummary->setTrackCount(getTrackCount(query));
    pGenreSummary->setTrackDuration(getTrackDuration(query));
}

void GenreStorage::repairDatabase(const QSqlDatabase& database) {
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

    // Genres
    {
        // Delete genres with empty names
        FwdSqlQuery query(database,
                QStringLiteral("DELETE FROM %1 WHERE %2 IS NULL OR TRIM(%2)=''")
                        .arg(GENRE_TABLE, GENRETABLE_NAME));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning()
                    << "Deleted" << query.numRowsAffected()
                    << "genres with empty names";
        }
    }
    {
        // Fix invalid values in the "locked" column
        FwdSqlQuery query(database,
                QStringLiteral("UPDATE %1 SET %2=0 WHERE %2 NOT IN (0,1)")
                        .arg(GENRE_TABLE, GENRETABLE_LOCKED));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning()
                    << "Fixed boolean values in table" << GENRE_TABLE
                    << "column" << GENRETABLE_LOCKED
                    << "for" << query.numRowsAffected() << "genres";
        }
    }
    {
        // Fix invalid values in the "autodj_source" column
        FwdSqlQuery query(database,
                QStringLiteral("UPDATE %1 SET %2=0 WHERE %2 NOT IN (0,1)")
                        .arg(GENRE_TABLE, GENRETABLE_AUTODJ_SOURCE));
        if (query.execPrepared() && (query.numRowsAffected() > 0)) {
            kLogger.warning()
                    << "Fixed boolean values in table" << GENRE_TABLE
                    << "column" << GENRETABLE_AUTODJ_SOURCE
                    << "for" << query.numRowsAffected() << "genres";
        }
    }
}

void GenreStorage::connectDatabase(const QSqlDatabase& database) {
    m_database = database;
    createViews();
}

void GenreStorage::disconnectDatabase() {
    // Ensure that we don't use the current database connection
    // any longer.
    m_database = QSqlDatabase();
}

void GenreStorage::createViews() {
    VERIFY_OR_DEBUG_ASSERT(
            FwdSqlQuery(m_database, kGenreSummaryViewQuery).execPrepared()) {
        kLogger.critical()
                << "Failed to create database view for genre summaries!";
    }
}

uint GenreStorage::countGenres() const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT COUNT(*) FROM %1").arg(GENRE_TABLE));
    if (query.execPrepared() && query.next()) {
        uint result = query.fieldValue(0).toUInt();
        DEBUG_ASSERT(!query.next());
        return result;
    } else {
        return 0;
    }
}

bool GenreStorage::readGenreById(GenreId id, Genre* pGenre) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:id")
                    .arg(GENRE_TABLE, GENRETABLE_ID));
    query.bindValue(":id", id);
    if (query.execPrepared()) {
        GenreSelectResult genres(std::move(query));
        if ((pGenre != nullptr) ? genres.populateNext(pGenre) : genres.next()) {
            VERIFY_OR_DEBUG_ASSERT(!genres.next()) {
                kLogger.warning() << "Ambiguous genre id:" << id;
            }
            return true;
        } else {
            kLogger.warning() << "Genre not found by id:" << id;
        }
    }
    return false;
}

bool GenreStorage::readGenreByName(const QString& name, Genre* pGenre) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:name")
                    .arg(GENRE_TABLE, GENRETABLE_NAME));
    query.bindValue(":name", name);
    if (query.execPrepared()) {
        GenreSelectResult genres(std::move(query));
        if ((pGenre != nullptr) ? genres.populateNext(pGenre) : genres.next()) {
            VERIFY_OR_DEBUG_ASSERT(!genres.next()) {
                kLogger.warning() << "Ambiguous genre name:" << name;
            }
            return true;
        } else {
            if (kLogger.debugEnabled()) {
                kLogger.debug() << "Genre not found by name:" << name;
            }
        }
    }
    return false;
}

GenreSelectResult GenreStorage::selectGenres() const {
    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT * FROM %1 ORDER BY %2")
                            .arg(GENRE_TABLE, GENRETABLE_NAME)));

    if (query.execPrepared()) {
        return GenreSelectResult(std::move(query));
    } else {
        return GenreSelectResult();
    }
}

GenreSelectResult GenreStorage::selectGenresByIds(
        const QString& subselectForGenreIds,
        SqlSubselectMode subselectMode) const {
    QString subselectPrefix;
    switch (subselectMode) {
    case SQL_SUBSELECT_IN:
        if (subselectForGenreIds.isEmpty()) {
            // edge case: no genres
            return GenreSelectResult();
        }
        subselectPrefix = "IN";
        break;
    case SQL_SUBSELECT_NOT_IN:
        if (subselectForGenreIds.isEmpty()) {
            // edge case: all genres
            return selectGenres();
        }
        subselectPrefix = "NOT IN";
        break;
    }
    DEBUG_ASSERT(!subselectPrefix.isEmpty());
    DEBUG_ASSERT(!subselectForGenreIds.isEmpty());

    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT * FROM %1 "
                                   "WHERE %2 %3 (%4) "
                                   "ORDER BY %5")
                            .arg(GENRE_TABLE,
                                    GENRETABLE_ID,
                                    subselectPrefix,
                                    subselectForGenreIds,
                                    GENRETABLE_NAME)));

    if (query.execPrepared()) {
        return GenreSelectResult(std::move(query));
    } else {
        return GenreSelectResult();
    }
}

GenreSelectResult GenreStorage::selectAutoDjGenres(bool autoDjSource) const {
    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT * FROM %1 WHERE %2=:autoDjSource "
                                   "ORDER BY %3")
                            .arg(GENRE_TABLE,
                                    GENRETABLE_AUTODJ_SOURCE,
                                    GENRETABLE_NAME)));
    query.bindValue(":autoDjSource", QVariant(autoDjSource));
    if (query.execPrepared()) {
        return GenreSelectResult(std::move(query));
    } else {
        return GenreSelectResult();
    }
}

GenreSummarySelectResult GenreStorage::selectGenreSummaries() const {
    FwdSqlQuery query(m_database,
            mixxx::DbConnection::collateLexicographically(
                    QStringLiteral("SELECT * FROM %1 ORDER BY %2")
                            .arg(GENRE_SUMMARY_VIEW, GENRETABLE_NAME)));
    if (query.execPrepared()) {
        return GenreSummarySelectResult(std::move(query));
    } else {
        return GenreSummarySelectResult();
    }
}

bool GenreStorage::readGenreSummaryById(
        GenreId id, GenreSummary* pGenreSummary) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM %1 WHERE %2=:id")
                    .arg(GENRE_SUMMARY_VIEW, GENRETABLE_ID));
    query.bindValue(":id", id);
    if (query.execPrepared()) {
        GenreSummarySelectResult genreSummaries(std::move(query));
        if ((pGenreSummary != nullptr)
                        ? genreSummaries.populateNext(pGenreSummary)
                        : genreSummaries.next()) {
            VERIFY_OR_DEBUG_ASSERT(!genreSummaries.next()) {
                kLogger.warning() << "Ambiguous genre id:" << id;
            }
            return true;
        } else {
            kLogger.warning() << "Genre summary not found by id:" << id;
        }
    }
    return false;
}

uint GenreStorage::countGenreTracks(GenreId genreId) const {
    QStringList genreTags;
    genreTags << QString("##%1##").arg(genreId.toString());

    // find existing display_group for genreId
    QSqlQuery displayGroupQuery(m_database);
    displayGroupQuery.prepare("SELECT id FROM genres WHERE display_group = :group_id");
    displayGroupQuery.bindValue(":group_id", genreId.toVariant());

    if (!displayGroupQuery.exec()) {
        qWarning() << "[GenreStorage] Failed to load display group members: "
                   << displayGroupQuery.lastError().text();
    } else {
        while (displayGroupQuery.next()) {
            genreTags << QString("##%1##").arg(displayGroupQuery.value(0).toInt());
        }
    }

    // construct where-clause
    QStringList likeClauses;
    for (const QString& tag : genreTags) {
        likeClauses << QString("library.genre LIKE '%%1%'").arg(tag);
    }
    QString whereClause = likeClauses.join(" OR ");
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT COUNT(*) FROM %1 "
                           "WHERE (%2) AND %3=0")
                    .arg(LIBRARY_TABLE,
                            whereClause,
                            LIBRARYTABLE_MIXXXDELETED));

    if (query.execPrepared() && query.next()) {
        uint result = query.fieldValue(0).toUInt();
        DEBUG_ASSERT(!query.next());
        qDebug() << "[GenreStorage] -> countGenreTracks result: " << result;
        return result;
    } else {
        return 0;
    }
}

GenreTrackSelectResult GenreStorage::selectTracksSortedByGenreNameLike(
        const QString& genreNameLike) const {
    // TODO: Do SQL LIKE wildcards in genreNameLike need to be escaped?
    // Consider using SqlLikeWildcardEscaper if needed in the future.

    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT %1 FROM %2 "
                           "WHERE %3 LIKE :genreNameLike "
                           "ORDER BY %3")
                    .arg(LIBRARYTABLE_ID,
                            GENRE_TABLE,
                            GENRETABLE_NAME));

    query.bindValue(":genreNameLike",
            QVariant(kSqlLikeMatchAll + genreNameLike + kSqlLikeMatchAll));

    if (query.execPrepared()) {
        return GenreTrackSelectResult(std::move(query));
    } else {
        return GenreTrackSelectResult();
    }
}

QString GenreStorage::formatQueryForTrackIdsByGenreNameLike(
        const QString& genreNameLike) const {
    FieldEscaper escaper(m_database);
    QString escapedGenreNameLike = escaper.escapeString(
            kSqlLikeMatchAll + genreNameLike + kSqlLikeMatchAll);

    return QString(
            "SELECT DISTINCT %1 FROM %2 "
            "WHERE EXISTS ("
            " SELECT 1 FROM %3 "
            " WHERE %4 LIKE %5 "
            " AND %2.%6 LIKE '%%' || '##' || %3.%7 || '##' || '%%'"
            ") ORDER BY %1")
            .arg(LIBRARYTABLE_ID,
                    LIBRARY_TABLE,
                    GENRE_TABLE,
                    GENRETABLE_NAME,
                    escapedGenreNameLike,
                    LIBRARYTABLE_GENRE,
                    GENRETABLE_ID);
}

// QString GenreStorage::formatQueryForTrackIdsByGenreNameLike(
//         const QString& genreNameLike) const {
//     FieldEscaper escaper(m_database);
//     QString escapedGenreNameLike = escaper.escapeString(
//             kSqlLikeMatchAll + genreNameLike + kSqlLikeMatchAll);
//
//     return QString(
//             "SELECT DISTINCT %1 FROM %2 "
//             "JOIN %3 ON %4 LIKE '%%'||%5||'%%' "
//             "WHERE %6 LIKE %7 "
//             "ORDER BY %1")
//             .arg(LIBRARYTABLE_ID,
//                     LIBRARY_TABLE,
//                     GENRE_TABLE,
//                     LIBRARYTABLE_GENRE,
//                     QStringLiteral("##") + QString::number(/* genre id
//                     placeholder */) + QStringLiteral("##"), // This is a
//                     problem, see below. GENRETABLE_NAME,
//                     escapedGenreNameLike);
// }

QString GenreStorage::formatQueryForTrackIdsWithGenre() {
    // The library.genre column contains genre tags like ##id##;
    // We use LIKE '%##id##%' to find tracks that have that genre.
    return QStringLiteral(
            "SELECT DISTINCT %1 FROM %2 "
            "WHERE %3 LIKE '%' || '##' || %4 || '##' || '%' "
            "ORDER BY %1")
            .arg(LIBRARYTABLE_ID,
                    LIBRARY_TABLE,
                    LIBRARYTABLE_GENRE,
                    LIBRARYTABLE_ID); // The %4 placeholder will be bound to the genre id string
}

GenreTrackSelectResult GenreStorage::selectAllTracksSorted() const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT DISTINCT %1 FROM %2 "
                           "WHERE %3 LIKE '%%##%%' "
                           "ORDER BY %1")
                    .arg(LIBRARYTABLE_ID,
                            LIBRARY_TABLE,
                            LIBRARYTABLE_GENRE));
    if (query.execPrepared()) {
        return GenreTrackSelectResult(std::move(query));
    } else {
        return GenreTrackSelectResult();
    }
}

GenreTrackSelectResult GenreStorage::selectGenreTracksSorted(
        GenreId genreId) const {
    QStringList genreTags;
    genreTags << QString("##%1##").arg(genreId.toString());

    // Fetch genres in the same display_group
    QSqlQuery displayGroupQuery(m_database);
    displayGroupQuery.prepare("SELECT id FROM genres WHERE display_group = :group_id");
    displayGroupQuery.bindValue(":group_id", genreId.toVariant());

    if (!displayGroupQuery.exec()) {
        qWarning() << "[GenreStorage] Failed to load display group members: "
                   << displayGroupQuery.lastError().text();
    } else {
        while (displayGroupQuery.next()) {
            genreTags << QString("##%1##").arg(displayGroupQuery.value(0).toInt());
        }
    }

    // Construct WHERE clause
    QStringList likeClauses;
    for (int i = 0; i < genreTags.size(); ++i) {
        likeClauses << QString("genre LIKE :like%1").arg(i);
    }

    QString queryString =
            QString("SELECT * FROM library WHERE (%1) AND mixxx_deleted = 0 "
                    "ORDER BY id")
                    .arg(likeClauses.join(" OR "));
    FwdSqlQuery query(m_database, queryString);

    for (int i = 0; i < genreTags.size(); ++i) {
        const QString paramName = QString(":like%1").arg(i);
        const QString pattern = "%" + genreTags[i] + "%";
        query.bindValue(paramName, QVariant(pattern));
    }

    if (query.execPrepared()) {
        return GenreTrackSelectResult(std::move(query));
    } else {
        return GenreTrackSelectResult();
    }
}

GenreTrackSelectResult GenreStorage::makeEmptyGenreResult(TrackId trackId) const {
    FwdSqlQuery emptyQuery(m_database,
            QStringLiteral("SELECT :trackId AS track_id, CAST(NULL AS INTEGER) "
                           "AS genre_id WHERE 0=1"));
    emptyQuery.bindValue(":trackId", trackId);
    emptyQuery.execPrepared();
    return GenreTrackSelectResult(std::move(emptyQuery));
}

GenreTrackSelectResult GenreStorage::selectTrackGenresSorted(TrackId trackId) const {
    FwdSqlQuery genreFieldQuery(m_database,
            QStringLiteral("SELECT %1 FROM %2 WHERE %3 = :trackId")
                    .arg(LIBRARYTABLE_GENRE, LIBRARY_TABLE, LIBRARYTABLE_ID));
    genreFieldQuery.bindValue(":trackId", trackId);

    if (!genreFieldQuery.execPrepared() || !genreFieldQuery.next()) {
        return makeEmptyGenreResult(trackId);
    }

    const QVariant genreValue = genreFieldQuery.fieldValue(0);
    // if (!genreValue.isValid() || genreValue.isNull()) {
    //     makeEmptyGenreResult(trackId);
    // }

    const QString genreField = genreValue.toString();
    // if (genreField.trimmed().isEmpty()) {
    //     makeEmptyGenreResult(trackId);
    // }

    QRegularExpression re("##(\\d+)##");
    QRegularExpressionMatchIterator i = re.globalMatch(genreField);

    QStringList idStrings;
    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        idStrings << match.captured(1);
    }

    if (idStrings.isEmpty()) {
        return makeEmptyGenreResult(trackId);
    }

    QStringList placeholders;
    for (int i = 0; i < idStrings.size(); ++i) {
        placeholders << QString(":id%1").arg(i);
    }

    const QString inClause = placeholders.join(',');
    FwdSqlQuery idQuery(m_database,
            QStringLiteral("SELECT :trackId AS track_id, %1 AS genre_id "
                           "FROM %2 WHERE %1 IN (%4) OR %3 IN (%4) ORDER BY %1 ")
                    .arg(GENRETABLE_ID,
                            GENRE_TABLE,
                            GENRETABLE_DISPLAYGROUP,
                            inClause));

    idQuery.bindValue(":trackId", trackId);
    for (int i = 0; i < idStrings.size(); ++i) {
        idQuery.bindValue(QString(":id%1").arg(i), QVariant(idStrings.at(i).toInt()));
    }
    if (idQuery.execPrepared()) {
        return GenreTrackSelectResult(std::move(idQuery));
    } else {
        return makeEmptyGenreResult(trackId);
    }
}

GenreSummarySelectResult GenreStorage::selectGenresWithTrackCount(
        const QList<TrackId>& trackIds) const {
    const QString trackIdList = joinSqlStringList(trackIds);

    QString sql = QStringLiteral(
            "SELECT *, "
            "  (SELECT COUNT(*) FROM library "
            "   WHERE library.id IN (%1) "
            "     AND ("
            "       library.genre LIKE '%%##' || genres.id || '##%%' "
            "       OR EXISTS ("
            "         SELECT 1 FROM genres AS g2 "
            "         WHERE g2.display_group = genres.id "
            "           AND library.genre LIKE '%%##' || g2.id || '##%%'"
            "       )"
            "     )"
            "  ) AS %2, "
            "  0 AS %3 "
            "FROM %4 "
            "WHERE is_visible = 1 "
            "ORDER BY %5")
                          .arg(trackIdList,
                                  GENRESUMMARY_TRACK_COUNT,
                                  GENRESUMMARY_TRACK_DURATION,
                                  GENRE_TABLE,
                                  GENRETABLE_NAME);

    qDebug() << "[GenreStorage] -> selectGenresWithTrackCount sql: " << sql;

    FwdSqlQuery query(m_database, mixxx::DbConnection::collateLexicographically(sql));
    if (query.execPrepared()) {
        return GenreSummarySelectResult(std::move(query));
    } else {
        return GenreSummarySelectResult();
    }
}

QSet<GenreId> GenreStorage::collectGenreIdsOfTracks(const QList<TrackId>& trackIds) const {
    // NOTE(uklotzde): One query per track id. This could be optimized
    // by querying for chunks of track ids and collecting the results.
    QSet<GenreId> trackGenres;
    for (const auto& trackId : trackIds) {
        // NOTE(uklotzde): The query result does not need to be sorted by genre id
        // here. But since the corresponding FK column is indexed the impact on the
        // performance should be negligible. By reusing an existing query we reduce
        // the amount of code and the number of prepared SQL queries.
        GenreTrackSelectResult genreTracks(selectTrackGenresSorted(trackId));
        while (genreTracks.next()) {
            DEBUG_ASSERT(genreTracks.trackId() == trackId);
            trackGenres.insert(genreTracks.genreId());
        }
    }
    return trackGenres;
}

bool GenreStorage::onInsertingGenre(
        const Genre& genre,
        GenreId* pGenreId) {
    VERIFY_OR_DEBUG_ASSERT(!genre.getId().isValid()) {
        kLogger.warning()
                << "Cannot insert genre with a valid id:" << genre.getId();
        return false;
    }
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "INSERT INTO %1 (%2,%3,%4) "
                    "VALUES (:name,:locked,:autoDjSource)")
                    .arg(
                            GENRE_TABLE,
                            GENRETABLE_NAME,
                            GENRETABLE_LOCKED,
                            GENRETABLE_AUTODJ_SOURCE));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return false;
    }
    GenreQueryBinder queryBinder(query);
    queryBinder.bindName(":name", genre);
    queryBinder.bindLocked(":locked", genre);
    queryBinder.bindAutoDjSource(":autoDjSource", genre);
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return false;
    }
    if (query.numRowsAffected() > 0) {
        DEBUG_ASSERT(query.numRowsAffected() == 1);
        if (pGenreId != nullptr) {
            *pGenreId = GenreId(query.lastInsertId());
            DEBUG_ASSERT(pGenreId->isValid());
        }
        return true;
    } else {
        return false;
    }
}

bool GenreStorage::onUpdatingGenre(
        const Genre& genre) {
    VERIFY_OR_DEBUG_ASSERT(genre.getId().isValid()) {
        kLogger.warning()
                << "Cannot update genre without a valid id";
        return false;
    }
    FwdSqlQuery query(m_database,
            QString(
                    "UPDATE %1 "
                    "SET %2=:name,%3=:locked,%4=:autoDjSource "
                    "WHERE %5=:id")
                    .arg(
                            GENRE_TABLE,
                            GENRETABLE_NAME,
                            GENRETABLE_LOCKED,
                            GENRETABLE_AUTODJ_SOURCE,
                            GENRETABLE_ID));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return false;
    }
    GenreQueryBinder queryBinder(query);
    queryBinder.bindId(":id", genre);
    queryBinder.bindName(":name", genre);
    queryBinder.bindLocked(":locked", genre);
    queryBinder.bindAutoDjSource(":autoDjSource", genre);
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return false;
    }
    if (query.numRowsAffected() > 0) {
        VERIFY_OR_DEBUG_ASSERT(query.numRowsAffected() <= 1) {
            kLogger.warning()
                    << "Updated multiple genres with the same id" << genre.getId();
        }
        return true;
    } else {
        kLogger.warning()
                << "Cannot update non-existent genre with id" << genre.getId();
        return false;
    }
}

bool GenreStorage::onDeletingGenre(GenreId genreId) {
    VERIFY_OR_DEBUG_ASSERT(genreId.isValid()) {
        kLogger.warning() << "Cannot delete genre without a valid id";
        return false;
    }
    // ##id## from genre field
    const QString IdFormatted = QStringLiteral("##%1##").arg(genreId.toString());

    {
        FwdSqlQuery query(m_database,
                QStringLiteral(
                        "UPDATE %1 "
                        "SET %2 = TRIM(REPLACE(%2, :placeholder, '')) "
                        "WHERE %2 LIKE :likePlaceholder")
                        .arg(LIBRARY_TABLE, LIBRARYTABLE_GENRE));
        VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
            return false;
        }

        query.bindValue(":placeholder", IdFormatted);
        query.bindValue(":likePlaceholder", QStringLiteral("%%1%").arg(IdFormatted));

        qDebug() << "[GenreStorage] -> IdFormatted " << IdFormatted;

        VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
            return false;
        }

        if (kLogger.debugEnabled()) {
            kLogger.debug() << "Removed genreId placeholder" << IdFormatted
                            << "from genre field in" << query.numRowsAffected() << "track(s)";
        }
    }

    // ;; clean up
    {
        FwdSqlQuery cleanupQuery(m_database,
                QStringLiteral("UPDATE %1 "
                               "SET %2 = TRIM(REPLACE(REPLACE(REPLACE(%2, "
                               "';;', ';'), ';;', ';'), ';;', ';'), ';') "
                               "WHERE %2 LIKE '%%;%%'")
                        .arg(LIBRARY_TABLE, LIBRARYTABLE_GENRE));

        VERIFY_OR_DEBUG_ASSERT(cleanupQuery.isPrepared()) {
            return false;
        }

        VERIFY_OR_DEBUG_ASSERT(cleanupQuery.execPrepared()) {
            return false;
        }

        if (kLogger.debugEnabled()) {
            kLogger.debug() << "Cleaned up genre delimiters in"
                            << cleanupQuery.numRowsAffected() << "track(s)";
        }
    }

    // genre
    {
        FwdSqlQuery query(m_database,
                QStringLiteral("DELETE FROM %1 WHERE %2=:id")
                        .arg(GENRE_TABLE, GENRETABLE_ID));
        VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
            return false;
        }
        query.bindValue(":id", genreId);
        VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
            return false;
        }

        if (query.numRowsAffected() > 0) {
            VERIFY_OR_DEBUG_ASSERT(query.numRowsAffected() <= 1) {
                kLogger.warning()
                        << "Deleted multiple genres with the same id" << genreId;
            }
            return true;
        } else {
            kLogger.warning()
                    << "Cannot delete non-existent genre with id" << genreId;
            return false;
        }
    }
}

bool GenreStorage::onAddingGenreTracks(GenreId genreId, const QList<TrackId>& trackIds) {
    VERIFY_OR_DEBUG_ASSERT(genreId.isValid()) {
        kLogger.warning() << "Invalid genreId in onAddingGenreTracks";
        return false;
    }

    if (trackIds.isEmpty()) {
        return true;
    }

    const QString idFormatted = QStringLiteral("##%1##").arg(genreId.toVariant().toInt());

    // update library.genre
    QStringList trackIdStrings;
    for (const TrackId& trackId : trackIds) {
        trackIdStrings << trackId.toVariant().toString();
    }
    const QString inClause = trackIdStrings.join(',');

    FwdSqlQuery updateQuery(m_database,
            QStringLiteral(
                    "UPDATE %1 SET %2 = "
                    "CASE "
                    "WHEN %2 IS NULL OR %2 = '' THEN :idFormatted "
                    "WHEN %2 LIKE '%%' || :idFormatted || '%%' THEN %2 "
                    "ELSE %2 || '; ' || :idFormatted "
                    "END "
                    "WHERE %3 IN (%4)")
                    .arg(LIBRARY_TABLE, LIBRARYTABLE_GENRE, LIBRARYTABLE_ID, inClause));
    VERIFY_OR_DEBUG_ASSERT(updateQuery.isPrepared()) {
        return false;
    }

    updateQuery.bindValue(":idFormatted", idFormatted);

    if (!updateQuery.execPrepared()) {
        kLogger.warning() << "[GenreStorage] Failed to update library.genre with" << idFormatted;
        return false;
    }

    if (kLogger.debugEnabled()) {
        kLogger.debug() << "[GenreStorage] Added genreId" << genreId
                        << "to genre field in" << updateQuery.numRowsAffected() << "track(s)";
    }

    return true;
}

bool GenreStorage::onRemovingGenreTracks(
        GenreId genreId,
        const QList<TrackId>& trackIds) {
    VERIFY_OR_DEBUG_ASSERT(genreId.isValid()) {
        kLogger.warning() << "Cannot remove tracks without a valid genre ID";
        return false;
    }

    if (trackIds.isEmpty()) {
        return true;
    }

    // ##id## from genre field
    QStringList trackIdStrings;
    for (const TrackId& trackId : trackIds) {
        QVariant var = trackId.toVariant();

        if (var.isValid() &&
                (QMetaType::canConvert(var.metaType(), QMetaType::fromType<int>()) ||
                        QMetaType::canConvert(var.metaType(), QMetaType::fromType<qint64>()))) {
            trackIdStrings << var.toString();

        } else {
            kLogger.warning() << "Invalid TrackId while removing from genre:" << var;
        }
    }

    if (!trackIdStrings.isEmpty()) {
        const QString idPlaceholder = QStringLiteral("##%1##").arg(genreId.toString());
        const QString inClause = trackIdStrings.join(',');

        FwdSqlQuery updateQuery(m_database,
                QStringLiteral(
                        "UPDATE %1 "
                        "SET %2 = TRIM(REPLACE(%2, :placeholder, '')) "
                        "WHERE %3 IN (%4) AND %2 LIKE :likePlaceholder")
                        .arg(
                                LIBRARY_TABLE,
                                LIBRARYTABLE_GENRE,
                                LIBRARYTABLE_ID,
                                inClause));
        VERIFY_OR_DEBUG_ASSERT(updateQuery.isPrepared()) {
            return false;
        }

        updateQuery.bindValue(":placeholder", idPlaceholder);
        updateQuery.bindValue(":likePlaceholder", QString("%%%1%%").arg(idPlaceholder));

        if (!updateQuery.execPrepared()) {
            kLogger.warning() << "Failed to remove genreId placeholder"
                              << idPlaceholder << "from track genre fields";
            return false;
        }

        if (kLogger.debugEnabled()) {
            kLogger.debug() << "Removed genreId placeholder" << idPlaceholder
                            << "from genre field in" << updateQuery.numRowsAffected() << "track(s)";
        }
    }

    // clean ;;
    {
        FwdSqlQuery cleanupQuery(m_database,
                QStringLiteral(
                        "UPDATE %1 "
                        "SET %2 = TRIM("
                        "REPLACE(REPLACE(REPLACE(%2, ';;', ';'), ';;', ';'), ';;', ';'), ';') "
                        "WHERE %2 LIKE '%%;%%'")
                        .arg(LIBRARY_TABLE, LIBRARYTABLE_GENRE));

        VERIFY_OR_DEBUG_ASSERT(cleanupQuery.isPrepared()) {
            return false;
        }

        if (!cleanupQuery.execPrepared()) {
            kLogger.warning() << "Failed to clean up genre delimiters after genre removal";
            return false;
        }

        if (kLogger.debugEnabled()) {
            kLogger.debug() << "Cleaned up genre delimiters in"
                            << cleanupQuery.numRowsAffected() << "track(s)";
        }
    }

    return true;
}

// bool GenreStorage::onPurgingTracks(
//         const QList<TrackId>& trackIds) {
//     return true;
// }
