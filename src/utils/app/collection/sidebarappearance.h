#ifndef KARTEND_UTILS_APP_COLLECTION_SIDEBARAPPEARANCE_H
#define KARTEND_UTILS_APP_COLLECTION_SIDEBARAPPEARANCE_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 7).
// Carrier for the per-collection metadata-sidebar appearance cluster.
// Kept in its own translation-unit-input so the sidebar / details-pane
// code paths can take a `const SidebarAppearance &` without dragging in
// CollectionConfig + the rest of UIConstants. collectionutils.h
// re-includes this header so existing callers compile unchanged.

#include <QHash>
#include <QString>

#include "../collectiontypes.h"
#include <uiconstants/detailspaneconstants.h>

/// Per-collection sidebar (details-pane) look. Extracted from the
/// CollectionConfig god-struct (Kartend-r4x5 follow-up: sidebar appearance).
/// Owns visibility, dock mode/position, background + pattern, text/accent
/// colors, header/section bubble color+opacity, width/height + lock,
/// active tab, and the per-collection font override. Members keep their
/// legacy names so existing kartend.cfg INI keys and kart-manifest JSON
/// keys (`sidebarMode`, `sidebarPosition`, `sidebarBackgroundColor`,
/// `sidebar_font_family`, …) round-trip unchanged. Accessed as
/// `cfg.sidebar.sidebarMode` — the `sidebar` prefix on member names is
/// preserved to match the persistence keys + the launcher/filter cluster
/// pattern (CollectionFilterPreferences::titleExclusionPatterns,
/// LauncherProfile::launcherPath).
struct SidebarAppearance {
  bool sidebarVisible = false;
  DetailsPaneMode sidebarMode = DetailsPaneMode::Overlay;
  /// sidebar enhancements. Position controls left/right placement;
  /// in Fixed mode this swaps the QHBoxLayout insertion index, in Overlay
  /// mode it swaps the X anchor in positionSidebarOverlay().
  DetailsPanePosition sidebarPosition = DetailsPanePosition::Right;
  /// Kartend-auh7u: vertical extent of the Left/Right Fixed dock — under the
  /// full-width toolbar (default) or spanning the window height with the
  /// toolbar stopping at the pane's edge. Ignored by Overlay mode and by
  /// Top/Bottom positions. Round-trips through the INI AND the kart
  /// manifest JSON (`sidebar_justification`); absent keys default to
  /// BelowToolbar so pre-auh7u bundles import unchanged.
  SidebarJustification sidebarJustification = SidebarJustification::BelowToolbar;
  /// Background rendering mode for the sidebar. Color and Image mirror the
  /// main-view background pattern. Pattern fills with `sidebarBackgroundColor`
  /// (or system Window when blank) and overlays the chosen procedural
  /// pattern tinted with `sidebarPatternColor`.
  DetailsPaneBackgroundType sidebarBackgroundType = DetailsPaneBackgroundType::Color;
  QString sidebarBackgroundColor; // hex; blank falls back to palette(Window)
  QString sidebarBackgroundImage; // path; sanitized via validatePathSecurity on save
  DetailsPanePattern sidebarPattern = DetailsPanePattern::Crosshatch;
  /// 0–100 % opacity multiplier applied to pattern strokes.
  /// Lower values fade the lines into the bg without changing color; users
  /// who add new DetailsPanePattern variants later get a single intensity
  /// knob for free. 50 matches the original sidebar dimming.
  int sidebarPatternIntensity = 50;
  QString sidebarPatternColor; // hex tint overlay painted on top of the pattern
  QString sidebarTextColor;    // hex; blank falls back to palette(WindowText)
  QString sidebarAccentColor;  // hex; blank falls back to palette(highlight)
  /// semi-opaque "bubble" backgrounds drawn behind sidebar
  /// content for readability over patterned/image backgrounds. The color
  /// holds RGB only; per-bubble alpha is the matching `*Opacity` field
  /// (0–255). Blank color falls back to a sensible default derived from
  /// `sidebarAccentColor` / `sidebarBackgroundColor`. Opacity == 0 disables
  /// the bubble even when the color is set.
  QString sidebarHeaderBgColor;
  QString sidebarSectionBgColor;
  /// 0–255 alpha applied to the corresponding bubble bg color. Defaults
  /// chosen so out-of-the-box bubbles are clearly visible without being
  /// fully opaque (which would hide the user's chosen pattern entirely).
  int sidebarHeaderBgOpacity = 200;
  int sidebarSectionBgOpacity = 170;
  /// Preferred sidebar width in pixels. Floored at MIN_WIDTH at apply time;
  /// no upper bound. Defaults to FIXED_WIDTH so existing collections keep
  /// their historical look. When `sidebarWidthLocked` is true the user
  /// cannot drag the inner edge to resize.
  int sidebarWidth = UIConstants::DetailsPane::FIXED_WIDTH;
  /// preferred pane height when docked Top or Bottom. Floored at
  /// MIN_HEIGHT at apply time; no upper bound. Defaults to FIXED_HEIGHT so
  /// a fresh switch to Top/Bottom dock has a sensible size.
  int sidebarHeight = UIConstants::DetailsPane::FIXED_HEIGHT;
  /// name kept as `sidebarWidthLocked` to preserve the existing INI key,
  /// but semantically locks BOTH width drag (L/R) and height drag (T/B).
  /// When true the user cannot drag the inner edge to resize.
  bool sidebarWidthLocked = true;
  /// Which built-in sidebar tab is active. Persisted per collection so users
  /// can keep one collection on the Collection summary tab while another
  /// stays on the per-Item view.
  DetailsPaneTab sidebarActiveTab = DetailsPaneTab::Item;
  /// Per-collection override for the metadata sidebar's text. Empty family /
  /// 0 size = inherit from the application font (which itself respects
  /// GeneralSettings::globalUiFontFamily).
  QString sidebarFontFamily;
  int sidebarFontPointSize = 0;
  /// Scrollbar policy for the pane (user request 2026-08-19). Covers BOTH
  /// mechanisms — the native bars and the overlay handles — because the pane
  /// holds several scroll areas and the overlay paints its own handle over a
  /// natively-disabled bar. Wheel scrolling is unaffected in every mode; this
  /// governs the indicator, never the content. INI key keeps its legacy
  /// `sidebarHideScrollbar` name so existing configs migrate themselves.
  ScrollbarMode sidebarScrollbarMode = ScrollbarMode::Show;

  bool operator==(const SidebarAppearance &other) const {
    return sidebarVisible == other.sidebarVisible && sidebarMode == other.sidebarMode &&
           sidebarPosition == other.sidebarPosition &&
           sidebarBackgroundType == other.sidebarBackgroundType &&
           sidebarBackgroundColor == other.sidebarBackgroundColor &&
           sidebarBackgroundImage == other.sidebarBackgroundImage &&
           sidebarPattern == other.sidebarPattern &&
           sidebarPatternIntensity == other.sidebarPatternIntensity &&
           sidebarPatternColor == other.sidebarPatternColor &&
           sidebarTextColor == other.sidebarTextColor &&
           sidebarAccentColor == other.sidebarAccentColor &&
           sidebarHeaderBgColor == other.sidebarHeaderBgColor &&
           sidebarSectionBgColor == other.sidebarSectionBgColor &&
           sidebarHeaderBgOpacity == other.sidebarHeaderBgOpacity &&
           sidebarSectionBgOpacity == other.sidebarSectionBgOpacity &&
           sidebarWidth == other.sidebarWidth && sidebarHeight == other.sidebarHeight &&
           sidebarWidthLocked == other.sidebarWidthLocked &&
           sidebarActiveTab == other.sidebarActiveTab &&
           sidebarFontFamily == other.sidebarFontFamily &&
           sidebarFontPointSize == other.sidebarFontPointSize &&
           sidebarScrollbarMode == other.sidebarScrollbarMode;
  }
  bool operator!=(const SidebarAppearance &other) const { return !(*this == other); }
};

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a) —
// must hash exactly the fields operator== compares (see gridlayoutpreferences.h
// for the full rationale). Scoped enums are cast to int because qHash(Enum) only
// exists since Qt 6.5 and CI pins Qt 6.4.2.
inline size_t qHash(const SidebarAppearance &key, size_t seed = 0) {
  return qHashMulti(
      seed, key.sidebarVisible, static_cast<int>(key.sidebarMode),
      static_cast<int>(key.sidebarPosition), static_cast<int>(key.sidebarBackgroundType),
      key.sidebarBackgroundColor, key.sidebarBackgroundImage, static_cast<int>(key.sidebarPattern),
      key.sidebarPatternIntensity, key.sidebarPatternColor, key.sidebarTextColor,
      key.sidebarAccentColor, key.sidebarHeaderBgColor, key.sidebarSectionBgColor,
      key.sidebarHeaderBgOpacity, key.sidebarSectionBgOpacity, key.sidebarWidth, key.sidebarHeight,
      key.sidebarWidthLocked, static_cast<int>(key.sidebarActiveTab), key.sidebarFontFamily,
      key.sidebarFontPointSize, static_cast<int>(key.sidebarScrollbarMode));
}

#endif
