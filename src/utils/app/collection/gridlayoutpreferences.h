#ifndef KARTEND_UTILS_APP_COLLECTION_GRIDLAYOUTPREFERENCES_H
#define KARTEND_UTILS_APP_COLLECTION_GRIDLAYOUTPREFERENCES_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 8).
// Carrier for the per-collection grid / item-layout cluster. Kept in its
// own translation-unit-input so the layout-calculator code paths can
// take a `const GridLayoutPreferences &` without dragging in
// CollectionConfig + the rest of UIConstants. collectionutils.h
// re-includes this header so existing callers compile unchanged.

#include <QHash>

#include "../collectiontypes.h"

#include <uiconstants/grid.h>
#include <uiconstants/item.h>

/// Per-collection grid / item-layout cluster. Extracted from CollectionConfig
/// (Kartend-r4x5 follow-up: grid+layout). Owns the items-per-row / per-column
/// fields (including the sidebar-hidden alternates), grid spacing, item box
/// dimensions + font + corner radius, and the per-axis scrollbar modes. Members
/// keep their legacy names so existing INI + kart-manifest keys round-trip
/// unchanged; access as `cfg.gridLayout.itemWidth` etc.
struct GridLayoutPreferences {
  int gridWidth = 4;
  /// per-collection items-per-column for the Horizontal view
  /// mode. 0 means "fall back to gridWidth" so existing collections that
  /// upgrade and try Horizontal mode still get a sensible default. Floored
  /// to MIN_WIDTH for any non-zero value at save time — no upper cap, so
  /// extra-wide layouts on 4K/8K displays are permitted.
  int horizontalGridHeight = 0;
  /// alternate items-per-row applied when the sidebar is hidden
  /// AND the active sidebar mode actually shrinks the grid (Expand). 0 means
  /// "inherit gridWidth" so existing collections behave unchanged. Overlay
  /// mode always uses gridWidth regardless of sidebar visibility, since the
  /// floating sidebar doesn't reduce the grid's available area.
  int gridWidthSidebarHidden = 0;
  /// alternate items-per-column for Horizontal view, applied when
  /// the sidebar is hidden in Expand mode. 0 means "inherit
  /// horizontalGridHeight" (which itself falls back to gridWidth when 0).
  int horizontalGridHeightSidebarHidden = 0;
  /// alternate vertical-axis grid override applied when a
  /// Top/Bottom-docked details pane hides in Expand mode. Reserved for views
  /// that have a meaningful items-per-column dimension (e.g. Horizontal); 0
  /// means "no override" so existing layouts are unaffected. Sibling to
  /// gridWidthSidebarHidden but for the vertical-shrink case. Persisted but
  /// not yet consumed by the layout calculator — exposed here so callers can
  /// round-trip the value through INI and the kart manifest.
  int gridHeightSidebarHidden = 0;
  int horizontalSpacing = UIConstants::Grid::SPACING;
  int verticalSpacing = 20;
  /// Scrollbar policy per axis (user request 2026-08-19). These were plain
  /// hide-yes/no bools until Autohide needed a third state. The INI KEYS keep
  /// their old "hide…" names so an existing config migrates itself — the
  /// readers accept the legacy "true"/"false" — which is why the key and the
  /// field no longer read quite the same.
  ScrollbarMode horizontalScrollbarMode = ScrollbarMode::Show;
  ScrollbarMode verticalScrollbarMode = ScrollbarMode::Show;
  int itemWidth = UIConstants::Item::DEFAULT_WIDTH;
  int itemHeight = UIConstants::Item::DEFAULT_HEIGHT;
  int fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  int cornerRadius = UIConstants::Item::DEFAULT_CORNER_RADIUS;

  bool operator==(const GridLayoutPreferences &other) const {
    return gridWidth == other.gridWidth && horizontalGridHeight == other.horizontalGridHeight &&
           gridWidthSidebarHidden == other.gridWidthSidebarHidden &&
           horizontalGridHeightSidebarHidden == other.horizontalGridHeightSidebarHidden &&
           gridHeightSidebarHidden == other.gridHeightSidebarHidden &&
           horizontalSpacing == other.horizontalSpacing &&
           verticalSpacing == other.verticalSpacing &&
           horizontalScrollbarMode == other.horizontalScrollbarMode &&
           verticalScrollbarMode == other.verticalScrollbarMode && itemWidth == other.itemWidth &&
           itemHeight == other.itemHeight && fontSize == other.fontSize &&
           cornerRadius == other.cornerRadius;
  }
  bool operator!=(const GridLayoutPreferences &other) const { return !(*this == other); }
};

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a):
// SettingsManager keeps a per-collection hash of each leaf cluster instead of a
// full by-value snapshot, so a save no longer deep-copies every embedded
// container. MUST hash exactly the fields operator== compares — if the two ever
// drift, an equal config could fingerprint differently (a spurious refresh) or,
// worse, a real change could go unnoticed. Keep the member list in lockstep with
// operator== above. Scoped enums are cast to int because qHash(Enum) only exists
// since Qt 6.5 and CI pins Qt 6.4.2.
inline size_t qHash(const GridLayoutPreferences &key, size_t seed = 0) {
  return qHashMulti(seed, key.gridWidth, key.horizontalGridHeight, key.gridWidthSidebarHidden,
                    key.horizontalGridHeightSidebarHidden, key.gridHeightSidebarHidden,
                    key.horizontalSpacing, key.verticalSpacing,
                    static_cast<int>(key.horizontalScrollbarMode),
                    static_cast<int>(key.verticalScrollbarMode), key.itemWidth, key.itemHeight,
                    key.fontSize, key.cornerRadius);
}

#endif
