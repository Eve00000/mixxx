#pragma once

#include <QString>
#include <QWidget>

#include "library/autosuggestions/autosuggestionsprocessor.h"
#include "library/autosuggestions/ui_dlgautosuggestions.h"
#include "library/libraryview.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"

class PlaylistTableModel;
class WLibrary;
class WTrackTableView;
class Library;
class KeyboardEventFilter;

class DlgAutoSuggestions : public QWidget, public Ui::DlgAutoSuggestions, public LibraryView {
    Q_OBJECT
  public:
    DlgAutoSuggestions(WLibrary* parent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            AutoSuggestionsProcessor* pProcessor,
            KeyboardEventFilter* pKeyboard);
    ~DlgAutoSuggestions() override;

    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    void pasteFromSidebar() override;
    void onSearch(const QString& text) override;
    void saveCurrentViewState() override;
    bool restoreCurrentViewState() override;

  public slots:
    void checkNowButton(bool buttonChecked);
    void toggleAutoSuggestionsButton(bool enable);
    void autoSuggestionsError(AutoSuggestionsProcessor::AutoSuggestionsError error);
    void refreshRateChanged(int time);
    void refreshRateSliderChanged(int value);
    void autoDJStateChanged(AutoSuggestionsProcessor::AutoSuggestionsState state);
    void updateSelectionInfo();

  signals:
    void loadTrack(TrackPointer tio);
#ifdef __STEM__
    void loadTrackToPlayer(TrackPointer tio,
            const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool);
#else
    void loadTrackToPlayer(TrackPointer tio, const QString& group, bool);
#endif
    void trackSelected(TrackPointer pTrack);

  private:
    void setupActionButton(QPushButton* pButton,
            void (DlgAutoSuggestions::*pSlot)(bool),
            const QString& fallbackText);
    void keyPressEvent(QKeyEvent* pEvent) override;

    const UserSettingsPointer m_pConfig;

    AutoSuggestionsProcessor* const m_pAutoSuggestionsProcessor;
    WTrackTableView* const m_pTrackTableView;
    const bool m_bShowButtonText;

    PlaylistTableModel* m_pAutoSuggestionsTableModel;

    QString m_enableBtnTooltip;
    QString m_disableBtnTooltip;
};
