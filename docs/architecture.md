# Architecture

Qt6 KDE application using manager-based architecture with dependency injection via setup structs.

## Overview

Kartend uses a **module-based architecture** where `MainWindow` owns `ApplicationManager`, which creates top-level managers in controlled destruction order. Managers communicate via Qt signals/slots and receive dependencies through dedicated setup structs.

## Project Structure

```
src/
├── core/                # Main application entry and window
├── modules/             # Feature modules grouped by domain
│   ├── behavior/        # App lifecycle, animation, search, launch, event filtering
│   │   ├── animation/   # Scroll animations, easing curves
│   │   ├── application/ # Manager lifecycle coordination
│   │   ├── event/       # Event filtering, gesture detection
│   │   ├── filter/      # Search and subcollection filtering
│   │   ├── launch/      # Item launching, process spawning
│   │   ├── search/      # Search bar logic, search modes
│   │   └── widgetpool/  # Widget recycling pool for ItemWidget reuse
│   ├── data/            # Persistence: SQLite, cache, sessions, settings, playlists
│   │   ├── cache/       # In-memory pixmap cache, disk persistence
│   │   ├── database/    # SQLite coordination via worker thread
│   │   ├── dat/         # Offline DAT-file identification + on-disk parse cache
│   │   ├── kart/        # Kart (collection bundle) import/export
│   │   ├── playlist/    # Playlist storage and export (JSON / M3U)
│   │   ├── query/       # Worker thread SQL queries
│   │   ├── restore/     # Selection state restoration during navigation
│   │   ├── scraper/     # Metadata scraping (core/ + parsers/ + providers/)
│   │   ├── session/     # Selection state persistence
│   │   └── settings/    # Config file I/O, settings dialog
│   ├── input/           # User input and navigation
│   │   ├── gamepad/     # Optional Qt6::Gamepad / SDL2 input backend
│   │   ├── interaction/ # Central input coordination, selection state
│   │   ├── keyboard/    # Arrow key navigation, key repeat, alphabetic jumping
│   │   ├── mouse/       # Click-hold scrolling, wheel events
│   │   ├── navigation/  # Collection switching, navigation stack
│   │   ├── scroll/      # Virtual scrolling, grid layout, widget factory
│   │   └── selection/   # Selection logic, click processing
│   └── media/           # Artwork pipeline, detail pages, overlays, viewport
│       ├── artwork/     # Async artwork loading with QtConcurrent
│       ├── attract/     # Attract-mode idle scroll/advance
│       ├── detailpage/  # Detail-page coordinator
│       ├── detailspane/ # Metadata / details side pane
│       ├── overlay/     # Selection / search loading overlays
│       └── viewport/    # Centering, viewport positioning
├── ui/                  # UI components and constants
│   ├── dialogs/         # Dialogs grouped by domain: settings/, collection/,
│   │                    # launcher/, scraper/, kart/ (loose dialogs at root)
│   └── widgets/         # Item widget, details pane, list header, overlays
└── utils/               # Shared utilities and data structures
```

## Core (`src/core/`)

| Component | Description |
|-----------|-------------|
| `main.cpp` | Application entry point that initializes Qt and displays the main window. |
| `mainwindow.cpp` | Main application window that owns ApplicationManager and orchestrates UI setup. |

## Modules (`src/modules/`)

| Manager | Description |
|---------|-------------|
| `applicationmanager` | Owns and coordinates all manager lifecycles with controlled destruction order. |
| `navigationmanager` | Manages collection switching, navigation stack, and subcollection hierarchy traversal. |
| `scrollmanager` | Manages virtual scrolling, widget pooling, and grid layout for large item collections. |
| `interactionmanager` | Orchestrates user interactions, delegating to specialized managers for input handling. |
| `selectionmanager` | Owns selection state and coordinates selection operations with visual feedback. |
| `viewportmanager` | Manages viewport positioning, item centering, and scroll-to-visible operations. |
| `keyboardmanager` | Handles keyboard input processing, arrow + alphabetic navigation, and key repeat. |
| `mousemanager` | Handles mouse input including click-hold scrolling, wheel events, and widget finding. |
| `gamepadmanager` | Optional gamepad backend (Qt6::Gamepad or SDL2) translating input to navigation events. |
| `searchmanager` | Handles search bar logic, search modes, and query debouncing for item filtering. |
| `eventmanager` | Filters and dispatches input events to specialized handlers for mouse, keyboard, and wheel. |
| `animationmanager` | Manages smooth scroll animations with easing curves for vertical and horizontal scrolling. |
| `launchmanager` | Launches media items with configured emulators, handling RetroArch cores and parameters. |
| `artworkmanager` | Handles async artwork loading with QtConcurrent, caching, and viewport-aware prioritization. |
| `databasemanager` | Coordinates SQLite database access via worker thread for collection metadata queries. |
| `querymanager` | Executes SQLite queries on worker thread for paginated item loading and filtering. |
| `cachemanager` | Manages in-memory pixmap cache with LRU eviction and optional disk persistence. |
| `sessionmanager` | Persists and restores selection state and item counts across application sessions. |
| `settingsmanager` | Handles config file I/O, collection settings, and the settings dialog interface. |
| `detailspanemanager` | Coordinates the details/metadata side pane (visibility, position, gallery content). |
| `filtermanager` | Applies search and subcollection filters to the active item set (helper owned by ScrollManager). |
| `widgetpoolmanager` | Recycles ItemWidget instances for virtual scrolling (helper owned by ScrollManager). |
| `datasourcemanager` | Owns FilterManager, ScrollDataManager, PreSearchStateManager, and SearchLoadingOverlay (helper extracted from ScrollManager). |
| `selectiondisplaymanager` | Owns SelectionOverlayManager, SelectionStateTracker, and ArtworkPreviewOverlay (helper extracted from ScrollManager). |
| `overlay` | SelectionOverlayManager / SearchLoadingOverlay rendering helpers for glide and loading visuals. |
| `restore` | SelectionRestoreManager coordinating selection restoration during navigation transitions. |

## Manager Hierarchy

**Two-tier ownership model:**
- **ApplicationManager** owns: `CacheManager`, `SessionManager`, `ArtworkManager`, `SettingsManager`, `DatabaseManager`, `ScrollManager`, `DetailsPaneManager`, `NavigationManager`, `InteractionManager`, `PlaylistManager`, `DetailPageManager`, `KartManager`
- **InteractionManager** owns: `SearchManager`, `SelectionManager`, `KeyboardManager`, `GamepadManager`, `ArrowNavigationHandler`, `AlphabeticNavigationHandler`, `AnimationManager`, `MouseManager`, `LaunchManager`, `ViewportManager`, `EventManager`, `AttractManager`

Additional helper managers owned by their parent feature module (not top-level): `WidgetPoolManager`, `FilterManager`, `SelectionRestoreManager`, `SelectionOverlayManager`, `SearchLoadingOverlay`, `NavigationStackManager`.

## UI (`src/ui/`)

| Component | Description |
|-----------|-------------|
| `uiconstants.h` | Centralized namespace for all UI timing, spacing, and dimension constants. |
| `settingsdialog` | Collection configuration dialog with tree-based hierarchy editing and live preview. |
| `detailspane` | Displays file metadata, artwork gallery, and item details in the side pane. |
| `itemwidget` | Media item widget displaying artwork, title, and selection state with pulse animation. |

## Utilities (`src/utils/`)

Utilities are grouped by concern in six subfolders.

### `src/utils/app/` — Application context, config, error model, logging

| Utility | Description |
|---------|-------------|
| `applicationcontext.h` | `ApplicationContext` struct (collection / ui / managers sub-structs) shared across managers. |
| `cliargs` | Command-line argument parsing for startup-collection overrides and headless Kart import/export. |
| `collectiontypes.h` | Standalone enums extracted from `collectionutils.h` (`HorizontalAlignment`, `DetailsPaneMode`, etc.). |
| `collectionutils` | `CollectionConfig`, `CollectionContext`, `CollectionHierarchyCache` plus hierarchy and validation helpers. |
| `errorutils.h` | Structured error handling: `ErrorCode` enum, `ErrorContext` struct, `Result<T>` template, `lcErrors` category. |
| `loggingcategories` | Cross-cutting `Q_LOGGING_CATEGORY` declarations (`lcPerfTrace`, `lcSearchDiag`, `lcScanFlow`). |
| `propertyutils.h` | `PropertyKeys` namespace with Qt dynamic property key constants. |
| `scrapelogger` | Optional scrape-diagnostic logging, driven by the scrape-logging setting. |
| `settingsutils` | Settings file path resolution and INI helpers. |
| `setuputils.h` | Macros that reduce setup-struct getter boilerplate (`SETUP_GETTER_*` family). |
| `stateutils.h` | Centralized state structs (`SelectionRestoreState`, `ScrollState`, …) replacing scattered dynamic properties. |

### `src/utils/db/` — SQLite schema and per-item stores

| Utility | Description |
|---------|-------------|
| `dbmigrations` | SQLite schema migration steps and `PRAGMA user_version` management. |
| `historystore` | `launch_history` table access. |
| `itemartwork` | `item_artwork` table — per-item artwork overrides with standard-type fallback. |
| `itemmetadata` | `item_metadata` table — custom titles, descriptions, genres, custom key/value fields. |
| `itemmetadatacache` | Per-item LRU (256 entries) fronting the per-item DB loads (metadata, artwork rows, usage stats). Invalidated on writes, rescans, reconnects. |
| `smartfilter` | Serializable filter spec driving a smart playlist's per-open query. |
| `smartplaylistevaluator` | Translates a `SmartFilter::Filter` into concrete (collection, item) matches. |
| `usagestatsstore` | `play_count`, `last_played`, `total_play_seconds` on the items table. |

### `src/utils/fs/` — Filesystem paths, validation, extension classification

| Utility | Description |
|---------|-------------|
| `configvalidation` | Schema validation of `CollectionConfig` plus `isCommandInPath()`. |
| `extensionutils` | File extension categorization by media type. |
| `launcherprobe` | PATH-probe for well-known launcher binaries. |
| `pathutils` | Path validation with `Result<T>` support, expansion, `syncDirectory()` for crash-safe writes. |
| `retroarchutils` | Discovery of a RetroArch install's libretro cores. |
| `romhasher` | Stream-hashing of media files for hash-based scraper lookups. |

### `src/utils/text/` — Search modes, string formatting, title filtering

| Utility | Description |
|---------|-------------|
| `searchutils.h` | `SearchMode` enum and search context utilities. |
| `stringutils.h` | String manipulation and title formatting helpers. |
| `titlefilter` | Process-wide title-cleanup engine; per-collection regex strip with read/write lock. |

### `src/utils/threading/` — Worker-thread orchestration and debouncing

| Utility | Description |
|---------|-------------|
| `adaptivebatcher.h` | Thread-safe batch-size controller with EMA-smoothed timing feedback. |
| `threadpoolutils` | Bounded-wait `QThreadPool` teardown with abandon-on-timeout fallback. |
| `timerutils` | `TimerUtils::Coordinator` for debounced viewport updates; `TimerUtils::DebouncedTimer` for generic debouncing. |

### `src/utils/view/` — Grid math, artwork / video helpers, viewport rendering

| Utility | Description |
|---------|-------------|
| `artworkutils` | Artwork file lookup, fuzzy matching, `Result<T>`-returning variants. |
| `gridutils.h` | Grid layout calculations, row/column math, container sizing. |
| `kdecolorscheme` | Discovery and parsing of KDE Plasma `.colors` scheme files. |
| `placeholderwarmer` | Batch pre-export of procedural placeholder artwork. |
| `textzoom` | Process-wide UI text zoom percentage (clamped to 50–300). |
| `videoutils` | Per-item preview-video file lookup (mirrors `artworkutils`). |

## Threading Model

- **Main thread**: All UI operations, manager coordination
- **Worker thread**: `DatabaseManager` → `QueryManager` via signal/slot (never direct calls)
- **QtConcurrent**: `ArtworkManager::loadArtworkParallel()` for batch image processing

Cross-thread communication pattern:

```cpp
// In DatabaseManager (main thread)
emit requestFetchItemsRange(context, collections, offset, limit, filter);

// In QueryManager (worker thread) - connected via Qt::QueuedConnection
void QueryManager::fetchItemsRange(...) { /* DB work */ }
emit itemsRangeLoaded(offset, filePaths, fileNames); // Back to main thread
```

## Dependency Injection

Managers receive dependencies via dedicated setup structs. Sibling-manager
pointers are *not* copied into the struct — they are read through the shared
`ApplicationContext` (`ctx`), which is the single source of truth. The setup
struct carries only `ctx` plus non-manager refs (UI widgets, collection-state
pointers, callbacks):

```cpp
// In header: setup struct carries ctx + non-manager refs only.
struct ScrollManagerSetup {
  const ApplicationContext *ctx = nullptr;
  QWidget *gridContainer = nullptr;
  // ... non-manager refs only
};

// In implementation: store ctx, then non-manager fields.
void ScrollManager::setupReferences(const ScrollManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_gridContainer = setup.gridContainer;
}

// Read siblings through ctx at the point of use.
if (auto *art = m_ctx ? m_ctx->artworkManager() : nullptr) {
  art->scheduleViewportUpdate();
}
```

`MainWindow::initializeAppContext()` populates `ctx->managers.*` before any
`setupReferences()` runs; setup calls are wired in
`MainWindow::setupManagers()` and related methods.

## Key Design Patterns

### Single Source of Truth

Avoid duplicating state across managers:
- `SelectionManager` owns selection restore state
- `DatabaseManager` owns file path resolution
- `InteractionManager` owns `InteractionStateHolder` for centralized interaction state

### Virtual Scrolling

`ScrollManager` only materializes visible rows. Widget recycling via pool:

```cpp
ItemWidget *ScrollManager::acquireWidget();  // Get from pool or create
void ScrollManager::releaseWidget(ItemWidget *widget);  // Return to pool
```

### Atomic File Writes

For data integrity when writing to disk, use `QSaveFile` (which manages the
temp-file + atomic-rename internally) plus `PathUtils::syncDirectory()` so
the rename survives crash / power loss:

```cpp
#include <QSaveFile>
#include "utils/fs/pathutils.h"

bool atomicWriteFile(const QString &filePath, const QByteArray &data) {
  const QString parentDir = QFileInfo(filePath).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) return false;

  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  if (file.write(data) != data.size()) {
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) return false;
  PathUtils::syncDirectory(parentDir);
  return true;
}
```

Adopters: `SessionManager`, `PlaylistManager`, `KartWriter` / `KartReader`,
`CacheDiskStorage`.
