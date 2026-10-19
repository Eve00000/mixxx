#include "engine/controls/ratecontrol.h"

#include <QtDebug>
#include <algorithm>
#include <cmath>

#include "control/controlobject.h"
#include "control/controlpotmeter.h"
#include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "control/controlttrotary.h"
#include "engine/controls/bpmcontrol.h"
#include "engine/controls/enginecontrol.h"
#include "engine/positionscratchcontroller.h"
#include "mixer/playermanager.h"
#include "moc_ratecontrol.cpp"
#include "util/defs.h"
#include "util/rotary.h"
#include "vinylcontrol/defs_vinylcontrol.h"

namespace {
constexpr int kRateSensitivityMin = 100;
constexpr int kRateSensitivityMax = 2500;
const QString kControlsGroup = QStringLiteral("[Controls]");
// Approximate Technics SL-1210 brake time from 33 RPM to standstill.
// At 45 RPM the platter spins 1.35x faster with the same brake torque,
// so it takes 1.35x longer to stop.
constexpr double kSpindownTime33 = 0.8; // seconds
constexpr double kSpindownTime45 = kSpindownTime33 * 45.0 / (100.0 / 3.0);
// Power cut: no brake, the coasting platter is only slowed by bearing friction
// (constant) and drag (proportional to speed).
// kCoastTime33 is the time from 33 RPM to standstill.
// kCoastDragRatio = drag / friction: higher drops fast first and then
// crawls, lower is closer to a straight line.
// At 45 RPM the platter has more momentum, so the constant
// friction has less effect and it coasts longer.
constexpr double kCoastTime33 = 8.0; // seconds
constexpr double kCoastDragRatio = 2.0;
const double kCoastDrag = std::log(1.0 + kCoastDragRatio) / kCoastTime33;
const double kCoastFriction33 = kCoastDrag / kCoastDragRatio;
const double kCoastFriction45 = kCoastFriction33 * (100.0 / 3.0) / 45.0;
// Spin-up: motor torque (speed units per second), then soft saturation
// when the platter settles into quartz lock
constexpr double kSpinupTorque33 = 0.45;
constexpr double kSpinupTorque45 = 0.75;
constexpr double kQuartzLockStart = 0.9;
constexpr double kQuartzLockGain = 2.0;
// Range of the spin-up / spin-down time preferences, 1.0 = Technics SL-1210
constexpr double kMinRampTimeFactor = 0.25;
constexpr double kMaxRampTimeFactor = 4.0;

// Backspin: the hand drives the platter from its current speed to the
// backspin speed within kBackspinFlickTime. Changing the direction at once
// would be an infinite acceleration and throw the needle out of the groove.
constexpr double kBackspinFlickTime = 0.08; // seconds
// Estimated fastest backspin that keeps the needle in the groove,
// 2 rev/s = -3.6x at 33 RPM, -2.7x at 45 RPM
constexpr double kMaxBackspinRevPerSec = 2.0;
constexpr double kDefaultBackspinRevPerSec = 1.0;

// Backspin to hotcue: largest position error at standstill that is
// corrected silently. A bigger error means the backspin was disturbed.
constexpr double kMaxLandingCorrection = 0.1; // seconds

// One step of the motor spinning the platter up: constant torque, then soft
// saturation when it settles into quartz lock
void advanceSpinUp(double* pPlatterSpeed, int rpmMode, double upDt) {
    *pPlatterSpeed += ((rpmMode == 0) ? kSpinupTorque33 : kSpinupTorque45) * upDt;
    if (*pPlatterSpeed > kQuartzLockStart) {
        *pPlatterSpeed += (1.0 - *pPlatterSpeed) * kQuartzLockGain * upDt;
    }
}

// Track seconds (at speed 1) the motor moves the platter from standstill
// until it reaches spinUpTarget, buffer by buffer like calculateSpeed().
// EngineBuffer advances the play position by speed * buffer length.
double spinUpDistance(double spinUpTarget, double dt, int rpmMode, double upFactor) {
    double platterSpeed = 0.0;
    double distance = 0.0;
    for (int i = 0; i < 1000000; ++i) {
        advanceSpinUp(&platterSpeed, rpmMode, dt / upFactor);
        if (platterSpeed >= 1.0) {
            break;
        }
        distance += spinUpTarget * platterSpeed * dt;
    }
    return distance;
}

double nominalRevPerSec(int rpmMode) {
    return ((rpmMode == 0) ? 100.0 / 3.0 : 45.0) / 60.0;
}

// Deceleration of a platter spinning backwards (speed < 0) in speed units
// per second. Bearing friction and drag always act. With the motor on, the
// motor pulls towards the forward target speed. With the motor off, the
// electronic brake acts, unless the power is cut.
double backspinDeceleration(double speed, bool motorOn, int rpmMode, int rampDownMode) {
    const bool is33 = rpmMode == 0;
    double deceleration = (is33 ? kCoastFriction33 : kCoastFriction45) +
            kCoastDrag * -speed;
    if (motorOn) {
        deceleration += is33 ? kSpinupTorque33 : kSpinupTorque45;
    } else if (rampDownMode == 0) {
        deceleration += 1.0 / (is33 ? kSpindownTime33 : kSpindownTime45);
    }
    return deceleration;
}

// Advances a backspin by dt. Returns false once the platter stands still.
// The flick is done by hand, so the ramp time preferences only stretch
// the physics after the release.
bool advanceBackspin(double* pSpeed,
        bool* pFlicking,
        double target,
        double flickAccel,
        double dt,
        bool motorOn,
        int rpmMode,
        int rampDownMode,
        double upFactor,
        double downFactor) {
    if (*pFlicking) {
        *pSpeed -= flickAccel * dt;
        if (*pSpeed <= target) {
            *pSpeed = target;
            *pFlicking = false;
        }
        return true;
    }
    const double physicsDt = dt / (motorOn ? upFactor : downFactor);
    *pSpeed += backspinDeceleration(*pSpeed, motorOn, rpmMode, rampDownMode) * physicsDt;
    return *pSpeed < 0.0;
}

// Settings that shape a backspin after the flick
struct BackspinPhysics {
    int rpmMode;       // 0 = 33 RPM, 1 = 45 RPM
    bool motorOn;      // play on after the backspin: the motor pulls back
    bool spinUp;       // motor on: spin-up ramp, or full speed at standstill
    int rampDownMode;  // motor off: 0 = brake, 1 = power cut
    double upFactor;   // spin-up time preference, 1.0 = SL-1210
    double downFactor; // spin-down time preference, 1.0 = SL-1210
};

// Track seconds moved by a backspin from the flick until the deck plays at
// spinUpTarget again (motor on), or stands still (motor off).
// Negative = it lands behind the start.
// Without spin-up, playback continues at full speed from the standstill.
double backspinLandingOffset(double startSpeed,
        double revPerSec,
        double spinUpTarget,
        double dt,
        const BackspinPhysics& physics) {
    const double target = -revPerSec / nominalRevPerSec(physics.rpmMode);
    double speed = startSpeed;
    bool flicking = speed > target;
    const double flickAccel = (speed - target) / kBackspinFlickTime;
    double offset = 0.0;
    for (int i = 0; i < 1000000 &&
            advanceBackspin(&speed,
                    &flicking,
                    target,
                    flickAccel,
                    dt,
                    physics.motorOn,
                    physics.rpmMode,
                    physics.rampDownMode,
                    physics.upFactor,
                    physics.downFactor);
            ++i) {
        offset += speed * dt;
    }
    // One buffer at standstill, then the spin-up
    if (physics.motorOn && physics.spinUp) {
        offset += spinUpDistance(spinUpTarget, dt, physics.rpmMode, physics.upFactor);
    }
    return offset;
}

// Backspin power in rev/s that lands at offset (track seconds, negative =
// behind the start), or a negative value if it is out of reach with at most
// maxRevPerSec (the backspin power of the preferences)
double backspinPowerForOffset(double offset,
        double startSpeed,
        double spinUpTarget,
        double dt,
        const BackspinPhysics& physics,
        double maxRevPerSec) {
    double low = 0.0;
    double high = std::clamp(maxRevPerSec, 0.0, kMaxBackspinRevPerSec);
    const auto landing = [&](double revPerSec) {
        return backspinLandingOffset(startSpeed, revPerSec, spinUpTarget, dt, physics);
    };
    // More power lands further back
    if (landing(high) > offset || landing(low) < offset) {
        return -1.0;
    }
    for (int i = 0; i < 40; ++i) {
        const double middle = (low + high) / 2.0;
        if (landing(middle) > offset) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return (low + high) / 2.0;
}
} // namespace

// Static default values for rate buttons (percents)
ControlValueAtomic<double> RateControl::m_dTemporaryRateChangeCoarse;
ControlValueAtomic<double> RateControl::m_dTemporaryRateChangeFine;
ControlValueAtomic<double> RateControl::m_dPermanentRateChangeCoarse;
ControlValueAtomic<double> RateControl::m_dPermanentRateChangeFine;
int RateControl::m_iRateRampSensitivity;
RateControl::RampMode RateControl::m_eRateRampMode;

std::atomic<bool> RateControl::m_rampUpEnabled{false};
std::atomic<bool> RateControl::m_rampDownEnabled{false};
std::atomic<int> RateControl::m_rampMode{0};
std::atomic<bool> RateControl::m_rampIncludePitch{true};
std::atomic<int> RateControl::m_rampDownMode{0};
std::atomic<double> RateControl::m_rampUpTimeFactor{1.0};
std::atomic<double> RateControl::m_rampDownTimeFactor{1.0};

std::atomic<bool> RateControl::m_backspinOnReverse{false};
std::atomic<double> RateControl::m_backspinRevPerSec{kDefaultBackspinRevPerSec};
std::atomic<bool> RateControl::m_backspinToHotcue{false};
std::atomic<bool> RateControl::m_backspinPlayOn{false};

const double RateControl::kWheelMultiplier = 40.0;
const double RateControl::kPausedJogMultiplier = 18.0;

RateControl::RateControl(const QString& group, UserSettingsPointer pConfig)
        : EngineControl(group, pConfig),
          m_pBpmControl(nullptr),
          m_pSampleRate(QStringLiteral("[App]"), QStringLiteral("samplerate")),
          m_pRateRatio(std::make_unique<ControlObject>(
                  ConfigKey(group, QStringLiteral("rate_ratio")),
                  true,
                  false,
                  false,
                  1.0)),
          m_pRateDir(std::make_unique<ControlObject>(
                  ConfigKey(group, QStringLiteral("rate_dir")))),
          m_pRateRange(std::make_unique<ControlPotmeter>(
                  ConfigKey(group, QStringLiteral("rateRange")), 0.01, 4.00)),
          // We need the sample rate so we can guesstimate something close
          // what latency is.
          // Allow rate slider to go out of bounds so that sync lock rate
          // adjustments are not capped.
          m_pRateSlider(std::make_unique<ControlPotmeter>(
                  ConfigKey(group, QStringLiteral("rate")), -1.0, 1.0, true)),
          // Search rate. Rate used when searching in sound. This overrules the
          // playback rate
          m_pRateSearch(std::make_unique<ControlPotmeter>(
                  ConfigKey(group, QStringLiteral("rateSearch")), -300., 300.)),
          // Temporary rate-change buttons
          m_pButtonRateTempDown(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_temp_down")))),
          m_pButtonRateTempDownSmall(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_temp_down_small")))),
          m_pButtonRateTempUp(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_temp_up")))),
          m_pButtonRateTempUpSmall(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_temp_up_small")))),
          // Permanent rate-change buttons
          m_pButtonRatePermDown(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_perm_down")))),
          m_pButtonRatePermDownSmall(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_perm_down_small")))),
          m_pButtonRatePermUp(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_perm_up")))),
          m_pButtonRatePermUpSmall(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("rate_perm_up_small")))),
          // Reverse button
          m_pReverseButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("reverse")))),
          m_pReverseRollButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("reverseroll")))),
          m_pForwardButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("fwd")))),
          m_pBackButton(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("back")))),
          // Wheel to control playback position/speed
          m_pWheel(std::make_unique<ControlTTRotary>(
                  ConfigKey(group, QStringLiteral("wheel")))),
          // Scratch controller, this is an accumulator which is useful for
          // controllers that return individual +1 or -1s, these get added up
          // and cleared when we read
          m_pScratch2(std::make_unique<ControlObject>(
                  ConfigKey(group, QStringLiteral("scratch2")))),
          m_pScratch2Enable(std::make_unique<ControlPushButton>(
                  ConfigKey(group, QStringLiteral("scratch2_enable")))),
          m_pScratch2Scratching(std::make_unique<ControlPushButton>(ConfigKey(
                  group, QStringLiteral("scratch2_indicates_scratching")))),
          m_pScratchController(
                  std::make_unique<PositionScratchController>(group)),
          m_pJog(std::make_unique<ControlObject>(
                  ConfigKey(group, QStringLiteral("jog")))),
          // FIXME: The filter length should be dependent on sample rate/block size or something
          m_pJogFilter(std::make_unique<Rotary>(25)),
          m_pVCEnabled(nullptr),
          m_pVCScratching(nullptr),
          m_pVCMode(nullptr),
          m_syncMode(group, QStringLiteral("sync_mode")),
          m_slipEnabled(group, QStringLiteral("slip_enabled")),
          m_wrapAroundCount(0),
          m_jumpPos(mixxx::audio::FramePos()),
          m_targetPos(mixxx::audio::FramePos()),
          m_bTempStarted(false),
          m_tempRateRatio(0.0),
          m_dRateTempRampChange(0.0),
          m_platterSpeed(1.0),
          m_lastTargetRate(0.0),
          m_spinningDown(false),
          m_seekedSinceLastBuffer(false),
          m_prevPaused(true),
          m_latchedPlay(false),
          m_resetRampRequested(false),
          m_rampAllowed(PlayerManager::isDeckGroup(group)),
          m_backspinActive(false),
          m_backspinRequested(false),
          m_reverseRollActive(false),
          m_backspinReversing(false),
          m_landingSeekPending(false) {
    // Vinyl control COs are only created for main decks
    if (PlayerManager::isDeckGroup(getGroup())) {
        m_pVCEnabled = ControlObject::getControl(
                ConfigKey(getGroup(), QStringLiteral("vinylcontrol_enabled")),
                ControlFlag::NoAssertIfMissing);
        m_pVCScratching = ControlObject::getControl(
                ConfigKey(getGroup(), QStringLiteral("vinylcontrol_scratching")),
                ControlFlag::NoAssertIfMissing);
        m_pVCMode = ControlObject::getControl(
                ConfigKey(getGroup(), QStringLiteral("vinylcontrol_mode")),
                ControlFlag::NoAssertIfMissing);
    }
    // This is the resulting rate ratio that can be used for display or calculations.
    // The track original rate ratio is 1.
    connect(m_pRateRatio.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotRateRatioChanged,
            Qt::DirectConnection);

    connect(m_pRateDir.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotRateRangeChanged,
            Qt::DirectConnection);
    connect(m_pRateRange.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotRateRangeChanged,
            Qt::DirectConnection);

    connect(m_pRateSlider.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotRateSliderChanged,
            Qt::DirectConnection);

    m_pReverseButton->set(0);
    connect(m_pReverseButton.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotReverseChanged,
            Qt::DirectConnection);

    connect(m_pForwardButton.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotControlFastForward,
            Qt::DirectConnection);
    m_pForwardButton->set(0);

    connect(m_pBackButton.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotControlFastBack,
            Qt::DirectConnection);
    m_pBackButton->set(0);

    connect(m_pReverseRollButton.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotReverseRollActivate,
            Qt::DirectConnection);

    // Permanent rate-change buttons
    m_pButtonRatePermDown->setKbdRepeatable(true);
    connect(m_pButtonRatePermDown.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotControlRatePermDown,
            Qt::DirectConnection);

    m_pButtonRatePermDownSmall->setKbdRepeatable(true);
    connect(m_pButtonRatePermDownSmall.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotControlRatePermDownSmall,
            Qt::DirectConnection);

    m_pButtonRatePermUp->setKbdRepeatable(true);
    connect(m_pButtonRatePermUp.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotControlRatePermUp,
            Qt::DirectConnection);

    m_pButtonRatePermUpSmall->setKbdRepeatable(true);
    connect(m_pButtonRatePermUpSmall.get(),
            &ControlObject::valueChanged,
            this,
            &RateControl::slotControlRatePermUpSmall,
            Qt::DirectConnection);

    // Scratch enable toggle
    m_pScratch2Enable->set(0);

    // Enable by default, because it was always scratching before introducing
    // this control.
    m_pScratch2Scratching->set(1.0);

    //     // Update Internal Settings
    //     // Set Pitchbend Mode
    //     m_eRateRampMode = static_cast<RampMode>(
    //         getConfig()->getValue(ConfigKey("[Controls]","RateRamp"),
    //                               static_cast<int>(RampMode::Stepping)));

    //     // Set the Sensitivity
    //     m_iRateRampSensitivity =
    //             getConfig()->getValueString(ConfigKey("[Controls]","RateRampSensitivity")).toInt();
}

void RateControl::setBpmControl(BpmControl* bpmcontrol) {
    m_pBpmControl = bpmcontrol;
}

// static
void RateControl::setRateRampMode(RampMode mode) {
    m_eRateRampMode = mode;
}

// static
RateControl::RampMode RateControl::getRateRampMode() {
    return m_eRateRampMode;
}

// static
void RateControl::setRateRampSensitivity(int sense) {
    // Reverse the actual sensitivity value passed.
    // That way the gui works in an intuitive manner.
    sense = kRateSensitivityMax - sense + kRateSensitivityMin;
    if (sense < kRateSensitivityMin) {
        m_iRateRampSensitivity = kRateSensitivityMin;
    } else if (sense > kRateSensitivityMax) {
        m_iRateRampSensitivity = kRateSensitivityMax;
    } else {
        m_iRateRampSensitivity = sense;
    }
}

// static
void RateControl::setRampUpEnabled(bool enabled) {
    m_rampUpEnabled = enabled;
}

// static
void RateControl::setRampDownEnabled(bool enabled) {
    m_rampDownEnabled = enabled;
}

// static
void RateControl::setRampDownMode(int mode) {
    m_rampDownMode = mode;
}

// static
void RateControl::setRampRpmMode(int mode) {
    m_rampMode = mode;
}

// static
void RateControl::setRampIncludePitch(bool includePitch) {
    m_rampIncludePitch = includePitch;
}

// static
void RateControl::setRampUpTimeFactor(double factor) {
    m_rampUpTimeFactor = std::clamp(factor, kMinRampTimeFactor, kMaxRampTimeFactor);
}

// static
void RateControl::setRampDownTimeFactor(double factor) {
    m_rampDownTimeFactor = std::clamp(factor, kMinRampTimeFactor, kMaxRampTimeFactor);
}

// static
double RateControl::spinUpTime(int rpmMode, double factor) {
    // Integrate the spin-up model of calculateSpeed() with a fine time step
    constexpr double kStep = 0.0005; // seconds
    double platterSpeed = 0.0;
    double time = 0.0;
    while (platterSpeed < 1.0 && time < 60.0) {
        advanceSpinUp(&platterSpeed, rpmMode, kStep);
        time += kStep;
    }
    return time * factor;
}

// static
double RateControl::spinDownTime(int rpmMode, int rampDownMode, double factor) {
    if (rampDownMode == 1) {
        // Power cut: solution of dv/dt = -(friction + drag * v) from 1 to 0
        const double friction = (rpmMode == 0) ? kCoastFriction33 : kCoastFriction45;
        return std::log(1.0 + kCoastDrag / friction) / kCoastDrag * factor;
    }
    return ((rpmMode == 0) ? kSpindownTime33 : kSpindownTime45) * factor;
}

// static
void RateControl::setBackspinOnReverse(bool enabled) {
    m_backspinOnReverse = enabled;
}

// static
void RateControl::setBackspinRevPerSec(double revPerSec) {
    m_backspinRevPerSec = std::clamp(revPerSec, 0.0, kMaxBackspinRevPerSec);
}

// static
double RateControl::maxBackspinRevPerSec() {
    return kMaxBackspinRevPerSec;
}

// static
void RateControl::setBackspinToHotcue(bool enabled) {
    m_backspinToHotcue = enabled;
}

// static
void RateControl::setBackspinPlayOn(bool playOn) {
    m_backspinPlayOn = playOn;
}

// static
double RateControl::backspinHotcueReach(int rpmMode,
        double upFactor,
        double revPerSec,
        bool spinUp,
        bool playOn,
        int rampDownMode,
        double downFactor) {
    constexpr double kStep = 0.0005; // seconds
    const BackspinPhysics physics{rpmMode,
            playOn,
            spinUp,
            rampDownMode,
            // Without spin-up the spin-up time preference has no effect
            spinUp ? std::clamp(upFactor, kMinRampTimeFactor, kMaxRampTimeFactor) : 1.0,
            std::clamp(downFactor, kMinRampTimeFactor, kMaxRampTimeFactor)};
    revPerSec = std::clamp(revPerSec, 0.0, kMaxBackspinRevPerSec);
    return -backspinLandingOffset(1.0, revPerSec, 1.0, kStep, physics);
}

// static
RateControl::BackspinTimes RateControl::backspinTimes(int rpmMode,
        bool motorOn,
        int rampDownMode,
        double revPerSec,
        double upFactor,
        double downFactor,
        bool spinUp,
        std::optional<double> startSpeed) {
    // Integrate the backspin model of calculateSpeed() with a fine time step
    constexpr double kStep = 0.0005; // seconds
    // Without spin-up the spin-up time preference has no effect
    upFactor = spinUp ? std::clamp(upFactor, kMinRampTimeFactor, kMaxRampTimeFactor)
                      : 1.0;
    downFactor = std::clamp(downFactor, kMinRampTimeFactor, kMaxRampTimeFactor);
    const double target = -std::clamp(revPerSec, 0.0, kMaxBackspinRevPerSec) /
            nominalRevPerSec(rpmMode);
    double speed = startSpeed.value_or(motorOn ? 1.0 : 0.0);
    const double flickAccel = (speed - target) / kBackspinFlickTime;
    bool flicking = speed > target;
    BackspinTimes times{0.0, 0.0, 0.0};
    while (times.backwardTime < 120.0 &&
            advanceBackspin(&speed,
                    &flicking,
                    target,
                    flickAccel,
                    kStep,
                    motorOn,
                    rpmMode,
                    rampDownMode,
                    upFactor,
                    downFactor)) {
        times.backwardTime += kStep;
        if (speed < 0.0) {
            times.rewind -= speed * kStep;
        }
    }
    times.recoverTime = motorOn
            ? times.backwardTime + (spinUp ? spinUpTime(rpmMode, upFactor) : 0.0)
            : 0.0;
    return times;
}

// static
void RateControl::setTemporaryRateChangeCoarseAmount(double v) {
    m_dTemporaryRateChangeCoarse.setValue(v);
}

// static
void RateControl::setTemporaryRateChangeFineAmount(double v) {
    m_dTemporaryRateChangeFine.setValue(v);
}

// static
void RateControl::setPermanentRateChangeCoarseAmount(double v) {
    m_dPermanentRateChangeCoarse.setValue(v);
}

// static
void RateControl::setPermanentRateChangeFineAmount(double v) {
    m_dPermanentRateChangeFine.setValue(v);
}

// static
double RateControl::getTemporaryRateChangeCoarseAmount() {
    return m_dTemporaryRateChangeCoarse.getValue();
}

// static
double RateControl::getTemporaryRateChangeFineAmount() {
    return m_dTemporaryRateChangeFine.getValue();
}

// static
double RateControl::getPermanentRateChangeCoarseAmount() {
    return m_dPermanentRateChangeCoarse.getValue();
}

// static
double RateControl::getPermanentRateChangeFineAmount() {
    return m_dPermanentRateChangeFine.getValue();
}

void RateControl::slotRateRangeChanged(double) {
    // update RateSlider with the new Range value butdo not change m_pRateRatio
    slotRateRatioChanged(m_pRateRatio->get());
}

void RateControl::slotRateSliderChanged(double v) {
    double rateRatio = 1.0 + m_pRateDir->get() * m_pRateRange->get() * v;
    m_pRateRatio->set(rateRatio);
}

void RateControl::slotRateRatioChanged(double v) {
    double rateRange = m_pRateRange->get();
    if (rateRange > 0.0) {
        double newRate = m_pRateDir->get() * (v - 1) / rateRange;
        m_pRateSlider->set(newRate);
    } else {
        m_pRateSlider->set(0);
    }
}

void RateControl::slotReverseRollActivate(double v) {
    if (v > 0.0) {
        // Censor always plays in reverse, also in backspin mode
        m_reverseRollActive = true;
        m_slipEnabled.set(1);
        m_pReverseButton->set(1);
    } else {
        m_pReverseButton->set(0);
        m_slipEnabled.set(0);
        m_reverseRollActive = false;
    }
}

void RateControl::slotReverseChanged(double v) {
    if (v > 0.0 && backspinOnReverse() && !m_reverseRollActive) {
        // Applied in calculateSpeed()
        m_backspinRequested = true;
    }
}

bool RateControl::backspinOnReverse() const {
    return m_rampAllowed && m_backspinOnReverse;
}

void RateControl::slotControlFastForward(double v) {
    // qDebug() << "slotControlFastForward(" << v << ")";
    if (v > 0.0) {
        m_pRateSearch->set(4.0);
    } else {
        m_pRateSearch->set(0.0);
    }
}

void RateControl::slotControlFastBack(double v) {
    // qDebug() << "slotControlFastBack(" << v << ")";
    if (v > 0.0) {
        m_pRateSearch->set(-4.0);
    } else {
        m_pRateSearch->set(0.0);
    }
}

void RateControl::slotControlRatePermDown(double v) {
    // Adjusts temp rate down if button pressed
    if (v > 0.0) {
        m_pRateSlider->set(m_pRateSlider->get() -
                m_pRateDir->get() * m_dPermanentRateChangeCoarse.getValue() / (100 * m_pRateRange->get()));
        slotRateSliderChanged(m_pRateSlider->get());
    }
}

void RateControl::slotControlRatePermDownSmall(double v) {
    // Adjusts temp rate down if button pressed
    if (v > 0.0) {
        m_pRateSlider->set(m_pRateSlider->get() -
                m_pRateDir->get() * m_dPermanentRateChangeFine.getValue() / (100. * m_pRateRange->get()));
        slotRateSliderChanged(m_pRateSlider->get());
    }
}

void RateControl::slotControlRatePermUp(double v) {
    // Adjusts temp rate up if button pressed
    if (v > 0.0) {
        m_pRateSlider->set(m_pRateSlider->get() +
                m_pRateDir->get() * m_dPermanentRateChangeCoarse.getValue() / (100. * m_pRateRange->get()));
        slotRateSliderChanged(m_pRateSlider->get());
    }
}

void RateControl::slotControlRatePermUpSmall(double v) {
    // Adjusts temp rate up if button pressed
    if (v > 0.0) {
        m_pRateSlider->set(m_pRateSlider->get() +
                m_pRateDir->get() * m_dPermanentRateChangeFine.getValue() /
                        (100. * m_pRateRange->get()));
        slotRateSliderChanged(m_pRateSlider->get());
    }
}

double RateControl::getWheelFactor() const {
    return m_pWheel->get();
}

double RateControl::getJogFactor() const {
    // FIXME: Sensitivity should be configurable separately?
    constexpr double jogSensitivity = 0.1; // Nudges during playback
    double jogValue = m_pJog->get();

    // Since m_pJog is an accumulator, reset it since we've used its value.
    if (jogValue != 0.) {
        m_pJog->set(0.);
    }

    double jogValueFiltered = m_pJogFilter->filter(jogValue);
    double jogFactor = jogValueFiltered * jogSensitivity;

    if (util_isnan(jogValue) || util_isnan(jogFactor)) {
        jogFactor = 0.0;
    }

    return jogFactor;
}

SyncMode RateControl::getSyncMode() const {
    return syncModeFromDouble(m_syncMode.get());
}

double RateControl::calculateSpeed(double baserate,
        double speed,
        bool paused,
        std::size_t samplesPerBuffer,
        bool* pReportScratching,
        bool* pReportReverse) {
    *pReportScratching = false;
    *pReportReverse = false;

    processTempRate(samplesPerBuffer);

    double rate;
    // Rate slider contribution to rate. The ramp subtracts it when it
    // targets the nominal 33/45 RPM speed, keeping nudges and pitch bend
    double sliderOffset = 0.0;
    const double searching = m_pRateSearch->get();
    if (searching != 0) {
        // If searching is in progress, it overrides everything else
        rate = searching;
    } else {
        double wheelFactor = getWheelFactor();
        double jogFactor = getJogFactor();
        bool bVinylControlEnabled = m_pVCEnabled && m_pVCEnabled->toBool();
        bool useScratch2Value = m_pScratch2Enable->toBool();

        // By default scratch2_enable is enough to determine if the user is
        // scratching or not. Moving platter controllers have to disable
        // "scratch2_indicates_scratching" if they are not scratching,
        // to allow things like key-lock.
        if (useScratch2Value && m_pScratch2Scratching->toBool()) {
            *pReportScratching = true;
        }

        if (bVinylControlEnabled) {
            if (m_pVCScratching && m_pVCScratching->toBool()) {
                *pReportScratching = true;
            }
            rate = speed;
        } else {
            double scratchFactor = m_pScratch2->get();
            // Don't trust values from m_pScratch2
            if (util_isnan(scratchFactor)) {
                scratchFactor = 0.0;
            }
            if (paused) {
                // Stopped. Wheel, jog and scratch controller all scrub through audio.
                if (useScratch2Value) {
                    rate = scratchFactor + jogFactor + wheelFactor * kWheelMultiplier;
                } else {
                    rate = jogFactor * kPausedJogMultiplier + wheelFactor;
                }
            } else {
                // The buffer is playing, so calculate the buffer rate.

                // There are four rate effects we apply: wheel, scratch, jog and temp.
                // Wheel:   a linear additive effect (no spring-back)
                // Scratch: a rate multiplier
                // Jog:     a linear additive effect whose value is filtered (springs back)
                // Temp:    pitch bend

                // New scratch behavior - overrides playback speed (and old behavior)
                if (useScratch2Value) {
                    rate = scratchFactor;
                } else {
                    // add temp rate, but don't go backwards
                    rate = math_max(speed + getTempRate(), 0.0);
                    rate += wheelFactor;
                    sliderOffset = speed - 1.0;
                }
                rate += jogFactor;
            }
        }

        double currentSample = frameInfo().currentPosition.toEngineSamplePos();
        // Let PositionScratchController also know if the play pos wrapped around
        // (beatloop or track repeat) so it can correctly interpret the sample position delta.
        m_pScratchController->process(currentSample,
                rate,
                samplesPerBuffer,
                baserate,
                m_wrapAroundCount,
                m_jumpPos,
                m_targetPos);
        // Reset count after use.
        m_wrapAroundCount = 0;

        // If waveform scratch is enabled, override all other controls
        if (m_pScratchController->isEnabled()) {
            rate = m_pScratchController->getRate();
            *pReportScratching = true;
        } else {
            // If sync lock is on, respond to it -- but vinyl and scratch mode always override.
            if (toSynchronized(getSyncMode()) && !paused &&
                    !bVinylControlEnabled && !useScratch2Value) {
                if (m_pBpmControl == nullptr) {
                    qDebug() << "ERROR: calculateRate m_pBpmControl is null during sync lock";
                    return 1.0;
                }

                double userTweak = 0.0;
                if (!*pReportScratching) {
                    // Only report user tweak if the user is not scratching.
                    userTweak = getTempRate() + wheelFactor + jogFactor;
                }
                rate = m_pBpmControl->calcSyncedRate(userTweak);
                // Sync lock dictates the target speed, including during a ramp
                sliderOffset = 0.0;
            }
            // If we are reversing (and not scratching,) flip the rate.  This is ok even when syncing.
            // Reverse with vinyl is only ok if absolute mode isn't on.
            int vcmode = m_pVCMode ? static_cast<int>(m_pVCMode->get()) : MIXXX_VCMODE_ABSOLUTE;
            // TODO(owen): Instead of just ignoring reverse mode, should we
            // disable absolute mode instead?
            // In backspin mode the reverse button flicks the record instead,
            // only censor (reverseroll) plays in reverse.
            const bool reversePlayback = !backspinOnReverse() || m_reverseRollActive;
            if (reversePlayback && m_pReverseButton->toBool() &&
                    !m_pScratch2Enable->toBool() &&
                    (!bVinylControlEnabled ||
                            vcmode != MIXXX_VCMODE_ABSOLUTE)) {
                rate = -rate;
                *pReportReverse = true;
            }
        }
    }

    if (!m_rampAllowed) {
        return rate;
    }

    if (m_resetRampRequested.exchange(false)) {
        m_platterSpeed = 1.0;
        m_spinningDown = false;
        m_latchedPlay = false;
        m_prevPaused = true;
        m_backspinActive = false;    // NEW
        m_backspinRequested = false; // NEW
    }

    // Physical SL-1210 spin-up / spin-down ramp
    // EngineBuffer passes paused = !play, and play is also 1 during cue
    // preview, so play can't tell a Play press from a cue preview.
    // play_latched (owned by CueControl) is 1 only for a latched play.
    const bool playLatched = m_pPlayLatched && m_pPlayLatched->toBool();
    const bool seeked = m_seekedSinceLastBuffer;
    m_seekedSinceLastBuffer = false;
    // EngineBuffer processes a queued seek before calculateSpeed(), and skips
    // it when the position did not change
    m_landingSeekPending = false;

    // Speed set by hand: scratching (spinny, waveform, controller platter),
    // jog/wheel scrubbing while paused, searching or vinyl control
    const bool manualSpeed = searching != 0 || *pReportScratching ||
            (paused && rate != 0.0) ||
            m_pScratch2Enable->toBool() ||
            (m_pVCEnabled && m_pVCEnabled->toBool());

    // play_latched drops to 0 slightly before play when pausing, so the
    // last playing buffer may already see 0. Remember it per play period.
    if (!paused && playLatched) {
        m_latchedPlay = true;
    }

    // moved up from below the state machine, the backspin needs it
    const double dt = static_cast<double>(samplesPerBuffer) / m_pSampleRate.get() / 2;
    // Spin-up target: the pitched speed, or the nominal 33/45 RPM speed
    // without the rate slider. Nudges and pitch bend are kept.
    // rate is already negated for reverse.
    const double spinUpTarget = m_rampIncludePitch
            ? rate
            : rate - (*pReportReverse ? -sliderOffset : sliderOffset);

    // NEW: backspin on the reverse button
    if (m_backspinRequested.exchange(false) && backspinOnReverse() && !manualSpeed) {
        startBackspin(paused, rate, spinUpTarget, dt);
    }
    m_backspinReversing = false;

    if (m_backspinActive) {
        if (manualSpeed || seeked) {
            // Record caught by hand or jump: the backspin is over, the
            // ramp logic below takes over
            m_backspinActive = false;
        } else {
            // The motor runs while the deck plays. Stop during the backspin
            // switches it off (brake or power cut), Play switches it on.
            // Staying paused, the pause from the flick applies with the
            // next buffer.
            if (paused) {
                m_backspin.stopping = false;
            }
            const bool motorOn = !paused && !m_backspin.stopping;
            // The backspin works without the spin-up ramp as well: the motor
            // still pulls the platter back, at standstill playback continues
            // at full speed. The spin-up time preference only applies with
            // the spin-up ramp.
            const bool spinUp = m_rampUpEnabled;
            const double backspinUpFactor = spinUp ? m_rampUpTimeFactor.load() : 1.0;
            if (motorOn != m_backspin.landingMotorOn) {
                // Play or Stop pressed during the backspin: the landing on
                // the hotcue was planned for the other case
                m_backspin.landingPosition = mixxx::audio::kInvalidFramePos;
            }
            if (advanceBackspin(&m_backspin.speed,
                        &m_backspin.flicking,
                        m_backspin.target,
                        m_backspin.flickAccel,
                        dt,
                        motorOn,
                        m_rampMode,
                        m_rampDownMode,
                        backspinUpFactor,
                        m_rampDownTimeFactor)) {
                m_prevPaused = paused;
                if (paused) {
                    m_latchedPlay = false;
                }
                // Free platter: vinyl pitch without keylock, and the linear
                // scaler passes smoothly through zero like when scratching
                *pReportScratching = true;
                m_backspinReversing = m_backspin.speed < 0.0;
                // Hand speed, independent of the rate slider
                return m_backspin.speed;
            }
            // Standstill
            m_backspinActive = false;
            m_spinningDown = false;
            if (!motorOn) {
                m_platterSpeed = 1.0;
                m_prevPaused = true;
                m_latchedPlay = false;
                if (m_backspin.landingPosition.isValid()) {
                    // Stopped at the hotcue: correct the small error of the
                    // prediction silently
                    const auto info = frameInfo();
                    const double error =
                            std::abs(m_backspin.landingPosition - info.currentPosition);
                    if (info.sampleRate.isValid() && error >= 0.5 &&
                            error < kMaxLandingCorrection * info.sampleRate.toDouble()) {
                        m_landingSeekPending = true;
                        seekExact(m_backspin.landingPosition);
                    }
                    m_backspin.landingPosition = mixxx::audio::kInvalidFramePos;
                }
                return rate; // paused rate
            }
            // Motor on: the regular spin-up below continues from standstill,
            // without spin-up ramp playback continues at full speed
            m_platterSpeed = spinUp ? 0.0 : 1.0;
            if (m_backspin.landingPosition.isValid()) {
                // Backspin to hotcue: the platter stands still for one
                // buffer. Nothing is audible, so the small error of the
                // prediction is corrected here and the spin-up reaches full
                // speed exactly at the hotcue. Without spin-up, playback
                // starts at the hotcue.
                const auto info = frameInfo();
                if (info.sampleRate.isValid()) {
                    const double remaining = spinUp
                            ? spinUpDistance(spinUpTarget, dt, m_rampMode, backspinUpFactor)
                            : 0.0;
                    const auto seekPosition = m_backspin.landingPosition -
                            remaining * info.sampleRate.toDouble();
                    const double error = std::abs(seekPosition - info.currentPosition);
                    if (error >= 0.5 &&
                            error < kMaxLandingCorrection * info.sampleRate.toDouble()) {
                        m_landingSeekPending = true;
                        seekExact(seekPosition);
                    }
                }
                m_backspin.landingPosition = mixxx::audio::kInvalidFramePos;
                *pReportScratching = true;
                return 0.0;
            }
        }
    }

    if (manualSpeed) {
        // Speed set by hand: cancel a running ramp
        m_platterSpeed = 1.0;
        m_spinningDown = false;
    } else if (!paused) {
        if (m_prevPaused) {
            if (playLatched && m_rampUpEnabled) {
                // Started by a latched Play: spin up. If the platter is still
                // spinning down, continue from its current speed.
                if (!m_spinningDown) {
                    m_platterSpeed = 0.0;
                }
            } else {
                // Cue preview, or ramp up disabled: start instantly
                m_platterSpeed = 1.0;
            }
            m_spinningDown = false;
        } else if (seeked) {
            // qDebug() << "RateControl -> spin-up cut by seek, platter was" << m_platterSpeed;
            // Jump while playing (hotcue, cue preview, beatjump, ...):
            // a running spin-up is cut, playback continues at full speed
            m_platterSpeed = 1.0;
        }
    } else {
        if (m_rampDownEnabled && !m_prevPaused && m_latchedPlay && !seeked) {
            // A latched play was paused: spin down from the current speed
            m_spinningDown = true;
        }
        if (seeked) {
            m_spinningDown = false;
        }
        if (!m_spinningDown) {
            m_platterSpeed = 1.0;
        }
    }
    if (paused) {
        m_latchedPlay = false;
    }
    m_prevPaused = paused;

    if (m_spinningDown) {
        // Spin-down time preference stretches the curve, 1.0 = SL-1210
        const double downDt = dt / m_rampDownTimeFactor;
        if (m_rampDownMode == 1) {
            // Power cut: the platter coasts without brake
            const double friction = (m_rampMode == 0) ? kCoastFriction33 : kCoastFriction45;
            m_platterSpeed -= (friction + kCoastDrag * m_platterSpeed) * downDt;
        } else {
            // Electronic brake: constant deceleration until standstill
            const double spindownTime = (m_rampMode == 0) ? kSpindownTime33 : kSpindownTime45;
            m_platterSpeed -= downDt / spindownTime;
        }
        if (m_platterSpeed <= 0.0) {
            m_platterSpeed = 1.0;
            m_spinningDown = false;
            return rate; // paused rate (0 unless jog scrubbing)
        }
        return m_lastTargetRate * m_platterSpeed;
    }

    if (m_platterSpeed < 1.0) {
        // Spin-up time preference stretches the curve, 1.0 = SL-1210
        // 33 RPM = heavier platter feel, 45 RPM = lighter
        advanceSpinUp(&m_platterSpeed, m_rampMode, dt / m_rampUpTimeFactor);
        if (m_platterSpeed < 1.0) {
            // target computed above as spinUpTarget (pitched or nominal)
            m_lastTargetRate = spinUpTarget;
            return m_lastTargetRate * m_platterSpeed;
        }
        m_platterSpeed = 1.0;
    }

    if (!paused) {
        m_lastTargetRate = rate;
    }
    return rate;
}

void RateControl::processTempRate(const std::size_t bufferSamples) {
    // Code to handle temporary rate change buttons.
    // We support two behaviors, the standard ramped pitch bending
    // and pitch shift stepping, which is the old behavior.

    RampDirection rampDirection = RampDirection::None;
    if (m_pButtonRateTempUp->toBool()) {
        rampDirection = RampDirection::Up;
    } else if (m_pButtonRateTempDown->toBool()) {
        rampDirection = RampDirection::Down;
    } else if (m_pButtonRateTempUpSmall->toBool()) {
        rampDirection = RampDirection::UpSmall;
    } else if (m_pButtonRateTempDownSmall->toBool()) {
        rampDirection = RampDirection::DownSmall;
    }

    if (rampDirection != RampDirection::None) {
        if (m_eRateRampMode == RampMode::Stepping) {
            if (!m_bTempStarted) {
                m_bTempStarted = true;
                // old temporary pitch shift behavior
                double change = m_dTemporaryRateChangeCoarse.getValue() / 100.0;
                double csmall = m_dTemporaryRateChangeFine.getValue() / 100.0;

                switch (rampDirection) {
                case RampDirection::Up:
                    setRateTemp(change);
                    break;
                case RampDirection::Down:
                    setRateTemp(-change);
                    break;
                case RampDirection::UpSmall:
                    setRateTemp(csmall);
                    break;
                case RampDirection::DownSmall:
                    setRateTemp(-csmall);
                    break;
                case RampDirection::None:
                default:
                    DEBUG_ASSERT(false);
                }
            }
        } else if (m_eRateRampMode == RampMode::Linear) {
            if (!m_bTempStarted) {
                m_bTempStarted = true;
                double latrate = bufferSamples / m_pSampleRate.get();
                m_dRateTempRampChange = latrate / (m_iRateRampSensitivity / 100.0);
            }

            switch (rampDirection) {
            case RampDirection::Up:
            case RampDirection::UpSmall:
                addRateTemp(m_dRateTempRampChange * m_pRateRange->get());
                break;
            case RampDirection::Down:
            case RampDirection::DownSmall:
                subRateTemp(m_dRateTempRampChange * m_pRateRange->get());
                break;
            case RampDirection::None:
            default:
                DEBUG_ASSERT(false);
            }
        }
    } else if (m_bTempStarted) {
        m_bTempStarted = false;
        resetRateTemp();
    }
}

double RateControl::getTempRate() {
    // qDebug() << m_tempRateRatio;
    return m_tempRateRatio;
}

void RateControl::setRateTemp(double v) {
    m_tempRateRatio = v;
    if (m_tempRateRatio < -1.0) {
        m_tempRateRatio = -1.0;
    } else if (m_tempRateRatio > 1.0) {
        m_tempRateRatio = 1.0;
    } else if (util_isnan(m_tempRateRatio)) {
        m_tempRateRatio = 0;
    }
}

void RateControl::addRateTemp(double v) {
    setRateTemp(m_tempRateRatio + v);
}

void RateControl::subRateTemp(double v) {
    setRateTemp(m_tempRateRatio - v);
}

void RateControl::resetRateTemp(void) {
    setRateTemp(0.0);
}

bool RateControl::isReverseButtonPressed() {
    if (backspinOnReverse() && !m_reverseRollActive) {
        // Loops and beatjumps follow the actual direction of the backspin
        return m_backspinReversing;
    }
    if (m_pReverseButton) {
        return m_pReverseButton->toBool();
    }
    return false;
}

mixxx::audio::FramePos RateControl::lastHotcueBefore(mixxx::audio::FramePos position) const {
    auto lastHotcue = mixxx::audio::kInvalidFramePos;
    for (const auto& pHotcuePosition : m_hotcuePositions) {
        const auto hotcue = mixxx::audio::FramePos::fromEngineSamplePosMaybeInvalid(
                pHotcuePosition->get());
        if (hotcue.isValid() && hotcue < position &&
                (!lastHotcue.isValid() || hotcue > lastHotcue)) {
            lastHotcue = hotcue;
        }
    }
    return lastHotcue;
}

void RateControl::startBackspin(bool paused, double rate, double spinUpTarget, double dt) {
    // Current platter speed, 1.0 = nominal 33/45 RPM
    double speed = 0.0;
    if (m_backspinActive) {
        speed = m_backspin.speed;
    } else if (m_spinningDown || (!paused && m_platterSpeed < 1.0)) {
        speed = m_lastTargetRate * m_platterSpeed;
    } else if (!paused) {
        speed = rate;
    }
    // Stay paused after the backspin: the deck is paused with the flick,
    // without the motor the platter brakes or coasts to a standstill
    const bool playOn = m_backspinPlayOn;
    m_backspin.stopping = !paused && !playOn;
    if (m_backspin.stopping && m_pPlay) {
        m_pPlay->set(0.0);
    }
    const bool motorOn = !paused && playOn;
    double revPerSec = m_backspinRevPerSec;
    m_backspin.landingPosition = mixxx::audio::kInvalidFramePos;
    m_backspin.landingMotorOn = motorOn;
    if (m_backspinToHotcue) {
        // Choose the power so that the motor brings the platter back to
        // full speed exactly at the last hotcue behind the play position,
        // or that the platter stops exactly there (stay paused, stopped
        // deck). The backspin power of the preferences is the maximum, out
        // of reach with it: normal backspin.
        const auto info = frameInfo();
        const auto hotcue = lastHotcueBefore(info.currentPosition);
        if (hotcue.isValid() && info.sampleRate.isValid()) {
            const double offset = (hotcue - info.currentPosition) /
                    info.sampleRate.toDouble();
            const bool spinUp = m_rampUpEnabled;
            const BackspinPhysics physics{m_rampMode,
                    motorOn,
                    spinUp,
                    m_rampDownMode,
                    spinUp ? m_rampUpTimeFactor.load() : 1.0,
                    m_rampDownTimeFactor};
            const double power = backspinPowerForOffset(
                    offset, speed, spinUpTarget, dt, physics, revPerSec);
            if (power >= 0.0) {
                revPerSec = power;
                m_backspin.landingPosition = hotcue;
            }
        }
    }
    m_backspin.target = -revPerSec / nominalRevPerSec(m_rampMode);
    m_backspin.speed = speed;
    // A platter already spinning back faster is not slowed by the hand
    m_backspin.flicking = speed > m_backspin.target;
    m_backspin.flickAccel = (speed - m_backspin.target) / kBackspinFlickTime;
    m_backspinActive = true;
    m_spinningDown = false;
    m_platterSpeed = 1.0;
}

void RateControl::notifyWrapAround(mixxx::audio::FramePos triggerPos,
        mixxx::audio::FramePos targetPos) {
    VERIFY_OR_DEBUG_ASSERT(triggerPos.isValid() && targetPos.isValid()) {
        m_wrapAroundCount = 0;
        // no need to reset the position, they're not used if count is 0.
        return;
    }
    m_wrapAroundCount++;
    m_jumpPos = triggerPos;
    m_targetPos = targetPos;
}

void RateControl::notifySeek(mixxx::audio::FramePos position) {
    // qDebug() << "RateControl::notifySeek" << getGroup() << position;
    m_pScratchController->notifySeek(position);
    if (m_landingSeekPending) {
        // Our own silent correction at the standstill of a backspin to
        // hotcue: the spin-up continues
        m_landingSeekPending = false;
        return;
    }
    // A pause combined with a jump (cue, start_stop, track load) must not
    // spin down from the new position
    m_seekedSinceLastBuffer = true;
}

void RateControl::resetPositionScratchController() {
    // Resets the scratch state to avoid engine freeze due to insanley high rate
    // reported on track load while scratching.
    // https://github.com/mixxxdj/mixxx/issues/15082
    m_pScratchController->reset();
    // Track changed: start the next track with a fresh motor state.
    // This runs in a worker thread, so calculateSpeed() applies it.
    requestRampReset();
}

void RateControl::setEngineMixer(EngineMixer* pEngineMixer) {
    EngineControl::setEngineMixer(pEngineMixer);
    // play_latched is owned by CueControl, which EngineBuffer creates after
    // RateControl. EngineBuffer calls setEngineMixer() once all controls
    // exist, outside the audio callback.
    m_pPlayLatched = std::make_unique<PollingControlProxy>(getGroup(),
            QStringLiteral("play_latched"),
            ControlFlag::AllowMissingOrInvalid);
    // play (owned by EngineBuffer), to pause the deck with the flick when
    // staying paused after the backspin
    m_pPlay = std::make_unique<PollingControlProxy>(getGroup(),
            QStringLiteral("play"),
            ControlFlag::AllowMissingOrInvalid);
    // Hotcues are owned by CueControl as well, for the backspin to hotcue
    m_hotcuePositions.clear();
    if (m_rampAllowed) {
        for (int i = 1; i <= kMaxNumberOfHotcues; ++i) {
            m_hotcuePositions.push_back(std::make_unique<PollingControlProxy>(getGroup(),
                    QStringLiteral("hotcue_%1_position").arg(i),
                    ControlFlag::AllowMissingOrInvalid));
        }
    }
}

void RateControl::requestRampReset() {
    m_resetRampRequested = true;
}
