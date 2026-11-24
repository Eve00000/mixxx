#pragma once

#include <QDialog>
#include <QList>
#include <QSet>

#include "library/dao/relationsdao.h"
#include "library/relations/ui_dlgrelationinfo.h"

class Library;

class DlgRelationInfo : public QDialog, public Ui::DlgRelationInfo {
    Q_OBJECT
  public:
    explicit DlgRelationInfo(
            RelationsDao::RelationGroup group,
            Library* pLibrary = nullptr);
    ~DlgRelationInfo() override = default;

  private slots:
    void slotOk();
    void slotCancel();
    void slotRemoveTrack();

  private:
    void init();
    bool saveRelation();

    RelationsDao::RelationGroup m_group;
    Library* m_pLibrary;
    QSet<TrackId> m_originalTrackIds;
};
