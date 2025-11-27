# Kartend

Collection & Artwork Frontend for KDE

**Kartend** (German: *Carding*) — Carding is a mechanical process that aligns cotton, wool or other fibers in the manufacture of textiles. Kartend is a highly customizable frontend that will allow you to organize, manage, and launch your files.

## Dependencies

```bash
sudo apt install clang cmake lld qt6-base-dev libqt6sql6-sqlite
```

## Architecture

Qt6 KDE application using manager-based architecture with dependency injection via setup structs.

### Core (`src/core/`)

| Component | Description |
|-----------|-------------|
| `main.cpp` | Application entry point that initializes Qt and displays the main window. |
| `mainwindow.cpp` | Main application window that owns ApplicationManager and orchestrates UI setup. |

### Managers (`src/managers/`)

| Manager | Description |
|---------|-------------|
| `applicationmanager` | Owns and coordinates all manager lifecycles with controlled destruction order. |
| `navigationmanager` | Manages collection switching, navigation stack, and subcollection hierarchy traversal. |
| `scrollmanager` | Manages virtual scrolling, widget pooling, and grid layout for large item collections. |
| `interactionmanager` | Orchestrates user interactions, delegating to specialized managers for input handling. |
| `selectionmanager` | Owns selection state and coordinates selection operations with visual feedback. |
| `viewportmanager` | Manages viewport positioning, item centering, and scroll-to-visible operations. |
| `keyboardmanager` | Handles keyboard input processing, arrow key navigation, and key repeat behavior. |
| `mousemanager` | Handles mouse input including click-hold scrolling, wheel events, and widget finding. |
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

### UI (`src/ui/`)

| Component | Description |
|-----------|-------------|
| `uiconstants.h` | Centralized namespace for all UI timing, spacing, and dimension constants. |
| `settingsdialog` | Collection configuration dialog with tree-based hierarchy editing and live preview. |
| `metadatasidebar` | Displays file metadata, artwork preview, and item details in the sidebar panel. |
| `itemwidget` | Media item widget displaying artwork, title, and selection state with pulse animation. |

### Utilities (`src/utils/`)

| Utility | Description |
|---------|-------------|
| `collectionutils.h` | Defines CollectionConfig, CollectionContext, and CollectionHierarchyCache data structures. |
| `configutils.h` | Provides config path resolution and variable expansion for settings files. |
| `searchutils.h` | Defines SearchMode enum and search context utilities for filtering operations. |
| `stringutils.h` | Formats numbers with comma separators for display in title counts. |
| `settingsutils` | Resolves settings file paths and provides INI file handling utilities. |
| `pathutils` | Provides file path normalization, extension handling, and path manipulation. |
| `gridutils.h` | Computes grid layout metrics including row/column positions and container sizes. |
| `extensionutils` | Categorizes file extensions by media type (ROM, disc image, archive, etc.). |
| `timerutils` | Provides debounced timer coordination for viewport and layout updates. |
| `propertyutils.h` | Defines PropertyKeys namespace with Qt dynamic property key constants. |

## Build

```bash
.scripts/build.sh
```

Build flags: `--debug`, `--maintenance`, `--pgo`

---

*The sculpture is already complete within the marble block, before I start my work. It is already there, I just have to chisel away the superfluous material.*

Founded 07/20/2025