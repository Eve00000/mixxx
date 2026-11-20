#pragma once

#include "library/libraryfeature.h"
#include "library/streaming/streamingprovider.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class Library;
class TreeItemModel;
class WLibrary;
class KeyboardEventFilter;
class DlgStreaming;

/// Library feature that adds a single streaming provider to the sidebar.
///
/// Each provider gets its own instance (its own sidebar entry, icon and view),
/// but all share the generic DlgStreaming view and StreamingTrackListModel via
/// the Provider interface.
class StreamingFeature final : public LibraryFeature {
    Q_OBJECT

  public:
    StreamingFeature(
            Library* pLibrary,
            UserSettingsPointer pConfig,
            mixxx::streaming::Provider* pProvider);
    ~StreamingFeature() override;

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;
    void bindLibraryWidget(
            WLibrary* pLibraryWidget,
            KeyboardEventFilter* pKeyboard) override;

  public slots:
    void activate() override;

  private:
    QString viewName() const;

    mixxx::streaming::Provider* m_pProvider;
    TreeItemModel* m_pSidebarModel;
    DlgStreaming* m_pView;
};
