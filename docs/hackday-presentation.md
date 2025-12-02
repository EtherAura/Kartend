# Kartend: A Deep Dive into Modern C++/Qt Architecture

> **Hack Day Presentation**  
> *Lessons from Building a High-Performance Media Collection Frontend*

---

## Executive Summary

Carding: the textile process of aligning fibers; Kartend aligns your files into organized, navigable collections.

**Kartend** (*Carding*) is a collection and artwork frontend for KDE. It is a Qt6 application for organizing, managing, and launching collections of files. 

This presentation explores the architecture decisions, design patterns, and engineering practices that emerged from building a responsive desktop application capable of handling large datasets with smooth performance.

```
Project Stats
-------------------------------------
Language:       C++23
Framework:      Qt6 (Core, Gui, Widgets, Sql, Concurrent)
Build System:   CMake 3.20+
Modules:        23 feature modules
Managers:       18+ component managers
LOC:            ~15,000+
Founded:        July 20, 2025
```

---

## Project Structure

```
src/
├── core/                    # Application entry & main window
│   ├── main.cpp            # Qt application bootstrap
│   ├── mainwindow.cpp/h    # Central orchestration
│   └── mainwindow.ui       # Qt Designer layout
│
├── modules/                 # Feature modules (23 directories)
│   ├── animation/          # Smooth scroll animations with easing
│   ├── application/        # Manager lifecycle coordination
│   ├── artwork/            # Async image loading via QtConcurrent
│   ├── cache/              # LRU pixmap cache with disk persistence
│   ├── database/           # SQLite coordination (main thread)
│   ├── event/              # Event filtering & gesture detection
│   ├── filter/             # Search & subcollection filtering
│   ├── interaction/        # Input handling orchestration
│   ├── keyboard/           # Arrow navigation, key repeat
│   ├── launch/             # Process spawning for media players
│   ├── mouse/              # Click-hold scroll, wheel events
│   ├── navigation/         # Collection switching, breadcrumb stack
│   ├── overlay/            # Selection glide animation overlays
│   ├── query/              # Worker thread SQL execution
│   ├── restore/            # Selection state restoration
│   ├── scroll/             # Virtual scrolling & widget pooling
│   ├── search/             # Search bar logic, query debouncing
│   ├── selection/          # Selection state management
│   ├── session/            # Session persistence
│   ├── settings/           # Config file I/O, settings dialog
│   ├── sidebar/            # Metadata sidebar visibility
│   ├── viewport/           # Item centering, scroll-to-visible
│   └── widgetpool/         # Widget recycling for performance
│
├── ui/                      # UI components
│   ├── uiconstants.h       # Centralized timing/spacing constants
│   ├── dialogs/            # Settings dialog
│   └── widgets/            # ItemWidget, MetadataSidebar
│
└── utils/                   # Shared utilities
    ├── applicationcontext.h # Dependency injection container
    ├── collectionutils.h    # Core data structures
    ├── errorutils.h         # Result<T> pattern & error codes
    ├── stateutils.h         # Centralized state structs
    ├── timerutils.h         # Debounced timer coordination
    └── ...                  # Path, string, grid utilities
```

---

## Architecture Deep Dive

### Two-Tier Manager Ownership Model

Kartend uses a **hierarchical manager pattern** where ownership and lifecycle are explicitly controlled. The declaration order in the header determines destruction order (reverse of declaration).

```
+------------------------------------------------------------------+
|                         MainWindow                                |
|   (owns ApplicationManager, coordinates UI setup)                 |
+------------------------------+-----------------------------------+
                               |
                               v
+------------------------------------------------------------------+
|                     ApplicationManager                            |
|   (owns top-level managers, controlled destruction order)         |
+------------------------------------------------------------------+
| CacheManager       | In-memory LRU cache + disk persistence      |
| SessionManager     | Selection state persistence across runs     |
| ArtworkManager     | Async QtConcurrent image loading           |
| SettingsManager    | INI config file I/O                        |
| DatabaseManager    | SQLite coordination -> QueryManager thread |
| ScrollManager      | Virtual scrolling orchestration            |
| SidebarManager     | Metadata sidebar visibility                |
| NavigationManager  | Collection switching, breadcrumb stack     |
| InteractionManager | Input handling coordinator                 |
+------------------------------------------------------------------+
                               |
                               v
+------------------------------------------------------------------+
|                    InteractionManager                             |
|   (owns input-related sub-managers)                               |
+------------------------------------------------------------------+
| AnimationManager      | Easing curves, scroll animations        |
| SelectionManager      | Selection state, visual feedback        |
| ViewportManager       | Item centering, scroll-to-visible       |
| KeyboardManager       | Arrow keys, key repeat handling         |
| MouseManager          | Click-hold, wheel events                |
| SearchManager         | Debounced search, mode switching        |
| EventManager          | Event filtering dispatch                |
| LaunchManager         | External process spawning               |
+------------------------------------------------------------------+
```

### Dependency Injection via Setup Structs

Instead of constructor injection with many parameters, Kartend uses **setup structs** for clean dependency injection:

```cpp
struct ScrollManagerSetup {
  const ApplicationContext *ctx = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *mediaScrollArea = nullptr;
  ArtworkManager *artworkManager = nullptr;
  const QList<CollectionConfig> *collections = nullptr;
  
  // Accessor with ctx fallback
  SETUP_GETTER_DECL(QWidget*, GridContainer)
};

// Usage:
ScrollManagerSetup setup;
setup.ctx = &m_appContext;
setup.gridContainer = ui->gridWidget;
m_scrollManager->setupReferences(setup);
```

**Benefits:**
- Clear, documented dependencies
- Optional context fallback reduces boilerplate
- Easy to mock for testing
- Self-documenting API

---

## Key Design Patterns

### 1. Virtual Scrolling with Widget Pooling

Kartend only renders visible rows:

```
+------------------------------------------+
| Viewport (visible area)                  |
|  +------+------+------+------+------+    |
|  | W1   | W2   | W3   | W4   | W5   | Row 5  <-- Widgets created
|  +------+------+------+------+------+    |
|  | W6   | W7   | W8   | W9   | W10  | Row 6  <-- from pool
|  +------+------+------+------+------+    |
+------------------------------------------+
| Virtual container (full height)          |
| Row 0-4:   (not rendered)               |
| Row 7-999: (not rendered)               |
+------------------------------------------+
```

```cpp
// Widget Pool Pattern
ItemWidget* ScrollManager::acquireWidget() {
    if (m_widgetPool->hasAvailable())
        return m_widgetPool->acquire();  // Reuse existing
    return createNewWidget();             // Create if needed
}

void ScrollManager::releaseWidget(ItemWidget* widget) {
    widget->resetForReuse();  // Clear state
    m_widgetPool->release(widget);  // Return to pool
}
```

**Performance Impact:**
- Constant memory footprint
- Smooth scrolling and animations
- Instant navigation

### 2. Centralized State Management

Replaces scattered Qt dynamic properties with typed, discoverable state:

```cpp
// InteractionStateHolder - single source of truth
class InteractionStateHolder : public QObject {
    SelectionRestoreState m_selectionRestore;
    ScrollState m_scroll;
    ArtworkState m_artwork;
    ArrowNavigationState m_arrow;
    ClickState m_click;
    StreamScrollState m_streamScroll;
    SearchState m_search;
    
public:
    // Typed access instead of property("someName").toBool()
    [[nodiscard]] ScrollState& scroll() { return m_scroll; }
    
    // Convenience methods for common transitions
    void beginProgrammaticScroll() {
        m_scroll.programmaticScroll = true;
        m_scroll.userScrollActive = false;
    }
    
    void suppressArrowCenterFor(qint64 durationMs) {
        m_arrow.suppressArrowCenter = true;
        m_arrow.suppressArrowCenterUntilMs = 
            QDateTime::currentMSecsSinceEpoch() + durationMs;
    }
};
```

**Benefits:**
- Compile-time type checking
- IntelliSense discoverability
- Single source of truth for complex state
- Easier debugging and reasoning

### 3. Worker Thread Database Pattern

SQLite operations run on a dedicated worker thread to keep the UI responsive:

```
+-------------------+     Signal      +-------------------+
|   Main Thread     | --------------> |   Worker Thread   |
|   DatabaseManager |                 |   QueryManager    |
|                   | <-------------- |                   |
|   (coordinates)   |     Signal      |   (executes SQL)  |
+-------------------+                 +-------------------+
```

```cpp
// Main thread: emit request, don't block UI
void DatabaseManager::fetchItemsRange(int offset, int limit) {
    emit requestFetchItemsRange(context, collections, offset, limit, filter);
}

// Worker thread: execute query
void QueryManager::fetchItemsRange(...) {
    QSqlQuery query = executeQuery(sql);  // Blocking, but on worker
    emit itemsRangeLoaded(offset, results);  // Back to main thread
}
```

### 4. Result\<T\> Error Handling

Inspired by Rust's Result type for structured error handling:

```cpp
template<typename T>
class Result {
    std::optional<T> m_value;
    ErrorContext m_error;
    
public:
    [[nodiscard]] bool isOk() const { return m_value.has_value(); }
    [[nodiscard]] bool isError() const { return !isOk(); }
    [[nodiscard]] const T& value() const { return *m_value; }
    [[nodiscard]] const ErrorContext& error() const { return m_error; }
    
    // Error code checking
    [[nodiscard]] bool hasErrorCode(ErrorCode code) const {
        return isError() && m_error.code == code;
    }
};

// Usage:
auto result = PathUtils::tryValidateAndExpandPath(path, collectionName);
if (result.isError()) {
    ErrorUtils::logError(result.error());
    return;
}
QString validatedPath = result.value();
```

---

## UI Constants Architecture

All magic numbers are centralized in namespaced constants:

```cpp
namespace UIConstants {
    namespace Grid {
        inline constexpr int DEFAULT_WIDTH = 6;
        inline constexpr int SPACING = 20;
        inline constexpr int BUFFER_ROWS = 2;
    }
    
    namespace Animation {
        inline constexpr int CENTER_SCROLL_DURATION_MS = 1500;
        inline constexpr int PULSE_DURATION_MS = 3000;
        inline constexpr double PULSE_OPACITY_LOW = 0.25;
    }
    
    namespace Keyboard {
        inline constexpr int BASE_INTERVAL_MS = 260;
        inline constexpr int REPEAT_START_DELAY_MS = 260;
    }
    
    namespace Search {
        inline constexpr int DEBOUNCE_DELAY_MS = 120;
    }
    
    // 16 namespaces total covering all UI timing/dimensions
}
```

**Why this matters:**
- Single place to tune all UX timings
- Consistent behavior across features
- Easy A/B testing of timing values
- Introducing new frontend options

---

## Data Structures

### CollectionConfig

Represents a single collection's configuration:

```cpp
struct CollectionConfig {
    QString name;
    QString mediaDirectory;
    QString artworkDirectory;
    QString launcherPath;
    QString corePath;           // RetroArch core support
    QString launchParameters;
    QStringList extensions;
    
    int gridWidth;
    int itemWidth;
    int itemHeight;
    int fontSize;
    int horizontalSpacing;
    int verticalSpacing;
    
    bool hideTitles;
    bool sidebarVisible;
    bool showAllSubcollectionItems;
    
    int parentCollectionIndex = -1;  // Hierarchy support
    HorizontalAlignment horizontalAlignment;
    SidebarMode sidebarMode;
    
    // Validation
    [[nodiscard]] bool isValid() const;
    void clampValues();  // Enforce min/max bounds
};
```

### CollectionHierarchyCache

Pre-computed lookups for O(1) parent/child queries:

```cpp
struct CollectionHierarchyCache {
    QHash<int, QList<int>> directChildren;   // parent -> [children]
    QHash<int, QList<int>> allDescendants;   // parent -> [all descendants]
    QHash<int, int> parentIndex;             // child -> parent
    
    // Rebuilt when collections modified
};
```

---

## Threading Model

| Thread | Components | Responsibility |
|--------|------------|----------------|
| Main | All managers, UI | User interaction, rendering |
| Worker | QueryManager | SQLite queries, file scanning |
| QtConcurrent | ArtworkManager | Parallel image loading |

**Cross-thread communication** uses Qt's signal/slot mechanism with `Qt::QueuedConnection` for thread safety.

---

## Transferable Lessons

### For Qt/C++ Projects

1. **Use setup structs** instead of constructor injection for cleaner APIs
2. **Centralize state** in typed holders rather than scattered properties
3. **Pool expensive widgets** for lists with 100+ items
4. **Always debounce** user input that triggers expensive operations
5. **Worker threads** for any blocking I/O (database, file system)

### For Architecture

1. **Two-tier ownership** makes destruction order explicit
2. **Dependency injection container** (`ApplicationContext`) reduces coupling
3. **Namespaced constants** prevent magic number proliferation
4. **Result types** make error handling explicit and testable

### For Code Quality

1. **`[[nodiscard]]`** on getters catches ignored return values
2. **Implicit null checks** (`if (ptr)` not `if (ptr != nullptr)`)
3. **Document all `QTimer::singleShot`** with WHY the delay exists
4. **Test critical calculations** (grid layout math has 19 unit tests)

---

## Testing

Unit tests use the Qt Test framework with CTest integration:

```bash
# Build with tests
cmake -DBUILD_TESTS=ON ..
make test_gridlayoutcalculator test_interactionstateholder

# Run tests
ctest --output-on-failure
```

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| `test_gridlayoutcalculator` | 19 | Grid metrics, item positioning, row ranges |
| `test_interactionstateholder` | 15 | State flags, suppression timers, struct access |

---

## Build System

### CMake Configuration Highlights

```cmake
# C++23 with Qt6
set(CMAKE_CXX_STANDARD 23)
find_package(Qt6 COMPONENTS Core Gui Widgets Sql Concurrent REQUIRED)

# Auto-generate MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Release optimizations
-O3 -ffast-math -march=native -flto=auto
-fomit-frame-pointer -funroll-loops
-fstack-protector-strong -D_FORTIFY_SOURCE=2

# Profile-guided optimization support
option(USE_PGO "Enable Profile-Guided Optimization" OFF)
```

### Build Commands

```bash
# Dependencies (Debian/Ubuntu)
sudo apt install clang cmake lld qt6-base-dev libqt6sql6-sqlite

# Build (release)
.scripts/build.sh

# Build (debug)
.scripts/build.sh --debug

# Build (with profile-guided optimization)
.scripts/build.sh --pgo

# Build (maintenance mode - warnings as errors)
.scripts/build.sh --maintenance
```

---

## Demo Highlights

1. **Virtual scrolling** - Load 10,000 items, scroll smoothly at 60fps
2. **Keyboard navigation** - Arrow keys with smooth centering animation
3. **Search** - Debounced filtering with mode switching (name/path)
4. **Collection hierarchy** - Parent/child collections with breadcrumb navigation
5. **Session persistence** - Selection state restored on application restart
6. **Async artwork loading** - Images load in background without blocking UI

---

## Future Directions

- **Plugin system** for custom launchers
- **Network collection sources** (SMB, NFS)
- **Metadata scraping** integration
- **Gamepad navigation** support
- **Wayland** native support improvements

---

## Key Takeaways

> *"The architecture of Kartend demonstrates that even a personal media launcher benefits from thoughtful design patterns. Virtual rendering, centralized state management, dependency injection, and worker threads create a foundation that scales from hundreds to tens of thousands of items without architectural changes."*

---
