#include "library/dao/relationsdao.h"

#include <QSqlQuery>
#include <QtDebug>

#include "library/queryutil.h"
#include "util/assert.h"
#include "util/db/fwdsqlquery.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger = mixxx::Logger("RelationsDAO");

RelationsDao::Relation relationFromRow(const QSqlRecord& row) {
    return RelationsDao::Relation{
            DbId(row.value(row.indexOf("id"))),
            row.value(row.indexOf("rel_type")).toString(),
            row.value(row.indexOf("rel_description")).toString(),
            row.value(row.indexOf("rel_datetime_added")).toDateTime(),
    };
}

RelationsDao::RelationGroup relationGroupFromRow(const QSqlRecord& row) {
    return RelationsDao::RelationGroup{
            DbId(row.value(row.indexOf("id"))),
            DbId(row.value(row.indexOf("rg_rel_id"))),
            row.value(row.indexOf("rg_comment")).toString(),
            row.value(row.indexOf("rg_datetime_added")).toDateTime(),
    };
}

} // namespace

QList<RelationsDao::Relation> RelationsDao::allRelations() const {
    QList<Relation> relations;
    if (!m_database.isOpen()) {
        return relations;
    }

    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM " RELATIONS_TABLE " ORDER BY rel_type"));
    DEBUG_ASSERT(query.isPrepared() && !query.hasError());

    if (!query.execPrepared()) {
        kLogger.warning() << "Failed to load relations";
        DEBUG_ASSERT(!"failed query");
        return relations;
    }
    while (query.next()) {
        relations.append(relationFromRow(query.record()));
    }
    return relations;
}

RelationsDao::Relation RelationsDao::relationById(DbId id) const {
    if (!m_database.isOpen() || !id.isValid()) {
        return Relation{};
    }

    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT * FROM " RELATIONS_TABLE " WHERE id=:id"));
    DEBUG_ASSERT(query.isPrepared() && !query.hasError());
    query.bindValue(":id", id.toVariant());

    if (!query.execPrepared() || !query.next()) {
        return Relation{};
    }
    return relationFromRow(query.record());
}

DbId RelationsDao::addRelation(const QString& type, const QString& description) const {
    DEBUG_ASSERT(!type.isEmpty());
    if (!m_database.isOpen()) {
        return DbId();
    }

    // Defensive check (no UNIQUE index in schema rev 41)
    {
        FwdSqlQuery exists(m_database,
                QStringLiteral("SELECT id FROM " RELATIONS_TABLE
                               " WHERE rel_type=:type"));
        DEBUG_ASSERT(exists.isPrepared() && !exists.hasError());
        exists.bindValue(":type", type);
        if (exists.execPrepared() && exists.next()) {
            return DbId(exists.record().value(exists.record().indexOf("id")));
        }
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "INSERT INTO " RELATIONS_TABLE
            " (rel_type, rel_description, rel_datetime_added) "
            "VALUES (:type, :description, :datetime_added)"));
    query.bindValue(":type", type);
    query.bindValue(":description", description);
    query.bindValue(":datetime_added", QDateTime::currentDateTimeUtc());

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return DbId();
    }
    const DbId newId(query.lastInsertId());
    DEBUG_ASSERT(newId.isValid());
    return newId;
}

bool RelationsDao::removeRelation(DbId id) const {
    if (!m_database.isOpen() || !id.isValid()) {
        return false;
    }

    {
        FwdSqlQuery check(m_database,
                QStringLiteral(
                        "SELECT COUNT(*) AS cnt FROM " RELATION_GROUPS_TABLE
                        " WHERE rg_rel_id=:id"));
        DEBUG_ASSERT(check.isPrepared() && !check.hasError());
        check.bindValue(":id", id.toVariant());
        if (check.execPrepared() && check.next()) {
            const QSqlRecord record = check.record();
            if (record.value(record.indexOf("cnt")).toInt() > 0) {
                kLogger.warning()
                        << "removeRelation: groups still reference relation"
                        << id.toString();
                return false;
            }
        }
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM " RELATIONS_TABLE " WHERE id=:id"));
    query.bindValue(":id", id.toVariant());

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    return true;
}

DbId RelationsDao::createGroup(DbId relationId,
        const QList<TrackId>& tracks,
        const QString& comment) const {
    if (!m_database.isOpen() || tracks.size() < 2) {
        kLogger.warning() << "createGroup: need at least 2 tracks and an open DB";
        return DbId();
    }

    QList<TrackId> unique = tracks;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    if (unique.size() < 2) {
        return DbId();
    }

    QSqlQuery groupQuery(m_database);
    groupQuery.prepare(QStringLiteral(
            "INSERT INTO " RELATION_GROUPS_TABLE
            " (rg_rel_id, rg_comment, rg_datetime_added) "
            "VALUES (:rg_rel_id, :rg_comment, :rg_datetime_added)"));
    groupQuery.bindValue(":rg_rel_id", relationId.toVariant());
    groupQuery.bindValue(":rg_comment", comment);
    groupQuery.bindValue(":rg_datetime_added", QDateTime::currentDateTimeUtc());

    if (!groupQuery.exec()) {
        LOG_FAILED_QUERY(groupQuery);
        return DbId();
    }
    const DbId groupId(groupQuery.lastInsertId());
    DEBUG_ASSERT(groupId.isValid());

    QSqlQuery trackQuery(m_database);
    trackQuery.prepare(QStringLiteral(
            "INSERT INTO " RELATION_GROUP_TRACKS_TABLE
            " "
            "(rgt_group_id, rgt_track_id, rgt_comment, rgt_datetime_added) "
            "VALUES (:rgt_group_id, :rgt_track_id, :rgt_comment, :rgt_datetime_added)"));
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (TrackId trackId : unique) {
        trackQuery.bindValue(":rgt_group_id", groupId.toVariant());
        trackQuery.bindValue(":rgt_track_id", trackId.toVariant());
        trackQuery.bindValue(":rgt_comment", QVariant());
        trackQuery.bindValue(":rgt_datetime_added", now);

        if (!trackQuery.exec()) {
            LOG_FAILED_QUERY(trackQuery);
            removeGroup(groupId); // Cleanup partial group
            return DbId();
        }
    }
    return groupId;
}

QList<RelationsDao::RelationGroup> RelationsDao::groupsForTrack(TrackId trackId) const {
    QList<RelationGroup> groups;
    if (!m_database.isOpen() || !trackId.isValid()) {
        return groups;
    }

    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT g.* FROM " RELATION_GROUPS_TABLE " g "
                           "JOIN " RELATION_GROUP_TRACKS_TABLE
                           " gt ON gt.rgt_group_id = g.id "
                           "WHERE gt.rgt_track_id=:track_id "
                           "ORDER BY g.id"));
    DEBUG_ASSERT(query.isPrepared() && !query.hasError());
    query.bindValue(":track_id", trackId.toVariant());

    if (!query.execPrepared()) {
        kLogger.warning() << "Failed to load groups for track" << trackId;
        return groups;
    }
    while (query.next()) {
        groups.append(relationGroupFromRow(query.record()));
    }
    return groups;
}

bool RelationsDao::removeGroup(DbId groupId) const {
    if (!m_database.isOpen() || !groupId.isValid()) {
        return false;
    }

    {
        QSqlQuery delTracks(m_database);
        delTracks.prepare(
                QStringLiteral("DELETE FROM " RELATION_GROUP_TRACKS_TABLE
                               " WHERE rgt_group_id=:id"));
        delTracks.bindValue(":id", groupId.toVariant());
        if (!delTracks.exec()) {
            LOG_FAILED_QUERY(delTracks);
            return false;
        }
    }

    QSqlQuery delGroup(m_database);
    delGroup.prepare(QStringLiteral("DELETE FROM " RELATION_GROUPS_TABLE " WHERE id=:id"));
    delGroup.bindValue(":id", groupId.toVariant());

    if (!delGroup.exec()) {
        LOG_FAILED_QUERY(delGroup);
        return false;
    }
    return true;
}

bool RelationsDao::updateGroupComment(DbId groupId, const QString& comment) const {
    if (!m_database.isOpen() || !groupId.isValid()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE " RELATION_GROUPS_TABLE
                                 " SET rg_comment=:comment WHERE id=:id"));
    query.bindValue(":comment", comment);
    query.bindValue(":id", groupId.toVariant());

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    return true;
}

QList<TrackId> RelationsDao::tracksInGroup(DbId groupId) const {
    QList<TrackId> tracks;
    if (!m_database.isOpen() || !groupId.isValid()) {
        return tracks;
    }

    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "SELECT rgt_track_id FROM " RELATION_GROUP_TRACKS_TABLE " "
                    "WHERE rgt_group_id=:id ORDER BY id"));
    DEBUG_ASSERT(query.isPrepared() && !query.hasError());
    query.bindValue(":id", groupId.toVariant());

    if (!query.execPrepared()) {
        return tracks;
    }
    const QSqlRecord record = query.record();
    const int idxTrackId = record.indexOf("rgt_track_id");

    while (query.next()) {
        tracks.append(TrackId(record.value(idxTrackId)));
    }
    return tracks;
}

bool RelationsDao::setTrackComment(DbId groupId, TrackId trackId, const QString& comment) const {
    if (!m_database.isOpen() || !groupId.isValid() || !trackId.isValid()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "UPDATE " RELATION_GROUP_TRACKS_TABLE
            " SET rgt_comment=:comment "
            "WHERE rgt_group_id=:group_id AND rgt_track_id=:track_id"));
    query.bindValue(":comment", comment);
    query.bindValue(":group_id", groupId.toVariant());
    query.bindValue(":track_id", trackId.toVariant());

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    return true;
}

bool RelationsDao::removeTrackFromGroup(DbId groupId, TrackId trackId) const {
    if (!m_database.isOpen() || !groupId.isValid() || !trackId.isValid()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "DELETE FROM " RELATION_GROUP_TRACKS_TABLE
            " "
            "WHERE rgt_group_id=:group_id AND rgt_track_id=:track_id"));
    query.bindValue(":group_id", groupId.toVariant());
    query.bindValue(":track_id", trackId.toVariant());

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    return true;
}

QList<RelationsDao::TrackRelationRow> RelationsDao::relationsForTrack(TrackId trackId) const {
    QList<TrackRelationRow> rows;
    if (!m_database.isOpen() || !trackId.isValid()) {
        return rows;
    }

    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "SELECT "
                    "  relations.id AS relation_id,"
                    "  relations.rel_type AS rel_type,"
                    "  relations.rel_description AS rel_description,"
                    "  relation_groups.id AS group_id,"
                    "  relation_groups.rg_comment AS rg_comment,"
                    "  other_track.rgt_comment AS rgt_comment,"
                    "  other_track.rgt_track_id AS rgt_track_id,"
                    "  library.artist AS artist,"
                    "  library.title AS title "
                    "FROM " RELATION_GROUP_TRACKS_TABLE " AS self_track "
                    "JOIN " RELATION_GROUPS_TABLE " AS relation_groups "
                    "  ON relation_groups.id = self_track.rgt_group_id "
                    "JOIN " RELATIONS_TABLE " AS relations "
                    "  ON relations.id = relation_groups.rg_rel_id "
                    "JOIN " RELATION_GROUP_TRACKS_TABLE " AS other_track "
                    "  ON other_track.rgt_group_id = relation_groups.id "
                    " AND other_track.rgt_track_id <> self_track.rgt_track_id "
                    "JOIN library "
                    "  ON library.id = other_track.rgt_track_id "
                    "WHERE self_track.rgt_track_id=:track_id "
                    "ORDER BY relations.rel_type, relation_groups.id, "
                    "library.artist, library.title"));

    DEBUG_ASSERT(query.isPrepared() && !query.hasError());
    query.bindValue(":track_id", trackId.toVariant());

    if (!query.execPrepared()) {
        kLogger.warning() << "relationsForTrack failed for track" << trackId;
        return rows;
    }

    const QSqlRecord record = query.record();
    const int idxRelationId = record.indexOf("relation_id");
    const int idxRelType = record.indexOf("rel_type");
    const int idxRelDescription = record.indexOf("rel_description");
    const int idxGroupId = record.indexOf("group_id");
    const int idxGroupComment = record.indexOf("rg_comment");
    const int idxTrackComment = record.indexOf("rgt_comment");
    const int idxLinkedTrackId = record.indexOf("rgt_track_id");
    const int idxArtist = record.indexOf("artist");
    const int idxTitle = record.indexOf("title");

    while (query.next()) {
        rows.append(TrackRelationRow{
                DbId(record.value(idxRelationId)),
                record.value(idxRelType).toString(),
                record.value(idxRelDescription).toString(),
                DbId(record.value(idxGroupId)),
                record.value(idxGroupComment).toString(),
                record.value(idxTrackComment).toString(),
                TrackId(record.value(idxLinkedTrackId)),
                record.value(idxArtist).toString(),
                record.value(idxTitle).toString(),
        });
    }
    return rows;
}
