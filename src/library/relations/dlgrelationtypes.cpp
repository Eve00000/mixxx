#include "library/relations/dlgrelationtypes.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "library/dao/relationsdao.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgrelationtypes.cpp"
#include "util/datetime.h"

namespace {
constexpr int kColumnType = 0;
constexpr int kColumnDescription = 1;
constexpr int kColumnGroups = 2;
constexpr int kColumnAdded = 3;
constexpr int kColumnCount = 4;
} // namespace

DlgRelationTypes::DlgRelationTypes(QWidget* pParent, Library* pLibrary)
        : QDialog(pParent),
          m_pLibrary(pLibrary),
          m_pTableRelationTypes(new QTableWidget(this)) {
    setWindowTitle(tr("Relation Types"));
    resize(800, 500);

    auto* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(new QLabel(
            tr("Double-click a type or a description to edit it. "
               "Renaming a type to an existing name merges the two."),
            this));
    pLayout->addWidget(m_pTableRelationTypes);

    m_pTableRelationTypes->setColumnCount(kColumnCount);
    m_pTableRelationTypes->setHorizontalHeaderLabels(
            {tr("Type"), tr("Description"), tr("Groups"), tr("Added")});
    m_pTableRelationTypes->verticalHeader()->setVisible(false);
    m_pTableRelationTypes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTableRelationTypes->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pTableRelationTypes->setEditTriggers(
            QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_pTableRelationTypes->horizontalHeader()->setStretchLastSection(true);

    auto* pButtonLayout = new QHBoxLayout();
    auto* pMergeRelationButton = new QPushButton(tr("Merge into..."), this);
    auto* pDeleteRelationButton = new QPushButton(tr("Delete"), this);
    auto* pDeleteUnusedRelationsButton = new QPushButton(tr("Delete unused"), this);
    auto* pCloseButton = new QPushButton(tr("Close"), this);
    pButtonLayout->addWidget(pMergeRelationButton);
    pButtonLayout->addWidget(pDeleteRelationButton);
    pButtonLayout->addWidget(pDeleteUnusedRelationsButton);
    pButtonLayout->addStretch();
    pButtonLayout->addWidget(pCloseButton);
    pLayout->addLayout(pButtonLayout);

    connect(pMergeRelationButton, &QPushButton::clicked, this, &DlgRelationTypes::slotMerge);
    connect(pDeleteRelationButton, &QPushButton::clicked, this, &DlgRelationTypes::slotDelete);
    connect(pDeleteUnusedRelationsButton,
            &QPushButton::clicked,
            this,
            &DlgRelationTypes::slotDeleteUnused);
    connect(pCloseButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_pTableRelationTypes,
            &QTableWidget::itemChanged,
            this,
            &DlgRelationTypes::slotItemChanged);

    reload();
}

RelationsDao& DlgRelationTypes::relationsDao() const {
    return m_pLibrary->trackCollectionManager()->internalCollection()->getRelationsDAO();
}

DbId DlgRelationTypes::selectedRelationId() const {
    const int row = m_pTableRelationTypes->currentRow();
    if (row < 0) {
        return DbId();
    }
    QTableWidgetItem* pItem = m_pTableRelationTypes->item(row, kColumnType);
    return pItem ? DbId(pItem->data(Qt::UserRole)) : DbId();
}

void DlgRelationTypes::reload() {
    const QSignalBlocker blocker(m_pTableRelationTypes);

    const QList<RelationsDao::Relation> relations = relationsDao().allRelations();
    m_pTableRelationTypes->setRowCount(relations.size());
    for (int row = 0; row < relations.size(); ++row) {
        const RelationsDao::Relation& relation = relations.at(row);

        auto* pTypeItem = new QTableWidgetItem(relation.type);
        pTypeItem->setData(Qt::UserRole, relation.id.toVariant());
        m_pTableRelationTypes->setItem(row, kColumnType, pTypeItem);

        m_pTableRelationTypes->setItem(row,
                kColumnDescription,
                new QTableWidgetItem(relation.description));

        auto* pGroupsItem = new QTableWidgetItem();
        pGroupsItem->setData(Qt::DisplayRole, relationsDao().groupCountForRelation(relation.id));
        pGroupsItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_pTableRelationTypes->setItem(row, kColumnGroups, pGroupsItem);

        auto* pAddedItem = new QTableWidgetItem(QLocale().toString(
                mixxx::localDateTimeFromUtc(relation.dateAdded),
                QLocale::ShortFormat));
        pAddedItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_pTableRelationTypes->setItem(row, kColumnAdded, pAddedItem);
    }
    m_pTableRelationTypes->resizeColumnToContents(kColumnType);
}

void DlgRelationTypes::slotItemChanged(QTableWidgetItem* pItem) {
    if (pItem->column() != kColumnType && pItem->column() != kColumnDescription) {
        return;
    }
    QTableWidgetItem* pTypeItem = m_pTableRelationTypes->item(pItem->row(), kColumnType);
    QTableWidgetItem* pDescriptionItem =
            m_pTableRelationTypes->item(pItem->row(), kColumnDescription);
    if (!pTypeItem || !pDescriptionItem) {
        return;
    }

    const DbId id(pTypeItem->data(Qt::UserRole));
    const QString type = pTypeItem->text().trimmed();
    const QString description = pDescriptionItem->text().trimmed();

    // The table is rebuilt after the edit has been handled
    const auto reloadLater = [this]() {
        QTimer::singleShot(0, this, [this]() {
            reload();
        });
    };

    if (type.isEmpty()) {
        QMessageBox::warning(this, tr("Relation Types"), tr("A Relation type needs a name."));
        reloadLater();
        return;
    }

    // Another type with the same name (ignoring case): offer to merge
    const QList<RelationsDao::Relation> relations = relationsDao().allRelations();
    for (const RelationsDao::Relation& other : relations) {
        if (!(other.id == id) && other.type.compare(type, Qt::CaseInsensitive) == 0) {
            const QMessageBox::StandardButton answer = QMessageBox::question(this,
                    tr("Merge Relation Types"),
                    tr("The Relation Type \"%1\" already exists. Merge this type into it? "
                       "All its groups move to \"%1\".")
                            .arg(other.type),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                relationsDao().mergeRelations(id, other.id);
            }
            reloadLater();
            return;
        }
    }

    relationsDao().updateRelation(id, type, description);
    reloadLater();
}

void DlgRelationTypes::slotMerge() {
    const DbId id = selectedRelationId();
    if (!id.isValid()) {
        return;
    }
    const RelationsDao::Relation source = relationsDao().relationById(id);

    QStringList names;
    QList<DbId> targetIds;
    const QList<RelationsDao::Relation> relations = relationsDao().allRelations();
    for (const RelationsDao::Relation& relation : relations) {
        if (!(relation.id == id)) {
            names.append(relation.type);
            targetIds.append(relation.id);
        }
    }
    if (names.isEmpty()) {
        QMessageBox::information(this,
                tr("Merge Relation Types"),
                tr("There is no other type to merge into."));
        return;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(this,
            tr("Merge Relation Types"),
            tr("Merge \"%1\" into:").arg(source.type),
            names,
            0,
            false,
            &ok);
    if (!ok) {
        return;
    }
    const int index = names.indexOf(chosen);
    if (index < 0) {
        return;
    }
    relationsDao().mergeRelations(id, targetIds.at(index));
    reload();
}

void DlgRelationTypes::slotDelete() {
    const DbId id = selectedRelationId();
    if (!id.isValid()) {
        return;
    }
    const RelationsDao::Relation relation = relationsDao().relationById(id);
    const int groupCount = relationsDao().groupCountForRelation(id);

    if (groupCount == 0) {
        const QMessageBox::StandardButton answer = QMessageBox::question(this,
                tr("Delete Relation Type"),
                tr("Delete the relation type \"%1\"?").arg(relation.type),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        relationsDao().removeRelation(id);
    } else {
        const QMessageBox::StandardButton answer = QMessageBox::question(this,
                tr("Delete Relation Type"),
                tr("The Relation Type \"%1\" is used by %2 relation group(s). Delete the type "
                   "and these groups? The tracks themselves are not affected.")
                        .arg(relation.type, QString::number(groupCount)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        relationsDao().removeRelationWithGroups(id);
    }
    reload();
}

void DlgRelationTypes::slotDeleteUnused() {
    const QMessageBox::StandardButton answer = QMessageBox::question(this,
            tr("Delete Unused Relation Types"),
            tr("Delete all relation types that have no groups?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    const int removed = relationsDao().removeUnusedRelations();
    QMessageBox::information(this,
            tr("Delete Unused Relation Types"),
            tr("%1 unused relation type(s) deleted.").arg(removed));
    reload();
}
