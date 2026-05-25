#include "widget/wtrackstemmenu.h"

#include <QAction>

#include "moc_wtrackstemmenu.cpp"

WTrackStemMenu::WTrackStemMenu(const QString& label,
        QWidget* parent,
        bool primaryDeck,
        const QString& group,
        const QList<StemInfo>& stemInfo)
        : QMenu(label, parent),
          m_group(group),
          m_selectMode(false),
          m_stemInfo(stemInfo),
          m_currentSelection() {
    if (primaryDeck) {
        QAction* pAction = new QAction(tr("Load for stem mixing"), this);
        addAction(pAction);
        connect(pAction, &QAction::triggered, this, [this, group] {
            emit selectedStem(group, {mixxx::StemChannelSelection()});
        });
    }

    // Load all stems action - uses appropriate mask based on mode
    QAction* pAction = new QAction(tr("Load all stems"), this);
    addAction(pAction);
    connect(pAction, &QAction::triggered, this, [this, group] {
        // Use the appropriate all-stems mask based on current mode
        mixxx::StemChannelSelection allStemsMask(
                mixxx::includeOriginalMasterStems()
                        ? mixxx::kStemMaskAll5
                        : mixxx::kStemMaskAll4);
        emit selectedStem(group, allStemsMask);
    });
    addSeparator();

    // Add PreMix/Original track action if 5-stem mode is active
    if (mixxx::includeOriginalMasterStems()) {
        m_preMixAction = make_parented<QAction>(
                tr("Load original/pre-mixed track"), this);
        addAction(m_preMixAction.get());
        connect(m_preMixAction.get(), &QAction::triggered, this, [this, group] {
            emit selectedStem(m_group, mixxx::StemChannel::PreMix);
        });
        connect(m_preMixAction.get(),
                &QAction::toggled,
                this,
                [this](bool checked) {
                    m_currentSelection.setFlag(mixxx::StemChannel::PreMix, checked);
                });
    }

    // Get active stem channels based on current mode
    QList<mixxx::StemChannel> activeStemChannels = getActiveStemChannels();

    int stemIdx = 0;
    for (const auto& stemTrack : std::as_const(activeStemChannels)) {
        QString stemLabel;
        if (stemTrack == mixxx::StemChannel::PreMix) {
            stemLabel = tr("Original/Pre-mixed");
        } else {
            // Map stem channel to info index (PreMix is index 0 in 5-stem mode)
            int infoIdx = mixxx::includeOriginalMasterStems() ? stemIdx - 1 : stemIdx;
            if (infoIdx >= 0 && infoIdx < m_stemInfo.size()) {
                stemLabel = m_stemInfo.at(infoIdx).getLabel();
            } else {
                stemLabel = tr("Stem %1").arg(static_cast<int>(stemTrack));
            }
        }

        m_stemActions.emplace_back(
                make_parented<QAction>(tr("Load the \"%1\" stem")
                                               .arg(stemLabel),
                        this));
        addAction(m_stemActions.back().get());
        connect(m_stemActions.back().get(), &QAction::triggered, this, [this, stemTrack] {
            emit selectedStem(m_group, stemTrack);
        });
        connect(m_stemActions.back().get(),
                &QAction::toggled,
                this,
                [this, stemTrack](bool checked) {
                    m_currentSelection.setFlag(stemTrack, checked);
                });
        stemIdx++;
    }

    m_selectAction = make_parented<QAction>(this);
    m_selectAction->setToolTip(tr("Load multiple stem into a stereo deck"));
    m_selectAction->setDisabled(true);
    addAction(m_selectAction.get());
    installEventFilter(this);
}

QList<mixxx::StemChannel> WTrackStemMenu::getActiveStemChannels() const {
    QList<mixxx::StemChannel> channels;

    if (mixxx::includeOriginalMasterStems()) {
        channels.append(mixxx::StemChannel::PreMix);
    }

    channels.append(mixxx::StemChannel::First);
    channels.append(mixxx::StemChannel::Second);
    channels.append(mixxx::StemChannel::Third);
    channels.append(mixxx::StemChannel::Fourth);

    return channels;
}

bool WTrackStemMenu::eventFilter(QObject* pObj, QEvent* e) {
    QInputEvent* pInputEvent = dynamic_cast<QInputEvent*>(e);
    if (pInputEvent != nullptr) {
        bool selectMode = pInputEvent->modifiers().testFlag(Qt::ControlModifier);
        if (selectMode != m_selectMode) {
            m_selectMode = selectMode;
            updateActions();
        }
    }

    if (m_selectMode && (e->type() == QEvent::MouseButtonRelease)) {
        QAction* pAction = activeAction();
        if (pAction && pAction->isCheckable()) {
            pAction->setChecked(!pAction->isChecked());
            updateActions();
            return true;
        }
    }
    return QObject::eventFilter(pObj, e);
}

void WTrackStemMenu::updateActions() {
    for (const auto& pAction : m_stemActions) {
        pAction->setCheckable(m_selectMode);
    }

    // Also handle PreMix action if it exists
    if (m_preMixAction) {
        m_preMixAction->setCheckable(m_selectMode);
    }

    m_selectAction->setText(m_selectMode
                    ? !m_currentSelection ? tr("Select stems to load")
                                          : tr("Release \"CTRL\" to load the "
                                               "current selection")
                    : tr("Use \"CTRL\" to select multiple stems"));
}

void WTrackStemMenu::showEvent(QShowEvent* pQEvent) {
    updateActions();
    QMenu::showEvent(pQEvent);
}

void WTrackStemMenu::keyPressEvent(QKeyEvent* pQEvent) {
    m_selectMode = pQEvent->modifiers() & Qt::ControlModifier;
    updateActions();
    pQEvent->accept();
}

void WTrackStemMenu::keyReleaseEvent(QKeyEvent* pQEvent) {
    bool selectMode = pQEvent->modifiers() & Qt::ControlModifier;
    if (!selectMode && m_selectMode && m_currentSelection) {
        emit selectedStem(m_group, m_currentSelection);
        m_currentSelection = mixxx::StemChannelSelection();
    }
    m_selectMode = selectMode;
    updateActions();
    pQEvent->accept();
}

// #include "widget/wtrackstemmenu.h"
//
// #include <QAction>
//
// #include "moc_wtrackstemmenu.cpp"
//
// namespace {
// const QList<mixxx::StemChannel> stemTracks = {
//         mixxx::StemChannel::First,
//         mixxx::StemChannel::Second,
//         mixxx::StemChannel::Third,
//         mixxx::StemChannel::Fourth,
// };
// } // namespace
//
// WTrackStemMenu::WTrackStemMenu(const QString& label,
//         QWidget* parent,
//         bool primaryDeck,
//         const QString& group,
//         const QList<StemInfo>& stemInfo)
//         : QMenu(label, parent),
//           m_group(group),
//           m_selectMode(false),
//           m_stemInfo(stemInfo),
//           m_currentSelection() {
//     if (primaryDeck) {
//         QAction* pAction = new QAction(tr("Load for stem mixing"), this);
//         addAction(pAction);
//         connect(pAction, &QAction::triggered, this, [this, group] {
//             emit selectedStem(group, {mixxx::StemChannelSelection()});
//         });
//     }
//
//     QAction* pAction = new QAction(tr("Load pre-mixed stereo track"), this);
//     addAction(pAction);
//     connect(pAction, &QAction::triggered, this, [this, group] {
//         emit selectedStem(group, mixxx::StemChannel::All);
//     });
//     addSeparator();
//
//     DEBUG_ASSERT(stemTracks.count() == mixxx::kMaxSupportedStems);
//     int stemIdx = 0;
//     for (const auto& stemTrack : stemTracks) {
//         m_stemActions.emplace_back(
//                 make_parented<QAction>(tr("Load the \"%1\" stem")
//                                                .arg(m_stemInfo.at(stemIdx).getLabel()),
//                         this));
//         addAction(m_stemActions.back().get());
//         connect(m_stemActions.back().get(), &QAction::triggered, this, [this, stemTrack] {
//             emit selectedStem(m_group, stemTrack);
//         });
//         connect(m_stemActions.back().get(),
//                 &QAction::toggled,
//                 this,
//                 [this, stemTrack](bool checked) {
//                     m_currentSelection.setFlag(stemTrack, checked);
//                 });
//         stemIdx++;
//     }
//     m_selectAction = make_parented<QAction>(this);
//     m_selectAction->setToolTip(tr("Load multiple stem into a stereo deck"));
//     m_selectAction->setDisabled(true);
//     addAction(m_selectAction.get());
//     installEventFilter(this);
// }
//
// bool WTrackStemMenu::eventFilter(QObject* pObj, QEvent* e) {
//     QInputEvent* pInputEvent = dynamic_cast<QInputEvent*>(e);
//     if (pInputEvent != nullptr) {
//         bool selectMode = pInputEvent->modifiers().testFlag(Qt::ControlModifier);
//         if (selectMode != m_selectMode) {
//             m_selectMode = selectMode;
//             updateActions();
//         }
//     }
//
//     if (m_selectMode && (e->type() == QEvent::MouseButtonRelease)) {
//         QAction* pAction = activeAction();
//         if (pAction && pAction->isCheckable()) {
//             pAction->setChecked(!pAction->isChecked());
//             updateActions();
//             return true;
//         }
//     }
//     return QObject::eventFilter(pObj, e);
// }
// void WTrackStemMenu::updateActions() {
//     for (const auto& pAction : m_stemActions) {
//         pAction->setCheckable(m_selectMode);
//     }
//     m_selectAction->setText(m_selectMode
//                     ? !m_currentSelection ? tr("Select stems to load")
//                                           : tr("Release \"CTRL\" to load the "
//                                                "current selection")
//                     : tr("Use \"CTRL\" to select multiple stems"));
// }
//
// void WTrackStemMenu::showEvent(QShowEvent* pQEvent) {
//     updateActions();
//     QMenu::showEvent(pQEvent);
// }
//
// void WTrackStemMenu::keyPressEvent(QKeyEvent* pQEvent) {
//     m_selectMode = pQEvent->modifiers() & Qt::ControlModifier;
//     updateActions();
//     pQEvent->accept();
// }
//
// void WTrackStemMenu::keyReleaseEvent(QKeyEvent* pQEvent) {
//     bool selectMode = pQEvent->modifiers() & Qt::ControlModifier;
//     if (!selectMode && m_selectMode && m_currentSelection) {
//         emit selectedStem(m_group, m_currentSelection);
//         m_currentSelection = mixxx::StemChannelSelection();
//     }
//     m_selectMode = selectMode;
//     updateActions();
//     pQEvent->accept();
// }
