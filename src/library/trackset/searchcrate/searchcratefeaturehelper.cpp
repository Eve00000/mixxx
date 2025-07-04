#include "library/trackset/searchcrate/searchcratefeaturehelper.h"

#include <QInputDialog>
#include <QLineEdit>

#include "library/trackcollection.h"
#include "library/trackset/searchcrate/searchcrate.h"
#include "library/trackset/searchcrate/searchcratesummary.h"
#include "moc_searchcratefeaturehelper.cpp"

const bool sDebugSearchCrateFeatureHelper = false;

SearchCrateFeatureHelper::SearchCrateFeatureHelper(
        TrackCollection* pTrackCollection,
        UserSettingsPointer pConfig)
        : m_pTrackCollection(pTrackCollection),
          m_pConfig(pConfig) {
}

QString SearchCrateFeatureHelper::proposeNameForNewSearchCrate(
        const QString& initialName) const {
    DEBUG_ASSERT(!initialName.isEmpty());
    QString proposedName;
    int suffixCounter = 0;
    do {
        if (suffixCounter++ > 0) {
            // Append suffix " 2", " 3", ...
            proposedName = QStringLiteral("%1 %2")
                                   .arg(initialName, QString::number(suffixCounter));
        } else {
            proposedName = initialName;
        }
    } while (m_pTrackCollection->searchCrates().readSearchCrateByName(proposedName));
    // Found an unused searchCrate name
    return proposedName;
}

SearchCrateId SearchCrateFeatureHelper::createEmptySearchCrate() {
    const QString proposedSearchCrateName =
            proposeNameForNewSearchCrate(tr("New SearchCrate"));
    SearchCrate newSearchCrate;
    for (;;) {
        bool ok = false;
        auto newName =
                QInputDialog::getText(
                        nullptr,
                        tr("Create New SearchCrate"),
                        tr("Enter name for new searchCrate:"),
                        QLineEdit::Normal,
                        proposedSearchCrateName,
                        &ok)
                        .trimmed();
        if (!ok) {
            return SearchCrateId();
        }
        if (newName.isEmpty()) {
            QMessageBox::warning(
                    nullptr,
                    tr("Creating SearchCrate Failed"),
                    tr("A searchCrate cannot have a blank name."));
            continue;
        }
        if (m_pTrackCollection->searchCrates().readSearchCrateByName(newName)) {
            QMessageBox::warning(
                    nullptr,
                    tr("Creating SearchCrate Failed"),
                    tr("A searchCrate by that name already exists."));
            continue;
        }
        newSearchCrate.setName(std::move(newName));
        DEBUG_ASSERT(newSearchCrate.hasName());
        break;
    }

    SearchCrateId newSearchCrateId;
    if (m_pTrackCollection->insertSearchCrate(newSearchCrate, &newSearchCrateId)) {
        DEBUG_ASSERT(newSearchCrateId.isValid());
        newSearchCrate.setId(newSearchCrateId);
        qDebug() << "Created new searchCrate" << newSearchCrate;
    } else {
        DEBUG_ASSERT(!newSearchCrateId.isValid());
        qWarning() << "Failed to create new searchCrate"
                   << "->" << newSearchCrate.getName();
        QMessageBox::warning(nullptr,
                tr("Creating SearchCrate Failed"),
                tr("An unknown error occurred while creating searchCrate: ") +
                        newSearchCrate.getName());
    }
    return newSearchCrateId;
}

SearchCrateId SearchCrateFeatureHelper::duplicateSearchCrate(const SearchCrate& oldSearchCrate) {
    if (sDebugSearchCrateFeatureHelper) {
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE SEARCHCRATES] -> START";
    }

    const QString proposedSearchCrateName =
            proposeNameForNewSearchCrate(
                    QStringLiteral("%1 %2")
                            .arg(oldSearchCrate.getName(), tr("copy", "//:")));

    const QString& newSearchInput = oldSearchCrate.getSearchInput();
    const QString& newSearchSql = oldSearchCrate.getSearchSql();
    if (sDebugSearchCrateFeatureHelper) {
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old searchInput" << newSearchInput;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old searchSql" << newSearchSql;
    }

    const QString& newCondition1Field = oldSearchCrate.getCondition1Field();
    const QString& newCondition2Field = oldSearchCrate.getCondition2Field();
    const QString& newCondition3Field = oldSearchCrate.getCondition3Field();
    const QString& newCondition4Field = oldSearchCrate.getCondition4Field();
    const QString& newCondition5Field = oldSearchCrate.getCondition5Field();
    const QString& newCondition6Field = oldSearchCrate.getCondition6Field();
    const QString& newCondition7Field = oldSearchCrate.getCondition7Field();
    const QString& newCondition8Field = oldSearchCrate.getCondition8Field();
    const QString& newCondition9Field = oldSearchCrate.getCondition9Field();
    const QString& newCondition10Field = oldSearchCrate.getCondition10Field();
    const QString& newCondition11Field = oldSearchCrate.getCondition11Field();
    const QString& newCondition12Field = oldSearchCrate.getCondition12Field();
    if (sDebugSearchCrateFeatureHelper) {
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old Condition1Field"
                 << newCondition1Field;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old Condition2Field"
                 << newCondition2Field;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old Condition3Field"
                 << newCondition3Field;
    }

    const QString& newCondition1Operator = oldSearchCrate.getCondition1Operator();
    const QString& newCondition2Operator = oldSearchCrate.getCondition2Operator();
    const QString& newCondition3Operator = oldSearchCrate.getCondition3Operator();
    const QString& newCondition4Operator = oldSearchCrate.getCondition4Operator();
    const QString& newCondition5Operator = oldSearchCrate.getCondition5Operator();
    const QString& newCondition6Operator = oldSearchCrate.getCondition6Operator();
    const QString& newCondition7Operator = oldSearchCrate.getCondition7Operator();
    const QString& newCondition8Operator = oldSearchCrate.getCondition8Operator();
    const QString& newCondition9Operator = oldSearchCrate.getCondition9Operator();
    const QString& newCondition10Operator = oldSearchCrate.getCondition10Operator();
    const QString& newCondition11Operator = oldSearchCrate.getCondition11Operator();
    const QString& newCondition12Operator = oldSearchCrate.getCondition12Operator();
    if (sDebugSearchCrateFeatureHelper) {
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old "
                    "Condition1Operator"
                 << newCondition1Operator;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old "
                    "Condition2Operator"
                 << newCondition2Operator;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old "
                    "Condition3Operator"
                 << newCondition3Operator;
    }

    const QString& newCondition1Value = oldSearchCrate.getCondition1Value();
    const QString& newCondition2Value = oldSearchCrate.getCondition2Value();
    const QString& newCondition3Value = oldSearchCrate.getCondition3Value();
    const QString& newCondition4Value = oldSearchCrate.getCondition4Value();
    const QString& newCondition5Value = oldSearchCrate.getCondition5Value();
    const QString& newCondition6Value = oldSearchCrate.getCondition6Value();
    const QString& newCondition7Value = oldSearchCrate.getCondition7Value();
    const QString& newCondition8Value = oldSearchCrate.getCondition8Value();
    const QString& newCondition9Value = oldSearchCrate.getCondition9Value();
    const QString& newCondition10Value = oldSearchCrate.getCondition10Value();
    const QString& newCondition11Value = oldSearchCrate.getCondition11Value();
    const QString& newCondition12Value = oldSearchCrate.getCondition12Value();
    if (sDebugSearchCrateFeatureHelper) {
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old Condition1Value"
                 << newCondition1Value;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old Condition2Value"
                 << newCondition2Value;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old Condition3Value"
                 << newCondition2Value;
    }

    const QString& newCondition1Combiner = oldSearchCrate.getCondition1Combiner();
    const QString& newCondition2Combiner = oldSearchCrate.getCondition2Combiner();
    const QString& newCondition3Combiner = oldSearchCrate.getCondition3Combiner();
    const QString& newCondition4Combiner = oldSearchCrate.getCondition4Combiner();
    const QString& newCondition5Combiner = oldSearchCrate.getCondition5Combiner();
    const QString& newCondition6Combiner = oldSearchCrate.getCondition6Combiner();
    const QString& newCondition7Combiner = oldSearchCrate.getCondition7Combiner();
    const QString& newCondition8Combiner = oldSearchCrate.getCondition8Combiner();
    const QString& newCondition9Combiner = oldSearchCrate.getCondition9Combiner();
    const QString& newCondition10Combiner = oldSearchCrate.getCondition10Combiner();
    const QString& newCondition11Combiner = oldSearchCrate.getCondition11Combiner();
    const QString& newCondition12Combiner = oldSearchCrate.getCondition12Combiner();
    if (sDebugSearchCrateFeatureHelper) {
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old "
                    "Condition1Combiner"
                 << newCondition1Combiner;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old "
                    "Condition2Combiner"
                 << newCondition2Combiner;
        qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE] -> old "
                    "Condition3Combiner"
                 << newCondition3Combiner;
    }

    SearchCrate newSearchCrate;
    for (;;) {
        bool ok = false;
        auto newName =
                QInputDialog::getText(
                        nullptr,
                        tr("Duplicate SearchCrate"),
                        tr("Enter name for new searchCrate:"),
                        QLineEdit::Normal,
                        proposedSearchCrateName,
                        &ok)
                        .trimmed();
        if (!ok) {
            return SearchCrateId();
        }
        if (newName.isEmpty()) {
            QMessageBox::warning(
                    nullptr,
                    tr("Duplicating SearchCrate Failed"),
                    tr("A searchCrate cannot have a blank name."));
            continue;
        }
        if (m_pTrackCollection->searchCrates().readSearchCrateByName(newName)) {
            QMessageBox::warning(
                    nullptr,
                    tr("Duplicating SearchCrate Failed"),
                    tr("A searchCrate by that name already exists."));
            continue;
        }
        newSearchCrate.setName(std::move(newName));
        newSearchCrate.setSearchInput(newSearchInput);
        newSearchCrate.setSearchSql(newSearchSql);
        newSearchCrate.setCondition1Field(newCondition1Field);
        newSearchCrate.setCondition2Field(newCondition2Field);
        newSearchCrate.setCondition3Field(newCondition3Field);
        newSearchCrate.setCondition4Field(newCondition4Field);
        newSearchCrate.setCondition5Field(newCondition5Field);
        newSearchCrate.setCondition6Field(newCondition6Field);
        newSearchCrate.setCondition7Field(newCondition7Field);
        newSearchCrate.setCondition8Field(newCondition8Field);
        newSearchCrate.setCondition9Field(newCondition9Field);
        newSearchCrate.setCondition10Field(newCondition10Field);
        newSearchCrate.setCondition11Field(newCondition11Field);
        newSearchCrate.setCondition12Field(newCondition12Field);
        newSearchCrate.setCondition1Operator(newCondition1Operator);
        newSearchCrate.setCondition2Operator(newCondition2Operator);
        newSearchCrate.setCondition3Operator(newCondition3Operator);
        newSearchCrate.setCondition4Operator(newCondition4Operator);
        newSearchCrate.setCondition5Operator(newCondition5Operator);
        newSearchCrate.setCondition6Operator(newCondition6Operator);
        newSearchCrate.setCondition7Operator(newCondition7Operator);
        newSearchCrate.setCondition8Operator(newCondition8Operator);
        newSearchCrate.setCondition9Operator(newCondition9Operator);
        newSearchCrate.setCondition10Operator(newCondition10Operator);
        newSearchCrate.setCondition11Operator(newCondition11Operator);
        newSearchCrate.setCondition12Operator(newCondition12Operator);
        newSearchCrate.setCondition1Value(newCondition1Value);
        newSearchCrate.setCondition2Value(newCondition2Value);
        newSearchCrate.setCondition3Value(newCondition3Value);
        newSearchCrate.setCondition4Value(newCondition4Value);
        newSearchCrate.setCondition5Value(newCondition5Value);
        newSearchCrate.setCondition6Value(newCondition6Value);
        newSearchCrate.setCondition7Value(newCondition7Value);
        newSearchCrate.setCondition8Value(newCondition8Value);
        newSearchCrate.setCondition9Value(newCondition9Value);
        newSearchCrate.setCondition10Value(newCondition10Value);
        newSearchCrate.setCondition11Value(newCondition11Value);
        newSearchCrate.setCondition12Value(newCondition12Value);
        newSearchCrate.setCondition1Combiner(newCondition1Combiner);
        newSearchCrate.setCondition2Combiner(newCondition2Combiner);
        newSearchCrate.setCondition3Combiner(newCondition3Combiner);
        newSearchCrate.setCondition4Combiner(newCondition4Combiner);
        newSearchCrate.setCondition5Combiner(newCondition5Combiner);
        newSearchCrate.setCondition6Combiner(newCondition6Combiner);
        newSearchCrate.setCondition7Combiner(newCondition7Combiner);
        newSearchCrate.setCondition8Combiner(newCondition8Combiner);
        newSearchCrate.setCondition9Combiner(newCondition9Combiner);
        newSearchCrate.setCondition10Combiner(newCondition10Combiner);
        newSearchCrate.setCondition11Combiner(newCondition11Combiner);
        newSearchCrate.setCondition12Combiner(newCondition12Combiner);

        DEBUG_ASSERT(newSearchCrate.hasName());
        break;
    }

    SearchCrateId newSearchCrateId;
    if (m_pTrackCollection->insertSearchCrate(newSearchCrate, &newSearchCrateId)) {
        DEBUG_ASSERT(newSearchCrateId.isValid());
        newSearchCrate.setId(newSearchCrateId);
        if (sDebugSearchCrateFeatureHelper) {
            qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE SEARCHCRATES] -> "
                        "Created new searchCrate"
                     << newSearchCrate;
        }
        QList<TrackId> trackIds;
        trackIds.reserve(
                m_pTrackCollection->searchCrates().countSearchCrateTracks(oldSearchCrate.getId()));
        {
            SearchCrateTrackSelectResult searchCrateTracks(
                    m_pTrackCollection->searchCrates()
                            .selectSearchCrateTracksSorted(
                                    oldSearchCrate.getId()));
            while (searchCrateTracks.next()) {
                trackIds.append(searchCrateTracks.trackId());
            }
        }
        if (m_pTrackCollection->addSearchCrateTracks(newSearchCrateId, trackIds)) {
            if (sDebugSearchCrateFeatureHelper) {
                qDebug() << "[SEARCHCRATES] [HELPER] [DUPLICATE SEARCHCRATES] "
                            "Duplicated searchCrate -> "
                         << oldSearchCrate << "->" << newSearchCrate;
            }
        } else {
            qWarning() << "Failed to copy tracks from"
                       << oldSearchCrate << "into" << newSearchCrate;
        }
    } else {
        qWarning() << "Failed to duplicate searchCrate"
                   << oldSearchCrate << "->" << newSearchCrate.getName();
        QMessageBox::warning(nullptr,
                tr("Duplicating SearchCrate Failed"),
                tr("An unknown error occurred while creating searchCrate: ") +
                        newSearchCrate.getName());
    }
    return newSearchCrateId;
}
