#pragma once

#include <QDialog>

#include "util/db/dbid.h"

class Library;
class QTableWidget;
class QTableWidgetItem;
class RelationsDao;

class DlgRelationTypes : public QDialog {
    Q_OBJECT
  public:
    DlgRelationTypes(QWidget* pParent, Library* pLibrary);

  private slots:
    void slotItemChanged(QTableWidgetItem* pItem);
    void slotMerge();
    void slotDelete();
    void slotDeleteUnused();

  private:
    RelationsDao& relationsDao() const;
    DbId selectedRelationId() const;
    void reload();

    Library* m_pLibrary;
    QTableWidget* m_pTable;
};
