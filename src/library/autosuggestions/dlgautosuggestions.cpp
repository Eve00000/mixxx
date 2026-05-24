#include "library/autosuggestions/dlgautosuggestions.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/playlisttablemodel.h"
#include "moc_dlgautosuggestions.cpp"
#include "track/track.h"
#include "util/assert.h"
#include "util/duration.h"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"

namespace {
const bool sDebugAutoSuggestionDialog = false;
} // anonymous namespace

DlgAutoSuggestions::DlgAutoSuggestions(WLibrary* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        AutoSuggestionsProcessor* pProcessor,
        KeyboardEventFilter* pKeyboard)
        : QWidget(parent),
          Ui::DlgAutoSuggestions(),
          m_pConfig(pConfig),
          m_pAutoSuggestionsProcessor(pProcessor),
          m_pTrackTableView(new WTrackTableView(this,
                  m_pConfig,
                  pLibrary,
                  parent->getTrackTableBackgroundColorOpacity(),
                  /*no sorting*/ false)),
          m_bShowButtonText(parent->getShowButtonText()),
          m_pAutoSuggestionsTableModel(nullptr) {
    setupUi(this);

    m_pTrackTableView->installEventFilter(pKeyboard);

    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            this,
            &DlgAutoSuggestions::loadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            this,
            &DlgAutoSuggestions::loadTrackToPlayer);
    connect(m_pTrackTableView,
            &WTrackTableView::trackSelected,
            this,
            &DlgAutoSuggestions::trackSelected);
    connect(m_pTrackTableView,
            &WTrackTableView::trackSelected,
            this,
            &DlgAutoSuggestions::updateSelectionInfo);

    connect(pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableFont);
    connect(pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableRowHeight);
    connect(pLibrary,
            &Library::setSelectedClick,
            m_pTrackTableView,
            &WTrackTableView::setSelectedClick);

    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    VERIFY_OR_DEBUG_ASSERT(box) {
    }
    else {
        box->removeWidget(m_pTrackTablePlaceholder);
        m_pTrackTablePlaceholder->hide();
        box->insertWidget(1, m_pTrackTableView);
    }

    m_pAutoSuggestionsTableModel = m_pAutoSuggestionsProcessor->getTableModel();
    m_pTrackTableView->loadTrackModel(m_pAutoSuggestionsTableModel);

    connect(pushButtonAutoSuggestions,
            &QPushButton::clicked,
            this,
            &DlgAutoSuggestions::toggleAutoSuggestionsButton);

    setupActionButton(pushButtonCheckNow, &DlgAutoSuggestions::checkNowButton, tr("Check"));

    m_enableBtnTooltip = tr(
            "Enable Auto Suggestions\n"
            "\n"
            "No Shortcut yet");
    m_disableBtnTooltip = tr(
            "Disable Auto Suggestions\n"
            "\n"
            "NO Shortcut yet");
    QString checkNowBtnTooltip = tr(
            "Trigger the manual check for suggestions\n"
            "\n"
            "NO Shortcut yet");
    QString spinBoxRefreshRateTooltip = tr(
            "Determines the refresh interval");
    QString labelRefreshRateTooltip = tr(
            "Seconds");

    pushButtonCheckNow->setToolTip(checkNowBtnTooltip);
    spinBoxRefreshRate->setToolTip(spinBoxRefreshRateTooltip);
    labelRefreshRateAppendix->setToolTip(labelRefreshRateTooltip);

    spinBoxRefreshRate->setFocusPolicy(Qt::ClickFocus);
    QLineEdit* lineEditrefreshRate(spinBoxRefreshRate->findChild<QLineEdit*>());
    lineEditrefreshRate->setFocusPolicy(Qt::ClickFocus);
    lineEditrefreshRate->installEventFilter(this);

    connect(spinBoxRefreshRate,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &DlgAutoSuggestions::refreshRateSliderChanged);

    spinBoxRefreshRate->setValue(static_cast<int>(m_pAutoSuggestionsProcessor->getRefreshRate()));
    connect(m_pAutoSuggestionsProcessor,
            &AutoSuggestionsProcessor::refreshRateChanged,
            this,
            &DlgAutoSuggestions::refreshRateChanged);

    connect(m_pAutoSuggestionsProcessor,
            &AutoSuggestionsProcessor::autoSuggestionsError,
            this,
            &DlgAutoSuggestions::autoSuggestionsError);

    connect(m_pAutoSuggestionsProcessor,
            &AutoSuggestionsProcessor::autoSuggestionsStateChanged,
            this,
            &DlgAutoSuggestions::autoDJStateChanged);
    autoDJStateChanged(m_pAutoSuggestionsProcessor->getState());

    updateSelectionInfo();
}

DlgAutoSuggestions::~DlgAutoSuggestions() {
    if (sDebugAutoSuggestionDialog) {
        qDebug() << "~DlgAutoSuggestions()";
    }
    delete m_pTrackTableView;
}

void DlgAutoSuggestions::setupActionButton(QPushButton* pButton,
        void (DlgAutoSuggestions::*pSlot)(bool),
        const QString& fallbackText) {
    connect(pButton, &QPushButton::clicked, this, pSlot);
    if (m_bShowButtonText) {
        pButton->setText(fallbackText);
    }
}

void DlgAutoSuggestions::onShow() {
    m_pAutoSuggestionsTableModel->select();
}

void DlgAutoSuggestions::onSearch(const QString& text) {
    Q_UNUSED(text);
}

void DlgAutoSuggestions::checkNowButton(bool) {
    m_pAutoSuggestionsProcessor->checkNow();
}

void DlgAutoSuggestions::toggleAutoSuggestionsButton(bool enable) {
    m_pAutoSuggestionsProcessor->toggleAutoSuggestions(enable);
}

void DlgAutoSuggestions::autoSuggestionsError(
        AutoSuggestionsProcessor::AutoSuggestionsError error) {
    switch (error) {
    case AutoSuggestionsProcessor::ASS_BLABLABLA2222:
        QMessageBox::warning(nullptr,
                tr("Auto Suggestions"),
                tr("Auto Suggestions"),
                QMessageBox::Ok);
        break;
    case AutoSuggestionsProcessor::ASS_OK:
    default:
        break;
    }
}

void DlgAutoSuggestions::refreshRateChanged(int time) {
    spinBoxRefreshRate->setValue(time);
}

void DlgAutoSuggestions::refreshRateSliderChanged(int value) {
    m_pAutoSuggestionsProcessor->setRefreshRate(value);
}

void DlgAutoSuggestions::autoDJStateChanged(AutoSuggestionsProcessor::AutoSuggestionsState state) {
    if (state == AutoSuggestionsProcessor::ASS_DISABLED) {
        pushButtonAutoSuggestions->setChecked(false);
        pushButtonAutoSuggestions->setToolTip(m_enableBtnTooltip);
        if (m_bShowButtonText) {
            pushButtonAutoSuggestions->setText(tr("Enable"));
        }
        pushButtonCheckNow->setEnabled(false);
    } else {
        pushButtonAutoSuggestions->setChecked(true);
        pushButtonAutoSuggestions->setToolTip(m_disableBtnTooltip);

        pushButtonCheckNow->setEnabled(true);
    }
}

void DlgAutoSuggestions::updateSelectionInfo() {
    QModelIndexList indices = m_pTrackTableView->selectionModel()->selectedRows();

    mixxx::Duration duration = m_pAutoSuggestionsTableModel->getTotalDuration(indices);

    QString label;

    if (!indices.isEmpty()) {
        label.append(mixxx::DurationBase::formatTime(duration.toDoubleSeconds()));
        label.append(QString(" (%1)").arg(indices.size()));
        labelSelectionInfo->setToolTip(tr("Displays the duration and number of selected tracks."));
        labelSelectionInfo->setText(label);
        labelSelectionInfo->setEnabled(true);
    } else {
        labelSelectionInfo->setText("");
        labelSelectionInfo->setEnabled(false);
    }
}

bool DlgAutoSuggestions::hasFocus() const {
    return m_pTrackTableView->hasFocus();
}

void DlgAutoSuggestions::setFocus() {
    m_pTrackTableView->setFocus();
}

void DlgAutoSuggestions::pasteFromSidebar() {
    m_pTrackTableView->pasteFromSidebar();
}

void DlgAutoSuggestions::keyPressEvent(QKeyEvent* pEvent) {
    if (pEvent->key() == Qt::Key_Return ||
            pEvent->key() == Qt::Key_Enter ||
            pEvent->key() == Qt::Key_Escape) {
        ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
        return;
    }
    QWidget::keyPressEvent(pEvent);
}

void DlgAutoSuggestions::saveCurrentViewState() {
    m_pTrackTableView->saveCurrentViewState();
}

bool DlgAutoSuggestions::restoreCurrentViewState() {
    return m_pTrackTableView->restoreCurrentViewState();
}
