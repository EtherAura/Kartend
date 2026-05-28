#include "folderbrowsingoptions_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace FolderBrowsingOptionsPersistence {

void load(QSettings &settings, FolderBrowsingOptions &opts) {
  opts.includeContentSubfolders = settings.value(keys::kIncludeContentSubfolders, false).toBool();
  opts.includeArtworkSubfolders = settings.value(keys::kIncludeArtworkSubfolders, false).toBool();
  opts.showAllSubfolderItems = settings.value(keys::kShowAllSubfolderItems, false).toBool();
  opts.hideSubfolderTitles = settings.value(keys::kHideSubfolderTitles, false).toBool();
  opts.showHiddenFolders = settings.value(keys::kShowHiddenFolders, false).toBool();
}

void save(QSettings &settings, const FolderBrowsingOptions &opts) {
  settings.setValue(keys::kIncludeContentSubfolders, opts.includeContentSubfolders);
  settings.setValue(keys::kIncludeArtworkSubfolders, opts.includeArtworkSubfolders);
  settings.setValue(keys::kShowAllSubfolderItems, opts.showAllSubfolderItems);
  settings.setValue(keys::kHideSubfolderTitles, opts.hideSubfolderTitles);
  settings.setValue(keys::kShowHiddenFolders, opts.showHiddenFolders);
}

} // namespace FolderBrowsingOptionsPersistence
