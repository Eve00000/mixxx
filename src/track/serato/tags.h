#pragma once

#include <optional>

#include "audio/signalinfo.h"
#include "track/beatsimporter.h"
#include "track/cueinfoimporter.h"
#include "track/serato/beatgrid.h"
#include "track/serato/markers.h"
#include "track/serato/markers2.h"

namespace mixxx {

/// DTO for storing information from the SeratoMarkers_/2 tags used by the
/// Serato DJ Pro software.
class SeratoTags final {
  public:
    enum class ParserStatus {
        None = 0,
        Parsed = 1,
        Failed = 2,
    };

    SeratoTags()
            : m_seratoBeatGridParserStatus(ParserStatus::None),
              m_seratoMarkersParserStatus(ParserStatus::None),
              m_seratoMarkers2ParserStatus(ParserStatus::None) {
    }

    static double guessTimingOffsetMillis(
            const QString& filePath,
            const QString& fileType,
            const audio::SignalInfo& signalInfo);

    bool isEmpty() const {
        return m_seratoBeatGrid.isEmpty() && m_seratoMarkers.isEmpty() &&
                m_seratoMarkers2.isEmpty();
    }

    /// Return the cumulated parse status for all Serato tags. If no tags were parsed,
    /// this returns `ParserStatus::None`. If any tag failed to parse, this
    /// returns `ParserStatus::Failed`.
    ParserStatus status() const {
        if (m_seratoBeatGridParserStatus == ParserStatus::Failed ||
                m_seratoMarkersParserStatus == ParserStatus::Failed ||
                m_seratoMarkers2ParserStatus == ParserStatus::Failed) {
            return ParserStatus::Failed;
        }

        if (m_seratoBeatGridParserStatus == ParserStatus::None ||
                m_seratoMarkersParserStatus == ParserStatus::None ||
                m_seratoMarkers2ParserStatus == ParserStatus::None) {
            return ParserStatus::None;
        }

        return ParserStatus::Parsed;
    }

    bool parseBeatGrid(const QByteArray& data, taglib::FileType fileType) {
        bool success = SeratoBeatGrid::parse(&m_seratoBeatGrid, data, fileType);
        m_seratoBeatGridParserStatus = success ? ParserStatus::Parsed : ParserStatus::Failed;
        return success;
    }

    bool parseMarkers(const QByteArray& data, taglib::FileType fileType) {
        bool success = SeratoMarkers::parse(&m_seratoMarkers, data, fileType);
        m_seratoMarkersParserStatus = success ? ParserStatus::Parsed : ParserStatus::Failed;
        return success;
    }

    bool parseMarkers2(const QByteArray& data, taglib::FileType fileType) {
        bool success = SeratoMarkers2::parse(&m_seratoMarkers2, data, fileType);
        m_seratoMarkers2ParserStatus = success ? ParserStatus::Parsed : ParserStatus::Failed;
        return success;
    }

    QByteArray dumpBeatGrid(taglib::FileType fileType) const {
        return m_seratoBeatGrid.dump(fileType);
    }

    QByteArray dumpMarkers(taglib::FileType fileType) const {
        return m_seratoMarkers.dump(fileType);
    }

    QByteArray dumpMarkers2(taglib::FileType fileType) const {
        return m_seratoMarkers2.dump(fileType);
    }

    std::unique_ptr<CueInfoImporter> createCueInfoImporter() const;
    BeatsImporterPointer importBeats() const;

    QList<CueInfo> getCueInfos() const;
    void setCueInfos(const QList<CueInfo>& cueInfos, double timingOffset = 0);

    void setBeats(BeatsPointer pBeats,
            const audio::SignalInfo& signalInfo,
            const Duration& duration,
            double timingOffset) {
        m_seratoBeatGrid.setBeats(pBeats, signalInfo, duration, timingOffset);
    }

    /// Return the track color.
    ///
    /// If no Serato tags are present in the file or if tags contain no
    /// information about the track color (which should never happen when
    /// writing the tags with Serato DJ software), then the outer optional will
    /// be `nullopt`. This means that the track color should be left unchanged
    /// on metadata reimport.
    ///
    /// In all other cases, the track color has been specified in the Serato
    /// tags successfully, and the inner optional value can be used as new
    /// track color when (re-)importing metadata.
    std::optional<RgbColor::optional_t> getTrackColor() const;
    void setTrackColor(const RgbColor::optional_t& color);

    bool isBpmLocked() const;
    void setBpmLocked(bool bpmLocked);

  private:
    SeratoBeatGrid m_seratoBeatGrid;
    ParserStatus m_seratoBeatGridParserStatus;
    SeratoMarkers m_seratoMarkers;
    ParserStatus m_seratoMarkersParserStatus;
    SeratoMarkers2 m_seratoMarkers2;
    ParserStatus m_seratoMarkers2ParserStatus;
};

inline bool operator==(const SeratoTags& lhs, const SeratoTags& rhs) {
    // FIXME: Find a more efficient way to do this
    return (lhs.dumpBeatGrid(taglib::FileType::MPEG) ==
                    rhs.dumpBeatGrid(taglib::FileType::MPEG) &&
            lhs.dumpMarkers(taglib::FileType::MPEG) ==
                    rhs.dumpMarkers(taglib::FileType::MPEG) &&
            lhs.dumpMarkers2(taglib::FileType::MPEG) ==
                    rhs.dumpMarkers2(taglib::FileType::MPEG));
}

inline bool operator!=(const SeratoTags& lhs, const SeratoTags& rhs) {
    return !(lhs == rhs);
}

inline QDebug operator<<(QDebug dbg, const SeratoTags& arg) {
    Q_UNUSED(arg);

    // TODO
    return dbg << "SeratoTags";
}

} // namespace mixxx

Q_DECLARE_TYPEINFO(mixxx::SeratoTags, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(mixxx::SeratoTags)
