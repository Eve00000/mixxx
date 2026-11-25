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
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgrelationinfo.cpp"
#include "track/track.h"
#include "util/datetime.h"

namespace {
constexpr int kColumnOrder = 0;
constexpr int kColumnTrack = 1;
constexpr int kColumnComment = 2;

// Leading number of a text ("117", "-1.2%"), 0 when the text does not start
// with a number. Used to sort comments that start with a value.
double leadingNumber(const QString& text) {
    static const QRegularExpression kNumber(
            QStringLiteral("^\\s*([-+]?\\d+(?:[.,]\\d+)?)"));
    const QRegularExpressionMatch match = kNumber.match(text);
    if (!match.hasMatch()) {
        return 0.0;
    }
    QString number = match.captured(1);
    number.replace(QLatin1Char(','), QLatin1Char('.'));
    return number.toDouble();
}

// Table item that sorts like the relation list: optionally on the leading
// number first, then on the text (numbers in the text in numeric order)
class SortItem : public QTableWidgetItem {
  public:
    SortItem(const QString& text, bool numberFirst)
            : QTableWidgetItem(text),
              m_numberFirst(numberFirst) {
    }

    bool operator<(const QTableWidgetItem& other) const override {
        if (m_numberFirst) {
            const double a = leadingNumber(text());
            const double b = leadingNumber(other.text());
            if (a < b) {
                return true;
            }
            if (a > b) {
                return false;
            }
        }
        static const QCollator kCollator = []() {
            QCollator collator;
            collator.setNumericMode(true);
            collator.setCaseSensitivity(Qt::CaseInsensitive);
            return collator;
        }();
        return kCollator.compare(text(), other.text()) < 0;
    }

  private:
    bool m_numberFirst;
};
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

    tableTracks->setColumnCount(3);
    tableTracks->setHorizontalHeaderLabels({tr("#"), tr("Track"), tr("Comment")});
    tableTracks->verticalHeader()->setVisible(false);
    tableTracks->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableTracks->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableTracks->horizontalHeader()->setSectionResizeMode(
            kColumnOrder, QHeaderView::ResizeToContents);
    tableTracks->horizontalHeader()->setSectionResizeMode(
            kColumnTrack, QHeaderView::ResizeToContents);
    tableTracks->horizontalHeader()->setStretchLastSection(true);

    // Sort the tracks of the group by clicking a header: # = order in which
    // the tracks were saved, Track = artist - title - album - year,
    // Comment = order / value in the comment. Clicking the same header again
    // reverses the order. The table is sorted once per click, so editing a
    // comment does not make the rows jump.
    tableTracks->horizontalHeader()->setSectionsClickable(true);
    tableTracks->horizontalHeader()->setSortIndicatorShown(true);
    connect(tableTracks->horizontalHeader(),
            &QHeaderView::sectionClicked,
            this,
            [this](int section) {
                // The header has already flipped its sort indicator when this
                // signal arrives (ascending for a new column, reversed for the
                // same column), so just sort with the order it shows
                tableTracks->sortItems(
                        section,
                        tableTracks->horizontalHeader()->sortIndicatorOrder());
            });

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

        auto* pOrderItem = new QTableWidgetItem();
        pOrderItem->setData(Qt::DisplayRole, row + 1);
        pOrderItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        tableTracks->setItem(row, kColumnOrder, pOrderItem);

        auto* pTrackItem = new SortItem(display, false);
        pTrackItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        pTrackItem->setData(Qt::UserRole, groupTrack.trackId.toVariant());
        tableTracks->setItem(row, kColumnTrack, pTrackItem);

        auto* pCommentItem = new SortItem(groupTrack.comment, true);
        pCommentItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        tableTracks->setItem(row, kColumnComment, pCommentItem);
        ++row;
    }
    tableTracks->horizontalHeader()->setSortIndicator(kColumnOrder, Qt::AscendingOrder);
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
