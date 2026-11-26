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

    // Show only the relations of one type (an invalid id shows all types)
    void setTypeFilter(DbId relationId);
    DbId typeFilter() const {
        return m_relationTypeFilter;
    }

    // The track of the deck this view shows the relations for (null in the
    // view with all relations)
    TrackPointer deckTrack() const {
        return m_pDeckTrack;
    }
    QString relationSearch() const {
        return m_relationSearch;
    }

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

    // Sorts on up to 3 columns, lifo,
    // including the relation columns
    bool sortOverride(int column, Qt::SortOrder order) override;

  public slots:
    void showAllRelations();

  private:
    TrackPointer m_pDeckTrack;
    QString m_relationSearch;
    DbId m_relationTypeFilter;
    QString relationFilterClause(const QString& keyword) const;

    QString relationSortClause() const;
    void loadSortSettings();
    QStringList m_sortFields;
    QList<Qt::SortOrder> m_sortOrders;

    friend class WRelationTableView;
};
