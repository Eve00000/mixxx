#include "library/relations/dlgrelationinfo.h"

#include <QHeaderView>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <algorithm>
#include <functional>

#include "defs_urls.h"
#include "library/library.h"
#include "library/relations/dlgsaverelation.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgrelationinfo.cpp"
#include "track/track.h"
#include "util/datetime.h"

namespace {
constexpr int kColumnTrack = 0;
constexpr int kColumnComment = 1;
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

    tableTracks->setColumnCount(2);
    tableTracks->setHorizontalHeaderLabels({tr("Track"), tr("Comment")});
    tableTracks->verticalHeader()->setVisible(false);
    tableTracks->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableTracks->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableTracks->horizontalHeader()->setSectionResizeMode(
            kColumnTrack, QHeaderView::ResizeToContents);
    tableTracks->horizontalHeader()->setStretchLastSection(true);

    if (!m_pLibrary) {
        return;
    }

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();

    // relation type name
    RelationsDao::Relation rel = relationsDao.relationById(m_group.relationId);
    txtType->setText(rel.type);

    txtComment->setPlainText(m_group.comment);
    txtDateAdded->setText(QLocale().toString(
            mixxx::localDateTimeFromUtc(m_group.dateAdded),
            QLocale::ShortFormat));

    // track list
    const QList<RelationsDao::GroupTrack> groupTracks = relationsDao.groupTracks(m_group.id);
    tableTracks->setRowCount(groupTracks.size());
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

        auto* pTrackItem = new QTableWidgetItem(display);
        pTrackItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        pTrackItem->setData(Qt::UserRole, groupTrack.trackId.toVariant());
        tableTracks->setItem(row, kColumnTrack, pTrackItem);

        auto* pCommentItem = new QTableWidgetItem(groupTrack.comment);
        pCommentItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        tableTracks->setItem(row, kColumnComment, pCommentItem);
        ++row;
    }
}

void DlgRelationInfo::slotRemoveTrack() {
    const QModelIndexList selectedRows = tableTracks->selectionModel()->selectedRows();
    QList<int> rows;
    rows.reserve(selectedRows.size());
    for (const QModelIndex& index : selectedRows) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : std::as_const(rows)) {
        tableTracks->removeRow(row);
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
    for (int row = 0; row < tableTracks->rowCount(); ++row) {
        QTableWidgetItem* pTrackItem = tableTracks->item(row, kColumnTrack);
        QTableWidgetItem* pCommentItem = tableTracks->item(row, kColumnComment);
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
    relationsDao.updateGroupComment(m_group.id, txtComment->toPlainText().trimmed());

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
