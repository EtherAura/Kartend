#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONCONTEXT_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONCONTEXT_H

// Runtime context bundle extracted from collectionutils.h (Kartend-0yz3
// step 12). CollectionContext carries the "what view is the user looking
// at right now" snapshot: the current collection index + its config, the
// active artwork dir, the file paths / names currently loaded, the sort
// mode, plus the precomputed descendants/UUID/directory maps the
// CollectionHierarchyCache hydrates per navigation entry. Lives in its
// own translation-unit-input so the QueryManager / scroll-pipeline /
// search code paths can take a `const CollectionContext &` without
// pulling in GeneralSettings + CollectionHierarchyCache.

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include "../collectiontypes.h"
#include "collectionconfig.h"

struct CollectionContext {
  int currentIndex = -1;
  CollectionConfig config;
  QString artworkDirectory;
  QStringList filePaths;
  QHash<QString, QString> fileNames;
  SortMode sortMode = SortMode::NameAscending; // Sort mode for this view
  bool excludeSubfoldersFromSort = false;      // Exclude subfolders/subcollections from sorting

  // Query-only scope controls (do not change UI behavior):
  // - queryIncludeDescendants: include descendants even if the collection's
  //   showAllSubcollectionItems is false.
  // - queryIncludeAllCollections: include all collections in DB queries.
  bool queryIncludeDescendants = false;
  bool queryIncludeAllCollections = false;

  // Pre-computed descendant indices from CollectionHierarchyCache.
  // When populated, QueryManager uses these directly instead of computing
  // descendants via O(n²) tree traversal. This provides O(1) access for
  // large collection hierarchies (e.g., 3000+ subcollections).
  QList<int> precomputedDescendants;

  // Pre-computed UUIDs for current collection + all descendants.
  // Eliminates repeated PathUtils::validateAndExpandPath (filesystem exists()
  // checks) and CollectionUtils::computeCollectionUuid (SHA1 hash) calls
  // during search queries. Computed once during cache rebuild.
  QStringList precomputedDescendantUuids;

  // Pre-computed directory maps for current collection + all descendants.
  // Maps UUID → media directory and UUID → artwork directory.
  // Eliminates repeated path expansion during range loading.
  QHash<QString, QString> precomputedUuidToMediaDir;
  QHash<QString, QString> precomputedUuidToArtworkDir;
  QHash<QString, int> precomputedUuidToCollectionIndex;

  // Optional overrides for UI-only composition.
  // Used for search UX: show only matching subcollections and/or suppress
  // virtual folders without changing collection config or DB query behavior.
  bool hasSubcollectionOverride = false;
  QList<int> subcollectionOverride;
  bool suppressVirtualFolders = false;

  // ─── Collection categorization filters ───────────────────────
  // Mirrored from GeneralSettings on every navigation entry so the scroll
  // pipeline can drop subcollection tiles whose effective type doesn't match
  // the active filter, or hide them entirely. Empty filter == show all.
  QString collectionTypeFilter;
  bool hideSubcollectionTiles = false;

  // Synthetic "Home" view that renders every root collection (parent == -1) as
  // a tile grid with no host collection of its own. When set, currentIndex is
  // -1 and the tile list comes from subcollectionOverride. No media items are
  // queried.
  bool isRootView = false;

  [[nodiscard]] bool isValid() const { return currentIndex >= 0 || isRootView; }
};

// Kartend-1iq9: registered here (instead of in the collectionutils.h
// umbrella) so callers that take CollectionContext through QVariant /
// queued signals only pull in this leaf header.
Q_DECLARE_METATYPE(CollectionContext)

#endif
