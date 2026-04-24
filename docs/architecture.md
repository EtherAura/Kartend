# Architecture

Qt6 KDE application using manager-based architecture with dependency injection via setup structs.

## Overview

Kartend uses a **module-based architecture** where `MainWindow` owns `ApplicationManager`, which creates top-level managers in controlled destruction order. Managers communicate via Qt signals/slots and receive dependencies through dedicated setup structs.

## Project Structure

```
src/
├── core/           # Main application entry and window
├── modules/        # Feature modules (one folder per manager)
│   ├── animation/  # Scroll animations, easing curves
│   ├── application/# Manager lifecycle coordination
│   ├── artwork/    # Async artwork loading with QtConcurrent
│   ├── cache/      # In-memory pixmap cache, disk persistence
│   ├── database/   # SQLite coordination via worker thread
│   ├── event/      # Event filtering, gesture detection
│   ├── filter/     # Search and subcollection filtering
│   ├── interaction/# Input handling, selection state
│   ├── keyboard/   # Arrow key navigation, key repeat, alphabetic jumping
│   ├── launch/     # Item launching, process spawning
│   ├── mouse/      # Click hold scrolling, wheel events
│   ├── navigation/ # Collection switching, navigation stack
│   ├── overlay/    # Selection overlay rendering
│   ├── query/      # Worker thread SQL queries
│   ├── restore/    # Selection state restoration
│   ├── scroll/     # Virtual scrolling, grid layout, widget factory
│   ├── search/     # Search bar logic, search modes
│   ├── selection/  # Selection logic, click processing
│   ├── session/    # Selection state persistence
│   ├── settings/   # Config file I/O, settings dialog
│   ├── sidebar/    # Metadata sidebar visibility
│   ├── viewport/   # Centering, viewport positioning
│   └── widgetpool/ # Widget recycling pool
├── ui/             # UI components and constants
│   ├── dialogs/    # Settings dialog, error dialog, shortcuts dialog
│   └── widgets/    # Item widget, metadata sidebar, list header, overlays
└── utils/          # Shared utilities and data structures
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
| `sidebarmanager` | Controls metadata sidebar visibility, positioning, and content updates. |
| `filtermanager` | Applies search and subcollection filters to the active item set (helper owned by ScrollManager). |
| `widgetpoolmanager` | Recycles ItemWidget instances for virtual scrolling (helper owned by ScrollManager). |
| `datasourcemanager` | Owns FilterManager, ScrollDataManager, PreSearchStateManager, and SearchLoadingOverlay (helper extracted from ScrollManager). |
| `selectiondisplaymanager` | Owns SelectionOverlayManager, SelectionStateTracker, and ArtworkPreviewOverlay (helper extracted from ScrollManager). |
| `overlay` | SelectionOverlayManager / SearchLoadingOverlay rendering helpers for glide and loading visuals. |
| `restore` | SelectionRestoreManager coordinating selection restoration during navigation transitions. |

## Manager Hierarchy

**Two-tier ownership model:**
- **ApplicationManager** owns: `CacheManager`, `SessionManager`, `ArtworkManager`, `SettingsManager`, `DatabaseManager`, `ScrollManager`, `SidebarManager`, `NavigationManager`, `InteractionManager`
- **InteractionManager** owns: `SearchManager`, `SelectionManager`, `KeyboardManager`, `GamepadManager`, `ArrowNavigationHandler`, `AlphabeticNavigationHandler`, `AnimationManager`, `MouseManager`, `LaunchManager`, `ViewportManager`, `EventManager`

Additional helper managers owned by their parent feature module (not top-level): `WidgetPoolManager`, `FilterManager`, `SelectionRestoreManager`, `SelectionOverlayManager`, `SearchLoadingOverlay`, `NavigationStackManager`.

## UI (`src/ui/`)

| Component | Description |
|-----------|-------------|
| `uiconstants.h` | Centralized namespace for all UI timing, spacing, and dimension constants. |
| `settingsdialog` | Collection configuration dialog with tree-based hierarchy editing and live preview. |
| `metadatasidebar` | Displays file metadata, artwork preview, and item details in the sidebar panel. |
| `itemwidget` | Media item widget displaying artwork, title, and selection state with pulse animation. |

## Utilities (`src/utils/`)

| Utility | Description |
|---------|-------------|
| `collectionutils.h` | Defines CollectionConfig, CollectionContext, and CollectionHierarchyCache data structures. |
| `configutils.h` | Provides config path resolution and variable expansion for settings files. |
| `errorutils.h` | Structured error handling with ErrorCode enum, ErrorContext, and Result<T> template. |
| `searchutils.h` | Defines SearchMode enum and search context utilities for filtering operations. |
| `stringutils.h` | Formats numbers with comma separators for display in title counts. |
| `settingsutils` | Resolves settings file paths and provides INI file handling utilities. |
| `pathutils` | Provides file path normalization, extension handling, and path manipulation. |
| `gridutils.h` | Computes grid layout metrics including row/column positions and container sizes. |
| `extensionutils` | Categorizes file extensions by media type (ROM, disc image, archive, etc.). |
| `timerutils` | Provides debounced timer coordination for viewport and layout updates. |
| `propertyutils.h` | Defines PropertyKeys namespace with Qt dynamic property key constants. |
| `stateutils.h` | Centralized state structs for interaction, scroll, and selection state. |

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

Managers receive dependencies via dedicated setup structs:

```cpp
// In header: define setup struct with needed pointers
struct ScrollManagerSetup {
  QWidget *gridContainer = nullptr;
  ArtworkManager *artworkManager = nullptr;
  // ...
};

// In implementation: store references
void ScrollManager::setupReferences(const ScrollManagerSetup &setup) {
  m_gridContainer = setup.gridContainer;
  m_artworkManager = setup.artworkManager;
}
```

Setup calls are wired in `MainWindow::setupManagers()` and related methods.

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

For data integrity when writing to disk, use the atomic write pattern (temp file + rename):

```cpp
bool atomicWriteFile(const QString &filePath, const QByteArray &data) {
  QString tempPath = filePath + ".tmp";
  QFile tempFile(tempPath);
  if (!tempFile.open(QIODevice::WriteOnly)) return false;
  
  qint64 written = tempFile.write(data);
  tempFile.close();
  
  if (written != data.size()) {
    QFile::remove(tempPath);
    return false;
  }
  
  if (QFile::exists(filePath)) QFile::remove(filePath);
  return QFile::rename(tempPath, filePath);
}
```
