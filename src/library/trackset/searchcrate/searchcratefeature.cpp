#include "library/trackset/searchcrate/searchcratefeature.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>
#include <algorithm>
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
#include "moc_searchcratefeature.cpp"
#include "sources/soundsourceproxy.h"
#include "track/track.h"
#include "util/defs.h"
#include "util/file.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"
#include "widget/wlibrarytextbrowser.h"

namespace {
const bool sDebugSearchCrateFeature = false;
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

SearchCrateFeature::SearchCrateFeature(
        Library* pLibrary, UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary,
                  pConfig,
                  "SEARCHCRATESHOME",
                  QStringLiteral("searchcrates")),
          m_lockedSearchCrateIcon(
                  ":/images/library/ic_library_locked_tracklist.svg"),
          m_pTrackCollection(
                  pLibrary->trackCollectionManager()->internalCollection()),
          m_searchCrateTableModel(this, pLibrary->trackCollectionManager()) {
    initActions();

    // construct child model
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    rebuildChildModel();

    connectLibrary(pLibrary);
    connectTrackCollection();
}

void SearchCrateFeature::initActions() {
    m_pCreateSearchCrateAction = make_parented<QAction>(tr("Create New SearchCrate"), this);
    connect(m_pCreateSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotCreateSearchCrate);
    m_pEditSearchCrateAction = make_parented<QAction>(tr("Edit SearchCrate"), this);
    connect(m_pEditSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotEditSearchCrate);
    m_pRenameSearchCrateAction = make_parented<QAction>(tr("Rename"), this);
    connect(m_pRenameSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotRenameSearchCrate);
    m_pDuplicateSearchCrateAction = make_parented<QAction>(tr("Duplicate"), this);
    connect(m_pDuplicateSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotDuplicateSearchCrate);
    m_pDeleteSearchCrateAction = make_parented<QAction>(tr("Remove"), this);
    const auto removeKeySequence =
            // TODO(XXX): Qt6 replace enum | with QKeyCombination
            QKeySequence(static_cast<int>(kHideRemoveShortcutModifier) |
                    kHideRemoveShortcutKey);
    m_pDeleteSearchCrateAction->setShortcut(removeKeySequence);
    connect(m_pDeleteSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotDeleteSearchCrate);
    m_pLockSearchCrateAction = make_parented<QAction>(tr("Lock"), this);
    connect(m_pLockSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotToggleSearchCrateLock);

    //    m_pAutoDjTrackSourceAction = make_parented<QAction>(tr("Auto DJ Track Source"), this);
    //    m_pAutoDjTrackSourceAction->setCheckable(true);
    //    connect(m_pAutoDjTrackSourceAction.get(),
    //            &QAction::changed,
    //            this,
    //            &SearchCrateFeature::slotAutoDjTrackSourceChanged);

    m_pAnalyzeSearchCrateAction = make_parented<QAction>(tr("Analyze entire SearchCrate"), this);
    connect(m_pAnalyzeSearchCrateAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotAnalyzeSearchCrate);

    // m_pImportPlaylistAction = make_parented<QAction>(tr("Import SearchCrate"), this);
    // connect(m_pImportPlaylistAction.get(),
    //         &QAction::triggered,
    //         this,
    //         &SearchCrateFeature::slotImportPlaylist);
    // m_pCreateImportPlaylistAction = make_parented<QAction>(tr("Import SearchCrate"), this);
    // connect(m_pCreateImportPlaylistAction.get(),
    //         &QAction::triggered,
    //         this,
    //         &SearchCrateFeature::slotCreateImportSearchCrate);
    m_pExportPlaylistAction = make_parented<QAction>(tr("Export SearchCrate as Playlist"), this);
    connect(m_pExportPlaylistAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotExportPlaylist);
    m_pExportTrackFilesAction = make_parented<QAction>(tr("Export Track Files"), this);
    connect(m_pExportTrackFilesAction.get(),
            &QAction::triggered,
            this,
            &SearchCrateFeature::slotExportTrackFiles);
#ifdef __ENGINEPRIME__
    // Engine DJ export needs to be adapted for searchCrates first
    // m_pExportAllSearchCratesAction = make_parented<QAction>(tr("Export to Engine DJ"), this);
    // connect(m_pExportAllSearchCratesAction.get(),
    //        &QAction::triggered,
    //        this,
    //        &SearchCrateFeature::exportAllSearchCrates);
    // m_pExportSearchCrateAction = make_parented<QAction>(tr("Export to Engine DJ"), this);
    connect(m_pExportSearchCrateAction.get(),
            &QAction::triggered,
            this,
            [this]() {
                SearchCrateId searchCrateId =
                        searchCrateIdFromIndex(m_lastRightClickedIndex);
                if (searchCrateId.isValid()) {
                    emit exportSearchCrate(searchCrateId);
                }
            });
#endif
}

void SearchCrateFeature::connectLibrary(Library* pLibrary) {
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
            &SearchCrateFeature::slotResetSelectedTrack);
}

void SearchCrateFeature::connectTrackCollection() {
    connect(m_pTrackCollection, // created new, duplicated or imported playlist to new searchCrate
            &TrackCollection::searchCrateInserted,
            this,
            &SearchCrateFeature::slotSearchCrateTableChanged);
    connect(m_pTrackCollection, // renamed, un/locked, toggled AutoDJ source
            &TrackCollection::searchCrateUpdated,
            this,
            &SearchCrateFeature::slotSearchCrateTableChanged);
    connect(m_pTrackCollection,
            &TrackCollection::searchCrateDeleted,
            this,
            &SearchCrateFeature::slotSearchCrateTableChanged);
    connect(m_pTrackCollection, // searchCrate tracks hidden, unhidden or purged
            &TrackCollection::searchCrateTracksChanged,
            this,
            &SearchCrateFeature::slotSearchCrateContentChanged);
    connect(m_pTrackCollection,
            &TrackCollection::searchCrateSummaryChanged,
            this,
            &SearchCrateFeature::slotUpdateSearchCrateLabels);
}

QVariant SearchCrateFeature::title() {
    return tr("SearchCrates");
}

QString SearchCrateFeature::formatRootViewHtml() const {
    QString searchCratesTitle = tr("SearchCrates");
    QString searchCratesSummary =
            tr("SearchCrates are a great way to help organize the music you want to "
               "DJ with.");
    QString searchCratesSummary2 =
            tr("Make a searchCrate for your next gig, for your favorite electrohouse "
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

std::unique_ptr<TreeItem> SearchCrateFeature::newTreeItemForSearchCrateSummary(
        const SearchCrateSummary& searchCrateSummary) {
    auto pTreeItem = TreeItem::newRoot(this);
    updateTreeItemForSearchCrateSummary(pTreeItem.get(), searchCrateSummary);
    return pTreeItem;
}

void SearchCrateFeature::updateTreeItemForSearchCrateSummary(
        TreeItem* pTreeItem, const SearchCrateSummary& searchCrateSummary) const {
    DEBUG_ASSERT(pTreeItem != nullptr);
    if (pTreeItem->getData().isNull()) {
        // Initialize a newly created tree item
        pTreeItem->setData(searchCrateSummary.getId().toVariant());
    } else {
        // The data (= SearchCrateId) is immutable once it has been set
        DEBUG_ASSERT(SearchCrateId(pTreeItem->getData()) == searchCrateSummary.getId());
    }
    // Update mutable properties
    pTreeItem->setLabel(formatLabel(searchCrateSummary));
    pTreeItem->setIcon(searchCrateSummary.isLocked() ? m_lockedSearchCrateIcon : QIcon());
}

// bool SearchCrateFeature::dropAcceptChild(
//         const QModelIndex& index, const QList<QUrl>& urls, QObject* pSource) {
//     SearchCrateId searchCrateId(searchCrateIdFromIndex(index));
//     VERIFY_OR_DEBUG_ASSERT(searchCrateId.isValid()) {
//         return false;
//     }
//     // If a track is dropped onto a searchCrate's name, but the track isn't in the
//     // library, then add the track to the library before adding it to the
//     // playlist.
//     // pSource != nullptr it is a drop from inside Mixxx and indicates all
//     // tracks already in the DB
//     QList<TrackId> trackIds =
//             m_pLibrary->trackCollectionManager()->resolveTrackIdsFromUrls(urls, !pSource);
//     if (trackIds.isEmpty()) {
//         return false;
//     }
//
//     m_pTrackCollection->addSearchCrateTracks(searchCrateId, trackIds);
//     return true;
// }

// bool SearchCrateFeature::dragMoveAcceptChild(const QModelIndex& index, const QUrl& url) {
//     SearchCrateId searchCrateId(searchCrateIdFromIndex(index));
//     if (!searchCrateId.isValid()) {
//         return false;
//     }
//     SearchCrate searchCrate;
//     if (!m_pTrackCollection->searchCrates().readSearchCrateById(searchCrateId, &searchCrate) ||
//             searchCrate.isLocked()) {
//         return false;
//     }
//     return SoundSourceProxy::isUrlSupported(url) ||
//             Parser::isPlaylistFilenameSupported(url.toLocalFile());
// }

void SearchCrateFeature::bindLibraryWidget(
        WLibrary* libraryWidget, KeyboardEventFilter* keyboard) {
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(formatRootViewHtml());
    edit->setOpenLinks(false);
    connect(edit,
            &WLibraryTextBrowser::anchorClicked,
            this,
            &SearchCrateFeature::htmlLinkClicked);
    libraryWidget->registerView(m_rootViewName, edit);
}

void SearchCrateFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    // store the sidebar widget pointer for later use in onRightClickChild
    m_pSidebarWidget = pSidebarWidget;
}

TreeItemModel* SearchCrateFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void SearchCrateFeature::activate() {
    m_lastClickedIndex = QModelIndex();
    BaseTrackSetFeature::activate();
}

void SearchCrateFeature::activateChild(const QModelIndex& index) {
    qDebug() << "   SearchCrateFeature::activateChild()" << index;
    SearchCrateId searchCrateId(searchCrateIdFromIndex(index));
    VERIFY_OR_DEBUG_ASSERT(searchCrateId.isValid()) {
        return;
    }
    m_lastClickedIndex = index;
    m_lastRightClickedIndex = QModelIndex();
    m_prevSiblingSearchCrate = SearchCrateId();
    emit saveModelState();
    m_searchCrateTableModel.selectSearchCrate(searchCrateId);
    emit showTrackModel(&m_searchCrateTableModel);
    emit enableCoverArtDisplay(true);
}

bool SearchCrateFeature::activateSearchCrate(SearchCrateId searchCrateId) {
    qDebug() << "SearchCrateFeature::activateSearchCrate()" << searchCrateId;
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

bool SearchCrateFeature::readLastRightClickedSearchCrate(SearchCrate* pSearchCrate) const {
    SearchCrateId searchCrateId(searchCrateIdFromIndex(m_lastRightClickedIndex));
    VERIFY_OR_DEBUG_ASSERT(searchCrateId.isValid()) {
        qWarning() << "Failed to determine id of selected searchCrate";
        return false;
    }
    VERIFY_OR_DEBUG_ASSERT(
            m_pTrackCollection->searchCrates().readSearchCrateById(searchCrateId, pSearchCrate)) {
        qWarning() << "Failed to read selected searchCrate with id" << searchCrateId;
        return false;
    }
    return true;
}

bool SearchCrateFeature::isChildIndexSelectedInSidebar(const QModelIndex& index) {
    return m_pSidebarWidget && m_pSidebarWidget->isChildIndexSelected(index);
}

void SearchCrateFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedIndex = QModelIndex();
    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pCreateSearchCrateAction.get());
#ifdef __ENGINEPRIME__
    // menu.addSeparator();
    // menu.addAction(m_pExportAllSearchCratesAction.get());
#endif
    menu.exec(globalPos);
}

void SearchCrateFeature::onRightClickChild(
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

    //    m_pAutoDjTrackSourceAction->setChecked(searchCrate.isAutoDjSource());

    m_pLockSearchCrateAction->setText(searchCrate.isLocked() ? tr("Unlock") : tr("Lock"));

    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pCreateSearchCrateAction.get());
    menu.addSeparator();
    menu.addAction(m_pEditSearchCrateAction.get());
    menu.addSeparator();
    //    menu.addAction(m_pCreateImportPlaylistAction.get());
    menu.addAction(m_pRenameSearchCrateAction.get());
    menu.addAction(m_pDuplicateSearchCrateAction.get());
    menu.addAction(m_pDeleteSearchCrateAction.get());
    menu.addAction(m_pLockSearchCrateAction.get());
    menu.addSeparator();
    //    menu.addAction(m_pAutoDjTrackSourceAction.get());
    //    menu.addSeparator();
    menu.addAction(m_pAnalyzeSearchCrateAction.get());
    menu.addSeparator();
    //    if (!searchCrate.isLocked()) {
    //        menu.addAction(m_pImportPlaylistAction.get());
    //    }
    menu.addAction(m_pExportPlaylistAction.get());
    menu.addAction(m_pExportTrackFilesAction.get());
#ifdef __ENGINEPRIME__
    menu.addAction(m_pExportSearchCrateAction.get());
#endif
    menu.exec(globalPos);
}

void SearchCrateFeature::slotCreateSearchCrate() {
    SearchCrateId searchCrateId =
            SearchCrateFeatureHelper(m_pTrackCollection, m_pConfig)
                    .createEmptySearchCrate();
    if (searchCrateId.isValid()) {
        // expand SearchCrates and scroll to new searchCrate
        m_pSidebarWidget->selectChildIndex(indexFromSearchCrateId(searchCrateId), false);
    }
}

void SearchCrateFeature::slotCreateSearchCrateFromSearch(const QString& text) {
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

void SearchCrateFeature::deleteItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotDeleteSearchCrate();
}

void SearchCrateFeature::slotDeleteSearchCrate() {
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
    qWarning() << "Failed to delete selected searchCrate";
}

void SearchCrateFeature::renameItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotRenameSearchCrate();
}

void SearchCrateFeature::slotRenameSearchCrate() {
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
                        tr("A searchCrate by that name already exists."));
                continue;
            }
            searchCrate.setName(std::move(newName));
            DEBUG_ASSERT(searchCrate.hasName());
            break;
        }

        if (!m_pTrackCollection->updateSearchCrate(searchCrate)) {
            qDebug() << "Failed to rename searchCrate" << searchCrate;
        }
    } else {
        qDebug() << "Failed to rename selected searchCrate";
    }
}

void SearchCrateFeature::slotDuplicateSearchCrate() {
    SearchCrate searchCrate;
    if (readLastRightClickedSearchCrate(&searchCrate)) {
        SearchCrateId newSearchCrateId =
                SearchCrateFeatureHelper(m_pTrackCollection, m_pConfig)
                        .duplicateSearchCrate(searchCrate);
        if (newSearchCrateId.isValid()) {
            qDebug() << "Duplicate searchCrate" << searchCrate
                     << ", new searchCrate:" << newSearchCrateId;
            return;
        }
    }
    qDebug() << "Failed to duplicate selected searchCrate";
}

void SearchCrateFeature::slotEditSearchCrate() {
    QMutex mutex;
    mutex.lock();
    if (sDebugSearchCrateFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] "
                    "-> slotEditSearchCrate";
    }
    SearchCrate searchCrate;
    readLastRightClickedSearchCrate(&searchCrate);
    if (sDebugSearchCrateFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                    "m_lastRightClickedIndex  = "
                 << m_lastRightClickedIndex;
    }
    // Load data into QVariant
    searchCrateData.clear();
    m_searchCrateTableModel.selectSearchCrate2QVL(
            searchCrateIdFromIndex(m_lastRightClickedIndex), searchCrateData);
    if (sDebugSearchCrateFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                    "SearchCrate data loaded into QVariantList:"
                 << searchCrateData;
    }

    QVariantList playlistsCratesData;
    m_searchCrateTableModel.selectPlaylistsCrates2QVL(playlistsCratesData);
    if (sDebugSearchCrateFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                    "Playlists & Crates data loaded into QVariantList:"
                 << playlistsCratesData;
    }

    if (readLastRightClickedSearchCrate(&searchCrate)) {
        SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
        if (sDebugSearchCrateFeature) {
            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                        "SlotEditSearchCrate -> searchCrateID = "
                     << searchCrateId;
        }
        // Pass this to provide the SearchCratesFeature instance
        dlgSearchCrateInfo infoDialog(this);

        if (sDebugSearchCrateFeature) {
            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> [START] -> "
                        "[INIT DIALOG] -> INIT DIALOG ";
        }

        infoDialog.init(searchCrateData, playlistsCratesData);
        // DLG -> Update SearchCrate on 'Apply'
        connect(&infoDialog,
                &dlgSearchCrateInfo::dataUpdated,
                this,
                [this, searchCrateId](const QVariantList& updatedData) mutable {
                    if (sDebugSearchCrateFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[UPDATE] -> START Request UPDATE SearchCrate "
                                    "searchCrateId "
                                 << searchCrateId;
                    }
                    searchCrateData = updatedData; // Capture the updated data from the UI
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    if (sDebugSearchCrateFeature) {
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
                        if (sDebugSearchCrateFeature) {
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
                &dlgSearchCrateInfo::requestDeleteSearchCrate,
                this,
                [this]() {
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    SearchCrateId previousSearchCrateId(searchCrateData[56]);
                    bool currentSearchCrateIdBOF(searchCrateData[57].toString() == "true");
                    SearchCrateId nextSearchCrateId(searchCrateData[58]);
                    bool currentSearchCrateIdEOF(searchCrateData[59].toString() == "true");
                    if (sDebugSearchCrateFeature) {
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
                        if (sDebugSearchCrateFeature) {
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
                                if (sDebugSearchCrateFeature) {
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
                                if (sDebugSearchCrateFeature) {
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
                &dlgSearchCrateInfo::requestNewSearchCrate,
                this,
                [this, searchCrateId]() {
                    if (sDebugSearchCrateFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[NEW] "
                                    "-> START Request NEW SearchCrate searchCrateId "
                                 << searchCrateId;
                    }
                    SearchCrateId searchCrateId =
                            SearchCrateFeatureHelper(m_pTrackCollection, m_pConfig)
                                    .createEmptySearchCrateFromUI();
                    if (!searchCrateId.isValid()) {
                        if (sDebugSearchCrateFeature) {
                            qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT "
                                        "SEARCHCRATES] -> "
                                        "[NEW] -> Creation failed.";
                        }
                        return;
                    } else {
                        if (sDebugSearchCrateFeature) {
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
                    if (sDebugSearchCrateFeature) {
                        qDebug() << "[SEARCHCRATESFEATURE] [SLOT EDIT SEARCHCRATES] -> "
                                    "[NEW] "
                                    "-> END SearchCrate created searchCrateId "
                                 << searchCrateId;
                    }
                    emit updateSearchCrateData(searchCrateData);
                });
        // DLG -> Previous SearchCrate on 'Previous'
        connect(&infoDialog,
                &dlgSearchCrateInfo::requestPreviousSearchCrate,
                this,
                [this]() {
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    SearchCrateId previousSearchCrateId(searchCrateData[56]);
                    bool currentSearchCrateIdBOF(searchCrateData[57].toString() == "true");
                    SearchCrateId nextSearchCrateId(searchCrateData[58]);
                    bool currentSearchCrateIdEOF(searchCrateData[59].toString() == "true");
                    if (sDebugSearchCrateFeature) {
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
                            if (sDebugSearchCrateFeature) {
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
                            if (sDebugSearchCrateFeature) {
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
                &dlgSearchCrateInfo::requestNextSearchCrate,
                this,
                [this]() {
                    // current searchCrateId @ 0 prev/bof/next/eof pointers @ 56
                    SearchCrateId searchCrateId(searchCrateData[0]);
                    SearchCrateId previousSearchCrateId(searchCrateData[56]);
                    bool currentSearchCrateIdBOF(searchCrateData[57].toString() == "true");
                    SearchCrateId nextSearchCrateId(searchCrateData[58]);
                    bool currentSearchCrateIdEOF(searchCrateData[59].toString() == "true");
                    if (sDebugSearchCrateFeature) {
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
                            if (sDebugSearchCrateFeature) {
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
                            if (sDebugSearchCrateFeature) {
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
            if (sDebugSearchCrateFeature) {
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
                if (sDebugSearchCrateFeature) {
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

void SearchCrateFeature::slotToggleSearchCrateLock() {
    SearchCrate searchCrate;
    if (readLastRightClickedSearchCrate(&searchCrate)) {
        searchCrate.setLocked(!searchCrate.isLocked());
        if (!m_pTrackCollection->updateSearchCrate(searchCrate)) {
            qDebug() << "Failed to toggle lock of searchCrate" << searchCrate;
        }
    } else {
        qDebug() << "Failed to toggle lock of selected searchCrate";
    }
}

// void SearchCrateFeature::slotAutoDjTrackSourceChanged() {
//     SearchCrate searchCrate;
//     if (readLastRightClickedSearchCrate(&searchCrate)) {
//         if (searchCrate.isAutoDjSource() != m_pAutoDjTrackSourceAction->isChecked()) {
//             searchCrate.setAutoDjSource(m_pAutoDjTrackSourceAction->isChecked());
//             m_pTrackCollection->updateSearchCrate(searchCrate);
//         }
//     }
// }

QModelIndex SearchCrateFeature::rebuildChildModel(SearchCrateId selectedSearchCrateId) {
    if (sDebugSearchCrateFeature) {
        qDebug() << "[SEARCHCRATESFEATURE] [RebuildChildModel]";
    }

    m_lastRightClickedIndex = QModelIndex();

    TreeItem* pRootItem = m_pSidebarModel->getRootItem();
    VERIFY_OR_DEBUG_ASSERT(pRootItem != nullptr) {
        return QModelIndex();
    }
    m_pSidebarModel->removeRows(0, pRootItem->childRows());

    std::vector<std::unique_ptr<TreeItem>> modelRows;
    modelRows.reserve(m_pTrackCollection->searchCrates().countSearchCrates());

    int selectedRow = -1;
    SearchCrateSummarySelectResult searchCrateSummaries(
            m_pTrackCollection->searchCrates().selectSearchCrateSummaries());
    SearchCrateSummary searchCrateSummary;
    while (searchCrateSummaries.populateNext(&searchCrateSummary)) {
        modelRows.push_back(newTreeItemForSearchCrateSummary(searchCrateSummary));
        if (selectedSearchCrateId == searchCrateSummary.getId()) {
            // save index for selection
            selectedRow = static_cast<int>(modelRows.size()) - 1;
        }
    }

    // Append all the newly created TreeItems in a dynamic way to the childmodel
    m_pSidebarModel->insertTreeItemRows(std::move(modelRows), 0);

    // Update rendering of searchCrates depending on the currently selected track
    slotTrackSelected(m_selectedTrackId);

    if (selectedRow >= 0) {
        return m_pSidebarModel->index(selectedRow, 0);
    } else {
        return QModelIndex();
    }
}

void SearchCrateFeature::updateChildModel(const QSet<SearchCrateId>& updatedSearchCrateIds) {
    const SearchCrateStorage& searchCrateStorage = m_pTrackCollection->searchCrates();
    for (const SearchCrateId& searchCrateId : updatedSearchCrateIds) {
        QModelIndex index = indexFromSearchCrateId(searchCrateId);
        VERIFY_OR_DEBUG_ASSERT(index.isValid()) {
            continue;
        }
        SearchCrateSummary searchCrateSummary;
        VERIFY_OR_DEBUG_ASSERT(
                searchCrateStorage.readSearchCrateSummaryById(searchCrateId, &searchCrateSummary)) {
            continue;
        }
        updateTreeItemForSearchCrateSummary(
                m_pSidebarModel->getItem(index), searchCrateSummary);
        m_pSidebarModel->triggerRepaint(index);
    }

    if (m_selectedTrackId.isValid()) {
        // SearchCrates containing the currently selected track might
        // have been modified.
        slotTrackSelected(m_selectedTrackId);
    }
}

SearchCrateId SearchCrateFeature::searchCrateIdFromIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return SearchCrateId();
    }
    TreeItem* item = static_cast<TreeItem*>(index.internalPointer());
    if (item == nullptr) {
        return SearchCrateId();
    }
    return SearchCrateId(item->getData());
}

QModelIndex SearchCrateFeature::indexFromSearchCrateId(SearchCrateId searchCrateId) const {
    VERIFY_OR_DEBUG_ASSERT(searchCrateId.isValid()) {
        return QModelIndex();
    }
    for (int row = 0; row < m_pSidebarModel->rowCount(); ++row) {
        QModelIndex index = m_pSidebarModel->index(row, 0);
        TreeItem* pTreeItem = m_pSidebarModel->getItem(index);
        DEBUG_ASSERT(pTreeItem != nullptr);
        if (!pTreeItem->hasChildren() && // leaf node
                (SearchCrateId(pTreeItem->getData()) == searchCrateId)) {
            return index;
        }
    }
    qDebug() << "Tree item for searchCrate not found:" << searchCrateId;
    return QModelIndex();
}

// void SearchCrateFeature::slotImportPlaylist() {
//     // qDebug() << "slotImportPlaylist() row:" ; //<< m_lastRightClickedIndex.data();
//
//     QString playlistFile = getPlaylistFile();
//     if (playlistFile.isEmpty()) {
//         return;
//     }
//
//     // Update the import/export searchCrate directory
//     QFileInfo fileDirectory(playlistFile);
//     m_pConfig->set(kConfigKeyLastImportExportSearchCrateDirectoryKey,
//             ConfigValue(fileDirectory.absoluteDir().canonicalPath()));
//
//     SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
//     SearchCrate searchCrate;
//     if (m_pTrackCollection->searchCrates().readSearchCrateById(searchCrateId, &searchCrate)) {
//         qDebug() << "Importing playlist file" << playlistFile << "into searchCrate"
//                  << searchCrateId << searchCrate;
//     } else {
//         qDebug() << "Importing playlist file" << playlistFile << "into searchCrate"
//                  << searchCrateId << searchCrate << "failed!";
//         return;
//     }
//
//     slotImportPlaylistFile(playlistFile, searchCrateId);
//     activateChild(m_lastRightClickedIndex);
// }

// void SearchCrateFeature::slotImportPlaylistFile(
//         const QString& playlistFile, SearchCrateId searchCrateId) {
//     // The user has picked a new directory via a file dialog. This means the
//     // system sandboxer (if we are sandboxed) has granted us permission to
//     this
//     // folder. We don't need access to this file on a regular basis so we do
//     not
//     // register a security bookmark.
//     // TODO(XXX): Parsing a list of track locations from a playlist file
//     // is a general task and should be implemented separately.
//     QList<QString> locations = Parser().parse(playlistFile);
//     if (locations.empty()) {
//         return;
//     }
//
//     if (searchCrateId == m_searchCrateTableModel.selectedSearchCrate()) {
//         // Add tracks directly to the model
//         m_searchCrateTableModel.addTracks(QModelIndex(), locations);
//     } else {
//         // Create a temporary table model since the main one might have
//         another
//         // searchCrate selected which is not the searchCrate that received
//         the right-click. std::unique_ptr<SearchCrateTableModel>
//         pSearchCrateTableModel =
//                 std::make_unique<SearchCrateTableModel>(this,
//                 m_pLibrary->trackCollectionManager());
//         pSearchCrateTableModel->selectSearchCrate(searchCrateId);
//         pSearchCrateTableModel->select();
//         pSearchCrateTableModel->addTracks(QModelIndex(), locations);
//     }
// }

// void SearchCrateFeature::slotCreateImportSearchCrate() {
//     // Get file to read
//     const QStringList playlistFiles = LibraryFeature::getPlaylistFiles();
//     if (playlistFiles.isEmpty()) {
//         return;
//     }
//
//     // Set last import directory
//     QFileInfo fileDirectory(playlistFiles.first());
//     m_pConfig->set(kConfigKeyLastImportExportSearchCrateDirectoryKey,
//             ConfigValue(fileDirectory.absoluteDir().canonicalPath()));
//
//     SearchCrateId lastSearchCrateId;
//
//     // For each selected file create a new searchCrate
//     for (const QString& playlistFile : playlistFiles) {
//         const QFileInfo fileInfo(playlistFile);
//
//         SearchCrate searchCrate;
//
//         // Get a valid name
//         const QString baseName = fileInfo.completeBaseName();
//         for (int i = 0;; ++i) {
//             auto name = baseName;
//             if (i > 0) {
//                 name += QStringLiteral(" %1").arg(i);
//             }
//             name = name.trimmed();
//             if (!name.isEmpty()) {
//                 if (!m_pTrackCollection->searchCrates().readSearchCrateByName(name)) {
//                     // unused searchCrate name found
//                     searchCrate.setName(std::move(name));
//                     DEBUG_ASSERT(searchCrate.hasName());
//                     break; // terminate loop
//                 }
//             }
//         }
//
//         if (!m_pTrackCollection->insertSearchCrate(searchCrate, &lastSearchCrateId)) {
//             QMessageBox::warning(nullptr,
//                     tr("SearchCrate Creation Failed"),
//                     tr("An unknown error occurred while creating searchCrate: ") +
//                             searchCrate.getName());
//             return;
//         }
//
//         slotImportPlaylistFile(playlistFile, lastSearchCrateId);
//     }
//     activateSearchCrate(lastSearchCrateId);
// }

void SearchCrateFeature::slotAnalyzeSearchCrate() {
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

void SearchCrateFeature::slotExportPlaylist() {
    SearchCrateId searchCrateId = searchCrateIdFromIndex(m_lastRightClickedIndex);
    SearchCrate searchCrate;
    if (m_pTrackCollection->searchCrates().readSearchCrateById(searchCrateId, &searchCrate)) {
        qDebug() << "Exporting searchCrate" << searchCrateId << searchCrate;
    } else {
        qDebug() << "Failed to export searchCrate" << searchCrateId;
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
    // Update the import/export searchCrate directory
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

void SearchCrateFeature::slotExportTrackFiles() {
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

void SearchCrateFeature::storePrevSiblingSearchCrateId(SearchCrateId searchCrateId) {
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

void SearchCrateFeature::slotSearchCrateTableChanged(SearchCrateId searchCrateId) {
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

void SearchCrateFeature::slotSearchCrateContentChanged(SearchCrateId searchCrateId) {
    QSet<SearchCrateId> updatedSearchCrateIds;
    updatedSearchCrateIds.insert(searchCrateId);
    updateChildModel(updatedSearchCrateIds);
}

void SearchCrateFeature::slotUpdateSearchCrateLabels(
        const QSet<SearchCrateId>& updatedSearchCrateIds) {
    updateChildModel(updatedSearchCrateIds);
}

void SearchCrateFeature::htmlLinkClicked(const QUrl& link) {
    if (QString(link.path()) == "create") {
        slotCreateSearchCrate();
    } else {
        qDebug() << "Unknown searchCrate link clicked" << link;
    }
}

void SearchCrateFeature::slotTrackSelected(TrackId trackId) {
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
    for (TreeItem* pTreeItem : pRootItem->children()) {
        DEBUG_ASSERT(pTreeItem != nullptr);
        bool searchCrateContainsSelectedTrack =
                m_selectedTrackId.isValid() &&
                std::binary_search(
                        sortedTrackSearchCrates.begin(),
                        sortedTrackSearchCrates.end(),
                        SearchCrateId(pTreeItem->getData()));
        pTreeItem->setBold(searchCrateContainsSelectedTrack);
    }

    m_pSidebarModel->triggerRepaint();
}

void SearchCrateFeature::slotResetSelectedTrack() {
    slotTrackSelected(TrackId{});
}
