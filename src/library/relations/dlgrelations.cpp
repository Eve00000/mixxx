#include "library/relations/dlgrelations.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QItemSelection>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/relations/dlgrelationinfo.h"
#include "library/relations/dlgrelationtypes.h"
#include "library/relations/dlgsaverelation.h"
#include "library/relations/relationautogenerator.h"
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

    connect(pushButtonRelationSave,
            &QPushButton::clicked,
            this,
            &DlgRelations::slotSaveRelationFromDecks);
    connect(pushButtonRelationEdit, &QPushButton::clicked, this, &DlgRelations::slotEditRelation);
    connect(pushButtonRelationDelete,
            &QPushButton::clicked,
            this,
            &DlgRelations::slotDeleteRelation);
    connect(pushButtonRelationTypes,
            &QPushButton::clicked,
            this,
            &DlgRelations::slotManageRelationTypes);
    connect(pushButtonRelationAuto,
            &QPushButton::clicked,
            this,
            &DlgRelations::slotAutoCreateRelations);
    // The button is only available when the library table has the columns of
    // at least one rule (not in the standard database)
    pushButtonRelationAuto->setVisible(
            !RelationAutoGenerator::availableRules(
                    m_pLibrary->trackCollectionManager()->internalCollection()->database())
                    .isEmpty());
    connect(comboBoxRelationType,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgRelations::slotTypeFilterChanged);
    refreshTypeFilter();
    connect(pushButtonRelationRemove,
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
        // The relation may use a new type
        refreshTypeFilter();
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

void DlgRelations::slotManageRelationTypes() {
    DlgRelationTypes dlgRelationTypes(this, m_pLibrary);
    dlgRelationTypes.exec();
    // Types may have been renamed, merged or deleted
    refreshTypeFilter();
    m_pRelationTableModel->select();
    activateButtons(false);
}

void DlgRelations::refreshTypeFilter() {
    // Keep the chosen type when the list of types is rebuilt
    const DbId selectedId = m_pRelationTableModel->typeFilter();
    const RelationsDao& relationsDao = m_pLibrary->trackCollectionManager()
                                               ->internalCollection()
                                               ->getRelationsDAO();
    const QList<RelationsDao::Relation> relations = [&]() -> QList<RelationsDao::Relation> {
        const TrackPointer pDeckTrack = m_pRelationTableModel->deckTrack();
        if (!pDeckTrack) {
            return relationsDao.allRelations();
        }
        // Deck view: only the types this track has relations of
        QList<RelationsDao::Relation> trackRelations;
        QList<DbId> seenIds;
        const QList<RelationsDao::TrackRelationRow> trackRows =
                relationsDao.relationsForTrack(pDeckTrack->getId());
        for (const RelationsDao::TrackRelationRow& trackRow : trackRows) {
            if (!seenIds.contains(trackRow.relationId)) {
                seenIds.append(trackRow.relationId);
                trackRelations.append(RelationsDao::Relation{
                        trackRow.relationId,
                        trackRow.relationType,
                        trackRow.relationDescription,
                        QDateTime()});
            }
        }
        return trackRelations;
    }();

    const QSignalBlocker blocker(comboBoxRelationType);
    comboBoxRelationType->clear();
    comboBoxRelationType->addItem(tr("All types"), QVariant());
    int selectedIndex = 0;
    for (const RelationsDao::Relation& relation : relations) {
        comboBoxRelationType->addItem(relation.type, relation.id.toVariant());
        if (relation.id == selectedId) {
            selectedIndex = comboBoxRelationType->count() - 1;
        }
    }
    comboBoxRelationType->setCurrentIndex(selectedIndex);

    // The chosen type may have been deleted: show all types again
    if (selectedIndex == 0 && selectedId.isValid()) {
        m_pRelationTableModel->setTypeFilter(DbId());
    }
}

void DlgRelations::slotAutoCreateRelations() {
    TrackCollection* pCollection = m_pLibrary->trackCollectionManager()->internalCollection();
    const QList<RelationAutoGenerator::Rule> rules =
            RelationAutoGenerator::availableRules(pCollection->database());
    if (rules.isEmpty()) {
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Create Relations Automatically"));
    auto* pLayout = new QVBoxLayout(&dlg);
    pLayout->addWidget(new QLabel(
            tr("Create relations between the tracks that have the same:"), &dlg));
    QList<QCheckBox*> checkBoxes;
    for (const RelationAutoGenerator::Rule& rule : rules) {
        auto* pCheckBox = new QCheckBox(
                tr("%1  (same %2)")
                        .arg(rule.relationType,
                                rule.table + QLatin1Char('.') + rule.keyColumn),
                &dlg);

        pCheckBox->setChecked(rule.enabledByDefault);
        pLayout->addWidget(pCheckBox);
        checkBoxes.append(pCheckBox);
    }
    auto* pNote = new QLabel(
            tr("Relations that already exist are not created again. An existing "
               "relation is only extended with tracks that are new in the collection."),
            &dlg);
    pNote->setWordWrap(true);
    pLayout->addWidget(pNote);
    auto* pButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    pLayout->addWidget(pButtons);
    connect(pButtons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(pButtons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QList<RelationAutoGenerator::Rule> selectedRules;
    for (int i = 0; i < rules.size(); ++i) {
        if (checkBoxes.at(i)->isChecked()) {
            selectedRules.append(rules.at(i));
        }
    }
    if (selectedRules.isEmpty()) {
        return;
    }

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<RelationAutoGenerator::RuleResult> results = RelationAutoGenerator::apply(
            pCollection->database(), pCollection->getRelationsDAO(), selectedRules);
    QGuiApplication::restoreOverrideCursor();

    QStringList lines;
    for (const RelationAutoGenerator::RuleResult& result : results) {
        QString line = tr("%1: %2 new, %3 extended, %4 unchanged")
                               .arg(result.relationType)
                               .arg(result.createdGroups)
                               .arg(result.extendedGroups)
                               .arg(result.unchangedGroups);
        if (result.failedGroups > 0) {
            line += tr(", %1 failed (see the log)").arg(result.failedGroups);
        }
        lines.append(line);
    }
    QMessageBox::information(this,
            tr("Create Relations Automatically"),
            lines.join(QLatin1Char('\n')));

    // New relation types may have been created
    refreshTypeFilter();
    m_pRelationTableModel->select();
    activateButtons(false);
}

void DlgRelations::slotTypeFilterChanged(int index) {
    m_pRelationTableModel->setTypeFilter(DbId(comboBoxRelationType->itemData(index)));
    activateButtons(false);
}

void DlgRelations::onShow() {
    // New types may have been created since the view was last shown
    refreshTypeFilter();
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
    // The track of the deck changed: show the types of this track
    refreshTypeFilter();
}

void DlgRelations::activateButtons(bool enable) {
    pushButtonRelationSave->setEnabled(true);
    pushButtonRelationEdit->setEnabled(enable);
    pushButtonRelationDelete->setEnabled(enable);
    pushButtonRelationRemove->setEnabled(enable);
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
