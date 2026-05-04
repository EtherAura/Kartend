#ifndef COLLECTIONUTILS_H
#define COLLECTIONUTILS_H

#include <algorithm>
#include <QDir>
#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>
#include <uiconstants.h>

// Forward declaration for validation
namespace ErrorUtils {
struct ErrorContext;
}

enum class HorizontalAlignment { Left = 0, Center = 1, Right = 2 };

enum class SidebarMode { Overlay = 0, Expand = 1 };

/// Kartend-63e: which side of the items viewport the sidebar lives on. Applies
/// in both Overlay and Fixed modes — Overlay swaps the X anchor, Fixed swaps
/// the layout insertion index.
enum class SidebarPosition { Right = 0, Left = 1 };

/// Kartend-63e: how the sidebar background is rendered. Color and Image mirror
/// the main-view BackgroundType values. Pattern adds a procedurally-drawn
/// pattern (currently only Crosshatch) tinted by `sidebarPatternColor`.
enum class SidebarBackgroundType { Color = 0, Image = 1, Pattern = 2 };

/// Kartend-63e: built-in sidebar background patterns. Single value today;
/// dots/lines/etc. can be added without breaking persistence because the int
/// representation is what's serialized.
enum class SidebarPattern { Crosshatch = 0 };

/// Kartend-63e: which built-in tab is active in the sidebar. Item is the
/// per-item view (artwork + metadata); Collection forces the collection
/// summary even with a selection; File is reserved for a user-customizable
/// pane in a later iteration.
enum class SidebarTab { Item = 0, Collection = 1, File = 2 };

enum class BackgroundType { Color = 0, Image = 1 };

/// View type for displaying collection items
enum class ViewType { Grid = 0, List = 1, CoverFlow = 2 };

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
  case ViewType::Grid:
  default:
    return "grid";
  }
}

[[nodiscard]] inline ViewType stringToViewType(const QString &str) {
  QString lower = str.toLower();
  if (lower == "list") return ViewType::List;
  if (lower == "coverflow") return ViewType::CoverFlow;
  return ViewType::Grid;
}

[[nodiscard]] inline QString sidebarPositionToString(SidebarPosition pos) {
  return pos == SidebarPosition::Left ? "left" : "right";
}

[[nodiscard]] inline SidebarPosition stringToSidebarPosition(const QString &str) {
  return str.toLower() == "left" ? SidebarPosition::Left : SidebarPosition::Right;
}

[[nodiscard]] inline QString sidebarBackgroundTypeToString(SidebarBackgroundType type) {
  switch (type) {
  case SidebarBackgroundType::Image:
    return "image";
  case SidebarBackgroundType::Pattern:
    return "pattern";
  case SidebarBackgroundType::Color:
  default:
    return "color";
  }
}

[[nodiscard]] inline SidebarBackgroundType stringToSidebarBackgroundType(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "image") return SidebarBackgroundType::Image;
  if (lower == "pattern") return SidebarBackgroundType::Pattern;
  return SidebarBackgroundType::Color;
}

[[nodiscard]] inline QString sidebarPatternToString(SidebarPattern pattern) {
  switch (pattern) {
  case SidebarPattern::Crosshatch:
  default:
    return "crosshatch";
  }
}

[[nodiscard]] inline SidebarPattern stringToSidebarPattern(const QString &str) {
  // Single value for now; future patterns slot in here without persistence breakage.
  Q_UNUSED(str);
  return SidebarPattern::Crosshatch;
}

[[nodiscard]] inline QString sidebarTabToString(SidebarTab tab) {
  switch (tab) {
  case SidebarTab::Collection:
    return "collection";
  case SidebarTab::File:
    return "file";
  case SidebarTab::Item:
  default:
    return "item";
  }
}

[[nodiscard]] inline SidebarTab stringToSidebarTab(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "collection") return SidebarTab::Collection;
  if (lower == "file") return SidebarTab::File;
  return SidebarTab::Item;
}

} // namespace CollectionUtils

/// Kartend-p1jd: a globally-registered, reusable launcher configuration. A
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

/// Kartend-bdl: one entry in a collection's launcher list. The legacy primary
/// launcher (CollectionConfig::launcherPath/corePath/launchParameters) is
/// always present as launcher index 0; entries in
/// CollectionConfig::additionalLaunchers occupy indices 1..N. `name` is a
/// user-visible label shown in the launcher chooser; empty falls back to the
/// executable basename.
///
/// Kartend-p1jd: when `presetId` matches a registered LauncherPreset id, the
/// preset's fields override the inline launcher fields at resolution time.
struct LauncherConfig {
  QString name;
  QString launcherPath;
  QString corePath;
  QString launchParameters;
  /// Optional reference to a globally-registered preset (Kartend-p1jd).
  /// When set and the preset exists, all other fields are inherited from
  /// the preset. Empty means "use the inline fields verbatim".
  QString presetId;

  bool operator==(const LauncherConfig &other) const {
    return name == other.name && launcherPath == other.launcherPath && corePath == other.corePath &&
           launchParameters == other.launchParameters && presetId == other.presetId;
  }
};

namespace LauncherUtils {

/// Returns a LauncherConfig with fields resolved against the preset list.
/// When `lc.presetId` matches a preset, that preset's name/path/core/params
/// replace the inline fields (the preset id stays attached for round-trip
/// persistence). When the preset is missing or `presetId` is empty, the
/// returned config equals `lc`. Kartend-p1jd.
[[nodiscard]] inline LauncherConfig resolvePreset(const LauncherConfig &lc,
                                                  const QList<LauncherPreset> &presets) {
  if (lc.presetId.isEmpty()) {
    return lc;
  }
  for (const LauncherPreset &preset : presets) {
    if (preset.id == lc.presetId) {
      LauncherConfig resolved;
      resolved.name = preset.name;
      resolved.launcherPath = preset.launcherPath;
      resolved.corePath = preset.corePath;
      resolved.launchParameters = preset.launchParameters;
      resolved.presetId = lc.presetId;
      return resolved;
    }
  }
  return lc;
}

} // namespace LauncherUtils

struct CollectionConfig {
  QString name;
  /// Free-form category/type tag (Kartend-dd8). Empty for an untagged
  /// collection — subcollections inherit the nearest non-empty ancestor type
  /// for filter purposes (see CollectionUtils::effectiveCollectionType). The
  /// value is just a user-chosen label like "Games" / "Movies" / "Music"; the
  /// app does not interpret it semantically.
  QString type;
  QString launcherPath;
  QString corePath;
  QString launchParameters;
  /// User-visible name for the primary (legacy) launcher. Empty is fine — the
  /// chooser dialog falls back to the executable basename. Kartend-bdl.
  QString launcherName;
  /// Extra launchers beyond the primary, indexed at 1..N in launcher views.
  /// Kartend-bdl.
  QList<LauncherConfig> additionalLaunchers;
  /// Index into the unified launcher view (0 = primary, 1..N =
  /// additionalLaunchers[0..N-1]). The chooser dialog pre-selects this entry
  /// when launcherCount() > 1; for single-launcher collections it is
  /// effectively ignored. Kartend-bdl.
  int defaultLauncherIndex = 0;
  QString mediaDirectory;
  QString artworkDirectory;
  QString videoDirectory;
  /// Directory that holds per-item manual files (PDF, EPUB, TXT, etc.).
  /// Files are matched by completeBaseName(), parallel to artworkDirectory
  /// and videoDirectory. Optional; when empty no manual is auto-discovered.
  /// Per-item overrides live in `item_metadata.manual_path` (Kartend-9jdv).
  QString manualDirectory;
  QString placeholderArtwork;
  QString collectionIcon;
  QStringList extensions;
  /// User-defined custom artwork type ids beyond the standard set
  /// (Kartend-53vk). Each entry is a free-form string the user picks; the
  /// sidebar uses it both as the artwork_type id stored in `item_artwork` and
  /// as the gallery thumbnail label until a friendly-name registry is added.
  /// Auto-discovery does NOT apply to custom types — they only resolve via a
  /// per-item manual override.
  QStringList customArtworkTypes;
  int gridWidth;
  bool sidebarVisible;
  int parentCollectionIndex = -1;
  bool isSubcollection = false;
  [[nodiscard]] bool hasParent() const { return parentCollectionIndex >= 0; }
  bool showAllSubcollectionItems = false;
  bool hideTitles = false;
  bool hideSubcollectionTitles = false;
  /// Per-collection regex patterns stripped from displayed item titles
  /// (Kartend-5h6). One pattern per entry; processed in order via
  /// QString::remove(). Common use is dropping region tags like `\s*\(USA\)`
  /// or revision tags like `\s*\[!\]` so only the canonical title shows in
  /// the grid/list. Patterns are stored verbatim (no escaping) — the user
  /// types regex syntax directly in the toolbar popup. Invalid patterns are
  /// skipped at compile time rather than aborting the whole list.
  QStringList titleExclusionPatterns;
  /// Master switch for the exclusion list (Kartend-5h6). When false the
  /// patterns persist but are not applied — lets the user toggle the cleanup
  /// from the toolbar without losing their pattern list.
  bool titleExclusionEnabled = true;
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Center;
  SidebarMode sidebarMode = SidebarMode::Overlay;
  /// Kartend-63e sidebar enhancements. Position controls left/right placement;
  /// in Fixed mode this swaps the QHBoxLayout insertion index, in Overlay mode
  /// it swaps the X anchor in positionSidebarOverlay().
  SidebarPosition sidebarPosition = SidebarPosition::Right;
  /// Background rendering mode for the sidebar. Color and Image mirror the
  /// main-view background pattern. Pattern fills with `sidebarBackgroundColor`
  /// (or system Window when blank) and overlays the chosen procedural pattern
  /// tinted with `sidebarPatternColor`.
  SidebarBackgroundType sidebarBackgroundType = SidebarBackgroundType::Color;
  QString sidebarBackgroundColor; // hex; blank falls back to palette(Window)
  QString sidebarBackgroundImage; // path; sanitized via validatePathSecurity on save
  SidebarPattern sidebarPattern = SidebarPattern::Crosshatch;
  /// Kartend-63e: 0–100 % opacity multiplier applied to pattern strokes.
  /// Lower values fade the lines into the bg without changing color; users
  /// who add new SidebarPattern variants later get a single intensity knob
  /// for free. 50 matches the original sidebar dimming.
  int sidebarPatternIntensity = 50;
  QString sidebarPatternColor; // hex tint overlay painted on top of the pattern
  QString sidebarTextColor;    // hex; blank falls back to palette(WindowText)
  QString sidebarAccentColor;  // hex; blank falls back to palette(highlight)
  /// Kartend-63e: semi-opaque "bubble" backgrounds drawn behind sidebar
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
  /// Preferred sidebar width in pixels. Clamped to [MIN_WIDTH, MAX_WIDTH] at
  /// apply time. Defaults to FIXED_WIDTH so existing collections keep their
  /// historical look. When `sidebarWidthLocked` is true the user cannot drag
  /// the inner edge to resize.
  int sidebarWidth = UIConstants::Sidebar::FIXED_WIDTH;
  bool sidebarWidthLocked = true;
  /// Which built-in sidebar tab is active. Persisted per collection so users
  /// can keep one collection on the Collection summary tab while another
  /// stays on the per-Item view.
  SidebarTab sidebarActiveTab = SidebarTab::Item;
  ViewType viewType = ViewType::Grid; // Grid (default) or List view
  /// Kartend-ks4n: when true, media items whose artwork lookup returns no
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
  QString primaryColor;    // Primary UI color for toolbar, menubar, search bar
  QString tileColor;       // Color for item tiles/placeholders (if blank, uses default)
  QString selectionColor;  // Color for selection rectangle and glide overlay border

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

  // ─── Playlist support (Kartend-vlm7) ───────────────────────────────────────
  // Runtime-only marker for synthesized "virtual collection" entries backed by
  // the SQLite `playlists` table instead of an INI section. Synthesised at
  // startup by PlaylistManager and appended to MainWindow::m_collections so
  // playlists nest into the hierarchy / appear as tiles like ordinary
  // subcollections. SettingsManager::saveCollections() MUST skip configs with
  // isPlaylist=true so they never round-trip into kartend.cfg.
  bool isPlaylist = false;
  QString playlistId; // UUID — matches playlists.id when isPlaylist is true.

  // ─── Collection links / alias parents (Kartend-gzmk) ────────────────────────
  // Names of additional parent collections this collection should appear
  // under (in addition to its primary parentCollectionIndex). Stored as
  // names rather than indices so reorder/rename of the parent list survives
  // round-trips. Resolved to indices on demand by CollectionHierarchyCache::
  // rebuild(). Unknown names — typo'd or referencing a deleted parent —
  // are silently ignored at resolution time so a stale config can't wedge
  // the tree. Self-references (a collection naming itself) are likewise
  // dropped. Empty when the collection has no aliases.
  QStringList additionalParentNames;
  /// Reserved-kind tag for built-in playlists (Kartend-5mg8). Empty for
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
           sidebarWidth == other.sidebarWidth && sidebarWidthLocked == other.sidebarWidthLocked &&
           sidebarActiveTab == other.sidebarActiveTab &&
           viewType == other.viewType && horizontalSpacing == other.horizontalSpacing &&
           verticalSpacing == other.verticalSpacing &&
           hideHorizontalScrollbar == other.hideHorizontalScrollbar &&
           hideVerticalScrollbar == other.hideVerticalScrollbar && itemWidth == other.itemWidth &&
           itemHeight == other.itemHeight && fontSize == other.fontSize &&
           cornerRadius == other.cornerRadius && backgroundType == other.backgroundType &&
           backgroundColor == other.backgroundColor && backgroundImage == other.backgroundImage &&
           primaryColor == other.primaryColor && tileColor == other.tileColor &&
           selectionColor == other.selectionColor && extractArchives == other.extractArchives &&
           extractedExtension == other.extractedExtension && expandMode == other.expandMode &&
           includeContentSubfolders == other.includeContentSubfolders &&
           includeArtworkSubfolders == other.includeArtworkSubfolders &&
           showAllSubfolderItems == other.showAllSubfolderItems &&
           hideSubfolderTitles == other.hideSubfolderTitles &&
           showHiddenFolders == other.showHiddenFolders && listFontSize == other.listFontSize &&
           listRowHeight == other.listRowHeight && listRowColor == other.listRowColor &&
           listAltRowColor == other.listAltRowColor && customFontFamily == other.customFontFamily &&
           isPlaylist == other.isPlaylist && playlistId == other.playlistId &&
           playlistReservedKind == other.playlistReservedKind &&
           additionalParentNames == other.additionalParentNames;
  }

  // ─── Launcher list helpers (Kartend-bdl) ───────────────────────────────────
  /// Total number of launchers visible to the user: 1 primary + N additional.
  [[nodiscard]] int launcherCount() const { return 1 + additionalLaunchers.size(); }

  /// Returns the launcher at the unified index. Index 0 is the primary
  /// (synthesized from the legacy fields); index 1..N maps to
  /// additionalLaunchers[0..N-1]. Out-of-range indices return an empty config.
  [[nodiscard]] LauncherConfig launcherAt(int index) const {
    if (index == 0) {
      // Qualify with this-> so the compiler doesn't confuse the legacy fields
      // with the same-named members on LauncherConfig during aggregate init.
      // The trailing empty string is the (Kartend-p1jd) presetId — the
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
    sidebarWidth =
        std::clamp(sidebarWidth, UIConstants::Sidebar::MIN_WIDTH, UIConstants::Sidebar::MAX_WIDTH);
    // Kartend-bdl: keep the default-launcher pointer inside the visible list
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
// Virtual folder counting
// ─────────────────────────────────────────────────────────────────────────────

/// Count virtual folders (subdirectories) for a collection config.
/// Returns 0 if includeContentSubfolders is disabled or showAllSubfolderItems
/// is enabled.
[[nodiscard]] inline int countVirtualFolders(const CollectionConfig &config) {
  // Only count virtual folders if includeContentSubfolders is enabled
  // AND showAllSubfolderItems is false (otherwise items are flattened)
  if (!config.includeContentSubfolders || config.showAllSubfolderItems) {
    return 0;
  }

  // Determine the effective directory to scan
  QString scanDir = config.mediaDirectory;
  if (!config.currentSubfolder.isEmpty()) {
    scanDir = QDir(scanDir).absoluteFilePath(config.currentSubfolder);
  }

  QDir dir(scanDir);
  if (!dir.exists()) {
    return 0;
  }

  // Count subdirectories, optionally including hidden folders
  QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
  if (config.showHiddenFolders) {
    filters |= QDir::Hidden;
  }
  return dir.entryList(filters).size();
}

} // namespace CollectionUtils

/// Sort mode for collection items
enum class SortMode {
  NameAscending = 0,        // A → Z (default)
  NameDescending = 1,       // Z → A
  CollectionAscending = 2,  // Collection name A → Z
  CollectionDescending = 3, // Collection name Z → A
  ArtworkFirst = 4,         // Items with artwork first
  ArtworkLast = 5,          // Items with artwork last
  Random = 6,               // Shuffle
  DateDescending = 7,       // Newest modified first
  DateAscending = 8,        // Oldest modified first
  SizeDescending = 9,       // Largest file first
  SizeAscending = 10        // Smallest file first
};

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

  // ─── Collection categorization filters (Kartend-dd8) ───────────────────────
  // Mirrored from GeneralSettings on every navigation entry so the scroll
  // pipeline can drop subcollection tiles whose effective type doesn't match
  // the active filter, or hide them entirely. Empty filter == show all.
  QString collectionTypeFilter;
  bool hideSubcollectionTiles = false;

  [[nodiscard]] bool isValid() const { return currentIndex >= 0; }
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

  // ─────────────────────────────────────────────────────────────────────────
  // Global UI font (Kartend-9v0o)
  // Applied via QApplication::setFont() after settings load so every widget
  // without an explicit font (menus, dialogs, toolbar, sidebar, item titles
  // that haven't set a per-collection customFontFamily, etc.) inherits it.
  // Empty family / 0 point size means "use the platform default" — that way
  // an upgrading install keeps Qt's chosen UI font until the user picks one.
  // ─────────────────────────────────────────────────────────────────────────
  QString globalUiFontFamily;
  int globalUiFontPointSize = 0;

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
  // Mouse: artwork-cycle modifier (Kartend-1v6)
  // Stored as the integer value of a single Qt::KeyboardModifier flag
  // (Shift / Control / Alt / Meta). Combined with middle-click to cycle the
  // grid widget through the item's available artwork types. Default Shift
  // chosen to leave plain middle-click on its existing media-preview action
  // (Kartend-ljey). Loader clamps unrecognized values back to Shift so a
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

  // ─────────────────────────────────────────────────────────────────────────
  // Attract mode / autoscroll (Kartend-1pp)
  // ─────────────────────────────────────────────────────────────────────────
  bool attractModeEnabled = false;
  int attractModeIdleTimeoutSec = 120; // Seconds of idle before activation
  bool attractModeAutoScrollEnabled = true; // Sub-toggle: viewport autoscroll
  double attractModeScrollSpeed = 1.0;      // Pixels per tick (0.1-10, sub-pixel via accumulator)
  bool attractModeAdvanceSelectionEnabled = false;
  int attractModeAdvanceSelectionIntervalSec = 5; // Seconds between selection advances
  bool attractModeAdvanceSelectionRandom = false; // Pick random vs. sequential next

  // Splash screens
  bool bootSplashEnabled = true;
  bool resumeFocusSplashEnabled = true;

  // ─────────────────────────────────────────────────────────────────────────
  // Runtime detection (Kartend-qxv)
  // When enabled, launched media items are tracked via a non-detached
  // QProcess so the UI can sleep behind a "Now Playing" overlay while the
  // child runs and automatically restore + raise the window when it exits.
  // ─────────────────────────────────────────────────────────────────────────
  bool runtimeDetectionEnabled = false;

  // ─────────────────────────────────────────────────────────────────────────
  // Launch history (Kartend-fse)
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
  // Collection categorization (Kartend-dd8)
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
  // Customizable toolbar (Kartend-81o)
  // Per-button visibility flags and text overrides for the items-page top bar.
  // Empty *Text strings mean "use the .ui default" so existing installs see no
  // visual change after upgrade. The search-mode button is icon-only and the
  // type-filter combo box has no user-editable text, so only visibility is
  // exposed for those.
  // ─────────────────────────────────────────────────────────────────────────
  bool toolbarShowGridViewButton = true;
  bool toolbarShowListViewButton = true;
  bool toolbarShowCoverFlowViewButton = true;
  bool toolbarShowHideSubcollectionsButton = true;
  bool toolbarShowTypeFilter = true;
  bool toolbarShowTitleFilter = true;
  bool toolbarShowSearchModeButton = true;
  bool toolbarShowSearchBar = true;
  QString toolbarGridViewButtonText;
  QString toolbarListViewButtonText;
  QString toolbarCoverFlowViewButtonText;
  QString toolbarHideSubcollectionsButtonText;
  QString toolbarTitleFilterText;

  // ─────────────────────────────────────────────────────────────────────────
  // Launcher presets (Kartend-p1jd)
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
  /// (Kartend-gzmk) for @p parentIndex. Primary children come first in
  /// insertion order; linked children are appended in insertion order with
  /// duplicates suppressed. Most navigation/scroll/search code paths read
  /// children through this accessor, so they pick up alias parents
  /// automatically when the cache is rebuilt.
  [[nodiscard]] QList<int> directChildren(int parentIndex) const {
    return m_directChildren.value(parentIndex);
  }

  /// Subset of directChildren(@p parentIndex) reachable only via the
  /// CollectionConfig::additionalParentNames link list — i.e. the
  /// "see-also" appearances. Used by the settings tree (Kartend-gzmk
  /// stage 2) to render linked appearances in italics. Does NOT include
  /// the primary children.
  [[nodiscard]] QList<int> linkedDirectChildren(int parentIndex) const {
    return m_linkedDirectChildren.value(parentIndex);
  }

  /// All descendants of @p parentIndex via the merged child graph
  /// (primary + linked). Deduped and cycle-bounded — even mutual links
  /// resolve to a finite set. The starting node itself is excluded.
  [[nodiscard]] QList<int> allDescendants(int parentIndex) const {
    return m_allDescendants.value(parentIndex);
  }

  // UUID accessors - O(1) lookup of pre-computed values
  [[nodiscard]] QString collectionUuid(int index) const { return m_collectionUuids.value(index); }

  [[nodiscard]] QString expandedMediaDir(int index) const {
    return m_expandedMediaDirs.value(index);
  }

  [[nodiscard]] QString expandedArtworkDir(int index) const {
    return m_expandedArtworkDirs.value(index);
  }

  [[nodiscard]] QString uuidToMediaDir(const QString &uuid) const {
    return m_uuidToMediaDir.value(uuid);
  }

  [[nodiscard]] QString uuidToArtworkDir(const QString &uuid) const {
    return m_uuidToArtworkDir.value(uuid);
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

  // Get all UUIDs for a collection and its descendants (for DB queries)
  [[nodiscard]] QStringList descendantUuids(int parentIndex) const {
    QStringList uuids;
    QString parentUuid = m_collectionUuids.value(parentIndex);
    if (!parentUuid.isEmpty()) {
      uuids << parentUuid;
    }
    for (int descendant : m_allDescendants.value(parentIndex)) {
      QString uuid = m_collectionUuids.value(descendant);
      if (!uuid.isEmpty()) {
        uuids << uuid;
      }
    }
    return uuids;
  }

  [[nodiscard]] bool isValid() const { return m_collections; }

private:
  /// Cycle-safe descendant walk over the merged child graph. Without the
  /// visited set, a Kartend-gzmk link cycle (A links B, B links A) would
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
  QHash<int, QList<int>> m_linkedDirectChildren; // Kartend-gzmk: linked-only subset
  QHash<int, QList<int>> m_allDescendants;

  // Pre-computed UUIDs and directory mappings (eliminates SHA1 on each startup)
  QHash<int, QString> m_collectionUuids;          // index -> UUID
  QHash<int, QString> m_expandedMediaDirs;        // index -> expanded media dir
  QHash<int, QString> m_expandedArtworkDirs;      // index -> expanded artwork dir
  QHash<QString, QString> m_uuidToMediaDir;       // UUID -> expanded media dir
  QHash<QString, QString> m_uuidToArtworkDir;     // UUID -> expanded artwork dir
  QHash<QString, int> m_uuidToCollectionIndex;    // UUID -> collection index
  QHash<QString, QString> m_mediaDirToArtworkDir; // media dir -> artwork dir (for file lookups)
};

// Legacy inline functions for backward compatibility
namespace CollectionUtils {

[[nodiscard]] inline QList<int>
collectDescendantIndices(int parentIndex, const QList<CollectionConfig> &collections) {
  QList<int> descendants;
  for (int index = 0; index < collections.size(); ++index) {
    if (collections[index].parentCollectionIndex == parentIndex) {
      descendants.append(index);
      descendants.append(collectDescendantIndices(index, collections));
    }
  }
  return descendants;
}

[[nodiscard]] inline QString hierarchicalNameFor(const CollectionConfig &collection,
                                                 const QList<CollectionConfig> &collections) {
  if (!collection.isSubcollection || collection.parentCollectionIndex < 0) {
    return collection.name;
  }
  QStringList parts;
  parts.prepend(collection.name);
  int parent = collection.parentCollectionIndex;
  while (parent >= 0 && parent < collections.size()) {
    const CollectionConfig &p = collections[parent];
    parts.prepend(p.name);
    if (!p.isSubcollection) break;
    parent = p.parentCollectionIndex;
  }
  return parts.join('/');
}

/**
 * @brief Returns the ancestor index chain for a collection, root-first.
 *
 * Walks up `parentCollectionIndex` from `collection`'s direct parent until the
 * first non-subcollection ancestor (inclusive). The returned list is ordered
 * from the outermost (root-most) ancestor to the direct parent and does NOT
 * include `collection` itself. Returns an empty list for non-subcollections or
 * collections whose `parentCollectionIndex` is out of range.
 *
 * Use this to render multi-level breadcrumbs (Kartend-7pq).
 */
[[nodiscard]] inline QList<int> ancestorIndexChain(const CollectionConfig &collection,
                                                   const QList<CollectionConfig> &collections) {
  QList<int> chain;
  if (!collection.isSubcollection || collection.parentCollectionIndex < 0) {
    return chain;
  }
  int parent = collection.parentCollectionIndex;
  // Guard against cycles by bounding the walk length.
  const int maxDepth = collections.size();
  int steps = 0;
  while (parent >= 0 && parent < collections.size() && steps < maxDepth) {
    chain.prepend(parent);
    const CollectionConfig &p = collections[parent];
    if (!p.isSubcollection) {
      break;
    }
    parent = p.parentCollectionIndex;
    ++steps;
  }
  return chain;
}

[[nodiscard]] inline QString selectionSessionKeyFor(const CollectionConfig &collection,
                                                    const QList<CollectionConfig> &collections) {
  const QString base = hierarchicalNameFor(collection, collections);
  QString subfolder = QDir::cleanPath(collection.currentSubfolder.trimmed());
  if (subfolder.isEmpty() || subfolder == ".") {
    return base;
  }
  return base + "|subfolder=" + subfolder;
}

[[nodiscard]] inline QList<int> directChildrenOf(int parentIndex,
                                                 const QList<CollectionConfig> &collections) {
  QList<int> children;
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == parentIndex) {
      children.append(i);
    }
  }
  return children;
}

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
[[nodiscard]] inline QString resolveArtworkDirectory(int collectionIndex,
                                                     const QList<CollectionConfig> &collections) {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }

  int current = collectionIndex;
  while (current >= 0 && current < collections.size()) {
    const CollectionConfig &c = collections[current];
    if (!c.artworkDirectory.trimmed().isEmpty()) {
      return c.artworkDirectory;
    }
    if (!c.isSubcollection || c.parentCollectionIndex < 0) {
      break;
    }
    current = c.parentCollectionIndex;
  }
  return {};
}

/**
 * @brief Resolves video directory for a collection, falling back to parent if
 * empty. Mirrors resolveArtworkDirectory; used by the sidebar so subcollections
 * inherit a parent's videoDirectory in showAllSubcollectionItems mode.
 */
[[nodiscard]] inline QString resolveVideoDirectory(int collectionIndex,
                                                   const QList<CollectionConfig> &collections) {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }
  int current = collectionIndex;
  while (current >= 0 && current < collections.size()) {
    const CollectionConfig &c = collections[current];
    if (!c.videoDirectory.trimmed().isEmpty()) {
      return c.videoDirectory;
    }
    if (!c.isSubcollection || c.parentCollectionIndex < 0) {
      break;
    }
    current = c.parentCollectionIndex;
  }
  return {};
}

[[nodiscard]] inline QString resolveManualDirectory(int collectionIndex,
                                                    const QList<CollectionConfig> &collections) {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }
  int current = collectionIndex;
  while (current >= 0 && current < collections.size()) {
    const CollectionConfig &c = collections[current];
    if (!c.manualDirectory.trimmed().isEmpty()) {
      return c.manualDirectory;
    }
    if (!c.isSubcollection || c.parentCollectionIndex < 0) {
      break;
    }
    current = c.parentCollectionIndex;
  }
  return {};
}

[[nodiscard]] inline QString resolvePlaceholderArtwork(int collectionIndex,
                                                       const QList<CollectionConfig> &collections) {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }

  int current = collectionIndex;
  while (current >= 0 && current < collections.size()) {
    const CollectionConfig &c = collections[current];
    if (!c.placeholderArtwork.trimmed().isEmpty()) {
      return c.placeholderArtwork;
    }
    if (!c.isSubcollection || c.parentCollectionIndex < 0) {
      break;
    }
    current = c.parentCollectionIndex;
  }
  return {};
}

/**
 * @brief Returns the effective category/type for a collection, walking up
 * the parent chain when the collection's own `type` field is empty.
 *
 * Kartend-dd8: the per-collection type is optional. Subcollections can either
 * declare their own type or inherit from the nearest non-empty ancestor —
 * this matches the user's mental model of "this whole branch is Games" while
 * still letting an oddball subcollection be tagged differently. Returns an
 * empty string when nothing in the chain is tagged.
 *
 * Cycle-safe: bounds the walk by `collections.size()` so a malformed
 * parentCollectionIndex chain can't loop forever.
 */
[[nodiscard]] inline QString effectiveCollectionType(int collectionIndex,
                                                     const QList<CollectionConfig> &collections) {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }
  int current = collectionIndex;
  const int maxDepth = collections.size();
  for (int steps = 0; steps < maxDepth; ++steps) {
    if (current < 0 || current >= collections.size()) {
      break;
    }
    const CollectionConfig &c = collections[current];
    if (!c.type.trimmed().isEmpty()) {
      return c.type.trimmed();
    }
    if (!c.isSubcollection || c.parentCollectionIndex < 0) {
      break;
    }
    current = c.parentCollectionIndex;
  }
  return {};
}

/**
 * @brief Returns the set of distinct non-empty `type` labels across the full
 * collection list (roots and subcollections), case-insensitive deduped and
 * sorted alphabetically. Used to populate the toolbar filter dropdown and
 * the per-collection editor's combobox completion (Kartend-dd8).
 */
[[nodiscard]] inline QStringList
collectAllCollectionTypes(const QList<CollectionConfig> &collections) {
  QStringList result;
  QSet<QString> seenLower;
  for (const CollectionConfig &c : collections) {
    QString trimmed = c.type.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    QString lower = trimmed.toLower();
    if (seenLower.contains(lower)) {
      continue;
    }
    seenLower.insert(lower);
    result.append(trimmed);
  }
  std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
  });
  return result;
}

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
