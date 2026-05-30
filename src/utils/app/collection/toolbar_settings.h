#ifndef KARTEND_UTILS_APP_COLLECTION_TOOLBAR_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_TOOLBAR_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Per-button
// visibility flags + text overrides for the items-page top bar. Empty *Text
// strings mean "use the .ui default" so existing installs see no visual
// change after upgrade.

#include <QString>

struct ToolbarSettings {
  bool toolbarShowGridViewButton = true;
  bool toolbarShowListViewButton = true;
  bool toolbarShowCoverFlowViewButton = true;
  bool toolbarShowHorizontalViewButton = true;
  bool toolbarShowHideSubcollectionsButton = true;
  bool toolbarShowTypeFilter = true;
  bool toolbarShowTitleFilter = true;
  bool toolbarShowSearchModeButton = true;
  bool toolbarShowSearchBar = true;
  QString toolbarGridViewButtonText;
  QString toolbarListViewButtonText;
  QString toolbarCoverFlowViewButtonText;
  QString toolbarHorizontalViewButtonText;
  QString toolbarHideSubcollectionsButtonText;
  QString toolbarTitleFilterText;
};

#endif // KARTEND_UTILS_APP_COLLECTION_TOOLBAR_SETTINGS_H
