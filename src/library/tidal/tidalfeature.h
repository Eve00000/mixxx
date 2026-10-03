#pragma once

#include "library/libraryfeature.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class Library;
class TreeItemModel;
class WLibrary;
class KeyboardEventFilter;
class DlgTidal;

namespace mixxx {
namespace tidal {
class TidalClient;
} // namespace tidal
} // namespace mixxx

/// Library feature that adds TIDAL streaming support to Mixxx.
///
/// TIDAL's desktop streams are segmented, unencrypted MPEG-DASH manifests.
/// This feature resolves and caches those segments and loads the resulting
/// file into a deck, so the regular Mixxx engine and analysis can be reused.
class TidalFeature final : public LibraryFeature {
    Q_OBJECT

  public:
    TidalFeature(
            Library* pLibrary,
            UserSettingsPointer pConfig);
    ~TidalFeature() override;

    QVariant title() override;

    TreeItemModel* sidebarModel() const override;

    void bindLibraryWidget(
            WLibrary* pLibraryWidget,
            KeyboardEventFilter* pKeyboard) override;

  public slots:
    void activate() override;

  private:
    mixxx::tidal::TidalClient* m_pTidalClient;
    TreeItemModel* m_pSidebarModel;
    DlgTidal* m_pView;
};
