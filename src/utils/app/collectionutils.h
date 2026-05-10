#ifndef COLLECTIONUTILS_H
#define COLLECTIONUTILS_H

// Standalone enums (HorizontalAlignment, DetailsPane*, BackgroundType,
// ViewType, SortMode) live in collectiontypes.h so files that only need
// the type tags don't pay the cost of including UIConstants, CollectionConfig,
// and the hierarchy cache. This header re-includes that file so existing
// callers compile unchanged.
#include "collectiontypes.h"

#include <algorithm>
#include <QDir>
#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>
// Pull only the four UIConstants sub-namespaces that this header actually
// references in its inline bodies (Grid, Item, ListView, DetailsPane). The
// umbrella <uiconstants.h> aggregates all 29 subheaders (~1015 LOC) — pulling
// it here forced every one of the ~113 TUs that includes collectionutils.h to
// reparse all 29 even though only these four are used. Subheaders are
// self-contained (no includes themselves), so this is a pure preprocessor
// cost cut with no behavioural change.
#include <uiconstants/detailspane.h>
#include <uiconstants/grid.h>
#include <uiconstants/item.h>
#include <uiconstants/listview.h>

// Forward declaration for validation
namespace ErrorUtils {
struct ErrorContext;
}

namespace CollectionUtils {

[[nodiscard]] inline QString alignmentToString(HorizontalAlignment alignment) {
  switch (alignment) {
  case HorizontalAlignment::Left:
    return "left";
  case HorizontalAlignment::Center:
    return "center";
  case HorizontalAlignment::Right:
    return "right";
  default:
    return "center";
  }
}

[[nodiscard]] inline HorizontalAlignment stringToAlignment(const QString &str) {
  QString lower = str.toLower();
  if (lower == "left") return HorizontalAlignment::Left;
  if (lower == "right") return HorizontalAlignment::Right;
  return HorizontalAlignment::Center;
}

[[nodiscard]] inline QString viewTypeToString(ViewType viewType) {
  switch (viewType) {
  case ViewType::List:
    return "list";
  case ViewType::CoverFlow:
    return "coverflow";
  case ViewType::Horizontal:
    return "horizontal";
  case ViewType::Grid:
  default:
    return "grid";
  }
}

[[nodiscard]] inline ViewType stringToViewType(const QString &str) {
  QString lower = str.toLower();
  if (lower == "list") return ViewType::List;
  if (lower == "coverflow") return ViewType::CoverFlow;
  if (lower == "horizontal") return ViewType::Horizontal;
  return ViewType::Grid;
}

[[nodiscard]] inline QString headerLogoPositionToString(HeaderLogoPosition pos) {
  switch (pos) {
  case HeaderLogoPosition::TopLeft:
    return "topleft";
  case HeaderLogoPosition::TopRight:
    return "topright";
  case HeaderLogoPosition::TopCenter:
  default:
    return "topcenter";
  }
}

[[nodiscard]] inline HeaderLogoPosition stringToHeaderLogoPosition(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "topleft") return HeaderLogoPosition::TopLeft;
  if (lower == "topright") return HeaderLogoPosition::TopRight;
  return HeaderLogoPosition::TopCenter;
}

[[nodiscard]] inline QString detailsPanePositionToString(DetailsPanePosition pos) {
  switch (pos) {
  case DetailsPanePosition::Left:
    return "left";
  case DetailsPanePosition::Top:
    return "top";
  case DetailsPanePosition::Bottom:
    return "bottom";
  case DetailsPanePosition::Right:
  default:
    return "right";
  }
}

[[nodiscard]] inline DetailsPanePosition stringToDetailsPanePosition(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "left") return DetailsPanePosition::Left;
  if (lower == "top") return DetailsPanePosition::Top;
  if (lower == "bottom") return DetailsPanePosition::Bottom;
  return DetailsPanePosition::Right;
}

/// true for Top/Bottom dock — tells layout/drag code to treat the
/// pane's height (not width) as the configurable dimension and to span the full
/// viewport perpendicular to the dock edge.
[[nodiscard]] inline bool isDetailsPaneHorizontal(DetailsPanePosition pos) {
  return pos == DetailsPanePosition::Top || pos == DetailsPanePosition::Bottom;
}

[[nodiscard]] inline QString detailsPaneBackgroundTypeToString(DetailsPaneBackgroundType type) {
  switch (type) {
  case DetailsPaneBackgroundType::Image:
    return "image";
  case DetailsPaneBackgroundType::Pattern:
    return "pattern";
  case DetailsPaneBackgroundType::Color:
  default:
    return "color";
  }
}

[[nodiscard]] inline DetailsPaneBackgroundType
stringToDetailsPaneBackgroundType(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "image") return DetailsPaneBackgroundType::Image;
  if (lower == "pattern") return DetailsPaneBackgroundType::Pattern;
  return DetailsPaneBackgroundType::Color;
}

[[nodiscard]] inline QString detailsPanePatternToString(DetailsPanePattern pattern) {
  switch (pattern) {
  case DetailsPanePattern::Crosshatch:
  default:
    return "crosshatch";
  }
}

[[nodiscard]] inline DetailsPanePattern stringToDetailsPanePattern(const QString &str) {
  // Single value for now; future patterns slot in here without persistence breakage.
  Q_UNUSED(str);
  return DetailsPanePattern::Crosshatch;
}

[[nodiscard]] inline QString detailsPaneTabToString(DetailsPaneTab tab) {
  switch (tab) {
  case DetailsPaneTab::Collection:
    return "collection";
  case DetailsPaneTab::File:
    return "file";
  case DetailsPaneTab::Item:
  default:
    return "item";
  }
}

[[nodiscard]] inline DetailsPaneTab stringToDetailsPaneTab(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "collection") return DetailsPaneTab::Collection;
  if (lower == "file") return DetailsPaneTab::File;
  return DetailsPaneTab::Item;
}

} // namespace CollectionUtils

/// a globally-registered, reusable launcher configuration. A
/// LauncherConfig that carries a non-empty `presetId` matching a preset's
/// `id` inherits its name + path + core + parameters from the preset at
/// resolution time (LauncherUtils::resolvePreset). Renaming a preset is
/// safe because references key off the stable `id`, not the user-visible
/// name. Deleting a referenced preset leaves the inline fields as the
/// fallback — typically empty, so the launch surfaces a clear error.
struct LauncherPreset {
  QString id;
  QString name;
  QString launcherPath;
  QString corePath;
  QString launchParameters;

  bool operator==(const LauncherPreset &other) const {
    return id == other.id && name == other.name && launcherPath == other.launcherPath &&
           corePath == other.corePath && launchParameters == other.launchParameters;
  }
};

/// one entry in a collection's launcher list. The legacy primary
/// launcher (CollectionConfig::launcherPath/corePath/launchParameters) is
/// always present as launcher index 0; entries in
/// CollectionConfig::additionalLaunchers occupy indices 1..N. `name` is a
/// user-visible label shown in the launcher chooser; empty falls back to the
/// executable basename.
///
/// when `presetId` matches a registered LauncherPreset id, the
/// preset's fields override the inline launcher fields at resolution time.
struct LauncherConfig {
  QString name;
  QString launcherPath;
  QString corePath;
  QString launchParameters;
  /// Optional reference to a globally-registered preset.
  /// When set and the preset exists, all other fields are inherited from
  /// the preset. Empty means "use the inline fields verbatim".
  QString presetId;

  bool operator==(const LauncherConfig &other) const {
    return name == other.name && launcherPath == other.launcherPath && corePath == other.corePath &&
           launchParameters == other.launchParameters && presetId == other.presetId;
  }
};

namespace LauncherUtils {

/// returns true when @p launcherPath points at a libretro
/// frontend (currently RetroArch). The substring check is the single source
/// of truth — callers elsewhere ask "does this launcher take a libretro
/// core?" instead of inspecting the path themselves, so the rest of the
/// codebase stays free of specific emulator names. The match runs against
/// the entire path so "/usr/bin/retroarch", "retroarch.exe", and bare
/// "retroarch" in $PATH all qualify.
[[nodiscard]] inline bool usesLibretroCore(const QString &launcherPath) {
  return launcherPath.contains(QStringLiteral("retroarch"), Qt::CaseInsensitive);
}

/// Returns a LauncherConfig with fields resolved against the preset list.
/// When `lc.presetId` matches a preset, that preset's name/path/core/params
/// replace the inline fields (the preset id stays attached for round-trip
/// persistence). When the preset is missing or `presetId` is empty, the
/// returned config equals `lc`.
[[nodiscard]] LauncherConfig resolvePreset(const LauncherConfig &lc,
                                           const QList<LauncherPreset> &presets);

} // namespace LauncherUtils

struct CollectionConfig {
  QString name;
  /// Free-form category/type tag. Empty for an untagged
  /// collection — subcollections inherit the nearest non-empty ancestor type
  /// for filter purposes (see CollectionUtils::effectiveCollectionType). The
  /// value is just a user-chosen label like "Games" / "Movies" / "Music"; the
  /// app does not interpret it semantically.
  QString type;
  QString launcherPath;
  QString corePath;
  QString launchParameters;
  /// User-visible name for the primary (legacy) launcher. Empty is fine — the
  /// chooser dialog falls back to the executable basename.
  QString launcherName;
  /// Extra launchers beyond the primary, indexed at 1..N in launcher views.

  QList<LauncherConfig> additionalLaunchers;
  /// Index into the unified launcher view (0 = primary, 1..N =
  /// additionalLaunchers[0..N-1]). The chooser dialog pre-selects this entry
  /// when launcherCount() > 1; for single-launcher collections it is
  /// effectively ignored.
  int defaultLauncherIndex = 0;
  QString mediaDirectory;
  QString artworkDirectory;
  QString videoDirectory;
  /// Directory that holds per-item manual files (PDF, EPUB, TXT, etc.).
  /// Files are matched by completeBaseName(), parallel to artworkDirectory
  /// and videoDirectory. Optional; when empty no manual is auto-discovered.
  /// Per-item overrides live in `item_metadata.manual_path`.
  QString manualDirectory;
  QString placeholderArtwork;
  QString collectionIcon;
  QStringList extensions;
  /// User-defined custom artwork type ids beyond the standard set
  /// Each entry is a free-form string the user picks; the
  /// sidebar uses it both as the artwork_type id stored in `item_artwork` and
  /// as the gallery thumbnail label until a friendly-name registry is added.
  /// Auto-discovery does NOT apply to custom types — they only resolve via a
  /// per-item manual override.
  QStringList customArtworkTypes;
  int gridWidth;
  /// per-collection items-per-column for the Horizontal view
  /// mode. 0 means "fall back to gridWidth" so existing collections that
  /// upgrade and try Horizontal mode still get a sensible default. Clamped to
  /// the same [MIN_WIDTH, MAX_WIDTH] range as gridWidth at save time.
  int horizontalGridHeight = 0;
  /// alternate items-per-row applied when the sidebar is hidden
  /// AND the active sidebar mode actually shrinks the grid (Expand). 0 means
  /// "inherit gridWidth" so existing collections behave unchanged. Overlay mode
  /// always uses gridWidth regardless of sidebar visibility, since the floating
  /// sidebar doesn't reduce the grid's available area.
  int gridWidthSidebarHidden = 0;
  /// alternate items-per-column for Horizontal view, applied when
  /// the sidebar is hidden in Expand mode. 0 means "inherit horizontalGridHeight"
  /// (which itself falls back to gridWidth when 0).
  int horizontalGridHeightSidebarHidden = 0;
  /// alternate vertical-axis grid override applied when a
  /// Top/Bottom-docked details pane hides in Expand mode. Reserved for views
  /// that have a meaningful items-per-column dimension (e.g. Horizontal); 0
  /// means "no override" so existing layouts are unaffected. Sibling to
  /// gridWidthSidebarHidden but for the vertical-shrink case. Persisted but
  /// not yet consumed by the layout calculator — exposed here so callers can
  /// round-trip the value through INI and the kart manifest.
  int gridHeightSidebarHidden = 0;
  bool sidebarVisible;
  int parentCollectionIndex = -1;
  bool isSubcollection = false;
  [[nodiscard]] bool hasParent() const { return parentCollectionIndex >= 0; }
  bool showAllSubcollectionItems = false;
  bool hideTitles = false;
  bool hideSubcollectionTitles = false;
  /// Per-collection regex patterns stripped from displayed item titles
  /// One pattern per entry; processed in order via
  /// QString::remove(). Common use is dropping region tags like `\s*\(USA\)`
  /// or revision tags like `\s*\[!\]` so only the canonical title shows in
  /// the grid/list. Patterns are stored verbatim (no escaping) — the user
  /// types regex syntax directly in the toolbar popup. Invalid patterns are
  /// skipped at compile time rather than aborting the whole list.
  QStringList titleExclusionPatterns;
  /// Master switch for the exclusion list. When false the
  /// patterns persist but are not applied — lets the user toggle the cleanup
  /// from the toolbar without losing their pattern list.
  bool titleExclusionEnabled = true;
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Center;
  DetailsPaneMode sidebarMode = DetailsPaneMode::Overlay;
  /// sidebar enhancements. Position controls left/right placement;
  /// in Fixed mode this swaps the QHBoxLayout insertion index, in Overlay mode
  /// it swaps the X anchor in positionSidebarOverlay().
  DetailsPanePosition sidebarPosition = DetailsPanePosition::Right;
  /// Background rendering mode for the sidebar. Color and Image mirror the
  /// main-view background pattern. Pattern fills with `sidebarBackgroundColor`
  /// (or system Window when blank) and overlays the chosen procedural pattern
  /// tinted with `sidebarPatternColor`.
  DetailsPaneBackgroundType sidebarBackgroundType = DetailsPaneBackgroundType::Color;
  QString sidebarBackgroundColor; // hex; blank falls back to palette(Window)
  QString sidebarBackgroundImage; // path; sanitized via validatePathSecurity on save
  DetailsPanePattern sidebarPattern = DetailsPanePattern::Crosshatch;
  /// 0–100 % opacity multiplier applied to pattern strokes.
  /// Lower values fade the lines into the bg without changing color; users
  /// who add new DetailsPanePattern variants later get a single intensity knob
  /// for free. 50 matches the original sidebar dimming.
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
  /// MIN_HEIGHT at apply time; no upper bound. Defaults to FIXED_HEIGHT so a
  /// fresh switch to Top/Bottom dock has a sensible size.
  int sidebarHeight = UIConstants::DetailsPane::FIXED_HEIGHT;
  /// name kept as `sidebarWidthLocked` to preserve the existing
  /// INI key, but semantically locks BOTH width drag (L/R) and height drag
  /// (T/B). When true the user cannot drag the inner edge to resize.
  bool sidebarWidthLocked = true;
  /// Which built-in sidebar tab is active. Persisted per collection so users
  /// can keep one collection on the Collection summary tab while another
  /// stays on the per-Item view.
  DetailsPaneTab sidebarActiveTab = DetailsPaneTab::Item;
  ViewType viewType = ViewType::Grid; // Grid (default) or List view
  /// when true, media items whose artwork lookup returns no
  /// match are hidden from the items page. Subcollections and virtual folders
  /// are unaffected. The filter combines with the active search /
  /// subcollection filter as an additional predicate, so search results are
  /// also pruned to items that have artwork.
  bool hideMissingArtwork = false;
  int horizontalSpacing = UIConstants::Grid::SPACING;
  int verticalSpacing = 20;
  bool hideHorizontalScrollbar = false;
  bool hideVerticalScrollbar = false;
  int itemWidth = UIConstants::Item::DEFAULT_WIDTH;
  int itemHeight = UIConstants::Item::DEFAULT_HEIGHT;
  int fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  int cornerRadius = UIConstants::Item::DEFAULT_CORNER_RADIUS;

  // Background settings
  BackgroundType backgroundType = BackgroundType::Color;
  QString backgroundColor; // Background color (hex like #1a1a2e)
  QString backgroundImage; // Background image path
  /// looping muted background video path. Active only when
  /// backgroundType == Video; empty disables the video and falls back to
  /// the system bg until the user picks a file. Sanitised on save like
  /// backgroundImage.
  QString backgroundVideo;
  QString primaryColor;   // Primary UI color for toolbar, menubar, search bar
  QString tileColor;      // Color for item tiles/placeholders (if blank, uses default)
  QString selectionColor; // Color for selection rectangle and glide overlay border

  /// optional header logo image painted at the top of the items
  /// viewport, distinct from `collectionIcon` (which renders on the
  /// collection-as-tile entry). Empty path disables the overlay. Sanitised
  /// like backgroundImage on save.
  QString headerLogoImage;
  HeaderLogoPosition headerLogoPosition = HeaderLogoPosition::TopCenter;

  /// edge-darkening vignette overlay on the items viewport.
  /// `vignetteIntensity` is the corner darkness percent (0 = no effect, 100
  /// = pitch black at the corners). 60 is a sensible "noticeable but
  /// subtle" default. Independent of background type — works equally well
  /// over color, image, and video bgs.
  bool vignetteEnabled = false;
  int vignetteIntensity = 60;

  /// per-collection wallpaper parallax. When enabled, the
  /// image background scrolls at `parallaxStrength` percent of the items
  /// scroll speed (0 = bg locked / no parallax movement; 100 = bg moves
  /// in lockstep with the content). Image bg only — video bgs paint via
  /// their own widget and are not affected by this toggle.
  bool wallpaperParallax = false;
  int parallaxStrength = 30;

  /// simulated backdrop blur on the items toolbar. When
  /// enabled with an image bg, the wallpaper is pre-blurred (cheap
  /// downscale-upscale) and painted as the toolbar background, mimicking
  /// macOS Vibrancy. `backdropBlurRadius` controls the blur intensity
  /// (higher = more blur). Video bgs are not blurred (per-frame Gaussian
  /// is too expensive without GPU shaders); the toggle is a no-op while a
  /// video bg is active.
  bool toolbarBackdropBlur = false;
  int backdropBlurRadius = 12;

  // Archive extraction for cores that don't support zipped content
  bool extractArchives = false; // Extract archives to temp dir before launch
  QString extractedExtension;   // File extension to launch from extracted archive

  // Expand mode: two-stage activation. When enabled, the first activation
  // (double-click or Enter) on a selected item shows the artwork preview
  // overlay instead of launching; a second activation on the same item
  // launches it. Selection change resets the expanded state.
  bool expandMode = false;

  // Folder browsing options
  bool includeContentSubfolders = false; // Show subfolders as virtual navigable folders
  bool includeArtworkSubfolders = false; // Match artwork from subfolders
  bool showAllSubfolderItems =
      false; // Mix subfolder items with parent (like showAllSubcollectionItems)
  bool hideSubfolderTitles = false; // Hide titles on virtual folder widgets
  bool showHiddenFolders = false;   // Show hidden folders (starting with dot)

  // Virtual subfolder tracking (runtime only, not persisted)
  QString currentSubfolder; // Current virtual subfolder path (relative to
                            // mediaDirectory)

  // List mode settings
  int listFontSize = UIConstants::Item::DEFAULT_FONT_SIZE;       // Font size for list view text
  int listRowHeight = UIConstants::ListView::DEFAULT_ROW_HEIGHT; // Row height in list view
  QString listRowColor;    // Primary row color (if blank, uses system Base)
  QString listAltRowColor; // Alternate row color (if blank, uses system
                           // AlternateBase)

  // Text appearance settings (per-collection)
  QString customFontFamily; // Custom font family (empty = system default)

  // ─── Sidebar font ───────────────────────────────────────────
  // Per-collection override for the metadata sidebar's text. Empty family /
  // 0 size = inherit from the application font (which itself respects
  // GeneralSettings::globalUiFontFamily). Stored alongside the rest of the
  // sidebarXxx appearance fields so a collection's sidebar look is fully
  // self-contained.
  // ───────────────────────────────────────────────────────────────────────────
  QString sidebarFontFamily;
  int sidebarFontPointSize = 0;

  // ─── Playlist support ───────────────────────────────────────
  // Runtime-only marker for synthesized "virtual collection" entries backed by
  // the SQLite `playlists` table instead of an INI section. Synthesised at
  // startup by PlaylistManager and appended to MainWindow::m_collections so
  // playlists nest into the hierarchy / appear as tiles like ordinary
  // subcollections. SettingsManager::saveCollections() MUST skip configs with
  // isPlaylist=true so they never round-trip into kartend.cfg.
  bool isPlaylist = false;
  QString playlistId; // UUID — matches playlists.id when isPlaylist is true.

  // ─── Collection links / alias parents ────────────────────────
  // Names of additional parent collections this collection should appear
  // under (in addition to its primary parentCollectionIndex). Stored as
  // names rather than indices so reorder/rename of the parent list survives
  // round-trips. Resolved to indices on demand by CollectionHierarchyCache::
  // rebuild(). Unknown names — typo'd or referencing a deleted parent —
  // are silently ignored at resolution time so a stale config can't wedge
  // the tree. Self-references (a collection naming itself) are likewise
  // dropped. Empty when the collection has no aliases.
  QStringList additionalParentNames;
  /// Reserved-kind tag for built-in playlists. Empty for
  /// ordinary user-created playlists; "favorites" for the auto-created
  /// favorites slot. The UI uses this to hide the Delete action on built-ins
  /// and to highlight the favorites toggle on items that already belong to
  /// the favorites playlist. Runtime-only, never persisted to INI.
  QString playlistReservedKind;

  CollectionConfig()
      : gridWidth(4), sidebarVisible(false), horizontalAlignment(HorizontalAlignment::Center) {}

  bool operator==(const CollectionConfig &other) const {
    return name == other.name && type == other.type && launcherPath == other.launcherPath &&
           corePath == other.corePath && launchParameters == other.launchParameters &&
           launcherName == other.launcherName && additionalLaunchers == other.additionalLaunchers &&
           defaultLauncherIndex == other.defaultLauncherIndex &&
           mediaDirectory == other.mediaDirectory && artworkDirectory == other.artworkDirectory &&
           videoDirectory == other.videoDirectory && manualDirectory == other.manualDirectory &&
           placeholderArtwork == other.placeholderArtwork &&
           collectionIcon == other.collectionIcon && extensions == other.extensions &&
           customArtworkTypes == other.customArtworkTypes && gridWidth == other.gridWidth &&
           horizontalGridHeight == other.horizontalGridHeight &&
           gridWidthSidebarHidden == other.gridWidthSidebarHidden &&
           horizontalGridHeightSidebarHidden == other.horizontalGridHeightSidebarHidden &&
           gridHeightSidebarHidden == other.gridHeightSidebarHidden &&
           sidebarVisible == other.sidebarVisible &&
           parentCollectionIndex == other.parentCollectionIndex &&
           isSubcollection == other.isSubcollection &&
           showAllSubcollectionItems == other.showAllSubcollectionItems &&
           hideTitles == other.hideTitles &&
           hideSubcollectionTitles == other.hideSubcollectionTitles &&
           titleExclusionPatterns == other.titleExclusionPatterns &&
           titleExclusionEnabled == other.titleExclusionEnabled &&
           horizontalAlignment == other.horizontalAlignment && sidebarMode == other.sidebarMode &&
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
           sidebarActiveTab == other.sidebarActiveTab && viewType == other.viewType &&
           horizontalSpacing == other.horizontalSpacing &&
           verticalSpacing == other.verticalSpacing &&
           hideHorizontalScrollbar == other.hideHorizontalScrollbar &&
           hideVerticalScrollbar == other.hideVerticalScrollbar && itemWidth == other.itemWidth &&
           itemHeight == other.itemHeight && fontSize == other.fontSize &&
           cornerRadius == other.cornerRadius && backgroundType == other.backgroundType &&
           backgroundColor == other.backgroundColor && backgroundImage == other.backgroundImage &&
           backgroundVideo == other.backgroundVideo && primaryColor == other.primaryColor &&
           tileColor == other.tileColor && selectionColor == other.selectionColor &&
           headerLogoImage == other.headerLogoImage &&
           headerLogoPosition == other.headerLogoPosition &&
           vignetteEnabled == other.vignetteEnabled &&
           vignetteIntensity == other.vignetteIntensity &&
           wallpaperParallax == other.wallpaperParallax &&
           parallaxStrength == other.parallaxStrength &&
           toolbarBackdropBlur == other.toolbarBackdropBlur &&
           backdropBlurRadius == other.backdropBlurRadius &&
           extractArchives == other.extractArchives &&
           extractedExtension == other.extractedExtension && expandMode == other.expandMode &&
           includeContentSubfolders == other.includeContentSubfolders &&
           includeArtworkSubfolders == other.includeArtworkSubfolders &&
           showAllSubfolderItems == other.showAllSubfolderItems &&
           hideSubfolderTitles == other.hideSubfolderTitles &&
           showHiddenFolders == other.showHiddenFolders && listFontSize == other.listFontSize &&
           listRowHeight == other.listRowHeight && listRowColor == other.listRowColor &&
           listAltRowColor == other.listAltRowColor && customFontFamily == other.customFontFamily &&
           sidebarFontFamily == other.sidebarFontFamily &&
           sidebarFontPointSize == other.sidebarFontPointSize && isPlaylist == other.isPlaylist &&
           playlistId == other.playlistId && playlistReservedKind == other.playlistReservedKind &&
           additionalParentNames == other.additionalParentNames;
  }

  // ─── Launcher list helpers ───────────────────────────────────
  /// Total number of launchers visible to the user: 1 primary + N additional.
  [[nodiscard]] int launcherCount() const { return 1 + additionalLaunchers.size(); }

  /// Returns the launcher at the unified index. Index 0 is the primary
  /// (synthesized from the legacy fields); index 1..N maps to
  /// additionalLaunchers[0..N-1]. Out-of-range indices return an empty config.
  [[nodiscard]] LauncherConfig launcherAt(int index) const {
    if (index == 0) {
      // Qualify with this-> so the compiler doesn't confuse the legacy fields
      // with the same-named members on LauncherConfig during aggregate init.
      // The trailing empty string is the presetId — the
      // primary launcher slot itself never references a preset.
      return LauncherConfig{this->launcherName, this->launcherPath, this->corePath,
                            this->launchParameters, QString{}};
    }
    const int additionalIndex = index - 1;
    if (additionalIndex < 0 || additionalIndex >= this->additionalLaunchers.size()) {
      return LauncherConfig{};
    }
    return this->additionalLaunchers[additionalIndex];
  }

  /// Friendly display name for a launcher entry. Falls back to the executable
  /// basename when the user-supplied name is empty, and to a numbered label
  /// when the launcher path itself is also empty.
  [[nodiscard]] QString launcherDisplayName(int index) const {
    LauncherConfig launcher = launcherAt(index);
    if (!launcher.name.trimmed().isEmpty()) {
      return launcher.name.trimmed();
    }
    if (!launcher.launcherPath.trimmed().isEmpty()) {
      QString basename = launcher.launcherPath.trimmed();
      int slash = basename.lastIndexOf(QLatin1Char('/'));
      if (slash >= 0) {
        basename = basename.mid(slash + 1);
      }
      return basename;
    }
    return QString::number(index + 1);
  }

  // Validation methods
  [[nodiscard]] bool isValid() const { return !name.isEmpty(); }

  [[nodiscard]] bool hasMediaDirectory() const { return !mediaDirectory.isEmpty(); }

  [[nodiscard]] bool hasArtworkDirectory() const { return !artworkDirectory.isEmpty(); }
  [[nodiscard]] bool hasVideoDirectory() const { return !videoDirectory.isEmpty(); }
  [[nodiscard]] bool hasManualDirectory() const { return !manualDirectory.isEmpty(); }

  [[nodiscard]] bool hasPlaceholderArtwork() const { return !placeholderArtwork.isEmpty(); }

  // Validates numeric fields are within acceptable ranges
  void clampValues() {
    gridWidth = std::clamp(gridWidth, UIConstants::Grid::MIN_WIDTH, UIConstants::Grid::MAX_WIDTH);
    // 0 stays 0 (means "inherit gridWidth"); any non-zero value
    // is clamped to the same range as gridWidth so a hand-edit can't pin the
    // column at 0 or saturate the layout calculation.
    if (horizontalGridHeight != 0) {
      horizontalGridHeight = std::clamp(horizontalGridHeight, UIConstants::Grid::MIN_WIDTH,
                                        UIConstants::Grid::MAX_WIDTH);
    }
    // 0 stays 0 ("inherit primary"); any non-zero alt is clamped
    // to the same valid range so a hand-edit can't pin the grid at 0 columns.
    if (gridWidthSidebarHidden != 0) {
      gridWidthSidebarHidden = std::clamp(gridWidthSidebarHidden, UIConstants::Grid::MIN_WIDTH,
                                          UIConstants::Grid::MAX_WIDTH);
    }
    if (horizontalGridHeightSidebarHidden != 0) {
      horizontalGridHeightSidebarHidden =
          std::clamp(horizontalGridHeightSidebarHidden, UIConstants::Grid::MIN_WIDTH,
                     UIConstants::Grid::MAX_WIDTH);
    }
    // same 0-stays-0 rule for the vertical-shrink override used
    // when a Top/Bottom-docked details pane hides in Expand mode.
    if (gridHeightSidebarHidden != 0) {
      gridHeightSidebarHidden = std::clamp(gridHeightSidebarHidden, UIConstants::Grid::MIN_WIDTH,
                                           UIConstants::Grid::MAX_WIDTH);
    }
    itemWidth = std::clamp(itemWidth, UIConstants::Item::MIN_WIDTH, UIConstants::Item::MAX_WIDTH);
    itemHeight =
        std::clamp(itemHeight, UIConstants::Item::MIN_HEIGHT, UIConstants::Item::MAX_HEIGHT);
    fontSize =
        std::clamp(fontSize, UIConstants::Item::MIN_FONT_SIZE, UIConstants::Item::MAX_FONT_SIZE);
    cornerRadius = std::clamp(cornerRadius, UIConstants::Item::MIN_CORNER_RADIUS,
                              UIConstants::Item::MAX_CORNER_RADIUS);
    // Spacing can be negative for overlap effects
    horizontalSpacing = std::clamp(horizontalSpacing, -100, 200);
    verticalSpacing = std::clamp(verticalSpacing, -100, 200);
    // List mode settings
    listFontSize = std::clamp(listFontSize, UIConstants::Item::MIN_FONT_SIZE,
                              UIConstants::Item::MAX_FONT_SIZE);
    listRowHeight = std::clamp(listRowHeight, UIConstants::ListView::MIN_ROW_HEIGHT,
                               UIConstants::ListView::MAX_ROW_HEIGHT);
    sidebarWidth = std::max(sidebarWidth, UIConstants::DetailsPane::MIN_WIDTH);
    // floor pane height; same no-upper-bound treatment as width.
    sidebarHeight = std::max(sidebarHeight, UIConstants::DetailsPane::MIN_HEIGHT);
    // corner darkness percent. 0 = effect off (the toggle is
    // separate); 100 = pitch black at the corners.
    vignetteIntensity = std::clamp(vignetteIntensity, 0, 100);
    // parallax strength percent.
    parallaxStrength = std::clamp(parallaxStrength, 0, 100);
    // backdrop blur radius. 4 is a barely-perceptible blur;
    // anything above 32 is heavy enough that the source becomes unreadable.
    backdropBlurRadius = std::clamp(backdropBlurRadius, 4, 32);
    // keep the default-launcher pointer inside the visible list
    // so a stale config (or a deletion that out-paced the index) can never
    // refer past the end. 0 is always valid because the primary slot exists
    // even when its launcherPath is empty.
    defaultLauncherIndex = std::clamp(defaultLauncherIndex, 0, launcherCount() - 1);
  }
};

// Collection index validation helpers - reduces repeated null checks
// Placed after CollectionConfig definition to avoid forward declaration issues
namespace CollectionUtils {

// ─────────────────────────────────────────────────────────────────────────────
// Index validation helpers - reduces repeated null/bounds checks
// ─────────────────────────────────────────────────────────────────────────────

/// Validates index against collection pointer (null-safe)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> *collections) {
  return collections && index >= 0 && index < collections->size();
}

/// Validates index pointer against collection pointer (null-safe for both)
[[nodiscard]] inline bool isValidIndex(const int *indexPtr,
                                       const QList<CollectionConfig> *collections) {
  return indexPtr && isValidIndex(*indexPtr, collections);
}

/// Validates index against collection reference (no null check needed)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> &collections) {
  return index >= 0 && index < collections.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Collection property accessors with validation
// ─────────────────────────────────────────────────────────────────────────────

/// Get grid width with fallback to default - reduces duplication across
/// managers
[[nodiscard]] inline int getGridWidth(const int *indexPtr,
                                      const QList<CollectionConfig> *collections) {
  if (!isValidIndex(indexPtr, collections)) {
    return UIConstants::Grid::DEFAULT_WIDTH;
  }
  return (*collections)[*indexPtr].gridWidth;
}

// ─────────────────────────────────────────────────────────────────────────────
// Effective grid sizing helpers
// ─────────────────────────────────────────────────────────────────────────────
//
// "Sidebar shrinking active" is the predicate captured by the caller: sidebar
// is currently hidden AND the collection's sidebarMode is Expand (i.e. the
// sidebar would push the grid when shown). Overlay mode never shrinks, so we
// always use the primary value there. The alt fields default to 0, which means
// "inherit the primary" — a fresh upgrade keeps existing layout behavior.

[[nodiscard]] inline int effectiveGridWidth(const CollectionConfig &config,
                                            bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.gridWidthSidebarHidden > 0) {
    return config.gridWidthSidebarHidden;
  }
  return config.gridWidth;
}

[[nodiscard]] inline int effectiveHorizontalGridHeight(const CollectionConfig &config,
                                                       bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.horizontalGridHeightSidebarHidden > 0) {
    return config.horizontalGridHeightSidebarHidden;
  }
  return config.horizontalGridHeight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Virtual folder counting
// ─────────────────────────────────────────────────────────────────────────────

/// Count virtual folders (subdirectories) for a collection config.
/// Returns 0 if includeContentSubfolders is disabled or showAllSubfolderItems
/// is enabled.
[[nodiscard]] int countVirtualFolders(const CollectionConfig &config);

} // namespace CollectionUtils

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

struct GeneralSettings {
  bool rememberSelection = false;
  bool wrapNavigation = false;
  // When enabled, moving the pointer over a visible item updates the current
  // selection without requiring a click. Kept as a global interaction preference
  // because it changes input semantics application-wide.
  bool selectItemOnHover = false;
  int pixmapCacheSizeMB = 50;             // Default 50MB, user configurable
  int keyboardRepeatIntervalMs = 260;     // Grid view keyboard repeat interval in ms
  int keyboardRepeatDelayMs = 260;        // Initial delay before keyboard repeat starts
  int clickHoldDelayMs = 500;             // Click-hold activation delay in ms
  int clickHoldRepeatIntervalMs = 320;    // Grid view interval between click-hold repeat steps
  int listKeyboardRepeatIntervalMs = 50;  // List view keyboard repeat interval in ms
  int listClickHoldRepeatIntervalMs = 80; // List view interval between click-hold repeat steps
  int mouseWheelRows = 1;                 // Rows to scroll per wheel step
  int scrollAnimationDurationMs = 1500;   // Scroll animation duration in ms
  // Global scroll-velocity multiplier applied to both mouse-wheel steps and
  // held-arrow key-repeat cadence. 1.0 = unchanged; >1 faster; <1 slower.
  // Clamped at load/save time to [0.25, 5.0] so the user can't pick values
  // that would stall or saturate the animation pipeline.
  double scrollVelocityMultiplier = 1.0;
  // Text appearance settings
  int titleTintSaturation = 180; // Title text saturation (0-255)
  int titleTintLightness = 60;   // Title text lightness (0-255)
  QString titleBaseColor;        // Base color for title text (empty = use highlight)

  // when an item has no real artwork and falls back to placeholder
  // art (procedural hatch tile or user-supplied placeholder image), draw the
  // item's title text on top of the placeholder. Independent of the existing
  // hideTitles flag — pairs well with hideTitles=on so the title only appears
  // where it adds information (i.e., on tiles that would otherwise be blank).
  bool showTitleInPlaceholder = false;

  // ─────────────────────────────────────────────────────────────────────────
  // Global UI font
  // Applied via QApplication::setFont() after settings load so every widget
  // without an explicit font (menus, dialogs, toolbar, sidebar, item titles
  // that haven't set a per-collection customFontFamily, etc.) inherits it.
  // Empty family / 0 point size means "use the platform default" — that way
  // an upgrading install keeps Qt's chosen UI font until the user picks one.
  // ─────────────────────────────────────────────────────────────────────────
  QString globalUiFontFamily;
  int globalUiFontPointSize = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Runtime text zoom
  // Application-wide multiplier applied on top of every font size — global UI
  // font, per-collection grid/list/coverflow item titles, and the metadata
  // sidebar. Stored as percent (100 = unscaled). Bound at runtime to
  // Ctrl+= / Ctrl+- / Ctrl+0 (zoom in / out / reset). Clamped at load/save
  // so a hand-edited value can't render text at 0pt or blow it out past 3×.
  // ─────────────────────────────────────────────────────────────────────────
  int uiTextZoomPercent = 100;

  // ─────────────────────────────────────────────────────────────────────────
  // Preview video volume
  // Global volume for sidebar / overlay preview audio, 0–100. Applied to all
  // VideoPreviewWidget instances via the static setGlobalVolume() hook so a
  // single toolbar slider controls every preview surface.
  // ─────────────────────────────────────────────────────────────────────────
  int previewVideoVolume = 100;

  // ─────────────────────────────────────────────────────────────────────────
  // Startup video
  // Optional one-shot intro clip (logo / branding) shown above the main
  // window on launch. Skippable via any key or mouse click. The enable bool
  // is independent of the path so the user can keep a path configured but
  // disable it temporarily without losing the value.
  // ─────────────────────────────────────────────────────────────────────────
  bool startupVideoEnabled = false;
  QString startupVideoPath;

  // ─────────────────────────────────────────────────────────────────────────
  // Controls: Keyboard bindings (single-key, no modifier semantics)
  // Defaults match current hard-coded behavior.
  // ─────────────────────────────────────────────────────────────────────────
  int keyNavLeft = Qt::Key_Left;
  int keyNavRight = Qt::Key_Right;
  int keyNavUp = Qt::Key_Up;
  int keyNavDown = Qt::Key_Down;
  int keyConfirm = Qt::Key_Return; // Return/Enter treated as equivalent
  int keyBack = Qt::Key_Escape;
  int keySearch = Qt::Key_Slash;
  int keyAlphabeticBack = Qt::Key_PageUp;
  int keyAlphabeticForward = Qt::Key_PageDown;
  int keyJumpFirst = Qt::Key_Home;
  int keyJumpLast = Qt::Key_End;
  // opens the dedicated item-detail page (full-window overlay)
  // for the current selection. Default I = "info"; ignored while the search
  // bar has focus so typing "i" in the filter still works.
  int keyItemDetails = Qt::Key_I;
  // Jumps directly to the synthetic Home view from any nesting depth. Default
  // 0 = unbound so an upgrading install picks up no surprise shortcut; only
  // honored when useHomeView is enabled.
  int keyHomeView = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Controls: Gamepad bindings
  // Button names are symbolic; backends map them as available.
  // ─────────────────────────────────────────────────────────────────────────
  bool gamepadUseDpad = true;
  bool gamepadUseLeftStick = true;
  QString gamepadConfirmButton = "A";
  QString gamepadBackButton = "B";
  QString gamepadToggleSidebarButton = "Y";
  // ─────────────────────────────────────────────────────────────────────────
  // Mouse: artwork-cycle modifier
  // Stored as the integer value of a single Qt::KeyboardModifier flag
  // (Shift / Control / Alt / Meta). Combined with middle-click to cycle the
  // grid widget through the item's available artwork types. Default Shift
  // chosen to leave plain middle-click on its existing media-preview action
  // Loader clamps unrecognized values back to Shift so a
  // hand-edit can't disable the gesture entirely.
  // ─────────────────────────────────────────────────────────────────────────
  int artworkCycleModifier = static_cast<int>(Qt::ShiftModifier);
  SortMode sortMode = SortMode::NameAscending; // Current sort mode
  bool excludeSubfoldersFromSort = false;      // Exclude subfolders/subcollections from sorting
  int listCollectionColumnWidth = 150;         // Collection column width in list view
  int listArtworkColumnWidth = 32;             // Artwork column width in list view
  // Name of the collection to open on startup. When empty (default), the first
  // root-level collection is opened. When set, takes priority over the default
  // root-collection selection. If the named collection no longer exists, falls
  // back to the default behavior.
  QString startupCollection;

  // Use a synthetic "Home" view at startup that shows one tile per root
  // collection. Takes priority over startupCollection when both are set.
  // `Back` from any root-level collection returns to the home view instead
  // of routing to the first root collection.
  bool useHomeView = false;

  // Optional override for the synthetic Home view's title label and toolbar
  // button. Empty homeViewLabel falls back to the localized "Home" string.
  // homeViewIcon is an absolute path; when set, the toolbar Home button uses
  // it instead of the themed icon.
  QString homeViewLabel;
  QString homeViewIcon;

  // ─────────────────────────────────────────────────────────────────────────
  // Attract mode / autoscroll
  // ─────────────────────────────────────────────────────────────────────────
  bool attractModeEnabled = false;
  int attractModeIdleTimeoutSec = 120;      // Seconds of idle before activation
  bool attractModeAutoScrollEnabled = true; // Sub-toggle: viewport autoscroll
  double attractModeScrollSpeed = 1.0;      // Pixels per tick (0.1-10, sub-pixel via accumulator)
  bool attractModeAdvanceSelectionEnabled = false;
  int attractModeAdvanceSelectionIntervalSec = 5; // Seconds between selection advances
  bool attractModeAdvanceSelectionRandom = false; // Pick random vs. sequential next

  // Splash screens
  // Empty title/subtitle strings mean "use the built-in default" (app display
  // name for boot title, localized "Welcome back" for resume title, etc.).
  // Custom values are used verbatim — no %1 substitution — so a user who
  // wants the app name in the text needs to type it.
  bool bootSplashEnabled = true;
  bool resumeFocusSplashEnabled = true;
  QString bootSplashTitle;
  QString bootSplashSubtitle;
  QString resumeFocusSplashTitle;
  QString resumeFocusSplashSubtitle;

  // ─────────────────────────────────────────────────────────────────────────
  // Runtime detection
  // When enabled, launched media items are tracked via a non-detached
  // QProcess so the UI can sleep behind a "Now Playing" overlay while the
  // child runs and automatically restore + raise the window when it exits.
  // ─────────────────────────────────────────────────────────────────────────
  bool runtimeDetectionEnabled = false;

  // ─────────────────────────────────────────────────────────────────────────
  // Launch history
  // Chronological log of every successful launch, persisted in the
  // `launch_history` SQLite table and rendered by the History tab of the
  // Statistics dialog. Disabled flips off both new-row inserts and the
  // dialog's tab so existing rows stop changing but stay viewable until
  // the user clicks "Clear history…". `historyMaxEntries` is the soft cap
  // applied after every insert; values <= 0 mean "unlimited".
  // ─────────────────────────────────────────────────────────────────────────
  bool historyEnabled = true;
  int historyMaxEntries = 500;

  // ─────────────────────────────────────────────────────────────────────────
  // Collection categorization
  // Toolbar-driven filters that hide subcollection tiles when navigating into
  // a parent. `collectionTypeFilter` is a user-visible label that matches
  // CollectionConfig::type (with parent-chain inheritance via
  // CollectionUtils::effectiveCollectionType); empty means "show all types".
  // `hideSubcollectionTiles` collapses every subcollection tile out of the
  // current view regardless of type, leaving only media items.
  // ─────────────────────────────────────────────────────────────────────────
  QString collectionTypeFilter;
  bool hideSubcollectionTiles = false;

  // ─────────────────────────────────────────────────────────────────────────
  // View-mode toggles
  // Persisted state for the View → Show Menu Bar (F10), Show Toolbar (F8),
  // and Fullscreen (F11) actions so the chosen UI chrome survives across
  // launches. Defaults match the original .ui-defined "all visible, not
  // fullscreen" state so a fresh install opens unchanged.
  // ─────────────────────────────────────────────────────────────────────────
  bool showMenuBar = true;
  bool showToolbar = true;
  bool fullscreen = false;

  // ─────────────────────────────────────────────────────────────────────────
  // Customizable toolbar
  // Per-button visibility flags and text overrides for the items-page top bar.
  // Empty *Text strings mean "use the .ui default" so existing installs see no
  // visual change after upgrade. The search-mode button is icon-only and the
  // type-filter combo box has no user-editable text, so only visibility is
  // exposed for those.
  // ─────────────────────────────────────────────────────────────────────────
  bool toolbarShowGridViewButton = true;
  bool toolbarShowListViewButton = true;
  bool toolbarShowCoverFlowViewButton = true;
  bool toolbarShowHorizontalViewButton = true;
  bool toolbarShowHideSubcollectionsButton = true;
  bool toolbarShowTypeFilter = true;
  bool toolbarShowTitleFilter = true;
  bool toolbarShowSearchModeButton = true;
  bool toolbarShowSearchBar = true;
  QString toolbarGridViewButtonText;
  QString toolbarListViewButtonText;
  QString toolbarCoverFlowViewButtonText;
  QString toolbarHorizontalViewButtonText;
  QString toolbarHideSubcollectionsButtonText;
  QString toolbarTitleFilterText;

  // ─────────────────────────────────────────────────────────────────────────
  // Launcher presets
  // Globally-registered, reusable launcher configurations referenced by
  // collection-level launcher entries via LauncherConfig::presetId. Stored
  // in the [Launchers] settings array; managed via the "Launchers" tab in
  // the settings dialog.
  // ─────────────────────────────────────────────────────────────────────────
  QList<LauncherPreset> launcherPresets;

  QHash<int, int> lastSelectedItems;
  GeneralSettings() = default;
};

// Cache for collection hierarchy lookups - avoids repeated O(n) scans
class CollectionHierarchyCache {
public:
  CollectionHierarchyCache() = default;

  // Rebuilds the hierarchy cache with pre-computed UUIDs and directory
  // mappings. Implemented in collectionutils.cpp to avoid header dependencies.
  void rebuild(const QList<CollectionConfig> &collections);

  /// Returns the union of primary children and linked children
  /// for @p parentIndex. Primary children come first in
  /// insertion order; linked children are appended in insertion order with
  /// duplicates suppressed. Most navigation/scroll/search code paths read
  /// children through this accessor, so they pick up alias parents
  /// automatically when the cache is rebuilt.
  [[nodiscard]] const QList<int> &directChildren(int parentIndex) const {
    auto it = m_directChildren.constFind(parentIndex);
    if (it == m_directChildren.cend()) {
      static const QList<int> kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  /// Subset of directChildren(@p parentIndex) reachable only via the
  /// CollectionConfig::additionalParentNames link list — i.e. the
  /// "see-also" appearances. Used by the settings tree (
  /// stage 2) to render linked appearances in italics. Does NOT include
  /// the primary children.
  [[nodiscard]] const QList<int> &linkedDirectChildren(int parentIndex) const {
    auto it = m_linkedDirectChildren.constFind(parentIndex);
    if (it == m_linkedDirectChildren.cend()) {
      static const QList<int> kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  /// All descendants of @p parentIndex via the merged child graph
  /// (primary + linked). Deduped and cycle-bounded — even mutual links
  /// resolve to a finite set. The starting node itself is excluded.
  [[nodiscard]] const QList<int> &allDescendants(int parentIndex) const {
    auto it = m_allDescendants.constFind(parentIndex);
    if (it == m_allDescendants.cend()) {
      static const QList<int> kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  // UUID accessors - O(1) lookup of pre-computed values
  [[nodiscard]] const QString &collectionUuid(int index) const {
    auto it = m_collectionUuids.constFind(index);
    if (it == m_collectionUuids.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &expandedMediaDir(int index) const {
    auto it = m_expandedMediaDirs.constFind(index);
    if (it == m_expandedMediaDirs.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &expandedArtworkDir(int index) const {
    auto it = m_expandedArtworkDirs.constFind(index);
    if (it == m_expandedArtworkDirs.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &uuidToMediaDir(const QString &uuid) const {
    auto it = m_uuidToMediaDir.constFind(uuid);
    if (it == m_uuidToMediaDir.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &uuidToArtworkDir(const QString &uuid) const {
    auto it = m_uuidToArtworkDir.constFind(uuid);
    if (it == m_uuidToArtworkDir.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] int uuidToCollectionIndex(const QString &uuid) const {
    return m_uuidToCollectionIndex.value(uuid, -1);
  }

  // Lookup artwork directory from a file path's parent directory
  [[nodiscard]] QString artworkDirForFilePath(const QString &filePath) const {
    // Extract the parent directory and try to find the artwork dir
    QFileInfo fi(filePath);
    QString parentDir = fi.absolutePath();
    // Exact match first
    if (m_mediaDirToArtworkDir.contains(parentDir)) {
      return m_mediaDirToArtworkDir.value(parentDir);
    }
    // Try without trailing slash variations
    if (parentDir.endsWith('/')) {
      QString normalized = parentDir.chopped(1);
      if (m_mediaDirToArtworkDir.contains(normalized)) {
        return m_mediaDirToArtworkDir.value(normalized);
      }
    } else {
      QString withSlash = parentDir + '/';
      if (m_mediaDirToArtworkDir.contains(withSlash)) {
        return m_mediaDirToArtworkDir.value(withSlash);
      }
    }
    return QString();
  }

  // Get all UUIDs for a collection and its descendants (for DB queries).
  // Precomputed in rebuild() so this is an O(1) lookup on the hot path.
  [[nodiscard]] const QStringList &descendantUuids(int parentIndex) const {
    auto it = m_descendantUuids.constFind(parentIndex);
    if (it == m_descendantUuids.cend()) {
      static const QStringList kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] bool isValid() const { return m_collections; }

private:
  /// Cycle-safe descendant walk over the merged child graph. Without the
  /// visited set, a link cycle (A links B, B links A) would
  /// loop forever. Traversal order is BFS; result excludes the starting
  /// node so a self-link doesn't make a collection its own descendant.
  QList<int> computeDescendants(int parentIndex) const {
    QList<int> result;
    QSet<int> visited;
    visited.insert(parentIndex);
    QList<int> stack = m_directChildren.value(parentIndex);
    while (!stack.isEmpty()) {
      int idx = stack.takeFirst();
      if (visited.contains(idx)) {
        continue;
      }
      visited.insert(idx);
      result.append(idx);
      stack.append(m_directChildren.value(idx));
    }
    return result;
  }

  const QList<CollectionConfig> *m_collections = nullptr;
  QHash<int, QList<int>> m_directChildren;       // primary ∪ linked, primary first
  QHash<int, QList<int>> m_linkedDirectChildren; // linked-only subset
  QHash<int, QList<int>> m_allDescendants;

  // Pre-computed UUIDs and directory mappings (eliminates SHA1 on each startup)
  QHash<int, QString> m_collectionUuids;          // index -> UUID
  QHash<int, QString> m_expandedMediaDirs;        // index -> expanded media dir
  QHash<int, QString> m_expandedArtworkDirs;      // index -> expanded artwork dir
  QHash<QString, QString> m_uuidToMediaDir;       // UUID -> expanded media dir
  QHash<QString, QString> m_uuidToArtworkDir;     // UUID -> expanded artwork dir
  QHash<QString, int> m_uuidToCollectionIndex;    // UUID -> collection index
  QHash<QString, QString> m_mediaDirToArtworkDir; // media dir -> artwork dir (for file lookups)
  QHash<int, QStringList> m_descendantUuids;      // index -> [self_uuid, descendant_uuids...]
};

// Legacy inline functions for backward compatibility
namespace CollectionUtils {

[[nodiscard]] QList<int> collectDescendantIndices(int parentIndex,
                                                  const QList<CollectionConfig> &collections);

[[nodiscard]] QString hierarchicalNameFor(const CollectionConfig &collection,
                                          const QList<CollectionConfig> &collections);

/**
 * @brief Returns the ancestor index chain for a collection, root-first.
 *
 * Walks up `parentCollectionIndex` from `collection`'s direct parent until the
 * first non-subcollection ancestor (inclusive). The returned list is ordered
 * from the outermost (root-most) ancestor to the direct parent and does NOT
 * include `collection` itself. Returns an empty list for non-subcollections or
 * collections whose `parentCollectionIndex` is out of range.
 *
 * Use this to render multi-level breadcrumbs.
 */
[[nodiscard]] QList<int> ancestorIndexChain(const CollectionConfig &collection,
                                            const QList<CollectionConfig> &collections);

[[nodiscard]] QString selectionSessionKeyFor(const CollectionConfig &collection,
                                             const QList<CollectionConfig> &collections);

/**
 * @brief Detects whether reparenting `childIndex` under `potentialParentIndex`
 * would create a cycle in the collection hierarchy.
 *
 * Walks up `potentialParentIndex`'s ancestor chain looking for `childIndex`.
 * If found, the proposed reparent is illegal. Also defends against pre-existing
 * cycles in the input data by tracking visited indices.
 *
 * Returns true (i.e. "circular, reject the operation") in these cases:
 *   - either index is out of range
 *   - childIndex == potentialParentIndex (self-parenting)
 *   - childIndex is already an ancestor of potentialParentIndex
 *   - the existing chain has a pre-existing cycle (data corruption)
 *
 * Pure function — extracted from SettingsDialog so the validation can be
 * unit-tested without instantiating the full settings dialog.
 */
[[nodiscard]] bool wouldCreateCircularReference(int childIndex, int potentialParentIndex,
                                                const QList<CollectionConfig> &collections);

[[nodiscard]] QList<int> directChildrenOf(int parentIndex,
                                          const QList<CollectionConfig> &collections);

/**
 * @brief Resolves artwork directory for a collection, falling back to parent if
 * empty.
 * @param collectionIndex Index of the collection to resolve artwork for.
 * @param collections List of all collections.
 * @return Artwork directory from this collection or nearest ancestor with one
 * set.
 *
 * Walks up the parent chain until a non-empty artworkDirectory is found.
 * Returns empty string if no ancestor has an artwork directory.
 */
/**
 * @brief Walks up the parent chain looking for a non-empty value of @p field
 * on @p collectionIndex or its nearest ancestor. Returns the empty string if
 * no ancestor has the field set.
 *
 * The four directory resolvers below were textually identical except for the
 * member they read; this helper consolidates the parent-chain walk so future
 * fixes (cycle handling, subcollection semantics) need only one edit.
 */
[[nodiscard]] QString resolveInheritedField(int collectionIndex,
                                            const QList<CollectionConfig> &collections,
                                            QString CollectionConfig::*field);

[[nodiscard]] QString resolveArtworkDirectory(int collectionIndex,
                                              const QList<CollectionConfig> &collections);

/**
 * @brief Resolves video directory for a collection, falling back to parent if
 * empty. Used by the sidebar so subcollections inherit a parent's
 * videoDirectory in showAllSubcollectionItems mode.
 */
[[nodiscard]] QString resolveVideoDirectory(int collectionIndex,
                                            const QList<CollectionConfig> &collections);

[[nodiscard]] QString resolveManualDirectory(int collectionIndex,
                                             const QList<CollectionConfig> &collections);

[[nodiscard]] QString resolvePlaceholderArtwork(int collectionIndex,
                                                const QList<CollectionConfig> &collections);

/**
 * @brief Returns the effective category/type for a collection, walking up
 * the parent chain when the collection's own `type` field is empty.
 *
 * the per-collection type is optional. Subcollections can either
 * declare their own type or inherit from the nearest non-empty ancestor —
 * this matches the user's mental model of "this whole branch is Games" while
 * still letting an oddball subcollection be tagged differently. Returns an
 * empty string when nothing in the chain is tagged.
 *
 * Cycle-safe: bounds the walk by `collections.size()` so a malformed
 * parentCollectionIndex chain can't loop forever.
 */
[[nodiscard]] QString effectiveCollectionType(int collectionIndex,
                                              const QList<CollectionConfig> &collections);

/**
 * @brief Returns the set of distinct non-empty `type` labels across the full
 * collection list (roots and subcollections), case-insensitive deduped and
 * sorted alphabetically. Used to populate the toolbar filter dropdown and
 * the per-collection editor's combobox completion.
 */
[[nodiscard]] QStringList collectAllCollectionTypes(const QList<CollectionConfig> &collections);

/**
 * @brief Computes a deterministic UUID from collection name and media
 * directory.
 * @param name Collection name.
 * @param mediaDir Media directory path.
 * @return SHA1 hash as hex string.
 */
[[nodiscard]] QString computeCollectionUuid(const QString &name, const QString &mediaDir);

} // namespace CollectionUtils

Q_DECLARE_METATYPE(CollectionConfig)
Q_DECLARE_METATYPE(CollectionContext)

#endif
