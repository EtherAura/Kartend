#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONCONFIG_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONCONFIG_H

// Per-collection configuration god-struct extracted from collectionutils.h
// (Kartend-0yz3 step 11). Embeds every leaf sub-cluster extracted in the
// earlier steps (LauncherProfile / GridLayoutPreferences / SidebarAppearance
// / CollectionBackground / ArchiveOptions / FolderBrowsingOptions /
// ListViewOptions / ScraperOverrides / CollectionFilterPreferences) plus the
// per-collection scalar fields (name/type, media + artwork + video + manual
// directories, extensions/customArtworkTypes lists, the parentage/playlist/
// alias scalars, and the horizontalAlignment + viewType + hideMissingArtwork
// + customFontFamily knobs). Kept in its own translation-unit-input so the
// rest of the codebase can `#include "collection/collectionconfig.h"` to get
// CollectionConfig without dragging in CollectionContext + GeneralSettings +
// the hierarchy cache + the standalone helper namespaces — all of which
// still live in the umbrella collectionutils.h for now.

#include <algorithm>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "../collectiontypes.h"
#include "archiveoptions.h"
#include "collectionbackground.h"
#include "collectionfilterpreferences.h"
#include "folderbrowsingoptions.h"
#include "gridlayoutpreferences.h"
#include "launcherconfig.h"
#include "listviewoptions.h"
#include "scraperoverrides.h"
#include "sidebarappearance.h"
#include <uiconstants/detailspaneconstants.h>
#include <uiconstants/grid.h>
#include <uiconstants/item.h>
#include <uiconstants/listview.h>

struct CollectionConfig {
  QString name;
  /// Free-form category/type tag. Empty for an untagged
  /// collection — subcollections inherit the nearest non-empty ancestor type
  /// for filter purposes (see CollectionUtils::effectiveCollectionType). The
  /// value is just a user-chosen label like "Games" / "Movies" / "Music"; the
  /// app does not interpret it semantically.
  QString type;
  /// Launcher cluster — primary launcher (path/core/parameters/name),
  /// additional launchers, and the chooser dialog's pre-selected index.
  /// Bundled into a focused sub-struct (Kartend-r4x5 follow-up: launcher):
  /// members are accessed via `cfg.launcher.launcherPath` /
  /// `cfg.launcher.additionalLaunchers` / etc. so the same INI keys
  /// (`launcherPath`, `corePath`, `launchParameters`, `launcherName`,
  /// `additionalLaunchers/*`, `defaultLauncherIndex`) and kart-manifest
  /// JSON keys round-trip unchanged.
  LauncherProfile launcher;
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
  /// Grid + item-layout cluster — items-per-row/-column (with sidebar-hidden
  /// alternates), spacing, item box dimensions + font + corner radius, and
  /// the scrollbar-hide toggles. Bundled into a focused sub-struct
  /// (Kartend-r4x5 follow-up: grid+layout): access as
  /// `cfg.gridLayout.gridWidth` / `cfg.gridLayout.itemWidth` etc. INI keys +
  /// kart-manifest JSON keys round-trip unchanged.
  GridLayoutPreferences gridLayout;
  int parentCollectionIndex = -1;
  bool isSubcollection = false;
  [[nodiscard]] bool hasParent() const { return parentCollectionIndex >= 0; }
  bool showAllSubcollectionItems = false;
  bool hideTitles = false;
  bool hideSubcollectionTitles = false;
  /// Title-cleanup filter — per-collection regex patterns plus a master
  /// toggle that lets the user pause the cleanup from the toolbar without
  /// dropping the pattern list. Bundled into a focused sub-struct as the
  /// first step of the CollectionConfig god-struct split: members are
  /// accessed via `cfg.filter.titleExclusionPatterns` /
  /// `cfg.filter.titleExclusionEnabled` so the same INI keys
  /// (`titleExclusionPatterns/*` and `titleExclusionEnabled`) round-trip
  /// without a config-format change.
  ///
  /// titleExclusionPatterns: regex patterns stripped from displayed item
  /// titles, processed in order via QString::remove(). Common use is
  /// dropping region tags like `\s*\(USA\)` or revision tags like
  /// `\s*\[!\]` so only the canonical title shows in the grid/list.
  /// Patterns are stored verbatim (no escaping) — the user types regex
  /// syntax directly in the toolbar popup. Invalid patterns are skipped
  /// at compile time rather than aborting the whole list.
  ///
  /// titleExclusionEnabled: master switch for the exclusion list. When
  /// false the patterns persist but are not applied — lets the user
  /// toggle the cleanup from the toolbar without losing their pattern
  /// list.
  CollectionFilterPreferences filter;
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Center;
  /// Sidebar (details-pane) appearance cluster — visibility, dock mode /
  /// position, background + pattern, text/accent colors, header/section
  /// bubble color+opacity, width/height + lock, active tab, and the
  /// per-collection font override. Bundled into a focused sub-struct
  /// (Kartend-r4x5 follow-up: sidebar appearance): members accessed as
  /// `cfg.sidebar.sidebarMode` etc. so the same INI keys + kart-manifest
  /// JSON keys (`sidebarMode`, `sidebarPosition`, `sidebarBackgroundColor`,
  /// `sidebar_font_family`, …) round-trip unchanged.
  SidebarAppearance sidebar;
  ViewType viewType = ViewType::Grid; // Grid (default) or List view
  /// when true, media items whose artwork lookup returns no
  /// match are hidden from the items page. Subcollections and virtual folders
  /// are unaffected. The filter combines with the active search /
  /// subcollection filter as an additional predicate, so search results are
  /// also pruned to items that have artwork.
  bool hideMissingArtwork = false;

  /// View background / wallpaper cluster — items-page background
  /// (color/image/video), primary/tile/selection palette, header logo,
  /// vignette overlay, wallpaper-parallax controls, and toolbar
  /// backdrop-blur knobs. Bundled into a focused sub-struct (Kartend-r4x5
  /// follow-up: view+background): access as `cfg.background.backgroundColor`
  /// etc. INI keys + kart-manifest JSON keys round-trip unchanged.
  CollectionBackground background;

  /// Archive-extraction cluster — toggles for cores that don't grok zipped
  /// content. Access as `cfg.archive.extractArchives` /
  /// `cfg.archive.extractedExtension`. INI keys (`extractArchives`,
  /// `extractedExtension`) round-trip unchanged.
  ArchiveOptions archive;

  // Expand mode: two-stage activation. When enabled, the first activation
  // (double-click or Enter) on a selected item shows the artwork preview
  // overlay instead of launching; a second activation on the same item
  // launches it. Selection change resets the expanded state.
  bool expandMode = false;

  /// When true, Kartend installs a QFileSystemWatcher on this collection's
  /// mediaDirectory tree at startup and re-runs forceRescanCollection
  /// (debounced) whenever a watched file/directory change fires. Disabled by
  /// default so the existing pull-based refresh behavior is preserved unless
  /// the user opts in per collection.
  bool watchFilesystem = false;

  /// Virtual-folder browsing cluster — the persisted "treat subfolders as
  /// virtual navigable folders" toggles plus the runtime currentSubfolder
  /// cursor. Access as `cfg.folderBrowsing.includeContentSubfolders` /
  /// `cfg.folderBrowsing.currentSubfolder`. INI keys round-trip unchanged.
  FolderBrowsingOptions folderBrowsing;

  /// List-view appearance overrides. Only consulted when
  /// `viewType == ViewType::List`. Access as `cfg.listView.listFontSize` etc.
  ListViewOptions listView;

  // Text appearance settings (per-collection)
  QString customFontFamily; // Custom font family (empty = system default)

  // ─── Sidebar font ───────────────────────────────────────────
  // Per-collection override for the metadata sidebar's text now lives on
  // the SidebarAppearance sub-struct (`cfg.sidebar.sidebarFontFamily` /
  // `cfg.sidebar.sidebarFontPointSize`). The font fields were folded into
  // the sidebar cluster to keep a collection's sidebar look fully
  // self-contained.

  // ─── Playlist support ───────────────────────────────────────
  // Runtime-only marker for synthesized "virtual collection" entries backed by
  // the SQLite `playlists` table instead of an INI section. Synthesised at
  // startup by PlaylistManager and appended to MainWindow::m_collections so
  // playlists nest into the hierarchy / appear as tiles like ordinary
  // subcollections. SettingsManager::saveCollections() MUST skip configs with
  // isPlaylist=true so they never round-trip into kartend.cfg.
  bool isPlaylist = false;
  QString playlistId; // UUID — matches playlists.id when isPlaylist is true.

  /// Scraper / DAT override cluster — pinning a scraper provider,
  /// overriding ScreenScraper's system id, hash-archive policy, and the
  /// DAT-file priority list. Access as
  /// `cfg.scraperOverrides.screenscraperSystemId` /
  /// `cfg.scraperOverrides.datFilePaths` / etc. INI keys + kart-manifest
  /// JSON keys (`screenscraperSystemId`, `screenscraperHashArchive`,
  /// `datFilePaths`, `scraperProviderId`) round-trip unchanged.
  ScraperOverrides scraperOverrides;
  /// Marks the synthesized config as a smart playlist (filter-driven).
  /// Set during MainWindow::resyncPlaylistCollections from the
  /// PlaylistRow.isSmart flag. UI surfaces (right-click menu, sidebar
  /// label) branch on this so static-only actions like "Add item to
  /// playlist" stay hidden for smart rows. Runtime-only, never persisted
  /// to INI.
  bool isSmartPlaylist = false;

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

  /// Pass-through bag for keys the load path read out of the collection's
  /// INI section that this build doesn't otherwise consume. Strict
  /// round-trip preservation: a newer build's key (e.g. a future feature
  /// flag) read by this older build is stashed here and re-emitted on
  /// save, so a save-by-older-build doesn't silently delete it. Only flat
  /// child keys are preserved — nested arrays (additionalLaunchers/N/*)
  /// are handled by their own load/save paths. Excluded from operator==
  /// because the bag has no semantic meaning to runtime code.
  QHash<QString, QVariant> preservedKeys;

  bool operator==(const CollectionConfig &other) const {
    return name == other.name && type == other.type && launcher == other.launcher &&
           mediaDirectory == other.mediaDirectory && artworkDirectory == other.artworkDirectory &&
           videoDirectory == other.videoDirectory && manualDirectory == other.manualDirectory &&
           placeholderArtwork == other.placeholderArtwork &&
           collectionIcon == other.collectionIcon && extensions == other.extensions &&
           customArtworkTypes == other.customArtworkTypes && gridLayout == other.gridLayout &&
           parentCollectionIndex == other.parentCollectionIndex &&
           isSubcollection == other.isSubcollection &&
           showAllSubcollectionItems == other.showAllSubcollectionItems &&
           hideTitles == other.hideTitles &&
           hideSubcollectionTitles == other.hideSubcollectionTitles && filter == other.filter &&
           horizontalAlignment == other.horizontalAlignment && sidebar == other.sidebar &&
           viewType == other.viewType && background == other.background &&
           archive == other.archive && expandMode == other.expandMode &&
           watchFilesystem == other.watchFilesystem && folderBrowsing == other.folderBrowsing &&
           listView == other.listView && customFontFamily == other.customFontFamily &&
           isPlaylist == other.isPlaylist && playlistId == other.playlistId &&
           isSmartPlaylist == other.isSmartPlaylist &&
           playlistReservedKind == other.playlistReservedKind &&
           scraperOverrides == other.scraperOverrides &&
           additionalParentNames == other.additionalParentNames;
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
    // Grid-width fields have no upper cap — wider-than-40 layouts are
    // legitimate on 4K/8K displays. Only the MIN_WIDTH floor is enforced
    // so a hand-edit or kart-import can't pin a non-zero value at 0
    // columns. The 0-sentinel ("inherit primary") stays untouched for the
    // alt fields.
    gridLayout.gridWidth = std::max(gridLayout.gridWidth, UIConstants::Grid::MIN_WIDTH);
    if (gridLayout.horizontalGridHeight != 0) {
      gridLayout.horizontalGridHeight =
          std::max(gridLayout.horizontalGridHeight, UIConstants::Grid::MIN_WIDTH);
    }
    if (gridLayout.gridWidthSidebarHidden != 0) {
      gridLayout.gridWidthSidebarHidden =
          std::max(gridLayout.gridWidthSidebarHidden, UIConstants::Grid::MIN_WIDTH);
    }
    if (gridLayout.horizontalGridHeightSidebarHidden != 0) {
      gridLayout.horizontalGridHeightSidebarHidden =
          std::max(gridLayout.horizontalGridHeightSidebarHidden, UIConstants::Grid::MIN_WIDTH);
    }
    if (gridLayout.gridHeightSidebarHidden != 0) {
      gridLayout.gridHeightSidebarHidden =
          std::max(gridLayout.gridHeightSidebarHidden, UIConstants::Grid::MIN_WIDTH);
    }
    gridLayout.itemWidth = std::clamp(gridLayout.itemWidth, UIConstants::Item::MIN_WIDTH,
                                      UIConstants::Item::MAX_WIDTH);
    gridLayout.itemHeight = std::clamp(gridLayout.itemHeight, UIConstants::Item::MIN_HEIGHT,
                                       UIConstants::Item::MAX_HEIGHT);
    gridLayout.fontSize = std::clamp(gridLayout.fontSize, UIConstants::Item::MIN_FONT_SIZE,
                                     UIConstants::Item::MAX_FONT_SIZE);
    gridLayout.cornerRadius =
        std::clamp(gridLayout.cornerRadius, UIConstants::Item::MIN_CORNER_RADIUS,
                   UIConstants::Item::MAX_CORNER_RADIUS);
    // Spacing can be negative for overlap effects
    gridLayout.horizontalSpacing = std::clamp(gridLayout.horizontalSpacing, -100, 200);
    gridLayout.verticalSpacing = std::clamp(gridLayout.verticalSpacing, -100, 200);
    // List mode settings
    listView.listFontSize = std::clamp(listView.listFontSize, UIConstants::Item::MIN_FONT_SIZE,
                                       UIConstants::Item::MAX_FONT_SIZE);
    listView.listRowHeight =
        std::clamp(listView.listRowHeight, UIConstants::ListView::MIN_ROW_HEIGHT,
                   UIConstants::ListView::MAX_ROW_HEIGHT);
    sidebar.sidebarWidth = std::max(sidebar.sidebarWidth, UIConstants::DetailsPane::MIN_WIDTH);
    // floor pane height; same no-upper-bound treatment as width.
    sidebar.sidebarHeight = std::max(sidebar.sidebarHeight, UIConstants::DetailsPane::MIN_HEIGHT);
    // corner darkness percent. 0 = effect off (the toggle is
    // separate); 100 = pitch black at the corners.
    background.vignetteIntensity = std::clamp(background.vignetteIntensity, 0, 100);
    // parallax strength percent.
    background.parallaxStrength = std::clamp(background.parallaxStrength, 0, 100);
    // backdrop blur radius. 4 is a barely-perceptible blur;
    // anything above 32 is heavy enough that the source becomes unreadable.
    background.backdropBlurRadius = std::clamp(background.backdropBlurRadius, 4, 32);
    // keep the default-launcher pointer inside the visible list
    // so a stale config (or a deletion that out-paced the index) can never
    // refer past the end. 0 is always valid because the primary slot exists
    // even when its launcherPath is empty.
    launcher.defaultLauncherIndex =
        std::clamp(launcher.defaultLauncherIndex, 0, launcher.launcherCount() - 1);
  }
};

#endif
