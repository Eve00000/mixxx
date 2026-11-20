#pragma once

#include "library/dao/relationsdao.h"
#include "preferences/usersettings.h"
#include "widget/wtracktableview.h"

class Library;
class RelationsTableModel;

class WRelationTableView : public WTrackTableView {
    Q_OBJECT
  public:
    WRelationTableView(
            QWidget* parent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            double trackTableBackgroundColorOpacity,
            bool drawGroupSeparators);

    QList<DbId> getSelectedRelationIds() const;
    RelationsDao::RelationGroup getSelectedGroup();

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    Library* m_pLibrary;
    bool m_bDrawGroupSeparators;
};
