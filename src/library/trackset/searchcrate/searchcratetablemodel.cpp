#include "library/trackset/searchcrate/searchcratetablemodel.h"

#include <QtDebug>

#include "library/dao/trackschema.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/searchcrate/searchcrate.h"
#include "library/trackset/searchcrate/searchcratefuntions.h"
#include "moc_searchcratetablemodel.cpp"
#include "track/track.h"
#include "util/db/fwdsqlquery.h"

namespace {
const bool sDebugSearchCrateTableModel = false;
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
    qDebug() << "SearchCrateTableModel::setSearchCrate()" << searchCrateId;
    if (searchCrateId == m_selectedSearchCrate) {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> Already focused on "
                        "searchCrate "
                     << searchCrateId;
        }
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

    const QString& checkStamp = QDateTime::currentDateTime().toString("hhmmss");
    const QString& tableName = QStringLiteral("searchcrate_%1")
                                       .arg(m_selectedSearchCrate.toString());
    const QString& tableNameOld =
            QStringLiteral("searchcrate_%1_%2")
                    .arg(m_selectedSearchCrate.toString(), checkStamp);
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
    bool getLocked;
    FwdSqlQuery queryGetLocked(m_database,
            QStringLiteral("SELECT locked from searchcrates where id=:searchcrateid"));
    queryGetLocked.bindValue(":searchcrateid", searchCrateId);
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> LOCKED ? -> get locked: queryGetLocked "
                 << "SELECT locked from searchcrates where id = "
                 << searchCrateId;
    }

    if (queryGetLocked.execPrepared() && queryGetLocked.next()) {
        getLocked = queryGetLocked.fieldValue(0).toBool();
    } else {
        getLocked = false;
    }
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> LOCKED ? -> locked: " << getLocked;
    }

    if (getLocked) {
        // read cache = tracks from searchcrate_tracks
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> LOCKED -> GET CACHED TRACKS ";
        }
        QString queryStringTempView =
                QStringLiteral(
                        "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
                        "SELECT %2 FROM %3 "
                        "WHERE %4 IN (%5) "
                        "AND %6=0")
                        .arg(tableName,            // 1
                                columns.join(","), // 2
                                LIBRARY_TABLE,     // 3
                                LIBRARYTABLE_ID,   // 4
                                SearchCrateStorage::formatSubselectQueryForSearchCrateTrackIds(
                                        searchCrateId),     // 5
                                LIBRARYTABLE_MIXXXDELETED); // 6
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> LOCKED -> GET CACHED TRACKS "
                        "queryStringTempView "
                     << queryStringTempView;
        }
        FwdSqlQuery(m_database, queryStringTempView).execPrepared();
    } else {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED";
        }
        // delete cache = delete tracks in searchcrate_tracks table witg selected id
        const QString& queryStringDeleteIDFromSearchCrateTracks =
                QStringLiteral(
                        "DELETE FROM searchcrate_tracks "
                        "WHERE searchcrate_id = %1")
                        .arg(searchCrateId.toVariant().toString());

        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED -> DELETE CACHED TRACKS "
                        "queryStringDeleteIDFromSearchCrateTracks "
                     << queryStringDeleteIDFromSearchCrateTracks;
        }
        FwdSqlQuery(m_database, queryStringDeleteIDFromSearchCrateTracks).execPrepared();

        // Create SQl based on conditions in searchCrate
        QVariantList searchCrateData;
        searchCrateData.clear();
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED -> "
                        "CONSTRUCT SQL -> selectSearchCrate2QVL ";
        }
        selectSearchCrate2QVL(searchCrateId, searchCrateData); // Fetch searchCrate data
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED -> "
                        "CONSTRUCT SQL -> whereClause ";
        }
        QString whereClause = buildWhereClause(searchCrateData); // Get the WHERE clause
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED -> "
                        "CONSTRUCT SQL -> whereClause "
                        "generated:"
                     << whereClause;
        }
        // create cache = put tracks in searchcrate_tracks table witg selected id
        const QString& queryStringIDtoSearchCrateTracks =
                QStringLiteral(
                        "INSERT OR IGNORE INTO searchcrate_tracks (searchcrate_id, "
                        "track_id) "
                        "SELECT %1, library.id FROM library WHERE %2")
                        .arg(searchCrateId.toVariant().toString(), whereClause);

        FwdSqlQuery(m_database, queryStringIDtoSearchCrateTracks).execPrepared();
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED -> CREATE "
                        "CACHE -> Create temp "
                        "view queryStringIDtoSearchCrateTracks "
                     << queryStringIDtoSearchCrateTracks;
        }

        const QString& queryStringDropView = QStringLiteral("Alter table rename %1 to %2 ")
                                                     .arg(tableName, tableNameOld);
        FwdSqlQuery(m_database, queryStringIDtoSearchCrateTracks).execPrepared();
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [NOT LOCKED] [CREATE CACHE] -> Drop view ";
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED -> CREATE "
                        "CACHE -> Rename TEMP table"
                        "queryStringDropView "
                     << queryStringDropView;
        }

        const QString& queryStringTempView =
                QStringLiteral(
                        "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
                        "SELECT %2 FROM %3 "
                        "WHERE %4")
                        .arg(tableName,            // 1
                                columns.join(","), // 2
                                LIBRARY_TABLE,     // 3
                                whereClause);      // 4

        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> NOT LOCKED -> CREATE "
                        "CACHE -> CREATE TEMP VIEW "
                        "queryStringTempView "
                     << queryStringTempView;
        }
        FwdSqlQuery(m_database, queryStringTempView).execPrepared();
    }

    columns[0] = LIBRARYTABLE_ID;
    columns[1] = LIBRARYTABLE_PREVIEW;
    columns[2] = LIBRARYTABLE_COVERART;
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT] -> LOCKED / NOT LOCKED "
                    "-> LOAD TRACKS IN TABLE";
    }

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

void SearchCrateTableModel::selectPlaylistsCrates2QVL(QVariantList& playlistsCratesData) {
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT PLAYLISTS CRATES 2 QVL] -> Start";
    }
    playlistsCratesData.clear();

    // Playlists
    QSqlQuery playlistQuery(m_database);
    playlistQuery.prepare(
            "SELECT id, name FROM Playlists WHERE hidden=0 ORDER BY name ASC, "
            "id ASC");
    if (playlistQuery.exec()) {
        while (playlistQuery.next()) {
            QVariantMap playlistEntry;
            playlistEntry["type"] = "playlist";
            playlistEntry["id"] = playlistQuery.value("id");
            playlistEntry["name"] = playlistQuery.value("name");
            playlistsCratesData.append(playlistEntry);
        }
    } else {
        qWarning() << "[SEARCHCRATESTABLEMODEL] [SELECT PLAYLISTS CRATES 2 QVL] -> "
                      "Playlists Failed:"
                   << playlistQuery.lastError();
    }

    // Playlists - history
    QSqlQuery historyQuery(m_database);
    historyQuery.prepare(
            "SELECT id, name FROM Playlists WHERE hidden=2 ORDER BY name DESC, "
            "id ASC");
    if (historyQuery.exec()) {
        while (historyQuery.next()) {
            QVariantMap historyEntry;
            historyEntry["type"] = "history";
            historyEntry["id"] = historyQuery.value("id");
            historyEntry["name"] = historyQuery.value("name");
            playlistsCratesData.append(historyEntry);
        }
    } else {
        qWarning() << "[SEARCHCRATESTABLEMODEL] [SELECT PLAYLISTS CRATES 2 QVL] -> "
                      "History Failed:"
                   << historyQuery.lastError();
    }

    // Crates
    QSqlQuery crateQuery(m_database);
    crateQuery.prepare("SELECT id, name FROM crates WHERE show=1 ORDER BY name ASC, id ASC");
    if (crateQuery.exec()) {
        while (crateQuery.next()) {
            QVariantMap crateEntry;
            crateEntry["type"] = "crate";
            crateEntry["id"] = crateQuery.value("id");
            crateEntry["name"] = crateQuery.value("name");
            playlistsCratesData.append(crateEntry);
        }
    } else {
        qWarning() << "[SEARCHCRATESTABLEMODEL] [SELECT PLAYLISTS CRATES 2 QVL] -> "
                      "Crates Failed:"
                   << crateQuery.lastError();
    }

    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECT PLAYLISTS CRATES 2 QVL] -> "
                    "Completed:"
                 << playlistsCratesData;
    }
}

void SearchCrateTableModel::selectSearchCrate2QVL(
        SearchCrateId searchCrateId, QVariantList& searchCrateData) {
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECTSEARCHCRATES2QVL] -> start with "
                    "SearchCrateId:"
                 << searchCrateId;
    }

    // Assuming m_database is properly connected
    QSqlQuery* query = new QSqlQuery(m_database);
    query->prepare("SELECT * FROM searchcrates WHERE id = :id");
    query->addBindValue(searchCrateId.toVariant());

    if (query->exec()) {
        if (query->next()) {
            searchCrateData.clear(); // Clear any existing data before appending

            // Populate searchCrateData with the fields from the database row
            searchCrateData.append(query->value("id").toString());           // id
            searchCrateData.append(query->value("name").toString());         // name
            searchCrateData.append(query->value("count").toInt());           // count
            searchCrateData.append(query->value("show").toBool());           // show
            searchCrateData.append(query->value("locked").toBool());         // locked
            searchCrateData.append(query->value("autodj_source").toBool());  // autoDJ
            searchCrateData.append(query->value("search_input").toString()); // search_input
            searchCrateData.append(query->value("search_sql").toString());   // search_sql

            for (int i = 1; i <= 12; ++i) { // Handle conditions
                searchCrateData.append(
                        query->value(QStringLiteral("condition%1_field").arg(i))
                                .toString());
                searchCrateData.append(
                        query->value(QStringLiteral("condition%1_operator").arg(i))
                                .toString());
                searchCrateData.append(
                        query->value(QStringLiteral("condition%1_value").arg(i))
                                .toString());
                searchCrateData.append(
                        query->value(QStringLiteral("condition%1_combiner").arg(i))
                                .toString());
            }

            if (sDebugSearchCrateTableModel) {
                qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECTSEARCHCRATES2QVL] -> loaded data into "
                            "QVariantList:"
                         << searchCrateData;
            }
            // Retrieve previous and next record IDs + BOF & EOF
            QVariant previousId = getPreviousRecordId(searchCrateId);
            // If no previous ID, at beginning
            bool isBOF = (previousId.isNull());
            QVariant nextId = getNextRecordId(searchCrateId);
            // If no next ID, at end
            bool isEOF = (nextId.isNull());
            // Appending previous & next ID a BOF & EOF
            searchCrateData.append(previousId);
            searchCrateData.append(isBOF);
            searchCrateData.append(nextId);
            searchCrateData.append(isEOF);
            if (sDebugSearchCrateTableModel) {
                qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECTSEARCHCRATES2QVL] -> data found for "
                            "SearchCrateId:"
                         << searchCrateId
                         << "Data in searchCrate:"
                         << searchCrateData;
            }
        } else {
            if (sDebugSearchCrateTableModel) {
                qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECTSEARCHCRATES2QVL] "
                            "-> No data found for "
                            "SearchCrateId:"
                         << searchCrateId;
            }
        }
    } else {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SELECTSEARCHCRATES2QVL] -> "
                        "Failed to execute query -"
                     << query->lastError();
        }
    }
    delete query;
}

QVariant SearchCrateTableModel::getPreviousRecordId(SearchCrateId currentId) {
    QSqlQuery query(m_database);
    query.prepare("SELECT id FROM searchcrates WHERE id < :id ORDER BY id DESC LIMIT 1");
    query.bindValue(":id", currentId.toVariant());
    if (query.exec() && query.next()) {
        return query.value("id");
    }
    return {}; // Return a null QVariant if no previous ID
}

QVariant SearchCrateTableModel::getNextRecordId(SearchCrateId currentId) {
    QSqlQuery query(m_database);
    query.prepare("SELECT id FROM searchcrates WHERE id > :id ORDER BY id ASC LIMIT 1");
    query.bindValue(":id", currentId.toVariant());
    if (query.exec() && query.next()) {
        return query.value("id");
    }
    return {}; // Return a null QVariant if no next ID
}

void SearchCrateTableModel::saveQVL2SearchCrate(
        SearchCrateId searchCrateId, const QVariantList& searchCrateData) {
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> starts "
                    "for ID:"
                 << searchCrateId;
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> UPDATE SQL WhereClause "
                 << buildWhereClause(searchCrateData).replace("'", "");
    }
    QString whereClause2Save = buildWhereClause(searchCrateData).replace("'", "");
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> UPDATE SQL WhereClause "
                 << whereClause2Save;
    }
    // Core update for basic fields
    const QString& baseInfoUpdate =
            QStringLiteral(
                    "UPDATE searchcrates SET name = '%1', count = %2, show = %3, "
                    "locked = %4, autodj_source = %5, "
                    "search_input = '%6', search_sql = '%7' WHERE id = %8")
                    .arg(searchCrateData[1].toString(),
                            searchCrateData[2].toString(), // 1
                            searchCrateData[3].toString(),
                            searchCrateData[4].toString(),
                            searchCrateData[5].toString(),
                            searchCrateData[6].toString(), // 5
                            whereClause2Save,
                            searchCrateData[0].toString()); // 7

    if (!FwdSqlQuery(m_database, baseInfoUpdate).execPrepared()) {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> "
                        "baseInfo Update failed:";
        }
        return;
    }

    // Update for condition1 through condition3 fields
    const QString& conditionUpdate1 = buildConditionUpdateQuery(searchCrateData, 8, 19);
    if (!FwdSqlQuery(m_database, conditionUpdate1).execPrepared()) {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> "
                        "Condition Update 1 failed:";
        }
        return;
    }

    // Update for condition4 through condition6 fields
    const QString& conditionUpdate2 = buildConditionUpdateQuery(searchCrateData, 20, 31);
    if (!FwdSqlQuery(m_database, conditionUpdate2).execPrepared()) {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> "
                        "Condition Update 2 failed:";
        }
        return;
    }

    // Update for condition7 through condition9 fields
    const QString& conditionUpdate3 = buildConditionUpdateQuery(searchCrateData, 32, 43);
    if (!FwdSqlQuery(m_database, conditionUpdate3).execPrepared()) {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> "
                        "Condition Update 3 failed:";
        }
        return;
    }

    // Update for condition10 through condition12 fields
    const QString& conditionUpdate4 = buildConditionUpdateQuery(searchCrateData, 44, 55);
    if (!FwdSqlQuery(m_database, conditionUpdate4).execPrepared()) {
        if (sDebugSearchCrateTableModel) {
            qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> "
                        "Condition Update 4 failed:";
        }
        return;
    }
    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [SAVEQVL2SEARCHCRATES] -> "
                    "completed for ID:"
                 << searchCrateId;
    }
}

QString SearchCrateTableModel::buildConditionUpdateQuery(
        const QVariantList& searchCrateData, int startIdx, int endIdx) {
    QString queryStr = "UPDATE searchcrates SET ";
    QStringList fieldNames = {"condition1_field",
            "condition1_operator",
            "condition1_value",
            "condition1_combiner",
            "condition2_field",
            "condition2_operator",
            "condition2_value",
            "condition2_combiner",
            "condition3_field",
            "condition3_operator",
            "condition3_value",
            "condition3_combiner",
            "condition4_field",
            "condition4_operator",
            "condition4_value",
            "condition4_combiner",
            "condition5_field",
            "condition5_operator",
            "condition5_value",
            "condition5_combiner",
            "condition6_field",
            "condition6_operator",
            "condition6_value",
            "condition6_combiner",
            "condition7_field",
            "condition7_operator",
            "condition7_value",
            "condition7_combiner",
            "condition8_field",
            "condition8_operator",
            "condition8_value",
            "condition8_combiner",
            "condition9_field",
            "condition9_operator",
            "condition9_value",
            "condition9_combiner",
            "condition10_field",
            "condition10_operator",
            "condition10_value",
            "condition10_combiner",
            "condition11_field",
            "condition11_operator",
            "condition11_value",
            "condition11_combiner",
            "condition12_field",
            "condition12_operator",
            "condition12_value",
            "condition12_combiner"};

    for (int i = startIdx; i <= endIdx; ++i) {
        queryStr += fieldNames[i - 8] + " = '" + searchCrateData[i].toString() + "'";
        if (i < endIdx) {
            queryStr += ", ";
        }
    }

    queryStr += " WHERE id = " + searchCrateData[0].toString();
    return queryStr;
}

QString SearchCrateTableModel::buildWhereClause(const QVariantList& searchCrateData) {
    qDebug() << "searchCrateData size:" << searchCrateData.size();
    QString whereClause = "(";
    bool hasConditions = false;

    QStringList combinerOptions = {") END", "AND", "OR", ") AND (", ") OR ("};
    // Assuming searchValue is at index 6 (search_input)
    // const QString& searchValue = searchCrateData[6].isNull() ? "" :
    // searchCrateData[6].toString();
    const QString& searchValue = searchCrateData[6].toString();

    for (int i = 1; i <= 12; ++i) {
        int baseIndex = 8 + (i - 1) * 4; // Adjusting for the correct index in searchCrateData

        const QString& field = searchCrateData[baseIndex].toString();
        const QString& op = searchCrateData[baseIndex + 1].toString();
        const QString& value = searchCrateData[baseIndex + 2].toString();
        // QString combiner = searchCrateData[baseIndex + 3].toString();

        //  begin build condition
        //  function moved to searchCratefunctions.h to share it with
        //  dlgsearchCrateinfo to create preview
        const QString& condition = buildCondition(field, op, value);

        //  end build condition
        if (condition != "") {
            hasConditions = true;
            whereClause += condition;
            if (i < 12 && combinerOptions.contains(searchCrateData[baseIndex + 3].toString())) {
                if (searchCrateData[baseIndex + 3].toString() == ") END") {
                    whereClause += ")";
                } else if ((searchCrateData[baseIndex + 3].toString() == "AND") ||
                        (searchCrateData[baseIndex + 3].toString() == "OR")) {
                    whereClause += " " + searchCrateData[baseIndex + 3].toString() + " ";
                } else {
                    whereClause += searchCrateData[baseIndex + 3].toString() + " ";
                }
            }
        }
    }

    if (!hasConditions) {
        whereClause += QStringLiteral(
                "library.artist LIKE '%%1%' OR "
                "library.title LIKE '%%1%' OR "
                "library.album LIKE '%%1%' OR "
                "library.album_artist LIKE '%%1%' OR "
                "library.composer LIKE '%%1%' OR "
                "library.genre LIKE '%%1%' OR "
                "library.comment LIKE '%%1%')")
                               .arg(searchValue);
    }

    //    whereClause += ")";

    if (sDebugSearchCrateTableModel) {
        qDebug() << "[SEARCHCRATESTABLEMODEL] [GETWHERECLAUSEFORSEARCHCRATES] "
                    "[CONSTRUCT SQL] -> Constructed WHERE clause:"
                 << whereClause;
    }
    return whereClause;
}
