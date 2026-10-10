#include "library/trackset/searchcrate/groupedsearchcratesfeature.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>
#include <algorithm>
#include <functional>
#include <vector>

#include "analyzer/analyzerscheduledtrack.h"
#include "library/export/trackexportwizard.h"
#include "library/library.h"
#include "library/library_prefs.h"
#include "library/parser.h"
#include "library/parsercsv.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/searchcrate/searchcratefeaturehelper.h"
#include "library/trackset/searchcrate/searchcratesummary.h"
#include "library/treeitem.h"
#include "moc_groupedsearchcratesfeature.cpp"
#include "sources/soundsourceproxy.h"
#include "track/track.h"
#include "util/defs.h"
#include "util/dnd.h"
#include "util/file.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"
#include "widget/wlibrarytextbrowser.h"

namespace {
const bool sDebugGroupedSearchCratesFeature = false;
// constexpr int kInvalidSearchCrateId = -1;
QString formatLabel(
        const SearchCrateSummary& searchCrateSummary) {
    return QStringLiteral("%1 (%2) %3")
            .arg(
                    searchCrateSummary.getName(),
                    QString::number(searchCrateSummary.getTrackCount()),
                    searchCrateSummary.getTrackDurationText());
}

const ConfigKey kConfigKeyLastImportExportSearchCrateDirectoryKey(
        "[Library]", "LastImportExportSearchCrateDirectory");

} // anonymous namespace

using namespace mixxx::library::prefs;

GroupedSearchCratesFeature::GroupedSearchCratesFeature(Library* pLibrary,
        UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary, pConfig, "SEARCHCRATEHOME", QStringLiteral("searchcrates")),
          m_lockedSearchCrateIcon(":/images/library/ic_library_locked_tracklist.svg"),
          m_pTrackCollection(pLibrary->trackCollectionManager()->internalCollection()),
          m_searchCrateTableModel(this, pLibrary->trackCollectionManager()) {
    initActions();

    // construct child model
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    rebuildChildModel();

    connectLibrary(pLibrary);
    connectTrackCollection();
}

void GroupedSearchCratesFeature::initActions() {
    m_pCreateSearchCrateAction = make_parented<QAction>(tr("Create New SearchCrate"), this);
    connect(m_pCreateSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotCreateSearchCrate);
    m_pEditSearchCrateAction = make_parented<QAction>(tr("Edit SearchCrate"), this);
    connect(m_pEditSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotEditSearchCrate);
    m_pRenameSearchCrateAction = make_parented<QAction>(tr("Rename"), this);
    connect(m_pRenameSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotRenameSearchCrate);
    m_pDuplicateSearchCrateAction = make_parented<QAction>(tr("Duplicate"), this);
    connect(m_pDuplicateSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotDuplicateSearchCrate);
    m_pDeleteSearchCrateAction = make_parented<QAction>(tr("Remove"), this);
    const auto removeKeySequence =
            // TODO(XXX): Qt6 replace enum | with QKeyCombination
            QKeySequence(static_cast<int>(kHideRemoveShortcutModifier) |
                    kHideRemoveShortcutKey);
    m_pDeleteSearchCrateAction->setShortcut(removeKeySequence);
    connect(m_pDeleteSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotDeleteSearchCrate);
    m_pLockSearchCrateAction = make_parented<QAction>(tr("Lock"), this);
    connect(m_pLockSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotToggleSearchCrateLock);
    m_pAutoDjTrackSourceAction = make_parented<QAction>(tr("Auto DJ Track Source"), this);
    m_pAutoDjTrackSourceAction->setCheckable(true);

    m_pAnalyzeSearchCrateAction = make_parented<QAction>(tr("Analyze entire SearchCrate"), this);
    connect(m_pAnalyzeSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotAnalyzeSearchCrate);

    m_pExportPlaylistAction = make_parented<QAction>(tr("Export SearchCrate as Playlist"), this);
    connect(m_pExportPlaylistAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotExportPlaylist);
    m_pExportTrackFilesAction = make_parented<QAction>(tr("Export Track Files"), this);
    connect(m_pExportTrackFilesAction.get(),
            &QAction::triggered,
            this,
            &GroupedSearchCratesFeature::slotExportTrackFiles);
#ifdef __ENGINEPRIME__
    // Engine DJ export needs to be adapted for searchCrates first
    // m_pExportAllSearchCratesAction = make_parented<QAction>(tr("Export to Engine DJ"), this);
    // connect(m_pExportAllSearchCratesAction.get(),
    // &QAction::triggered,
    // this,
    // &GroupedSearchCratesFeature::exportAllSearchCrates);
    // m_pExportSearchCrateAction = make_parented<QAction>(tr("Export to Engine DJ"), this);
    connect(m_pExportSearchCrateAction.get(),
            &QAction::triggered,
            this,
            [this]() {
                SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
                if (searchCrateId.isValid()) {
                    emit exportSearchCrate(searchCrateId);
                }
            });
#endif
}

void GroupedSearchCratesFeature::connectLibrary(Library* pLibrary) {
    connect(pLibrary,
            &Library::trackSelected,
            this,
            [this](const TrackPointer& pTrack) {
                const auto trackId = pTrack ? pTrack->getId() : TrackId{};
                slotTrackSelected(trackId);
            });
    connect(pLibrary,
            &Library::switchToView,
            this,
            &GroupedSearchCratesFeature::slotResetSelectedTrack);
}

void GroupedSearchCratesFeature::connectTrackCollection() {
    connect(m_pTrackCollection, // created new, duplicated or imported playlist to new SearchCrate
            &TrackCollection::searchCrateInserted,
            this,
            &GroupedSearchCratesFeature::slotSearchCrateTableChanged);
    connect(m_pTrackCollection, // renamed, un/locked, toggled AutoDJ source
            &TrackCollection::searchCrateUpdated,
            this,
            &GroupedSearchCratesFeature::slotSearchCrateTableChanged);
    connect(m_pTrackCollection,
            &TrackCollection::searchCrateDeleted,
            this,
            &GroupedSearchCratesFeature::slotSearchCrateTableChanged);
    connect(m_pTrackCollection, // searchCrate tracks hidden, unhidden or purged
            &TrackCollection::searchCrateTracksChanged,
            this,
            &GroupedSearchCratesFeature::slotSearchCrateContentChanged);
    connect(m_pTrackCollection,
            &TrackCollection::searchCrateSummaryChanged,
            this,
            &GroupedSearchCratesFeature::slotUpdateSearchCrateLabels);
}

QVariant GroupedSearchCratesFeature::title() {
    return tr("SearchCrates (Grouped)");
}

QString GroupedSearchCratesFeature::formatRootViewHtml() const {
    QString searchCratesTitle = tr("SearchCrates");
    QString searchCratesSummary =
            tr("SearchCrates are a great way to help organize the music you want to "
               "DJ with.");
    QString searchCratesSummary2 =
            tr("Make a SearchCrate for your next gig, for your favorite electrohouse "
               "tracks, or for your most requested tracks.");
    QString searchCratesSummary3 =
            tr("SearchCrates let you organize your music however you'd like!");

    QString html;
    QString createSearchCrateLink = tr("Create New SearchCrate");
    html.append(QStringLiteral("<h2>%1</h2>").arg(searchCratesTitle));
    html.append(QStringLiteral("<p>%1</p>").arg(searchCratesSummary));
    html.append(QStringLiteral("<p>%1</p>").arg(searchCratesSummary2));
    html.append(QStringLiteral("<p>%1</p>").arg(searchCratesSummary3));
    // Colorize links in lighter blue, instead of QT default dark blue.
    // Links are still different from regular text, but readable on dark/light backgrounds.
    // https://github.com/mixxxdj/mixxx/issues/9103
    html.append(
            QStringLiteral("<a style=\"color:#0496FF;\" href=\"create\">%1</a>")
                    .arg(createSearchCrateLink));
    return html;
}

std::unique_ptr<TreeItem> GroupedSearchCratesFeature::newTreeItemForSearchCrateSummary(
        const SearchCrateSummary& searchCrateSummary) {
    auto pTreeItem = TreeItem::newRoot(this);
    updateTreeItemForSearchCrateSummary(pTreeItem.get(), searchCrateSummary);
    return pTreeItem;
}

void GroupedSearchCratesFeature::updateTreeItemForSearchCrateSummary(
        TreeItem* pTreeItem, const SearchCrateSummary& searchCrateSummary) const {
    DEBUG_ASSERT(pTreeItem != nullptr);
    if (pTreeItem->getData().isNull()) {
        pTreeItem->setData(searchCrateSummary.getId().toVariant());
    } else {
        DEBUG_ASSERT(SearchCrateId(pTreeItem->getData()) == searchCrateSummary.getId());
    }
    // Do not overwrite the label if the caller already set a trimmed one
    // (e.g. the group-prefix-stripped name for grouped display).
    if (pTreeItem->getLabel().isEmpty()) {
        pTreeItem->setLabel(formatLabel(searchCrateSummary));
    }
    pTreeItem->setIcon(searchCrateSummary.isLocked() ? m_lockedSearchCrateIcon : QIcon());
}

void GroupedSearchCratesFeature::bindLibraryWidget(
        WLibrary* libraryWidget, KeyboardEventFilter* keyboard) {
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(formatRootViewHtml());
    edit->setOpenLinks(false);
    connect(edit,
            &WLibraryTextBrowser::anchorClicked,
            this,
            &GroupedSearchCratesFeature::htmlLinkClicked);
    libraryWidget->registerView(m_rootViewName, edit);
}

void GroupedSearchCratesFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    // store the sidebar widget pointer for later use in onRightClickChild
    m_pSidebarWidget = pSidebarWidget;
}

TreeItemModel* GroupedSearchCratesFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void GroupedSearchCratesFeature::activate() {
    m_lastClickedIndex = QModelIndex();
    BaseTrackSetFeature::activate();
}

bool GroupedSearchCratesFeature::activateSearchCrate(SearchCrateId searchCrateId) {
    qDebug() << "GroupedSearchCratesFeature::activateSearchCrate()" << searchCrateId;
    VERIFY_OR_DEBUG_ASSERT(searchCrateId.isValid()) {
        return false;
    }
    if (!m_pTrackCollection->searchCrates().readSearchCrateSummaryById(searchCrateId)) {
        // this may happen if called by slotSearchCrateTableChanged()
        // and the searchCrate has just been deleted
        return false;
    }
    QModelIndex index = indexFromSearchCrateId(searchCrateId);
    VERIFY_OR_DEBUG_ASSERT(index.isValid()) {
        return false;
    }
    m_lastClickedIndex = index;
    m_lastRightClickedIndex = QModelIndex();
    m_prevSiblingSearchCrate = SearchCrateId();
    emit saveModelState();
    m_searchCrateTableModel.selectSearchCrate(searchCrateId);
    emit showTrackModel(&m_searchCrateTableModel);
    emit enableCoverArtDisplay(true);
    // Update selection
    emit featureSelect(this, m_lastClickedIndex);
    return true;
}

bool GroupedSearchCratesFeature::readLastRightClickedSearchCrate(SearchCrate* pSearchCrate) const {
    const SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
    if (!searchCrateId.isValid()) {
        // Not a crate row (e.g. a group node) � silently refuse.
        return false;
    }
    if (!m_pTrackCollection->searchCrates().readSearchCrateById(searchCrateId, pSearchCrate)) {
        qWarning() << "Failed to read selected searchCrate with id" << searchCrateId;
        return false;
    }
    return true;
}

bool GroupedSearchCratesFeature::isChildIndexSelectedInSidebar(const QModelIndex& index) {
    return m_pSidebarWidget && m_pSidebarWidget->isChildIndexSelected(index);
}

void GroupedSearchCratesFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedIndex = QModelIndex();
    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pCreateSearchCrateAction.get());
#ifdef __ENGINEPRIME__
#endif
    menu.exec(globalPos);
}

void GroupedSearchCratesFeature::onRightClickChild(
        const QPoint& globalPos, const QModelIndex& index) {
    // Save the model index so we can get it in the action slots...
    m_lastRightClickedIndex = index;
    SearchCrateId searchCrateId(searchCrateIdFromIndex(index));
    if (!searchCrateId.isValid()) {
        return;
    }

    SearchCrate searchCrate;
    if (!m_pTrackCollection->searchCrates().readSearchCrateById(searchCrateId, &searchCrate)) {
        return;
    }

    m_pDeleteSearchCrateAction->setEnabled(!searchCrate.isLocked());
    m_pRenameSearchCrateAction->setEnabled(!searchCrate.isLocked());
    // m_pImportPlaylistAction->setEnabled(!searchCrate.isLocked());

    m_pAutoDjTrackSourceAction->setChecked(searchCrate.isAutoDjSource());

    m_pLockSearchCrateAction->setText(searchCrate.isLocked() ? tr("Unlock") : tr("Lock"));
    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pCreateSearchCrateAction.get());
    menu.addSeparator();
    menu.addAction(m_pEditSearchCrateAction.get());
    menu.addSeparator();
    menu.addAction(m_pRenameSearchCrateAction.get());
    menu.addAction(m_pDuplicateSearchCrateAction.get());
    menu.addAction(m_pDeleteSearchCrateAction.get());
    menu.addAction(m_pLockSearchCrateAction.get());
    menu.addSeparator();
    menu.addAction(m_pAnalyzeSearchCrateAction.get());
    menu.addSeparator();
    menu.addAction(m_pExportPlaylistAction.get());
    menu.addAction(m_pExportTrackFilesAction.get());
#ifdef __ENGINEPRIME__
    menu.addAction(m_pExportSearchCrateAction.get());
#endif
    menu.exec(globalPos);
}

void GroupedSearchCratesFeature::slotCreateSearchCrate() {
    SearchCrateId searchCrateId =
            SearchCrateFeatureHelper(m_pTrackCollection, m_pConfig)
                    .createEmptySearchCrate();
    if (searchCrateId.isValid()) {
        // expand SearchCrates and scroll to new searchCrate
        rebuildChildModel(searchCrateId);
        m_pSidebarWidget->selectChildIndex(indexFromSearchCrateId(searchCrateId), false);
    }
}

void GroupedSearchCratesFeature::slotCreateSearchCrateFromSearch(const QString& text) {
    SearchCrateId searchCrateId =
            SearchCrateFeatureHelper(m_pTrackCollection, m_pConfig)
                    .createEmptySearchCrateFromSearch(text);

    if (searchCrateId.isValid()) {
        // expand SearchCrate and scroll to new searchCrate
        m_pSidebarWidget->selectChildIndex(indexFromSearchCrateId(searchCrateId), false);
        m_lastRightClickedIndex = indexFromSearchCrateId(searchCrateId);
        activateSearchCrate(searchCrateId);
    }
}

void GroupedSearchCratesFeature::deleteItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotDeleteSearchCrate();
}

void GroupedSearchCratesFeature::slotDeleteSearchCrate() {
    SearchCrate searchCrate;
    if (readLastRightClickedSearchCrate(&searchCrate)) {
        if (searchCrate.isLocked()) {
            qWarning() << "Refusing to delete locked searchCrate" << searchCrate;
            return;
        }
        SearchCrateId searchCrateId = searchCrate.getId();
        // Store sibling id to restore selection after searchCrate was deleted
        // to avoid the scroll position being reset to SearchCrate root item.
        m_prevSiblingSearchCrate = SearchCrateId();
        if (isChildIndexSelectedInSidebar(m_lastRightClickedIndex)) {
            storePrevSiblingSearchCrateId(searchCrateId);
        }

        QMessageBox::StandardButton btn = QMessageBox::question(nullptr,
                tr("Confirm Deletion"),
                tr("Do you really want to delete searchCrate <b>%1</b>?")
                        .arg(searchCrate.getName()),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (btn == QMessageBox::Yes) {
            if (m_pTrackCollection->deleteSearchCrate(searchCrateId)) {
                qDebug() << "Deleted searchCrate" << searchCrate;
                return;
            }
        } else {
            return;
        }
    }
    qWarning() << "Failed to delete selected SearchCrate";
}

void GroupedSearchCratesFeature::renameItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotRenameSearchCrate();
}

void GroupedSearchCratesFeature::slotRenameSearchCrate() {
    SearchCrate searchCrate;
    if (readLastRightClickedSearchCrate(&searchCrate)) {
        const QString oldName = searchCrate.getName();
        searchCrate.resetName();
        for (;;) {
            bool ok = false;
            auto newName =
                    QInputDialog::getText(nullptr,
                            tr("Rename SearchCrate"),
                            tr("Enter new name for searchCrate:"),
                            QLineEdit::Normal,
                            oldName,
                            &ok)
                            .trimmed();
            if (!ok || newName.isEmpty()) {
                return;
            }
            if (newName.isEmpty()) {
                QMessageBox::warning(nullptr,
                        tr("Renaming SearchCrate Failed"),
                        tr("A searchCrate cannot have a blank name."));
                continue;
            }
            if (m_pTrackCollection->searchCrates().readSearchCrateByName(newName)) {
                QMessageBox::warning(nullptr,
                        tr("Renaming SearchCrate Failed"),
                        tr("A SearchCrate by that name already exists."));
                continue;
            }
            searchCrate.setName(std::move(newName));
            DEBUG_ASSERT(searchCrate.hasName());
            break;
        }

        if (!m_pTrackCollection->updateSearchCrate(searchCrate)) {
            qDebug() << "Failed to rename SearchCrate" << searchCrate;
        }
    } else {
        qDebug() << "Failed to rename selected SearchCrate";
    }
}

void GroupedSearchCratesFeature::slotDuplicateSearchCrate() {
    SearchCrate searchCrate;
    if (readLastRightClickedSearchCrate(&searchCrate)) {
        SearchCrateId newSearchCrateId =
                SearchCrateFeatureHelper(m_pTrackCollection, m_pConfig)
                        .duplicateSearchCrate(searchCrate);
        if (newSearchCrateId.isValid()) {
            qDebug() << "Duplicate Searchcrate" << searchCrate
                     << ", new Searchcrate:" << newSearchCrateId;
            return;
        }
    }
    qDebug() << "Failed to duplicate selected SearchCrate";
}

void GroupedSearchCratesFeature::slotEditSearchCrate() {
    QMutex mutex;
    mutex.lock();
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] "
                    "-> slotEditSearchCrate";
    }
    SearchCrate searchCrate;
    readLastRightClickedSearchCrate(&searchCrate);
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                    "m_lastRightClickedIndex  = "
                 << m_lastRightClickedIndex;
    }
    // Load data into QVariant
    searchCrateData.clear();
    m_searchCrateTableModel.selectSearchCrate2QVL(
            searchCrateIdFromIndex(m_lastRightClickedIndex), searchCrateData);
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                    "SearchCrate data loaded into QVariantList:"
                 << searchCrateData;
    }

    QVariantList playlistsCratesData;
    m_searchCrateTableModel.selectPlaylistsCrates2QVL(playlistsCratesData);
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                    "Playlists & Crates data loaded into QVariantList:"
                 << playlistsCratesData;
    }

    if (readLastRightClickedSearchCrate(&searchCrate)) {
        SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
        if (sDebugGroupedSearchCratesFeature) {
            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                        "SlotEditSearchCrate -> searchCrateID = "
                     << searchCrateId;
        }
        // Pass this to provide the SearchCratesFeature instance
        // dlgSearchCrateInfo infoDialog(this);
        dlgGroupedSearchCratesInfo infoDialog(this);

        if (sDebugGroupedSearchCratesFeature) {
            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                        "[INIT DIALOG] -> INIT DIALOG ";
        }

        infoDialog.init(searchCrateData, playlistsCratesData);
        // DLG -> Update SearchCrate on 'Apply'
        connect(&infoDialog,
                &dlgGroupedSearchCratesInfo::dataUpdated,
                this,
                [this, searchCrateId](const QVariantList& updatedData) mutable {
                    if (sDebugGroupedSearchCratesFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[UPDATE] -> START Request UPDATE SearchCrate "
                                    "searchCrateId "
                                 << searchCrateId;
                    }
                    searchCrateData = updatedData; // Capture the updated data from the UI
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    if (sDebugGroupedSearchCratesFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] extracted "
                                    "searchCrateId from searchCrateData: "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current searchCrateId "
                                 << searchCrateId;
                    }
                    if (searchCrateId.isValid()) {
                        // Store updated data
                        m_searchCrateTableModel.saveQVL2SearchCrate(searchCrateId, searchCrateData);
                        // Send updated data back to ui-> adapted sql
                        m_searchCrateTableModel.selectSearchCrate2QVL(
                                searchCrateIdFromIndex(m_lastRightClickedIndex),
                                searchCrateData);
                        activateSearchCrate(searchCrateId);
                        m_lastClickedIndex = indexFromSearchCrateId(searchCrateId);
                        m_lastRightClickedIndex = indexFromSearchCrateId(searchCrateId);
                        slotSearchCrateTableChanged(searchCrateId);
                        if (sDebugGroupedSearchCratesFeature) {
                            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                        "[UPDATE] -> END UPDATE searchCrateId "
                                     << searchCrateId;
                        }
                        emit updateSearchCrateData(searchCrateData);
                    } else {
                        return;
                    }
                });
        // DLG -> Delete SearchCrate on 'Delete'
        connect(&infoDialog,
                &dlgGroupedSearchCratesInfo::requestDeleteSearchCrate,
                this,
                [this]() {
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    SearchCrateId previousSearchCrateId(searchCrateData[56]);
                    bool currentSearchCrateIdBOF(searchCrateData[57].toString() == "true");
                    SearchCrateId nextSearchCrateId(searchCrateData[58]);
                    bool currentSearchCrateIdEOF(searchCrateData[59].toString() == "true");
                    if (sDebugGroupedSearchCratesFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[DELETE] -> START Request DELETE SearchCrate "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] extracted "
                                    "searchCrateId from searchCrateData: "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current searchCrateId "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] previous SearchCrateId: "
                                 << previousSearchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId BOF: "
                                 << currentSearchCrateIdBOF;
                        qDebug() << "[SEARCHCRATESFEATURE] next SearchCrateId: "
                                 << nextSearchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId EOF: "
                                 << currentSearchCrateIdEOF;
                    }
                    if (!searchCrateId.isValid()) {
                        if (sDebugGroupedSearchCratesFeature) {
                            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT "
                                        "SEARCHCRATES] -> "
                                        "[DELETE] -> Invalid SearchCrateId. : "
                                     << searchCrateId;
                        }
                        return;
                    } else {
                        slotDeleteSearchCrate();
                        if (currentSearchCrateIdBOF && !currentSearchCrateIdEOF) {
                            if (nextSearchCrateId.isValid()) {
                                m_searchCrateTableModel.selectSearchCrate2QVL(
                                        nextSearchCrateId,
                                        searchCrateData);
                                emit updateSearchCrateData(searchCrateData);
                                if (sDebugGroupedSearchCratesFeature) {
                                    qDebug() << "[SEARCHCRATESFEATURE] [SLOT "
                                                "EDIT SEARCHCRATES] -> "
                                                "[DELETE] -> SearchCrate "
                                                "DELETED, new active "
                                                "searchCrate: "
                                             << nextSearchCrateId;
                                }
                                m_lastRightClickedIndex = indexFromSearchCrateId(nextSearchCrateId);
                                activateSearchCrate(nextSearchCrateId);
                            } else {
                                return;
                            }
                        } else {
                            if (previousSearchCrateId.isValid()) {
                                m_searchCrateTableModel.selectSearchCrate2QVL(
                                        previousSearchCrateId,
                                        searchCrateData);
                                emit updateSearchCrateData(searchCrateData);
                                if (sDebugGroupedSearchCratesFeature) {
                                    qDebug() << "[SEARCHCRATESFEATURE] [SLOT "
                                                "EDIT SEARCHCRATES] -> "
                                                "[DELETE] -> SearchCrate "
                                                "DELETED, new active "
                                                "searchCrate: "
                                             << previousSearchCrateId;
                                }
                                m_lastRightClickedIndex =
                                        indexFromSearchCrateId(
                                                previousSearchCrateId);
                                activateSearchCrate(previousSearchCrateId);
                            } else {
                                return;
                            }
                        }
                        slotSearchCrateTableChanged(previousSearchCrateId);
                    }
                });
        // DLG -> New SearchCrate on 'New'
        connect(&infoDialog,
                &dlgGroupedSearchCratesInfo::requestNewSearchCrate,
                this,
                [this, searchCrateId]() {
                    if (sDebugGroupedSearchCratesFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[NEW] "
                                    "-> START Request NEW SearchCrate searchCrateId "
                                 << searchCrateId;
                    }
                    SearchCrateId searchCrateId =
                            SearchCrateFeatureHelper(m_pTrackCollection, m_pConfig)
                                    .createEmptySearchCrateFromUI();
                    if (!searchCrateId.isValid()) {
                        if (sDebugGroupedSearchCratesFeature) {
                            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT "
                                        "SEARCHCRATES] -> "
                                        "[NEW] -> Creation failed.";
                        }
                        return;
                    } else {
                        if (sDebugGroupedSearchCratesFeature) {
                            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT "
                                        "SEARCHCRATES] -> "
                                        "[NEW] -> New searchCrate created. "
                                        "searchCrateId "
                                     << searchCrateId;
                        }
                    }
                    activateSearchCrate(searchCrateId);
                    searchCrateData.clear();
                    m_searchCrateTableModel.selectSearchCrate2QVL(
                            searchCrateId, searchCrateData);
                    slotSearchCrateTableChanged(searchCrateId);
                    m_lastRightClickedIndex = indexFromSearchCrateId(searchCrateId);
                    if (sDebugGroupedSearchCratesFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[NEW] "
                                    "-> END SearchCrate created searchCrateId "
                                 << searchCrateId;
                    }
                    emit updateSearchCrateData(searchCrateData);
                });
        // DLG -> Previous SearchCrate on 'Previous'
        connect(&infoDialog,
                &dlgGroupedSearchCratesInfo::requestPreviousSearchCrate,
                this,
                [this]() {
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    SearchCrateId previousSearchCrateId(searchCrateData[56]);
                    bool currentSearchCrateIdBOF(searchCrateData[57].toString() == "true");
                    SearchCrateId nextSearchCrateId(searchCrateData[58]);
                    bool currentSearchCrateIdEOF(searchCrateData[59].toString() == "true");
                    if (sDebugGroupedSearchCratesFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[PREVIOUS] -> START Request PREVIOUS SearchCrate "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] extracted "
                                    "searchCrateId from searchCrateData: "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current searchCrateId "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] previous SearchCrateId: "
                                 << previousSearchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId BOF: "
                                 << currentSearchCrateIdBOF;
                        qDebug() << "[SEARCHCRATESFEATURE] next SearchCrateId: "
                                 << nextSearchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId EOF: "
                                 << currentSearchCrateIdEOF;
                    }
                    if (currentSearchCrateIdBOF && !currentSearchCrateIdEOF) {
                        if (nextSearchCrateId.isValid()) {
                            m_searchCrateTableModel.selectSearchCrate2QVL(
                                    nextSearchCrateId,
                                    searchCrateData);
                            emit updateSearchCrateData(searchCrateData);
                            if (sDebugGroupedSearchCratesFeature) {
                                qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                            "[PREVIOUS] -> new active searchCrate: "
                                         << nextSearchCrateId;
                            }
                            m_lastRightClickedIndex = indexFromSearchCrateId(nextSearchCrateId);
                            activateSearchCrate(nextSearchCrateId);
                            slotSearchCrateTableChanged(nextSearchCrateId);
                        } else {
                            return;
                        }
                    } else {
                        if (previousSearchCrateId.isValid()) {
                            m_searchCrateTableModel.selectSearchCrate2QVL(
                                    previousSearchCrateId,
                                    searchCrateData);
                            emit updateSearchCrateData(searchCrateData);
                            if (sDebugGroupedSearchCratesFeature) {
                                qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                            "[PREVIOUS] -> new active searchCrate: "
                                         << previousSearchCrateId;
                            }
                            m_lastRightClickedIndex = indexFromSearchCrateId(previousSearchCrateId);
                            activateSearchCrate(previousSearchCrateId);
                            slotSearchCrateTableChanged(previousSearchCrateId);
                        } else {
                            return;
                        }
                    }
                });
        // DLG -> Next SearchCrate on 'Next'
        connect(&infoDialog,
                &dlgGroupedSearchCratesInfo::requestNextSearchCrate,
                this,
                [this]() {
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    SearchCrateId previousSearchCrateId(searchCrateData[56]);
                    bool currentSearchCrateIdBOF(searchCrateData[57].toString() == "true");
                    SearchCrateId nextSearchCrateId(searchCrateData[58]);
                    bool currentSearchCrateIdEOF(searchCrateData[59].toString() == "true");
                    if (sDebugGroupedSearchCratesFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[NEXT] -> START Request NEXT SearchCrate "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] extracted "
                                    "searchCrateId from searchCrateData: "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current searchCrateId "
                                 << searchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] previous SearchCrateId: "
                                 << previousSearchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId BOF: "
                                 << currentSearchCrateIdBOF;
                        qDebug() << "[SEARCHCRATESFEATURE] next SearchCrateId: "
                                 << nextSearchCrateId;
                        qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId EOF: "
                                 << currentSearchCrateIdEOF;
                    }
                    if (currentSearchCrateIdEOF && !currentSearchCrateIdBOF) {
                        if (previousSearchCrateId.isValid()) {
                            m_searchCrateTableModel.selectSearchCrate2QVL(
                                    previousSearchCrateId,
                                    searchCrateData);
                            emit updateSearchCrateData(searchCrateData);
                            if (sDebugGroupedSearchCratesFeature) {
                                qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                            "[NEXT] -> new active searchCrate: "
                                         << previousSearchCrateId;
                            }
                            m_lastRightClickedIndex = indexFromSearchCrateId(previousSearchCrateId);
                            activateSearchCrate(previousSearchCrateId);
                            slotSearchCrateTableChanged(previousSearchCrateId);
                        } else {
                            return;
                        }
                    } else {
                        if (nextSearchCrateId.isValid()) {
                            m_searchCrateTableModel.selectSearchCrate2QVL(
                                    nextSearchCrateId,
                                    searchCrateData);
                            emit updateSearchCrateData(searchCrateData);
                            if (sDebugGroupedSearchCratesFeature) {
                                qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                            "[NEXT] -> new active searchCrate: "
                                         << nextSearchCrateId;
                            }
                            m_lastRightClickedIndex = indexFromSearchCrateId(nextSearchCrateId);
                            activateSearchCrate(nextSearchCrateId);
                            slotSearchCrateTableChanged(nextSearchCrateId);
                        } else {
                            return;
                        }
                    }
                });
        // Execute & close the dialog
        if (infoDialog.exec() == QDialog::Accepted) {
            // Extract SearchCrateId from searchCrateData
            // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
            SearchCrateId searchCrateId(searchCrateData[0]);
            SearchCrateId previousSearchCrateId(searchCrateData[56]);
            bool currentSearchCrateIdBOF(searchCrateData[57].toString() == "true");
            SearchCrateId nextSearchCrateId(searchCrateData[58]);
            bool currentSearchCrateIdEOF(searchCrateData[59].toString() == "true");
            if (sDebugGroupedSearchCratesFeature) {
                qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                            "[NEXT] -> START Request NEXT SearchCrate "
                         << searchCrateId;
                qDebug() << "[SEARCHCRATESFEATURE] extracted searchCrateId from searchCrateData: "
                         << searchCrateId;
                qDebug() << "[SEARCHCRATESFEATURE] current searchCrateId "
                         << searchCrateId;
                qDebug() << "[SEARCHCRATESFEATURE] previous SearchCrateId: "
                         << previousSearchCrateId;
                qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId BOF: "
                         << currentSearchCrateIdBOF;
                qDebug() << "[SEARCHCRATESFEATURE] next SearchCrateId: "
                         << nextSearchCrateId;
                qDebug() << "[SEARCHCRATESFEATURE] current SearchCrateId EOF: "
                         << currentSearchCrateIdEOF;
            }

            if (searchCrateId.isValid()) {
                // Store updated data
                m_searchCrateTableModel.saveQVL2SearchCrate(searchCrateId, searchCrateData);
                // Send updated data back to ui-> adapted sql
                m_searchCrateTableModel.selectSearchCrate2QVL(
                        searchCrateIdFromIndex(m_lastRightClickedIndex),
                        searchCrateData);
                activateSearchCrate(searchCrateId);
                m_lastClickedIndex = indexFromSearchCrateId(searchCrateId);
                m_lastRightClickedIndex = indexFromSearchCrateId(searchCrateId);
                slotSearchCrateTableChanged(searchCrateId);
                if (sDebugGroupedSearchCratesFeature) {
                    qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [CLOSE "
                                "DIALOG] -> SearchCrate data saved from QVariantList "
                                "to database for "
                                "SearchCrateId:"
                             << searchCrateId;
                }
                emit updateSearchCrateData(searchCrateData);
            } else {
                return;
            }
        }
    }
    mutex.unlock();
}

void GroupedSearchCratesFeature::slotToggleSearchCrateLock() {
    SearchCrate searchCrate;
    if (readLastRightClickedSearchCrate(&searchCrate)) {
        searchCrate.setLocked(!searchCrate.isLocked());
        if (!m_pTrackCollection->updateSearchCrate(searchCrate)) {
            qDebug() << "Failed to toggle lock of SearchCrate" << searchCrate;
        }
    } else {
        qDebug() << "Failed to toggle lock of selected SearchCrate";
    }
}

SearchCrateId GroupedSearchCratesFeature::searchCrateIdFromIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return SearchCrateId();
    }
    TreeItem* item = static_cast<TreeItem*>(index.internalPointer());
    if (item == nullptr) {
        return SearchCrateId();
    }
    return SearchCrateId(item->getData());
}

QModelIndex GroupedSearchCratesFeature::indexFromSearchCrateId(SearchCrateId searchCrateId) const {
    VERIFY_OR_DEBUG_ASSERT(searchCrateId.isValid()) {
        return QModelIndex();
    }
    // Recursive search, since searchCrates can live inside groups.
    std::function<QModelIndex(const QModelIndex&)> findInChildren =
            [&](const QModelIndex& parent) -> QModelIndex {
        const int rows = m_pSidebarModel->rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            const QModelIndex idx = m_pSidebarModel->index(row, 0, parent);
            TreeItem* pTreeItem = m_pSidebarModel->getItem(idx);
            if (pTreeItem == nullptr) {
                continue;
            }
            if (!pTreeItem->hasChildren() && CrateId(pTreeItem->getData()) == searchCrateId) {
                return idx;
            }
            const QModelIndex found = findInChildren(idx);
            if (found.isValid()) {
                return found;
            }
        }
        return QModelIndex();
    };

    const QModelIndex rootIdx = QModelIndex();
    return findInChildren(rootIdx);
}

void GroupedSearchCratesFeature::slotAnalyzeSearchCrate() {
    if (m_lastRightClickedIndex.isValid()) {
        SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
        if (searchCrateId.isValid()) {
            QList<AnalyzerScheduledTrack> tracks;
            tracks.reserve(
                    m_pTrackCollection->searchCrates().countSearchCrateTracks(searchCrateId));
            {
                SearchCrateTrackSelectResult searchCrateTracks(
                        m_pTrackCollection->searchCrates().selectSearchCrateTracksSorted(
                                searchCrateId));
                while (searchCrateTracks.next()) {
                    tracks.append(searchCrateTracks.trackId());
                }
            }
            emit analyzeTracks(tracks);
        }
    }
}

void GroupedSearchCratesFeature::slotExportPlaylist() {
    SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
    SearchCrate searchCrate;
    if (m_pTrackCollection->searchCrates().readSearchCrateById(searchCrateId, &searchCrate)) {
        qDebug() << "Exporting SearchCrate" << searchCrateId << searchCrate;
    } else {
        qDebug() << "Failed to export SearchCrate" << searchCrateId;
        return;
    }

    QString lastSearchCrateDirectory = m_pConfig->getValue(
            kConfigKeyLastImportExportSearchCrateDirectoryKey,
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation));

    // Open a dialog to let the user choose the file location for searchCrate export.
    // The location is set to the last used directory for import/export and the file
    // name to the playlist name.
    const QString fileLocation = getFilePathWithVerifiedExtensionFromFileDialog(
            tr("Export SearchCrate"),
            lastSearchCrateDirectory.append("/").append(searchCrate.getName()),
            tr("M3U Playlist (*.m3u);;M3U8 Playlist (*.m3u8);;PLS Playlist "
               "(*.pls);;Text CSV (*.csv);;Readable Text (*.txt)"),
            tr("M3U Playlist (*.m3u)"));
    // Exit method if user cancelled the open dialog.
    if (fileLocation.isEmpty()) {
        return;
    }
    // Update the import/export SearchCrate directory
    QFileInfo fileDirectory(fileLocation);
    m_pConfig->set(kConfigKeyLastImportExportSearchCrateDirectoryKey,
            ConfigValue(fileDirectory.absoluteDir().canonicalPath()));

    // The user has picked a new directory via a file dialog. This means the
    // system sandboxer (if we are sandboxed) has granted us permission to this
    // folder. We don't need access to this file on a regular basis so we do not
    // register a security bookmark.

    // check config if relative paths are desired
    bool useRelativePath =
            m_pConfig->getValue<bool>(
                    kUseRelativePathOnExportConfigKey);

    // Create list of files of the searchCrate
    // Create a new table model since the main one might have an active search.
    std::unique_ptr<SearchCrateTableModel> pSearchCrateTableModel =
            std::make_unique<SearchCrateTableModel>(this, m_pLibrary->trackCollectionManager());
    pSearchCrateTableModel->selectSearchCrate(searchCrateId);
    pSearchCrateTableModel->select();

    if (fileLocation.endsWith(".csv", Qt::CaseInsensitive)) {
        ParserCsv::writeCSVFile(fileLocation, pSearchCrateTableModel.get(), useRelativePath);
    } else if (fileLocation.endsWith(".txt", Qt::CaseInsensitive)) {
        ParserCsv::writeReadableTextFile(fileLocation, pSearchCrateTableModel.get(), false);
    } else {
        // populate a list of files of the searchCrate
        QList<QString> playlistItems;
        int rows = pSearchCrateTableModel->rowCount();
        for (int i = 0; i < rows; ++i) {
            QModelIndex index = pSearchCrateTableModel->index(i, 0);
            playlistItems << pSearchCrateTableModel->getTrackLocation(index);
        }
        exportPlaylistItemsIntoFile(
                fileLocation,
                playlistItems,
                useRelativePath);
    }
}

void GroupedSearchCratesFeature::slotExportTrackFiles() {
    SearchCrateId searchCrateId(searchCrateIdFromIndex(m_lastRightClickedIndex));
    if (!searchCrateId.isValid()) {
        return;
    }
    // Create a new table model since the main one might have an active search.
    std::unique_ptr<SearchCrateTableModel> pSearchCrateTableModel =
            std::make_unique<SearchCrateTableModel>(this, m_pLibrary->trackCollectionManager());
    pSearchCrateTableModel->selectSearchCrate(searchCrateId);
    pSearchCrateTableModel->select();

    int rows = pSearchCrateTableModel->rowCount();
    TrackPointerList trackpointers;
    for (int i = 0; i < rows; ++i) {
        QModelIndex index = pSearchCrateTableModel->index(i, 0);
        auto pTrack = pSearchCrateTableModel->getTrack(index);
        VERIFY_OR_DEBUG_ASSERT(pTrack != nullptr) {
            continue;
        }
        trackpointers.push_back(pTrack);
    }

    if (trackpointers.isEmpty()) {
        return;
    }

    TrackExportWizard track_export(nullptr, m_pConfig, trackpointers);
    track_export.exportTracks();
}

void GroupedSearchCratesFeature::storePrevSiblingSearchCrateId(SearchCrateId searchCrateId) {
    QModelIndex actIndex = indexFromSearchCrateId(searchCrateId);
    m_prevSiblingSearchCrate = SearchCrateId();
    for (int i = (actIndex.row() + 1); i >= (actIndex.row() - 1); i -= 2) {
        QModelIndex newIndex = actIndex.sibling(i, actIndex.column());
        if (newIndex.isValid()) {
            TreeItem* pTreeItem = m_pSidebarModel->getItem(newIndex);
            DEBUG_ASSERT(pTreeItem != nullptr);
            if (!pTreeItem->hasChildren()) {
                m_prevSiblingSearchCrate = searchCrateIdFromIndex(newIndex);
            }
        }
    }
}

void GroupedSearchCratesFeature::slotSearchCrateTableChanged(SearchCrateId searchCrateId) {
    Q_UNUSED(searchCrateId);
    if (isChildIndexSelectedInSidebar(m_lastClickedIndex)) {
        // If the previously selected searchCrate was loaded to the tracks table and
        // selected in the sidebar try to activate that or a sibling
        rebuildChildModel();
        if (!activateSearchCrate(m_searchCrateTableModel.selectedSearchCrate())) {
            // probably last clicked searchCrate was deleted, try to
            // select the stored sibling
            if (m_prevSiblingSearchCrate.isValid()) {
                activateSearchCrate(m_prevSiblingSearchCrate);
            }
        }
    } else {
        // No valid selection to restore
        rebuildChildModel();
    }
}

void GroupedSearchCratesFeature::slotSearchCrateContentChanged(SearchCrateId searchCrateId) {
    QSet<SearchCrateId> updatedSearchCrateIds;
    updatedSearchCrateIds.insert(searchCrateId);
    updateChildModel(updatedSearchCrateIds);
}

void GroupedSearchCratesFeature::slotUpdateSearchCrateLabels(
        const QSet<SearchCrateId>& updatedSearchCrateIds) {
    updateChildModel(updatedSearchCrateIds);
}

void GroupedSearchCratesFeature::htmlLinkClicked(const QUrl& link) {
    if (QString(link.path()) == "create") {
        slotCreateSearchCrate();
    } else {
        qDebug() << "Unknown SearchCrate link clicked" << link;
    }
}

void GroupedSearchCratesFeature::slotTrackSelected(TrackId trackId) {
    m_selectedTrackId = trackId;

    TreeItem* pRootItem = m_pSidebarModel->getRootItem();
    VERIFY_OR_DEBUG_ASSERT(pRootItem != nullptr) {
        return;
    }

    std::vector<SearchCrateId> sortedTrackSearchCrates;
    if (m_selectedTrackId.isValid()) {
        SearchCrateTrackSelectResult trackSearchCratesIter(
                m_pTrackCollection->searchCrates()
                        .selectTrackSearchCratesSorted(m_selectedTrackId));
        while (trackSearchCratesIter.next()) {
            sortedTrackSearchCrates.push_back(trackSearchCratesIter.searchCrateId());
        }
    }

    // Set all searchCrates the track is in bold (or if there is no track selected,
    // clear all the bolding).
    // Recursive lambda: also on a grouped searchCrate parent
    std::function<bool(TreeItem*)> markRecursive = [&](TreeItem* pItem) -> bool {
        if (pItem == nullptr) {
            return false;
        }
        if (pItem->children().isEmpty()) {
            const bool bold = m_selectedTrackId.isValid() &&
                    std::binary_search(sortedTrackSearchCrates.begin(),
                            sortedTrackSearchCrates.end(),
                            SearchCrateId(pItem->getData()));
            pItem->setBold(bold);
            return bold;
        }
        bool anyChildBold = false;
        for (TreeItem* pChild : pItem->children()) {
            anyChildBold |= markRecursive(pChild);
        }
        pItem->setBold(anyChildBold);
        return anyChildBold;
    };

    for (TreeItem* pTreeItem : pRootItem->children()) {
        markRecursive(pTreeItem);
    }

    pRootItem->setBold(m_selectedTrackId.isValid() && sortedTrackSearchCrates.size() > 0);

    m_pSidebarModel->triggerRepaint();
}

void GroupedSearchCratesFeature::slotResetSelectedTrack() {
    slotTrackSelected(TrackId{});
}

QList<QVariantMap> GroupedSearchCratesFeature::getGroupedSearchCratesFromConfig() {
    // Read grouping config here and pass it to the model.
    // GroupedSearchCratesLength is stored as an int in the user config for
    // 0 => fixed length off / variable mask on
    const bool groupedSearchCratesLength =
            m_pConfig->getValue<int>(ConfigKey("[Library]", "GroupedSearchCratesLength")) == 0
            ? false
            : true;
    const int groupedSearchCratesFixedLength =
            m_pConfig->getValue<int>(
                    ConfigKey("[Library]", "GroupedSearchCratesFixedLength"), 0);
    const QString groupedSearchCratesVarLengthMask =
            m_pConfig->getValue(ConfigKey("[Library]", "GroupedSearchCratesVarLengthMask"));

    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[GROUPEDSEARCHCRATESFEATURE] GroupedSearchCratesLength ="
                 << groupedSearchCratesLength;
        qDebug() << "[GROUPEDSEARCHCRATESFEATURE] GroupedSearchCratesFixedLength ="
                 << groupedSearchCratesFixedLength;
        qDebug() << "[GROUPEDSEARCHCRATESFEATURE] GroupedSearchCratesVarLengthMask ="
                 << groupedSearchCratesVarLengthMask;
    }

    return m_searchCrateTableModel.getGroupedSearchCrates(
            groupedSearchCratesLength,
            groupedSearchCratesFixedLength,
            groupedSearchCratesVarLengthMask);
}

QModelIndex GroupedSearchCratesFeature::rebuildChildModel(SearchCrateId selectedSearchCrateId) {
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[GROUPEDSEARCHCRATESFEATURE] -> rebuildChildModel()" << selectedSearchCrateId;
    }

    QModelIndex previouslySelectedIndex = m_lastRightClickedIndex;

    // remember open/close state of groups
    QMap<QString, bool> groupExpandedStates;
    if (m_pSidebarWidget) {
        for (int row = 0; row < m_pSidebarModel->rowCount(); ++row) {
            QModelIndex groupIndex = m_pSidebarModel->index(row, 0);
            if (groupIndex.isValid()) {
                TreeItem* pGroupItem = m_pSidebarModel->getItem(groupIndex);
                if (pGroupItem) {
                    const QString& groupName = pGroupItem->getLabel();
                    groupExpandedStates[groupName] =
                            m_pSidebarWidget->isExpanded(groupIndex);
                }
            }
        }
    }

    m_lastRightClickedIndex = QModelIndex();
    TreeItem* pRootItem = m_pSidebarModel->getRootItem();
    VERIFY_OR_DEBUG_ASSERT(pRootItem != nullptr) {
        return QModelIndex();
    }
    m_pSidebarModel->removeRows(0, pRootItem->childRows());

    const QList<QVariantMap> groupedSearchCrates = getGroupedSearchCratesFromConfig();

    // Delimiter used both for the var-length mask and for full-path storage.
    const QString delimiter = m_pConfig->getValue(
            ConfigKey("[Library]", "GroupedSearchCratesVarLengthMask"));

    // Fixed-prefix mode.
    if (m_pConfig->getValue<int>(ConfigKey("[Library]", "GroupedSearchCratesLength")) != 0) {
        QMap<QString, int> groupCounts;
        for (const auto& searchCrateData : groupedSearchCrates) {
            const QString& groupName = searchCrateData["group_name"].toString();
            groupCounts[groupName]++;
        }

        QMap<QString, TreeItem*> groupItems;
        std::vector<std::unique_ptr<TreeItem>> modelRows;

        for (const auto& searchCrateData : groupedSearchCrates) {
            const QString& groupName = searchCrateData["group_name"].toString();
            SearchCrateId searchCrateId(searchCrateData["searchCrate_id"]);

            SearchCrateSummary searchCrateSummary;
            if (!m_pTrackCollection->searchCrates().readSearchCrateSummaryById(
                        searchCrateId, &searchCrateSummary)) {
                qWarning() << "[GROUPEDSEARCHCRATESFEATURE] Failed to fetch summary "
                              "for searchCrate ID:"
                           << searchCrateId;
                continue;
            }

            const QString searchCrateSummaryName = formatLabel(searchCrateSummary);

            if (groupCounts[groupName] > 1) {
                TreeItem* pGroupItem = groupItems.value(groupName, nullptr);
                if (!pGroupItem) {
                    // auto newGroup = std::make_unique<TreeItem>(
                    //        groupName, kInvalidSearchCrateId);
                    auto newGroup = std::make_unique<TreeItem>(groupName, QVariant());
                    pGroupItem = newGroup.get();
                    groupItems.insert(groupName, pGroupItem);
                    modelRows.push_back(std::move(newGroup));
                }

                const QString displaySearchCrateName =
                        searchCrateSummaryName.mid(groupName.length()).trimmed();

                TreeItem* pChildItem = pGroupItem->appendChild(
                        displaySearchCrateName, searchCrateId.toVariant().toInt());
                pChildItem->setFullPath(groupName + delimiter + displaySearchCrateName);
                updateTreeItemForSearchCrateSummary(pChildItem, searchCrateSummary);
            } else {
                auto newSearchCrate = std::make_unique<TreeItem>(
                        searchCrateSummaryName, searchCrateId.toVariant().toInt());
                newSearchCrate->setFullPath(searchCrateSummaryName);
                updateTreeItemForSearchCrateSummary(newSearchCrate.get(), searchCrateSummary);
                modelRows.push_back(std::move(newSearchCrate));
            }
        }

        m_pSidebarModel->insertTreeItemRows(std::move(modelRows), 0);
        slotTrackSelected(m_selectedTrackId);
    } else {
        // Variable-prefix / multi-level mode.
        QMap<QString, QList<QVariantMap>> topLevelGroups;
        for (const auto& searchCrateData : groupedSearchCrates) {
            const QString& groupName = searchCrateData["group_name"].toString();
            const QString topGroup = groupName.section(delimiter, 0, 0);
            topLevelGroups[topGroup].append(searchCrateData);
        }

        std::function<void(const QString&, const QList<QVariantMap>&, TreeItem*)>
                buildTreeStructure;
        buildTreeStructure = [&](const QString& currentPath,
                                     const QList<QVariantMap>& searchCrates,
                                     TreeItem* pParentItem) {
            QMap<QString, QList<QVariantMap>> subgroupedSearchCrates;

            for (const QVariantMap& searchCrateData : searchCrates) {
                const QString& groupName = searchCrateData["group_name"].toString();
                if (!groupName.startsWith(currentPath)) {
                    continue;
                }

                const QString remainingPath = groupName.mid(currentPath.length());
                int delimiterPos = remainingPath.indexOf(delimiter);

                if (delimiterPos >= 0) {
                    const QString subgroupName = remainingPath.left(delimiterPos);
                    subgroupedSearchCrates[subgroupName].append(searchCrateData);
                } else {
                    SearchCrateId searchCrateId(searchCrateData["searchCrate_id"]);
                    SearchCrateSummary searchCrateSummary;
                    if (!m_pTrackCollection->searchCrates().readSearchCrateSummaryById(
                                searchCrateId, &searchCrateSummary)) {
                        qWarning() << "[GROUPEDSEARCHCRATESFEATURE] Failed to fetch "
                                      "summary for searchCrate ID:"
                                   << searchCrateId;
                        continue;
                    }

                    const QString displaySearchCrateName =
                            formatLabel(searchCrateSummary).mid(currentPath.length());

                    TreeItem* pChildItem = pParentItem->appendChild(
                            displaySearchCrateName.trimmed(), searchCrateId.toVariant());
                    pChildItem->setFullPath(currentPath + delimiter +
                            displaySearchCrateName);
                    updateTreeItemForSearchCrateSummary(pChildItem, searchCrateSummary);
                }
            }

            for (auto it = subgroupedSearchCrates.constBegin();
                    it != subgroupedSearchCrates.constEnd();
                    ++it) {
                const QString& subgroupName = it.key();
                const QList<QVariantMap>& subgroupSearchCrates = it.value();
                if (subgroupSearchCrates.isEmpty()) {
                    continue;
                }
                if (subgroupSearchCrates.size() > 1) {
                    // auto pNewSubgroup = std::make_unique<TreeItem>(
                    //        subgroupName, kInvalidSearchCrateId);
                    auto pNewSubgroup = std::make_unique<TreeItem>(subgroupName, QVariant());
                    TreeItem* pSubgroupItem = pNewSubgroup.get();
                    pParentItem->insertChild(
                            pParentItem->childCount(), std::move(pNewSubgroup));
                    buildTreeStructure(currentPath + subgroupName + delimiter,
                            subgroupSearchCrates,
                            pSubgroupItem);
                } else {
                    const QVariantMap& searchCrateData = subgroupSearchCrates.first();
                    SearchCrateId searchCrateId(searchCrateData["searchCrate_id"]);
                    SearchCrateSummary searchCrateSummary;
                    if (!m_pTrackCollection->searchCrates().readSearchCrateSummaryById(
                                searchCrateId, &searchCrateSummary)) {
                        qWarning() << "[GROUPEDSEARCHCRATESFEATURE] Failed to fetch "
                                      "summary for searchCrate ID:"
                                   << searchCrateId;
                        continue;
                    }

                    const QString displaySearchCrateName =
                            formatLabel(searchCrateSummary).mid(currentPath.length());

                    TreeItem* pChildItem = pParentItem->appendChild(
                            displaySearchCrateName.trimmed(), searchCrateId.toVariant());
                    pChildItem->setFullPath(currentPath + delimiter +
                            displaySearchCrateName);
                    updateTreeItemForSearchCrateSummary(pChildItem, searchCrateSummary);
                }
            }
        };

        for (auto it = topLevelGroups.constBegin();
                it != topLevelGroups.constEnd();
                ++it) {
            const QString& groupName = it.key();
            const QList<QVariantMap>& searchCrates = it.value();
            if (searchCrates.isEmpty()) {
                continue;
            }

            if (searchCrates.size() > 1) {
                // auto pNewGroup = std::make_unique<TreeItem>(
                //        groupName, kInvalidSearchCrateId);
                auto pNewGroup = std::make_unique<TreeItem>(groupName, QVariant());
                TreeItem* pGroupItem = pNewGroup.get();
                pRootItem->insertChild(
                        pRootItem->childCount(), std::move(pNewGroup));
                buildTreeStructure(groupName + delimiter, searchCrates, pGroupItem);
            } else {
                const QVariantMap& searchCrateData = searchCrates.first();
                SearchCrateId searchCrateId(searchCrateData["searchCrate_id"]);
                SearchCrateSummary searchCrateSummary;
                if (!m_pTrackCollection->searchCrates().readSearchCrateSummaryById(
                            searchCrateId, &searchCrateSummary)) {
                    qWarning() << "[GROUPEDSEARCHCRATESFEATURE] Failed to fetch "
                                  "summary for searchCrate ID:"
                               << searchCrateId;
                    continue;
                }

                const QString displaySearchCrateName = formatLabel(searchCrateSummary);
                TreeItem* pChildItem = pRootItem->appendChild(
                        displaySearchCrateName.trimmed(), searchCrateId.toVariant());
                updateTreeItemForSearchCrateSummary(pChildItem, searchCrateSummary);
            }
        }
        slotTrackSelected(m_selectedTrackId);
    }

    // restore open/close state of groups
    if (m_pSidebarWidget) {
        for (int row = 0; row < m_pSidebarModel->rowCount(); ++row) {
            QModelIndex groupIndex = m_pSidebarModel->index(row, 0);
            if (groupIndex.isValid()) {
                TreeItem* pGroupItem = m_pSidebarModel->getItem(groupIndex);
                if (pGroupItem) {
                    const QString& groupName = pGroupItem->getLabel();
                    if (groupExpandedStates.contains(groupName)) {
                        m_pSidebarWidget->setExpanded(
                                groupIndex, groupExpandedStates[groupName]);
                    }
                }
            }
        }
    }

    if (selectedSearchCrateId.isValid()) {
        const QModelIndex idx = indexFromSearchCrateId(selectedSearchCrateId);
        if (idx.isValid()) {
            return idx;
        }
    }

    return previouslySelectedIndex.isValid() ? previouslySelectedIndex
                                             : QModelIndex();
}

void GroupedSearchCratesFeature::updateChildModel(
        const QSet<SearchCrateId>& updatedSearchCrateIds) {
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[GROUPEDSEARCHCRATESFEATURE] -> updateChildModel() -> Updating "
                    "searchCrates"
                 << updatedSearchCrateIds;
    }

    for (const SearchCrateId& searchCrateId : updatedSearchCrateIds) {
        QModelIndex index = indexFromSearchCrateId(searchCrateId);
        if (!index.isValid()) {
            // The searchCrate no longer exists in the tree (e.g. after a rename that
            // moved it into a different group); rebuild everything.
            rebuildChildModel();
            return;
        }
        SearchCrateSummary searchCrateSummary;
        if (!m_pTrackCollection->searchCrates().readSearchCrateSummaryById(
                    searchCrateId, &searchCrateSummary)) {
            continue;
        }
        updateTreeItemForSearchCrateSummary(
                m_pSidebarModel->getItem(index), searchCrateSummary);
        m_pSidebarModel->triggerRepaint(index);
    }

    if (m_selectedTrackId.isValid()) {
        slotTrackSelected(m_selectedTrackId);
    }
}

void GroupedSearchCratesFeature::activateChild(const QModelIndex& index) {
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[GROUPEDSEARCHCRATESFEATURE] -> activateChild() -> index" << index;
    }

    if (!index.isValid()) {
        qWarning() << "[GROUPEDSEARCHCRATESFEATURE] -> activateChild() -> invalid index";
        return;
    }

    const SearchCrateId searchCrateId = searchCrateIdFromIndex(index);
    ////////////////////////////////////////////////////////////////////
    // Group node (no valid SearchCrateId -> it is a group / subgroup header)
    ////////////////////////////////////////////////////////////////////
    if (!searchCrateId.isValid()) {
        if (sDebugGroupedSearchCratesFeature) {
            qDebug() << "[GROUPEDSEARCHCRATESFEATURE] -> activateChild() -> "
                        "Group activated";
        }

        const QString fullPath = fullPathFromIndex(index);
        if (fullPath.isEmpty()) {
            qWarning() << "[GROUPEDSEARCHCRATESFEATURE] -> activateChild() -> "
                          "Group activated: no valid full path for index:"
                       << index;
            return;
        }

        if (sDebugGroupedSearchCratesFeature) {
            qDebug() << "[GROUPEDSEARCHCRATESFEATURE] -> activateChild() -> "
                        "Group activated -> fullPath:"
                     << fullPath;
        }

        m_lastClickedIndex = index;
        m_lastRightClickedIndex = QModelIndex();
        m_prevSiblingSearchCrate = SearchCrateId();
        emit saveModelState();
        emit disableSearch();
        emit enableCoverArtDisplay(false);

        m_searchCrateTableModel.selectSearchCrateGroup(fullPath);

        emit showTrackModel(&m_searchCrateTableModel);
        return;
    }

    ///////////////////
    // Leaf searchCrate node
    ///////////////////
    if (sDebugGroupedSearchCratesFeature) {
        qDebug() << "[GROUPEDSEARCHCRATESFEATURE] -> activateChild() -> "
                    "Child searchCrate activated -> searchCrateId:"
                 << searchCrateId;
    }

    // Make sure the searchCrate still exists (it may have been deleted between
    // the model rebuild and this activation).
    if (!m_pTrackCollection->searchCrates().readSearchCrateSummaryById(searchCrateId)) {
        qWarning() << "[GROUPEDSEARCHCRATESFEATURE] -> activateChild() -> "
                      "SearchCrate no longer exists:"
                   << searchCrateId;
        return;
    }

    m_lastClickedIndex = index;
    m_lastRightClickedIndex = QModelIndex();
    m_prevSiblingSearchCrate = SearchCrateId();
    emit saveModelState();
    m_searchCrateTableModel.selectSearchCrate(searchCrateId);
    emit showTrackModel(&m_searchCrateTableModel);
    emit enableCoverArtDisplay(true);
    emit featureSelect(this, m_lastClickedIndex);
}

QString GroupedSearchCratesFeature::fullPathFromIndex(const QModelIndex& index) const {
    const QString delimiter = m_pConfig->getValue(
            ConfigKey("[Library]", "GroupedSearchCratesVarLengthMask"));
    if (!index.isValid()) {
        return QString();
    }

    TreeItem* pItem = m_pSidebarModel->getItem(index);
    if (!pItem) {
        return QString();
    }

    QString fullPath;
    TreeItem* currentItem = pItem;
    while (currentItem) {
        if (!fullPath.isEmpty()) {
            fullPath.prepend(delimiter);
        }
        fullPath.prepend(currentItem->getLabel());
        currentItem = currentItem->parent();
    }

    // The root item itself has no label; the first prepend is from the root,
    // which we have to drop. Append the delimiter at the end so the resulting
    // LIKE 'prefix<delim>%' pattern only matches actual group members.
    fullPath = fullPath.mid(delimiter.length()).append(delimiter);
    return fullPath;
}

QString GroupedSearchCratesFeature::groupNameFromIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return QString();
    }
    TreeItem* pItem = m_pSidebarModel->getItem(index);
    if (!pItem) {
        return QString();
    }
    return pItem->getLabel();
}

void GroupedSearchCratesFeature::updateFullPathRecursive(
        TreeItem* pItem, const QString& parentPath) {
    const QString delimiter = m_pConfig->getValue(
            ConfigKey("[Library]", "GroupedSearchCratesVarLengthMask"));
    if (!pItem) {
        return;
    }

    QString currentFullPath = parentPath.isEmpty()
            ? pItem->getLabel()
            : parentPath + delimiter + pItem->getLabel();
    pItem->setFullPath(currentFullPath);

    for (TreeItem* pChild : pItem->children()) {
        updateFullPathRecursive(pChild, currentFullPath);
    }
}
