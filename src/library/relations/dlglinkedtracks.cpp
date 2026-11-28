#include "library/relations/dlglinkedtracks.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QPointer>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "library/dao/relationsdao.h"
#include "library/library.h"
#include "library/relations/relationformat.h"
#include "library/relations/relationsortitem.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlglinkedtracks.cpp"
#include "track/track.h"

namespace {
constexpr int kColumnRelationType = 0;
constexpr int kColumnGroup = 1;
constexpr int kColumnGroupComment = 2;
constexpr int kColumnArtist = 3;
constexpr int kColumnTitle = 4;
constexpr int kColumnAlbum = 5;
constexpr int kColumnFileType = 6;
constexpr int kColumnYear = 7;
constexpr int kColumnBPM = 8;
constexpr int kColumnKey = 9;
constexpr int kColumnComment = 10;
constexpr int kColumnCount = 11;

QByteArray s_lastGeometry;
// The relation type chosen in the type selector, kept for the next window
QString s_lastRelationType;

// BPM with 2 decimals, right aligned like in the track table
RelationSortItem* makeBpmItem(const RelationsDao::TrackRelationRow& relationRow) {
    auto* pItem = new RelationSortItem(relationformat::bpm(relationRow.bpm), true);
    pItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignRight);
    return pItem;
}

// Key in the notation chosen by the user, with the key colour when enabled.
// Sorts on the leading number first, so 8A comes before 10A.
RelationSortItem* makeKeyItem(const RelationsDao::TrackRelationRow& relationRow) {
    auto* pItem = new RelationSortItem(
            relationformat::key(relationRow.key, relationRow.keyId), true);
    const QColor color = relationformat::keyColor(relationRow.keyId);
    if (color.isValid()) {
        pItem->setData(Qt::DecorationRole, relationformat::colorSwatch(color));
    }
    return pItem;
}
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
          m_pTableLinkedTracks(new QTableWidget(this)) {
    setAttribute(Qt::WA_DeleteOnClose);

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();

    TrackPointer pTrack = m_pLibrary->trackCollectionManager()->getTrackById(trackId);
    setWindowTitle(pTrack
                    ? tr("Linked Tracks: %1 - %2").arg(pTrack->getArtist(), pTrack->getTitle())
                    : tr("Linked Tracks"));

    auto* pLayout = new QVBoxLayout(this);
    auto* pTypeLayout = new QHBoxLayout();
    pTypeLayout->addWidget(new QLabel(tr("Relation type:"), this));
    m_pComboBoxLinkedTracksType = new QComboBox(this);
    pTypeLayout->addWidget(m_pComboBoxLinkedTracksType);
    pTypeLayout->addStretch();
    pLayout->addLayout(pTypeLayout);
    pLayout->addWidget(m_pTableLinkedTracks);
    pLayout->addWidget(new QLabel(
            tr("Click a track to select it in the library. "
               "Click a column header to sort."),
            this));

    m_pTableLinkedTracks->setColumnCount(kColumnCount);
    m_pTableLinkedTracks->setHorizontalHeaderLabels({tr("Type"),
            tr("Group"),
            tr("Group Comment"),
            tr("Artist"),
            tr("Title"),
            tr("Album"),
            tr("FileType"),
            tr("Year"),
            tr("BPM"),
            tr("Key"),
            tr("Comment")});
    m_pTableLinkedTracks->verticalHeader()->setVisible(false);
    m_pTableLinkedTracks->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTableLinkedTracks->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pTableLinkedTracks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pTableLinkedTracks->horizontalHeader()->setStretchLastSection(true);

    const QList<RelationsDao::TrackRelationRow> rows =
            relationsDao.relationsForTrack(trackId);
    m_pTableLinkedTracks->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const RelationsDao::TrackRelationRow& relationRow = rows.at(row);

        auto* pTypeItem = new RelationSortItem(relationRow.relationType, false);
        // The linked track of this row
        pTypeItem->setData(Qt::UserRole, relationRow.linkedTrackId.toVariant());
        m_pTableLinkedTracks->setItem(row, kColumnRelationType, pTypeItem);

        auto* pGroupItem = new QTableWidgetItem();
        pGroupItem->setData(Qt::DisplayRole, relationRow.groupId.toVariant());
        m_pTableLinkedTracks->setItem(row, kColumnGroup, pGroupItem);

        m_pTableLinkedTracks->setItem(row,
                kColumnGroupComment,
                new RelationSortItem(
                        relationformat::expandComment(relationRow.groupComment),
                        true));
        m_pTableLinkedTracks->setItem(row,
                kColumnArtist,
                new RelationSortItem(relationRow.artist, false));
        m_pTableLinkedTracks->setItem(row,
                kColumnTitle,
                new RelationSortItem(relationRow.title, false));
        m_pTableLinkedTracks->setItem(row,
                kColumnAlbum,
                new RelationSortItem(relationRow.album, false));
        m_pTableLinkedTracks->setItem(row,
                kColumnFileType,
                new RelationSortItem(relationRow.filetype, false));
        m_pTableLinkedTracks->setItem(row,
                kColumnBPM,
                makeBpmItem(relationRow));
        m_pTableLinkedTracks->setItem(row,
                kColumnKey,
                makeKeyItem(relationRow));
        m_pTableLinkedTracks->setItem(row,
                kColumnYear,
                new RelationSortItem(relationRow.year.trimmed().left(4), true));
        m_pTableLinkedTracks->setItem(row,
                kColumnComment,
                new RelationSortItem(relationformat::expandComment(
                                             relationRow.groupTrackComment),
                        true));
    }
    m_pTableLinkedTracks->resizeColumnsToContents();

    // Initial order: type, then the order of the query (group, artist, title)
    m_pTableLinkedTracks->horizontalHeader()->setSortIndicator(
            kColumnRelationType, Qt::AscendingOrder);
    m_pTableLinkedTracks->setSortingEnabled(true);

    // Type selector: show only the linked tracks of one relation type
    QMap<QString, int> trackCountByType;
    for (const RelationsDao::TrackRelationRow& typeRow : rows) {
        ++trackCountByType[typeRow.relationType];
    }
    m_pComboBoxLinkedTracksType->addItem(tr("All types (%1)").arg(rows.size()), QString());
    int selectedTypeIndex = 0;
    for (auto it = trackCountByType.constBegin(); it != trackCountByType.constEnd(); ++it) {
        m_pComboBoxLinkedTracksType->addItem(
                QStringLiteral("%1 (%2)").arg(it.key()).arg(it.value()), it.key());
        if (it.key() == s_lastRelationType) {
            selectedTypeIndex = m_pComboBoxLinkedTracksType->count() - 1;
        }
    }
    const auto applyTypeFilter = [this]() {
        const QString type = m_pComboBoxLinkedTracksType->currentData().toString();
        for (int row = 0; row < m_pTableLinkedTracks->rowCount(); ++row) {
            const QTableWidgetItem* pTypeItem =
                    m_pTableLinkedTracks->item(row, kColumnRelationType);
            m_pTableLinkedTracks->setRowHidden(
                    row, !type.isEmpty() && pTypeItem && pTypeItem->text() != type);
        }
    };
    connect(m_pComboBoxLinkedTracksType,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this, applyTypeFilter](int) {
                s_lastRelationType = m_pComboBoxLinkedTracksType->currentData().toString();
                applyTypeFilter();
            });
    m_pComboBoxLinkedTracksType->setCurrentIndex(selectedTypeIndex);
    applyTypeFilter();

    connect(m_pTableLinkedTracks, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem* pItem = m_pTableLinkedTracks->item(row, kColumnRelationType);
        if (pItem) {
            selectInLibrary(TrackId(pItem->data(Qt::UserRole)));
        }
    });

    if (s_lastGeometry.isEmpty()) {
        resize(900, 500);
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
