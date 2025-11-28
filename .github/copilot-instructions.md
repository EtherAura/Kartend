# Kartend - AI Coding Instructions

## Architecture Overview

Qt6 KDE frontend for organizing and launching multimedia collections. Uses **manager-based architecture** with dependency injection via setup structs.

### Manager Hierarchy

`MainWindow` owns `ApplicationManager`, which creates all managers in controlled destruction order (declared bottom-up in `applicationmanager.h`):

| Manager | Purpose | Key Signals |
|---------|---------|-------------|
| `NavigationManager` | Collection switching, navigation stack | `onCollectionSelected`, `onSubcollectionEntered` |
| `ScrollManager` | Virtual scrolling, grid layout | `widgetClicked`, `requestItemsRange` |
| `InteractionManager` | Input handling, selection state | `handleWidgetClicked`, `selectItemByIndex` |
| `SelectionManager` | Selection logic, click processing | `selectionChanged`, `requestCenterVertically` |
| `ViewportManager` | Centering, viewport positioning | `centerItemVertically`, `ensureItemVisible` |
| `KeyboardManager` | Arrow key navigation, key repeat | `repeatStep`, `stopRepeat` |
| `MouseManager` | Click hold scrolling, wheel events | `holdScrollingStarted`, `holdScrollingStopped` |
| `SearchManager` | Search bar logic, search modes | `requestSelectionRestore`, `requestScrollbarRecovery` |
| `EventManager` | Event filtering, gesture detection | `requestArrowKeyNavigation` |
| `AnimationManager` | Scroll animations, easing curves | `animateVerticalScroll` |
| `LaunchManager` | Item launching, process spawning | `launchItem` |
| `ArtworkManager` | Async artwork loading with `QtConcurrent` | `loadArtworkParallel`, `addPendingArtwork` |
| `DatabaseManager` | SQLite coordination via worker thread | `itemsLoaded`, `itemCountLoaded`, `itemsRangeLoaded` |
| `QueryManager` | Worker thread SQL queries for DatabaseManager | `itemsLoaded`, `itemsRangeLoaded` |
| `CacheManager` | In-memory pixmap cache, disk persistence | |
| `SessionManager` | Selection state persistence, counts caching | |
| `SettingsManager` | Config file I/O, settings dialog | |
| `SidebarManager` | Metadata sidebar visibility and positioning | |

### Adding Manager Dependencies

Use dedicated `*Setup` structs for dependency injection:
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

### Utility Modules (`src/utils/`)

| Utility | Purpose |
|---------|---------|
| `collectionutils.h` | `CollectionConfig`, `CollectionContext`, `CollectionHierarchyCache` structs and hierarchy helpers |
| `configutils.h` | Config parsing helpers, default value handling |
| `errorutils.h` | `ErrorCode` enum, `ErrorContext` struct, `Result<T>` template for structured error handling |
| `searchutils.h` | `SearchMode` enum, search context utilities |
| `stringutils.h` | String manipulation, title formatting |
| `settingsutils.h/.cpp` | Settings file path resolution, INI helpers |
| `pathutils.h/.cpp` | File path normalization, extension handling |
| `gridutils.h` | Grid layout calculations, row/column math |
| `extensionutils.h/.cpp` | File extension categorization, media type detection |
| `timerutils.h/.cpp` | `TimerUtils::Coordinator` for debounced updates, `TimerUtils::DebouncedTimer` for generic debouncing |
| `propertyutils.h` | `PropertyKeys` namespace with Qt dynamic property key constants |

### Key Data Structures (`src/utils/collectionutils.h`)

- `CollectionConfig` - Per-collection settings (paths, grid dimensions, appearance)
- `CollectionContext` - Runtime state (index, file lists, artwork mappings)
- `CollectionHierarchyCache` - Pre-computed parent/child lookups for performance

## Build Commands

**Preferred:** Use the build script in release mode (no flags):
```bash
.scripts/build.sh
```

Build script flags:
- `--debug` - Debug build with map file
- `--maintenance` - Warnings as errors, enables `--apply-fixes` and `--format-apply`
- `--pgo` - Profile-guided optimization (two-pass build)

**Manual builds:**
```bash
cd build/release && cmake ../.. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
```

## Testing

Tests are planned but not yet implemented.

## Code Conventions

### UI Constants

All magic numbers live in `UIConstants` namespace (`src/ui/uiconstants.h`). **Never hardcode** timing, spacing, or dimensions.

UIConstants is organized into logical sub-namespaces:

| Namespace | Purpose |
|-----------|---------|
| `UIConstants::Grid` | Grid layout (row height, spacing, margins) |
| `UIConstants::Item` | Item widget dimensions and padding |
| `UIConstants::Timing` | General timing constants (delays, debounce) |
| `UIConstants::Animation` | Animation durations and easing |
| `UIConstants::Keyboard` | Key repeat rates, arrow navigation timing |
| `UIConstants::Mouse` | Click hold, scroll step timing |
| `UIConstants::Search` | Search debounce, minimum query length |
| `UIConstants::Selection` | Selection overlay, highlight timing |
| `UIConstants::Navigation` | Collection navigation timing |
| `UIConstants::Artwork` | Artwork loading batch sizes, delays |
| `UIConstants::Cache` | Cache size limits, persistence timing |
| `UIConstants::Sidebar` | Sidebar dimensions and animation |
| `UIConstants::Viewport` | Viewport calculations, scroll margins |
| `UIConstants::Widget` | Widget pool sizes, creation limits |
| `UIConstants::Metadata` | Metadata sidebar update timing |
| `UIConstants::Color` | Transparency, overlay colors |
| `UIConstants::Placeholder` | Placeholder image dimensions |
| `UIConstants::Dialog` | Settings dialog dimensions |
| `UIConstants::Emoji` | Unicode emoji constants for UI |

Example usage:
```cpp
// Use namespaced constants
constexpr int delay = UIConstants::Timing::SEARCH_DEBOUNCE_DELAY_MS;
constexpr int batch = UIConstants::Artwork::BATCH_HIGH;

// Legacy aliases still work for backward compatibility
constexpr int delay = UIConstants::SEARCH_DEBOUNCE_DELAY_MS;
```

### Header Guards

Use `#ifndef CLASSNAME_H` pattern. Qt forward declarations with `QT_BEGIN_NAMESPACE`/`QT_END_NAMESPACE` blocks.

### Return Value Annotations

Use `[[nodiscard]]` on const getter methods and factory functions to ensure return values are used:
```cpp
[[nodiscard]] int getCurrentGridWidth() const;
[[nodiscard]] QString filePathForVisualIndex(int visualIndex) const;
[[nodiscard]] auto areItemsShared(int fromIndex, int toIndex) const -> bool;
```

### Threading Model

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

### Virtual Scrolling

`ScrollManager` only materializes visible rows. Widget recycling via pool:
```cpp
MediaItemWidget *ScrollManager::acquireWidget();  // Get from pool or create
void ScrollManager::releaseWidget(MediaItemWidget *widget);  // Return to pool
```

### Config File

INI format at `~/.config/EtherAura/Kartend/kartend.cfg`:
```ini
[%General]
MainScreen_gridWidth=6
rememberSelection=true

[CollectionName]
mediaDirectory=/path/to/roms
artworkDirectory=/path/to/covers
parentCollectionIndex=0
```

### Collection Hierarchy

Collections support parent-child relationships via `parentCollectionIndex`. Use:
- `directChildrenOf()` - Get immediate children
- `collectDescendantIndices()` - Recursive descendants
- `CollectionHierarchyCache` - Pre-computed lookups for performance

#### Using CollectionHierarchyCache

The hierarchy cache provides O(1) lookups for parent/child relationships. It's owned by `MainWindow` and rebuilt automatically when collections are modified:

```cpp
// Access the cache via MainWindow
const auto &cache = mainWindow->getHierarchyCache();

// O(1) lookup for direct children
const auto &children = cache.directChildren.value(parentIndex);

// O(1) lookup for all descendants
const auto &descendants = cache.allDescendants.value(parentIndex);

// O(1) lookup for parent collection
int parent = cache.parentIndex.value(childIndex, -1);
```

The cache is rebuilt via `rebuildHierarchyCache()` when `SettingsManager` emits `collectionsModified()`.

### Artwork Loading Pipeline

1. `ArtworkManager::addPendingArtwork()` queues items
2. `loadArtworkParallel()` processes batches asynchronously via `QtConcurrent`
3. `CacheManager` provides memory cache with disk persistence
4. Silent background loading via `processPersistentSilentLoad()`

## Common Tasks

### Adding a New Manager

1. Create header/source in `src/managers/`
2. Add `#include` and `std::unique_ptr` member to `ApplicationManager` (order matters for destruction)
3. Create `*Setup` struct with dependencies
4. Call `setupReferences()` from `MainWindow::setupManagers()`
5. Add to `CMakeLists.txt` SOURCES and HEADERS lists

### Adding New Collection Properties

1. Add field to `CollectionConfig` struct in `collectionutils.h`
2. Update `SettingsManager::loadCollectionConfig()` and `saveCollectionConfig()`
3. If UI-configurable, update `settingsdialog.ui` and `SettingsDialog` class

### Debugging Timing Issues

Use `TimerUtils::Coordinator` for debounced updates:
```cpp
m_timerCoordinator->scheduleViewportUpdate();  // Debounced, won't spam
```

Use `TimerUtils::DebouncedTimer` for generic debounce patterns:
```cpp
// Create timer with interval
m_debouncer = new TimerUtils::DebouncedTimer(100, this);
connect(m_debouncer, &TimerUtils::DebouncedTimer::triggered, this, &MyClass::onDebounced);

// Call trigger() repeatedly - only fires once after 100ms of inactivity
m_debouncer->trigger();
```

### Error Handling

Use `ErrorUtils` for structured error reporting:
```cpp
#include "errorutils.h"

// Create error context
auto ctx = ErrorUtils::ErrorContext::error(
    ErrorUtils::ErrorCode::DatabaseQueryFailed,
    "Failed to fetch items",
    "QueryManager::fetchItems"
).withDetails(query.lastError().text());

// Log with appropriate severity
ErrorUtils::logError(ctx);

// Use Result<T> for functions that can fail
ErrorUtils::Result<int> countItems() {
    if (!m_db.isOpen()) {
        return ErrorUtils::ErrorContext::error(
            ErrorUtils::ErrorCode::DatabaseNotOpen,
            "Database not open"
        );
    }
    return itemCount;
}
```

## Dependencies

- Qt6: Core, Gui, Widgets, Sql, Concurrent
- CMake 3.20+
- Clang or GCC with C++23 support
- lld linker (for release builds)

