# Kartend - AI Coding Instructions

## Architecture Overview

Qt6 KDE frontend for organizing and launching multimedia collections. Uses **module-based architecture** with dependency injection via setup structs.

### Project Structure

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
│   ├── interaction/# Input handling, selection state
│   ├── keyboard/   # Arrow key navigation, key repeat
│   ├── launch/     # Item launching, process spawning
│   ├── mouse/      # Click hold scrolling, wheel events
│   ├── navigation/ # Collection switching, navigation stack
│   ├── query/      # Worker thread SQL queries
│   ├── scroll/     # Virtual scrolling, grid layout, widget factory
│   ├── search/     # Search bar logic, search modes
│   ├── selection/  # Selection logic, click processing
│   ├── session/    # Selection state persistence
│   ├── settings/   # Config file I/O, settings dialog
│   ├── sidebar/    # Metadata sidebar visibility
│   └── viewport/   # Centering, viewport positioning
├── ui/             # UI components and constants
│   ├── dialogs/    # Settings dialog
│   └── widgets/    # Item widget, metadata sidebar
└── utils/          # Shared utilities and data structures
```

### Manager Hierarchy

`MainWindow` owns `ApplicationManager`, which creates top-level managers in controlled destruction order (declared bottom-up in `applicationmanager.h`).

**Two-tier ownership model:**
- **ApplicationManager** owns: `CacheManager`, `SessionManager`, `ArtworkManager`, `SettingsManager`, `DatabaseManager`, `ScrollManager`, `SidebarManager`, `NavigationManager`, `InteractionManager`
- **InteractionManager** owns: `SearchManager`, `SelectionManager`, `KeyboardManager`, `ArrowNavigationHandler`, `AnimationManager`, `MouseManager`, `LaunchManager`, `ViewportManager`, `EventManager`

**Sub-manager registration:** InteractionManager's owned sub-managers are registered in `ApplicationContext` after setup, enabling sibling access via ctx:
```cpp
// In MainWindow::setupManagerConnections(), after InteractionManager setup:
m_appContext.animationManager = getInteractionManager()->animationManager();
m_appContext.selectionManager = getInteractionManager()->selectionManager();
// ... etc
```

| Manager | Owner | Key Signals |
|---------|-------|-------------|
| `NavigationManager` | ApplicationManager | `onCollectionSelected`, `onSubcollectionEntered` |
| `ScrollManager` | ApplicationManager | `widgetClicked`, `requestItemsRange` |
| `InteractionManager` | ApplicationManager | `handleWidgetClicked`, `selectItemByIndex` |
| `ArrowNavigationHandler` | InteractionManager | `requestFullSelectionUpdate`, `requestRecenter` |
| `SelectionManager` | InteractionManager | `selectionChanged`, `requestCenterVertically` |
| `ViewportManager` | InteractionManager | `centerItemVertically`, `ensureItemVisible` |
| `KeyboardManager` | InteractionManager | `repeatStep`, `stopRepeat` |
| `MouseManager` | InteractionManager | `holdScrollingStarted`, `holdScrollingStopped` |
| `SearchManager` | InteractionManager | `requestSelectionRestore`, `requestScrollbarRecovery` |
| `EventManager` | InteractionManager | `requestArrowKeyNavigation` |
| `AnimationManager` | InteractionManager | `animateVerticalScroll` |
| `LaunchManager` | InteractionManager | `launchItem` |
| `ArtworkManager` | ApplicationManager | `loadArtworkParallel`, `addPendingArtwork` |
| `DatabaseManager` | ApplicationManager | `itemsLoaded`, `itemCountLoaded`, `itemsRangeLoaded`, `resolveFilePath` |
| `QueryManager` | DatabaseManager (worker thread) | `itemsLoaded`, `itemsRangeLoaded` |
| `CacheManager` | ApplicationManager | |
| `SessionManager` | ApplicationManager | |
| `SettingsManager` | ApplicationManager | |
| `SidebarManager` | ApplicationManager | |

### Atomic File Writes Pattern

For data integrity when writing to disk, use the atomic write pattern (temp file + rename):

```cpp
// In SessionManager::atomicWriteFile()
bool atomicWriteFile(const QString &filePath, const QByteArray &data) {
  QString tempPath = filePath + ".tmp";
  
  // Write to temporary file first
  QFile tempFile(tempPath);
  if (!tempFile.open(QIODevice::WriteOnly)) {
    return false;
  }
  qint64 written = tempFile.write(data);
  tempFile.close();
  
  if (written != data.size()) {
    QFile::remove(tempPath);  // Clean up partial write
    return false;
  }
  
  // Remove existing file, then atomic rename
  if (QFile::exists(filePath)) {
    QFile::remove(filePath);
  }
  return QFile::rename(tempPath, filePath);
}
```

This pattern prevents data corruption if the application crashes during write.

### State Ownership

**Single source of truth pattern:** Avoid duplicating state across managers.
- `SelectionManager` owns selection restore state (`m_restoringSelection`, `m_targetRestoreIndex`)
- `ViewportManager` queries `SelectionManager` rather than maintaining copies
- `DatabaseManager` owns file path resolution (relative→absolute path mapping)
- `InteractionManager` owns `InteractionStateHolder` for centralized interaction state
- Use `stateutils.h` structs for complex state that spans multiple concerns

### InteractionStateHolder

Centralized state holder replacing scattered Qt dynamic properties with typed state:

```cpp
// Access via InteractionManager
InteractionStateHolder &state = interactionManager->state();

// Reading state - typed, discoverable
if (state.isSelectionSuppressed()) {
  int pending = state.pendingSelectionIndex();
}

// Writing state - uses helper methods
state.beginSelectionSuppression(pendingIndex);
state.suppressArrowCenterFor(220);  // milliseconds
state.setGlideAnimating(true);

// Direct struct access for complex state
state.click().rowChangeFirstClickIndex = -1;
state.scroll().programmaticScroll = true;
```

State structs from `stateutils.h`:
- `SelectionRestoreState` - Selection restoration during navigation
- `ScrollState` - Scroll flags (programmatic, user, continuous)
- `ArtworkState` - Artwork suppression during animations
- `ArrowNavigationState` - Arrow key centering suppression
- `ClickState` - Click deferral, double-click detection
- `StreamScrollState` - Streaming/hold scroll velocity state
- `SearchState` - Search bar state (e.g., cleared by escape)

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

### Scroll Module (`src/modules/scroll/`)

The scroll module handles virtual scrolling with widget pooling:

| Class | Purpose |
|-------|---------|
| `ScrollManager` | Core orchestration of virtual scrolling, viewport updates |
| `GridLayoutCalculator` | Stateless grid metric calculations (row/column positions) |
| `ItemWidgetFactory` | Widget creation and configuration, delegates to pool |
| `WidgetPoolManager` | Widget recycling pool for performance |
| `VirtualContainerManager` | Container lifecycle and sizing |
| `SelectionCoordinator` | Selection state and movement analysis |
| `SelectionOverlayManager` | Glide animation overlay rendering |
| `ScrollEventHandler` | Scroll event wiring and user scroll detection |
| `FilterManager` | Search and subcollection filtering |

### Interaction Module (`src/modules/interaction/`)

The interaction module coordinates user input handling:

| Class | Purpose |
|-------|---------|
| `InteractionManager` | Central coordinator for input handling, owns sub-managers |
| `InteractionStateHolder` | Centralized typed state replacing scattered dynamic properties |

### Database Module (`src/modules/database/`, `src/modules/query/`)

Database operations use a worker thread pattern:

| Class | Purpose |
|-------|---------|
| `DatabaseManager` | Main thread coordinator, emits signals to worker |
| `QueryManager` | Worker thread, executes SQL queries, emits results |

All database errors use structured `ErrorContext` reporting via `errorutils.h`.

#### Database Reconnection Pattern

`QueryManager` implements automatic reconnection for transient database failures:

```cpp
// ensureDatabaseConnection() attempts reconnection with retry logic
// Called at the start of each query operation
if (!ensureDatabaseConnection()) {
  initDatabase();
  if (!m_db.isOpen()) {
    emit itemCountLoaded(0);  // Return safe default
    return;
  }
}
```

The reconnection logic:
- Attempts up to 3 reconnections with 100ms delay between attempts
- Clears the prepared statement cache on reconnection
- Re-initializes PRAGMAs (foreign_keys, journal_mode, synchronous)
- Logs reconnection attempts with `ErrorContext` for debugging

Error codes for connection state:
- `DatabaseConnectionLost` - Connection was lost, attempting reconnection
- `DatabaseConnectionRestored` - Reconnection succeeded
- `DatabaseConnectionFailed` - All reconnection attempts failed

#### ErrorContext Signal Propagation

Database errors propagate through the signal chain as full `ErrorContext` objects:

```cpp
// In QueryManager (worker thread)
void errorOccurred(const ErrorUtils::ErrorContext &error);  // Signal

// In DatabaseManager (main thread) - forwards to NavigationManager
connect(m_worker, &QueryManager::errorOccurred, 
        this, &DatabaseManager::errorOccurred);

// In NavigationManager - displays to user
void NavigationManager::onMediaLibraryError(const ErrorUtils::ErrorContext &error) {
  // Show error dialog with full context
  showErrorDialog(error.message, error.details);
}
```

Register `ErrorContext` for cross-thread signals:
```cpp
// In QueryManager constructor
qRegisterMetaType<ErrorUtils::ErrorContext>("ErrorUtils::ErrorContext");
```

### Utility Modules (`src/utils/`)

| Utility | Purpose |
|---------|---------|
| `collectionutils.h` | `CollectionConfig`, `CollectionContext`, `CollectionHierarchyCache` structs and hierarchy helpers |
| `configutils.h` | Config parsing helpers, default value handling |
| `errorutils.h` | `ErrorCode` enum, `ErrorContext` struct, `Result<T>` template for structured error handling |
| `searchutils.h` | `SearchMode` enum, search context utilities |
| `stringutils.h` | String manipulation, title formatting |
| `settingsutils.h/.cpp` | Settings file path resolution, INI helpers |
| `pathutils.h/.cpp` | File path validation with `Result<T>` support, extension handling |
| `gridutils.h` | Grid layout calculations, row/column math |
| `extensionutils.h/.cpp` | File extension categorization, media type detection |
| `timerutils.h/.cpp` | `TimerUtils::Coordinator` for debounced updates, `TimerUtils::DebouncedTimer` for generic debouncing |
| `propertyutils.h` | `PropertyKeys` namespace with Qt dynamic property key constants |
| `stateutils.h` | Centralized state structs (`SelectionRestoreState`, `ScrollState`, etc.) to replace scattered dynamic properties |
| `applicationcontext.h` | `ApplicationContext` struct for shared dependencies across managers |

### Key Data Structures (`src/utils/collectionutils.h`)

- `CollectionConfig` - Per-collection settings (paths, grid dimensions, appearance)
- `CollectionContext` - Runtime state (index, file lists, artwork mappings)
- `CollectionHierarchyCache` - Pre-computed parent/child lookups for performance

### Validation Helpers (`src/utils/collectionutils.h`)

Use these inline helpers instead of verbose null/bounds checks:

```cpp
// Validate collection index with null-safety
if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
  return;
}

// Overload for plain int (no pointer)
if (!CollectionUtils::isValidIndex(index, m_collections)) {
  return;
}

// Get grid width with null-safety (returns 0 if invalid)
int gridWidth = CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
```

## Build Commands

**Preferred:** Use the build script in release mode (no flags):
```bash
.scripts/build.sh
```

Build script flags:
- `--debug` - Debug build with map file, **required to see `qWarning()`/`qDebug()` output**
- `--maintenance` - Warnings as errors, enables `--apply-fixes` and `--format-apply`
- `--pgo` - Profile-guided optimization (two-pass build)

**Debug builds:** Use `--debug` flag when you need to see debug/warning output:
```bash
.scripts/build.sh --debug
```
The debug binary is at `build/debug/kartend`.

**Manual builds:**
```bash
cd build/release && cmake ../.. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
```

## Testing

Unit tests use the **Qt Test framework** with CTest integration.

### Test Structure

```
tests/
├── CMakeLists.txt              # Test build configuration
├── test_gridlayoutcalculator.cpp  # Grid layout math tests
└── test_interactionstateholder.cpp # State holder tests
```

### Building Tests

Enable tests with the `BUILD_TESTS` CMake flag:
```bash
cd build/release
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make test_gridlayoutcalculator test_interactionstateholder
```

### Running Tests

Run individual test:
```bash
./tests/test_gridlayoutcalculator
./tests/test_interactionstateholder
```

Run all tests via CTest:
```bash
cd tests && ctest --output-on-failure
```

### Adding New Tests

1. Create `tests/test_<classname>.cpp` with Qt Test structure:
```cpp
#include <QTest>
#include "classname.h"

class TestClassName : public QObject {
  Q_OBJECT
private slots:
  void testSomething();
};

void TestClassName::testSomething() {
  QCOMPARE(actual, expected);
}

QTEST_MAIN(TestClassName)
#include "test_classname.moc"
```

2. Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_classname test_classname.cpp ${SOURCES})
target_link_libraries(test_classname PRIVATE Qt6::Test Qt6::Widgets ...)
add_test(NAME test_classname COMMAND test_classname)
```

### Current Test Coverage

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| `test_gridlayoutcalculator` | 19 | Grid metrics, item positioning, row ranges |
| `test_interactionstateholder` | 15 | State flags, suppression timers, struct access, search state |
| `test_launchmanager` | 12 | Security validation, path checking, parameter parsing |
| `test_pathutils` | 10 | Path validation, expansion, Result<T> error handling |
| `test_widgetpoolmanager` | 20 | Widget acquisition, release, soft/hard clear, stale limits |
| `test_gridutils` | 24 | Row/column math, centering, grid metrics calculation |

**Total: 100+ unit tests across 6 test suites**

## Code Conventions

### Null Check Style

Use **implicit boolean** style for null checks - do NOT use explicit `!= nullptr` or `== nullptr`:
```cpp
// Good - implicit boolean
if (m_scrollManager) { ... }
if (!m_itemScrollArea) { return; }

// Bad - explicit nullptr comparison
if (m_scrollManager != nullptr) { ... }
if (m_itemScrollArea == nullptr) { return; }
```

### QTimer::singleShot Documentation

All `QTimer::singleShot` calls **must** have a comment explaining WHY the delay exists:
```cpp
// Defer clearing ProgrammaticScroll flag until after Qt processes
// the setValue() - ensures the scroll event handler sees the flag is set
QTimer::singleShot(0, this, [this]() { ... });

// Wait 50ms for in-flight QtConcurrent operations to notice the
// cancellation flag before resetting it for future operations
QTimer::singleShot(50, this, [this]() { ... });
```

Common reasons for 0ms delays:
- Defer until after current event processing completes
- Allow Qt layout/property changes to settle
- Prevent reentrancy during signal handling

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
ItemWidget *ScrollManager::acquireWidget();  // Get from pool or create
void ScrollManager::releaseWidget(ItemWidget *widget);  // Return to pool
```

### Config File

INI format at `~/.config/kartend/kartend.cfg`:
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

### Adding a New Module

1. Create folder in `src/modules/<modulename>/`
2. Create header/source files (e.g., `<modulename>manager.h/.cpp`)
3. Add `#include` and `std::unique_ptr` member to `ApplicationManager` (order matters for destruction)
4. Create `*Setup` struct with dependencies
5. Call `setupReferences()` from `MainWindow::setupManagers()`
6. Add to `CMakeLists.txt`:
   - Add source/header paths to SOURCES and HEADERS lists
   - Add include directory to `target_include_directories`

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

// Create error context with appropriate severity
// Use info() for expected conditions (e.g., file not found during search)
auto ctx = ErrorUtils::ErrorContext::info(
    ErrorUtils::ErrorCode::FileNotFound,
    "No matching artwork found",
    "ArtworkUtils::tryFindArtworkForFile"
).withDetails("Searched for: game.png in /covers");

// Use warning() for recoverable issues
auto ctx = ErrorUtils::ErrorContext::warning(
    ErrorUtils::ErrorCode::InvalidArgument,
    "Empty filename provided",
    "ArtworkUtils::tryFindArtworkForFile"
);

// Use error() for functional errors
auto ctx = ErrorUtils::ErrorContext::error(
    ErrorUtils::ErrorCode::DatabaseQueryFailed,
    "Failed to fetch items",
    "QueryManager::fetchItems"
).withDetails(query.lastError().text());

// Use critical() for operation-blocking errors
auto ctx = ErrorUtils::ErrorContext::critical(
    ErrorUtils::ErrorCode::DatabaseNotOpen,
    "Cannot proceed without database connection",
    "DatabaseManager::initialize"
);

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

### Path Validation with Result<T>

Use `PathUtils::tryValidateAndExpandPath()` for structured error context:
```cpp
#include "pathutils.h"

// New Result-returning version with structured errors
auto result = PathUtils::tryValidateAndExpandPath(path, collectionName);
if (result.isError()) {
    ErrorUtils::logError(result.error());
    return;  // or handle gracefully
}
QString validatedPath = result.value();

// Legacy function still available for backward compatibility
QString path = PathUtils::validateAndExpandPath(path, collectionName);
if (path.isEmpty()) { /* handle error without context */ }
```

### Artwork Path Resolution with Result<T>

Use `ArtworkUtils::tryFindArtworkForFile()` for structured error context:
```cpp
#include "artworkutils.h"

// Result-returning version distinguishes error types
auto result = ArtworkUtils::tryFindArtworkForFile(fileName, artworkDir);
if (result.isOk()) {
    loadPixmap(result.value());
} else if (result.hasErrorCode(ErrorCode::ArtworkDirectoryNotFound)) {
    // Directory missing - log and skip
    ErrorUtils::logError(result.error());
} else if (result.hasErrorCode(ErrorCode::FileNotFound)) {
    // Normal condition - use placeholder silently
    usePlaceholder();
}

// Legacy function returns empty string on any failure
QString path = ArtworkUtils::findArtworkForFile(fileName, artworkDir);
if (path.isEmpty()) { /* no context about why */ }
```

## Dependencies

- Qt6: Core, Gui, Widgets, Sql, Concurrent
- CMake 3.20+
- Clang or GCC with C++23 support
- lld linker (for release builds)

