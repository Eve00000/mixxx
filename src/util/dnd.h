#pragma once

#include <QDrag>
#include <QDropEvent>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QUrl>

#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "util/fileinfo.h"
#include "widget/trackdroptarget.h"

class DragAndDropHelper final {
  public:
    DragAndDropHelper() = delete;

    static bool urlsContainSupportedTrackFiles(
            const QList<QUrl>& urls,
            bool acceptPlaylists);

    static QList<mixxx::FileInfo> supportedTracksFromUrls(
            const QList<QUrl>& urls,
            bool stopOnFirstMatch,
            bool acceptPlaylists);

    static bool allowDeckCloneAttempt(
            const QDropEvent& event,
            const QString& group);

    static bool dragEnterAccept(
            const QMimeData& mimeData,
            const QString& sourceIdentifier,
            bool stopOnFirstMatch,
            bool acceptPlaylists);

    static QDrag* dragTrack(
            TrackPointer pTrack,
            QWidget* pDragSource,
            const QString& sourceIdentifier);

    static QDrag* dragTrackLocations(
            const QList<QString>& locations,
            QWidget* pDragSource,
            const QString& sourceIdentifier);

    static void handleTrackDragEnterEvent(
            QDragEnterEvent* pEvent,
            const QString& group,
            UserSettingsPointer pConfig);

    static void handleTrackDropEvent(
            QDropEvent* pEvent,
            TrackDropTarget& target,
            const QString& group,
            UserSettingsPointer pConfig);

    /// Returns true if the MIME data carries TIDAL streaming tracks.
    static bool isTidalTracksMimeData(const QMimeData& mimeData);

    /// Handles dropping TIDAL streaming tracks. Returns true if the event was
    /// consumed, i.e. it carried TIDAL tracks, a drop callback is registered
    /// and loading to the target group is allowed.
    static bool handleTidalDropEvent(
            QDropEvent* pEvent,
            TrackDropTarget& target,
            const QString& group,
            UserSettingsPointer pConfig);

    static void mousePressed(QMouseEvent* pEvent);

    static bool mouseMoveInitiatesDrag(QMouseEvent* pEvent);
};
