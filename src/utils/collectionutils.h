#ifndef COLLECTIONUTILS_H
#define COLLECTIONUTILS_H

#include <QDir>
#include <QHash>
#include <QMetaType>
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

enum class BackgroundType { Color = 0, Image = 1 };

/// View type for displaying collection items
enum class ViewType { Grid = 0, List = 1 };

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
  case ViewType::Grid:
  default:
    return "grid";
  }
}

[[nodiscard]] inline ViewType stringToViewType(const QString &str) {
  QString lower = str.toLower();
  if (lower == "list") return ViewType::List;
  return ViewType::Grid;
}

} // namespace CollectionUtils

/// Kartend-bdl: one entry in a collection's launcher list. The legacy primary
/// launcher (CollectionConfig::launcherPath/corePath/launchParameters) is
/// always present as launcher index 0; entries in
/// CollectionConfig::additionalLaunchers occupy indices 1..N. `name` is a
/// user-visible label shown in the launcher chooser; empty falls back to the
/// executable basename.
struct LauncherConfig {
  QString name;
  QString launcherPath;
  QString corePath;
  QString launchParameters;

  bool operator==(const LauncherConfig &other) const {
    return name == other.name && launcherPath == other.launcherPath &&
           corePath == other.corePath && launchParameters == other.launchParameters;
  }
};

struct CollectionConfig {
  QString name;
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
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Center;
  SidebarMode sidebarMode = SidebarMode::Overlay;
  ViewType viewType = ViewType::Grid; // Grid (default) or List view
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

  CollectionConfig()
      : gridWidth(4), sidebarVisible(false), horizontalAlignment(HorizontalAlignment::Center) {}

  bool operator==(const CollectionConfig &other) const {
    return name == other.name && launcherPath == other.launcherPath && corePath == other.corePath &&
           launchParameters == other.launchParameters && launcherName == other.launcherName &&
           additionalLaunchers == other.additionalLaunchers &&
           defaultLauncherIndex == other.defaultLauncherIndex &&
           mediaDirectory == other.mediaDirectory && artworkDirectory == other.artworkDirectory &&
           videoDirectory == other.videoDirectory &&
           manualDirectory == other.manualDirectory &&
           placeholderArtwork == other.placeholderArtwork &&
           collectionIcon == other.collectionIcon && extensions == other.extensions &&
           customArtworkTypes == other.customArtworkTypes && gridWidth == other.gridWidth &&
           sidebarVisible == other.sidebarVisible &&
           parentCollectionIndex == other.parentCollectionIndex &&
           isSubcollection == other.isSubcollection &&
           showAllSubcollectionItems == other.showAllSubcollectionItems &&
           hideTitles == other.hideTitles &&
           hideSubcollectionTitles == other.hideSubcollectionTitles &&
           horizontalAlignment == other.horizontalAlignment && sidebarMode == other.sidebarMode &&
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
           listAltRowColor == other.listAltRowColor && customFontFamily == other.customFontFamily;
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
      return LauncherConfig{this->launcherName, this->launcherPath, this->corePath,
                            this->launchParameters};
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
  int attractModeScrollSpeed = 1;      // Pixels per tick (1-10)

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

  [[nodiscard]] QList<int> directChildren(int parentIndex) const {
    return m_directChildren.value(parentIndex);
  }

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
  QList<int> computeDescendants(int parentIndex) const {
    QList<int> result;
    QList<int> stack = m_directChildren.value(parentIndex);
    while (!stack.isEmpty()) {
      int idx = stack.takeFirst();
      result.append(idx);
      stack.append(m_directChildren.value(idx));
    }
    return result;
  }

  const QList<CollectionConfig> *m_collections = nullptr;
  QHash<int, QList<int>> m_directChildren;
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
