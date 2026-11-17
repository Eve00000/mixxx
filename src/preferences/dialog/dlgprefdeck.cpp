#include "preferences/dialog/dlgprefdeck.h"

#include <QDoubleSpinBox>
#include <QStandardItemModel>
#include <algorithm>
#include <iterator>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "defs_urls.h"
#include "engine/controls/ratecontrol.h"
#include "engine/sync/enginesync.h"
#include "mixer/basetrackplayer.h"
#include "mixer/playermanager.h"
#include "moc_dlgprefdeck.cpp"
#include "preferences/usersettings.h"
#include "util/duration.h"

namespace {
constexpr int kDefaultRateRangePercent = 8;
constexpr double kRateDirectionInverted = -1;
constexpr bool kDefaultRateDirectionInverted = true;
constexpr RateControl::RampMode kDefaultRampingMode = RateControl::RampMode::Stepping;
constexpr double kDefaultTemporaryRateChangeCoarse = 4.00; // percent
constexpr double kDefaultTemporaryRateChangeFine = 2.00;
constexpr double kDefaultPermanentRateChangeCoarse = 0.50;
constexpr double kDefaultPermanentRateChangeFine = 0.05;
constexpr int kDefaultRateRampSensitivity = 250;
constexpr double kDefaultPositionDisplayType =
        static_cast<double>(TrackTime::DisplayMode::ELAPSED_AND_REMAINING);
constexpr int kDefaultRampTimePercent = 100; // Technics SL-1210

// Backspin power: "rotation" backward "in" seconds
struct Fraction {
    int numerator;
    int denominator;
    double value() const {
        return static_cast<double>(numerator) / denominator;
    }
    QString text() const {
        return denominator == 1
                ? QString::number(numerator)
                : QStringLiteral("%1/%2").arg(numerator).arg(denominator);
    }
};
constexpr Fraction kBackspinRotations[] = {{1, 4}, {1, 3}, {1, 2}, {1, 1}};
constexpr Fraction kBackspinTimes[] = {{1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}};
// 1/2 rotation in 1/2 s = 1 rev/s
constexpr int kDefaultBackspinRotation = 2;
constexpr int kDefaultBackspinTime = 1;

// bool kDefaultCloneDeckOnLoad is defined in header file to make it available
// to playermanager.cpp
const QString kAppGroup = QStringLiteral("[App]");
const QString kControlsGroup = QStringLiteral("[Controls]");
} // namespace

DlgPrefDeck::DlgPrefDeck(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_pConfig(pConfig),
          m_pControlTrackTimeDisplay(std::make_unique<ControlObject>(
                  ConfigKey(kControlsGroup, QStringLiteral("ShowDurationRemaining")))),
          m_pControlTrackTimeFormat(std::make_unique<ControlObject>(
                  ConfigKey(kControlsGroup, QStringLiteral("TimeFormat")))),
          m_pNumDecks(make_parented<ControlProxy>(
                  kAppGroup, QStringLiteral("num_decks"), this)),
          m_pNumSamplers(make_parented<ControlProxy>(
                  kAppGroup, QStringLiteral("num_samplers"), this)),
          m_iNumConfiguredDecks(0),
          m_iNumConfiguredSamplers(0) {
    setupUi(this);
    // Create text color for the cue mode link "?" to the manual
    createLinkColor();

    m_pNumDecks->connectValueChanged(this, [=, this](double value) { slotNumDecksChanged(value); });
    slotNumDecksChanged(m_pNumDecks->get(), true);

    m_pNumSamplers->connectValueChanged(
            this, [=, this](double value) { slotNumSamplersChanged(value); });
    slotNumSamplersChanged(m_pNumSamplers->get(), true);

    // Set default value in config file and control objects, if not present
    // Default is "0" = Mixxx Mode
    int cueDefaultValue = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("CueDefault")), 0);

    // Update combo box
    ComboBoxCueMode->addItem(tr("Mixxx mode"), static_cast<int>(CueMode::Mixxx));
    ComboBoxCueMode->addItem(tr("Mixxx mode (no blinking)"), static_cast<int>(CueMode::MixxxNoBlinking));
    ComboBoxCueMode->addItem(tr("Pioneer mode"), static_cast<int>(CueMode::Pioneer));
    ComboBoxCueMode->addItem(tr("Denon mode"), static_cast<int>(CueMode::Denon));
    ComboBoxCueMode->addItem(tr("Numark mode"), static_cast<int>(CueMode::Numark));
    ComboBoxCueMode->addItem(tr("CUP mode"), static_cast<int>(CueMode::CueAndPlay));
    const int cueModeIndex = cueDefaultIndexByData(cueDefaultValue);
    ComboBoxCueMode->setCurrentIndex(cueModeIndex);
    slotCueModeCombobox(cueModeIndex);
    for (ControlProxy* pControl : std::as_const(m_cueControls)) {
        pControl->set(static_cast<int>(m_cueMode));
    }
    connect(ComboBoxCueMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::slotCueModeCombobox);

    // Track time display configuration
    connect(m_pControlTrackTimeDisplay.get(),
            &ControlObject::valueChanged,
            this,
            QOverload<double>::of(&DlgPrefDeck::slotSetTrackTimeDisplay));

    double positionDisplayType = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("PositionDisplay")),
            kDefaultPositionDisplayType);
    if (positionDisplayType ==
            static_cast<double>(TrackTime::DisplayMode::REMAINING)) {
        radioButtonRemaining->setChecked(true);
        m_pControlTrackTimeDisplay->set(
            static_cast<double>(TrackTime::DisplayMode::REMAINING));
    } else if (positionDisplayType ==
                   static_cast<double>(TrackTime::DisplayMode::ELAPSED_AND_REMAINING)) {
        radioButtonElapsedAndRemaining->setChecked(true);
        m_pControlTrackTimeDisplay->set(
            static_cast<double>(TrackTime::DisplayMode::ELAPSED_AND_REMAINING));
    } else {
        radioButtonElapsed->setChecked(true);
        m_pControlTrackTimeDisplay->set(
            static_cast<double>(TrackTime::DisplayMode::ELAPSED));
    }
    connect(buttonGroupTrackTime,
            QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this,
            QOverload<QAbstractButton*>::of(
                    &DlgPrefDeck::slotSetTrackTimeDisplay));

    // display time format
    connect(m_pControlTrackTimeFormat.get(),
            &ControlObject::valueChanged,
            this,
            &DlgPrefDeck::slotTimeFormatChanged);

    // Track Display model
    comboBoxTimeFormat->clear();

    comboBoxTimeFormat->addItem(tr("mm:ss%1zz - Traditional")
                                .arg(mixxx::DurationBase::kDecimalSeparator),
                                static_cast<int>
                                (TrackTime::DisplayFormat::TRADITIONAL));

    comboBoxTimeFormat->addItem(tr("mm:ss - Traditional (Coarse)"),
                                static_cast<int>
                                (TrackTime::DisplayFormat::TRADITIONAL_COARSE));

    comboBoxTimeFormat->addItem(tr("s%1zz - Seconds")
                                .arg(mixxx::DurationBase::kDecimalSeparator),
                                static_cast<int>
                                (TrackTime::DisplayFormat::SECONDS));

    comboBoxTimeFormat->addItem(tr("sss%1zz - Seconds (Long)")
                                .arg(mixxx::DurationBase::kDecimalSeparator),
                                static_cast<int>
                                (TrackTime::DisplayFormat::SECONDS_LONG));

    comboBoxTimeFormat->addItem(tr("s%1sss%2zz - Kiloseconds")
                                .arg(QString(mixxx::DurationBase::kDecimalSeparator),
                                     QString(mixxx::DurationBase::kKiloGroupSeparator)),
                                static_cast<int>
                                (TrackTime::DisplayFormat::KILO_SECONDS));

    double time_format = static_cast<double>(
            m_pConfig->getValue(
                    ConfigKey(kControlsGroup, QStringLiteral("TimeFormat")),
                    static_cast<int>(TrackTime::DisplayFormat::TRADITIONAL)));
    m_pControlTrackTimeFormat->set(time_format);
    comboBoxTimeFormat->setCurrentIndex(
                comboBoxTimeFormat->findData(time_format));

    comboBoxLoadPoint->addItem(tr("Intro start"), static_cast<int>(SeekOnLoadMode::IntroStart));
    comboBoxLoadPoint->addItem(tr("Main cue"), static_cast<int>(SeekOnLoadMode::MainCue));
    comboBoxLoadPoint->addItem(tr("First hotcue"), static_cast<int>(SeekOnLoadMode::FirstHotcue));
    comboBoxLoadPoint->addItem(tr("First sound (skip silence)"), static_cast<int>(SeekOnLoadMode::FirstSound));
    comboBoxLoadPoint->addItem(tr("Beginning of track"), static_cast<int>(SeekOnLoadMode::Beginning));
    bool seekModeExisted = m_pConfig->exists(
            ConfigKey(kControlsGroup, QStringLiteral("CueRecall")));
    int seekMode = m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("CueRecall")),
            static_cast<int>(SeekOnLoadMode::IntroStart));
    comboBoxLoadPoint->setCurrentIndex(
            comboBoxLoadPoint->findData(seekMode));
    m_seekOnLoadMode = static_cast<SeekOnLoadMode>(seekMode);
    connect(comboBoxLoadPoint,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::slotSetTrackLoadMode);

    comboBoxLoadWhenDeckPlaying->addItem(
            tr("Reject"), static_cast<int>(LoadWhenDeckPlaying::Reject));
    comboBoxLoadWhenDeckPlaying->addItem(tr("Allow, but stop deck"),
            static_cast<int>(LoadWhenDeckPlaying::AllowButStopDeck));
    comboBoxLoadWhenDeckPlaying->addItem(tr("Allow, play from load point"),
            static_cast<int>(LoadWhenDeckPlaying::Allow));
    int loadWhenDeckPlaying;
    if (m_pConfig->exists(kConfigKeyLoadWhenDeckPlaying)) {
        loadWhenDeckPlaying = m_pConfig->getValueString(kConfigKeyLoadWhenDeckPlaying).toInt();
    } else {
        // upgrade from older versions
        if (m_pConfig->getValue(kConfigKeyAllowTrackLoadToPlayingDeck, false)) {
            loadWhenDeckPlaying = static_cast<int>(LoadWhenDeckPlaying::Allow);
        } else {
            loadWhenDeckPlaying = static_cast<int>(kDefaultLoadWhenDeckPlaying);
        }
    }
    comboBoxLoadWhenDeckPlaying->setCurrentIndex(
            comboBoxLoadWhenDeckPlaying->findData(loadWhenDeckPlaying));
    m_loadWhenDeckPlaying = static_cast<LoadWhenDeckPlaying>(loadWhenDeckPlaying);
    connect(comboBoxLoadWhenDeckPlaying,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::slotLoadWhenDeckPlayingIndexChanged);

    // This option was introduced in Mixxx 2.3 with the intro & outro cues.
    // If the user has set main cue points with the intention of starting tracks
    // from those points, enable this option. With Denon and Numark CueModes,
    // it is not safe to assume that the user wants to start tracks from the
    // main cue point because it is very easy to move the main cue point without
    // explicitly intending to in those modes (the main cue point moves whenever
    // the deck is not at the main cue point and play is pressed).
    bool introStartMoveDefault = (m_seekOnLoadMode == SeekOnLoadMode::MainCue ||
                                         !seekModeExisted) &&
            !(m_cueMode == CueMode::Denon ||
                    m_cueMode == CueMode::Numark);
    m_bSetIntroStartAtMainCue = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("SetIntroStartAtMainCue")),
            introStartMoveDefault);
    // This is an ugly hack to ensure AnalyzerSilence gets the correct default
    // value because ConfigValue::getValue does not set the value of the ConfigValue
    // in case no value had been set previously (when mixxx.cfg is empty).
    m_pConfig->setValue(
            ConfigKey(kControlsGroup, QStringLiteral("SetIntroStartAtMainCue")),
            m_bSetIntroStartAtMainCue);
    checkBoxIntroStartMove->setChecked(m_bSetIntroStartAtMainCue);
    connect(checkBoxIntroStartMove,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::slotMoveIntroStartCheckbox);

    // Double-tap Load to clone a deck via keyboard or controller ([ChannelN],LoadSelectedTrack)
    m_bCloneDeckOnLoadDoubleTap = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("CloneDeckOnLoadDoubleTap")), true);
    checkBoxCloneDeckOnLoadDoubleTap->setChecked(m_bCloneDeckOnLoadDoubleTap);
    connect(checkBoxCloneDeckOnLoadDoubleTap,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::slotCloneDeckOnLoadDoubleTapCheckbox);

    m_bRateDownIncreasesSpeed = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RateDir")), kDefaultRateDirectionInverted);
    setRateDirectionForAllDecks(m_bRateDownIncreasesSpeed);
    checkBoxInvertSpeedSlider->setChecked(m_bRateDownIncreasesSpeed);
    connect(checkBoxInvertSpeedSlider,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::slotRateInversionCheckbox);

    ComboBoxRateRange->clear();
    ComboBoxRateRange->addItem(tr("4%"), 4);
    ComboBoxRateRange->addItem(tr("6% (semitone)"), 6);
    ComboBoxRateRange->addItem(tr("8% (Technics SL-1210)"), 8);
    ComboBoxRateRange->addItem(tr("10%"), 10);
    ComboBoxRateRange->addItem(tr("16%"), 16);
    ComboBoxRateRange->addItem(tr("24%"), 24);
    ComboBoxRateRange->addItem(tr("50%"), 50);
    ComboBoxRateRange->addItem(tr("90%"), 90);
    connect(ComboBoxRateRange,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::slotRateRangeComboBox);

    // RateRange is the legacy ConfigKey. RateRangePercent is used now.
    if (m_pConfig->exists(ConfigKey(kControlsGroup, QStringLiteral("RateRange"))) &&
            !m_pConfig->exists(ConfigKey(kControlsGroup, QStringLiteral("RateRangePercent")))) {
        int legacyIndex = m_pConfig
                                  ->getValueString(ConfigKey(kControlsGroup,
                                          QStringLiteral("RateRange")))
                                  .toInt();
        if (legacyIndex == 0) {
            m_iRateRangePercent = 6;
        } else if (legacyIndex == 1) {
            m_iRateRangePercent = 8;
        } else {
            m_iRateRangePercent = (legacyIndex-1) * 10;
        }
    } else {
        m_iRateRangePercent = m_pConfig->getValue(
                ConfigKey(kControlsGroup, QStringLiteral("RateRangePercent")),
                kDefaultRateRangePercent);
    }
    if (!(m_iRateRangePercent > 0 && m_iRateRangePercent <= 90)) {
        m_iRateRangePercent = kDefaultRateRangePercent;
    }
    setRateRangeForAllDecks(m_iRateRangePercent);
    // Write verified value back to config so DlgPrefLibrary can use it to
    // calculate the 'fuzzy' BPM search range
    m_pConfig->set(ConfigKey(kControlsGroup, QStringLiteral("RateRangePercent")),
            ConfigValue{m_iRateRangePercent});

    // Key lock mode
    connect(buttonGroupKeyLockMode,
            QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this,
            &DlgPrefDeck::slotKeyLockModeSelected);

    m_keylockMode = static_cast<KeylockMode>(
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("keylockMode")),
                    static_cast<int>(KeylockMode::LockOriginalKey)));
    for (ControlProxy* pControl : std::as_const(m_keylockModeControls)) {
        pControl->set(static_cast<double>(m_keylockMode));
    }

    // Key unlock mode
    connect(buttonGroupKeyUnlockMode,
            QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this,
            &DlgPrefDeck::slotKeyUnlockModeSelected);

    m_keyunlockMode = static_cast<KeyunlockMode>(
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("keyunlockMode")),
                    static_cast<int>(KeyunlockMode::ResetLockedKey)));
    for (ControlProxy* pControl : std::as_const(m_keyunlockModeControls)) {
        pControl->set(static_cast<int>(m_keyunlockMode));
    }

    // Cue Mode
    // Add "(?)" with a manual link to the label
    labelCueMode->setText(labelCueMode->text() + QChar(' ') +
            coloredLinkString(
                    m_pLinkColor,
                    QStringLiteral("(?)"),
                    MIXXX_MANUAL_CUE_MODES_URL));

    // Sync Mode
    // Add "(?)" with a manual link to the label
    labelSyncMode->setText(labelSyncMode->text() + QChar(' ') +
            coloredLinkString(
                    m_pLinkColor,
                    QStringLiteral("(?)"),
                    MIXXX_MANUAL_SYNC_MODES_URL));

    // Speed / Pitch reset configuration
    // Update "reset speed" and "reset pitch" check boxes
    // TODO: All defaults should only be set in slotResetToDefaults.
    auto configSPAutoReset = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("SpeedAutoReset")),
            BaseTrackPlayer::TrackLoadReset::RESET_PITCH);

    m_speedAutoReset = (configSPAutoReset == BaseTrackPlayer::TrackLoadReset::RESET_SPEED ||
            configSPAutoReset == BaseTrackPlayer::TrackLoadReset::RESET_PITCH_AND_SPEED);
    m_pitchAutoReset = (configSPAutoReset == BaseTrackPlayer::TrackLoadReset::RESET_PITCH ||
            configSPAutoReset == BaseTrackPlayer::TrackLoadReset::RESET_PITCH_AND_SPEED);

    checkBoxResetSpeed->setChecked(m_speedAutoReset);
    checkBoxResetPitch->setChecked(m_pitchAutoReset);

    connect(checkBoxResetSpeed, &QCheckBox::toggled, this, &DlgPrefDeck::slotUpdateSpeedAutoReset);
    connect(checkBoxResetPitch, &QCheckBox::toggled, this, &DlgPrefDeck::slotUpdatePitchAutoReset);

    //
    // Ramping Temporary Rate Change configuration
    //

    // Rate Ramp Sensitivity slider
    m_iRateRampSensitivity =
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("RateRampSensitivity")),
                    kDefaultRateRampSensitivity);
    SliderRateRampSensitivity->setValue(m_iRateRampSensitivity);
    connect(SliderRateRampSensitivity,
            &QSlider::valueChanged,
            this,
            &DlgPrefDeck::slotRateRampSensitivitySlider);

    // Enable/disable permanent rate spinboxes when smooth ramping is selected
    connect(radioButtonRateRampModeLinear,
            &QRadioButton::toggled,
            labelSpeedRampSensitivity,
            &QWidget::setEnabled);
    connect(radioButtonRateRampModeLinear,
            &QRadioButton::toggled,
            SliderRateRampSensitivity,
            &QWidget::setEnabled);
    // Enable/disable temporary rate spinboxes when abrupt ramping is selected
    connect(radioButtonRateRampModeStepping,
            &QRadioButton::toggled,
            labelSpeedTemporary,
            &QWidget::setEnabled);
    connect(radioButtonRateRampModeStepping,
            &QRadioButton::toggled,
            spinBoxTemporaryRateCoarse,
            &QWidget::setEnabled);
    connect(radioButtonRateRampModeStepping,
            &QRadioButton::toggled,
            spinBoxTemporaryRateFine,
            &QWidget::setEnabled);
    // Set Ramp Rate On or Off
    connect(radioButtonRateRampModeLinear,
            &QRadioButton::toggled,
            this,
            &DlgPrefDeck::slotRateRampingModeLinearButton);
    m_bRateRamping = static_cast<RateControl::RampMode>(
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("RateRamp")),
                    static_cast<int>(kDefaultRampingMode)));
    if (m_bRateRamping == RateControl::RampMode::Linear) {
        radioButtonRateRampModeLinear->setChecked(true);
    } else {
        radioButtonRateRampModeStepping->setChecked(true);
    }

    // Rate buttons configuration
    connect(spinBoxTemporaryRateCoarse,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRateTempCoarseSpinbox);
    connect(spinBoxTemporaryRateFine,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRateTempFineSpinbox);
    connect(spinBoxPermanentRateCoarse,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRatePermCoarseSpinbox);
    connect(spinBoxPermanentRateFine,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRatePermFineSpinbox);

    m_dRateTempCoarse = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RateTempLeft")),
            kDefaultTemporaryRateChangeCoarse);
    m_dRateTempFine = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RateTempRight")),
            kDefaultTemporaryRateChangeFine);
    m_dRatePermCoarse = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RatePermLeft")),
            kDefaultPermanentRateChangeCoarse);
    m_dRatePermFine = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RatePermRight")),
            kDefaultPermanentRateChangeFine);

    spinBoxTemporaryRateCoarse->setValue(m_dRateTempCoarse);
    spinBoxTemporaryRateFine->setValue(m_dRateTempFine);
    spinBoxPermanentRateCoarse->setValue(m_dRatePermCoarse);
    spinBoxPermanentRateFine->setValue(m_dRatePermFine);

    RateControl::setTemporaryRateChangeCoarseAmount(m_dRateTempCoarse);
    RateControl::setTemporaryRateChangeFineAmount(m_dRateTempFine);
    RateControl::setPermanentRateChangeCoarseAmount(m_dRatePermCoarse);
    RateControl::setPermanentRateChangeFineAmount(m_dRatePermFine);

    // --- Physical Spin-up Ramp initialization ---
    bool rampUpEnabled = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpEnabled")), false);
    bool rampDownEnabled = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampDownEnabled")), false);
    int rampMode = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpMode")), 0);
    bool rampIncludePitch = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpIncludePitch")), true);

    checkBoxRampUpEnabled->setChecked(rampUpEnabled);
    checkBoxRampDownEnabled->setChecked(rampDownEnabled);
    const int rampDownMode = m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampDownMode")), 0);
    if (rampDownMode == 1) {
        radioButtonRampDownPowerCut->setChecked(true);
    } else {
        radioButtonRampDownBrake->setChecked(true);
    }

    // Spin-down type is only relevant if spin-down is enabled
    const auto updateRampDownModeEnabled = [this](bool enabled) {
        labelRampDownMode->setEnabled(enabled);
        radioButtonRampDownBrake->setEnabled(enabled);
        radioButtonRampDownPowerCut->setEnabled(enabled);
        labelRampDownTime->setEnabled(enabled);
        sliderRampDownTime->setEnabled(enabled);
        labelRampDownTimeValue->setEnabled(enabled);
    };
    connect(checkBoxRampDownEnabled, &QCheckBox::toggled, this, updateRampDownModeEnabled);
    updateRampDownModeEnabled(checkBoxRampDownEnabled->isChecked());
    comboBoxRampUpMode->setCurrentIndex(rampMode);
    checkBoxRampUpIncludePitch->setChecked(rampIncludePitch);

    sliderRampUpTime->setValue(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpTime")),
            kDefaultRampTimePercent));
    sliderRampDownTime->setValue(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampDownTime")),
            kDefaultRampTimePercent));

    // The time labels depend on the sliders, the platter type and the
    // spin-down type
    connect(sliderRampUpTime, &QSlider::valueChanged, this, &DlgPrefDeck::updateRampTimeLabels);
    connect(sliderRampDownTime, &QSlider::valueChanged, this, &DlgPrefDeck::updateRampTimeLabels);
    connect(comboBoxRampUpMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::updateRampTimeLabels);
    connect(radioButtonRampDownPowerCut,
            &QRadioButton::toggled,
            this,
            &DlgPrefDeck::updateRampTimeLabels);

    // --- Backspin on the reverse button ---
    for (const auto& rotation : kBackspinRotations) {
        comboBoxBackspinRotation->addItem(tr("%1 rotation back").arg(rotation.text()));
    }
    for (const auto& time : kBackspinTimes) {
        comboBoxBackspinTime->addItem(tr("in %1 s").arg(time.text()));
    }
    loadBackspinSettings();
    connect(comboBoxBackspinRotation,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::updateBackspinChoices);
    connect(comboBoxBackspinTime,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::updateRampTimeLabels);
    // The backspin power is only relevant in backspin mode
    const auto updateBackspinEnabled = [this](bool enabled) {
        labelBackspinPower->setEnabled(enabled);
        comboBoxBackspinRotation->setEnabled(enabled);
        comboBoxBackspinTime->setEnabled(enabled);
        labelBackspinValue->setEnabled(enabled);
        checkBoxBackspinPlayOn->setEnabled(enabled);
        labelBackspinSlipmat->setEnabled(enabled);
        comboBoxBackspinSlipmat->setEnabled(enabled);
        checkBoxBackspinStart->setEnabled(enabled);
        checkBoxBackspinToHotcue->setEnabled(enabled);
        labelBackspinToHotcueValue->setEnabled(
                enabled && checkBoxBackspinToHotcue->isChecked());
    };
    connect(radioButtonReverseBackspin, &QRadioButton::toggled, this, updateBackspinEnabled);
    connect(checkBoxBackspinPlayOn, &QCheckBox::toggled, this, &DlgPrefDeck::updateRampTimeLabels);
    connect(comboBoxBackspinSlipmat,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::updateRampTimeLabels);
    connect(checkBoxBackspinStart,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::updateRampTimeLabels);
    connect(checkBoxBackspinToHotcue, &QCheckBox::toggled, this, [this](bool checked) {
        labelBackspinToHotcueValue->setEnabled(
                checked && radioButtonReverseBackspin->isChecked());
    });
    connect(radioButtonReverseBackspin, &QRadioButton::toggled, this, updateBackspinEnabled);
    updateBackspinEnabled(radioButtonReverseBackspin->isChecked());
    updateBackspinChoices();
    RateControl::setBackspinOnReverse(radioButtonReverseBackspin->isChecked());
    RateControl::setBackspinRevPerSec(backspinRevPerSec());
    RateControl::setBackspinToHotcue(checkBoxBackspinToHotcue->isChecked());
    RateControl::setBackspinPlayOn(checkBoxBackspinPlayOn->isChecked());
    RateControl::setBackspinSlipmat(comboBoxBackspinSlipmat->currentIndex());
    RateControl::setBackspinStart(checkBoxBackspinStart->isChecked());

    // Spin-up time is only relevant if spin-up is enabled
    const auto updateRampUpTimeEnabled = [this](bool enabled) {
        labelRampUpTime->setEnabled(enabled);
        sliderRampUpTime->setEnabled(enabled);
        labelRampUpTimeValue->setEnabled(enabled);
    };
    connect(checkBoxRampUpEnabled, &QCheckBox::toggled, this, updateRampUpTimeEnabled);
    // The backspin times depend on whether the platter spins up afterwards
    connect(checkBoxRampUpEnabled, &QCheckBox::toggled, this, &DlgPrefDeck::updateRampTimeLabels);
    updateRampUpTimeEnabled(checkBoxRampUpEnabled->isChecked());
    updateRampTimeLabels();

    RateControl::setRampUpEnabled(checkBoxRampUpEnabled->isChecked());
    RateControl::setRampDownEnabled(checkBoxRampDownEnabled->isChecked());
    RateControl::setRampDownMode(radioButtonRampDownPowerCut->isChecked() ? 1 : 0);
    RateControl::setRampRpmMode(comboBoxRampUpMode->currentIndex());
    RateControl::setRampIncludePitch(checkBoxRampUpIncludePitch->isChecked());
    RateControl::setRampUpTimeFactor(sliderRampUpTime->value() / 100.0);
    RateControl::setRampDownTimeFactor(sliderRampDownTime->value() / 100.0);

    slotUpdate();
}

DlgPrefDeck::~DlgPrefDeck() {
    qDeleteAll(m_rateControls);
    qDeleteAll(m_rateDirectionControls);
    qDeleteAll(m_cueControls);
    qDeleteAll(m_rateRangeControls);
    qDeleteAll(m_keylockModeControls);
    qDeleteAll(m_keyunlockModeControls);
}

void DlgPrefDeck::slotUpdate() {
    checkBoxIntroStartMove->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("SetIntroStartAtMainCue")), false));

    slotSetTrackTimeDisplay(m_pControlTrackTimeDisplay->get());

    checkBoxCloneDeckOnLoadDoubleTap->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("CloneDeckOnLoadDoubleTap")), true));

    double rateRange = m_rateRangeControls[0]->get();
    int index = ComboBoxRateRange->findData(static_cast<int>(rateRange * 100.0));
    if (index == -1) {
        ComboBoxRateRange->addItem(QString::number(rateRange * 100.).append("%"),
                rateRange * 100.);
    }
    ComboBoxRateRange->setCurrentIndex(index);

    double rateDirection = m_rateDirectionControls[0]->get();
    checkBoxInvertSpeedSlider->setChecked(rateDirection == kRateDirectionInverted);

    double cueMode = m_cueControls[0]->get();
    index = ComboBoxCueMode->findData(static_cast<int>(cueMode));
    ComboBoxCueMode->setCurrentIndex(index);

    const EngineSync::SyncLockAlgorithm syncLockAlgorithm =
            static_cast<EngineSync::SyncLockAlgorithm>(m_pConfig->getValue<int>(
                    ConfigKey(kBpmConfigGroup, kSyncLockAlgorithmConfigKey),
                    EngineSync::SyncLockAlgorithm::PREFER_SOFT_LEADER));
    if (syncLockAlgorithm == EngineSync::SyncLockAlgorithm::PREFER_SOFT_LEADER) {
        radioButtonSoftLeader->setChecked(true);
    } else {
        radioButtonLockBpm->setChecked(true);
    }

    KeylockMode keylockMode =
            static_cast<KeylockMode>(static_cast<int>(m_keylockModeControls[0]->get()));
    if (keylockMode == KeylockMode::LockCurrentKey) {
        radioButtonCurrentKey->setChecked(true);
    } else {
        radioButtonOriginalKey->setChecked(true);
    }

    KeyunlockMode keyunlockMode =
            static_cast<KeyunlockMode>(static_cast<int>(m_keyunlockModeControls[0]->get()));
    if (keyunlockMode == KeyunlockMode::KeepLockedKey) {
        radioButtonKeepUnlockedKey->setChecked(true);
    } else {
        radioButtonResetUnlockedKey->setChecked(true);
    }

    auto reset = m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("SpeedAutoReset")),
            BaseTrackPlayer::TrackLoadReset::RESET_PITCH);
    if (reset == BaseTrackPlayer::TrackLoadReset::RESET_PITCH) {
        checkBoxResetPitch->setChecked(true);
        checkBoxResetSpeed->setChecked(false);
    } else if (reset == BaseTrackPlayer::TrackLoadReset::RESET_SPEED) {
        checkBoxResetPitch->setChecked(false);
        checkBoxResetSpeed->setChecked(true);
    } else if (reset == BaseTrackPlayer::TrackLoadReset::RESET_PITCH_AND_SPEED) {
        checkBoxResetPitch->setChecked(true);
        checkBoxResetSpeed->setChecked(true);
    } else if (reset == BaseTrackPlayer::TrackLoadReset::RESET_NONE) {
        checkBoxResetPitch->setChecked(false);
        checkBoxResetSpeed->setChecked(false);
    }

    if (m_bRateRamping == RateControl::RampMode::Linear) {
        radioButtonRateRampModeLinear->setChecked(true);
    } else {
        radioButtonRateRampModeStepping->setChecked(true);
    }

    SliderRateRampSensitivity->setValue(
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("RateRampSensitivity")),
                    kDefaultRateRampSensitivity));

    spinBoxTemporaryRateCoarse->setValue(RateControl::getTemporaryRateChangeCoarseAmount());
    spinBoxTemporaryRateFine->setValue(RateControl::getTemporaryRateChangeFineAmount());
    spinBoxPermanentRateCoarse->setValue(RateControl::getPermanentRateChangeCoarseAmount());
    spinBoxPermanentRateFine->setValue(RateControl::getPermanentRateChangeFineAmount());

    checkBoxRampUpEnabled->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpEnabled")), false));
    checkBoxRampDownEnabled->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampDownEnabled")), false));
    comboBoxRampUpMode->setCurrentIndex(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpMode")), 0));
    checkBoxRampUpIncludePitch->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpIncludePitch")), true));
    if (m_pConfig->getValue(
                ConfigKey(kControlsGroup, QStringLiteral("RampDownMode")), 0) == 1) {
        radioButtonRampDownPowerCut->setChecked(true);
    } else {
        radioButtonRampDownBrake->setChecked(true);
    }

    sliderRampUpTime->setValue(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampUpTime")),
            kDefaultRampTimePercent));
    sliderRampDownTime->setValue(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("RampDownTime")),
            kDefaultRampTimePercent));

    loadBackspinSettings();
}

void DlgPrefDeck::slotResetToDefaults() {
    // Track time display mode
    slotSetTrackTimeDisplay(kDefaultPositionDisplayType);

    // Up increases speed.
    checkBoxInvertSpeedSlider->setChecked(kDefaultRateDirectionInverted);

    // 8% Rate Range
    ComboBoxRateRange->setCurrentIndex(ComboBoxRateRange->findData(kDefaultRateRangePercent));

    // Clone decks by double-tapping Load button.
    checkBoxCloneDeckOnLoadDoubleTap->setChecked(kDefaultCloneDeckOnLoad);

    // Mixxx cue mode
    ComboBoxCueMode->setCurrentIndex(0);

    // What to do if someone loads into a playing deck
    comboBoxLoadWhenDeckPlaying->setCurrentIndex(static_cast<int>(kDefaultLoadWhenDeckPlaying));

    // Load at intro start
    comboBoxLoadPoint->setCurrentIndex(
            comboBoxLoadPoint->findData(static_cast<int>(SeekOnLoadMode::IntroStart)));

    // Rate-ramping default off.
    radioButtonRateRampModeStepping->setChecked(true);

    SliderRateRampSensitivity->setValue(kDefaultRateRampSensitivity);

    // Permanent and temporary pitch adjust fine/coarse.
    spinBoxTemporaryRateCoarse->setValue(4.0);
    spinBoxTemporaryRateFine->setValue(2.0);
    spinBoxPermanentRateCoarse->setValue(0.50);
    spinBoxPermanentRateFine->setValue(0.05);

    checkBoxResetSpeed->setChecked(false);
    checkBoxResetPitch->setChecked(true);

    radioButtonSoftLeader->setChecked(true);

    radioButtonOriginalKey->setChecked(true);
    radioButtonResetUnlockedKey->setChecked(true);

    checkBoxRampUpEnabled->setChecked(false);
    checkBoxRampDownEnabled->setChecked(false);
    radioButtonRampDownBrake->setChecked(true);
    comboBoxRampUpMode->setCurrentIndex(0); // 33 RPM
    checkBoxRampUpIncludePitch->setChecked(true);
    sliderRampUpTime->setValue(kDefaultRampTimePercent);
    sliderRampDownTime->setValue(kDefaultRampTimePercent);

    radioButtonReversePlayback->setChecked(true);
    comboBoxBackspinRotation->setCurrentIndex(kDefaultBackspinRotation);
    comboBoxBackspinTime->setCurrentIndex(kDefaultBackspinTime);
    checkBoxBackspinToHotcue->setChecked(false);
    checkBoxBackspinPlayOn->setChecked(false);
    comboBoxBackspinSlipmat->setCurrentIndex(0);
    checkBoxBackspinStart->setChecked(false);
}

void DlgPrefDeck::slotMoveIntroStartCheckbox(bool checked) {
    m_bSetIntroStartAtMainCue = checked;
}

void DlgPrefDeck::slotRateRangeComboBox(int index) {
    m_iRateRangePercent = ComboBoxRateRange->itemData(index).toInt();
}

void DlgPrefDeck::setRateRangeForAllDecks(int rangePercent) {
    for (ControlProxy* pControl : std::as_const(m_rateRangeControls)) {
        pControl->set(rangePercent / 100.0);
    }
}

void DlgPrefDeck::slotRateInversionCheckbox(bool inverted) {
    m_bRateDownIncreasesSpeed = inverted;
}

void DlgPrefDeck::setRateDirectionForAllDecks(bool inverted) {
    double oldRateDirectionMultiplier = m_rateDirectionControls[0]->get();
    double rateDirectionMultiplier = 1.0;
    if (inverted) {
        rateDirectionMultiplier = kRateDirectionInverted;
    }
    for (ControlProxy* pControl : std::as_const(m_rateDirectionControls)) {
        pControl->set(rateDirectionMultiplier);
    }

    // If the rate slider direction setting has changed,
    // multiply the rate by -1 so the current sound does not change.
    if (rateDirectionMultiplier != oldRateDirectionMultiplier) {
        for (ControlProxy* pControl : std::as_const(m_rateControls)) {
            pControl->set(-1 * pControl->get());
        }
    }
}

void DlgPrefDeck::slotKeyLockModeSelected(QAbstractButton* pressedButton) {
    if (pressedButton == radioButtonCurrentKey) {
        m_keylockMode = KeylockMode::LockCurrentKey;
    } else {
        m_keylockMode = KeylockMode::LockOriginalKey;
    }
}

void DlgPrefDeck::slotKeyUnlockModeSelected(QAbstractButton* pressedButton) {
    if (pressedButton == radioButtonResetUnlockedKey) {
        m_keyunlockMode = KeyunlockMode::ResetLockedKey;
    } else {
        m_keyunlockMode = KeyunlockMode::KeepLockedKey;
    }
}

void DlgPrefDeck::slotCueModeCombobox(int index) {
    m_cueMode = static_cast<CueMode>(ComboBoxCueMode->itemData(index).toInt());
}

void DlgPrefDeck::slotCloneDeckOnLoadDoubleTapCheckbox(bool checked) {
    m_bCloneDeckOnLoadDoubleTap = checked;
}

void DlgPrefDeck::slotSetTrackTimeDisplay(QAbstractButton* b) {
    if (b == radioButtonRemaining) {
        m_timeDisplayMode = TrackTime::DisplayMode::REMAINING;
    } else if (b == radioButtonElapsedAndRemaining) {
        m_timeDisplayMode = TrackTime::DisplayMode::ELAPSED_AND_REMAINING;
    } else {
        m_timeDisplayMode = TrackTime::DisplayMode::ELAPSED;
    }
}

void DlgPrefDeck::slotSetTrackTimeDisplay(double v) {
    m_timeDisplayMode = static_cast<TrackTime::DisplayMode>(static_cast<int>(v));
    m_pConfig->set(ConfigKey(kControlsGroup, QStringLiteral("PositionDisplay")), ConfigValue(v));
    if (m_timeDisplayMode == TrackTime::DisplayMode::REMAINING) {
        radioButtonRemaining->setChecked(true);
    } else if (m_timeDisplayMode == TrackTime::DisplayMode::ELAPSED_AND_REMAINING) {
        radioButtonElapsedAndRemaining->setChecked(true);
    } else { // Elapsed
        radioButtonElapsed->setChecked(true);
    }
}

void DlgPrefDeck::slotRateTempCoarseSpinbox(double value) {
    m_dRateTempCoarse = value;
}

void DlgPrefDeck::slotRateTempFineSpinbox(double value) {
    m_dRateTempFine = value;
}

void DlgPrefDeck::slotRatePermCoarseSpinbox(double value) {
    m_dRatePermCoarse = value;
}

void DlgPrefDeck::slotRatePermFineSpinbox(double value) {
    m_dRatePermFine = value;
}

void DlgPrefDeck::slotRateRampSensitivitySlider(int value) {
    m_iRateRampSensitivity = value;
}

void DlgPrefDeck::slotRateRampingModeLinearButton(bool checked) {
    if (checked) {
        m_bRateRamping = RateControl::RampMode::Linear;
    } else {
        m_bRateRamping = RateControl::RampMode::Stepping;
    }
}

void DlgPrefDeck::slotTimeFormatChanged(double v) {
    int i = static_cast<int>(v);
    m_pConfig->set(ConfigKey(kControlsGroup, QStringLiteral("TimeFormat")), ConfigValue(v));
    comboBoxTimeFormat->setCurrentIndex(
                comboBoxTimeFormat->findData(i));
}

void DlgPrefDeck::slotSetTrackLoadMode(int comboboxIndex) {
    m_seekOnLoadMode = static_cast<SeekOnLoadMode>(
            comboBoxLoadPoint->itemData(comboboxIndex).toInt());
}

void DlgPrefDeck::slotLoadWhenDeckPlayingIndexChanged(int comboboxIndex) {
    m_loadWhenDeckPlaying = static_cast<LoadWhenDeckPlaying>(
            comboBoxLoadWhenDeckPlaying->itemData(comboboxIndex).toInt());
}

void DlgPrefDeck::slotApply() {
    m_pConfig->set(ConfigKey(kControlsGroup, QStringLiteral("SetIntroStartAtMainCue")),
            ConfigValue(m_bSetIntroStartAtMainCue));

    double timeDisplay = static_cast<double>(m_timeDisplayMode);
    m_pConfig->set(ConfigKey(kControlsGroup, QStringLiteral("PositionDisplay")),
            ConfigValue(timeDisplay));
    m_pControlTrackTimeDisplay->set(timeDisplay);

    // time format
    double timeFormat = comboBoxTimeFormat->itemData(comboBoxTimeFormat->currentIndex()).toDouble();
    m_pControlTrackTimeFormat->set(timeFormat);
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("TimeFormat")), timeFormat);

    // Set cue mode for every deck
    for (ControlProxy* pControl : std::as_const(m_cueControls)) {
        pControl->set(static_cast<int>(m_cueMode));
    }
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("CueDefault")), m_cueMode);

    m_pConfig->setValue(kConfigKeyLoadWhenDeckPlaying, m_loadWhenDeckPlaying);

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("CueRecall")), m_seekOnLoadMode);
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("CloneDeckOnLoadDoubleTap")),
            m_bCloneDeckOnLoadDoubleTap);

    // Set rate range
    // Set the config value before setting the CO values in setRateRangeForAllDecks()
    // because a proxy in DlgPrefLibrary listens to [Channe1],rate_range changes
    // in order to update the fuzzy BPM range with the new value of "RateRangePercent".
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RateRangePercent")),
            m_iRateRangePercent);
    setRateRangeForAllDecks(m_iRateRangePercent);

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RateDir")),
            m_bRateDownIncreasesSpeed);
    setRateDirectionForAllDecks(m_bRateDownIncreasesSpeed);

    BaseTrackPlayer::TrackLoadReset configSPAutoReset = BaseTrackPlayer::TrackLoadReset::RESET_NONE;

    if (m_speedAutoReset && m_pitchAutoReset) {
        configSPAutoReset = BaseTrackPlayer::TrackLoadReset::RESET_PITCH_AND_SPEED;
    } else if (m_speedAutoReset) {
        configSPAutoReset = BaseTrackPlayer::TrackLoadReset::RESET_SPEED;
    } else if (m_pitchAutoReset) {
        configSPAutoReset = BaseTrackPlayer::TrackLoadReset::RESET_PITCH;
    }

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("SpeedAutoReset")),
            configSPAutoReset);

    if (radioButtonSoftLeader->isChecked()) {
        m_pConfig->setValue(ConfigKey(kBpmConfigGroup, kSyncLockAlgorithmConfigKey),
                static_cast<int>(EngineSync::SyncLockAlgorithm::PREFER_SOFT_LEADER));
    } else {
        m_pConfig->setValue(ConfigKey(kBpmConfigGroup, kSyncLockAlgorithmConfigKey),
                static_cast<int>(EngineSync::SyncLockAlgorithm::PREFER_LOCK_BPM));
    }

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("keylockMode")),
            m_keylockMode);
    // Set key lock behavior for every group
    for (ControlProxy* pControl : std::as_const(m_keylockModeControls)) {
        pControl->set(static_cast<double>(m_keylockMode));
    }

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("keyunlockMode")),
            m_keyunlockMode);
    // Set key un-lock behavior for every group
    for (ControlProxy* pControl : std::as_const(m_keyunlockModeControls)) {
        pControl->set(static_cast<double>(m_keyunlockMode));
    }

    RateControl::setRateRampMode(m_bRateRamping);
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RateRamp")), m_bRateRamping);

    RateControl::setRateRampSensitivity(m_iRateRampSensitivity);
    m_pConfig->setValue(
            ConfigKey(kControlsGroup, QStringLiteral("RateRampSensitivity")),
            m_iRateRampSensitivity);

    RateControl::setTemporaryRateChangeCoarseAmount(m_dRateTempCoarse);
    RateControl::setTemporaryRateChangeFineAmount(m_dRateTempFine);
    RateControl::setPermanentRateChangeCoarseAmount(m_dRatePermCoarse);
    RateControl::setPermanentRateChangeFineAmount(m_dRatePermFine);

    m_pConfig->setValue(
            ConfigKey(kControlsGroup, QStringLiteral("RateTempLeft")),
            m_dRateTempCoarse);
    m_pConfig->setValue(
            ConfigKey(kControlsGroup, QStringLiteral("RateTempRight")),
            m_dRateTempFine);
    m_pConfig->setValue(
            ConfigKey(kControlsGroup, QStringLiteral("RatePermLeft")),
            m_dRatePermCoarse);
    m_pConfig->setValue(
            ConfigKey(kControlsGroup, QStringLiteral("RatePermRight")),
            m_dRatePermFine);

    const int rampDownMode = radioButtonRampDownPowerCut->isChecked() ? 1 : 0;
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RampDownMode")),
            rampDownMode);
    RateControl::setRampDownMode(rampDownMode);

    // Save Physical Spin-up Spin-Down Ramp settings
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RampUpEnabled")),
            checkBoxRampUpEnabled->isChecked());

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RampDownEnabled")),
            checkBoxRampDownEnabled->isChecked());

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RampUpMode")),
            comboBoxRampUpMode->currentIndex());

    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RampUpIncludePitch")),
            checkBoxRampUpIncludePitch->isChecked());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RampUpTime")),
            sliderRampUpTime->value());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("RampDownTime")),
            sliderRampDownTime->value());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("ReverseButtonMode")),
            radioButtonReverseBackspin->isChecked() ? 1 : 0);
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinRotation")),
            comboBoxBackspinRotation->currentIndex());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinTime")),
            comboBoxBackspinTime->currentIndex());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinToHotcue")),
            checkBoxBackspinToHotcue->isChecked());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinPlayOn")),
            checkBoxBackspinPlayOn->isChecked());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinSlipmat")),
            comboBoxBackspinSlipmat->currentIndex());
    m_pConfig->setValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinStart")),
            checkBoxBackspinStart->isChecked());

    RateControl::setRampUpEnabled(checkBoxRampUpEnabled->isChecked());
    RateControl::setRampDownEnabled(checkBoxRampDownEnabled->isChecked());
    RateControl::setRampRpmMode(comboBoxRampUpMode->currentIndex());
    RateControl::setRampIncludePitch(checkBoxRampUpIncludePitch->isChecked());
    RateControl::setRampUpTimeFactor(sliderRampUpTime->value() / 100.0);
    RateControl::setRampDownTimeFactor(sliderRampDownTime->value() / 100.0);
    RateControl::setBackspinOnReverse(radioButtonReverseBackspin->isChecked());
    RateControl::setBackspinRevPerSec(backspinRevPerSec());
    RateControl::setBackspinToHotcue(checkBoxBackspinToHotcue->isChecked());
    RateControl::setBackspinPlayOn(checkBoxBackspinPlayOn->isChecked());
    RateControl::setBackspinSlipmat(comboBoxBackspinSlipmat->currentIndex());
    RateControl::setBackspinStart(checkBoxBackspinStart->isChecked());
}

void DlgPrefDeck::slotNumDecksChanged(double new_count, bool initializing) {
    int numdecks = static_cast<int>(new_count);
    if (numdecks <= m_iNumConfiguredDecks) {
        // TODO(owilliams): If we implement deck deletion, shrink the size of configured decks.
        return;
    }

    for (int i = m_iNumConfiguredDecks; i < numdecks; ++i) {
        QString group = PlayerManager::groupForDeck(i);
        m_rateControls.push_back(new ControlProxy(
                group, "rate"));
        m_rateRangeControls.push_back(new ControlProxy(
                group, "rateRange"));
        m_rateDirectionControls.push_back(new ControlProxy(
                group, "rate_dir"));
        m_cueControls.push_back(new ControlProxy(
                group, "cue_mode"));
        m_keylockModeControls.push_back(new ControlProxy(
                group, "keylockMode"));
        m_keylockModeControls.last()->set(static_cast<double>(m_keylockMode));
        m_keyunlockModeControls.push_back(new ControlProxy(
                group, "keyunlockMode"));
        m_keyunlockModeControls.last()->set(static_cast<double>(m_keyunlockMode));
    }

    m_iNumConfiguredDecks = numdecks;

    // The rate range hasn't been read from the config file when this is first called.
    if (!initializing) {
        setRateDirectionForAllDecks(m_rateDirectionControls[0]->get() == kRateDirectionInverted);
        setRateRangeForAllDecks(static_cast<int>(m_rateRangeControls[0]->get() * 100.0));
    }
}

void DlgPrefDeck::slotNumSamplersChanged(double new_count, bool initializing) {
    int numsamplers = static_cast<int>(new_count);
    if (numsamplers <= m_iNumConfiguredSamplers) {
        return;
    }

    for (int i = m_iNumConfiguredSamplers; i < numsamplers; ++i) {
        QString group = PlayerManager::groupForSampler(i);
        m_rateControls.push_back(new ControlProxy(
                group, "rate"));
        m_rateRangeControls.push_back(new ControlProxy(
                group, "rateRange"));
        m_rateDirectionControls.push_back(new ControlProxy(
                group, "rate_dir"));
        m_cueControls.push_back(new ControlProxy(
                group, "cue_mode"));
        m_keylockModeControls.push_back(new ControlProxy(
                group, "keylockMode"));
        m_keylockModeControls.last()->set(static_cast<double>(m_keylockMode));
        m_keyunlockModeControls.push_back(new ControlProxy(
                group, "keyunlockMode"));
        m_keyunlockModeControls.last()->set(static_cast<double>(m_keyunlockMode));
    }

    m_iNumConfiguredSamplers = numsamplers;

    // The rate range hasn't been read from the config file when this is first called.
    if (!initializing) {
        setRateDirectionForAllDecks(m_rateDirectionControls[0]->get() == kRateDirectionInverted);
        setRateRangeForAllDecks(static_cast<int>(m_rateRangeControls[0]->get() * 100.0));
    }
}

void DlgPrefDeck::slotUpdateSpeedAutoReset(bool b) {
    m_speedAutoReset = b;
}

void DlgPrefDeck::slotUpdatePitchAutoReset(bool b) {
    m_pitchAutoReset = b;
}

int DlgPrefDeck::cueDefaultIndexByData(int userData) const {
    for (int i = 0; i < ComboBoxCueMode->count(); ++i) {
        if (ComboBoxCueMode->itemData(i).toInt() == userData) {
            return i;
        }
    }
    qWarning() << "No default cue behavior found for value" << userData
               << "returning default";
    return 0;
}

void DlgPrefDeck::updateRampTimeLabels() {
    const int rpmMode = comboBoxRampUpMode->currentIndex();
    const int rampDownMode = radioButtonRampDownPowerCut->isChecked() ? 1 : 0;
    const auto timeText = [](int percent, double seconds) {
        QString text = tr("%1 % (%2 s)").arg(percent).arg(seconds, 0, 'f', 1);
        if (percent == kDefaultRampTimePercent) {
            text += QChar(' ') + tr("Technics SL-1210");
        }
        return text;
    };
    const int upPercent = sliderRampUpTime->value();
    labelRampUpTimeValue->setText(timeText(upPercent,
            RateControl::spinUpTime(rpmMode, upPercent / 100.0)));
    const int downPercent = sliderRampDownTime->value();
    labelRampDownTimeValue->setText(timeText(downPercent,
            RateControl::spinDownTime(rpmMode, rampDownMode, downPercent / 100.0)));

    // Backspin of a playing deck and of a stopped deck. Playing on, the
    // motor pulls the platter forward again: with the spin-up ramp, or at
    // full speed from the standstill. Staying paused, the platter brakes
    // or coasts to a standstill.
    const bool spinUp = checkBoxRampUpEnabled->isChecked();
    const bool playOn = checkBoxBackspinPlayOn->isChecked();
    const int slipmat = comboBoxBackspinSlipmat->currentIndex();
    const double revPerSec = backspinRevPerSec();
    const double nominalRevPerSec = ((rpmMode == 0) ? 100.0 / 3.0 : 45.0) / 60.0;
    const auto playing = RateControl::backspinTimes(rpmMode,
            playOn,
            rampDownMode,
            revPerSec,
            upPercent / 100.0,
            downPercent / 100.0,
            spinUp,
            1.0,
            slipmat);
    const auto stopped = RateControl::backspinTimes(rpmMode,
            false,
            rampDownMode,
            revPerSec,
            upPercent / 100.0,
            downPercent / 100.0,
            spinUp,
            std::nullopt,
            slipmat);
    const QString playingText = playOn
            ? tr("Playing: rewinds %1 s, back at speed after %2 s.")
                      .arg(playing.rewind, 0, 'f', 1)
                      .arg(playing.recoverTime, 0, 'f', 1)
            : tr("Playing: rewinds %1 s and stops after %2 s.")
                      .arg(playing.rewind, 0, 'f', 1)
                      .arg(playing.backwardTime, 0, 'f', 1);
    // Spinback start: a stopped deck starts with a backspin into the
    // current position instead
    const double startDistance = RateControl::backspinStartDistance(
            rpmMode, upPercent / 100.0, revPerSec, spinUp, slipmat);
    QString stoppedText = tr("Stopped: rewinds %1 s in %2 s.")
                                  .arg(stopped.rewind, 0, 'f', 1)
                                  .arg(stopped.backwardTime, 0, 'f', 1);
    if (checkBoxBackspinStart->isChecked()) {
        // Too weak: the spin-up carries the deck further than the backspin
        // rewinds, a normal backspin is done
        stoppedText = startDistance > 0.05
                ? tr("Stopped: spinback start from %1 s after the current position.")
                          .arg(startDistance, 0, 'f', 1)
                : tr("Stopped: too weak for a spinback start, rewinds %1 s in %2 s.")
                          .arg(stopped.rewind, 0, 'f', 1)
                          .arg(stopped.backwardTime, 0, 'f', 1);
    }
    labelBackspinValue->setText(
            tr("%1 rev/s (%2x). %3 %4")
                    .arg(revPerSec, 0, 'f', 2)
                    .arg(-revPerSec / nominalRevPerSec, 0, 'f', 1)
                    .arg(playingText, stoppedText));

    // Backspin to hotcue: how far back the hotcue can be with this power
    const double reach = RateControl::backspinHotcueReach(rpmMode,
            upPercent / 100.0,
            revPerSec,
            spinUp,
            playOn,
            rampDownMode,
            downPercent / 100.0,
            slipmat);
    if (reach > 0.05) {
        labelBackspinToHotcueValue->setText(
                (playOn ? tr("Lands on a hotcue up to %1 s behind the play position, "
                             "e.g. hotcue at 0:10 from up to 0:%2.")
                        : tr("Stops on a hotcue up to %1 s behind the play position, "
                             "e.g. hotcue at 0:10 from up to 0:%2."))
                        .arg(reach, 0, 'f', 1)
                        .arg(10.0 + reach, 4, 'f', 1, QChar('0')));
    } else {
        labelBackspinToHotcueValue->setText(
                tr("This backspin power is too weak: the spin-up ends after "
                   "the play position. Choose a stronger backspin."));
    }
}

void DlgPrefDeck::loadBackspinSettings() {
    if (m_pConfig->getValue(
                ConfigKey(kControlsGroup, QStringLiteral("ReverseButtonMode")), 0) == 1) {
        radioButtonReverseBackspin->setChecked(true);
    } else {
        radioButtonReversePlayback->setChecked(true);
    }
    comboBoxBackspinRotation->setCurrentIndex(std::clamp(
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinRotation")),
                    kDefaultBackspinRotation),
            0,
            comboBoxBackspinRotation->count() - 1));
    comboBoxBackspinTime->setCurrentIndex(std::clamp(
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinTime")),
                    kDefaultBackspinTime),
            0,
            comboBoxBackspinTime->count() - 1));
    checkBoxBackspinToHotcue->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("BackspinToHotcue")), false));
    checkBoxBackspinPlayOn->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("BackspinPlayOn")), false));
    comboBoxBackspinSlipmat->setCurrentIndex(std::clamp(
            m_pConfig->getValue(ConfigKey(kControlsGroup, QStringLiteral("BackspinSlipmat")), 0),
            0,
            comboBoxBackspinSlipmat->count() - 1));
    checkBoxBackspinStart->setChecked(m_pConfig->getValue(
            ConfigKey(kControlsGroup, QStringLiteral("BackspinStart")), false));
}

double DlgPrefDeck::backspinRevPerSec() const {
    const int rotation = std::clamp(comboBoxBackspinRotation->currentIndex(),
            0,
            static_cast<int>(std::size(kBackspinRotations)) - 1);
    const int time = std::clamp(comboBoxBackspinTime->currentIndex(),
            0,
            static_cast<int>(std::size(kBackspinTimes)) - 1);
    return kBackspinRotations[rotation].value() / kBackspinTimes[time].value();
}

void DlgPrefDeck::updateBackspinChoices() {
    // Disable the times that would make the flick faster than the needle can
    // follow, and fall back to the fastest allowed one
    auto* pModel = qobject_cast<QStandardItemModel*>(comboBoxBackspinTime->model());
    const int rotation = std::clamp(comboBoxBackspinRotation->currentIndex(),
            0,
            static_cast<int>(std::size(kBackspinRotations)) - 1);
    int fastestAllowed = 0;
    for (int i = 0; i < static_cast<int>(std::size(kBackspinTimes)); ++i) {
        const bool allowed = kBackspinRotations[rotation].value() / kBackspinTimes[i].value() <=
                RateControl::maxBackspinRevPerSec() + 1e-9;
        if (pModel) {
            pModel->item(i)->setEnabled(allowed);
        }
        if (allowed) {
            fastestAllowed = i;
        }
    }
    if (comboBoxBackspinTime->currentIndex() > fastestAllowed) {
        comboBoxBackspinTime->setCurrentIndex(fastestAllowed);
    }
    updateRampTimeLabels();
}
