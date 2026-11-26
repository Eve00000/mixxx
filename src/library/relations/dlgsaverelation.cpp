#include "library/relations/dlgsaverelation.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include "track/track.h"

// static
QString DlgSaveRelation::trackLabel(const QString& artist,
        const QString& title,
        const QString& album,
        const QString& year) {
    QString label = QStringLiteral("%1 - %2").arg(artist, title);
    QStringList details;
    if (!album.isEmpty()) {
        details << album;
    }
    const QString shortYear = year.trimmed().left(4);
    if (!shortYear.isEmpty()) {
        details << shortYear;
    }
    if (!details.isEmpty()) {
        label += QStringLiteral(" (") + details.join(QStringLiteral(" - ")) + QLatin1Char(')');
    }
    return label;
}

// static
DbId DlgSaveRelation::execute(
        QWidget* pParent,
        RelationsDao& relationsDao,
        const QList<Entry>& entries,
        DbId presetRelationId) {
    QList<Entry> trackEntries;
    for (const Entry& entry : entries) {
        if (entry.pTrack && entry.pTrack->getId().isValid()) {
            trackEntries.append(entry);
        }
    }
    if (trackEntries.size() < 2) {
        QMessageBox::information(pParent,
                tr("Save Relation"),
                tr("At least 2 tracks are needed to create a relation."));
        return DbId();
    }

    const QList<RelationsDao::Relation> relations = relationsDao.allRelations();

    QDialog dlg(pParent);
    dlg.setWindowTitle(tr("Save Relation"));
    dlg.resize(800, 500);
    auto* pMainLayout = new QVBoxLayout(&dlg);

    auto* pFormLayout = new QFormLayout();
    auto* pRelationTypeCombo = new QComboBox(&dlg);
    pRelationTypeCombo->setEditable(true);
    int presetIndex = -1;
    for (int i = 0; i < relations.size(); ++i) {
        pRelationTypeCombo->addItem(relations.at(i).type);
        if (presetRelationId.isValid() && relations.at(i).id == presetRelationId) {
            presetIndex = i;
        }
    }
    pRelationTypeCombo->setCurrentIndex(presetIndex);
    pRelationTypeCombo->lineEdit()->setPlaceholderText(
            tr("Choose a type or type a new one, e.g. BPM, Key, Energy"));
    pFormLayout->addRow(tr("Type:"), pRelationTypeCombo);

    auto* pRelationGroupCommentEdit = new QLineEdit(&dlg);
    pRelationGroupCommentEdit->setPlaceholderText(tr("e.g. 117"));
    pFormLayout->addRow(tr("Group Comment:"), pRelationGroupCommentEdit);
    pMainLayout->addLayout(pFormLayout);

    auto* pTableRelationTracks = new QTableWidget(trackEntries.size(), 2, &dlg);
    pTableRelationTracks->setHorizontalHeaderLabels({tr("Track"), tr("Comment")});
    pTableRelationTracks->verticalHeader()->setVisible(false);
    pTableRelationTracks->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::ResizeToContents);
    pTableRelationTracks->horizontalHeader()->setStretchLastSection(true);
    for (int row = 0; row < trackEntries.size(); ++row) {
        const Entry& entry = trackEntries.at(row);
        QString text = trackLabel(entry.pTrack->getArtist(),
                entry.pTrack->getTitle(),
                entry.pTrack->getAlbum(),
                entry.pTrack->getYear());
        if (!entry.prefix.isEmpty()) {
            text = entry.prefix + QStringLiteral(": ") + text;
        }
        auto* pTrackItem = new QTableWidgetItem(text);
        pTrackItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        pTrackItem->setCheckState(Qt::Checked);
        pTableRelationTracks->setItem(row, 0, pTrackItem);
        pTableRelationTracks->setItem(row, 1, new QTableWidgetItem());
    }
    pMainLayout->addWidget(pTableRelationTracks);

    auto* pAutoFillCheck = new QCheckBox(
            tr("Auto fill track order comments (Track 01, Track 02, ...)"), &dlg);
    pMainLayout->addWidget(pAutoFillCheck);

    // Numbers the checked tracks in the order of the table
    const auto autoFillTrackOrder = [pTableRelationTracks, pAutoFillCheck]() {
        if (!pAutoFillCheck->isChecked()) {
            return;
        }
        int checkedCount = 0;
        for (int row = 0; row < pTableRelationTracks->rowCount(); ++row) {
            if (pTableRelationTracks->item(row, 0)->checkState() == Qt::Checked) {
                ++checkedCount;
            }
        }
        const int width = qMax(2, QString::number(checkedCount).size());
        const QSignalBlocker blocker(pTableRelationTracks);
        int number = 0;
        for (int row = 0; row < pTableRelationTracks->rowCount(); ++row) {
            if (pTableRelationTracks->item(row, 0)->checkState() == Qt::Checked) {
                ++number;
                pTableRelationTracks->item(row, 1)->setText(
                        tr("Track %1").arg(number, width, 10, QLatin1Char('0')));
            } else {
                pTableRelationTracks->item(row, 1)->setText(QString());
            }
        }
    };
    QObject::connect(pAutoFillCheck, &QCheckBox::toggled, &dlg, autoFillTrackOrder);
    // Only (un)checking a track renumbers, editing a comment does not
    QObject::connect(pTableRelationTracks,
            &QTableWidget::itemChanged,
            &dlg,
            [autoFillTrackOrder](QTableWidgetItem* pItem) {
                if (pItem->column() == 0) {
                    autoFillTrackOrder();
                }
            });

    auto* pButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    pMainLayout->addWidget(pButtons);

    QObject::connect(pButtons, &QDialogButtonBox::accepted, &dlg, [&]() {
        int checkedCount = 0;
        for (int row = 0; row < pTableRelationTracks->rowCount(); ++row) {
            if (pTableRelationTracks->item(row, 0)->checkState() == Qt::Checked) {
                ++checkedCount;
            }
        }
        if (pRelationTypeCombo->currentText().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg,
                    tr("Save Relation"),
                    tr("Please choose or enter a relation type."));
            return;
        }
        if (checkedCount < 2) {
            QMessageBox::warning(&dlg,
                    tr("Save Relation"),
                    tr("Select at least 2 tracks."));
            return;
        }
        dlg.accept();
    });
    QObject::connect(pButtons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        return DbId();
    }

    const QString type = pRelationTypeCombo->currentText().trimmed();
    DbId relationId;
    for (const RelationsDao::Relation& relation : relations) {
        if (relation.type.compare(type, Qt::CaseInsensitive) == 0) {
            relationId = relation.id;
            break;
        }
    }
    if (!relationId.isValid()) {
        relationId = relationsDao.addRelation(type, QString());
    }
    if (!relationId.isValid()) {
        QMessageBox::warning(pParent,
                tr("Save Relation"),
                tr("Failed to create the relation type."));
        return DbId();
    }

    QList<TrackId> trackIds;
    QList<QString> trackComments;
    for (int row = 0; row < trackEntries.size(); ++row) {
        if (pTableRelationTracks->item(row, 0)->checkState() == Qt::Checked) {
            trackIds.append(trackEntries.at(row).pTrack->getId());
            trackComments.append(pTableRelationTracks->item(row, 1)->text().trimmed());
        }
    }

    const DbId groupId = relationsDao.createGroup(
            relationId, trackIds, pRelationGroupCommentEdit->text().trimmed());
    if (!groupId.isValid()) {
        QMessageBox::warning(pParent,
                tr("Save Relation"),
                tr("Failed to create the relation."));
        return DbId();
    }
    for (int i = 0; i < trackIds.size(); ++i) {
        if (!trackComments.at(i).isEmpty()) {
            relationsDao.setTrackComment(groupId, trackIds.at(i), trackComments.at(i));
        }
    }
    return groupId;
}
