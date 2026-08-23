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
  /// Whether the panel takes layout width or floats above the content
  /// (user request 2026-08-20: "i want to allow it to overlap without moving
  /// the grid at all. navigation side bar needs to function the same").
  ///
  /// Expand — today's behaviour — docks the panel into the row, so opening
  /// or resizing it moves the items viewport's ORIGIN and every item with
  /// it. No amount of grid-side position holding can compensate for that:
  /// the grid is clipped to a viewport that itself moved.
  ///
  /// Overlay floats the panel above the content instead. The viewport keeps
  /// its full geometry, so the grid does not move at all and the panel
  /// simply covers whatever is beneath it.
  ///
  /// Defaults to Expand so no existing collection's layout changes; this is
  /// opt-in per collection, exactly like the details pane's own mode.
  DetailsPaneMode treeMode = DetailsPaneMode::Expand;
  /// Panel width in px, set by dragging the panel's inner-edge grip
  /// (Kartend-ob1c9.1 follow-on; user request 2026-08-17). Per-collection
  /// like the rest of the block. The persistence layer clamps to
  /// [kMinWidth, kMaxWidth] so a hand-edited INI can't collapse the panel
  /// to nothing or push the content view off-screen.
  int treeWidth = 240;

  static constexpr int kMinWidth = 140;
  static constexpr int kMaxWidth = 600;

  /// What a row with artwork shows (Kartend-j1mtg, user request 2026-08-22:
  /// "we should have an option to just show the text, even if the icon is
  /// available. or, show the text, but make the icon small and next to the
  /// text"). Replaces the treeIconsOnly bool from 2026-08-17.
  ///
  /// TextOnly is the DEFAULT (user request 2026-08-22, revising the
  /// IconAndText default this setting shipped with earlier the same day). A
  /// column of names reads as a list of places to go; once every row carries a
  /// picture the sidebar competes with the artwork grid it sits beside, which
  /// is the thing actually worth looking at. Artwork is still on file and one
  /// setting away.
  ///
  /// Note this default is reached only by a config with NO tree-mode key at
  /// all. An existing install has either the tri-state key or the legacy
  /// treeIconsOnly bool, both of which are honoured on load, so nobody's
  /// icons disappear on upgrade.
  ///
  /// Rows with NO artwork keep their name in every mode; a blank unlabelled
  /// row would be unusable, so text is the fallback, not a casualty.
  TreeIconDisplay treeIconDisplay = TreeIconDisplay::TextOnly;
  /// Scroll a row's name horizontally when it does not fit (user request
  /// 2026-08-22). It answers a real problem — a narrow sidebar elides long
  /// platform names to a common prefix, "Famicom - Nintendo Enterta…" beside
  /// "Super Famicom - Super Nint…", so the elision hides exactly the part that
  /// tells them apart — but movement in a sidebar is intrusive enough that it
  /// should be asked for rather than arrive unannounced.
  ///
  /// OFF by default (user request 2026-08-22, revising the on-by-default this
  /// shipped with earlier the same day).
  ///
  /// Only rows whose text actually overflows move; everything else is drawn
  /// exactly as before, so a library of short names sees no motion at all.
  bool treeScrollClippedLabels = false;
  /// Scroll a clipped name while the pointer is OVER that row (user request
  /// 2026-08-22). ON by default, and the reason the always-on variant above
  /// can safely default off: pointing at a row is a deliberate "what is this?",
  /// so the movement is asked for, affects exactly one row, and stops the
  /// moment the pointer leaves. Nothing moves unprompted.
  ///
  /// Independent of treeScrollClippedLabels rather than a mode of it — with
  /// both off nothing ever scrolls, with both on the hovered row is simply
  /// already scrolling.
  bool treeScrollClippedLabelsOnHover = true;
  /// Branch connector lines (user request 2026-08-17). OFF by default —
  /// the chevrons alone carry the structure; the lines are visual noise
  /// most themes are better without. Chevrons stay either way.
  bool treeShowLines = false;
  /// Row icon edge length in px (user request 2026-08-17). Clamped by the
  /// persistence layer like treeWidth.
  int treeIconSize = 16;

  static constexpr int kMinIconSize = 12;
  /// Raised from 64 on 2026-08-18: 64 logical px was a sensible ceiling on
  /// a 1080p panel and far too small on a 4K one. The ceiling now only
  /// exists to keep a typo'd config from producing a single row taller
  /// than any screen; pick the size that suits the display.
  static constexpr int kMaxIconSize = 512;

  /// Icon rendering style (user request 2026-08-17): as-is, a fixed
  /// dark/light monochrome silhouette, or tinted. Tinted (accent-coloured,
  /// luminance-preserving) became the DEFAULT the same day — colour logos
  /// clash with per-collection theming; the v2->v3 migration stamps it
  /// onto existing sections still on 'normal'.
  TreeIconStyle treeIconStyle = TreeIconStyle::Tinted;
  /// Monochrome/tinted styles only (user request 2026-08-18): render the
  /// ACTIVE collection's logo in its original colour so the one you are
  /// viewing stands out of the uniform row. Ignored by the Normal style,
  /// where every logo is already in colour.
  bool treeColorizeSelected = false;
  /// Scrollbar policy for the panel (user request 2026-08-19). Has to cover
  /// BOTH mechanisms — the native bars and the overlay handles — because the
  /// overlay forces the native policy off and paints its own handle, so a
  /// policy change alone reads as doing nothing. Wheel and keyboard scrolling
  /// are unaffected in every mode. INI key keeps its legacy
  /// `collectionTreeHideScrollbar` name so existing configs migrate.
  ScrollbarMode treeScrollbarMode = ScrollbarMode::Show;
  /// Tint colour for TreeIconStyle::Tinted, as a hex string. EMPTY means
  /// "the collection's accent colour" (the same primary colour the rest of
  /// the chrome re-themes with) — the requested default.
  QString treeIconTintColor;

  bool operator==(const CollectionTreeSettings &other) const = default;
};

#endif // COLLECTIONTREESETTINGS_H
