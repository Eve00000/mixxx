#pragma once

#include <QAbstractTableModel>
#include <QList>

#include "library/streaming/streamingprovider.h"
#include "library/trackmodel.h"

class TrackCollectionManager;

/// In-memory track model for streaming search results.
///
/// The tracks are not stored in the Mixxx database. Playback is handled by the
/// provider, which downloads an unencrypted stream to a local cache file that
/// is then loaded into a deck.
class StreamingTrackListModel final : public QAbstractTableModel, public TrackModel {
    Q_OBJECT

  public:
    enum Column {
        ColumnArtist = 0,
        ColumnTitle,
        ColumnAlbum,
        ColumnDuration,
        ColumnQuality,
        ColumnCount,
    };

    StreamingTrackListModel(
            QObject* parent,
            TrackCollectionManager* pTrackCollectionManager,
            const QString& providerId,
            const QString& displayName);

    void setTracks(const mixxx::streaming::TrackList& tracks);
    void clearTracks();

    mixxx::streaming::Track trackAtRow(int row) const;
    bool hasTrackAtRow(int row) const;

    // Inherited from QAbstractItemModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
            int section,
            Qt::Orientation orientation,
            int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    QStringList mimeTypes() const override;
    Qt::DropActions supportedDragActions() const override;

    // Inherited from TrackModel
    Capabilities getCapabilities() const override;
    TrackPointer getTrack(const QModelIndex& index) const override;
    TrackPointer getTrackByRef(const TrackRef& trackRef) const override;
    QUrl getTrackUrl(const QModelIndex& index) const override;
    QString getTrackLocation(const QModelIndex& index) const override;
    TrackId getTrackId(const QModelIndex& index) const override;
    CoverInfo getCoverInfo(const QModelIndex& index) const override;
    const QVector<int> getTrackRows(TrackId trackId) const override;
    void search(const QString& searchText) override;
    const QString currentSearch() const override;
    bool isColumnInternal(int column) override;
    bool isColumnHiddenByDefault(int column) override;
    TrackModel::SortColumnId sortColumnIdFromColumnIndex(int index) const override;
    int columnIndexFromSortColumnId(TrackModel::SortColumnId sortColumn) const override;
    QString modelKey(bool noSearch) const override;
    bool updateTrackGenre(
            Track* pTrack,
            const QString& genre) const override;

  private:
    QString placeholderUrl(const mixxx::streaming::Track& track) const;

    mixxx::streaming::TrackList m_tracks;
    const QString m_providerId;
    const QString m_displayName;
    TrackCollectionManager* const m_pTrackCollectionManager;
};
