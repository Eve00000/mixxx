#pragma once

#include <QDialog>

#include "track/trackid.h"

class Library;
class QComboBox;
class QTableWidget;

class DlgLinkedTracks : public QDialog {
    Q_OBJECT
  public:
    // Shows the linked tracks window of a track. Only one window is open at a
    // time: an open window is replaced.
    static void showForTrack(QWidget* pParent, Library* pLibrary, TrackId trackId);

    void done(int result) override;

  private:
    DlgLinkedTracks(QWidget* pParent, Library* pLibrary, TrackId trackId);

    void selectInLibrary(TrackId trackId);

    Library* m_pLibrary;
    QTableWidget* m_pTableLinkedTracks;
    QComboBox* m_pComboBoxLinkedTracksType;
};
