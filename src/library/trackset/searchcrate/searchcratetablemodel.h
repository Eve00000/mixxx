#pragma once

#include "library/trackset/searchcrate/searchcrateid.h"
#include "library/trackset/tracksettablemodel.h"

class SearchCrateTableModel final : public TrackSetTableModel {
    Q_OBJECT

  public:
    SearchCrateTableModel(QObject* parent, TrackCollectionManager* pTrackCollectionManager);
    ~SearchCrateTableModel() final = default;

    void selectSearchCrate(SearchCrateId searchCrateId = SearchCrateId());
    SearchCrateId selectedSearchCrate() const {
        return m_selectedSearchCrate;
    }

    bool addTrack(const QModelIndex& index, const QString& location);

    void removeTracks(const QModelIndexList& indices) final;
    /// Returns the number of unsuccessful additions.
    int addTracksWithTrackIds(const QModelIndex& index,
            const QList<TrackId>& tracks,
            int* pOutInsertionPos) final;
    bool isLocked() final;

    void selectSearchCrate2QVL(SearchCrateId searchCrateId, QVariantList& searchCrateData);

    QString buildWhereClause(const QVariantList& searchCrateData);
    QVariant getPreviousRecordId(SearchCrateId currentId);
    QVariant getNextRecordId(SearchCrateId currentId);

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

  private:
    SearchCrateId m_selectedSearchCrate;
    QHash<SearchCrateId, QString> m_searchTexts;
};
