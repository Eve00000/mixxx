#include "library/tidal/dlgtidal.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/proxytrackmodel.h"
#include "library/tidal/tidaltracklistmodel.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgtidal.cpp"
#include "track/track.h"
#include "util/logger.h"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"

namespace {
const mixxx::Logger kLogger("DlgTidal");

const ConfigKey kShowTidalLibraryConfigKey = ConfigKey("[Library]", "ShowTidalLibrary");
} // anonymous namespace

DlgTidal::DlgTidal(
        WLibrary* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        KeyboardEventFilter* pKeyboard,
        mixxx::tidal::TidalClient* pTidalClient)
        : QWidget(parent),
          m_pConfig(std::move(pConfig)),
          m_pLibrary(pLibrary),
          m_pTidalClient(pTidalClient),
          m_pTrackCollectionManager(pLibrary->trackCollectionManager()),
          m_pSearchLineEdit(new QLineEdit(this)),
          m_pSearchButton(new QPushButton(tr("Search"), this)),
          m_pLoginButton(new QPushButton(tr("Log in to TIDAL"), this)),
          m_pLogoutButton(new QPushButton(tr("Log out"), this)),
          m_pStatusLabel(new QLabel(this)),
          m_pTrackTableView(new WTrackTableView(
                  this, m_pConfig, pLibrary, parent->getTrackTableBackgroundColorOpacity())),
          m_pTrackModel(new TidalTrackListModel(this, m_pTrackCollectionManager)),
          m_pProxyModel(new ProxyTrackModel(m_pTrackModel, true)) {
    m_pTrackTableView->installEventFilter(pKeyboard);

    auto* layout = new QVBoxLayout(this);
    auto* searchRow = new QHBoxLayout();
    m_pSearchLineEdit->setPlaceholderText(tr("Search TIDAL for tracks..."));
    searchRow->addWidget(m_pSearchLineEdit);
    searchRow->addWidget(m_pSearchButton);
    searchRow->addWidget(m_pLoginButton);
    searchRow->addWidget(m_pLogoutButton);
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
            &DlgTidal::slotSearch);
    connect(m_pSearchButton,
            &QPushButton::clicked,
            this,
            &DlgTidal::slotSearch);
    connect(m_pLoginButton,
            &QPushButton::clicked,
            this,
            &DlgTidal::slotLoginClicked);
    connect(m_pLogoutButton,
            &QPushButton::clicked,
            this,
            &DlgTidal::slotLogoutClicked);

    connect(m_pTidalClient,
            &mixxx::tidal::TidalClient::deviceLoginStarted,
            this,
            &DlgTidal::slotDeviceLoginStarted);
    connect(m_pTidalClient,
            &mixxx::tidal::TidalClient::deviceLoginFinished,
            this,
            &DlgTidal::slotDeviceLoginFinished);
    connect(m_pTidalClient,
            &mixxx::tidal::TidalClient::sessionChanged,
            this,
            &DlgTidal::slotSessionChanged);
    connect(m_pTidalClient,
            &mixxx::tidal::TidalClient::downloadProgress,
            this,
            &DlgTidal::slotDownloadProgress);

    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            this,
            &DlgTidal::slotLoadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            this,
            &DlgTidal::slotLoadTrackToPlayer);
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

    updateLoginUi(m_pTidalClient->hasSession());
}

DlgTidal::~DlgTidal() {
    // Delete the table view before the models, because the view saves its
    // header state using the model in its destructor.
    delete m_pTrackTableView;
}

void DlgTidal::onSearch(const QString& text) {
    if (m_pSearchLineEdit->text() != text) {
        m_pSearchLineEdit->setText(text);
    }
    m_pProxyModel->setFilterFixedString(text);
}

void DlgTidal::onShow() {
    m_pTrackTableView->restoreCurrentViewState();
}

bool DlgTidal::hasFocus() const {
    return m_pTrackTableView->hasFocus() || m_pSearchLineEdit->hasFocus();
}

void DlgTidal::setFocus() {
    m_pSearchLineEdit->setFocus();
}

void DlgTidal::updateLoginUi(bool loggedIn) {
    m_pLoginButton->setEnabled(!loggedIn);
    m_pLogoutButton->setEnabled(loggedIn);
    m_pSearchButton->setEnabled(loggedIn);
    m_pSearchLineEdit->setEnabled(loggedIn);
    if (loggedIn) {
        m_pStatusLabel->setText(tr("Logged in to TIDAL. Search for a track and "
                                   "double-click it to load it to a deck."));
    } else {
        m_pStatusLabel->setText(tr("Not logged in to TIDAL."));
    }
}

void DlgTidal::slotLoginClicked() {
    m_pStatusLabel->setText(tr("Requesting a login code from TIDAL..."));
    m_pTidalClient->startDeviceLogin();
}

void DlgTidal::slotLogoutClicked() {
    m_pTidalClient->logout();
}

void DlgTidal::slotDeviceLoginStarted(
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

void DlgTidal::slotDeviceLoginFinished(bool success, const QString& error) {
    if (!success) {
        m_pStatusLabel->setText(tr("TIDAL login failed: %1").arg(error));
        QMessageBox::warning(this, tr("TIDAL login failed"), error);
        updateLoginUi(m_pTidalClient->hasSession());
    }
}

void DlgTidal::slotSessionChanged(bool loggedIn) {
    updateLoginUi(loggedIn);
}

void DlgTidal::slotDownloadProgress(int completed, int total) {
    if (total <= 0) {
        return;
    }
    m_pStatusLabel->setText(
            tr("Downloading track... %1/%2 segments").arg(completed).arg(total));
}

void DlgTidal::slotSearch() {
    if (!m_pTidalClient->hasSession()) {
        QMessageBox::information(this,
                tr("Please log in"),
                tr("Please log in to TIDAL before searching."));
        return;
    }
    const QString query = m_pSearchLineEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }
    m_pStatusLabel->setText(tr("Searching TIDAL for \"%1\"...").arg(query));
    m_pSearchButton->setEnabled(false);
    m_pTidalClient->search(query,
            [this](bool ok, const QList<mixxx::tidal::TidalTrack>& tracks,
                    const QString& error) {
                m_pSearchButton->setEnabled(true);
                if (!ok) {
                    m_pStatusLabel->setText(tr("Search failed: %1").arg(error));
                    return;
                }
                m_pTrackModel->setTracks(tracks);
                m_pStatusLabel->setText(
                        tr("Found %1 tracks.").arg(tracks.size()));
            });
}

void DlgTidal::slotLoadTrack(TrackPointer pTrack) {
    Q_UNUSED(pTrack);
    // Double-click always loads to the first available deck via
    // loadTrackToPlayer; there is no meaningful "load" without a target.
    resolveAndLoad(pTrack, QString());
}

void DlgTidal::slotLoadTrackToPlayer(TrackPointer pTrack, const QString& group) {
    resolveAndLoad(pTrack, group);
}
void DlgTidal::resolveAndLoad(
        const TrackPointer& pPlaceholder,
        const QString& group) {
    // Resolve the TIDAL track from the placeholder track URL, falling back to
    // the currently selected row if no URL is available.
    int row = -1;
    if (pPlaceholder) {
        const QString url = pPlaceholder->getURL();
        if (url.startsWith(QLatin1String("tidal://track/"))) {
            bool ok = false;
            const qint64 id = url.mid(QStringLiteral("tidal://track/").size())
                                      .toLongLong(&ok);
            if (ok) {
                for (int i = 0; i < m_pTrackModel->rowCount(); ++i) {
                    if (m_pTrackModel->trackAtRow(i).id == id) {
                        row = i;
                        break;
                    }
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

void DlgTidal::downloadAndLoad(
        const mixxx::tidal::TidalTrack& tidalTrack,
        const QString& group) {
    m_pStatusLabel->setText(tr("Downloading \"%1\" from TIDAL...").arg(tidalTrack.title));
    m_pSearchButton->setEnabled(false);

    m_pTidalClient->downloadTrack(tidalTrack,
            [this, tidalTrack, group](bool ok, const QUrl& url, const QString& error) {
                m_pSearchButton->setEnabled(true);
                if (!ok) {
                    m_pStatusLabel->setText(tr("Failed to stream track: %1").arg(error));
                    return;
                }
                TrackPointer pTrack = trackFromFile(tidalTrack, url.toLocalFile());
                m_pStatusLabel->setText(tr("Loaded \"%1\".").arg(tidalTrack.title));
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

TrackPointer DlgTidal::trackFromFile(
        const mixxx::tidal::TidalTrack& tidalTrack,
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
    pTrack->setArtist(tidalTrack.artist);
    pTrack->setTitle(tidalTrack.title);
    pTrack->setAlbum(tidalTrack.album);
    pTrack->setYear(tidalTrack.year);
    if (tidalTrack.trackNumber > 0) {
        pTrack->setTrackNumber(QString::number(tidalTrack.trackNumber));
    }
    if (tidalTrack.durationSec > 0) {
        pTrack->setDuration(static_cast<double>(tidalTrack.durationSec));
    }
    pTrack->setComment(
            tr("TIDAL stream (id %1)").arg(tidalTrack.id));
    return pTrack;
}
