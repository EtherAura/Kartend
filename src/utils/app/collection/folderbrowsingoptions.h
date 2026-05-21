#ifndef KARTEND_UTILS_APP_COLLECTION_FOLDERBROWSINGOPTIONS_H
#define KARTEND_UTILS_APP_COLLECTION_FOLDERBROWSINGOPTIONS_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 3).
// Carrier for the per-collection virtual-folder browsing toggles + the
// runtime subfolder cursor. Kept in its own translation-unit-input so
// callers that only branch on these flags don't drag in CollectionConfig
// + UIConstants. collectionutils.h re-includes this header so existing
// callers compile unchanged.

#include <QString>

/// Virtual-folder browsing options + the runtime cursor for the currently
/// active subfolder. Currently-active subfolder lives here so the cluster
/// owns one cohesive piece of state — the persisted toggles and the
/// runtime cursor are accessed together by the navigation/scroll paths.
/// INI keys + kart-manifest JSON keys round-trip unchanged.
struct FolderBrowsingOptions {
  /// Show subfolders under mediaDirectory as virtual navigable folders.
  bool includeContentSubfolders = false;
  /// Match artwork from subfolders of artworkDirectory.
  bool includeArtworkSubfolders = false;
  /// Mix subfolder items with parent (like showAllSubcollectionItems).
  bool showAllSubfolderItems = false;
  /// Hide titles on virtual folder widgets.
  bool hideSubfolderTitles = false;
  /// Show hidden folders (names starting with a dot).
  bool showHiddenFolders = false;

  /// Runtime-only: current virtual subfolder path (relative to
  /// mediaDirectory). NOT persisted — reset to empty on collection load.
  QString currentSubfolder;

  bool operator==(const FolderBrowsingOptions &other) const {
    return includeContentSubfolders == other.includeContentSubfolders &&
           includeArtworkSubfolders == other.includeArtworkSubfolders &&
           showAllSubfolderItems == other.showAllSubfolderItems &&
           hideSubfolderTitles == other.hideSubfolderTitles &&
           showHiddenFolders == other.showHiddenFolders &&
           currentSubfolder == other.currentSubfolder;
  }
  bool operator!=(const FolderBrowsingOptions &other) const { return !(*this == other); }
};

#endif
