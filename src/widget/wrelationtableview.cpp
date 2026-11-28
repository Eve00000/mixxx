#include "widget/wrelationtableview.h"

#include <QList>
#include <QPainter>

#include "library/dao/trackschema.h"
#include "library/library.h"
#include "library/relations/relationstablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_wrelationtableview.cpp"

const int SEPARATOR_SIZE = 2;

WRelationTableView::WRelationTableView(
        QWidget* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        double trackTableBackgroundColorOpacity,
        bool drawGroupSeparators)
        : WTrackTableView(parent,
                  pConfig,
                  pLibrary,
                  trackTableBackgroundColorOpacity),
          m_pLibrary(pLibrary),
          m_bDrawGroupSeparators(drawGroupSeparators) {
    setDragDropMode(QAbstractItemView::DragOnly);
    setDragEnabled(true);
}

QList<DbId> WRelationTableView::getSelectedRelationIds() const {
    RelationsTableModel* pRelationTableModel = dynamic_cast<RelationsTableModel*>(model());
    VERIFY_OR_DEBUG_ASSERT(pRelationTableModel != nullptr) {
        qWarning() << "No relation model";
        return {};
    }

    const QModelIndexList rows = getSelectedRows();
    QList<DbId> relationIds;
    relationIds.reserve(rows.size());
    for (const QModelIndex& row : rows) {
        const DbId relationId = pRelationTableModel->getRelationId(row);
        if (relationId.isValid()) {
            relationIds.append(relationId);
        } else {
            qDebug() << "Skipping row" << row << "with invalid relation id";
        }
    }

    return relationIds;
}

QList<WRelationTableView::GroupTrack> WRelationTableView::getSelectedGroupTracks() const {
    RelationsTableModel* pRelationTableModel = dynamic_cast<RelationsTableModel*>(model());
    VERIFY_OR_DEBUG_ASSERT(pRelationTableModel != nullptr) {
        qWarning() << "No relation model";
        return {};
    }

    const QModelIndexList rows = getSelectedRows();
    QList<GroupTrack> result;
    result.reserve(rows.size());
    for (const QModelIndex& row : rows) {
        const DbId groupId = pRelationTableModel->getRelationId(row);
        const TrackId trackId = pRelationTableModel->getTrackId(row);
        if (groupId.isValid() && trackId.isValid()) {
            result.append(GroupTrack{groupId, trackId});
        }
    }
    return result;
}

RelationsDao::RelationGroup WRelationTableView::getSelectedGroup() {
    QList<DbId> relationIds = getSelectedRelationIds();
    if (relationIds.isEmpty()) {
        return RelationsDao::RelationGroup{};
    }

    return m_pLibrary
            ->trackCollectionManager()
            ->internalCollection()
            ->getRelationsDAO()
            .getGroupById(relationIds[0]);
}

void WRelationTableView::paintEvent(QPaintEvent* event) {
    WTrackTableView::paintEvent(event);

    if (m_bDrawGroupSeparators && model()) {
        QPainter painter(viewport());
        QPen pen(QColor(60, 60, 60), SEPARATOR_SIZE);
        painter.setPen(pen);

        int rowCount = model()->rowCount();

        // -> find the columnindex Group ID
        int groupIdCol = -1;
        for (int c = 0; c < model()->columnCount(); ++c) {
            if (model()->headerData(c, Qt::Horizontal).toString() == "Group ID" ||
                    model()->headerData(c, Qt::Horizontal).toString() == "Relation Type") {
            }
        }

        // If hidden -> still query the model for the index by data.
        // Group ID @ index 1 -> fieldIndex by name -> cast
        RelationsTableModel* pModel = dynamic_cast<RelationsTableModel*>(model());
        if (pModel) {
            groupIdCol = pModel->fieldIndex(ColumnCache::COLUMN_RELATIONGROUPSTABLE_ID_ALIAS);
        }

        if (groupIdCol != -1 && rowCount > 1) {
            int firstRow = rowAt(0);
            int lastRow = rowAt(viewport()->height() - 1);
            if (firstRow < 1) {
                firstRow = 1;
            }
            if (lastRow < 0) {
                lastRow = rowCount - 1;
            }
            for (int row = firstRow; row <= lastRow; ++row) {
                QVariant prevGroup = model()->index(row - 1, groupIdCol).data();
                QVariant currGroup = model()->index(row, groupIdCol).data();

                // new Group Id = line separator
                if (prevGroup != currGroup) {
                    int y = rowViewportPosition(row);
                    painter.drawLine(0, y, viewport()->width(), y);
                }
            }
        }
    }
}
