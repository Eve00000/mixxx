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

// #pragma once
//
// #include <QList>
// #include <QSqlDatabase>
// #include <QString>
//
// #include "library/dao/relationsdao.h"
//
//// Creates relation groups from tracks that share a value in a library column
//// (e.g. the same MusicBrainz recording id). Add a rule to
//// RelationAutoGenerator::rules() to generate relations for other metadata.
// class RelationAutoGenerator {
//   public:
//     struct Rule {
//         // Relation type of the generated groups
//         QString relationType;
//         // Description of the relation type
//         QString description;
//         // Library column with the shared value, also used as track comment
//         QString keyColumn;
//         // Library column of the first track that is used as group comment
//         QString groupCommentColumn;
//     };
//
//     struct RuleResult {
//         QString relationType;
//         int createdGroups = 0;
//         int extendedGroups = 0;
//         int unchangedGroups = 0;
//         int failedGroups = 0;
//     };
//
//     // All rules
//     static QList<Rule> rules();
//     // The rules of which the columns exist in the library table
//     static QList<Rule> availableRules(const QSqlDatabase& database);
//
//     // Applies the rules. A group that already exists is not created again, it
//     // is only extended with the tracks that are new in the collection.
//     static QList<RuleResult> apply(const QSqlDatabase& database,
//             RelationsDao& relationsDao,
//             const QList<Rule>& rules);
// };
