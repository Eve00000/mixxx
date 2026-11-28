#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include "library/dao/relationsdao.h"

// Creates relation groups from tracks that share a value (e.g. the same
// MusicBrainz recording id). Add a rule to RelationAutoGenerator::rules() to
// generate relations from other metadata, also from other tables.
class RelationAutoGenerator {
  public:
    struct Rule {
        // Relation type of the generated groups
        QString relationType;
        // Description of the relation type
        QString description;
        // The table with the tracks that are related. In the SQL expressions
        // below this table has the alias t, the library table has the alias l.
        QString table;
        // Column of the table with the id of the library track
        QString trackIdColumn;
        // Column of the table with the shared value: tracks with the same value
        // are related
        QString keyColumn;
        // SQL expression: comment of the group, taken from the first track
        QString groupCommentExpression;
        // SQL expression: comment of every track in the group
        QString trackCommentExpression;
        // SQL expression: order of the tracks in a group, the first track is
        // the first one. Empty: order of the library id.
        QString orderExpression;
        // Optional: extra table joined into the candidate query, eg cmrt_groups.
        // Empty: no extra join, the SQL expressions below must not use x.
        // When set, this table has the alias x in the SQL expressions below and
        // is joined with:
        // LEFT JOIN <joinTable> x ON x.<joinKeyColumn> = t.<keyColumn>
        // The join is a LEFT JOIN because the joined row is only matched for
        // some tracks of a group (eg only the canonical track); the other
        // tracks of the group must still be returned.
        QString joinTable;
        // Column of joinTable that equals the rule key column (the group id)
        QString joinKeyColumn;
        // Optional SQL condition on the tracks (aliases t and l), eg "t.bpm > 0".
        // Tracks that do not meet it are ignored. Use it to skip "no value"
        // values (0) that would otherwise form one giant group.
        QString valueCondition;
        // false: the rule is not checked by default in the dialog (rules that
        // create very large groups)
        bool enabledByDefault = true;
        // How the values of the key column are compared
        enum class Match {
            // exactly the same text
            Exact,
            // ignoring case and white space (Abba = ABBA)
            Text,
            // as a number rounded to matchDecimals decimals (127.998 = 128.0)
            Number
        };
        Match match = Match::Exact;
        int matchDecimals = 0;
        // true: an existing group is recognized by its group comment (which must
        // be unique), false: by the track comments
        bool identifyByGroupComment = false;
        // "table.column" entries that must exist for the rule to be available
        QStringList requiredColumns;
    };

    struct RuleResult {
        QString relationType;
        int createdGroups = 0;
        int extendedGroups = 0;
        int unchangedGroups = 0;
        int failedGroups = 0;
    };

    // All rules
    static QList<Rule> rules();
    // The rules of which the tables and columns exist in the database
    static QList<Rule> availableRules(const QSqlDatabase& database);

    // Applies the rules. A group that already exists is not created again, it
    // is only extended with the tracks that are new in the collection.
    static QList<RuleResult> apply(const QSqlDatabase& database,
            RelationsDao& relationsDao,
            const QList<Rule>& rules);
};
