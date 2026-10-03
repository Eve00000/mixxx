#include "library/tidal/tidalfeature.h"

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/tidal/dlgtidal.h"
#include "library/tidal/tidalclient.h"
#include "library/tidal/tidalmimedata.h"
#include "library/treeitem.h"
#include "moc_tidalfeature.cpp"
#include "track/track.h"
#include "widget/wlibrary.h"

namespace {
const QString kViewName = QStringLiteral("TIDAL");
} // anonymous namespace

TidalFeature::TidalFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("tidal")),
          m_pTidalClient(new mixxx::tidal::TidalClient(pConfig, this)),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pView(nullptr) {
    // The sidebar of this feature is a single, non-expandable entry. The
    // actual search UI is shown in the library view.
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);
    pRootItem->appendChild(tr("Search"));
    m_pSidebarModel->setRootItem(std::move(pRootItem));

    // Allow dragging TIDAL tracks onto a deck. The callback captures this
    // feature; it is cleared again in the destructor.
    mixxx::tidal::setTidalDropCallback(
            [this](const QList<mixxx::tidal::TidalTrack>& tracks,
                    const QString& group) {
                if (tracks.isEmpty()) {
                    return;
                }
                if (m_pView) {
                    m_pView->downloadAndLoad(tracks.first(), group);
                } else {
                    // The view has not been created yet, so download here and
                    // emit the load request directly.
                    const auto track = tracks.first();
                    m_pTidalClient->downloadTrack(
                            track,
                            [this, track, group](bool ok,
                                    const QUrl& url,
                                    const QString& error) {
                                if (!ok) {
                                    qWarning()
                                            << "Failed to stream TIDAL track:"
                                            << error;
                                    return;
                                }
                                TrackPointer pTrack = Track::newTemporary(url.toLocalFile());
                                pTrack->setArtist(track.artist);
                                pTrack->setTitle(track.title);
                                pTrack->setAlbum(track.album);
#ifdef __STEM__
                                emit loadTrackToPlayer(pTrack,
                                        group,
                                        mixxx::StemChannelSelection(),
                                        false);
#else
                                emit loadTrackToPlayer(pTrack, group, false);
#endif
                            });
                }
            });
}

TidalFeature::~TidalFeature() {
    mixxx::tidal::setTidalDropCallback(nullptr);
}

QVariant TidalFeature::title() {
    return QVariant(tr("TIDAL"));
}

TreeItemModel* TidalFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void TidalFeature::bindLibraryWidget(
        WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    // The view is deleted by the library widget.
    m_pView = new DlgTidal(
            pLibraryWidget, m_pConfig, m_pLibrary, pKeyboard, m_pTidalClient);
    pLibraryWidget->registerView(kViewName, m_pView);
    connect(m_pView,
            &DlgTidal::loadTrack,
            this,
            &TidalFeature::loadTrack);
    connect(m_pView,
            &DlgTidal::loadTrackToPlayer,
            this,
            &TidalFeature::loadTrackToPlayer);
}

void TidalFeature::activate() {
    emit switchToView(kViewName);
    emit enableCoverArtDisplay(false);
    emit disableSearch();
}
