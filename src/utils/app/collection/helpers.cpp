// Kartend-ysyn: helpers split out of collectionutils.cpp. Hosts the
// non-inline CollectionUtils helpers whose implementation needs QDir
// or other heavy headers — putting them here keeps helpers.h cheap to
// include while still letting the standalone API stay near its inline
// siblings.
#include "helpers.h"

#include <QDir>

namespace CollectionUtils {

int countVirtualFolders(const CollectionConfig &config) {
  // Only count virtual folders if includeContentSubfolders is enabled
  // AND showAllSubfolderItems is false (otherwise items are flattened)
  if (!config.folderBrowsing.includeContentSubfolders ||
      config.folderBrowsing.showAllSubfolderItems) {
    return 0;
  }

  // Determine the effective directory to scan
  QString scanDir = config.mediaDirectory;
  if (!config.folderBrowsing.currentSubfolder.isEmpty()) {
    scanDir = QDir(scanDir).absoluteFilePath(config.folderBrowsing.currentSubfolder);
  }

  QDir dir(scanDir);
  if (!dir.exists()) {
    return 0;
  }

  // Count subdirectories, optionally including hidden folders
  QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
  if (config.folderBrowsing.showHiddenFolders) {
    filters |= QDir::Hidden;
  }
  return dir.entryList(filters).size();
}

} // namespace CollectionUtils
