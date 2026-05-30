#include "view_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace ViewSettingsPersistence {

void load(QSettings &settings, ViewSettings &opts) {
  // Read raw; the caller coerces an out-of-range int back to NameAscending.
  opts.sortMode = static_cast<SortMode>(
      settings.value(keys::kSortMode, static_cast<int>(SortMode::NameAscending)).toInt());
  opts.excludeSubfoldersFromSort = settings.value(keys::kExcludeSubfoldersFromSort, false).toBool();
  opts.listCollectionColumnWidth = settings.value(keys::kListCollectionColumnWidth, 150).toInt();
  opts.listArtworkColumnWidth = settings.value(keys::kListArtworkColumnWidth, 32).toInt();
  opts.collectionTypeFilter =
      settings.value(keys::kCollectionTypeFilter, QString()).toString().trimmed();
  opts.hideSubcollectionTiles = settings.value(keys::kHideSubcollectionTiles, false).toBool();
  opts.showTitleInPlaceholder = settings.value(keys::kShowTitleInPlaceholder, false).toBool();
  opts.showMenuBar = settings.value(keys::kShowMenuBar, true).toBool();
  opts.showToolbar = settings.value(keys::kShowToolbar, true).toBool();
  opts.fullscreen = settings.value(keys::kFullscreen, false).toBool();
}

void save(QSettings &settings, const ViewSettings &opts) {
  settings.setValue(keys::kSortMode, static_cast<int>(opts.sortMode));
  settings.setValue(keys::kExcludeSubfoldersFromSort, opts.excludeSubfoldersFromSort);
  settings.setValue(keys::kListCollectionColumnWidth, opts.listCollectionColumnWidth);
  settings.setValue(keys::kListArtworkColumnWidth, opts.listArtworkColumnWidth);
  settings.setValue(keys::kCollectionTypeFilter, opts.collectionTypeFilter);
  settings.setValue(keys::kHideSubcollectionTiles, opts.hideSubcollectionTiles);
  settings.setValue(keys::kShowTitleInPlaceholder, opts.showTitleInPlaceholder);
  settings.setValue(keys::kShowMenuBar, opts.showMenuBar);
  settings.setValue(keys::kShowToolbar, opts.showToolbar);
  settings.setValue(keys::kFullscreen, opts.fullscreen);
}

} // namespace ViewSettingsPersistence
