#include "library/relations/relationautogenerator.h"

#include <QHash>
#include <QSet>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <utility>

#include "library/dao/trackschema.h"
#include "library/queryutil.h"
#include "util/assert.h"
#include "util/db/fwdsqlquery.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("RelationAutoGenerator");

// The names of the columns of a table (empty when the table does not exist)
QSet<QString> tableColumns(const QSqlDatabase& database, const QString& table) {
    QSet<QString> columns;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        LOG_FAILED_QUERY(query);
        return columns;
    }
    while (query.next()) {
        // Columns of table_info: cid, name, type, notnull, dflt_value, pk
        columns.insert(query.value(1).toString());
    }
    return columns;
}

struct Candidate {
    QString key;
    QString groupComment;
    QString trackComment;
    TrackId trackId;
};

// A rule for tracks that have the same value in a column of the library table:
// the group comment is a column of the first track, the track comment is the
// shared value
RelationAutoGenerator::Rule libraryRule(const QString& relationType,
        const QString& description,
        const QString& keyColumn,
        const QString& groupCommentColumn) {
    RelationAutoGenerator::Rule rule;
    rule.relationType = relationType;
    rule.description = description;
    rule.table = QStringLiteral(LIBRARY_TABLE);
    rule.trackIdColumn = QStringLiteral("id");
    rule.keyColumn = keyColumn;
    rule.groupCommentExpression = QStringLiteral("l.") + groupCommentColumn;
    rule.trackCommentExpression = QStringLiteral("t.") + keyColumn;
    rule.identifyByGroupComment = false;
    rule.requiredColumns = {
            QStringLiteral(LIBRARY_TABLE ".") + keyColumn,
            QStringLiteral(LIBRARY_TABLE ".") + groupCommentColumn};
    return rule;
}

// The value of a track in the form in which values are compared with each other
QString normalizeKey(const RelationAutoGenerator::Rule& rule, const QString& value) {
    switch (rule.match) {
    case RelationAutoGenerator::Rule::Match::Text:
        return value.simplified().toCaseFolded();
    case RelationAutoGenerator::Rule::Match::Number: {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok) {
            return QString();
        }
        return QString::number(number, 'f', rule.matchDecimals);
    }
    case RelationAutoGenerator::Rule::Match::Exact:
        break;
    }
    return value;
}

RelationAutoGenerator::RuleResult applyRule(const QSqlDatabase& database,
        RelationsDao& relationsDao,
        const RelationAutoGenerator::Rule& rule) {
    RelationAutoGenerator::RuleResult result;
    result.relationType = rule.relationType;

    // The relation type of the rule. It is created when the first group is needed.
    DbId relationId;
    {
        const QList<RelationsDao::Relation> relations = relationsDao.allRelations();
        for (const RelationsDao::Relation& relation : relations) {
            if (relation.type.compare(rule.relationType, Qt::CaseInsensitive) == 0) {
                relationId = relation.id;
                break;
            }
        }
    }

    // The groups that already exist for this relation type, recognized by the
    // group comment or by the track comments
    QHash<QString, QVariant> groupIdByIdentity;
    QHash<QString, QSet<TrackId>> trackIdsByGroup;
    if (relationId.isValid()) {
        {
            const QString identitySql = rule.identifyByGroupComment
                    ? QStringLiteral(
                              "SELECT g.rg_comment AS k, g.id AS group_id "
                              "FROM " RELATION_GROUPS_TABLE
                              " g WHERE g.rg_rel_id=:rel_id AND g.rg_comment "
                              "IS NOT NULL "
                              "AND g.rg_comment <> ''")
                    : QStringLiteral(
                              "SELECT gt.rgt_comment AS k, g.id AS group_id "
                              "FROM " RELATION_GROUPS_TABLE
                              " g JOIN " RELATION_GROUP_TRACKS_TABLE
                              " gt ON gt.rgt_group_id = g.id "
                              "WHERE g.rg_rel_id=:rel_id AND gt.rgt_comment IS "
                              "NOT NULL "
                              "AND gt.rgt_comment <> ''");
            FwdSqlQuery query(database, identitySql);
            DEBUG_ASSERT(query.isPrepared() && !query.hasError());
            query.bindValue(":rel_id", relationId.toVariant());
            if (query.execPrepared()) {
                while (query.next()) {
                    const QSqlRecord record = query.record();
                    groupIdByIdentity.insert(
                            normalizeKey(rule,
                                    record.value(QStringLiteral("k"))
                                            .toString()),
                            record.value(QStringLiteral("group_id")));
                }
            }
        }
        {
            FwdSqlQuery query(database,
                    QStringLiteral("SELECT rgt_group_id, rgt_track_id "
                                   "FROM " RELATION_GROUP_TRACKS_TABLE
                                   " WHERE rgt_group_id IN (SELECT id "
                                   "FROM " RELATION_GROUPS_TABLE
                                   " WHERE rg_rel_id=:rel_id)"));
            DEBUG_ASSERT(query.isPrepared() && !query.hasError());
            query.bindValue(":rel_id", relationId.toVariant());
            if (query.execPrepared()) {
                while (query.next()) {
                    const QSqlRecord record = query.record();
                    trackIdsByGroup[record.value(QStringLiteral("rgt_group_id")).toString()]
                            .insert(TrackId(record.value(QStringLiteral("rgt_track_id"))));
                }
            }
        }
    }

    // Optional condition on the tracks, eg to skip tracks without a value
    const QString valueCondition = rule.valueCondition.isEmpty()
            ? QString()
            : QStringLiteral("AND (") + rule.valueCondition + QStringLiteral(") ");

    // Optional extra join to a second table (eg cmrt_groups). When set, the
    // joined table has the alias x and is available to the SQL expressions of
    // the rule. It is a LEFT JOIN because the joined row only matches some
    // tracks of a group (eg only the canonical track); the other tracks of the
    // group must still be returned.
    const QString extraJoin = rule.joinTable.isEmpty()
            ? QString()
            : QStringLiteral("LEFT JOIN %1 x ON x.%2 = t.%3 ")
                      .arg(rule.joinTable, rule.joinKeyColumn, rule.keyColumn);

    // The tracks of the rule. Tracks with the same value are related, how the
    // values are compared depends on the match mode of the rule. Hidden tracks
    // are ignored.
    QList<Candidate> candidates;
    {
        const QString orderExpression = rule.orderExpression.isEmpty()
                ? QStringLiteral("l.id")
                : rule.orderExpression + QStringLiteral(", l.id");
        // Exactly matching values: the tracks that have no partner are already
        // left out by the query. The other match modes compare the values in the
        // code below, because the same value can be written in different ways.
        const QString partnerFilter =
                rule.match == RelationAutoGenerator::Rule::Match::Exact
                ? QStringLiteral(
                          "AND t.%2 IN (SELECT t2.%2 FROM %1 t2 "
                          "JOIN " LIBRARY_TABLE
                          " l2 ON l2.id = t2.%3 "
                          "WHERE l2.mixxx_deleted = 0 AND t2.%2 IS NOT NULL "
                          "AND TRIM(t2.%2) <> '' GROUP BY t2.%2 HAVING COUNT(*) "
                          ">= 2) ")
                          .arg(rule.table, rule.keyColumn, rule.trackIdColumn)
                : QString();
        FwdSqlQuery query(database,
                QStringLiteral(
                        "SELECT t.%2 AS k, t.%3 AS id, %4 AS gc, %5 AS tc FROM "
                        "%1 t "
                        "JOIN " LIBRARY_TABLE " l ON l.id = t.%3 "
                        "%7"
                        "WHERE l.mixxx_deleted = 0 AND t.%2 IS NOT NULL AND "
                        "TRIM(t.%2) <> '' "
                        "%8"
                        "%9"
                        "ORDER BY %6")
                        .arg(rule.table,
                                rule.keyColumn,
                                rule.trackIdColumn,
                                rule.groupCommentExpression,
                                rule.trackCommentExpression,
                                orderExpression,
                                extraJoin,
                                valueCondition,
                                partnerFilter));
        if (!query.isPrepared() || !query.execPrepared()) {
            kLogger.warning() << "Failed to read the tracks for relation type"
                              << rule.relationType;
            ++result.failedGroups;
            return result;
        }
        while (query.next()) {
            const QSqlRecord record = query.record();
            candidates.append(Candidate{
                    record.value(QStringLiteral("k")).toString(),
                    record.value(QStringLiteral("gc")).toString(),
                    record.value(QStringLiteral("tc")).toString(),
                    TrackId(record.value(QStringLiteral("id")))});
        }
    }

    // Group the tracks by value (compared as the match mode of the rule says).
    // Within a group the order of the query is kept: the first track of a
    // group is the first one of the query.
    QHash<QString, QList<int>> candidateIndexesByKey;
    QStringList keys;
    for (int i = 0; i < candidates.size(); ++i) {
        const QString normalizedKey = normalizeKey(rule, candidates.at(i).key);
        if (normalizedKey.isEmpty()) {
            continue;
        }
        QList<int>& indexes = candidateIndexesByKey[normalizedKey];
        if (indexes.isEmpty()) {
            keys.append(normalizedKey);
        }
        indexes.append(i);
    }

    // Declared outside the group loop so clazy-container-inside-loop does not
    // flag it and the capacity is reused across groups. Cleared before each
    // new group is built.
    QList<RelationsDao::GroupTrack> groupTracks;

    for (const QString& key : std::as_const(keys)) {
        const QList<int> indexes = candidateIndexesByKey.value(key);
        if (indexes.size() < 2) {
            continue;
        }
        const QString groupComment = candidates.at(indexes.first()).groupComment.trimmed();
        const QString identity = rule.identifyByGroupComment ? groupComment : key;

        if (rule.identifyByGroupComment && groupComment.isEmpty()) {
            // Such a group could not be recognized the next time
            kLogger.warning() << rule.relationType << "empty group comment for" << key;
            ++result.failedGroups;
            continue;
        }

        const auto existing = groupIdByIdentity.constFind(identity);
        if (existing != groupIdByIdentity.constEnd()) {
            // The group exists: only add the tracks that are not in it yet
            const DbId groupId(existing.value());
            const QSet<TrackId> members = trackIdsByGroup.value(groupId.toString());
            int added = 0;
            bool failed = false;
            for (const int index : indexes) {
                const Candidate& candidate = candidates.at(index);
                if (members.contains(candidate.trackId)) {
                    continue;
                }
                if (relationsDao.addTrackToGroup(
                            groupId, candidate.trackId, candidate.trackComment)) {
                    ++added;
                } else {
                    failed = true;
                }
            }
            if (failed) {
                ++result.failedGroups;
            } else if (added > 0) {
                ++result.extendedGroups;
            } else {
                ++result.unchangedGroups;
            }
        } else {
            if (!relationId.isValid()) {
                relationId = relationsDao.addRelation(rule.relationType, rule.description);
            }
            if (!relationId.isValid()) {
                ++result.failedGroups;
            } else {
                groupTracks.clear();
                groupTracks.reserve(indexes.size());
                for (const int index : indexes) {
                    const Candidate& candidate = candidates.at(index);
                    groupTracks.append(RelationsDao::GroupTrack{
                            candidate.trackId, candidate.trackComment});
                }
                const DbId groupId = relationsDao.createGroupWithTrackComments(
                        relationId, groupTracks, groupComment);
                if (groupId.isValid()) {
                    ++result.createdGroups;
                } else {
                    ++result.failedGroups;
                }
            }
        }
    }

    kLogger.info() << rule.relationType << "created" << result.createdGroups
                   << "extended" << result.extendedGroups
                   << "unchanged" << result.unchangedGroups
                   << "failed" << result.failedGroups;
    return result;
}

} // namespace

// static
QList<RelationAutoGenerator::Rule> RelationAutoGenerator::rules() {
    QList<Rule> allRules = {
            libraryRule(QStringLiteral("Artist"),
                    QStringLiteral("Tracks with the same Artist"),
                    QStringLiteral("artist"),
                    QStringLiteral("artist")),
            libraryRule(QStringLiteral("Album"),
                    QStringLiteral("Tracks with the same Album"),
                    QStringLiteral("album"),
                    QStringLiteral("album")),
            libraryRule(QStringLiteral("Album-Artist"),
                    QStringLiteral("Tracks with the same Album-Artist"),
                    QStringLiteral("album_artist"),
                    QStringLiteral("album_artist")),
            libraryRule(QStringLiteral("Title"),
                    QStringLiteral("Tracks with the same Title"),
                    QStringLiteral("title"),
                    QStringLiteral("title")),
            libraryRule(QStringLiteral("Composer"),
                    QStringLiteral("Tracks with the same Composer"),
                    QStringLiteral("composer"),
                    QStringLiteral("composer")),
            libraryRule(QStringLiteral("Year"),
                    QStringLiteral("Tracks with the same Year"),
                    QStringLiteral("year"),
                    QStringLiteral("year")),
            libraryRule(QStringLiteral("Color"),
                    QStringLiteral("Tracks with the same Color"),
                    QStringLiteral("color"),
                    QStringLiteral("color")),
            libraryRule(QStringLiteral("BPM"),
                    QStringLiteral("Tracks with the same BPM"),
                    QStringLiteral("bpm"),
                    QStringLiteral("bpm")),
            libraryRule(QStringLiteral("KEY"),
                    QStringLiteral("Tracks with the same KEY"),
                    QStringLiteral("key_id"),
                    QStringLiteral("key")),
            libraryRule(QStringLiteral("Genre"),
                    QStringLiteral("Tracks with the same Genre"),
                    QStringLiteral("genre"),
                    QStringLiteral("genre")),
            libraryRule(QStringLiteral("Rating"),
                    QStringLiteral("Tracks with the same Rating"),
                    QStringLiteral("rating"),
                    QStringLiteral("rating")),
            libraryRule(QStringLiteral("MB-Recording"),
                    QStringLiteral("Tracks with the same MusicBrainz recording id"),
                    QStringLiteral("musicbrainz_recording_id"),
                    QStringLiteral("title")),
            libraryRule(QStringLiteral("MB-Release"),
                    QStringLiteral("Tracks with the same MusicBrainz release id"),
                    QStringLiteral("musicbrainz_release_id"),
                    QStringLiteral("album")),
            libraryRule(QStringLiteral("MB-Track"),
                    QStringLiteral("Tracks with the same MusicBrainz track id"),
                    QStringLiteral("musicbrainz_track_id"),
                    QStringLiteral("title")),
            libraryRule(QStringLiteral("MB-Artist"),
                    QStringLiteral("Tracks with the same MusicBrainz artist id"),
                    QStringLiteral("musicbrainz_artist_id"),
                    QStringLiteral("artist")),
    };

    // "No value" conditions (0 means: not analyzed or not set) and readable group
    // comments for numeric columns. The rules that create very large groups are
    // not checked by default in the dialog.
    const QStringList textMatchTypes = {QStringLiteral("Artist"),
            QStringLiteral("Album"),
            QStringLiteral("Album-Artist"),
            QStringLiteral("Title"),
            QStringLiteral("Composer"),
            QStringLiteral("Year"),
            QStringLiteral("Genre"),
            QStringLiteral("Rating")};
    const QStringList largeGroupTypes = {QStringLiteral("Title"),
            QStringLiteral("Year"),
            QStringLiteral("Color"),
            QStringLiteral("BPM"),
            QStringLiteral("KEY")};
    for (Rule& rule : allRules) {
        if (rule.relationType == QStringLiteral("BPM")) {
            rule.valueCondition = QStringLiteral("t.bpm > 0");
            //  Tracks with the same BPM, rounded to whole BPM, are related
            //  (matchDecimals: 0 = whole BPM, 1 = 0.1 BPM)
            rule.match = Rule::Match::Number;
            rule.matchDecimals = 0;
            rule.groupCommentExpression =
                    QStringLiteral("printf('%.%1f', l.bpm)").arg(rule.matchDecimals);
            rule.trackCommentExpression = QStringLiteral("printf('%.2f', t.bpm)");
        } else if (rule.relationType == QStringLiteral("KEY")) {
            rule.valueCondition = QStringLiteral("t.key_id > 0");
            // The comments contain the key as {key:NN}: it is shown in the key
            // notation chosen by the user, see relationformat::expandComment()
            rule.groupCommentExpression = QStringLiteral("printf('{key:%02d}', l.key_id)");
            rule.trackCommentExpression = QStringLiteral("printf('{key:%02d}', t.key_id)");
        } else if (rule.relationType == QStringLiteral("Color")) {
            rule.groupCommentExpression = QStringLiteral("printf('#%06X', l.color)");
        }
        // Text values are compared ignoring case and white space (Abba = ABBA)
        if (textMatchTypes.contains(rule.relationType)) {
            rule.match = Rule::Match::Text;
        }
        if (largeGroupTypes.contains(rule.relationType)) {
            rule.enabledByDefault = false;
        }
    }

    //// CMRT: tracks that are members of the same CMRT group. The group is
    //// recognized by its group comment, the track comment tells which track
    //// is the canonical one.
    {
        Rule cmrt;
        cmrt.relationType = QStringLiteral("CMRT");
        cmrt.description = QStringLiteral("Tracks in the same CMRT group");
        cmrt.table = QStringLiteral("cmrt_members");
        cmrt.trackIdColumn = QStringLiteral("track_id");
        cmrt.keyColumn = QStringLiteral("group_id");
        // The canonical track is the member whose library id equals the
        // canonical_track_id of the group. It is looked up in cmrt_groups,
        // which is joined with the alias x.
        cmrt.joinTable = QStringLiteral("cmrt_groups");
        cmrt.joinKeyColumn = QStringLiteral("group_id");
        cmrt.groupCommentExpression = QStringLiteral("'CMRT-ID' || t.group_id");
        // NOTE: compare against t.track_id (the rule's own track id column),
        // which is the same value as l.id, but is always in scope and clearly
        // refers to the member row. CAST to INTEGER so a type mismatch between
        // cmrt_groups.canonical_track_id and cmrt_members.track_id cannot make
        // the comparison silently fail.
        cmrt.trackCommentExpression = QStringLiteral(
                "CASE WHEN CAST(x.canonical_track_id AS INTEGER) = "
                "CAST(t.track_id AS INTEGER) "
                "THEN 'Canonical' ELSE 'CMRT-MEMBER' END");
        // The canonical track must be the first track of the group
        cmrt.orderExpression = QStringLiteral(
                "CASE WHEN CAST(x.canonical_track_id AS INTEGER) = "
                "CAST(t.track_id AS INTEGER) "
                "THEN 0 ELSE 1 END");
        cmrt.identifyByGroupComment = true;
        cmrt.requiredColumns = {
                QStringLiteral("cmrt_members.group_id"),
                QStringLiteral("cmrt_members.track_id"),
                QStringLiteral("cmrt_groups.group_id"),
                QStringLiteral("cmrt_groups.canonical_track_id")};
        allRules.append(cmrt);
    }

    // Rules for other tables, eg CMRT in development branch
    return allRules;
}

// static
QList<RelationAutoGenerator::Rule> RelationAutoGenerator::availableRules(
        const QSqlDatabase& database) {
    QList<Rule> available;
    QHash<QString, QSet<QString>> columnsByTable;
    const QList<Rule> allRules = rules();
    for (const Rule& rule : allRules) {
        bool isAvailable = true;
        for (const QString& requirement : rule.requiredColumns) {
            const int dot = requirement.indexOf(QLatin1Char('.'));
            if (dot <= 0) {
                continue;
            }
            const QString table = requirement.left(dot);
            const QString column = requirement.mid(dot + 1);
            if (!columnsByTable.contains(table)) {
                columnsByTable.insert(table, tableColumns(database, table));
            }
            if (!columnsByTable.value(table).contains(column)) {
                isAvailable = false;
                break;
            }
        }
        if (isAvailable) {
            available.append(rule);
        }
    }
    return available;
}

// static
QList<RelationAutoGenerator::RuleResult> RelationAutoGenerator::apply(
        const QSqlDatabase& database,
        RelationsDao& relationsDao,
        const QList<Rule>& rules) {
    QList<RuleResult> results;
    // One transaction for everything: thousands of inserts are much faster this way
    ScopedTransaction transaction(database);
    for (const Rule& rule : rules) {
        results.append(applyRule(database, relationsDao, rule));
    }
    transaction.commit();
    return results;
}
