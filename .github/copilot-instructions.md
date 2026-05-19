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
9. **Update CHANGELOG.md** — if the change is user-visible (new feature, bug fix, behaviour change, breaking change, removed/deprecated feature, security fix), append a bullet to the matching subsection under `[Unreleased]` (`### Added`, `### Changed`, `### Fixed`, `### Removed`, `### Deprecated`, `### Security`). One bullet per logical change, written for end users (not "refactored InternalManager" — write what the user can now see/do/avoid). Internal refactors, agent-doc edits, dev-tooling changes, and test-only changes skip this step. Keep beads IDs out of the bullet (per the hard rule below).
10. **Close the issue(s)** — `bd close <id1> <id2> ...` (batch when possible). Use `--reason="..."` if non-obvious.
11. **MANDATORY PUSH** — work is NOT complete until `git push` succeeds:
    ```bash
    git status                # verify what changed
    git add <files>
    git commit -m "..."
    git pull --rebase
    git push                  # push code (beads DB is local-only — never pushed)
    git status                # MUST show "up to date with origin"
    ```
12. **Verify** — `git status` clean, `bd list --status=in_progress` empty (or only intentionally deferred work).

### Hard Rules

- ❌ Never start coding before there is a `bd` issue ID claimed by you.
- ❌ Never use `bd edit` (opens `$EDITOR`, blocks the agent). Use `--title/--description/--notes/--design/--append-notes`.
- ❌ Never close an issue without committing AND pushing the corresponding code.
- ❌ Never invent placeholder issue IDs — always use real `bd-*` IDs returned by `bd create`.
- ❌ **Never reference beads issue codes (`Kartend-XXXX`) in code, comments, commit messages, docstrings, or any user-facing surface** (tooltips, labels, dialog text, AppStream metadata, README, CHANGELOG, etc.). Beads IDs are internal-only — they belong in `bd` notes/design fields and the `bd` database, never in the codebase. The issue's *content* (rationale, design) goes in the comment; the *ID* stays out.
- ✅ When the user describes new work, your FIRST action is `bd create` (or `bd ready` to check for an existing match).
- ✅ When in doubt about priority: P0=critical/blocking, P1=high, P2=normal (default), P3=backlog, P4=future/nice-to-have.
- ✅ When discovering related work mid-task, file it (`--deps discovered-from:<id>`) — don't expand the current issue's scope.
- ❌ **Never restructure `src/` or `tests/` without refreshing the agent docs in the same commit.** Any folder move/rename/add/removal under `src/modules/`, `src/utils/`, `src/ui/uiconstants/`, or `tests/` — and any manager or `UIConstants::*` sub-namespace rename — MUST regenerate the matching trees and tables in `.github/copilot-instructions.md`, `docs/architecture.md`, `docs/testing.md` (for `tests/` changes), and any `docs/guide/*.md` page that links into the moved path. Stale paths silently mislead every future agent. Before closing the issue, run `grep -rn 'src/modules/<old>\|src/utils/<old>\|OldClassName' docs/ .github/` — it should return zero hits.
- ❌ **Never close a user-visible bd issue without appending a bullet to `CHANGELOG.md` `[Unreleased]`.** Anything missed at close time stays missed forever — the release-cut step at `CONTRIBUTING.md:91` promotes `[Unreleased]` verbatim to the new version's section. "User-visible" = a user could notice the difference: new feature, bug fix, behaviour change, removed/deprecated feature, security fix. Internal refactors, agent-doc edits, dev-tooling, and test-only changes are exempt. The empty `### Added` / `### Changed` / `### Fixed` skeleton lives at the top of `CHANGELOG.md` so there's no ambiguity about where to append.

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
├── api/                 # Neutral header-only role interfaces (i*.h); no .cpp
├── core/                # Main application entry and window
├── modules/             # Feature modules grouped by domain
│   ├── behavior/        # Manager lifecycle coordination
│   │   └── application/ # Manager lifecycle coordination
│   ├── data/            # Persistence: SQLite, cache, sessions, settings, playlists
│   │   ├── cache/       # In-memory pixmap cache, disk persistence
│   │   ├── database/    # SQLite coordination via worker thread
│   │   ├── dat/         # Offline DAT-file identification + on-disk parse cache
│   │   ├── kart/        # Kart (collection bundle) import/export
│   │   ├── playlist/    # Playlist storage and export (JSON / M3U)
│   │   ├── query/       # Worker thread SQL queries
│   │   ├── restore/     # Selection state restoration during navigation
│   │   ├── scraper/     # Metadata scraping
│   │   │   ├── core/        # Orchestration, HTTP, persistence, credentials
│   │   │   ├── parsers/     # Provider-response parsers
│   │   │   └── providers/   # Metadata providers + registry
│   │   ├── session/     # Selection state persistence
│   │   └── settings/    # Config file I/O, settings dialog
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
│       ├── detailpage/  # Detail-page coordinator
│       └── detailspane/ # Metadata / details side pane
├── ui/                  # UI components and constants
│   ├── dialogs/         # Dialogs and settings panels, grouped by domain
│   │   ├── settings/    # Settings dialog core + all tab panels
│   │   ├── collection/  # Collection create / remove / picker dialogs
│   │   ├── launcher/    # Launcher chooser / editor dialogs
│   │   ├── scraper/     # Scraper settings / credentials / result dialogs
│   │   └── kart/        # Kart merge / progress dialogs
│   │                    # (errordialog, firstrunwizard, shortcutsdialog,
│   │                    #  statisticsdialog stay at dialogs/ root)
│   └── widgets/         # Item widget, details pane, generic overlays,
│                        # video-preview widgets
└── utils/               # Shared utilities and data structures
```

### Module Layering

All of `src/` currently links into one `OBJECT` library, so module
boundaries are **not** compiler-enforced. One invariant is enforced by a
lint instead:

> **`src/utils/` is the foundation layer.** It must not `#include` any
> header from `src/modules/`, `src/ui/`, or `src/core/`. Shared code that
> a utils file needs belongs *in* `src/utils/`.

> **`src/api/` is the neutral role-interface layer.** It holds the abstract
> manager interfaces (`i*.h`) and carries no `.cpp`. Each interface depends
> only on Qt headers, `src/utils/` headers, and forward declarations, so any
> module may `#include` an `i*.h` without creating a module→module edge.
> Consumers that only call interface-covered methods should include the
> relevant `src/api/i*.h` instead of the concrete manager header.

`.scripts/check-layering.py` checks this and runs in the
`maintenance-check` CI job. (`uiconstants.h` is a documented allowlisted
exception — a pure constants namespace pending relocation.) Splitting each
top-level module into its own library with explicit `target_link_libraries`
is the longer-term fix; until then, do not add upward edges from utils.

### Manager Hierarchy

`MainWindow` owns `ApplicationManager`, which creates top-level managers in controlled destruction order (declared bottom-up in `applicationmanager.h`).

**Two-tier ownership model:**
- **ApplicationManager** owns: `CacheManager`, `SessionManager`, `ArtworkManager`, `SettingsManager`, `DatabaseManager`, `ScrollManager`, `DetailsPaneManager`, `NavigationManager`, `InteractionManager`, `PlaylistManager`, `DetailPageManager`, `KartManager`
- **InteractionManager** owns: `SearchManager`, `SelectionManager`, `KeyboardManager`, `GamepadManager`, `ArrowNavigationHandler`, `AlphabeticNavigationHandler`, `AnimationManager`, `MouseManager`, `LaunchManager`, `ViewportManager`, `EventManager`, `AttractManager`

Additional helper managers owned by their parent feature module (not top-level): `WidgetPoolManager`, `FilterManager`, `SelectionRestoreManager`, `SelectionOverlayManager`, `SearchLoadingOverlay`, `NavigationStackManager`.

**Sub-manager registration:** InteractionManager's owned sub-managers are registered in `ApplicationContext` eagerly — *before* any `setupReferences()` runs — by `MainWindow::initializeAppContext()`. The unique_ptrs are constructed in InteractionManager's ctor, so they're addressable as soon as InteractionManager itself exists. Registering them up front is the precondition that lets every manager's setupReferences read siblings exclusively through `ctx->managers.*`.

```cpp
// In MainWindow::initializeAppContext(), as soon as InteractionManager is constructed:
if (auto *im = getInteractionManager()) {
  m_appContext.managers.animationManager = im->animationManager();
  m_appContext.managers.selectionManager = im->selectionManager();
  // ... and so on for every owned sub-manager
}
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
| `DetailsPaneManager` | ApplicationManager | details/metadata side pane coordinator |
| `PlaylistManager` | ApplicationManager | owns its own SQLite connection on the main thread |
| `DetailPageManager` | ApplicationManager | detail-page coordinator |
| `KartManager` | ApplicationManager | Kart import/export coordinator |
| `AttractManager` | InteractionManager | attract-mode idle scroll/advance |
| `GamepadManager` | InteractionManager | `dpadPressed`, `buttonPressed` (compiled in when Qt6::Gamepad or SDL2 is found) |
| `AlphabeticNavigationHandler` | InteractionManager | `requestSelectionByIndex` |
| `WidgetPoolManager` | ScrollManager | (helper, no signals) |
| `FilterManager` | ScrollManager | (helper, no signals) |
| `SelectionOverlayManager` | ScrollManager | (helper, no signals) |
| `SelectionRestoreManager` | NavigationManager | (helper, no signals) |
| `NavigationStackManager` | NavigationManager | (helper, no signals) |
| `CollectionBackgroundController` | NavigationManager | (helper, no signals) — drives the items-page background/overlay UI surface |

### Atomic File Writes Pattern

For data integrity when writing to disk, use `QSaveFile` (which handles the
temp-file + atomic-rename dance) plus `PathUtils::syncDirectory()` so the
rename itself survives a crash or power loss:

```cpp
#include <QSaveFile>
#include "utils/fs/pathutils.h"

bool atomicWriteFile(const QString &filePath, const QByteArray &data) {
  const QString parentDir = QFileInfo(filePath).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
    return false;
  }

  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  if (file.write(data) != data.size()) {
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    return false;
  }
  PathUtils::syncDirectory(parentDir);
  return true;
}
```

Canonical adopters: `SessionManager::atomicWriteFile`,
`PlaylistManager::exportToJson` / `exportToM3U`, `KartWriter`, `KartReader`,
`CacheDiskStorage`. Do **not** hand-roll `QFile` + temp-name + `rename()` —
`QSaveFile` already gets the ordering right and the parent-dir fsync is the
piece raw `rename()` calls forget.

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

`ApplicationContext` is the single source of truth for sibling-manager pointers. Managers must NOT cache sibling-manager pointers as direct member fields. Read them through `m_ctx` at the point of use — typically via thin private inline accessors.

```cpp
// Header: setup struct only carries ctx + non-manager refs (UI widgets,
// collection-state pointers, callbacks). Sibling managers are NEVER fields.
struct ScrollManagerSetup {
  const ApplicationContext *ctx = nullptr;
  QWidget *gridContainer = nullptr;
  // ... non-manager refs only
};

class ScrollManager : public QObject {
private:
  // ctx is the single source of truth for sibling managers.
  const ApplicationContext *m_ctx = nullptr;

  // Optional: per-manager inline accessors keep call sites concise.
  [[nodiscard]] ArtworkManager *artworkMgr() const {
    return m_ctx ? m_ctx->artworkManager() : nullptr;
  }
};

// Implementation: setupReferences stores ctx, then non-manager fields.
void ScrollManager::setupReferences(const ScrollManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_gridContainer = setup.gridContainer;
}

// Use ctx at call sites (snapshot for repeated reads in a function).
void ScrollManager::doWork() {
  if (auto *art = artworkMgr()) {
    art->scheduleViewportUpdate();
  }
}
```

`MainWindow::initializeAppContext()` populates `ctx->managers.*` for every top-level manager *and* the InteractionManager-owned sub-managers BEFORE any `setupReferences()` runs. This is required so each manager's setupReferences sees a fully-populated context. Top-level managers that need siblings at construction time (DatabaseManager, SettingsManager) take `const ApplicationContext *ctx` in their constructor; `ApplicationManager::initialize(ApplicationContext *)` populates ctx incrementally as it constructs each manager so later constructors see the earlier ones.

Setup calls are wired in `MainWindow::setupManagers()` and related methods.

### Scroll Module (`src/modules/input/scroll/`)

The scroll module handles virtual scrolling with widget pooling:

| Class | Purpose |
|-------|---------|
| `ScrollManager` | Core orchestration of virtual scrolling, viewport updates |
| `CoverFlowController` | Drives the CoverFlow ViewType — owns the carousel widget, syncs its card list / config / visibility with the grid |
| `VirtualScrollEngine` | Visible-range computation and row materialization |
| `GridLayoutCalculator` | Stateless grid metric calculations (row/column positions) |
| `ItemWidgetFactory` | Widget creation and configuration, delegates to the pool |
| `VirtualContainerManager` | Container lifecycle and sizing |
| `ScrollDataManager` | Scroll-side item/data bookkeeping |
| `DataSourceManager` | Item data-source binding for the scroll view |
| `SelectionCoordinator` | Selection state and movement analysis |
| `SelectionDisplayManager` | Selection rendering on materialized widgets |
| `SelectionStateTracker` | Selection-state snapshot tracking |
| `ScrollEventHandler` | Scroll event wiring and user scroll detection |
| `ArrowKeyScrollHelper` | Arrow-key driven scroll stepping |
| `PreSearchStateManager` | Saves/restores pre-search scroll state |

Note: the widget-recycling pool (`WidgetPoolManager`), search/subcollection
filtering (`FilterManager`), and glide-overlay rendering
(`SelectionOverlayManager`) are *not* in this folder — they live in
`src/modules/input/widgetpool/`, `src/modules/input/filter/`, and
`src/modules/input/overlay/` respectively.

### Interaction Module (`src/modules/input/interaction/`)

The interaction module coordinates user input handling:

| Class | Purpose |
|-------|---------|
| `InteractionManager` | Central coordinator for input handling, owns sub-managers |
| `InteractionStateHolder` | Centralized typed state replacing scattered dynamic properties |

### Database Module (`src/modules/data/database/`, `src/modules/data/query/`)

Database operations use a worker thread pattern:

| Class | Purpose |
|-------|---------|
| `DatabaseManager` | Main thread coordinator, emits signals to worker |
| `QueryManager` | Worker thread, executes SQL queries, emits results |
| `ScanService` | Owns QueryManager's collection-rescan subsystem (filesystem walk, `scanned_items` staging, scan-and-save pipelines, scan cancellation token); borrows the worker `QSqlDatabase` + `PreparedStatementCache` by reference |

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

Utilities are grouped by concern in six subfolders. Each has its own
`CMakeLists.txt` and contributes to the same `kartend_utils` target.

#### `src/utils/app/` — Application context, config, error model, logging

| Utility | Purpose |
|---------|---------|
| `applicationcontext.h` | `ApplicationContext` struct (collection / ui / managers sub-structs) shared across managers |
| `cliargs.{h,cpp}` | Command-line argument parsing (startup-collection override, headless Kart import/export) |
| `collectiontypes.h` | Standalone enums extracted from `collectionutils.h` (`HorizontalAlignment`, `DetailsPaneMode`, etc.) |
| `collectionutils.{h,cpp}` | `CollectionConfig`, `CollectionContext`, `CollectionHierarchyCache`, validation helpers |
| `errorutils.h` | `ErrorCode` enum, `ErrorContext` struct, `Result<T>` template, `lcErrors` logging category |
| `loggingcategories.{h,cpp}` | Cross-cutting `Q_LOGGING_CATEGORY` declarations (`lcPerfTrace`, `lcSearchDiag`, `lcScanFlow`) |
| `propertyutils.h` | `PropertyKeys` namespace with Qt dynamic property key constants |
| `scrapelogger.{h,cpp}` | Optional scrape-diagnostic logging, driven by the scrape-logging setting |
| `settingsutils.{h,cpp}` | Settings file path resolution, INI helpers |
| `setuputils.h` | Macros that reduce setup-struct getter boilerplate (`SETUP_GETTER_*` family) |
| `stateutils.h` | Centralized state structs (`SelectionRestoreState`, `ScrollState`, etc.) replacing scattered dynamic properties |

#### `src/utils/db/` — SQLite schema, stores, per-item persistence

| Utility | Purpose |
|---------|---------|
| `dbmigrations.{h,cpp}` | SQLite schema migration steps, `PRAGMA user_version` management |
| `historystore.{h,cpp}` | `launch_history` table access |
| `itemartwork.{h,cpp}` | `item_artwork` table — per-item artwork overrides (manual path + standard-type fallback) |
| `itemmetadata.{h,cpp}` | `item_metadata` table — custom titles, descriptions, genres, key/value fields |
| `itemmetadatacache.{h,cpp}` | Per-item LRU (256 entries) fronting `loadItemMetadata` / `loadItemArtwork` / `loadItemUsageStats`. Three slots per (collectionUuid, path); invalidated on writes, rescans, and reconnects. |
| `smartfilter.{h,cpp}` | Serializable filter spec driving a smart playlist's per-open query |
| `smartplaylistevaluator.{h,cpp}` | Translates a `SmartFilter::Filter` into concrete (collection, item) matches |
| `usagestatsstore.{h,cpp}` | `play_count`, `last_played`, `total_play_seconds` on the items table |

#### `src/utils/fs/` — Filesystem paths, validation, extension classification

| Utility | Purpose |
|---------|---------|
| `configvalidation.{h,cpp}` | Schema validation of `CollectionConfig` plus `isCommandInPath()` |
| `extensionutils.{h,cpp}` | File extension categorization, media type detection |
| `launcherprobe.{h,cpp}` | PATH-probe for well-known launcher binaries |
| `pathutils.{h,cpp}` | Path validation with `Result<T>` support, expansion, `syncDirectory()` for crash-safe writes |
| `retroarchutils.{h,cpp}` | Discovery of a RetroArch install's libretro cores |
| `romhasher.{h,cpp}` | Stream-hashing of media files for hash-based scraper lookups |

#### `src/utils/text/` — Search modes, string formatting, title filtering

| Utility | Purpose |
|---------|---------|
| `searchutils.h` | `SearchMode` enum, search context utilities |
| `stringutils.h` | String manipulation, title formatting (number-with-commas etc.) |
| `titlefilter.{h,cpp}` | Process-wide title-cleanup engine; per-collection regex strip with read/write lock |

#### `src/utils/threading/` — Worker-thread orchestration and debouncing

| Utility | Purpose |
|---------|---------|
| `adaptivebatcher.h` | Thread-safe batch-size controller with EMA-smoothed timing feedback |
| `threadpoolutils.{h,cpp}` | Bounded-wait `QThreadPool` teardown with abandon-on-timeout fallback |
| `timerutils.{h,cpp}` | `TimerUtils::Coordinator` for debounced viewport updates, `TimerUtils::DebouncedTimer` for generic debouncing |

#### `src/utils/view/` — Grid math, artwork helpers, viewport rendering

| Utility | Purpose |
|---------|---------|
| `artworkutils.{h,cpp}` | Artwork file lookup, fuzzy matching, `Result<T>`-returning variants |
| `gridutils.h` | Grid layout calculations, row/column math, container sizing |
| `kdecolorscheme.{h,cpp}` | Discovery and parsing of KDE Plasma `.colors` scheme files |
| `placeholderwarmer.{h,cpp}` | Batch pre-export of procedural placeholder artwork |
| `textzoom.{h,cpp}` | Process-wide UI text zoom percentage (clamped to 50–300) |
| `videoutils.{h,cpp}` | Per-item preview-video file lookup (mirrors `artworkutils` for video) |

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

> **clang-tidy `--apply-fixes` trap:** the `modernize-use-default-member-init`
> fixer can mangle structs that combine an empty-bodied constructor with a
> member-initializer list (e.g. `T() : x(4), y(false) {}`) — it has been
> observed to leave behind broken `= default;` syntax on `CollectionConfig`
> and similar config structs. Prefer in-class member init from the start
> (`int x = 4;`) and drop the explicit constructor entirely, so the check has
> nothing to rewrite. After running `--maintenance --apply-fixes`, diff
> `src/utils/app/collectionutils.h` and any other config structs before
> committing.

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
├── CMakeLists.txt           # Monolithic test build configuration
├── modules/                 # Per-feature unit tests. One flat folder per
│   │                        # feature — NOT nested like src/modules/<group>/.
│   │                        # A test for src/modules/<group>/<feature>/ lives
│   │                        # at tests/modules/<feature>/ (group omitted).
│   ├── animation/           #   test_animationmanager
│   ├── artwork/             #   test_artworkmanager
│   ├── attract/             #   test_attracthelpers
│   ├── cache/               #   test_cachemanager
│   ├── database/            #   test_databasemanager
│   ├── dat/                 #   test_datcache, test_datlookup
│   ├── detailpage/          #   test_detailpagehelpers
│   ├── event/               #   test_eventhelpers
│   ├── filter/              #   test_filterhelpers
│   ├── gamepad/             #   test_gamepadhelpers
│   ├── interaction/         #   test_interactionhelpers, test_interactionstateholder
│   ├── kart/                #   test_kartmanifest, test_kartmerge, test_kartreader, test_kartwriter
│   ├── keyboard/            #   test_keyboardhelpers, test_keyboardmanager
│   ├── launch/              #   test_launchmanager
│   ├── mouse/               #   test_mousehelpers
│   ├── navigation/          #   test_navigationhelpers, test_navigationstackmanager,
│   │                        #   test_emptystatewidget
│   ├── overlay/             #   test_overlayhelpers
│   ├── playlist/            #   test_playlistmanager
│   ├── query/               #   test_queryhelpers, test_querymanager_{abspath,broken_symlinks,cancel_scan,cross_collection_count,shell_collection_sort}
│   ├── restore/             #   test_selectionrestorehelpers
│   ├── scraper/             #   provider / parser / persistence tests + stubmetadataprovider.h
│   ├── scroll/              #   test_gridlayoutcalculator, test_scrolldatamanager, test_scrollhelpers
│   ├── search/              #   test_searchhelpers
│   ├── selection/           #   test_selectionhelpers
│   ├── session/             #   test_sessionmanager
│   ├── settings/            #   test_settingshelpers
│   ├── viewport/            #   test_viewporthelpers
│   └── widgetpool/          #   test_widgetpoolmanager
├── utils/                   # Per-helper unit tests — ONLY for src/utils/.
│   ├── test_cliargs.cpp     #   (module tests do NOT belong here; see modules/)
│   ├── test_collectionutils.cpp
│   ├── test_configvalidation.cpp
│   ├── test_dbmigrations.cpp
│   ├── test_gridutils.cpp
│   ├── test_historystore.cpp
│   ├── test_itemartwork.cpp
│   ├── test_itemmetadata.cpp
│   ├── test_itemmetadatacache.cpp
│   ├── test_kdecolorscheme.cpp
│   ├── test_launcherprobe.cpp
│   ├── test_pathutils.cpp
│   ├── test_placeholderwarmer.cpp
│   ├── test_retroarchutils.cpp
│   ├── test_romhasher.cpp
│   ├── test_scrapelogger.cpp
│   ├── test_searchutils.cpp
│   ├── test_smartfilter.cpp
│   ├── test_smartplaylistevaluator.cpp
│   ├── test_stringutils.cpp
│   ├── test_titlefilter.cpp
│   ├── test_usagestatsstore.cpp
│   └── test_videoutils.cpp
├── integration/             # Multi-manager scenarios, single binary, fixture-driven
│   ├── mainwindowfixture.{cpp,h}
│   ├── mocks/               # mockdatabasemanager.h, mocksettingsmanager.h, mockedmainwindowfixture.{cpp,h}
│   ├── test_main.cpp        # QApplication harness for the integration suite
│   ├── test_applicationmanager_lifecycle.{cpp,h}
│   ├── test_applysettingsdialog.{cpp,h}
│   ├── test_detailspane_coverflow.{cpp,h}
│   ├── test_eventmanager_detailspane.{cpp,h}
│   ├── test_mainwindow_smoke.{cpp,h}
│   ├── test_navigationmanager.{cpp,h}
│   ├── test_scrollmanager.{cpp,h}
│   ├── test_settingsdialog_changes.{cpp,h}
│   └── test_settingsdialog_scope.{cpp,h}
├── benchmarks/              # Perf benchmarks — ctest label "benchmark",
│   └── bench_filterhelpers.cpp   #   skipped by default; run via `ctest -L benchmark`
└── ui/widgets/              # Widget-level rendering and behavior
    └── test_coverflowwidget.cpp
```

### Building Tests

Enable tests with the `KARTEND_BUILD_TESTS` CMake flag:
```bash
cmake -S . -B build/ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKARTEND_BUILD_TESTS=ON
cmake --build build/ninja-release --parallel $(nproc)
```

### Running Tests

Run individual test:
```bash
cd build/ninja-release
./tests/test_gridlayoutcalculator
./tests/test_sessionmanager
./tests/test_pathutils
./tests/test_integration       # single binary for all tests/integration/*
# ... CMake places every test binary directly under build/<config>/tests/
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

Tests are grouped into five areas. Each `test_*.cpp` is a standalone Qt
Test binary discovered by CTest, with the exception of the integration
suite which links all of its `TestXxx` classes into a single binary
(`test_integration`) driven by `tests/integration/test_main.cpp`.

| Area | Path | Coverage |
|------|------|----------|
| Module unit tests | `tests/modules/<feature>/` | Per-manager and per-helper coverage for `src/modules/`. One flat folder per feature (the `behavior/data/input/media` group level is omitted). Includes `dat/` and `scraper/`. |
| Utility unit tests | `tests/utils/` | Helpers under `src/utils/` **only** — `app`, `db`, `fs`, `text`, `threading`, `view`. Tests for `src/modules/` files must NOT land here. |
| Integration tests | `tests/integration/` | One binary (`test_integration`). `MainWindowFixture`-driven multi-manager scenarios; shared mocks under `tests/integration/mocks/`. |
| UI widget tests | `tests/ui/widgets/` | Widget-level rendering and behavior for generic `src/ui/widgets/` widgets (e.g. `CoverFlowWidget`). Tests for widgets that live under `src/modules/` go in `tests/modules/<feature>/`. |
| Benchmarks | `tests/benchmarks/` | Perf benchmarks labelled `benchmark`; skipped by default. Run with `ctest -L benchmark`. |

Binary and method counts drift fast — prefer `ctest --output-on-failure
--test-dir build/ninja-release` for an authoritative list and pass count.

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

### Icon Theme

**Always render UI glyphs as KDE Breeze theme icons.** Do not embed unicode/emoji
("⊞ ☰ ◖◉◗ 🏷 🔎") or QString text in place of an icon — those don't follow the
user's chosen icon theme, scale poorly across DPI, and look out of place next to
real Breeze icons.

- Look up the canonical Breeze name on
  [api.kde.org/frameworks/breeze-icons](https://api.kde.org/frameworks/breeze-icons/html/index.html)
  before introducing a new glyph. Common names already wired in
  [src/ui/uiconstants/icons.h](src/ui/uiconstants/icons.h):
  `search`, `filter-symbolic`, `application-menu`, `view-choose`, `folder`,
  `folder-open`, `folder-documents`, `view-preview`.
- Resolve via `UIConstants::Icons::fromTheme(name)` (single name) or the
  initializer-list overload (name + fallbacks). Prefer adding new constants to
  `icons.h` over inlining string literals at the call site.
- For toolbar / button glyphs: set `setIcon(...)` and clear the legacy text
  with `setText(QString())`. Use `setToolButtonStyle(Qt::ToolButtonIconOnly)`
  on QToolButtons. Hide the auto-injected popup arrow on
  `InstantPopup`-mode QToolButtons via stylesheet
  `QToolButton::menu-indicator { image: none; width: 0; }` so the icon stays
  centered.
- For QLineEdit-embedded actions (e.g. the search-mode toggle), use
  `QLineEdit::addAction(icon, QLineEdit::LeadingPosition)` rather than
  parking a sibling QPushButton next to the field.
- Add new Breeze icon names to `UIConstants::Icons` rather than repeating
  raw strings; the wrapper centralizes fallback chains for distros that ship
  partial Breeze packages.

### UI Constants

All magic numbers live in `UIConstants` namespace (`src/ui/uiconstants.h`). **Never hardcode** timing, spacing, or dimensions.

UIConstants is organized into logical sub-namespaces:

| Namespace | Purpose |
|-----------|---------|
| `UIConstants::Animation` | Scroll/pulse animation durations and keyframes |
| `UIConstants::Artwork` | Async artwork loading batch sizes, throttling |
| `UIConstants::Attract` | Idle-triggered attract-mode autoscroll tunables |
| `UIConstants::Cache` | Memory cache sizing, disk persistence timing |
| `UIConstants::CollectionIcon` | Collection preview/icon display sizing |
| `UIConstants::Color` | Color manipulation for theme-aware rendering |
| `UIConstants::Concurrency` | Background thread pool sizing |
| `UIConstants::Database` | DB-backed scan and cache-validation thresholds |
| `UIConstants::DetailsPane` | Details-pane dimensions and timing |
| `UIConstants::Dialog` | Application dialog dimensions |
| `UIConstants::Gamepad` | Analog stick deadzones for digital navigation |
| `UIConstants::Grid` | Virtual scrolling grid layout and container sizing |
| `UIConstants::Icons` | Breeze/Plasma theme icon names |
| `UIConstants::Item` | ItemWidget sizing, fonts, validation bounds |
| `UIConstants::Keyboard` | Arrow-key repeat rate and centering timing |
| `UIConstants::Launch` | External process spawn / archive extract limits |
| `UIConstants::ListView` | Text-only list view mode constants |
| `UIConstants::Metadata` | Metadata sidebar layout constants |
| `UIConstants::Mouse` | Click-hold, wheel, and double-click timing |
| `UIConstants::Navigation` | Collection navigation, progress, scrollbar recovery |
| `UIConstants::Offset` | Common offset values for layout calculations |
| `UIConstants::Overlay` | Transient overlay timing (e.g. search loading) |
| `UIConstants::Placeholder` | Deterministic placeholder artwork generation |
| `UIConstants::Scroll` | Virtual scroll, viewport, artwork prewarm tunables |
| `UIConstants::Search` | Search debounce and refocus timing |
| `UIConstants::Selection` | Selection restore and double-click timing |
| `UIConstants::Timing` | General delays (ms unless noted) |
| `UIConstants::Viewport` | Main content area sizing constraints |
| `UIConstants::Widget` | ItemWidget dimensions, borders, pool sizing |

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

### Class Naming Conventions

Class name suffixes are **role markers** — pick the one that matches what the
class *is*, and don't invent new suffixes. The codebase currently uses 39
`*Manager` classes; that name is overused, so prefer a more specific suffix
below when one fits, and reserve `Manager` for genuine feature-area owners.

| Suffix | Role | Notes |
|--------|------|-------|
| `Manager` | Long-lived stateful owner/coordinator of a feature area | Default only when nothing more specific fits |
| `Handler` | Focused responder to one event/input kind | e.g. `WheelEventHandler`, `HoverScrollHandler` |
| `Controller` | Drives one UI surface (menu, toolbar, dialog) | e.g. `MenuController`, `ToolbarController` |
| `Coordinator` | Cross-cutting orchestration helper, owns no feature state | e.g. `SelectionCoordinator` |
| `Engine` | Pure computational core, no Qt/UI dependencies | e.g. `VirtualScrollEngine` |
| `Service` | Stateless-ish operation provider, callable by many owners | e.g. `ScraperService` |
| `Worker` | Runs on a non-main (worker) thread | pair with `QThread`/`QtConcurrent` |
| `Provider` | Pluggable backend behind an interface | e.g. `ScreenScraperProvider` |
| `Registry` | Lookup table of providers/objects by key | e.g. `MetadataProviderRegistry` |
| `Factory` | Constructs and configures objects | e.g. `ItemWidgetFactory` |
| `Parser` | Parses an external response/format into typed data | e.g. `TmdbParser` |
| `Cache` | Caching layer fronting a slower source | e.g. `FileMapCache` |
| `*helpers` (file) | Free-function namespace, no class | e.g. `scrollhelpers.cpp` |

When unsure, prefer the **narrowest** accurate suffix. Renaming existing
misnamed classes is deferred to dedicated rename passes — do not bulk-rename
opportunistically inside an unrelated change.

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

