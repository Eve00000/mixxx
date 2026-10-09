#pragma once

#include <QVariantMap>

#include "library/trackset/crate/crateid.h"
#include "library/trackset/tracksettablemodel.h"

class CrateTableModel final : public TrackSetTableModel {
    Q_OBJECT

  public:
    CrateTableModel(QObject* parent, TrackCollectionManager* pTrackCollectionManager);
    ~CrateTableModel() final = default;

    void selectCrate(CrateId crateId = CrateId());
    CrateId selectedCrate() const {
        return m_selectedCrate;
    }

    void selectCrateGroup(const QString& groupName);

    // Returns a flat list of {group_name, crate_id, crate_name} entries.
    // - If groupedCratesLength is true, names are grouped by a fixed prefix of
    //   groupedCratesFixedLength characters.
    // - Otherwise, the var-length mask string is used as a delimiter and
    //   multi-level groups are derived.
    QList<QVariantMap> getGroupedCrates(
            bool groupedCratesLength,
            int groupedCratesFixedLength,
            const QString& groupedCratesVarLengthMask);

    bool addTrack(const QModelIndex& index, const QString& location);

    void removeTracks(const QModelIndexList& indices) final;
    /// Returns the number of unsuccessful additions.
    int addTracksWithTrackIds(const QModelIndex& index,
            const QList<TrackId>& tracks,
            int* pOutInsertionPos) final;
    bool isLocked() final;

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

  private:
    CrateId m_selectedCrate;
    QHash<CrateId, QString> m_searchTexts;
};
