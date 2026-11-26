#include "library/relations/dlglinkedtracks.h"

#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "library/dao/relationsdao.h"
#include "library/library.h"
#include "library/relations/relationsortitem.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlglinkedtracks.cpp"
#include "track/track.h"

namespace {
constexpr int kColumnType = 0;
constexpr int kColumnGroup = 1;
constexpr int kColumnGroupComment = 2;
constexpr int kColumnArtist = 3;
constexpr int kColumnTitle = 4;
constexpr int kColumnAlbum = 5;
constexpr int kColumnYear = 6;
constexpr int kColumnComment = 7;
constexpr int kColumnCount = 8;

QByteArray s_lastGeometry;
} // namespace

// static
void DlgLinkedTracks::showForTrack(QWidget* pParent, Library* pLibrary, TrackId trackId) {
    static QPointer<DlgLinkedTracks> s_pDialog;
    if (s_pDialog) {
        s_pDialog->close();
    }
    s_pDialog = new DlgLinkedTracks(pParent, pLibrary, trackId);
    s_pDialog->show();
}

DlgLinkedTracks::DlgLinkedTracks(QWidget* pParent, Library* pLibrary, TrackId trackId)
        : QDialog(pParent),
          m_pLibrary(pLibrary),
          m_pTable(new QTableWidget(this)) {
    setAttribute(Qt::WA_DeleteOnClose);

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();

    TrackPointer pTrack = m_pLibrary->trackCollectionManager()->getTrackById(trackId);
    setWindowTitle(pTrack
                    ? tr("Linked Tracks: %1 - %2").arg(pTrack->getArtist(), pTrack->getTitle())
                    : tr("Linked Tracks"));

    auto* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(m_pTable);
    pLayout->addWidget(new QLabel(
            tr("Click a track to select it in the library. "
               "Click a column header to sort."),
            this));

    m_pTable->setColumnCount(kColumnCount);
    m_pTable->setHorizontalHeaderLabels({tr("Type"),
            tr("Group"),
            tr("Group Comment"),
            tr("Artist"),
            tr("Title"),
            tr("Album"),
            tr("Year"),
            tr("Comment")});
    m_pTable->verticalHeader()->setVisible(false);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pTable->horizontalHeader()->setStretchLastSection(true);

    const QList<RelationsDao::TrackRelationRow> rows =
            relationsDao.relationsForTrack(trackId);
    m_pTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const RelationsDao::TrackRelationRow& relationRow = rows.at(row);

        auto* pTypeItem = new RelationSortItem(relationRow.relationType, false);
        // The linked track of this row
        pTypeItem->setData(Qt::UserRole, relationRow.linkedTrackId.toVariant());
        m_pTable->setItem(row, kColumnType, pTypeItem);

        auto* pGroupItem = new QTableWidgetItem();
        pGroupItem->setData(Qt::DisplayRole, relationRow.groupId.toVariant());
        m_pTable->setItem(row, kColumnGroup, pGroupItem);

        m_pTable->setItem(row,
                kColumnGroupComment,
                new RelationSortItem(relationRow.groupComment, true));
        m_pTable->setItem(row, kColumnArtist, new RelationSortItem(relationRow.artist, false));
        m_pTable->setItem(row, kColumnTitle, new RelationSortItem(relationRow.title, false));
        m_pTable->setItem(row, kColumnAlbum, new RelationSortItem(relationRow.album, false));
        m_pTable->setItem(row,
                kColumnYear,
                new RelationSortItem(relationRow.year.trimmed().left(4), true));
        m_pTable->setItem(row,
                kColumnComment,
                new RelationSortItem(relationRow.groupTrackComment, true));
    }
    m_pTable->resizeColumnsToContents();

    // Initial order: type, then the order of the query (group, artist, title)
    m_pTable->horizontalHeader()->setSortIndicator(kColumnType, Qt::AscendingOrder);
    m_pTable->setSortingEnabled(true);

    connect(m_pTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem* pItem = m_pTable->item(row, kColumnType);
        if (pItem) {
            selectInLibrary(TrackId(pItem->data(Qt::UserRole)));
        }
    });

    if (s_lastGeometry.isEmpty()) {
        resize(920, 380);
    } else {
        restoreGeometry(s_lastGeometry);
    }
}

void DlgLinkedTracks::done(int result) {
    s_lastGeometry = saveGeometry();
    QDialog::done(result);
}

void DlgLinkedTracks::selectInLibrary(TrackId trackId) {
    if (!trackId.isValid() || !m_pLibrary) {
        return;
    }
    // Show the Tracks view without a search filter, then select the track in it
    m_pLibrary->searchTracksInCollection(QString());
    QTimer::singleShot(0, m_pLibrary, [pLibrary = m_pLibrary, trackId]() {
        emit pLibrary->selectTrack(trackId);
    });
}
