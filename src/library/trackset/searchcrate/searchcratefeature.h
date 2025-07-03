#pragma once

#include <QList>
#include <QModelIndex>
#include <QPointer>
#include <QUrl>
#include <QVariant>

#include "library/trackset/basetracksetfeature.h"
#include "library/trackset/searchcrate/searchcrate.h"
#include "library/trackset/searchcrate/searchcratetablemodel.h"
#include "preferences/usersettings.h"
#include "track/trackid.h"
#include "util/parented_ptr.h"

// forward declaration(s)
class Library;
class WLibrarySidebar;
class QAction;
class QPoint;
class SearchCrateSummary;

class SearchCrateFeature : public BaseTrackSetFeature {
    Q_OBJECT

  public:
    SearchCrateFeature(Library* pLibrary,
            UserSettingsPointer pConfig);
    ~SearchCrateFeature() override = default;

    QVariant title() override;

    bool dropAcceptChild(const QModelIndex& index,
            const QList<QUrl>& urls,
            QObject* pSource) override;
    bool dragMoveAcceptChild(const QModelIndex& index, const QUrl& url) override;

    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;
    void bindSidebarWidget(WLibrarySidebar* pSidebarWidget) override;

    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;
    void slotCreateSearchCrate();
    void deleteItem(const QModelIndex& index) override;
    void renameItem(const QModelIndex& index) override;

#ifdef __ENGINEPRIME__
  signals:
    void exportAllSearchCrates();
    // void exportSearchCrate(SearchCrateId searchCrateId);
#endif

  private slots:
    void slotDeleteSearchCrate();
    void slotRenameSearchCrate();
    void slotDuplicateSearchCrate();
    void slotAutoDjTrackSourceChanged();
    void slotToggleSearchCrateLock();
    void slotImportPlaylist();
    void slotImportPlaylistFile(const QString& playlistFile, SearchCrateId searchCrateId);
    void slotCreateImportSearchCrate();
    void slotExportPlaylist();
    // Copy all of the tracks in a searchCrate to a new directory (like a thumbdrive).
    void slotExportTrackFiles();
    void slotAnalyzeSearchCrate();
    void slotSearchCrateTableChanged(SearchCrateId searchCrateId);
    void slotSearchCrateContentChanged(SearchCrateId searchCrateId);
    void htmlLinkClicked(const QUrl& link);
    void slotTrackSelected(TrackId trackId);
    void slotResetSelectedTrack();
    void slotUpdateSearchCrateLabels(const QSet<SearchCrateId>& updatedSearchCrateIds);

  private:
    void initActions();
    void connectLibrary(Library* pLibrary);
    void connectTrackCollection();

    bool activateSearchCrate(SearchCrateId searchCrateId);

    std::unique_ptr<TreeItem> newTreeItemForSearchCrateSummary(
            const SearchCrateSummary& searchCrateSummary);
    void updateTreeItemForSearchCrateSummary(
            TreeItem* pTreeItem,
            const SearchCrateSummary& searchCrateSummary) const;

    QModelIndex rebuildChildModel(SearchCrateId selectedSearchCrateId = SearchCrateId());
    void updateChildModel(const QSet<SearchCrateId>& updatedSearchCrateIds);

    SearchCrateId searchCrateIdFromIndex(const QModelIndex& index) const;
    QModelIndex indexFromSearchCrateId(SearchCrateId searchCrateId) const;

    bool isChildIndexSelectedInSidebar(const QModelIndex& index);
    bool readLastRightClickedSearchCrate(SearchCrate* pSearchCrate) const;

    QString formatRootViewHtml() const;

    const QIcon m_lockedSearchCrateIcon;

    TrackCollection* const m_pTrackCollection;

    SearchCrateTableModel m_searchCrateTableModel;

    // Stores the id of a searchCrate in the sidebar that is adjacent to the
    // searchCrate(searchCrateId).
    void storePrevSiblingSearchCrateId(SearchCrateId searchCrateId);
    // Can be used to restore a similar selection after the sidebar model was rebuilt.
    SearchCrateId m_prevSiblingSearchCrate;

    QModelIndex m_lastClickedIndex;
    QModelIndex m_lastRightClickedIndex;
    TrackId m_selectedTrackId;

    parented_ptr<QAction> m_pCreateSearchCrateAction;
    parented_ptr<QAction> m_pDeleteSearchCrateAction;
    parented_ptr<QAction> m_pRenameSearchCrateAction;
    parented_ptr<QAction> m_pLockSearchCrateAction;
    parented_ptr<QAction> m_pDuplicateSearchCrateAction;
    parented_ptr<QAction> m_pAutoDjTrackSourceAction;
    parented_ptr<QAction> m_pImportPlaylistAction;
    parented_ptr<QAction> m_pCreateImportPlaylistAction;
    parented_ptr<QAction> m_pExportPlaylistAction;
    parented_ptr<QAction> m_pExportTrackFilesAction;
#ifdef __ENGINEPRIME__
    parented_ptr<QAction> m_pExportAllSearchCratesAction;
    parented_ptr<QAction> m_pExportSearchCrateAction;
#endif
    parented_ptr<QAction> m_pAnalyzeSearchCrateAction;

    QPointer<WLibrarySidebar> m_pSidebarWidget;
};
