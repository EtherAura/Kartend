#include "toolbar_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace ToolbarSettingsPersistence {

void load(QSettings &settings, ToolbarSettings &opts) {
  opts.toolbarShowGridViewButton = settings.value(keys::kToolbarShowGridViewButton, true).toBool();
  opts.toolbarShowListViewButton = settings.value(keys::kToolbarShowListViewButton, true).toBool();
  opts.toolbarShowCoverFlowViewButton =
      settings.value(keys::kToolbarShowCoverFlowViewButton, true).toBool();
  opts.toolbarShowHorizontalViewButton =
      settings.value(keys::kToolbarShowHorizontalViewButton, true).toBool();
  opts.toolbarShowHideSubcollectionsButton =
      settings.value(keys::kToolbarShowHideSubcollectionsButton, true).toBool();
  opts.toolbarShowTypeFilter = settings.value(keys::kToolbarShowTypeFilter, true).toBool();
  opts.toolbarShowTitleFilter = settings.value(keys::kToolbarShowTitleFilter, true).toBool();
  opts.toolbarShowSearchModeButton =
      settings.value(keys::kToolbarShowSearchModeButton, true).toBool();
  opts.toolbarShowSearchBar = settings.value(keys::kToolbarShowSearchBar, true).toBool();
  opts.toolbarGridViewButtonText =
      settings.value(keys::kToolbarGridViewButtonText, QString()).toString();
  opts.toolbarListViewButtonText =
      settings.value(keys::kToolbarListViewButtonText, QString()).toString();
  opts.toolbarCoverFlowViewButtonText =
      settings.value(keys::kToolbarCoverFlowViewButtonText, QString()).toString();
  opts.toolbarHorizontalViewButtonText =
      settings.value(keys::kToolbarHorizontalViewButtonText, QString()).toString();
  opts.toolbarHideSubcollectionsButtonText =
      settings.value(keys::kToolbarHideSubcollectionsButtonText, QString()).toString();
  opts.toolbarTitleFilterText = settings.value(keys::kToolbarTitleFilterText, QString()).toString();
}

void save(QSettings &settings, const ToolbarSettings &opts) {
  settings.setValue(keys::kToolbarShowGridViewButton, opts.toolbarShowGridViewButton);
  settings.setValue(keys::kToolbarShowListViewButton, opts.toolbarShowListViewButton);
  settings.setValue(keys::kToolbarShowCoverFlowViewButton, opts.toolbarShowCoverFlowViewButton);
  settings.setValue(keys::kToolbarShowHorizontalViewButton, opts.toolbarShowHorizontalViewButton);
  settings.setValue(keys::kToolbarShowHideSubcollectionsButton,
                    opts.toolbarShowHideSubcollectionsButton);
  settings.setValue(keys::kToolbarShowTypeFilter, opts.toolbarShowTypeFilter);
  settings.setValue(keys::kToolbarShowTitleFilter, opts.toolbarShowTitleFilter);
  settings.setValue(keys::kToolbarShowSearchModeButton, opts.toolbarShowSearchModeButton);
  settings.setValue(keys::kToolbarShowSearchBar, opts.toolbarShowSearchBar);
  settings.setValue(keys::kToolbarGridViewButtonText, opts.toolbarGridViewButtonText);
  settings.setValue(keys::kToolbarListViewButtonText, opts.toolbarListViewButtonText);
  settings.setValue(keys::kToolbarCoverFlowViewButtonText, opts.toolbarCoverFlowViewButtonText);
  settings.setValue(keys::kToolbarHorizontalViewButtonText, opts.toolbarHorizontalViewButtonText);
  settings.setValue(keys::kToolbarHideSubcollectionsButtonText,
                    opts.toolbarHideSubcollectionsButtonText);
  settings.setValue(keys::kToolbarTitleFilterText, opts.toolbarTitleFilterText);
}

} // namespace ToolbarSettingsPersistence
