#include "library/relations/dlgrelations.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/relations/dlgrelationinfo.h"
#include "library/relations/dlgsaverelation.h"
#include "library/relations/relationstablemodel.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_dlgrelations.cpp"
#include "track/track.h"
#include "util/assert.h"
#include "widget/wlibrary.h"
#include "widget/wrelationtableview.h"

DlgRelations::DlgRelations(
        WLibrary* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        KeyboardEventFilter* pKeyboard,
        bool relationGroupView)
        : QWidget(parent),
          Ui::DlgRelations(),
          m_pRelationTableView(
                  new WRelationTableView(
                          this,
                          pConfig,
                          pLibrary,
                          parent->getTrackTableBackgroundColorOpacity(),
                          relationGroupView)),
          m_pLibrary(pLibrary),
          m_bRelationGroupView(relationGroupView) {
    setupUi(this);
    m_pRelationTableView->installEventFilter(pKeyboard);

    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    VERIFY_OR_DEBUG_ASSERT(box) {
    }
    else {
        box->removeWidget(m_pRelationTablePlaceholder);
        m_pRelationTablePlaceholder->hide();
        box->insertWidget(1, m_pRelationTableView);
    }

    m_pRelationTableModel = new RelationsTableModel(this, pLibrary->trackCollectionManager());
    m_pRelationTableView->loadTrackModel(m_pRelationTableModel);

    connect(pushButtonSave, &QPushButton::clicked, this, &DlgRelations::slotSaveRelationFromDecks);
    connect(pushButtonEdit, &QPushButton::clicked, this, &DlgRelations::slotEditRelation);
    connect(pushButtonDelete, &QPushButton::clicked, this, &DlgRelations::slotDeleteRelation);
    connect(pushButtonRemove,
            &QPushButton::clicked,
            this,
            &DlgRelations::slotRemoveTrackFromRelation);
    connect(m_pRelationTableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            &DlgRelations::selectionChanged);
    connect(m_pRelationTableView,
            &WRelationTableView::trackSelected,
            this,
            &DlgRelations::trackSelected);
    connect(m_pRelationTableView,
            &WRelationTableView::loadTrack,
            this,
            &DlgRelations::loadTrack);
    connect(m_pRelationTableView,
            &WRelationTableView::loadTrackToPlayer,
            this,
            &DlgRelations::loadTrackToPlayer);

    connect(pLibrary,
            &Library::setTrackTableFont,
            m_pRelationTableView,
            &WRelationTableView::setTrackTableFont);
    connect(pLibrary,
            &Library::setTrackTableRowHeight,
            m_pRelationTableView,
            &WRelationTableView::setTrackTableRowHeight);
    connect(pLibrary,
            &Library::setSelectedClick,
            m_pRelationTableView,
            &WRelationTableView::setSelectedClick);
}

DlgRelations::~DlgRelations() {
    delete m_pRelationTableView;
    delete m_pRelationTableModel;
}

void DlgRelations::slotEditRelation() {
    RelationsDao::RelationGroup group = m_pRelationTableView->getSelectedGroup();
    if (!group.id.isValid()) {
        return;
    }
    DlgRelationInfo dlgRelationInfo(group, m_pLibrary);
    if (dlgRelationInfo.exec() == QDialog::Accepted) {
        m_pRelationTableModel->select();
        activateButtons(false);
    }
}

void DlgRelations::slotSaveRelationFromDecks() {
    constexpr int kMaxDecks = 4;

    QList<DlgSaveRelation::Entry> entries;
    QSet<TrackId> seenTrackIds;
    for (int i = 0; i < kMaxDecks; ++i) {
        TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(
                PlayerManager::groupForDeck(i));
        if (!pTrack || !pTrack->getId().isValid() ||
                seenTrackIds.contains(pTrack->getId())) {
            continue;
        }
        seenTrackIds.insert(pTrack->getId());
        entries.append(DlgSaveRelation::Entry{pTrack, tr("Deck %1").arg(i + 1)});
    }

    if (entries.size() < 2) {
        QMessageBox::information(this,
                tr("Save Relation"),
                tr("Load at least 2 library tracks into the decks to create a relation."));
        return;
    }

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();
    const DbId groupId = DlgSaveRelation::execute(this, relationsDao, entries);
    if (groupId.isValid()) {
        m_pRelationTableModel->select();
    }
}

void DlgRelations::slotDeleteRelation() {
    const QList<WRelationTableView::GroupTrack> selected =
            m_pRelationTableView->getSelectedGroupTracks();
    if (selected.isEmpty()) {
        return;
    }

    QList<DbId> groupIds;
    for (const WRelationTableView::GroupTrack& item : selected) {
        if (!groupIds.contains(item.groupId)) {
            groupIds.append(item.groupId);
        }
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Delete Relation"),
            tr("Delete the %1 selected relation(s) completely? "
               "The tracks themselves are not affected.")
                    .arg(groupIds.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();
    for (const DbId& groupId : std::as_const(groupIds)) {
        relationsDao.removeGroup(groupId);
    }

    m_pRelationTableModel->select();
    activateButtons(false);
}

void DlgRelations::slotRemoveTrackFromRelation() {
    const QList<WRelationTableView::GroupTrack> selected =
            m_pRelationTableView->getSelectedGroupTracks();
    if (selected.isEmpty()) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Remove Track from Relation"),
            tr("Remove the %1 selected track(s) from their relation?\n\n"
               "A relation needs at least 2 tracks. A relation that is left "
               "with fewer than 2 tracks is deleted.")
                    .arg(selected.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                         ->internalCollection()
                                         ->getRelationsDAO();
    for (const WRelationTableView::GroupTrack& item : selected) {
        relationsDao.removeTrackFromGroup(item.groupId, item.trackId);
    }

    m_pRelationTableModel->select();
    activateButtons(false);
}

void DlgRelations::onShow() {
    m_pRelationTableModel->select();
    activateButtons(false);
}

void DlgRelations::onSearch(const QString& text) {
    m_pRelationTableModel->searchRelations(text);
}

QString DlgRelations::currentSearch() {
    return m_pRelationTableModel->relationSearch();
}

void DlgRelations::showAllRelations() {
    m_pRelationTableModel->showAllRelations();
}

void DlgRelations::showRelatedTracks(TrackPointer pTrack) {
    m_pRelationTableModel->showRelatedTracks(pTrack);
}

void DlgRelations::activateButtons(bool enable) {
    pushButtonSave->setEnabled(true);
    pushButtonEdit->setEnabled(enable);
    pushButtonDelete->setEnabled(enable);
    pushButtonRemove->setEnabled(enable);
}

void DlgRelations::selectionChanged(const QItemSelection& selected,
        const QItemSelection& deselected) {
    Q_UNUSED(deselected);
    activateButtons(!selected.indexes().isEmpty());
}

bool DlgRelations::hasFocus() const {
    return m_pRelationTableView->hasFocus();
}

void DlgRelations::saveCurrentViewState() {
    m_pRelationTableView->saveCurrentViewState();
}

bool DlgRelations::restoreCurrentViewState() {
    return m_pRelationTableView->restoreCurrentViewState();
}

void DlgRelations::setFocus() {
    m_pRelationTableView->setFocus();
}

void DlgRelations::installEventFilter(QObject* pFilter) {
    QWidget::installEventFilter(pFilter);
    m_pRelationTableView->installEventFilter(pFilter);
}
