#ifndef COLLECTIONTREESETTINGS_H
#define COLLECTIONTREESETTINGS_H

#include "collectiontypes.h"

/// Per-collection state for the collection tree panel (Kartend-ob1c9) — the
/// left/right dockable, hidable tree navigator in the main window. Grouped
/// as one struct for the same reason SidebarAppearance is: members are
/// accessed as `cfg.collectionTree.treeVisible` etc., and the block
/// round-trips through settingsmanagercollections as a unit.
///
/// NOT to be confused with the details pane ("sidebar" throughout the
/// codebase, SidebarAppearance) — the tree panel is a separate piece of
/// chrome with deliberately similar per-collection semantics: each
/// collection remembers whether the tree was visible and which side it
/// docked on while browsing it.
struct CollectionTreeSettings {
  /// Panel shown while this collection is active. Defaults ON — the panel
  /// is the feature; hiding it is the per-collection opt-out.
  bool treeVisible = true;
  /// Dock side. Only Left and Right are meaningful for the tree; the
  /// persistence layer clamps anything else back to Left, and reuses the
  /// shared DetailsPanePosition enum so "like the other sidebar" stays
  /// literally true in the INI vocabulary.
  DetailsPanePosition treePosition = DetailsPanePosition::Left;

  bool operator==(const CollectionTreeSettings &other) const = default;
};

#endif // COLLECTIONTREESETTINGS_H
