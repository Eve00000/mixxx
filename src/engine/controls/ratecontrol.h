#pragma once

#include <QObject>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "control/pollingcontrolproxy.h"
#include "engine/controls/enginecontrol.h"
#include "engine/sync/syncable.h"
#include "preferences/usersettings.h"

class BpmControl;
class Rotary;
class ControlTTRotary;
class ControlObject;
class ControlPotmeter;
class ControlPushButton;
class ControlProxy;
class PositionScratchController;

// RateControl is an EngineControl that is in charge of managing the rate of
// playback of a given channel of audio in the Mixxx engine. Using input from
// various controls, RateControl will calculate the current rate.
class RateControl : public EngineControl {
    Q_OBJECT
public:
  RateControl(const QString& group, UserSettingsPointer pConfig);

  // Enumerations which hold the state of the pitchbend buttons.
  // These enumerations can be used like a bitmask.
  enum class RampDirection {
      None,      // No buttons are held down
      Down,      // Down button is being held
      Up,        // Up button is being held
      DownSmall, // DownSmall button is being held
      UpSmall,   // UpSmall button is being held
  };

  enum class RampMode {
      Stepping = 0, // pitch takes a temporary step up/down a certain amount
      Linear = 1    // pitch moves up/down in a progressively linear fashion
  };

  void setBpmControl(BpmControl* bpmcontrol);

  // Returns the current engine rate.  "reportScratching" is used to tell
  // the caller that the user is currently scratching, and this is used to
  // disable keylock.
  double calculateSpeed(
          double baserate,
          double speed,
          bool paused,
          std::size_t samplesPerBuffer,
          bool* pReportScratching,
          bool* pReportReverse);

  // Set rate change when temp rate button is pressed
  static void setTemporaryRateChangeCoarseAmount(double v);
  static double getTemporaryRateChangeCoarseAmount();
  // Set rate change when temp rate small button is pressed
  static void setTemporaryRateChangeFineAmount(double v);
  static double getTemporaryRateChangeFineAmount();
  // Set rate change when perm rate button is pressed
  static void setPermanentRateChangeCoarseAmount(double v);
  static double getPermanentRateChangeCoarseAmount();
  // Set rate change when perm rate small button is pressed
  static void setPermanentRateChangeFineAmount(double v);
  static double getPermanentRateChangeFineAmount();
  // Set Rate Ramp Mode
  static void setRateRampMode(RampMode mode);
  static RampMode getRateRampMode();
  // Set Rate Ramp Sensitivity
  static void setRateRampSensitivity(int);
  static int getRateRampSensitivity();
  bool isReverseButtonPressed();
  // ReadAheadManager::getNextSamples() notifies us each time the play position
  // wrapped around during one buffer process (beatloop or track repeat) so
  // PositionScratchController can correctly interpret the sample position delta.
  void notifyWrapAround(mixxx::audio::FramePos triggerPos,
          mixxx::audio::FramePos targetPos);
  void notifySeek(mixxx::audio::FramePos position) override;
  void setEngineMixer(EngineMixer* pEngineMixer) override;
  void resetPositionScratchController();
  // Thread-safe: request a fresh motor state, applied in calculateSpeed()
  void requestRampReset();

  static void setRampUpEnabled(bool enabled);
  static void setRampDownEnabled(bool enabled);
  static void setRampRpmMode(int mode); // 0 = 33 RPM, 1 = 45 RPM
  static void setRampIncludePitch(bool includePitch);
  static void setRampDownMode(int mode); // 0 = brake, 1 = power cut
  // 1.0 = Technics SL-1210, clamped to 0.25 .. 4.0
  static void setRampUpTimeFactor(double factor);
  static void setRampDownTimeFactor(double factor);
  // Resulting ramp times in seconds, for the preferences
  static double spinUpTime(int rpmMode, double factor);
  static double spinDownTime(int rpmMode, int rampDownMode, double factor);

  // Reverse button mode: false = reverse playback, true = backspin
  static void setBackspinOnReverse(bool enabled);
  // Backward platter speed after the flick in revolutions per second,
  // clamped to maxBackspinRevPerSec()
  static void setBackspinRevPerSec(double revPerSec);
  // Fastest backspin that keeps the needle in the groove
  static double maxBackspinRevPerSec();
  struct BackspinTimes {
      double backwardTime; // seconds from the flick until the platter stands still
      double rewind;       // seconds of audio played backwards at nominal speed
      double recoverTime;  // motor on: seconds from the flick until full speed
  };
  // After a backspin of a playing deck: true = play on (spin-up or full
  // speed), false = stay paused. The deck is paused with the flick, the
  // platter brakes or coasts to a standstill (spin-down type).
  static void setBackspinPlayOn(bool playOn);
  // Playing deck: choose the backspin power, at most the one set with
  // setBackspinRevPerSec(), so that the spin-up reaches full speed exactly
  // at the last hotcue behind the play position. Staying paused: the
  // platter stops exactly at the hotcue.
  static void setBackspinToHotcue(bool enabled);
  // How many seconds back a hotcue can be at most for that, at nominal speed.
  // Zero or negative: the spin-up ends after the play position even with
  // revPerSec, no hotcue behind it can be reached.
  // spinUp = false: physical spin-up disabled, playback continues at full
  // speed from the standstill, upFactor is ignored.
  // playOn = false: stay paused, the platter brakes or coasts
  // (rampDownMode, downFactor) to a standstill at the hotcue.
  static double backspinHotcueReach(int rpmMode,
          double upFactor,
          double revPerSec,
          bool spinUp = true,
          bool playOn = true,
          int rampDownMode = 0,
          double downFactor = 1.0);
  // Backspin of a deck at nominal speed (motor on) or at standstill
  // (motor off), for the preferences. spinUp as above. startSpeed overrides
  // the speed at the flick, e.g. 1.0 for a playing deck that stays paused.
  static BackspinTimes backspinTimes(int rpmMode,
          bool motorOn,
          int rampDownMode,
          double revPerSec,
          double upFactor,
          double downFactor,
          bool spinUp = true,
          std::optional<double> startSpeed = std::nullopt);

public slots:
  void slotRateRangeChanged(double);
  void slotRateSliderChanged(double);
  void slotRateRatioChanged(double);
  void slotReverseRollActivate(double);
  void slotReverseChanged(double);
  void slotControlRatePermDown(double);
  void slotControlRatePermDownSmall(double);
  void slotControlRatePermUp(double);
  void slotControlRatePermUpSmall(double);
  void slotControlFastForward(double);
  void slotControlFastBack(double);

private:
  void processTempRate(const size_t bufferSamples);
  double getJogFactor() const;
  double getWheelFactor() const;
  SyncMode getSyncMode() const;

  // Set rate change of the temporary pitch rate
  void setRateTemp(double v);
  // Add a value to the temporary pitch rate
  void addRateTemp(double v);
  // Subtract a value from the temporary pitch rate
  void subRateTemp(double v);
  // Reset the temporary pitch rate
  void resetRateTemp(void);
  // Get the 'Raw' Temp Rate
  double getTempRate(void);

  // For Sync Lock
  BpmControl* m_pBpmControl;

  PollingControlProxy m_pSampleRate;
  std::unique_ptr<ControlObject> m_pRateRatio;
  std::unique_ptr<ControlObject> m_pRateDir;
  std::unique_ptr<ControlObject> m_pRateRange;
  std::unique_ptr<ControlPotmeter> m_pRateSlider;
  std::unique_ptr<ControlPotmeter> m_pRateSearch;

  std::unique_ptr<ControlPushButton> m_pButtonRateTempDown;
  std::unique_ptr<ControlPushButton> m_pButtonRateTempDownSmall;
  std::unique_ptr<ControlPushButton> m_pButtonRateTempUp;
  std::unique_ptr<ControlPushButton> m_pButtonRateTempUpSmall;

  std::unique_ptr<ControlPushButton> m_pButtonRatePermDown;
  std::unique_ptr<ControlPushButton> m_pButtonRatePermDownSmall;
  std::unique_ptr<ControlPushButton> m_pButtonRatePermUp;
  std::unique_ptr<ControlPushButton> m_pButtonRatePermUpSmall;

  std::unique_ptr<ControlPushButton> m_pReverseButton;
  std::unique_ptr<ControlPushButton> m_pReverseRollButton;
  std::unique_ptr<ControlObject> m_pForwardButton;
  std::unique_ptr<ControlObject> m_pBackButton;

  std::unique_ptr<ControlTTRotary> m_pWheel;
  std::unique_ptr<ControlObject> m_pScratch2;
  std::unique_ptr<ControlPushButton> m_pScratch2Enable;
  std::unique_ptr<ControlObject> m_pScratch2Scratching;

  std::unique_ptr<PositionScratchController> m_pScratchController;

  std::unique_ptr<ControlObject> m_pJog;
  std::unique_ptr<Rotary> m_pJogFilter;

  ControlObject* m_pVCEnabled;
  ControlObject* m_pVCScratching;
  ControlObject* m_pVCMode;

  PollingControlProxy m_syncMode;
  PollingControlProxy m_slipEnabled;

  // Values used when temp and perm rate buttons are pressed
  static ControlValueAtomic<double> m_dTemporaryRateChangeCoarse;
  static ControlValueAtomic<double> m_dTemporaryRateChangeFine;
  static ControlValueAtomic<double> m_dPermanentRateChangeCoarse;
  static ControlValueAtomic<double> m_dPermanentRateChangeFine;

  int m_wrapAroundCount;
  mixxx::audio::FramePos m_jumpPos;
  mixxx::audio::FramePos m_targetPos;

  // This is true if we've already started to ramp the rate
  bool m_bTempStarted;
  // Set the Temporary Rate Change Mode
  static RampMode m_eRateRampMode;
  // The Rate Temp Sensitivity, the higher it is the slower it gets
  static int m_iRateRampSensitivity;
  // Factor applied to the deprecated "wheel" rate value.
  static const double kWheelMultiplier;
  // Factor applied to jogwheels when the track is paused to speed up seeking.
  static const double kPausedJogMultiplier;
  // Temporary pitchrate, added to the permanent rate for calculateRate
  double m_tempRateRatio;
  // Speed for temporary rate change
  double m_dRateTempRampChange;

  // Physical spin-up / spin-down
  std::unique_ptr<PollingControlProxy> m_pPlayLatched;
  double m_platterSpeed;   // 0.0 = standstill, 1.0 = target speed reached
  double m_lastTargetRate; // speed the platter was ramping to / running at
  bool m_spinningDown;
  bool m_seekedSinceLastBuffer;
  bool m_prevPaused;
  bool m_latchedPlay;
  static std::atomic<bool> m_rampUpEnabled;
  static std::atomic<bool> m_rampDownEnabled;
  static std::atomic<int> m_rampMode; // 0 = 33 RPM, 1 = 45 RPM
  static std::atomic<bool> m_rampIncludePitch;
  static std::atomic<int> m_rampDownMode; // 0 = brake, 1 = power cut
  // Set on track change (worker thread), applied in calculateSpeed()
  std::atomic<bool> m_resetRampRequested;
  static std::atomic<double> m_rampUpTimeFactor;
  static std::atomic<double> m_rampDownTimeFactor;
  bool m_rampAllowed; // only main decks, not samplers / preview deck

  // Backspin on the reverse button
  bool backspinOnReverse() const;
  void startBackspin(bool paused, double rate, double spinUpTarget, double dt);
  mixxx::audio::FramePos lastHotcueBefore(mixxx::audio::FramePos position) const;
  struct BackspinState {
      double speed = 0.0;      // 1.0 = nominal 33/45 RPM forward
      double target = 0.0;     // speed after the flick, negative
      double flickAccel = 0.0; // speed units per second while the hand flicks
      bool flicking = false;
      // Backspin to hotcue: full speed is reached here (play on), or the
      // platter stops here (stay paused)
      mixxx::audio::FramePos landingPosition;
      // The landing was planned with the motor on (play on)
      bool landingMotorOn = true;
      // Stay paused: the deck was paused with the flick, until EngineBuffer
      // reports it paused
      bool stopping = false;
  };
  BackspinState m_backspin;
  bool m_backspinActive;
  // Set by the reverse button (GUI / controller thread)
  std::atomic<bool> m_backspinRequested;
  std::atomic<bool> m_reverseRollActive;
  // Read by LoopingControl from other threads
  std::atomic<bool> m_backspinReversing;
  // The correction seek at standstill must not cut the spin-up
  bool m_landingSeekPending;
  // Hotcue positions (owned by CueControl) for the backspin to hotcue
  std::vector<std::unique_ptr<PollingControlProxy>> m_hotcuePositions;
  static std::atomic<bool> m_backspinOnReverse;
  static std::atomic<double> m_backspinRevPerSec;
  static std::atomic<bool> m_backspinToHotcue;
  static std::atomic<bool> m_backspinPlayOn;
  // Stay paused after the backspin: pauses the deck with the flick
  std::unique_ptr<PollingControlProxy> m_pPlay;
};
