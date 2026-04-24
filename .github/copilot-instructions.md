# Kartend - AI Coding Instructions

## 🚨 MANDATORY WORKFLOW — BEADS-FIRST, STRICT ORDER 🚨

**Every task — bug, feature, refactor, doc, chore — MUST follow this order. No exceptions.**

### Before Touching Code

1. **Find or file the issue** — `bd ready` (pick existing) OR `bd create --title="..." --description="..." --type=bug|feature|task|chore --priority=0..4`. Never start work without an issue ID.
2. **Claim it** — `bd update <id> --claim`. This marks it `in_progress` atomically and prevents duplicate work.
3. **Read context** — `bd show <id>` and check `bd memories <keyword>` for prior insights on the topic.

### While Working

4. **One issue in_progress at a time.** If new work surfaces, file it as a separate issue with `--deps discovered-from:<current-id>` and stay focused.
5. **Use `bd` exclusively for tracking.** Do NOT use TodoWrite, manage_todo_list, markdown TODOs, or scratch lists. The `bd` database is the single source of truth.
6. **Capture insights immediately** — `bd remember "<insight>"` for any non-obvious discovery (build quirk, race condition, API gotcha). Do NOT create MEMORY.md files.
7. **Annotate the issue as you go** — `bd update <id> --append-notes="..."` for progress notes, `bd update <id> --design="..."` for design decisions.

### Before Saying "Done"

8. **Run quality gates** — `.scripts/build.sh --tests --run-tests` (or `--maintenance` for strict checks). Fix until clean.
9. **Close the issue(s)** — `bd close <id1> <id2> ...` (batch when possible). Use `--reason="..."` if non-obvious.
10. **MANDATORY PUSH** — work is NOT complete until `git push` succeeds:
    ```bash
    git status                # verify what changed
    git add <files>
    git commit -m "..."
    git pull --rebase
    bd dolt push              # push beads database
    git push                  # push code
    git status                # MUST show "up to date with origin"
    ```
11. **Verify** — `git status` clean, `bd list --status=in_progress` empty (or only intentionally deferred work).

### Hard Rules

- ❌ Never start coding before there is a `bd` issue ID claimed by you.
- ❌ Never use `bd edit` (opens `$EDITOR`, blocks the agent). Use `--title/--description/--notes/--design/--append-notes`.
- ❌ Never close an issue without committing AND pushing the corresponding code.
- ❌ Never invent placeholder issue IDs — always use real `bd-*` IDs returned by `bd create`.
- ✅ When the user describes new work, your FIRST action is `bd create` (or `bd ready` to check for an existing match).
- ✅ When in doubt about priority: P0=critical/blocking, P1=high, P2=normal (default), P3=backlog, P4=future/nice-to-have.
- ✅ When discovering related work mid-task, file it (`--deps discovered-from:<id>`) — don't expand the current issue's scope.

### Quick Reference

```bash
bd ready                                    # what to work on next
bd show <id>                                # full issue context
bd update <id> --claim                      # take ownership
bd update <id> --append-notes="progress"    # log progress without opening editor
bd remember "<insight>"                     # persistent knowledge
bd memories <keyword>                       # search past insights
bd close <id> [<id2> ...]                   # finish (then commit + push)
bd dep add <a> <b>                          # a depends on b
bd list --status=in_progress                # what you're actively working on
bd preflight                                # pre-PR sanity (lint, stale, orphans)
```

Run `bd prime` at the start of any session for the full command surface.

---

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
│   ├── filter/     # Search and subcollection filtering
│   ├── gamepad/    # Optional Qt6::Gamepad / SDL2 input backend
│   ├── interaction/# Input handling, selection state
│   ├── keyboard/   # Arrow key navigation, key repeat, alphabetic jumping
│   ├── launch/     # Item launching, process spawning
│   ├── mouse/      # Click hold scrolling, wheel events
│   ├── navigation/ # Collection switching, navigation stack
│   ├── overlay/    # Selection / search loading overlays
│   ├── query/      # Worker thread SQL queries
│   ├── restore/    # Selection state restoration during navigation
│   ├── scroll/     # Virtual scrolling, grid layout, widget factory
│   ├── search/     # Search bar logic, search modes
│   ├── selection/  # Selection logic, click processing
│   ├── session/    # Selection state persistence
│   ├── settings/   # Config file I/O, settings dialog
│   ├── sidebar/    # Metadata sidebar visibility
│   ├── viewport/   # Centering, viewport positioning
│   └── widgetpool/ # Widget recycling pool for ItemWidget reuse
├── ui/             # UI components and constants
│   ├── dialogs/    # Settings dialog, error dialog, shortcuts dialog
│   └── widgets/    # Item widget, metadata sidebar, list header, overlays
└── utils/          # Shared utilities and data structures
```

### Manager Hierarchy

`MainWindow` owns `ApplicationManager`, which creates top-level managers in controlled destruction order (declared bottom-up in `applicationmanager.h`).

**Two-tier ownership model:**
- **ApplicationManager** owns: `CacheManager`, `SessionManager`, `ArtworkManager`, `SettingsManager`, `DatabaseManager`, `ScrollManager`, `SidebarManager`, `NavigationManager`, `InteractionManager`
- **InteractionManager** owns: `SearchManager`, `SelectionManager`, `KeyboardManager`, `GamepadManager`, `ArrowNavigationHandler`, `AlphabeticNavigationHandler`, `AnimationManager`, `MouseManager`, `LaunchManager`, `ViewportManager`, `EventManager`

Additional helper managers owned by their parent feature module (not top-level): `WidgetPoolManager`, `FilterManager`, `SelectionRestoreManager`, `SelectionOverlayManager`, `SearchLoadingOverlay`, `NavigationStackManager`.

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
| `GamepadManager` | InteractionManager | `dpadPressed`, `buttonPressed` (compiled in when Qt6::Gamepad or SDL2 is found) |
| `AlphabeticNavigationHandler` | InteractionManager | `requestSelectionByIndex` |
| `WidgetPoolManager` | ScrollManager | (helper, no signals) |
| `FilterManager` | ScrollManager | (helper, no signals) |
| `SelectionOverlayManager` | ScrollManager | (helper, no signals) |
| `SelectionRestoreManager` | NavigationManager | (helper, no signals) |
| `NavigationStackManager` | NavigationManager | (helper, no signals) |

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
- `--tests --run-tests` - Build and run unit tests

**Debug builds:** Use `--debug` flag when you need to see debug/warning output:
```bash
.scripts/build.sh --debug
```
The debug binary is at `build/ninja-debug/kartend` (or `build/make-debug/kartend` if using Make).

**Manual builds:**
```bash
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja-release --parallel $(nproc)
```

## Testing

Unit tests use the **Qt Test framework** with CTest integration.

### Test Structure

```
tests/
├── CMakeLists.txt                       # Test build configuration
├── test_artworkmanager.cpp              # Artwork loading & path resolution
├── test_cachemanager.cpp                # Pixmap cache, LRU, disk persistence
├── test_collectionutils.cpp             # CollectionConfig + hierarchy helpers
├── test_dbmigrations.cpp                # SQLite schema migrations
├── test_databasemanager.cpp             # DB worker shutdown + path resolution
├── test_filterhelpers.cpp               # Filter predicate helpers
├── test_gridlayoutcalculator.cpp        # Grid layout math
├── test_gridutils.cpp                   # Grid math utilities
├── test_interactionstateholder.cpp      # Centralized interaction state
├── test_keyboardmanager.cpp             # Key repeat, arrow/alpha handlers
├── test_launchmanager.cpp               # Launch security validation
├── test_mousehelpers.cpp                # Mouse helper math
├── test_navigationhelpers.cpp           # Navigation helpers
├── test_navigationstackmanager.cpp      # Navigation stack push/pop
├── test_pathutils.cpp                   # Path validation, Result<T>
├── test_queryhelpers.cpp                # SQL helper builders
├── test_querymanager_cancel_scan.cpp    # Scan cancellation flow
├── test_scrollhelpers.cpp               # Scroll math helpers
├── test_searchhelpers.cpp               # Search helpers
├── test_searchutils.cpp                 # SearchMode utilities
├── test_selectionhelpers.cpp            # Selection helpers
├── test_sessionmanager.cpp              # Session persistence (atomic writes)
├── test_stringutils.cpp                 # String formatting
└── test_widgetpoolmanager.cpp           # Widget recycling pool
```

### Building Tests

Enable tests with the `BUILD_TESTS` CMake flag:
```bash
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build/ninja-release --parallel $(nproc)
```

### Running Tests

Run individual test:
```bash
cd build/ninja-release
./tests/test_gridlayoutcalculator
./tests/test_sessionmanager
# ... one binary per suite under tests/
```

Run all tests via CTest:
```bash
ctest --test-dir build/ninja-release --output-on-failure
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
| `test_artworkmanager` | 15 | Artwork path resolution, batch loading, suppression |
| `test_cachemanager` | 15 | Pixmap cache, LRU eviction, disk persistence |
| `test_collectionutils` | 30 | CollectionConfig, hierarchy cache, validation helpers |
| `test_dbmigrations` | 10 | SQLite schema migration steps |
| `test_databasemanager` | 6 | Worker-thread shutdown, SQL connection cleanup, path resolution |
| `test_filterhelpers` | 15 | Filter predicate helpers |
| `test_gridlayoutcalculator` | 17 | Grid metrics, item positioning, row ranges |
| `test_gridutils` | 22 | Row/column math, centering, grid metrics calculation |
| `test_interactionstateholder` | 13 | State flags, suppression timers, struct access, search state |
| `test_keyboardmanager` | 27 | Key repeat, arrow + alphabetic navigation handlers |
| `test_launchmanager` | 29 | Security validation, path checking, parameter parsing |
| `test_mousehelpers` | 17 | Mouse helper math (hold scroll, wheel) |
| `test_navigationhelpers` | 18 | Navigation helper utilities |
| `test_navigationstackmanager` | 19 | Navigation stack push/pop/clear |
| `test_pathutils` | 27 | Path validation, expansion, Result<T> error handling |
| `test_queryhelpers` | 21 | SQL helper builders |
| `test_querymanager_cancel_scan` | 1 | Scan cancellation flow |
| `test_scrollhelpers` | 16 | Scroll helper math |
| `test_searchhelpers` | 16 | Search helper utilities |
| `test_searchutils` | 3 | SearchMode utilities |
| `test_selectionhelpers` | 17 | Selection helper math |
| `test_sessionmanager` | 17 | Session persistence with atomic writes |
| `test_stringutils` | 10 | String formatting |
| `test_widgetpoolmanager` | 17 | Widget acquisition, release, soft/hard clear, stale limits |

**Total: 398 unit test methods across 24 test suites**

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
QTimer::singleShot(0, this, [this] { ... });

// Wait 50ms for in-flight QtConcurrent operations to notice the
// cancellation flag before resetting it for future operations
QTimer::singleShot(50, this, [this] { ... });
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

Use `ErrorUtils` for structured error reporting. The canonical patterns are:

- Test for an error with `ctx.isError()` (or `result.isError()` for `Result<T>`).
  Do **not** compare `ctx.code` against `ErrorCode::Success` directly.
- Test for a specific failure mode on a `Result<T>` with
  `result.hasErrorCode(ErrorCode::SomeCode)`. Do **not** dereference
  `result.error().code` for that purpose.
- Prefer `Result<T>` at I/O boundaries (file, database, network, parsing) and
  raw `ErrorContext` for fire-and-log call sites that have no value to return.
- `ErrorCode::Success` is the sentinel for non-error contexts; never raise an
  error with this code.

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

## Non-Interactive Shell Commands

**ALWAYS use non-interactive flags** with file operations to avoid hanging on confirmation prompts. On some systems `cp`/`mv`/`rm` are aliased to `-i` (interactive) and will hang indefinitely waiting for y/n input.

```bash
# Force overwrite without prompting
cp -f source dest           # NOT: cp source dest
mv -f source dest           # NOT: mv source dest
rm -f file                  # NOT: rm file

# Recursive operations
rm -rf directory            # NOT: rm -r directory
cp -rf source dest          # NOT: cp -r source dest
```

Other commands that may prompt:
- `scp` — use `-o BatchMode=yes`
- `ssh` — use `-o BatchMode=yes` to fail instead of prompting
- `apt-get` — use `-y`
- `brew` — set `HOMEBREW_NO_AUTO_UPDATE=1`

