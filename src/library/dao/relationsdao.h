#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include "library/dao/dao.h"
#include "track/trackid.h"
#include "util/db/dbid.h"

#define RELATIONS_TABLE "relations"
#define RELATION_GROUPS_TABLE "relation_groups"
#define RELATION_GROUP_TRACKS_TABLE "relation_group_tracks"

class RelationsDao : public DAO {
  public:
    ~RelationsDao() override = default;

    struct Relation {
        DbId id;
        QString type;
        QString description;
        QDateTime dateAdded;
    };

    struct RelationGroup {
        DbId id;
        DbId relationId;
        QString comment;
        QDateTime dateAdded;
    };

    struct GroupTrack {
        TrackId trackId;
        QString comment;
    };

    struct TrackRelationRow {
        DbId relationId;
        QString relationType;
        QString relationDescription;
        DbId groupId;
        QString groupComment;
        QString groupTrackComment;
        TrackId linkedTrackId;
        QString artist;
        QString title;
        QString album;
        QString year;
    };

    QList<Relation> allRelations() const;
    Relation relationById(DbId id) const;
    DbId addRelation(const QString& type, const QString& description) const;
    bool removeRelation(DbId id) const;
    bool updateRelation(DbId id, const QString& type, const QString& description) const;
    // Moves all groups of sourceId to targetId and deletes sourceId
    bool mergeRelations(DbId sourceId, DbId targetId) const;
    // Deletes the relation type together with all its groups
    bool removeRelationWithGroups(DbId id) const;
    // Deletes all relation types that have no groups, returns how many
    int removeUnusedRelations() const;
    int groupCountForRelation(DbId id) const;

    // Groups
    DbId createGroup(DbId relationId,
            const QList<TrackId>& tracks,
            const QString& comment) const;
    QList<RelationGroup> groupsForTrack(TrackId trackId) const;
    bool removeGroup(DbId groupId) const;
    bool updateGroupComment(DbId groupId, const QString& comment) const;

    // MemberTracks
    QList<TrackId> tracksInGroup(DbId groupId) const;
    QList<GroupTrack> groupTracks(DbId groupId) const;
    bool setTrackComment(DbId groupId,
            TrackId trackId,
            const QString& comment) const;
    bool removeTrackFromGroup(DbId groupId, TrackId trackId) const;

    // Deck view query
    QList<TrackRelationRow> relationsForTrack(TrackId trackId) const;

    RelationGroup getGroupById(DbId groupId) const;
    QList<RelationGroup> allGroups() const;

    // Maintenance: removes relation data of library purged tracks, of
    // non-existent groups and relation types, and groups with fewer than 2 tracks
    void repairDatabase(const QSqlDatabase& database) const;
};
