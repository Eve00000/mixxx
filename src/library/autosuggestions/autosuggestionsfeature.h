#pragma once

#include <QCryptographicHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariant>

#include "library/libraryfeature.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class DlgAutoSuggestions;
class Library;
class PlayerManagerInterface;
class TrackCollection;
class AutoSuggestionsProcessor;
class WLibrarySidebar;
class QAction;
class QModelIndex;
class QPoint;

class AutoSuggestionsFeature : public LibraryFeature {
    Q_OBJECT
  public:
    AutoSuggestionsFeature(Library* pLibrary,
            UserSettingsPointer pConfig);
    virtual ~AutoSuggestionsFeature();

    QVariant title() override;

    void clear() override;
    void paste() override;
    void deleteItem(const QModelIndex& index) override;

    bool dropAccept(const QList<QUrl>& urls, QObject* pSource) override;
    bool dragMoveAccept(const QList<QUrl>& urls) override;

    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;
    void bindSidebarWidget(WLibrarySidebar* pSidebarWidget) override;

    TreeItemModel* sidebarModel() const override;

    bool hasTrackTable() override {
        return true;
    }

  public slots:
    void activate() override;

    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;
    void ResetFileTracking();

  private slots:
    void slotEnableAutoSuggestions();
    void slotDisableAutoSuggestions();
    void slotClearQueue();
    void SlotCheckAutoSuggestionsFileNow();
    void SlotForceCheckAutoSuggestionsFileNow();
    void slotRemoveAllSuggestions();

  private:
    TrackCollection* const m_pTrackCollection;

    PlaylistDAO& m_playlistDao;
    int m_iAutoSuggestionsPlaylistId;
    AutoSuggestionsProcessor* m_pAutoSuggestionsProcessor;
    parented_ptr<TreeItemModel> m_pSidebarModel;
    DlgAutoSuggestions* m_pAutoSuggestionsView;
    const QString m_viewName;

    void ImportAutoSuggestionFile();

    parented_ptr<QAction> m_pEnableAutoSuggestionsAction;
    parented_ptr<QAction> m_pDisableAutoSuggestionsAction;
    parented_ptr<QAction> m_pClearQueueAction;

    parented_ptr<QAction> m_pRemoveCrateFromAutoDjAction;
    QPointer<WLibrarySidebar> m_pSidebarWidget;

    qint64 m_lastFileTimestamp;
    QByteArray m_lastFileHash;
    QString m_lastAutoSuggestionsFile;
};
