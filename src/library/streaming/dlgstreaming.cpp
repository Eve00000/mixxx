#include "library/streaming/dlgstreaming.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/proxytrackmodel.h"
#include "library/streaming/streamingtracklistmodel.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgstreaming.cpp"
#include "track/track.h"
#include "util/logger.h"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"

namespace {
const mixxx::Logger kLogger("DlgStreaming");
} // anonymous namespace

DlgStreaming::DlgStreaming(
        WLibrary* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        KeyboardEventFilter* pKeyboard,
        mixxx::streaming::Provider* pProvider)
        : QWidget(parent),
          m_pConfig(std::move(pConfig)),
          m_pLibrary(pLibrary),
          m_pProvider(pProvider),
          m_pTrackCollectionManager(pLibrary->trackCollectionManager()),
          m_pSearchLineEdit(new QLineEdit(this)),
          m_pSearchButton(new QPushButton(tr("Search"), this)),
          m_pQualityComboBox(new QComboBox(this)),
          m_pLoginButton(new QPushButton(tr("Log in"), this)),
          m_pLogoutButton(new QPushButton(tr("Log out"), this)),
          m_pStatusLabel(new QLabel(this)),
          m_pTrackTableView(new WTrackTableView(
                  this, m_pConfig, pLibrary, parent->getTrackTableBackgroundColorOpacity())),
          m_pTrackModel(new StreamingTrackListModel(this,
                  m_pTrackCollectionManager,
                  pProvider->id(),
                  pProvider->displayName())),
          m_pProxyModel(new ProxyTrackModel(m_pTrackModel, true)) {
    m_pTrackTableView->installEventFilter(pKeyboard);

    auto* layout = new QVBoxLayout(this);
    auto* searchRow = new QHBoxLayout();
    m_pSearchLineEdit->setPlaceholderText(m_pProvider->searchPlaceholderText());
    searchRow->addWidget(m_pSearchLineEdit);
    searchRow->addWidget(m_pSearchButton);

    const QStringList qualityLabels = m_pProvider->qualityLabels();
    if (qualityLabels.isEmpty()) {
        m_pQualityComboBox->hide();
    } else {
        for (int i = 0; i < qualityLabels.size(); ++i) {
            m_pQualityComboBox->addItem(qualityLabels.at(i), i);
        }
        const int current = m_pProvider->currentQualityIndex();
        if (current >= 0) {
            m_pQualityComboBox->setCurrentIndex(current);
        }
        searchRow->addWidget(m_pQualityComboBox);
    }

    if (m_pProvider->requiresLogin()) {
        searchRow->addWidget(m_pLoginButton);
        searchRow->addWidget(m_pLogoutButton);
    } else {
        m_pLoginButton->hide();
        m_pLogoutButton->hide();
    }

    layout->addLayout(searchRow);
    layout->addWidget(m_pStatusLabel);
    layout->addWidget(m_pTrackTableView);

    m_pProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_pProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_pProxyModel->setParent(this);
    m_pTrackTableView->loadTrackModel(m_pProxyModel);

    connect(m_pSearchLineEdit,
            &QLineEdit::returnPressed,
            this,
            &DlgStreaming::slotSearch);
    connect(m_pSearchButton,
            &QPushButton::clicked,
            this,
            &DlgStreaming::slotSearch);
    connect(m_pQualityComboBox,
            &QComboBox::currentIndexChanged,
            this,
            &DlgStreaming::slotQualityChanged);
    connect(m_pLoginButton,
            &QPushButton::clicked,
            this,
            &DlgStreaming::slotLoginClicked);
    connect(m_pLogoutButton,
            &QPushButton::clicked,
            this,
            &DlgStreaming::slotLogoutClicked);

    connect(m_pProvider,
            &mixxx::streaming::Provider::loginStarted,
            this,
            &DlgStreaming::slotLoginStarted);
    connect(m_pProvider,
            &mixxx::streaming::Provider::loginFinished,
            this,
            &DlgStreaming::slotLoginFinished);
    connect(m_pProvider,
            &mixxx::streaming::Provider::sessionChanged,
            this,
            &DlgStreaming::slotSessionChanged);
    connect(m_pProvider,
            &mixxx::streaming::Provider::downloadProgress,
            this,
            &DlgStreaming::slotDownloadProgress);

    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            this,
            &DlgStreaming::slotLoadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            this,
            &DlgStreaming::slotLoadTrackToPlayer);
    connect(pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableFont);
    connect(pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableRowHeight);
    connect(pLibrary,
            &Library::setSelectedClick,
            m_pTrackTableView,
            &WTrackTableView::setSelectedClick);

    updateLoginUi(!m_pProvider->requiresLogin() || m_pProvider->hasSession());
}

DlgStreaming::~DlgStreaming() {
    // Delete the table view before the models, because the view saves its
    // header state using the model in its destructor.
    delete m_pTrackTableView;
}

void DlgStreaming::onSearch(const QString& text) {
    if (m_pSearchLineEdit->text() != text) {
        m_pSearchLineEdit->setText(text);
    }
    m_pProxyModel->setFilterFixedString(text);
}

void DlgStreaming::onShow() {
    m_pTrackTableView->restoreCurrentViewState();
}

bool DlgStreaming::hasFocus() const {
    return m_pTrackTableView->hasFocus() || m_pSearchLineEdit->hasFocus();
}

void DlgStreaming::setFocus() {
    m_pSearchLineEdit->setFocus();
}

void DlgStreaming::updateLoginUi(bool loggedIn) {
    const bool needsLogin = m_pProvider->requiresLogin();
    m_pLoginButton->setEnabled(needsLogin && !loggedIn);
    m_pLogoutButton->setEnabled(needsLogin && loggedIn);
    m_pSearchButton->setEnabled(!needsLogin || loggedIn);
    m_pSearchLineEdit->setEnabled(!needsLogin || loggedIn);
    if (needsLogin && !loggedIn) {
        m_pStatusLabel->setText(
                tr("Not logged in to %1.").arg(m_pProvider->displayName()));
    } else {
        m_pStatusLabel->setText(
                tr("Search %1 for a track and double-click it to load it to a deck.")
                        .arg(m_pProvider->displayName()));
    }
}

void DlgStreaming::slotLoginClicked() {
    if (m_pProvider->requiresManualToken()) {
        bool ok = false;
        const QString prompt = m_pProvider->manualTokenPrompt();
        const QString title = tr("Log in to %1").arg(m_pProvider->displayName());
        // Ask up to twice: the first attempt may be rejected by the service.
        const QString token = QInputDialog::getMultiLineText(
                this, title, prompt, QString(), &ok);
        if (!ok || token.trimmed().isEmpty()) {
            return;
        }
        m_pStatusLabel->setText(
                tr("Logging in to %1...").arg(m_pProvider->displayName()));
        m_pProvider->submitManualToken(token.trimmed());
        return;
    }
    m_pStatusLabel->setText(
            tr("Requesting a login code from %1...").arg(m_pProvider->displayName()));
    m_pProvider->startLogin();
}

void DlgStreaming::slotLogoutClicked() {
    m_pProvider->logout();
}

void DlgStreaming::slotLoginStarted(
        const QString& userCode,
        const QString& verificationUri,
        int expiresInSeconds) {
    m_pStatusLabel->setText(
            tr("To log in, visit %1 and enter the code %2. The code expires "
               "in %3 seconds.")
                    .arg(verificationUri, userCode)
                    .arg(expiresInSeconds));
    QDesktopServices::openUrl(QUrl(verificationUri));
}

void DlgStreaming::slotLoginFinished(bool success, const QString& error) {
    if (!success) {
        m_pStatusLabel->setText(
                tr("%1 login failed: %2").arg(m_pProvider->displayName(), error));
        QMessageBox::warning(this,
                tr("%1 login failed").arg(m_pProvider->displayName()),
                error);
        updateLoginUi(m_pProvider->hasSession());
    }
}

void DlgStreaming::slotSessionChanged(bool loggedIn) {
    updateLoginUi(loggedIn);
}

void DlgStreaming::slotDownloadProgress(int completed, int total) {
    if (total <= 0) {
        return;
    }
    m_pStatusLabel->setText(
            tr("Downloading track... %1/%2").arg(completed).arg(total));
}

void DlgStreaming::slotQualityChanged(int index) {
    if (index < 0) {
        return;
    }
    m_pProvider->setQualityIndex(m_pQualityComboBox->itemData(index).toInt());
}

void DlgStreaming::slotSearch() {
    if (m_pProvider->requiresLogin() && !m_pProvider->hasSession()) {
        QMessageBox::information(this,
                tr("Please log in"),
                tr("Please log in to %1 before searching.")
                        .arg(m_pProvider->displayName()));
        return;
    }
    const QString query = m_pSearchLineEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }
    m_pStatusLabel->setText(
            tr("Searching %1 for \"%2\"...").arg(m_pProvider->displayName(), query));
    m_pSearchButton->setEnabled(false);
    m_pProvider->search(query,
            [this](bool ok, const mixxx::streaming::TrackList& tracks,
                    const QString& error) {
                m_pSearchButton->setEnabled(true);
                if (!ok) {
                    m_pStatusLabel->setText(tr("Search failed: %1").arg(error));
                    return;
                }
                m_pTrackModel->setTracks(tracks);
                m_pStatusLabel->setText(tr("Found %1 tracks.").arg(tracks.size()));
            });
}

void DlgStreaming::slotLoadTrack(TrackPointer pTrack) {
    // Double-click always loads to the first available deck via
    // loadTrackToPlayer; there is no meaningful "load" without a target.
    resolveAndLoad(pTrack, QString());
}

void DlgStreaming::slotLoadTrackToPlayer(TrackPointer pTrack, const QString& group) {
    resolveAndLoad(pTrack, group);
}

void DlgStreaming::resolveAndLoad(
        const TrackPointer& pPlaceholder,
        const QString& group) {
    // Resolve the track from the placeholder URL, falling back to the currently
    // selected row if no URL is available.
    int row = -1;
    if (pPlaceholder) {
        const QString url = pPlaceholder->getURL();
        const QString prefix = m_pProvider->id() + QStringLiteral("://track/");
        if (url.startsWith(prefix)) {
            const QString id = url.mid(prefix.size());
            for (int i = 0; i < m_pTrackModel->rowCount(); ++i) {
                if (m_pTrackModel->trackAtRow(i).id == id) {
                    row = i;
                    break;
                }
            }
        }
    }
    if (row < 0) {
        const QModelIndex current = m_pTrackTableView->currentIndex();
        if (!current.isValid()) {
            return;
        }
        row = m_pProxyModel->mapToSource(current).row();
    }
    if (!m_pTrackModel->hasTrackAtRow(row)) {
        return;
    }
    downloadAndLoad(m_pTrackModel->trackAtRow(row), group);
}

void DlgStreaming::downloadAndLoad(
        const mixxx::streaming::Track& track,
        const QString& group) {
    m_pStatusLabel->setText(
            tr("Downloading \"%1\" from %2...")
                    .arg(track.title, m_pProvider->displayName()));
    m_pSearchButton->setEnabled(false);

    m_pProvider->downloadTrack(track,
            [this, track, group](bool ok, const QUrl& url, const QString& error) {
                m_pSearchButton->setEnabled(true);
                if (!ok) {
                    m_pStatusLabel->setText(tr("Failed to stream track: %1").arg(error));
                    return;
                }
                TrackPointer pTrack = trackFromFile(track, url.toLocalFile());
                m_pStatusLabel->setText(tr("Loaded \"%1\".").arg(track.title));
                if (group.isEmpty()) {
                    emit loadTrack(pTrack);
                } else {
#ifdef __STEM__
                    emit loadTrackToPlayer(pTrack,
                            group,
                            mixxx::StemChannelSelection(),
                            false);
#else
                    emit loadTrackToPlayer(pTrack, group, false);
#endif
                }
            });
}

TrackPointer DlgStreaming::trackFromFile(
        const mixxx::streaming::Track& track,
        const QString& filePath) const {
    // Import the downloaded file into the library so that it gets a valid
    // TrackId. This is required for the regular analysis pipeline (waveform,
    // beat grid, BPM and key detection) to pick the track up after it has been
    // loaded to a deck, which in turn enables key shifting and time stretching.
    TrackPointer pTrack =
            m_pTrackCollectionManager->getOrAddTrack(TrackRef::fromFilePath(filePath));
    if (!pTrack) {
        // Fallback: play the file without library integration.
        pTrack = Track::newTemporary(filePath);
    }
    pTrack->setArtist(track.artist);
    pTrack->setTitle(track.title);
    pTrack->setAlbum(track.album);
    pTrack->setYear(track.year);
    if (track.trackNumber > 0) {
        pTrack->setTrackNumber(QString::number(track.trackNumber));
    }
    if (track.durationSec > 0) {
        pTrack->setDuration(static_cast<double>(track.durationSec));
    }
    pTrack->setComment(tr("%1 stream (id %2)")
                               .arg(m_pProvider->displayName(), track.id));
    return pTrack;
}
