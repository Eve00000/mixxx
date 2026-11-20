#include "widget/wrelationtableview.h"

#include <QPainter>

#include "library/dao/trackschema.h"
#include "library/library.h"
#include "library/relations/relationstablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_wrelationtableview.cpp"

const int SEPARATOR_SIZE = 2; // Slightly thicker for groups

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

RelationsDao::RelationGroup WRelationTableView::getSelectedGroup() {
    QList<DbId> relationIds = getSelectedRelationIds();
    if (relationIds.isEmpty()) {
        return RelationsDao::RelationGroup{};
    }

    // In our grouped schema, the "ID" of the relation IS the group ID.
    return m_pLibrary
            ->trackCollectionManager()
            ->internalCollection()
            ->getRelationsDAO()
            .getGroupById(relationIds[0]);
}

void WRelationTableView::paintEvent(QPaintEvent* event) {
    QTableView::paintEvent(event);

    if (m_bDrawGroupSeparators && model()) {
        QPainter painter(viewport());
        QPen pen(QColor(60, 60, 60), SEPARATOR_SIZE);
        painter.setPen(pen);

        int rowCount = model()->rowCount();

        // Find the column index for our Group ID so we can check when it changes
        int groupIdCol = -1;
        for (int c = 0; c < model()->columnCount(); ++c) {
            // We can look up the column by its header name or use the model's fieldIndex
            if (model()->headerData(c, Qt::Horizontal).toString() == "Group ID" ||
                    model()->headerData(c, Qt::Horizontal).toString() == "Relation Type") {
                // We actually don't want to rely on the header text, let's find
                // the hidden Group ID column. Since it's internal/hidden, we
                // can just check the raw data.
            }
        }

        // If the column is hidden, we can still query the model for the index
        // by data. Alternatively, assume the model always has the group ID at
        // index 1 (as defined in relationstablemodel.cpp). Let's use fieldIndex
        // by name if we cast it.
        RelationsTableModel* pModel = dynamic_cast<RelationsTableModel*>(model());
        if (pModel) {
            groupIdCol = pModel->fieldIndex(ColumnCache::COLUMN_RELATIONGROUPSTABLE_ID);
        }

        if (groupIdCol != -1) {
            for (int row = 1; row < rowCount; ++row) {
                QVariant prevGroup = model()->index(row - 1, groupIdCol).data();
                QVariant currGroup = model()->index(row, groupIdCol).data();

                // Draw a line when the group ID changes
                if (prevGroup != currGroup) {
                    int y = rowViewportPosition(row);
                    painter.drawLine(0, y, viewport()->width(), y);
                }
            }
        }
    }
}
