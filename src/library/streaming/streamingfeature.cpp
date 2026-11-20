#include "library/streaming/streamingfeature.h"

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/streaming/dlgstreaming.h"
#include "library/streaming/streamingmimedata.h"
#include "library/treeitem.h"
#include "moc_streamingfeature.cpp"
#include "track/track.h"
#include "widget/wlibrary.h"

StreamingFeature::StreamingFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig,
        mixxx::streaming::Provider* pProvider)
        : LibraryFeature(pLibrary, pConfig, pProvider->iconName()),
          m_pProvider(pProvider),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pView(nullptr) {
    // The sidebar of this feature is a single, non-expandable entry. The
    // actual search UI is shown in the library view.
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);
    pRootItem->appendChild(tr("Search"));
    m_pSidebarModel->setRootItem(std::move(pRootItem));

    // Allow dragging this provider's tracks onto a deck. The handler captures
    // this feature; it is cleared again in the destructor.
    const QString providerId = m_pProvider->id();
    mixxx::streaming::registerDropHandler(
            providerId,
            [this](const mixxx::streaming::TrackList& tracks, const QString& group) {
                if (tracks.isEmpty()) {
                    return;
                }
                if (m_pView) {
                    m_pView->downloadAndLoad(tracks.first(), group);
                } else {
                    // The view has not been created yet, so download here and
                    // emit the load request directly.
                    const auto track = tracks.first();
                    m_pProvider->downloadTrack(
                            track,
                            [this, track, group](bool ok,
                                    const QUrl& url,
                                    const QString& error) {
                                if (!ok) {
                                    qWarning()
                                            << "Failed to stream track:"
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

StreamingFeature::~StreamingFeature() {
    mixxx::streaming::unregisterDropHandler(m_pProvider->id());
}

QString StreamingFeature::viewName() const {
    return QStringLiteral("STREAMING_") + m_pProvider->id().toUpper();
}

QVariant StreamingFeature::title() {
    return QVariant(m_pProvider->displayName());
}

TreeItemModel* StreamingFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void StreamingFeature::bindLibraryWidget(
        WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    // The view is deleted by the library widget.
    m_pView = new DlgStreaming(
            pLibraryWidget, m_pConfig, m_pLibrary, pKeyboard, m_pProvider);
    pLibraryWidget->registerView(viewName(), m_pView);
    connect(m_pView,
            &DlgStreaming::loadTrack,
            this,
            &StreamingFeature::loadTrack);
    connect(m_pView,
            &DlgStreaming::loadTrackToPlayer,
            this,
            &StreamingFeature::loadTrackToPlayer);
}

void StreamingFeature::activate() {
    emit switchToView(viewName());
    emit enableCoverArtDisplay(false);
    emit disableSearch();
}
