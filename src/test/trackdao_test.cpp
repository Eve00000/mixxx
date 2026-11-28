#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QSqlQuery>
#include <tuple>

#include "test/librarytest.h"
#include "track/beats.h"
#include "track/globaltrackcache.h"
#include "track/track.h"

using ::testing::UnorderedElementsAre;

class TrackDAOTest : public LibraryTest {
};


TEST_F(TrackDAOTest, detectMovedTracks) {
    TrackDAO& trackDAO = internalCollection()->getTrackDAO();

    QString filename = QStringLiteral("file.mp3");

    mixxx::FileInfo oldFile(QDir(QDir::tempPath() + QStringLiteral("/old/dir1")), filename);
    mixxx::FileInfo newFile(QDir(QDir::tempPath() + QStringLiteral("/new/dir1")), filename);
    mixxx::FileInfo otherFile(QDir(QDir::tempPath() + QStringLiteral("/new")), filename);

    TrackPointer pOldTrack = Track::newTemporary(mixxx::FileAccess(oldFile));
    TrackPointer pNewTrack = Track::newTemporary(mixxx::FileAccess(newFile));
    TrackPointer pOtherTrack = Track::newTemporary(mixxx::FileAccess(otherFile));

    // Arbitrary duration
    pOldTrack->setDuration(135);
    pNewTrack->setDuration(135.7);
    pOtherTrack->setDuration(135.7);

    TrackId oldId = internalCollection()->addTrack(pOldTrack, false);
    TrackId newId = internalCollection()->addTrack(pNewTrack, false);
    internalCollection()->addTrack(pOtherTrack, false);

    // Mark as missing
    QSqlQuery query(dbConnection());
    query.prepare("UPDATE track_locations SET fs_deleted=1 WHERE location=:location");
    query.bindValue(":location", oldFile.location());
    query.exec();

    QList<RelocatedTrack> relocatedTracks;
    QStringList addedTracks(newFile.location());
    bool cancel = false;
    trackDAO.detectMovedTracks(&relocatedTracks, addedTracks, &cancel);

    QSet<TrackId> updatedTrackIds;
    QSet<TrackId> removedTrackIds;
    for (const auto& relocatedTrack : std::as_const(relocatedTracks)) {
        updatedTrackIds.insert(relocatedTrack.updatedTrackRef().getId());
        removedTrackIds.insert(relocatedTrack.deletedTrackId());
    }

    EXPECT_THAT(updatedTrackIds, UnorderedElementsAre(oldId));
    EXPECT_THAT(removedTrackIds, UnorderedElementsAre(newId));

    QSet<QString> trackLocations = trackDAO.getAllTrackLocations();
    EXPECT_THAT(trackLocations, UnorderedElementsAre(newFile.location(), otherFile.location()));
}

// Regression test for the bug where a BPM-locked track without a beatgrid
// loses its lock when reloaded from the database.
// https://github.com/mixxxdj/mixxx/issues/15196
TEST_F(TrackDAOTest, bpmLockPreservedForTrackWithoutBeats) {
    const mixxx::FileInfo fileInfo(
            QDir(QDir::tempPath()), QStringLiteral("bpmlocked-no-beats.mp3"));
    TrackPointer pTrack = Track::newTemporary(mixxx::FileAccess(fileInfo));
    pTrack->setDuration(135);
    // Lock the BPM although the track has no beatgrid at all.
    pTrack->setBpmLocked(true);
    ASSERT_FALSE(pTrack->getBeats());
    ASSERT_TRUE(pTrack->isBpmLocked());

    const TrackId trackId = internalCollection()->addTrack(pTrack, false);
    ASSERT_TRUE(trackId.isValid());

    // Dropping the last reference evicts the track from the cache
    // synchronously (eviction runs as a direct call on this thread), so the
    // lookup below reloads it from the database instead of returning the
    // cached in-memory object whose lock flag was never lost.
    pTrack.reset();
    ASSERT_TRUE(GlobalTrackCacheLocker().isEmpty());

    const TrackPointer pReloaded = internalCollection()->getTrackById(trackId);
    ASSERT_TRUE(pReloaded);
    EXPECT_FALSE(pReloaded->getBeats());
    EXPECT_TRUE(pReloaded->isBpmLocked());
}

TEST_F(TrackDAOTest, markTrackLocationsAsVerifiedRecoversPresentFilesOnly) {
    // Regression cover for both directions of mixxxdj/mixxx#13533:
    //   1. A track erroneously flagged fs_deleted=1 must be revived when the
    //      file is still present in an unchanged-hash directory (the original
    //      bug).
    //   2. A track legitimately flagged fs_deleted=1 because the file was
    //      removed before the saved directory hash was last updated must NOT
    //      be revived on a subsequent unchanged-hash rescan.
    //
    // markTrackLocationsAsVerified is the cleanup-phase entry point that the
    // scanner uses for every verified location, including those collected
    // from unchanged-hash directories. It should clear fs_deleted /
    // needs_verification for exactly the locations passed in, and leave
    // unrelated rows untouched.
    TrackDAO& trackDAO = internalCollection()->getTrackDAO();

    QDir dir(QDir::tempPath() + QStringLiteral("/verified_dir"));
    mixxx::FileInfo presentFile(dir, QStringLiteral("present.mp3"));
    mixxx::FileInfo deletedFile(dir, QStringLiteral("deleted.mp3"));

    TrackPointer pPresent = Track::newTemporary(mixxx::FileAccess(presentFile));
    TrackPointer pDeleted = Track::newTemporary(mixxx::FileAccess(deletedFile));
    pPresent->setDuration(180);
    pDeleted->setDuration(180);

    TrackId presentId = internalCollection()->addTrack(pPresent, false);
    TrackId deletedId = internalCollection()->addTrack(pDeleted, false);
    ASSERT_TRUE(presentId.isValid());
    ASSERT_TRUE(deletedId.isValid());

    // Both rows simulate the post-`invalidateTrackLocationsInLibrary` state at
    // the start of a scan. The "deleted" row additionally carries the
    // fs_deleted=1 that the previous scan's verifyRemainingTracks set when
    // the file disappeared from disk between scans.
    QSqlQuery setQuery(dbConnection());
    setQuery.prepare(
            "UPDATE track_locations "
            "SET fs_deleted=:fs_deleted, needs_verification=1 "
            "WHERE location=:location");
    setQuery.bindValue(":fs_deleted", 1);
    setQuery.bindValue(":location", presentFile.location());
    ASSERT_TRUE(setQuery.exec());
    setQuery.bindValue(":fs_deleted", 1);
    setQuery.bindValue(":location", deletedFile.location());
    ASSERT_TRUE(setQuery.exec());

    // The recursive scanner only feeds locations whose file is currently
    // present in the directory into markTrackLocationsAsVerified. Simulate
    // that for a delete-then-rescan: only `presentFile` is in the list.
    trackDAO.markTrackLocationsAsVerified(QStringList{presentFile.location()});

    QSqlQuery readQuery(dbConnection());
    readQuery.prepare(
            "SELECT fs_deleted, needs_verification FROM track_locations "
            "WHERE location=:location");

    readQuery.bindValue(":location", presentFile.location());
    ASSERT_TRUE(readQuery.exec());
    ASSERT_TRUE(readQuery.next());
    EXPECT_EQ(0, readQuery.value(0).toInt())
            << "fs_deleted should be cleared for a still-present file";
    EXPECT_EQ(0, readQuery.value(1).toInt())
            << "needs_verification should be cleared for a still-present file";

    readQuery.bindValue(":location", deletedFile.location());
    ASSERT_TRUE(readQuery.exec());
    ASSERT_TRUE(readQuery.next());
    EXPECT_EQ(1, readQuery.value(0).toInt())
            << "fs_deleted must be preserved for a file no longer in the directory";
    EXPECT_EQ(1, readQuery.value(1).toInt())
            << "needs_verification must remain set so verifyRemainingTracks can confirm deletion";
}

namespace {
TrackPointer newCmrtTestTrack(const QString& fileName, mixxx::Bpm bpm) {
    TrackPointer pTrack = Track::newTemporary(
            mixxx::FileAccess(mixxx::FileInfo(QDir(QDir::tempPath()), fileName)));
    pTrack->setAudioProperties(
            mixxx::audio::ChannelCount(2),
            mixxx::audio::SampleRate(44100),
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(180));
    pTrack->trySetBeats(mixxx::Beats::fromConstTempo(
            pTrack->getSampleRate(), mixxx::audio::kStartFramePos, bpm));
    return pTrack;
}

} // namespace

// While "Use CMRT Data" is active, a member track's beats, BPM and cues are a
// shifted copy of its canonical track's data. Saving the member in that state
// (e.g. after changing its rating) must not persist that copy as the member's
// own data, and unchecking the overlay must restore the member's own beats
// and cues -- even if the member is BPM-locked.
TEST_F(TrackDAOTest, cmrtOverlayIsNotSavedAsOwnData) {
    TrackDAO& trackDAO = internalCollection()->getTrackDAO();
    TrackPointer pMember = newCmrtTestTrack(
            QStringLiteral("cmrt-member.mp3"), mixxx::Bpm(120));
    TrackPointer pCanonical = newCmrtTestTrack(
            QStringLiteral("cmrt-canonical.flac"), mixxx::Bpm(128));
    const TrackId memberId = internalCollection()->addTrack(pMember, false);
    const TrackId canonicalId = internalCollection()->addTrack(pCanonical, false);
    ASSERT_TRUE(memberId.isValid());
    ASSERT_TRUE(canonicalId.isValid());
    pMember->createAndAddCue(mixxx::CueType::HotCue,
            0,
            mixxx::audio::FramePos(44100),
            mixxx::audio::kInvalidFramePos);
    pMember->setBpmLocked(true);
    ASSERT_TRUE(trackDAO.saveTrack(pMember.get()));
    pCanonical->createAndAddCue(mixxx::CueType::HotCue,
            1,
            mixxx::audio::FramePos(88200),
            mixxx::audio::kInvalidFramePos);
    pCanonical->createAndAddCue(mixxx::CueType::HotCue,
            2,
            mixxx::audio::FramePos(132300),
            mixxx::audio::kInvalidFramePos);
    ASSERT_TRUE(trackDAO.saveTrack(pCanonical.get()));
    const auto storedMemberValues = [&]() {
        QSqlQuery query(dbConnection());
        query.prepare(QStringLiteral(
                "SELECT bpm, rating, "
                "(SELECT COUNT(*) FROM cues WHERE track_id=:id1 AND hotcue=0), "
                "(SELECT COUNT(*) FROM cues WHERE track_id=:id2) "
                "FROM library WHERE id=:id3"));
        query.bindValue(":id1", memberId.toVariant());
        query.bindValue(":id2", memberId.toVariant());
        query.bindValue(":id3", memberId.toVariant());
        EXPECT_TRUE(query.exec());
        EXPECT_TRUE(query.next());
        return std::make_tuple(query.value(0).toDouble(),
                query.value(1).toInt(),
                query.value(2).toInt(),
                query.value(3).toInt());
    };
    ASSERT_EQ(std::make_tuple(120.0, 0, 1, 1), storedMemberValues());
    pMember->applyCmrtOverlay(pCanonical, 0.5, pMember->getSampleRate());
    ASSERT_TRUE(pMember->hasCmrtOverlayActive());
    EXPECT_DOUBLE_EQ(128.0, pMember->getBpm());
    EXPECT_EQ(2, pMember->getCuePoints().size());
    // Unrelated edit while the overlay is active
    pMember->setRating(3);
    ASSERT_TRUE(pMember->isDirty());
    ASSERT_TRUE(trackDAO.saveTrack(pMember.get()));
    // The rating is saved, the member's own BPM and cues are untouched
    EXPECT_EQ(std::make_tuple(120.0, 3, 1, 1), storedMemberValues());
    trackDAO.reloadOwnCuesAndBeats(pMember);
    EXPECT_FALSE(pMember->hasCmrtOverlayActive());
    EXPECT_DOUBLE_EQ(120.0, pMember->getBpm());
    EXPECT_TRUE(pMember->isBpmLocked());
    const QList<CuePointer> cues = pMember->getCuePoints();
    ASSERT_EQ(1, cues.size());
    EXPECT_EQ(0, cues.first()->getHotCue());
    EXPECT_EQ(mixxx::audio::FramePos(44100), cues.first()->getPosition());
    EXPECT_EQ(std::make_tuple(120.0, 3, 1, 1), storedMemberValues());
    // The canonical track keeps its own data
    EXPECT_DOUBLE_EQ(128.0, pCanonical->getBpm());
    EXPECT_EQ(2, pCanonical->getCuePoints().size());
}
