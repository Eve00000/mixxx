#include "library/relations/dlgrelationinfo.h"

#include <QCollator>
#include <QHeaderView>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTableWidgetItem>
#include <algorithm>
#include <functional>

#include "defs_urls.h"
#include "library/library.h"
#include "library/relations/dlgsaverelation.h"
#include "library/relations/relationsortitem.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgrelationinfo.cpp"
#include "track/track.h"
#include "util/datetime.h"

namespace {
constexpr int kColumnOrder = 0;
constexpr int kColumnTrack = 1;
constexpr int kColumnComment = 2;
} // namespace

DlgRelationInfo::DlgRelationInfo(
        const RelationsDao::RelationGroup& group,
        Library* pLibrary)
        : QDialog(nullptr),
          m_group(group),
          m_pLibrary(pLibrary) {
    init();
}

void DlgRelationInfo::init() {
    setupUi(this);
    setWindowIcon(QIcon(MIXXX_ICON_PATH));

    connect(btnOK, &QPushButton::clicked, this, &DlgRelationInfo::slotOk);
    connect(btnCancel, &QPushButton::clicked, this, &DlgRelationInfo::slotCancel);
    connect(btnRemoveTrack, &QPushButton::clicked, this, &DlgRelationInfo::slotRemoveTrack);

    tableRelationTracks->setColumnCount(3);
    tableRelationTracks->setHorizontalHeaderLabels({tr("#"), tr("Track"), tr("Comment")});
    tableRelationTracks->verticalHeader()->setVisible(false);
    tableRelationTracks->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableRelationTracks->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableRelationTracks->horizontalHeader()->setSectionResizeMode(
            kColumnOrder, QHeaderView::ResizeToContents);
    tableRelationTracks->horizontalHeader()->setSectionResizeMode(
            kColumnTrack, QHeaderView::ResizeToContents);
    tableRelationTracks->horizontalHeader()->setStretchLastSection(true);

    // Sort the tracks of the group by clicking a header: # = order in which
    // the tracks were saved, Track = artist - title - album - year,
    // Comment = order / value in the comment. Clicking the same header again
    // reverses the order. The table is sorted once per click, so editing a
    // comment does not make the rows jump.
    tableRelationTracks->horizontalHeader()->setSectionsClickable(true);
    tableRelationTracks->horizontalHeader()->setSortIndicatorShown(true);
    connect(tableRelationTracks->horizontalHeader(),
            &QHeaderView::sectionClicked,
            this,
            [this](int section) {
                // The header has already flipped its sort indicator when this
                // signal arrives (ascending for a new column, reversed for the
                // same column), so just sort with the order it shows
                tableRelationTracks->sortItems(
                        section,
                        tableRelationTracks->horizontalHeader()->sortIndicatorOrder());
            });

    if (!m_pLibrary) {
        return;
    }

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();

    // relation type name
    RelationsDao::Relation rel = relationsDao.relationById(m_group.relationId);
    txtRelationType->setText(rel.type);

    txtRelationComment->setPlainText(m_group.comment);
    txtRelationDateAdded->setText(QLocale().toString(
            mixxx::localDateTimeFromUtc(m_group.dateAdded),
            QLocale::ShortFormat));

    // track list
    const QList<RelationsDao::GroupTrack> groupTracks = relationsDao.groupTracks(m_group.id);
    tableRelationTracks->setRowCount(groupTracks.size());
    int row = 0;
    for (const RelationsDao::GroupTrack& groupTrack : groupTracks) {
        m_originalTrackIds.insert(groupTrack.trackId);

        TrackPointer pTrack =
                m_pLibrary->trackCollectionManager()->getTrackById(
                        groupTrack.trackId);
        const QString display = pTrack
                ? DlgSaveRelation::trackLabel(pTrack->getArtist(),
                          pTrack->getTitle(),
                          pTrack->getAlbum(),
                          pTrack->getYear())
                : tr("Track %1 (not in library)").arg(groupTrack.trackId.toString());

        auto* pOrderItem = new QTableWidgetItem();
        pOrderItem->setData(Qt::DisplayRole, row + 1);
        pOrderItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        tableRelationTracks->setItem(row, kColumnOrder, pOrderItem);

        auto* pTrackItem = new RelationSortItem(display, false);
        pTrackItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        pTrackItem->setData(Qt::UserRole, groupTrack.trackId.toVariant());
        tableRelationTracks->setItem(row, kColumnTrack, pTrackItem);

        auto* pCommentItem = new RelationSortItem(groupTrack.comment, true);
        pCommentItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        tableRelationTracks->setItem(row, kColumnComment, pCommentItem);
        ++row;
    }
    tableRelationTracks->horizontalHeader()->setSortIndicator(kColumnOrder, Qt::AscendingOrder);
}

void DlgRelationInfo::slotRemoveTrack() {
    const QModelIndexList selectedRows = tableRelationTracks->selectionModel()->selectedRows();
    QList<int> rows;
    rows.reserve(selectedRows.size());
    for (const QModelIndex& index : selectedRows) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : std::as_const(rows)) {
        tableRelationTracks->removeRow(row);
    }
}

void DlgRelationInfo::slotOk() {
    if (!saveRelation()) {
        return;
    }
    accept();
}

void DlgRelationInfo::slotCancel() {
    reject();
}

bool DlgRelationInfo::saveRelation() {
    if (!m_pLibrary) {
        return true;
    }

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();

    QList<RelationsDao::GroupTrack> remainingTracks;
    QSet<TrackId> remainingIds;
    for (int row = 0; row < tableRelationTracks->rowCount(); ++row) {
        QTableWidgetItem* pTrackItem = tableRelationTracks->item(row, kColumnTrack);
        QTableWidgetItem* pCommentItem = tableRelationTracks->item(row, kColumnComment);
        if (!pTrackItem) {
            continue;
        }
        const TrackId trackId(pTrackItem->data(Qt::UserRole));
        remainingTracks.append(RelationsDao::GroupTrack{
                trackId,
                pCommentItem ? pCommentItem->text().trimmed() : QString()});
        remainingIds.insert(trackId);
    }

    if (remainingTracks.size() < 2) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
                this,
                tr("Delete Relation"),
                tr("A relation needs at least 2 tracks. Delete the whole relation?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return false;
        }
        relationsDao.removeGroup(m_group.id);
        return true;
    }

    // Save the new group comment
    relationsDao.updateGroupComment(m_group.id, txtRelationComment->toPlainText().trimmed());

    for (const TrackId& trackId : std::as_const(m_originalTrackIds)) {
        if (!remainingIds.contains(trackId)) {
            relationsDao.removeTrackFromGroup(m_group.id, trackId);
        }
    }
    for (const RelationsDao::GroupTrack& remaining : std::as_const(remainingTracks)) {
        relationsDao.setTrackComment(m_group.id, remaining.trackId, remaining.comment);
    }
    return true;
}
