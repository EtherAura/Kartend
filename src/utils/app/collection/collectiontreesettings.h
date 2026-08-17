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
  /// Kartend-auh7u: whether the panel sits under the full-width toolbar or
  /// spans the entire window height with the toolbar stopping at its edge.
  /// Default FULL-HEIGHT (user decision 2026-08-17; the v1->v2 settings
  /// migration stamps the same onto existing collections) — the navigation
  /// sidebar reads as a full-height file-manager-style panel, unlike the
  /// details pane whose default stays below-toolbar.
  SidebarJustification treeJustification = SidebarJustification::FullHeight;
  /// Panel width in px, set by dragging the panel's inner-edge grip
  /// (Kartend-ob1c9.1 follow-on; user request 2026-08-17). Per-collection
  /// like the rest of the block. The persistence layer clamps to
  /// [kMinWidth, kMaxWidth] so a hand-edited INI can't collapse the panel
  /// to nothing or push the content view off-screen.
  int treeWidth = 240;

  static constexpr int kMinWidth = 140;
  static constexpr int kMaxWidth = 600;

  /// Icons-only mode (user request 2026-08-17): rows whose icon resolves show
  /// ONLY the icon (name demoted to a tooltip); rows with no icon keep their
  /// text — a blank unlabelled row would be unusable, so text is the fallback,
  /// not a casualty.
  bool treeIconsOnly = false;
  /// Branch connector lines (user request 2026-08-17). OFF by default —
  /// the chevrons alone carry the structure; the lines are visual noise
  /// most themes are better without. Chevrons stay either way.
  bool treeShowLines = false;
  /// Row icon edge length in px (user request 2026-08-17). Clamped by the
  /// persistence layer like treeWidth.
  int treeIconSize = 16;

  static constexpr int kMinIconSize = 12;
  static constexpr int kMaxIconSize = 64;

  /// Icon rendering style (user request 2026-08-17): as-is, a fixed
  /// dark/light monochrome silhouette, or tinted. Tinted (accent-coloured,
  /// luminance-preserving) became the DEFAULT the same day — colour logos
  /// clash with per-collection theming; the v2->v3 migration stamps it
  /// onto existing sections still on 'normal'.
  TreeIconStyle treeIconStyle = TreeIconStyle::Tinted;
  /// Tint colour for TreeIconStyle::Tinted, as a hex string. EMPTY means
  /// "the collection's accent colour" (the same primary colour the rest of
  /// the chrome re-themes with) — the requested default.
  QString treeIconTintColor;

  bool operator==(const CollectionTreeSettings &other) const = default;
};

#endif // COLLECTIONTREESETTINGS_H
