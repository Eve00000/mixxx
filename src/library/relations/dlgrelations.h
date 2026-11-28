#pragma once

#include "library/libraryview.h"
#include "library/ui_dlgrelations.h"

class WLibrary;
class WRelationTableView;
class RelationsTableModel;
class DlgRelationInfo;
class QItemSelection;
class Library;
class KeyboardEventFilter;

class DlgRelations : public QWidget, public Ui::DlgRelations, public LibraryView {
    Q_OBJECT

  public:
    DlgRelations(WLibrary* parent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard,
            bool relationGroupView);
    ~DlgRelations() override;

    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    void onSearch(const QString& text) override;
    QString currentSearch();
    void saveCurrentViewState() override;
    bool restoreCurrentViewState() override;

  public slots:
    void installEventFilter(QObject* pFilter);
    void showAllRelations();
    void showRelatedTracks(TrackPointer pTrack);
    void selectionChanged(const QItemSelection&, const QItemSelection&);
    void slotSaveRelationFromDecks();
    void slotEditRelation();
    void slotDeleteRelation();
    void slotRemoveTrackFromRelation();
    void slotManageRelationTypes();
    void slotTypeFilterChanged(int index);
    void slotAutoCreateRelations();

  signals:
    void trackSelected(TrackPointer pTrack);
    void loadTrack(TrackPointer pTrack);
    void loadTrackToPlayer(TrackPointer pTrack, const QString& player);

  private:
    void activateButtons(bool enable);
    void refreshTypeFilter();

    RelationsTableModel* m_pRelationTableModel;
    WRelationTableView* m_pRelationTableView;

    Library* m_pLibrary;

    bool m_bRelationGroupView;
};
