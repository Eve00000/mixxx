#pragma once

#include "library/basesqltablemodel.h"

class RelationsTableModel final : public BaseSqlTableModel {
    Q_OBJECT

  public:
    RelationsTableModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager);
    ~RelationsTableModel() final = default;

    bool isColumnInternal(int column) override;
    bool isColumnHiddenByDefault(int column) override;

    DbId getRelationId(const QModelIndex& index) const;

    void showRelatedTracks(TrackPointer pTrack);

    // Search in the track columns and in the relation columns
    // (type, group comment, track comment)
    void searchRelations(const QString& searchText);
    QString relationSearch() const {
        return m_relationSearch;
    }

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

  public slots:
    void showAllRelations();

  private:
    TrackPointer m_pDeckTrack;
    QString m_relationSearch;

    friend class WRelationTableView;
};
