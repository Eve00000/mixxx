#pragma once

#include <QAbstractTableModel>
#include <QList>

#include "library/trackmodel.h"
#include "library/tidal/tidalclient.h"

class TrackCollectionManager;

/// In-memory track model for TIDAL search results.
///
/// The tracks are not stored in the Mixxx database. Playback is handled by
/// downloading the (unencrypted) MPEG-DASH stream to the local cache and then
/// loading the resulting file into a deck.
class TidalTrackListModel final : public QAbstractTableModel, public TrackModel {
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

    TidalTrackListModel(
            QObject* parent,
            TrackCollectionManager* pTrackCollectionManager);
    ~TidalTrackListModel() override = default;

    void setTracks(const QList<mixxx::tidal::TidalTrack>& tracks);
    void clearTracks();

    mixxx::tidal::TidalTrack trackAtRow(int row) const;
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
    QList<mixxx::tidal::TidalTrack> m_tracks;
    TrackCollectionManager* const m_pTrackCollectionManager;
};
