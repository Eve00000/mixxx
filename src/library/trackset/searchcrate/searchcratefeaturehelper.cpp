#include "library/trackset/searchcrate/searchcratefeaturehelper.h"

#include <QInputDialog>
#include <QLineEdit>

#include "library/trackcollection.h"
#include "library/trackset/searchcrate/searchcrate.h"
#include "library/trackset/searchcrate/searchcratesummary.h"
#include "moc_searchcratefeaturehelper.cpp"

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
    const QString proposedSearchCrateName =
            proposeNameForNewSearchCrate(
                    QStringLiteral("%1 %2")
                            .arg(oldSearchCrate.getName(), tr("copy", "//:")));
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
        DEBUG_ASSERT(newSearchCrate.hasName());
        break;
    }

    SearchCrateId newSearchCrateId;
    if (m_pTrackCollection->insertSearchCrate(newSearchCrate, &newSearchCrateId)) {
        DEBUG_ASSERT(newSearchCrateId.isValid());
        newSearchCrate.setId(newSearchCrateId);
        qDebug() << "Created new searchCrate" << newSearchCrate;
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
            qDebug() << "Duplicated searchCrate"
                     << oldSearchCrate << "->" << newSearchCrate;
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
