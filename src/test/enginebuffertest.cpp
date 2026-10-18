// Tests for enginebuffer.cpp

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QString>
#include <QTest>
#include <QtDebug>
#include <cmath>

#include "control/controlobject.h"
#include "engine/controls/ratecontrol.h"
#include "engine/defs_keylock.h"
#include "mixer/basetrackplayer.h"
#include "preferences/usersettings.h"
#include "test/mixxxtest.h"
#include "test/mockedenginebackendtest.h"
#include "test/signalpathtest.h"

// In case any of the test in this file fail. You can use the audioplot.py tool
// in the tools folder to visually compare the results of the enginebuffer
// with the golden test data.

namespace {
const QString kAppGroup = QStringLiteral("[App]");
}

class EngineBufferTest : public MockedEngineBackendTest {};

class EngineBufferE2ETest : public SignalPathTest {};

TEST_F(EngineBufferTest, DisableKeylockResetsPitch) {
    // To prevent one-slider users from getting stuck on a key,
    // KeyunlockMode::ResetLockedKey resets the musical pitch.
    ControlObject::set(ConfigKey(m_sGroup1, "keylockMode"),
            static_cast<double>(KeylockMode::LockCurrentKey));
    ControlObject::set(ConfigKey(m_sGroup1, "keyunlockMode"),
            static_cast<double>(KeyunlockMode::ResetLockedKey));
    ControlObject::set(ConfigKey(m_sGroup1, "file_bpm"), 128.0);
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "pitch"), 0.5);
    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 0.0);
    // We require a buffer process to see that the keylock state has changed.
    ProcessBuffer();
    ASSERT_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch")));
}

TEST_F(EngineBufferTest, DisableKeylockKeepsPitch) {
    // Pitch must not change when unlocking with KeyunlockMode::KeepLockedKey.
    ControlObject::set(ConfigKey(m_sGroup1, "keylockMode"),
            static_cast<double>(KeylockMode::LockCurrentKey));
    ControlObject::set(ConfigKey(m_sGroup1, "keyunlockMode"),
            static_cast<double>(KeyunlockMode::KeepLockedKey));
    ControlObject::set(ConfigKey(m_sGroup1, "file_bpm"), 128.0);
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "pitch"), 0.5);
    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 0.0);
    // We require a buffer process to see that the keylock state has changed.
    ProcessBuffer();
    ASSERT_NEAR(0.5, ControlObject::get(ConfigKey(m_sGroup1, "pitch")), 1e-10);
}

TEST_F(EngineBufferTest, TrackLoadResetsPitch) {
    // When a new track is loaded, the pitch value should be reset.
    config()->setValue(ConfigKey("[Controls]", "SpeedAutoReset"),
            BaseTrackPlayer::TrackLoadReset::RESET_PITCH);
    ControlObject::set(ConfigKey(m_sGroup1, "file_bpm"), 128.0);
    ControlObject::set(ConfigKey(m_sGroup1, "pitch_adjust"), 0.5);
    ProcessBuffer();
    ASSERT_NEAR(0.5, ControlObject::get(ConfigKey(m_sGroup1, "pitch_adjust")), 1e-10);

    m_pMixerDeck1->loadFakeTrack(false, 0.0);
    ASSERT_NEAR(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch_adjust")), 1e-10);
}

TEST_F(EngineBufferTest, TrackLoadResetsPitch_LockCurrentKey) {
    // The pitch should be reset to 0 when a new track was loaded when
    // * rate is not 0
    // * keylock is ON
    // * keylock mode is LockCurrentKey,
    // * Reset Pitch on track load option is enabled
    //
    // First test case:
    // * change tempo with key unlocked -> pitch changes
    // * lock key
    // // * reset pitch -> is now 0 OPTIONAL
    // * load another track -> pitch should (still) be 0
    config()->setValue(ConfigKey("[Controls]", "SpeedAutoReset"),
            BaseTrackPlayer::TrackLoadReset::RESET_PITCH);
    ControlObject::set(ConfigKey(m_sGroup1, "keylockMode"),
            static_cast<double>(KeylockMode::LockCurrentKey));
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.5);
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "reset_key"), 1.0);
    ProcessBuffer();
    // Note that pitch_adjust is NOT reset to 0 with KeylockMode::LockCurrentKey
    ASSERT_DOUBLE_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch")));
    ProcessBuffer();

    m_pMixerDeck1->loadFakeTrack(false, 0.0);
    ProcessBuffer();

    ASSERT_DOUBLE_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch")));
}

TEST_F(EngineBufferTest, PitchRoundtrip) {
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "keylockMode"),
            static_cast<double>(KeylockMode::LockOriginalKey));
    ControlObject::set(ConfigKey(m_sGroup1, "keyunlockMode"),
            static_cast<double>(KeyunlockMode::ResetLockedKey));
    ProcessBuffer();
    // we are in kPakmOffsetScaleReseting mode
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.5);
    ProcessBuffer();
    // pitch_adjust must not change
    ASSERT_DOUBLE_EQ(0.0,
                     ControlObject::get(ConfigKey(m_sGroup1, "pitch_adjust")));

    ControlObject::set(ConfigKey(m_sGroup1, "pitch_adjust"), 0.5);
    ProcessBuffer();
    // rate must not change
    ASSERT_DOUBLE_EQ(0.5, ControlObject::get(ConfigKey(m_sGroup1, "rate")));

    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ProcessBuffer();
    // pitch is reset, as well as pitch_adjust
    // speed must not change, pitch and pitch_adjust must be 0
    ASSERT_DOUBLE_EQ(0.5, ControlObject::get(ConfigKey(m_sGroup1, "rate")));
    ASSERT_DOUBLE_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch")));
    ASSERT_DOUBLE_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch_adjust")));

    ControlObject::set(ConfigKey(m_sGroup1, "keylockMode"),
            static_cast<double>(KeylockMode::LockCurrentKey));
    ProcessBuffer();
    // rate must not change
    ASSERT_DOUBLE_EQ(0.5, ControlObject::get(ConfigKey(m_sGroup1, "rate")));
    // pitch must reflect the absolute pitch
    ASSERT_DOUBLE_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch")));

    ControlObject::set(ConfigKey(m_sGroup1, "keylockMode"),
            static_cast<double>(KeylockMode::LockOriginalKey));
    ProcessBuffer();
    // rate must not change
    ASSERT_DOUBLE_EQ(0.5, ControlObject::get(ConfigKey(m_sGroup1, "rate")));
    // pitch must be 0
    ASSERT_NEAR(0.0, ControlObject::get(ConfigKey(m_sGroup1, "pitch")), 1e-10);
}

#ifdef __RUBBERBAND__
TEST_F(EngineBufferTest, SlowRubberBand) {
    // At very slow speeds, RubberBand needs to reallocate buffers and since
    // this
    // is done in the engine thread it can be a major party-stopper.
    // Make sure slow speeds still use the linear scaler.
    ControlObject::set(ConfigKey(m_sGroup1, "pitch"), 2.8);

    // Hack to get a slow, non-scratching direct speed
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 0.0072);

    // With Soundtouch, it should switch the scaler as well
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::SoundTouch));
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);

    // Back to full speed
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 1);
    ProcessBuffer();

    // With Rubberband, and transport stopped it should be still keylock
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::RubberBandFaster));
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 0.0);
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleKeylock1, m_pChannel1->getEngineBuffer()->m_pScale);

    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 0.0072);

    // Playing at low rate, the vinyl scaler should be used
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);
}
#endif

TEST_F(EngineBufferTest, ScalerNoTransport) {
    // normally use the Vinyl scaler
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);

    // switch to keylock scaler
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleKeylock1, m_pChannel1->getEngineBuffer()->m_pScale);

    // Stop and disable keylock: do not change scaler
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 0.0);
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleKeylock1, m_pChannel1->getEngineBuffer()->m_pScale);

    // play: we need to use vinyl scaler
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);
}

TEST_F(EngineBufferTest, VinylScalerRampZero) {
    // scratch in play mode
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2_enable"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), 1.0);

    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);
    EXPECT_EQ(m_pMockScaleVinyl1->getProcessedTempo(), 1.0);

    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), 0.0);

    // we are in scratching mode so a zero rate has to be processed
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);
    EXPECT_EQ(m_pMockScaleVinyl1->getProcessedTempo(), 0.0);
}

TEST_F(EngineBufferTest, ReadFadeOut) {
    // Start playing
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);

    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);
    EXPECT_EQ(m_pMockScaleVinyl1->getProcessedTempo(), 1.0);

    // pause
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);

    // The scaler need to be processed with the old rate to
    // prepare the crossfade buffer
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);
    EXPECT_EQ(m_pMockScaleVinyl1->getProcessedTempo(), 1.0);
}

TEST_F(EngineBufferTest, ResetPitchAdjustUsesLinear) {
    // If the key was adjusted, but keylock is off, and then the key is
    // reset, then the engine should be using the linear scaler.

    // First, we should be using the keylock scaler here.
    ControlObject::set(ConfigKey(m_sGroup1, "pitch"), 1.2);
    // Remember that a rate of "0" is "regular playback speed".
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.05);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleKeylock1, m_pChannel1->getEngineBuffer()->m_pScale);

    // Reset pitch adjust, and we should be back to the linear scaler.
    ControlObject::set(ConfigKey(m_sGroup1, "pitch_adjust_set_default"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(m_pMockScaleVinyl1, m_pChannel1->getEngineBuffer()->m_pScale);
}

TEST_F(EngineBufferE2ETest, SoundTouchCrashTest) {
    // Soundtouch has a bug where a pitch value of zero causes an infinite loop
    // and crash.
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::SoundTouch));
    ControlObject::set(ConfigKey(m_sGroup1, "pitch"), 1.2);
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.05);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    // Start by playing with soundtouch enabled.
    ProcessBuffer();
    // Pause the buffer.  This causes the pitch to be set to 0.
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), -0.05);
    // Should not crash
    ProcessBuffer();
}

TEST_F(EngineBufferE2ETest, BasicProcessingTest) {
    // Confirm that playing ramps from silence, pausing ramps to silence,
    // and also just confirm that playing works as predicted.
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.05);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("BasicProcessingTestPlay"));
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("BasicProcessingTestPlaying"));
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("BasicProcessingTestPause"));
}

TEST_F(EngineBufferE2ETest, ScratchTest) {
    // Confirm that vinyl scratching smoothly transitions from one direction
    // to the other.
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2_enable"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), 1.1);
    m_pChannel1->getEngineBuffer()->queueNewPlaypos(
            mixxx::audio::FramePos(225), EngineBuffer::SEEK_EXACT);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), -1.1);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("ScratchTestMain"));
}

TEST_F(EngineBufferE2ETest, ScratchTestStart) {
    // Confirm that vinyl scratching smoothly transitions from zero speed
    // to an the other speed above the 0.07 threshold.
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2_enable"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), 1);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), 0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), 0.5);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("ScratchTestStart"));
}

TEST_F(EngineBufferE2ETest, ReverseTest) {
    // Confirm that pushing the reverse button smoothly transitions.
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("ReverseTest"));
}

// DISABLED: This test is too dependent on the sound touch library version.
TEST_F(EngineBufferE2ETest, DISABLED_SoundTouchToggleTest) {
   // Test various cases where SoundTouch toggles on and off.
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::SoundTouch));
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.5);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    // Test transition from vinyl to keylock
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("SoundTouchTest"));
    // Test transition from keylock to vinyl due to slow speed.
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 0.0072);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("SoundTouchTestSlow"));
    // Test transition back to keylock due to regular speed.
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("SoundTouchTestRegular"));
}

#ifdef __RUBBERBAND__
// DISABLED: This test is too dependent on the rubber band library version.
TEST_F(EngineBufferE2ETest, DISABLED_RubberbandToggleTest) {
   // Test various cases where Rubberband toggles on and off.
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::RubberBandFaster));
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.5);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    // Test transition from vinyl to keylock
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("RubberbandTest"));
    // Test transition from keylock to vinyl due to slow speed.
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 0.0072);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("RubberbandTestSlow"));
    // Test transition back to keylock due to regular speed.
    ControlObject::set(ConfigKey(m_sGroup1, "rateSearch"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("RubberbandTestRegular"));
}
#endif

// DISABLED: This test is too dependent on the sound touch library version.
// NOTE(uklotzde, 2018-01-10): We have also seen spurious failures on the
// Linux build server under high load. These failures might by caused by
// delayed asynchronous reads from CachingReader. The corresponding chunks
// will be filled with 0-samples by the engine buffer scaler.
TEST_F(EngineBufferE2ETest, DISABLED_KeylockReverseTest) {
    // Confirm that when toggling reverse while keylock is on, interpolation
    // is smooth.
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::SoundTouch));
    ControlObject::set(ConfigKey(m_sGroup1, "keylockMode"),
            static_cast<double>(KeylockMode::LockOriginalKey));
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.5);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "keylock"), 1.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("KeylockReverseTest"));
}

TEST_F(EngineBufferE2ETest, SeekTest) {
    // Confirm that seeking to a new position smoothly transitions.
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    m_pChannel1->getEngineBuffer()->queueNewPlaypos(
            mixxx::audio::FramePos(500), EngineBuffer::SEEK_EXACT);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("SeekTest"));
}

TEST_F(EngineBufferE2ETest, SoundTouchReverseTest) {
    // This test must not crash when changing to reverse while pitch is tweaked
    // Testing issue #8061
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::SoundTouch));
    ControlObject::set(ConfigKey(m_sGroup1, "pitch"), -1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 1.0);
    ProcessBuffer();
    // Note: we cannot compare a golden buffer here, because the result depends
    // on the uses library version
}

#ifdef __RUBBERBAND__
TEST_F(EngineBufferE2ETest, RubberbandReverseTest) {
    // This test must not crash when changing to reverse while pitch is tweaked
    // Testing issue #8061
    ControlObject::set(ConfigKey(kAppGroup, QStringLiteral("keylock_engine")),
            static_cast<double>(EngineBuffer::KeylockEngine::RubberBandFaster));
    ControlObject::set(ConfigKey(m_sGroup1, "pitch"), -1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 1.0);
    ProcessBuffer();
    // Note: we cannot compare a golden buffer here, because the result depends
    // on the uses library version
}
#endif

TEST_F(EngineBufferE2ETest, CueGotoAndStopTest) {
    // Be sure, that the Crossfade buffer is processed only once
    // Issue #8251
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "cue_gotoandstop"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("CueGotoAndStopTest"));
}

TEST_F(EngineBufferE2ETest, CueGotoAndPlayTest) {
    // Be sure, cue seek is not overwritten by quantization seek
    // Issue #8249
    ControlObject::set(ConfigKey(m_sGroup1, "quantize"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "cue_point"), 0.0);
    m_pChannel1->getEngineBuffer()->queueNewPlaypos(
            mixxx::audio::FramePos(500), EngineBuffer::SEEK_EXACT);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "cue_gotoandplay"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("CueGotoAndPlayTest"));
}

TEST_F(EngineBufferE2ETest, CueStartPlayTest) {
    // Be sure, cue seek is not overwritten by quantization seek
    // Issue #8252
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "start_play"), 1.0);
    ProcessBuffer();
    assertBufferMatchesReference(m_pEngineMixer->getMainBuffer(),
            QStringLiteral("StartPlayTest"));
}

TEST_F(EngineBufferE2ETest, CueGotoAndPlayDenon) {
    // Be sure, cue point is not moved
    // enable Denon mode issue #8254
    ControlObject::set(ConfigKey(m_sGroup1, "cue_mode"), 2.0); // CUE_MODE_DENON
    m_pChannel1->getEngineBuffer()->queueNewPlaypos(
            mixxx::audio::FramePos(500), EngineBuffer::SEEK_EXACT);
    ProcessBuffer();
    double cueBefore = ControlObject::get(ConfigKey(m_sGroup1, "cue_point"));
    ControlObject::set(ConfigKey(m_sGroup1, "cue_gotoandplay"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(cueBefore, ControlObject::get(ConfigKey(m_sGroup1, "cue_point")));
}

TEST_F(EngineBufferTest, RateTempTest) {
    RateControl::setTemporaryRateChangeCoarseAmount(4);
    RateControl::setTemporaryRateChangeFineAmount(2);

    ControlObject::set(ConfigKey(m_sGroup1, "rate_dir"), 1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(1.0, m_pChannel1->getEngineBuffer()->m_speed_old);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up"), 1);
    ProcessBuffer();
    EXPECT_EQ(1.04, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up"), 0);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up_small"), 1);
    ProcessBuffer();
    EXPECT_EQ(1.02, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up_small"), 0);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down"), 1);
    ProcessBuffer();
    EXPECT_EQ(0.96, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down"), 0);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down_small"), 1);
    ProcessBuffer();
    EXPECT_EQ(0.98, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down_small"), 0);

    ControlObject::set(ConfigKey(m_sGroup1, "rate_dir"), -1);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up"), 1);
    ProcessBuffer();
    EXPECT_EQ(1.04, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up"), 0);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up_small"), 1);
    ProcessBuffer();
    EXPECT_EQ(1.02, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_up_small"), 0);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down"), 1);
    ProcessBuffer();
    EXPECT_EQ(0.96, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down"), 0);

    ProcessBuffer();

    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down_small"), 1);
    ProcessBuffer();
    EXPECT_EQ(0.98, m_pChannel1->getEngineBuffer()->m_speed_old);
    ControlObject::set(ConfigKey(m_sGroup1, "rate_temp_down_small"), 0);
}


TEST_F(EngineBufferTest, RatePermTest) {
    RateControl::setPermanentRateChangeCoarseAmount(4);
    RateControl::setPermanentRateChangeFineAmount(2);

    ControlObject::set(ConfigKey(m_sGroup1, "rate_dir"), 1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(1.0, m_pChannel1->getEngineBuffer()->m_speed_old);

    ControlObject::set(ConfigKey(m_sGroup1, "rate_perm_up"), 1);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "rate_perm_up"), 0);
    EXPECT_EQ(1.04, m_pChannel1->getEngineBuffer()->m_speed_old);

    ControlObject::set(ConfigKey(m_sGroup1, "rate_perm_up_small"), 1);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "rate_perm_up_small"), 0);
    EXPECT_EQ(1.06, m_pChannel1->getEngineBuffer()->m_speed_old);
}

namespace {
// Must match kSpindownTime33 in ratecontrol.cpp
constexpr double kBrakeTime33 = 0.8;
} // namespace

class RateControlRampTest : public MockedEngineBackendTest {
  protected:
    void SetUp() override {
        MockedEngineBackendTest::SetUp();
        m_oldSampleRate = ControlObject::get(ConfigKey(kAppGroup, "samplerate"));
        ControlObject::set(ConfigKey(kAppGroup, "samplerate"), 44100);
        resetRampSettings();
    }

    void TearDown() override {
        // The ramp settings are static, don't leak them into other tests
        resetRampSettings();
        ControlObject::set(ConfigKey(kAppGroup, "samplerate"), m_oldSampleRate);
        MockedEngineBackendTest::TearDown();
    }

    static void resetRampSettings() {
        RateControl::setRampUpEnabled(false);
        RateControl::setRampDownEnabled(false);
        RateControl::setRampRpmMode(0);
        RateControl::setRampIncludePitch(true);
        RateControl::setRampDownMode(0);
        RateControl::setRateRampMode(RateControl::RampMode::Stepping);
        RateControl::setRampUpTimeFactor(1.0);
        RateControl::setRampDownTimeFactor(1.0);
        RateControl::setBackspinOnReverse(false);
        RateControl::setBackspinRevPerSec(1.0);
        RateControl::setBackspinToHotcue(false);
        RateControl::setBackspinPlayOn(true);
    }

    mixxx::audio::FramePos playPos() const {
        return m_pChannel1->getEngineBuffer()->getPlayPos();
    }

    // Sets hotcue 1 at the given play position
    void setHotcue1(double playposition) {
        ControlObject::set(ConfigKey(m_sGroup1, "playposition"), playposition);
        ProcessBuffer();
        ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_set"), 1.0);
        ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_set"), 0.0);
    }

    mixxx::audio::FramePos hotcue1() const {
        return mixxx::audio::FramePos::fromEngineSamplePosMaybeInvalid(
                ControlObject::get(ConfigKey(m_sGroup1, "hotcue_1_position")));
    }

    // Backspin, then returns the play position when full speed is reached
    mixxx::audio::FramePos backspinUntilFullSpeed() {
        const double fullSpeed = speed();
        tapReverse();
        auto position = playPos();
        bool rampedDown = false;
        for (int i = 0; i < 100000; ++i) {
            const auto before = playPos();
            ProcessBuffer();
            if (speed() < fullSpeed) {
                rampedDown = true;
            } else if (rampedDown) {
                position = before;
                break;
            }
        }
        return position;
    }

    // Press and release the reverse button
    void tapReverse() {
        ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 1.0);
        ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 0.0);
    }

    // Processes until the deck stops going backwards, returns the buffers
    int buffersUntilForwardOrStill() {
        int buffers = 0;
        do {
            ProcessBuffer();
            ++buffers;
        } while (speed() < 0.0 && buffers < 100000);
        return buffers;
    }

    double speed(const QString& group) const {
        if (group == m_sGroup2) {
            return m_pMixerDeck2->getEngineDeck()->getEngineBuffer()->getSpeed();
        }
        return m_pChannel1->getEngineBuffer()->getSpeed();
    }
    double speed() const {
        return speed(m_sGroup1);
    }

    double bufferSeconds() const {
        // kProcessBufferSize counts stereo samples
        return kProcessBufferSize / 2.0 /
                ControlObject::get(ConfigKey(kAppGroup, "samplerate"));
    }

    void processSeconds(double seconds) {
        const int buffers = static_cast<int>(std::ceil(seconds / bufferSeconds()));
        for (int i = 0; i < buffers; ++i) {
            ProcessBuffer();
        }
    }

    // Start deck 1 instantly (spin-up disabled) and keep the ramp settings
    void playAtFullSpeed() {
        const bool rampUp = false;
        RateControl::setRampUpEnabled(rampUp);
        ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
        ProcessBuffer();
        ASSERT_EQ(1.0, speed());
    }

    double m_oldSampleRate;
};

TEST_F(RateControlRampTest, DisabledRampChangesNothing) {
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(1.0, speed());
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ProcessBuffer();
    EXPECT_EQ(0.0, speed());
}

TEST_F(RateControlRampTest, SpinUpReachesFullSpeed) {
    RateControl::setRampUpEnabled(true);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    const double first = speed();
    EXPECT_GT(first, 0.0);
    EXPECT_LT(first, 0.1);
    processSeconds(1.0);
    EXPECT_GT(speed(), first);
    EXPECT_LT(speed(), 1.0);
    processSeconds(3.0);
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, BrakeTimeIsSampleRateIndependent) {
    for (const double sampleRate : {44100.0, 96000.0}) {
        ControlObject::set(ConfigKey(kAppGroup, "samplerate"), sampleRate);
        ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.0);
        playAtFullSpeed();
        RateControl::setRampDownEnabled(true);
        ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
        int buffers = 0;
        do {
            ProcessBuffer();
            ++buffers;
        } while (speed() > 0.0 && buffers < 100000);
        EXPECT_NEAR(kBrakeTime33, buffers * bufferSeconds(), 2 * bufferSeconds())
                << "sample rate " << sampleRate;
        RateControl::setRampDownEnabled(false);
    }
}

TEST_F(RateControlRampTest, PowerCutCoastsLongerThanBrake) {
    playAtFullSpeed();
    RateControl::setRampDownEnabled(true);
    RateControl::setRampDownMode(1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    processSeconds(kBrakeTime33);
    // The brake would be at standstill here, the coasting platter is not
    EXPECT_GT(speed(), 0.3);
    processSeconds(10.0);
    EXPECT_EQ(0.0, speed());
}

TEST_F(RateControlRampTest, PlayDuringCoastContinuesFromCurrentSpeed) {
    playAtFullSpeed();
    RateControl::setRampUpEnabled(true);
    RateControl::setRampDownEnabled(true);
    RateControl::setRampDownMode(1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    processSeconds(0.5);
    const double coasting = speed();
    ASSERT_GT(coasting, 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_GT(speed(), coasting);
    EXPECT_LT(speed(), 1.0);
}

TEST_F(RateControlRampTest, PauseWithSeekStopsInstantly) {
    playAtFullSpeed();
    RateControl::setRampDownEnabled(true);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.25);
    ProcessBuffer();
    EXPECT_EQ(0.0, speed());
}

TEST_F(RateControlRampTest, JumpCutsSpinUp) {
    RateControl::setRampUpEnabled(true);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    processSeconds(0.5);
    ASSERT_LT(speed(), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    ProcessBuffer();
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, CuePreviewStartsInstantly) {
    RateControl::setRampUpEnabled(true);
    ControlObject::set(ConfigKey(m_sGroup1, "cue_preview"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(1.0, speed());
    ControlObject::set(ConfigKey(m_sGroup1, "cue_preview"), 0.0);
}

TEST_F(RateControlRampTest, JogCancelsSpinDown) {
    playAtFullSpeed();
    RateControl::setRampDownEnabled(true);
    RateControl::setRampDownMode(1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    processSeconds(0.2);
    ASSERT_GT(speed(), 0.0);
    // Jog backwards on the paused deck: scrubs instead of coasting on
    ControlObject::set(ConfigKey(m_sGroup1, "jog"), -1.0);
    ProcessBuffer();
    EXPECT_LT(speed(), 0.0);
}

TEST_F(RateControlRampTest, TrackLoadResetsMotorState) {
    playAtFullSpeed();
    RateControl::setRampUpEnabled(true);
    // No paused buffer in between: without the reset the deck would still
    // be considered running and skip the spin-up
    m_pMixerDeck1->loadFakeTrack(false, 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    EXPECT_LT(speed(), 0.1);
}

TEST_F(RateControlRampTest, PassthroughDoesNotReplaySpinDown) {
    playAtFullSpeed();
    RateControl::setRampDownEnabled(true);
    // Passthrough is only accepted with a configured input
    ControlObject* pInputConfigured = ControlObject::getControl(
            ConfigKey(m_sGroup1, "input_configured"));
    ASSERT_NE(nullptr, pInputConfigured);
    pInputConfigured->forceSet(1.0);

    // While passthrough is active, EngineBuffer is not processed
    ControlObject::set(ConfigKey(m_sGroup1, "passthrough"), 1.0);
    ASSERT_EQ(1.0, ControlObject::get(ConfigKey(m_sGroup1, "passthrough")));
    ASSERT_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "play")));
    ControlObject::set(ConfigKey(m_sGroup1, "passthrough"), 0.0);
    ProcessBuffer();
    EXPECT_EQ(0.0, speed());

    pInputConfigured->forceSet(0.0);
}

TEST_F(RateControlRampTest, SpinUpWithoutSliderKeepsPitchBend) {
    RateControl::setRampUpEnabled(true);
    RateControl::setRampIncludePitch(false);
    RateControl::setTemporaryRateChangeCoarseAmount(4);
    for (const auto& group : {m_sGroup1, m_sGroup2}) {
        ControlObject::set(ConfigKey(group, "rate_dir"), 1.0);
        ControlObject::set(ConfigKey(group, "rateRange"), 0.08);
        ControlObject::set(ConfigKey(group, "rate"), 0.5); // +4 %
    }
    // Deck 2 additionally bends the pitch up by 4 %
    ControlObject::set(ConfigKey(m_sGroup2, "rate_temp_up"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup2, "play"), 1.0);
    processSeconds(0.5);
    ASSERT_LT(speed(m_sGroup1), 1.0);
    // The slider is excluded on both decks, the pitch bend is kept
    EXPECT_NEAR(1.04, speed(m_sGroup2) / speed(m_sGroup1), 1e-6);
    ControlObject::set(ConfigKey(m_sGroup2, "rate_temp_up"), 0.0);
}

TEST_F(RateControlRampTest, HotcueCutsSpinUp) {
    // Set hotcue 1 in the middle of the track, then go back to the start
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_set"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_set"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.0);
    ProcessBuffer();
    ASSERT_EQ(1.0, ControlObject::get(ConfigKey(m_sGroup1, "hotcue_1_status")) > 0);

    RateControl::setRampUpEnabled(true);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    processSeconds(0.5);
    ASSERT_LT(speed(), 1.0);

    ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_activate"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(1.0, speed());
    ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_activate"), 0.0);
}

TEST_F(RateControlRampTest, SpinUpTimeFollowsPreference) {
    RateControl::setRampUpEnabled(true);
    RateControl::setRampUpTimeFactor(0.5);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    int buffers = 0;
    do {
        ProcessBuffer();
        ++buffers;
    } while (speed() < 1.0 && buffers < 100000);
    EXPECT_NEAR(RateControl::spinUpTime(0, 0.5),
            buffers * bufferSeconds(),
            2 * bufferSeconds());
}

TEST_F(RateControlRampTest, BrakeTimeFollowsPreference) {
    playAtFullSpeed();
    RateControl::setRampDownEnabled(true);
    RateControl::setRampDownTimeFactor(2.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    int buffers = 0;
    do {
        ProcessBuffer();
        ++buffers;
    } while (speed() > 0.0 && buffers < 100000);
    EXPECT_NEAR(2.0 * kBrakeTime33, buffers * bufferSeconds(), 2 * bufferSeconds());
    EXPECT_NEAR(RateControl::spinDownTime(0, 0, 2.0), 2.0 * kBrakeTime33, 1e-9);
}

TEST_F(RateControlRampTest, PowerCutTimeFollowsPreference) {
    playAtFullSpeed();
    RateControl::setRampDownEnabled(true);
    RateControl::setRampDownMode(1);
    RateControl::setRampDownTimeFactor(0.5);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    int buffers = 0;
    do {
        ProcessBuffer();
        ++buffers;
    } while (speed() > 0.0 && buffers < 100000);
    EXPECT_NEAR(RateControl::spinDownTime(0, 1, 0.5),
            buffers * bufferSeconds(),
            2 * bufferSeconds());
}

TEST_F(RateControlRampTest, DisablingRampLetsRunningRampFinish) {
    playAtFullSpeed();
    RateControl::setRampDownEnabled(true);
    RateControl::setRampDownMode(1);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    processSeconds(0.2);
    const double coasting = speed();
    ASSERT_GT(coasting, 0.0);
    // Switching spin-down off does not cut the audible coast
    RateControl::setRampDownEnabled(false);
    ProcessBuffer();
    EXPECT_GT(speed(), 0.0);
    EXPECT_LT(speed(), coasting);
    // The next pause stops instantly
    processSeconds(10.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    ProcessBuffer();
    EXPECT_EQ(0.0, speed());
}

TEST_F(RateControlRampTest, ReverseButtonPlaysReverseByDefault) {
    playAtFullSpeed();
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(-1.0, speed());
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 0.0);
    ProcessBuffer();
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, BackspinMotorPullsPlatterForward) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    // Spin-up after the backspin
    RateControl::setRampUpEnabled(true);
    RateControl::setBackspinOnReverse(true);
    tapReverse();
    ProcessBuffer();
    // The hand flicks gradually, no instant direction change
    EXPECT_GT(speed(), 0.0);
    EXPECT_LT(speed(), 1.0);
    processSeconds(0.1);
    // 1 rev/s at 33 RPM, slightly slowed down by the motor already
    EXPECT_LT(speed(), -1.6);
    EXPECT_GT(speed(), -1.8);
    EXPECT_TRUE(m_pChannel1->getEngineBuffer()->getScratching());

    const auto expected = RateControl::backspinTimes(0, true, 0, 1.0, 1.0, 1.0);
    const int buffers = buffersUntilForwardOrStill() + 1 +
            static_cast<int>(std::ceil(0.1 / bufferSeconds()));
    EXPECT_NEAR(expected.backwardTime, buffers * bufferSeconds(), 3 * bufferSeconds());
    EXPECT_EQ(1.0, ControlObject::get(ConfigKey(m_sGroup1, "play")));

    // The motor spins the platter up to full speed again
    processSeconds(expected.recoverTime - expected.backwardTime + 0.1);
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, BackspinTimesMatchPreferenceLabel) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    // Spin-up after the backspin
    RateControl::setRampUpEnabled(true);
    RateControl::setBackspinOnReverse(true);
    RateControl::setRampUpTimeFactor(2.0);
    tapReverse();
    int buffers = 0;
    do {
        ProcessBuffer();
        ++buffers;
    } while (speed() < 1.0 && buffers < 100000);
    const auto expected = RateControl::backspinTimes(0, true, 0, 1.0, 2.0, 1.0);
    EXPECT_NEAR(expected.recoverTime, buffers * bufferSeconds(), 3 * bufferSeconds());
}

TEST_F(RateControlRampTest, StopDuringBackspinBrakes) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    tapReverse();
    processSeconds(0.1);
    ASSERT_LT(speed(), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 0.0);
    const int buffers = buffersUntilForwardOrStill();
    // Brake + friction stop the platter faster than the motor would
    const auto motorOn = RateControl::backspinTimes(0, true, 0, 1.0, 1.0, 1.0);
    EXPECT_LT(buffers * bufferSeconds() + 0.1, motorOn.backwardTime);
    EXPECT_EQ(0.0, speed());
    processSeconds(0.5);
    EXPECT_EQ(0.0, speed());
}

TEST_F(RateControlRampTest, BackspinOnStoppedDeckPowerCut) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    ProcessBuffer();
    RateControl::setBackspinOnReverse(true);
    RateControl::setRampDownMode(1);
    tapReverse();
    const int buffers = buffersUntilForwardOrStill();
    const auto expected = RateControl::backspinTimes(0, false, 1, 1.0, 1.0, 1.0);
    EXPECT_NEAR(expected.backwardTime, buffers * bufferSeconds(), 3 * bufferSeconds());
    EXPECT_EQ(0.0, speed());
    EXPECT_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "play")));
}

TEST_F(RateControlRampTest, CensorReversesInBackspinMode) {
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    ControlObject::set(ConfigKey(m_sGroup1, "reverseroll"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(-1.0, speed());
    ControlObject::set(ConfigKey(m_sGroup1, "reverseroll"), 0.0);
    ProcessBuffer();
    ProcessBuffer();
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, HeldReverseDoesNotReverseInBackspinMode) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinRevPerSec(0.25);
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 1.0);
    processSeconds(5.0);
    // The short backspin is over, the deck plays forward although reverse is held
    EXPECT_EQ(1.0, speed());
    ControlObject::set(ConfigKey(m_sGroup1, "reverse"), 0.0);
}

TEST_F(RateControlRampTest, ScratchCatchesBackspin) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    tapReverse();
    processSeconds(0.2);
    ASSERT_LT(speed(), 0.0);
    // Hand on the record
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2"), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2_enable"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(0.0, speed());
    // Released: plays on, no backspin left
    ControlObject::set(ConfigKey(m_sGroup1, "scratch2_enable"), 0.0);
    ProcessBuffer();
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, BackspinPowerIsLimited) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinRevPerSec(10.0);
    tapReverse();
    double minSpeed = 0.0;
    for (int i = 0; i < 200; ++i) {
        ProcessBuffer();
        minSpeed = std::min(minSpeed, speed());
    }
    const double maxSpeed = RateControl::maxBackspinRevPerSec() / (100.0 / 3.0 / 60.0);
    EXPECT_GE(minSpeed, -maxSpeed - 1e-9);
    EXPECT_LT(minSpeed, -maxSpeed + 0.2);
}

TEST_F(RateControlRampTest, BackspinLandsOnLastHotcue) {
    // The fake track is 5 s long: hotcue at 0.5 s, backspin from 4 s
    setHotcue1(0.1);
    ASSERT_TRUE(hotcue1().isValid());
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    playAtFullSpeed();
    // Spin-up after the backspin
    RateControl::setRampUpEnabled(true);
    processSeconds(0.2);
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    RateControl::setBackspinRevPerSec(RateControl::maxBackspinRevPerSec());
    const auto landing = backspinUntilFullSpeed();
    // Full speed exactly at the hotcue, within 1 ms
    EXPECT_NEAR(hotcue1().value(), landing.value(), 44.1)
            << "landed " << (landing - hotcue1()) / 44.1 << " ms off";
}

TEST_F(RateControlRampTest, BackspinLandsOnLastHotcueWithPitch) {
    ControlObject::set(ConfigKey(m_sGroup1, "rate_dir"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "rateRange"), 0.08);
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.5); // +4 %
    RateControl::setRampUpTimeFactor(1.5);
    RateControl::setRampRpmMode(1);
    setHotcue1(0.4);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    RateControl::setRampUpEnabled(false);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    processSeconds(0.2);
    // Spin-up after the backspin
    RateControl::setRampUpEnabled(true);
    ASSERT_NEAR(1.04, speed(), 1e-9);
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    RateControl::setBackspinRevPerSec(RateControl::maxBackspinRevPerSec());
    const auto landing = backspinUntilFullSpeed();
    EXPECT_NEAR(hotcue1().value(), landing.value(), 44.1)
            << "landed " << (landing - hotcue1()) / 44.1 << " ms off";
}

TEST_F(RateControlRampTest, HotcueOutOfReachUsesBackspinPower) {
    // 45 RPM reaches less than 4 s back within the needle limit
    // 1 rev/s at 45 RPM reaches less than 4 s back
    RateControl::setRampRpmMode(1);
    ASSERT_LT(RateControl::backspinHotcueReach(1, 1.0, 1.0), 4.0);
    setHotcue1(0.02);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.9);
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    tapReverse();
    processSeconds(0.1);
    // The configured 1 rev/s = -1.33x at 45 RPM, slightly slowed by the motor
    EXPECT_LT(speed(), -1.2);
    EXPECT_GT(speed(), -1.34);
}

TEST_F(RateControlRampTest, BackspinToHotcueCorrectsPitchChange) {
    ControlObject::set(ConfigKey(m_sGroup1, "rate_dir"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "rateRange"), 0.08);
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.5); // +4 %
    setHotcue1(0.3);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    RateControl::setRampUpEnabled(false);
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    processSeconds(0.2);
    // Spin-up after the backspin
    RateControl::setRampUpEnabled(true);
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    RateControl::setBackspinRevPerSec(RateControl::maxBackspinRevPerSec());
    tapReverse();
    processSeconds(0.3);
    ASSERT_LT(speed(), 0.0);
    // Pitch back to 0 % while spinning back: the spin-up covers less
    // distance than predicted, the standstill correction makes up for it
    ControlObject::set(ConfigKey(m_sGroup1, "rate"), 0.0);
    auto before = playPos();
    bool reachedStandstill = false;
    for (int i = 0; i < 100000; ++i) {
        before = playPos();
        ProcessBuffer();
        if (speed() >= 0.0) {
            reachedStandstill = true;
        }
        if (reachedStandstill && speed() >= 1.0) {
            break;
        }
    }
    EXPECT_NEAR(hotcue1().value(), before.value(), 44.1)
            << "landed " << (before - hotcue1()) / 44.1 << " ms off";
}

TEST_F(RateControlRampTest, JumpAfterHotcueLandingIsNotSwallowed) {
    setHotcue1(0.3);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    playAtFullSpeed();
    // Spin-up after the backspin
    RateControl::setRampUpEnabled(true);
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    RateControl::setBackspinRevPerSec(RateControl::maxBackspinRevPerSec());
    backspinUntilFullSpeed();
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.9);
    ProcessBuffer();
    EXPECT_NEAR(0.9, ControlObject::get(ConfigKey(m_sGroup1, "playposition")), 0.01);
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, HotcueReachGrowsWithBackspinPower) {
    // Weak flick: the spin-up ends after the play position, no hotcue
    // behind it can be reached
    EXPECT_LT(RateControl::backspinHotcueReach(0, 1.0, 0.25), 0.0);
    double previous = RateControl::backspinHotcueReach(0, 1.0, 0.5);
    for (const double revPerSec : {1.0, 1.5, 2.0}) {
        const double reach = RateControl::backspinHotcueReach(0, 1.0, revPerSec);
        EXPECT_GT(reach, previous) << revPerSec << " rev/s";
        previous = reach;
    }
    // Above the needle limit nothing changes
    EXPECT_DOUBLE_EQ(RateControl::backspinHotcueReach(0, 1.0, 2.0),
            RateControl::backspinHotcueReach(0, 1.0, 10.0));
}

TEST_F(RateControlRampTest, BackspinPowerLimitsHotcueLanding) {
    // 1 rev/s at 33 RPM reaches about 1.3 s back
    const double reach = RateControl::backspinHotcueReach(0, 1.0, 1.0);
    ASSERT_GT(reach, 1.0);
    ASSERT_LT(reach, 2.0);
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    RateControl::setBackspinRevPerSec(1.0);

    // Hotcue 0.95 s back: lands, with less than the selected power
    setHotcue1(0.65);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    playAtFullSpeed();
    // Spin-up after the backspin
    RateControl::setRampUpEnabled(true);
    processSeconds(0.2);
    const auto landing = backspinUntilFullSpeed();
    EXPECT_NEAR(hotcue1().value(), landing.value(), 44.1)
            << "landed " << (landing - hotcue1()) / 44.1 << " ms off";

    // Hotcue 3.2 s back: out of reach with 1 rev/s, normal backspin
    ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_clear"), 1.0);
    ControlObject::set(ConfigKey(m_sGroup1, "hotcue_1_clear"), 0.0);
    setHotcue1(0.2);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    ProcessBuffer();
    processSeconds(0.2);
    tapReverse();
    processSeconds(0.1);
    // The selected 1 rev/s = -1.8x at 33 RPM, slightly slowed by the motor
    EXPECT_LT(speed(), -1.6);
    EXPECT_GT(speed(), -1.8);
}

TEST_F(RateControlRampTest, BackspinWithoutSpinUpPlaysAtFullSpeedFromStandstill) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed(); // physical spin-up disabled
    // Only applies with spin-up
    RateControl::setRampUpTimeFactor(4.0);
    RateControl::setBackspinOnReverse(true);
    tapReverse();
    processSeconds(0.1);
    ASSERT_LT(speed(), 0.0);
    // The motor still pulls the platter back, at standstill playback
    // continues at full speed without a ramp
    const int buffers = buffersUntilForwardOrStill() +
            static_cast<int>(std::ceil(0.1 / bufferSeconds()));
    EXPECT_EQ(1.0, speed());
    const auto expected = RateControl::backspinTimes(0, true, 0, 1.0, 4.0, 1.0, false);
    EXPECT_NEAR(expected.backwardTime, buffers * bufferSeconds(), 3 * bufferSeconds());
    EXPECT_DOUBLE_EQ(expected.backwardTime, expected.recoverTime);
    EXPECT_DOUBLE_EQ(expected.backwardTime,
            RateControl::backspinTimes(0, true, 0, 1.0, 1.0, 1.0, false).backwardTime);
}

TEST_F(RateControlRampTest, BackspinToHotcueWithoutSpinUpStartsAtHotcue) {
    // No spin-up run-up in front of the hotcue: reaches further back
    EXPECT_GT(RateControl::backspinHotcueReach(0, 1.0, 1.0, false),
            RateControl::backspinHotcueReach(0, 1.0, 1.0, true));
    setHotcue1(0.35);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    playAtFullSpeed(); // physical spin-up disabled
    processSeconds(0.2);
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    RateControl::setBackspinRevPerSec(RateControl::maxBackspinRevPerSec());
    tapReverse();
    bool spinningBack = false;
    for (int i = 0; i < 100000; ++i) {
        ProcessBuffer();
        if (speed() < 0.0) {
            spinningBack = true;
        } else if (spinningBack && speed() == 1.0) {
            break;
        }
    }
    ASSERT_EQ(1.0, speed());
    // Position where the first full speed buffer started
    const double bufferFrames = bufferSeconds() * 44100.0;
    const double start = playPos().value() - bufferFrames;
    EXPECT_NEAR(hotcue1().value(), start, 44.1)
            << "started " << (start - hotcue1().value()) / 44.1 << " ms off";
}

TEST_F(RateControlRampTest, BackspinStaysPausedBrakes) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinPlayOn(false);
    tapReverse();
    ProcessBuffer();
    // Paused with the flick
    EXPECT_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "play")));
    processSeconds(0.1);
    ASSERT_LT(speed(), 0.0);
    const int buffers = buffersUntilForwardOrStill() + 1 +
            static_cast<int>(std::ceil(0.1 / bufferSeconds()));
    // A playing deck that is paused with the flick: no motor, the brake
    // stops the platter
    const auto expected = RateControl::backspinTimes(0, false, 0, 1.0, 1.0, 1.0, true, 1.0);
    EXPECT_NEAR(expected.backwardTime, buffers * bufferSeconds(), 3 * bufferSeconds());
    EXPECT_EQ(0.0, speed());
    processSeconds(1.0);
    EXPECT_EQ(0.0, speed());
    EXPECT_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "play")));
}

TEST_F(RateControlRampTest, PlayDuringStayPausedBackspinPlaysOn) {
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.5);
    playAtFullSpeed();
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinPlayOn(false);
    tapReverse();
    processSeconds(0.2);
    ASSERT_LT(speed(), 0.0);
    // Play during the backspin switches the motor on again
    ControlObject::set(ConfigKey(m_sGroup1, "play"), 1.0);
    processSeconds(5.0);
    EXPECT_EQ(1.0, speed());
}

TEST_F(RateControlRampTest, BackspinStaysPausedOnLastHotcue) {
    setHotcue1(0.4);
    ControlObject::set(ConfigKey(m_sGroup1, "playposition"), 0.8);
    playAtFullSpeed();
    processSeconds(0.2);
    RateControl::setBackspinOnReverse(true);
    RateControl::setBackspinToHotcue(true);
    RateControl::setBackspinPlayOn(false);
    RateControl::setBackspinRevPerSec(RateControl::maxBackspinRevPerSec());
    ASSERT_GT(RateControl::backspinHotcueReach(0, 1.0, 2.0, true, false, 0, 1.0), 2.5);
    tapReverse();
    processSeconds(10.0);
    // Parked on the hotcue, paused, ready to play
    EXPECT_EQ(0.0, speed());
    EXPECT_EQ(0.0, ControlObject::get(ConfigKey(m_sGroup1, "play")));
    EXPECT_NEAR(hotcue1().value(), playPos().value(), 44.1)
            << "stopped " << (playPos() - hotcue1()) / 44.1 << " ms off";
}
