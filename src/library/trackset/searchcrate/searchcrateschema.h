#pragma once

#include <QString>

#define SEARCHCRATE_TABLE "searchCrates"
#define SEARCHCRATE_TRACKS_TABLE "searchCrate_tracks"

const QString SEARCHCRATETABLE_ID = QStringLiteral("id");
const QString SEARCHCRATETABLE_NAME = QStringLiteral("name");

// TODO(XXX): Fix AutoDJ database design.
// SearchCrates should have no dependency on AutoDJ stuff. Which
// searchCrates are used as a source for AutoDJ has to be stored
// and managed by the AutoDJ component in a separate table.
// This refactoring should be deferred until consensus on the
// redesign of the AutoDJ feature has been reached. The main
// ideas of the new design should be documented for verification
// before starting to code.
const QString SEARCHCRATETABLE_AUTODJ_SOURCE = QStringLiteral("autodj_source");

const QString SEARCHCRATETRACKSTABLE_SEARCHCRATEID = QStringLiteral("searchCrate_id");
const QString SEARCHCRATETRACKSTABLE_TRACKID = QStringLiteral("track_id");
