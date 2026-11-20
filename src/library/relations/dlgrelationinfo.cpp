#include "library/relations/dlgrelationinfo.h"

#include <QIcon>

#include "defs_urls.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgrelationinfo.cpp"

DlgRelationInfo::DlgRelationInfo(RelationsDao::RelationGroup group, Library* pLibrary)
        : QDialog(nullptr),
          m_group(group),
          m_pLibrary(pLibrary) {
    init();
}

void DlgRelationInfo::init() {
    setupUi(this);
    setWindowIcon(QIcon(MIXXX_ICON_PATH));

    connect(btnOK, &QPushButton::clicked, this, &DlgRelationInfo::slotOk);
    connect(btnCancel, &QPushButton::clicked, this, &DlgRelationInfo::slotCancel);

    if (!m_pLibrary) {
        return;
    }

    auto pRelationsDao = m_pLibrary->trackCollectionManager()
                                 ->internalCollection()
                                 ->getRelationsDAO();

    // Fetch the relation type name
    RelationsDao::Relation rel = pRelationsDao.relationById(m_group.relationId);
    txtType->setText(rel.type);

    txtComment->setPlainText(m_group.comment);
    txtDateAdded->setText(m_group.dateAdded.date().toString());

    // Populate the track list
    QList<TrackId> trackIds = pRelationsDao.tracksInGroup(m_group.id);
    for (TrackId trackId : trackIds) {
        TrackPointer pTrack = m_pLibrary->trackCollectionManager()->getTrackById(trackId);
        if (pTrack) {
            QString display = QStringLiteral("%1 - %2")
                                      .arg(pTrack->getArtist(), pTrack->getTitle());
            listTracks->addItem(display);
        }
    }
}

void DlgRelationInfo::slotOk() {
    saveRelation();
    accept();
}

void DlgRelationInfo::slotCancel() {
    reject();
}

void DlgRelationInfo::saveRelation() {
    if (!m_pLibrary) {
        return;
    }

    // Save the new group comment to the database
    m_pLibrary
            ->trackCollectionManager()
            ->internalCollection()
            ->getRelationsDAO()
            .updateGroupComment(m_group.id, txtComment->toPlainText());
}
