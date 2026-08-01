# Architecture

Qt6 KDE application using manager-based architecture with dependency injection via setup structs.

## Overview

Kartend uses a **module-based architecture** where `MainWindow` owns `ApplicationManager`, which creates top-level managers in controlled destruction order. Managers communicate via Qt signals/slots and receive dependencies through dedicated setup structs.

## Project Structure

```
src/
├── api/                 # Neutral header-only role interfaces (i*.h); no .cpp
├── chrome/              # Neutral chrome layer — "dumb" widgets shared between
│   │                    # input/media/core/ui without taking an upward dep
│   │                    # on src/ui/. Layering enforced by check-layering.py.
│   ├── items/           # ItemWidget, CoverFlowWidget, placeholder renderers
│   ├── media/           # VideoPreviewWidget, video thumbnail extractor
│   └── overlays/        # ArtworkPreviewOverlay, OverlayZOrderRegistry
├── core/                # Main application entry and window
├── modules/             # Feature modules grouped by domain
│   ├── behavior/        # Manager lifecycle coordination
│   │   └── application/ # Manager lifecycle coordination
│   ├── data/            # Persistence: SQLite, cache, sessions, settings, playlists
│   │   ├── cache/       # In-memory pixmap cache, disk persistence
│   │   ├── database/    # SQLite coordination via worker thread
│   │   ├── dat/         # Offline DAT-file identification + on-disk parse cache
│   │   ├── dataudit/    # DAT-based collection audit (scan vs DAT catalogue,
│   │   │                # missing/unknown report, fix-list export)
│   │   ├── kart/        # Kart (collection bundle) import/export
│   │   ├── playlist/    # Playlist storage and export (JSON / M3U)
│   │   ├── query/       # Worker thread SQL queries
│   │   ├── restore/     # Selection state restoration during navigation
│   │   ├── scraper/     # Metadata scraping (core/ + parsers/ + providers/)
│   │   ├── session/     # Selection state persistence
│   │   ├── settings/    # Config file I/O, collection settings persistence
│   │   └── watcher/     # Debounced filesystem watch → rescan for collections
│   │                    # with watchFilesystem enabled
│   ├── input/           # User input, navigation, and input-driven behavior
│   │   ├── animation/   # Scroll animations, easing curves
│   │   ├── attract/     # Attract-mode idle scroll/advance
│   │   ├── event/       # Event filtering, gesture detection
│   │   ├── filter/      # Search and subcollection filtering
│   │   ├── gamepad/     # Optional Qt6::Gamepad / SDL2 input backend
│   │   ├── interaction/ # Central input coordination, selection state
│   │   ├── keyboard/    # Arrow key navigation, key repeat, alphabetic jumping
│   │   ├── launch/      # Item launching, process spawning
│   │   ├── mouse/       # Click-hold scrolling, wheel events
│   │   ├── navigation/  # Collection switching, navigation stack, background
│   │   │                # + loading + empty-state widgets
│   │   ├── overlay/     # Selection / search loading overlays
│   │   ├── scroll/      # Virtual scrolling, grid layout, widget factory,
│   │   │                # list-header widget
│   │   ├── search/      # Search bar logic, search modes
│   │   ├── selection/   # Selection logic, click processing
│   │   ├── viewport/    # Centering, viewport positioning
│   │   └── widgetpool/  # Widget recycling pool for ItemWidget reuse
│   └── media/           # Artwork pipeline, detail pages
│       ├── artwork/     # Async artwork loading with QtConcurrent
│       └── detailpage/  # Detail-page coordinator
├── ui/                  # UI components (dialogs, widget panes, controllers)
│   ├── controllers/     # Controllers that orchestrate UI widgets but stay
│   │   │                # at the ui/ layer (e.g. DetailsPaneManager, moved
│   │   │                # out of modules/media/ when relayered as ui/;
│   │   │                # SettingsDialogController, moved out of
│   │   │                # modules/data/settings/ in Kartend-q8p29)
│   │   ├── detailspanemanager/
│   │   └── settingsdialogcontroller/
│   ├── dialogs/         # Dialogs grouped by domain: settings/ (further
│   │                    # split into core/ + appearance/ + behavior/ +
│   │                    # artwork/ + launchers/ + collections/),
│   │                    # collection/, launcher/, scraper/,
│   │                    # kart/ (loose dialogs at root)
│   └── widgets/         # UI-coupled widgets: details pane, marquee window,
│                        # splash / now-playing / startup-video / text-zoom
│                        # overlays (neutral widgets live in src/chrome/)
└── utils/               # Shared utilities and data structures
```

## Core (`src/core/`)

| Component | Description |
|-----------|-------------|
| `main.cpp` | Application entry point that initializes Qt and displays the main window. |
| `mainwindow.cpp` | Main application window that owns ApplicationManager and orchestrates UI setup. The implementation is split across eight sibling TUs (`mainwindow.cpp`, `mainwindow_setup.cpp`, `mainwindow_wiring.cpp`, `mainwindow_managerwiring.cpp`, `mainwindow_timers.cpp`, `mainwindow_scraper.cpp`, `mainwindow_toolbar.cpp`, `mainwindow_dialogs.cpp`); see [mainwindow-partials.md](mainwindow-partials.md) for the responsibility map and the rule for where new code goes. |
| `marqueecontroller` | Drives the secondary-monitor marquee / topper window — owns the MarqueeWindow and the artwork-refresh debounce timer (extracted from MainWindow). |
| `scrolleventscontroller` | Owns MainWindow's reactions to ScrollManager view-mode / column-resize / CoverFlow activation signals (sort-mode change, list-column-width persist, CoverFlow item-launch, sidebar-yield for CoverFlow / artwork-preview overlay). Replaces the prior `mainwindow_scrollevents.cpp` partial. |
| `menucontroller` | Builds the menu bar and the command palette, and connects every action to its MainWindow-supplied callback (split across `menucontroller.cpp`, `menucontroller_actions.cpp`, `menucontroller_dynamicmenus.cpp`). |
| `toolbarcontroller` | Owns the items-page toolbar's stateful widgets and their sync/refresh glue: layout-picker button + view actions, the search-mode action embedded in the search bar, and the consolidated filter button. |
| `dialogcontroller` | Centralizes dialog construction so MainWindow doesn't `#include` every dialog header; owns dialog parenting/modality. |
| `dbeventscontroller` | Owns MainWindow's reactions to DatabaseManager scan/count signals plus the scan-counter / startup-overlay-suppression state those slots read and write. |
| `scrapercontroller` | Owns the scraper-flow entry points (open scraper dialog, resume pending scrape at startup) and the long-lived ScrapeResultDialog + ScraperService they cache. |
| `datauditcontroller` | Owns the DAT Audit sub-window — lazily constructed on first open, hidden (not destroyed) on close — mirroring ScraperController's dialog-cache role. |
| `librarytoolscontroller` | Owns the five per-collection library tools (collection health, duplicates/variants, bulk edit, missing-metadata review, artwork wizard) sharing the validate-collection → resolve-uuid → run-dialog skeleton. |
| `gridwidthdebouncer` | Three-stage debounced pipeline (save / rebuild / preview) behind the menu-driven grid-width adjustment shortcuts. |
| `titlecountshelpers` | Stateless helpers rendering the window title for the active collection (subfolder / hierarchy-root / leaf formats plus child-part counts). |

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
| `launchmanager` | Launches media items with configured external launchers, handling per-launcher parameters and the RetroArch core slot. |
| `artworkmanager` | Handles async artwork loading with QtConcurrent, caching, and viewport-aware prioritization. |
| `databasemanager` | Coordinates SQLite database access via worker thread for collection metadata queries. |
| `querymanager` | Executes SQLite queries on worker thread for paginated item loading and filtering. |
| `cachemanager` | Manages in-memory pixmap cache with LRU eviction and optional disk persistence. |
| `sessionmanager` | Persists and restores selection state and item counts across application sessions. |
| `settingsmanager` | Handles config file I/O and collection settings persistence. The settings-dialog orchestration (open/diff/apply flow + the async "Collection Added" summaries) moved to `SettingsDialogController` at `src/ui/controllers/settingsdialogcontroller/` (Kartend-q8p29, following the DetailsPaneManager precedent). |
| `detailspanemanager` | Coordinates the details/metadata side pane (visibility, position, gallery content). Lives at `src/ui/controllers/detailspanemanager/` — a controller for a ui-layer widget (DetailsPane), at the ui/ layer. |
| `filtermanager` | Applies search and subcollection filters to the active item set (helper owned by ScrollManager). |
| `widgetpoolmanager` | Recycles ItemWidget instances for virtual scrolling (helper owned by ScrollManager). |
| `datasourcemanager` | Owns FilterManager, ScrollDataManager, PreSearchStateManager, and SearchLoadingOverlay (helper extracted from ScrollManager). |
| `selectiondisplaymanager` | Owns SelectionOverlayManager, SelectionStateTracker, and ArtworkPreviewOverlay (helper extracted from ScrollManager). |
| `overlay` | SelectionOverlayManager / SearchLoadingOverlay rendering helpers for glide and loading visuals. |
| `restore` | SelectionRestoreCoordinator coordinating selection restoration during navigation transitions. |

## Manager Hierarchy

**Two-tier ownership model:**
- **ApplicationManager** owns: `CacheManager`, `SessionManager`, `ArtworkManager`, `SettingsManager`, `DatabaseManager`, `ScrollManager`, `DetailsPaneManager`, `NavigationManager`, `InteractionManager`, `PlaylistManager`, `DetailPageManager`, `KartManager`
- **InteractionManager** owns: `SearchManager`, `SelectionManager`, `KeyboardManager`, `GamepadManager`, `ArrowNavigationHandler`, `AlphabeticNavigationHandler`, `AnimationManager`, `MouseManager`, `LaunchManager`, `ViewportManager`, `EventManager`, `AttractManager`

Additional helper managers owned by their parent feature module (not top-level): `WidgetPoolManager`, `FilterManager`, `SelectionRestoreCoordinator`, `SelectionOverlayManager`, `SearchLoadingOverlay`, `NavigationStackManager`, `CollectionBackgroundController`.

## Class-name suffix conventions

Service-like classes carry one of several suffixes. The distinction is a
**descriptive convention** (not lint-enforced) — `Manager` is the historical
default and dominates the tree, so the suffix is a hint to a class's role,
not a guarantee. Prefer these meanings for new classes:

| Suffix | Role |
|--------|------|
| `Manager` | Owns lifecycle and/or mutable state for a feature; long-lived; coordinates sub-helpers. The default for a feature module's top-level object. |
| `Service` | Performs an operation (often DB- or IO-backed) with little long-lived UI state of its own (e.g. `ScanService`, `ScraperService`). |
| `Controller` | Orchestrates UI widgets / dialogs at the `ui/` or `core/` layer (e.g. `MenuController`, `SettingsDialogController`, `DetailsPaneManager`'s controller role). |
| `Coordinator` | Cross-manager glue that sequences work across siblings without owning their state (e.g. `SelectionRestoreCoordinator`). |
| `Runner` | Drives a bounded, often-cancellable batch job (e.g. `BatchScrapeRunner`, `DatAuditRunner`). |
| `Provider` / `Parser` | Pluggable scraper backends and their response parsers under `modules/data/scraper/`. |
| `Cache` / `Store` | Bounded in-memory cache, or a thin typed accessor over one DB table. |
| `Handler` / `Engine` | A focused sub-unit owned by a Manager (input `Handler`s; the virtual-scroll `Engine`). |

Don't mass-rename existing classes to fit this table — it documents intent
for new code and explains why similar classes carry different suffixes.

## UI (`src/ui/`)

| Component | Description |
|-----------|-------------|
| `uiconstants/` | Per-area headers (`grid.h`, `dialog.h`, `icons.h`, …) carrying UI timing, spacing, and dimension constants. **Relocated to `src/utils/uiconstants/`** — they are a pure compile-time foundation namespace, so they now live at the utils layer (dropping the last layering-lint exception); consumers still include them as `uiconstants/<name>.h`. The old `uiconstants.h` umbrella was deprecated when the 117 callers were migrated to the topical subheaders. |
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
| `collection/archiveoptions.h` | `ArchiveOptions` leaf struct (per-collection archive-extraction toggles: `extractArchives`, `extractedExtension`). Peeled from the `collectionutils.h` god-header. |
| `collection/collectionbackground.h` | `CollectionBackground` leaf struct (per-collection view-background / wallpaper cluster: items-page background, palette, header logo, vignette, parallax, toolbar backdrop blur). Peeled from the `collectionutils.h` god-header. |
| `collection/collectionconfig.h` | `CollectionConfig` per-collection god-struct (embeds every leaf cluster + `LauncherProfile` + per-collection scalar fields + the `isValid` / `clampValues` helpers). Includes every leaf header it embeds, so a TU that needs `CollectionConfig` no longer drags in `CollectionContext` + `GeneralSettings` + the hierarchy cache from `collectionutils.h`. Peeled from `collectionutils.h`. |
| `collection/collectioncontext.h` | `CollectionContext` runtime context bundle (current index + config snapshot, active artwork dir, loaded file paths/names, sort mode, query-scope toggles, precomputed descendants/UUIDs/dir maps from the hierarchy cache, UI overrides, type filters, root-view flag). Peeled from `collectionutils.h`. |
| `collection/collectionfilterpreferences.h` | `CollectionFilterPreferences` leaf struct (per-collection title-cleanup regex patterns + master toggle). Peeled from the `collectionutils.h` god-header. |
| `collection/collectionhierarchycache.h` | `CollectionHierarchyCache` precomputed parent → children + descendant + UUID + media/artwork-dir maps; rebuilt once per navigation entry for O(1) lookup on the QueryManager / search hot path. Peeled from `collectionutils.h`; `rebuild()` impl lives in `collection/collectionhierarchycache.cpp`. |
| `collection/folderbrowsingoptions.h` | `FolderBrowsingOptions` leaf struct (virtual-folder browsing toggles + runtime subfolder cursor). Peeled from the `collectionutils.h` god-header. |
| `collection/generalsettings.h` | `GeneralSettings` app-wide settings struct. Formerly a 456-LOC god-struct; decomposed (Kartend-q1w6) into a ~99-line composition of 15 per-domain leaf structs (input, keybinding, gamepad, scraper, attract, marquee, splash, runtime detection, startup, media, appearance, view, toolbar, launcher, history), each in its own `collection/*_settings.h` with a matching `*_settings_persistence` pair — so settings churn now lands in the leaf headers, not here. `generalsettings.h` includes every leaf it embeds; many consumers still include the full umbrella, but single-domain consumers can (opportunistically) include just their leaf header. Peeled from `collectionutils.h`; the "god-struct" descriptions in older notes are historical provenance. |
| `collection/gridlayoutpreferences.h` | `GridLayoutPreferences` leaf struct (per-collection grid / item-layout cluster: items-per-row, spacing, item-box dimensions + font + corner radius, scrollbar toggles). Peeled from the `collectionutils.h` god-header. |
| `collection/launcherconfig.h` | `LauncherConfig` (one launcher entry) + `LauncherProfile` (per-collection launcher container with primary + additional launchers + chooser-default index + lookup helpers) + `LauncherUtils::usesLibretroCore` / `LauncherUtils::resolvePreset` decl. First non-leaf peel — depends on `LauncherPreset`. Peeled from `collectionutils.h`; `resolvePreset` impl lives in `collection/launcherconfig.cpp`. |
| `collection/launcherpreset.h` | `LauncherPreset` leaf struct (globally-registered, reusable launcher configuration). First struct peeled out of the `collectionutils.h` god-header. |
| `collection/listviewoptions.h` | `ListViewOptions` leaf struct (per-collection list-view appearance overrides: `listFontSize`, `listRowHeight`, `listRowColor`, `listAltRowColor`). Peeled from the `collectionutils.h` god-header. |
| `collection/scraperoverrides.h` | `ScraperOverrides` leaf struct (per-collection scraper / DAT overrides: `screenscraperSystemId`, `screenscraperHashArchive`, `datFilePaths`, `scraperProviderId`). Peeled from the `collectionutils.h` god-header. |
| `collection/sidebarappearance.h` | `SidebarAppearance` leaf struct (per-collection metadata-sidebar appearance: visibility, dock mode/position, background + pattern, colors, opacity, dimensions, font). Peeled from the `collectionutils.h` god-header. |
| `collectiontypes.h` | Standalone enums extracted from `collectionutils.h` (`HorizontalAlignment`, `DetailsPaneMode`, etc.). |
| `collection/` leaf headers | The god-header peel is **complete**: the former `collectionutils.h` umbrella header has been removed, and callers include the specific leaf headers under `collection/` directly. The `CollectionUtils::` helper namespace (hierarchy + validation functions) lives in `collection/hierarchyhelpers.h`, `collection/validationhelpers.h`, and `collection/typehelpers.h`. The "Peeled from `collectionutils.h`" notes above are historical provenance — the umbrella no longer exists. |
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

`MainWindow::initializeAppContext()` (mainwindow_setup.cpp) populates
`ctx->managers.*` before any `setupReferences()` runs. The per-manager setup
calls themselves are wired in the `wireXxxManager()` functions in
mainwindow_managerwiring.cpp (each builds the manager's Setup struct, wires
owner-supplied closures, then calls `setupReferences()`) plus the `setupXxx()`
helpers in mainwindow_setup.cpp.

The scroll layer is additionally exposed as six narrow **role interfaces**
(Kartend-h1l8f): `IScrollManager` is a pure facade union of
`IVirtualScrollLifecycle`, `IGridLayoutScroll`, `ISelectionOverlayScroll`,
`ISearchStateScroll`, `IArtworkPreviewScroll`, and `IScrollDataSource`, each
reachable through its own ctx accessor (`ctx->scrollLifecycle()`,
`ctx->scrollGrid()`, `ctx->scrollOverlay()`, `ctx->scrollSearch()`,
`ctx->scrollPreview()`, `ctx->scrollData()`). All seven pointers alias the
same `ScrollManager` and are seeded/nulled in lockstep via
`ManagerRefs::seedScrollRoles()`. Consumers take the narrowest role(s) they
use; the facade remains for consumers spanning three or more roles, for the
`virtualScrollSetupComplete` signal connection, and for QObject-based
lifetime guards (`QPointer`, `singleShotGuarded`) — the roles are plain
abstract classes.

### The friend + back-pointer engine pattern

When a manager/widget grows a cohesive sub-unit too large to stay in its TU
(a layout engine, a load scheduler, a paint helper), the codebase extracts it
as a **friend class holding a guarded back-pointer to its host** rather than
duplicating state or widening the host's public API. Invariants:

- **Canonical state stays on the host.** The engine reads/writes the host's
  members through friendship; it keeps no shadow copies, so the host's other
  helpers and existing access sites are unchanged.
- **The back-pointer is guarded**: `QPointer<Host> m_owner` (see
  `VirtualScrollEngine::m_owner`), with the engine Qt-parented under (or
  member-owned by) the host so under normal teardown the host outlives it —
  the QPointer only defends against a future destruction-order refactor.
- **No public leakage.** Friendship is the whole point: the host's privates
  stay private to everyone except the one named engine. Don't add public
  accessors "for the engine".
- **Teardown**: the engine dies with its host (QObject parent or member
  order); it must not touch `m_owner` from its own destructor.

Current adopters: `VirtualScrollEngine` (ScrollManager),
`ViewportArtworkScheduler` (ArtworkManager), `EntityScrapeCoordinator`
(ScraperService), `CoverFlowGalleryStrip` (CoverFlowWidget),
`ScrapeResultDialogUnified` (ScrapeResultDialog), `DetailsPaneArtwork` +
`DetailsPaneMetadataView` (DetailsPane).

**Prefer the no-friend alternative when the helper needs only a narrow
surface.** `DetailsPaneGalleryView` uses the host's public API plus injected
widget refs (no friendship), and `CollectionRemover` talks to SettingsDialog
through the small `CollectionRemoverHost` interface that *replaced* a former
`friend` declaration. Reach for friendship only when the sub-unit genuinely
shares the host's mutable state; if a dozen members suffice, inject them.

### Two ctx patterns: `ApplicationContext` vs controller-ctx structs

There are two distinct context shapes in the codebase. They are
intentionally different — the naming distinguishes them at a glance.

| Shape | Holder | Accessor style | Used by |
|-------|--------|----------------|---------|
| `ApplicationContext` | Raw `IXxxManager *` fields | `ctx->scrollManager()` (**no** `get` prefix) | Every manager outside `src/core/` reaches its siblings this way |
| `<Controller>Context` (e.g. `MenuControllerContext`, `ScrollEventsControllerContext`) | `std::function<XxxManager *()>` callbacks | `m_ctx.getScrollManager()` (**with** `get` prefix) | `src/core/` controllers that need lazy, MainWindow-mediated access during the bring-up window when manager pointers are still null |

The `get` prefix on controller-ctx fields is the conventional C++ getter
hint for a callable / `std::function`, signalling that the call site is
invoking a thunk rather than dereferencing a pointer. The plain accessor
on `ApplicationContext` reflects that those *are* direct pointer reads
of a value already resolved. Don't normalise the two — the prefix is the
signal.

The audit-grep against legacy `mainWindow->getXxxManager` callers
(documented in `.scripts/check-layering.py`) is unaffected: it matches
on the `mainWindow->` qualifier, which neither pattern uses.

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

For data integrity when writing a whole-file payload to disk, call
`PathUtils::atomicWriteFile(filePath, data)` (`src/utils/fs/pathutils.h`)
instead of hand-rolling the sequence. It creates the parent directory,
writes through `QSaveFile` (sibling temp file + atomic rename on commit,
cancelling on a short write) and then `PathUtils::syncDirectory()`s the
parent so the rename itself survives crash / power loss. It returns `bool`
and logs the failing stage under the `kartend.pathutils` category; callers
that report through `ErrorUtils::Result` wrap the `false` return in their
own domain-specific `ErrorContext`.

Adopters: `SessionManager`, `PlaylistSerializer`, `ScrapePendingState`,
`ThemePresetIO` / `PresentationProfileIO`. Two writers intentionally keep
their own `QSaveFile` sequence and must not be "cleaned up" onto the
helper: `KartWriter` streams archive entries incrementally (the payload
cannot be buffered as one `QByteArray`), and `CacheDiskStorage` batches
the parent-directory fsync once per save batch instead of per file
(Kartend-6n5r).

### QObject lifecycle: teardown guards (and where `parent()` still matters)

**Primary convention (current).** Manager teardown safety is guarded by
**explicit shutdown flags**, not by `parent()` checks: per-class
`m_destroying` / `m_isShuttingDown` members, `appNotShuttingDown()`-style
predicates threaded through setup structs, and
`QApplication::closingDown()`. The historical `!parent()` manager guards
have been retired as they were found to be dead or fragile — e.g.
`SelectionRestoreCoordinator`'s former `!parent()` check was removed as
dead (Kartend-je2wy; the coordinator's parent is non-null for its whole
lifetime) and `SettingsManager`'s
`dynamic_cast<IMainWindow *>(parent())` host derivation moved off the
manager entirely (see settingsmanager.cpp:285's provenance comment).
When you need a destruction-phase guard in new code, add a shutdown
flag — don't reach for `parent()`.

**Two scopes — pick deliberately (Kartend-m15wq).** The shutdown flags
above come in two distinct *scopes* that become true at *different times*
during teardown. Choosing the wrong one for a new slot reads a stale
"safe" answer and touches a half-destroyed object — exactly the
shutdown-only use-after-free class that is hard to reproduce. Match the
scope to the question you are actually asking:

- **Manager-local — "is *this specific manager* being destroyed?"**
  Canonical shape: a per-class `bool m_destroying` set to `true` as the
  *first* statement of that manager's destructor (see
  `ScrollManager::~ScrollManager`, `InteractionManager::~InteractionManager`).
  Flips early, per manager, in member-destruction order. Use it for slots
  and callbacks that can fire *into one manager* while that manager (or its
  owned sub-objects) is tearing down — e.g.
  `InteractionManager::eventFilter` short-circuiting on `m_destroying`, or
  `SelectionDisplayManager::updateSelectionForIndex` consulting its owner's
  destruction state.
- **App-global — "is the *whole application* shutting down?"**
  Canonical shape: the `appNotShuttingDown()` / `m_isShuttingDown()`
  predicate threaded through the setup struct (a `std::function<bool()>`
  reading the app-level shutdown flag), backstopped by
  `QApplication::closingDown()`. See
  `SelectionRestoreCoordinator::validateSelectionRestoreContext`
  (`selectionrestorecoordinator.cpp`), which gates deferred restore timers on
  both. Flips once, late, for the entire process. Use it for deferred work
  (timers, queued lambdas) that should abandon during application quit
  regardless of any single manager's destruction progress.

These do **not** flip together: a manager can be mid-destruction
(`m_destroying == true`) while the app is *not* yet shutting down (e.g. a
live theme reload or layout swap), and the app can be shutting down before
a given manager's destructor has run. A guard chosen for the wrong scope
is silently wrong only at teardown.

**The `m_destroyingProvider` callback (manager-local, indirected).**
`SelectionDisplayManager` is owned by `ScrollManager` but lives in a
separate translation unit and must not cyclically `#include` it, so it
reads its owner's manager-local `m_destroying` through a
`std::function<bool()>` provider wired in
`ScrollManager::setupReferences`
(`m_selectionDisplay->setDestroyingProvider([this] { return m_destroying; })`,
scrollmanagersetup.cpp). This is the *same* manager-local scope as a direct
`m_destroying` read — the callback only exists to cross the TU boundary
without exposing `ScrollManager`'s private flag as public API. Converging
it onto a direct accessor would require adding a public destruction accessor
to `ScrollManager` and is **not** a behavior-neutral mechanical change, so
the callback indirection is retained intentionally (Kartend-m15wq). Treat
`m_destroyingProvider()` as the manager-local predicate, not a fourth idiom.

**Where `parent()` IS still load-bearing.** Two narrower patterns
survive and are legitimate:

- **Chrome/navigation overlay event filters** compare
  `watched == parent()` before re-laying themselves out on parent
  resize (`BackdropBlurOverlay`, `BackgroundVideoWidget`,
  `LoadingOverlay`, `VignetteOverlay`) — when the overlay has been
  re-parented away from the original chrome, the event is no longer
  theirs to handle.
- **Dialog host derivation**: `ShortcutsDialog` and `SettingsDialog`
  re-derive their host via `dynamic_cast<IMainWindow *>(parent())`
  (`SettingsDialog` caches it once in its constructor). A *non-null*
  parent that isn't an `IMainWindow` now **warns** — and `SettingsDialog`
  additionally `Q_ASSERT_X`s — so the cast no longer fails silently
  (Kartend-rn0ym). Genuine headless contexts (tests, CLI flows) pass
  `nullptr` and skip MainWindow-specific wiring by design.

**Rule for changing constructor parents.** Before flipping a
constructor's parent from `this` to `nullptr` (or removing a parent
argument), `grep -rn "parent()" src/<module>` and confirm no surviving
guard of the two kinds above relies on it. The grep is cheap and the
silent-no-op failure mode is not.

The full hard-rule wording — including the
"setup-struct sibling-manager pointer" complement — lives in
[`.github/copilot-instructions.md` §7](../../.github/copilot-instructions.md).
That file is the source of truth; this section is a developer-facing
explainer of why it exists.
