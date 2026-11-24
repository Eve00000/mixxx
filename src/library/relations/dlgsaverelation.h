#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>

#include "library/dao/relationsdao.h"
#include "track/track_decl.h"

class QWidget;

class DlgSaveRelation {
    Q_DECLARE_TR_FUNCTIONS(RelationSaveDialog)
  public:
    struct Entry {
        TrackPointer pTrack;
        QString prefix;
    };

    // Show the dialog & create the relation group
    // Returns id of new group
    // or invalid id when cancelled or failed
    static DbId execute(QWidget* pParent,
            RelationsDao& relationsDao,
            const QList<Entry>& entries,
            DbId presetRelationId = DbId());

    // Artist - Title (Album - Year)
    // empty parts are left out
    // only left 4 chars of year
    static QString trackLabel(const QString& artist,
            const QString& title,
            const QString& album,
            const QString& year);
};
