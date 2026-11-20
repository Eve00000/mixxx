#pragma once

#include <QDialog>
#include <QList>

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

  private:
    void init();
    void saveRelation();

    RelationsDao::RelationGroup m_group;
    Library* m_pLibrary;
};
