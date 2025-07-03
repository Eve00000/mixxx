#pragma once

#include <QList>
#include <QSet>

#include "library/trackset/searchcrate/searchcrateid.h"
#include "track/trackid.h"
#include "util/db/fwdsqlqueryselectresult.h"
#include "util/db/sqlstorage.h"
#include "util/db/sqlsubselectmode.h"

class SearchCrate;
class SearchCrateSummary;

class SearchCrateQueryFields {
  public:
    SearchCrateQueryFields() {
    }
    explicit SearchCrateQueryFields(const FwdSqlQuery& query);
    virtual ~SearchCrateQueryFields() = default;

    SearchCrateId getId(const FwdSqlQuery& query) const {
        return SearchCrateId(query.fieldValue(m_iId));
    }
    QString getName(const FwdSqlQuery& query) const {
        return query.fieldValue(m_iName).toString();
    }
    bool isLocked(const FwdSqlQuery& query) const {
        return query.fieldValueBoolean(m_iLocked);
    }
    bool isAutoDjSource(const FwdSqlQuery& query) const {
        return query.fieldValueBoolean(m_iAutoDjSource);
    }

    void populateFromQuery(
            const FwdSqlQuery& query,
            SearchCrate* pSearchCrate) const;

  private:
    DbFieldIndex m_iId;
    DbFieldIndex m_iName;
    DbFieldIndex m_iLocked;
    DbFieldIndex m_iAutoDjSource;
};

class SearchCrateSelectResult : public FwdSqlQuerySelectResult {
  public:
    SearchCrateSelectResult(SearchCrateSelectResult&& other)
            : FwdSqlQuerySelectResult(std::move(other)),
              m_queryFields(std::move(other.m_queryFields)) {
    }
    ~SearchCrateSelectResult() override = default;

    bool populateNext(SearchCrate* pSearchCrate) {
        if (next()) {
            m_queryFields.populateFromQuery(query(), pSearchCrate);
            return true;
        } else {
            return false;
        }
    }

  private:
    friend class SearchCrateStorage;
    SearchCrateSelectResult() = default;
    explicit SearchCrateSelectResult(FwdSqlQuery&& query)
            : FwdSqlQuerySelectResult(std::move(query)),
              m_queryFields(FwdSqlQuerySelectResult::query()) {
    }

    SearchCrateQueryFields m_queryFields;
};

class SearchCrateSummaryQueryFields : public SearchCrateQueryFields {
  public:
    SearchCrateSummaryQueryFields() = default;
    explicit SearchCrateSummaryQueryFields(const FwdSqlQuery& query);
    ~SearchCrateSummaryQueryFields() override = default;

    uint getTrackCount(const FwdSqlQuery& query) const {
        QVariant varTrackCount = query.fieldValue(m_iTrackCount);
        if (varTrackCount.isNull()) {
            return 0; // searchCrate is empty
        } else {
            return varTrackCount.toUInt();
        }
    }
    double getTrackDuration(const FwdSqlQuery& query) const {
        QVariant varTrackDuration = query.fieldValue(m_iTrackDuration);
        if (varTrackDuration.isNull()) {
            return 0.0; // searchCrate is empty
        } else {
            return varTrackDuration.toDouble();
        }
    }

    void populateFromQuery(
            const FwdSqlQuery& query,
            SearchCrateSummary* pSearchCrateSummary) const;

  private:
    DbFieldIndex m_iTrackCount;
    DbFieldIndex m_iTrackDuration;
};

class SearchCrateSummarySelectResult : public FwdSqlQuerySelectResult {
  public:
    SearchCrateSummarySelectResult(SearchCrateSummarySelectResult&& other)
            : FwdSqlQuerySelectResult(std::move(other)),
              m_queryFields(std::move(other.m_queryFields)) {
    }
    ~SearchCrateSummarySelectResult() override = default;

    bool populateNext(SearchCrateSummary* pSearchCrateSummary) {
        if (next()) {
            m_queryFields.populateFromQuery(query(), pSearchCrateSummary);
            return true;
        } else {
            return false;
        }
    }

  private:
    friend class SearchCrateStorage;
    SearchCrateSummarySelectResult() = default;
    explicit SearchCrateSummarySelectResult(FwdSqlQuery&& query)
            : FwdSqlQuerySelectResult(std::move(query)),
              m_queryFields(FwdSqlQuerySelectResult::query()) {
    }

    SearchCrateSummaryQueryFields m_queryFields;
};

class SearchCrateTrackQueryFields {
  public:
    SearchCrateTrackQueryFields() = default;
    explicit SearchCrateTrackQueryFields(const FwdSqlQuery& query);
    virtual ~SearchCrateTrackQueryFields() = default;

    SearchCrateId searchCrateId(const FwdSqlQuery& query) const {
        return SearchCrateId(query.fieldValue(m_iSearchCrateId));
    }
    TrackId trackId(const FwdSqlQuery& query) const {
        return TrackId(query.fieldValue(m_iTrackId));
    }

  private:
    DbFieldIndex m_iSearchCrateId;
    DbFieldIndex m_iTrackId;
};

// class TrackQueryFields {
//    public:
//      TrackQueryFields() = default;
//      explicit TrackQueryFields(const FwdSqlQuery& query);
//      virtual ~TrackQueryFields() = default;
//
//      TrackId trackId(const FwdSqlQuery& query) const {
//          return TrackId(query.fieldValue(m_iTrackId));
//      }
//
//    private:
//      DbFieldIndex m_iTrackId;
// };

class SearchCrateTrackSelectResult : public FwdSqlQuerySelectResult {
  public:
    SearchCrateTrackSelectResult(SearchCrateTrackSelectResult&& other)
            : FwdSqlQuerySelectResult(std::move(other)),
              m_queryFields(std::move(other.m_queryFields)) {
    }
    ~SearchCrateTrackSelectResult() override = default;

    SearchCrateId searchCrateId() const {
        return m_queryFields.searchCrateId(query());
    }
    TrackId trackId() const {
        return m_queryFields.trackId(query());
    }

  private:
    friend class SearchCrateStorage;
    SearchCrateTrackSelectResult() = default;
    explicit SearchCrateTrackSelectResult(FwdSqlQuery&& query)
            : FwdSqlQuerySelectResult(std::move(query)),
              m_queryFields(FwdSqlQuerySelectResult::query()) {
    }

    SearchCrateTrackQueryFields m_queryFields;
};

// class TrackSelectResult : public FwdSqlQuerySelectResult {
//   public:
//     TrackSelectResult(TrackSelectResult&& other)
//             : FwdSqlQuerySelectResult(std::move(other)),
//               m_queryFields(std::move(other.m_queryFields)) {
//     }
//     ~TrackSelectResult() override = default;

//     TrackId trackId() const {
//         return m_queryFields.trackId(query());
//     }

//   private:
//     friend class SearchCrateStorage;
//     TrackSelectResult() = default;
//     explicit TrackSelectResult(FwdSqlQuery&& query)
//             : FwdSqlQuerySelectResult(std::move(query)),
//               m_queryFields(FwdSqlQuerySelectResult::query()) {
//     }

//     TrackQueryFields m_queryFields;
// };

class SearchCrateStorage : public virtual /*implements*/ SqlStorage {
  public:
    SearchCrateStorage() = default;
    ~SearchCrateStorage() override = default;

    void repairDatabase(
            const QSqlDatabase& database) override;

    void connectDatabase(
            const QSqlDatabase& database) override;
    void disconnectDatabase() override;

    /////////////////////////////////////////////////////////////////////////
    // SearchCrate write operations (transactional, non-const)
    // Only invoked by TrackCollection!
    //
    // Naming conventions:
    //  on<present participle>...()
    //    - Invoked within active transaction
    //    - May fail
    //    - Performs only database modifications that are either committed
    //      or implicitly reverted on rollback
    //  after<present participle>...()
    //    - Invoked after preceding transaction has been committed (see above)
    //    - Must not fail
    //    - Typical use case: Update internal caches and compute change set
    //      for notifications
    /////////////////////////////////////////////////////////////////////////

    bool onInsertingSearchCrate(
            const SearchCrate& searchCrate,
            SearchCrateId* pSearchCrateId = nullptr);

    bool onUpdatingSearchCrate(
            const SearchCrate& searchCrate);

    bool onDeletingSearchCrate(
            SearchCrateId searchCrateId);

    bool onAddingSearchCrateTracks(
            SearchCrateId searchCrateId,
            const QList<TrackId>& trackIds);

    bool onRemovingSearchCrateTracks(
            SearchCrateId searchCrateId,
            const QList<TrackId>& trackIds);

    bool onPurgingTracks(
            const QList<TrackId>& trackIds);

    /////////////////////////////////////////////////////////////////////////
    // SearchCrate read operations (read-only, const)
    /////////////////////////////////////////////////////////////////////////

    uint countSearchCrates() const;

    // Omit the pSearchCrate parameter for checking if the corresponding searchCrate exists.
    bool readSearchCrateById(
            SearchCrateId id,
            SearchCrate* pSearchCrate = nullptr) const;
    bool readSearchCrateByName(
            const QString& name,
            SearchCrate* pSearchCrate = nullptr) const;

    // The following list results are ordered by searchCrate name:
    //  - case-insensitive
    //  - locale-aware
    SearchCrateSelectResult selectSearchCrates() const; // all searchCrates
    SearchCrateSelectResult selectSearchCratesByIds(    // subset of searchCrates
            const QString& subselectForSearchCrateIds,
            SqlSubselectMode subselectMode) const;

    // TODO(XXX): Move this function into the AutoDJ component after
    // fixing various database design flaws in AutoDJ itself (see also:
    // searchCrateschema.h). AutoDJ should use the function selectSearchCratesByIds()
    // from this class for the actual implementation.
    // This refactoring should be deferred until consensus on the
    // redesign of the AutoDJ feature has been reached. The main
    // ideas of the new design should be documented for verification
    // before starting to code.

    // Not needed
    // SearchCrateSelectResult selectAutoDjSearchCrates(bool autoDjSource = true) const;

    // SearchCrate content, i.e. the searchCrate's tracks referenced by id
    uint countSearchCrateTracks(SearchCrateId searchCrateId) const;

    // Format a subselect query for the tracks contained in searchCrate.
    static QString formatSubselectQueryForSearchCrateTrackIds(
            SearchCrateId searchCrateId); // no db access

    QString formatQueryForTrackIdsBySearchCrateNameLike(
            const QString& searchCrateNameLike) const;      // no db access
    static QString formatQueryForTrackIdsWithSearchCrate(); // no db access
    // Select the track ids of a searchCrate or the searchCrate ids of a track respectively.
    // The results are sorted (ascending) by the target id, i.e. the id that is
    // not provided for filtering. This enables the caller to perform efficient
    // binary searches on the result set after storing it in a list or vector.
    SearchCrateTrackSelectResult selectSearchCrateTracksSorted(
            SearchCrateId searchCrateId) const;
    SearchCrateTrackSelectResult selectTrackSearchCratesSorted(
            TrackId trackId) const;
    SearchCrateSummarySelectResult selectSearchCratesWithTrackCount(
            const QList<TrackId>& trackIds) const;
    SearchCrateTrackSelectResult selectTracksSortedBySearchCrateNameLike(
            const QString& searchCrateNameLike) const;
    // TrackSelectResult selectAllTracksSorted() const;

    // Returns the set of searchCrate ids for searchCrates that contain any of the
    // provided track ids.
    QSet<SearchCrateId> collectSearchCrateIdsOfTracks(
            const QList<TrackId>& trackIds) const;

    /////////////////////////////////////////////////////////////////////////
    // SearchCrateSummary view operations (read-only, const)
    /////////////////////////////////////////////////////////////////////////

    // Track summaries of all searchCrates:
    //  - Hidden tracks are excluded from the searchCrate summary statistics
    //  - The result list is ordered by searchCrate name:
    //     - case-insensitive
    //     - locale-aware
    SearchCrateSummarySelectResult selectSearchCrateSummaries() const; // all searchCrates

    // Omit the pSearchCrate parameter for checking if the corresponding searchCrate exists.
    bool readSearchCrateSummaryById(SearchCrateId id,
            SearchCrateSummary* pSearchCrateSummary = nullptr) const;

  private:
    void createViews();

    QSqlDatabase m_database;
};
