#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONFILTERPREFERENCES_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONFILTERPREFERENCES_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 6).
// Carrier for the per-collection title-cleanup filter (pattern list +
// master toggle). Kept in its own translation-unit-input so the filter
// code paths can take a `const CollectionFilterPreferences &` without
// dragging in CollectionConfig + UIConstants. collectionutils.h
// re-includes this header so existing callers compile unchanged.

#include <QStringList>

/// Per-collection title-cleanup filter. First sub-struct extracted from the
/// CollectionConfig god-struct (Kartend-r4x5 cluster: filter) — owns the
/// pattern list + the toolbar-driven master toggle that gates pattern
/// application without dropping the user's saved patterns.
///
/// Operator== is value semantics (compare each field). Persistence keys
/// remain `titleExclusionPatterns` / `titleExclusionEnabled` on the parent
/// collection so existing kartend.cfg files round-trip unchanged.
struct CollectionFilterPreferences {
  /// Regex patterns stripped from displayed item titles; processed in
  /// order via QString::remove().
  QStringList titleExclusionPatterns;
  /// Master switch — when false the patterns persist but are not applied.
  bool titleExclusionEnabled = true;

  bool operator==(const CollectionFilterPreferences &other) const {
    return titleExclusionPatterns == other.titleExclusionPatterns &&
           titleExclusionEnabled == other.titleExclusionEnabled;
  }
  bool operator!=(const CollectionFilterPreferences &other) const { return !(*this == other); }
};

#endif
