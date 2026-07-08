# MainWindow Partial Files — Responsibility Map

`MainWindow` is the top-level Qt window that owns `ApplicationManager`
and orchestrates everything the user sees. Its implementation is split
across eight sibling translation units under `src/core/` so the file
sizes stay tractable and grep stays fast. This page is the canonical
rule for what goes where and where to put new code.

> Heads up: every partial declares its members in the single
> `mainwindow.h` header. Splits are by *responsibility*, not by feature
> — adding a new manager wiring doesn't add a new partial, it goes in
> the existing one for its kind.

## Partial responsibilities

| File | Responsibility | What lives here |
|---|---|---|
| `mainwindow.cpp` | The class core: constructor, destructor, event handlers (`closeEvent`, `changeEvent`, `eventFilter`), and the small grab-bag of single-method handlers that don't fit a more specific partial. Also home to `Q_LOGGING_CATEGORY(lcMainWindow, ...)`. | `MainWindow::MainWindow`, `~MainWindow`, `closeEvent`, `changeEvent`, the keyboard / focus event handlers, the runtime-finished / details-page lambdas, `setViewTypeFromToolbar`, `refreshTitleCounts`, the marquee shims (`applyMarqueeSettings`, `updateMarqueeArtwork` — one-line forwarders into MarqueeController). |
| `mainwindow_setup.cpp` | One-shot construction: building `ApplicationContext`, instantiating the controllers that aren't managers (`DialogController`, `ToolbarController`, `MenuController`, `ScrollEventsController`, `DbEventsController`), wiring the menu bar, applying loaded settings to widgets at startup. | `setupApplicationContext`, `setupControllers`, `setupMenuBar`, `applySettingsToUi`, anything called from the constructor that's "create / wire / configure" but not "connect signal". |
| `mainwindow_wiring.cpp` | Signal/slot wiring tables. **Only** `QObject::connect()` calls and the slot-handler implementations that route to extracted controllers. One `connect<X>Manager()` function per sender; high-frequency connects use `Qt::UniqueConnection` per the file-header convention. | `connectDatabaseManager`, `connectScrollManager`, `connectSidebarManager`, `connectInteractionManager`, and the small slot handlers MainWindow keeps because they touch members the controllers don't have. |
| `mainwindow_managerwiring.cpp` | Per-manager **first-time setup** (Kartend-dk2c.5): each `wireXxxManager()` builds that manager's Setup struct, wires owner-supplied UI-layer closures (dialog runners, gating predicates), calls `setupReferences`, then connects any post-setup signals the manager needs. Runs once during construction, after `initializeAppContext()`. The split vs `mainwindow_wiring.cpp` is "first-time setup" here vs "ongoing signal routing" there. | The `wireXxxManager()` family. |
| `mainwindow_timers.cpp` | **Debounce/throttle timer wiring only.** `setupInitialTimers` defers post-construction work (centering scroll, first-run wizard, splash dismiss) and `setupDebouncedSaveTimers` wires the gridWidth debouncer's callbacks. Anything else that uses `QTimer::singleShot` lives at its real callsite. | `setupInitialTimers`, the debouncer-wiring helpers, the post-show defer-to-next-tick handlers. |
| `mainwindow_scraper.cpp` | IMainWindow forwarders into the extracted ScraperController / DatAuditController — the flows themselves live in those controllers. One real body remains: `datAuditStatusForCollection`, a short-lived app-DB read for the settings panel's audit hint (~110 LOC total). | `openScraperDialog`, `promptResumePendingScrapeIfAny`, `openDatAuditDialog`, `openDatAuditForCollection`, `datAuditStatusForCollection`. |
| `mainwindow_toolbar.cpp` | Items-page toolbar event handlers (zoom reset, pause-preview shortcut, volume slider) that don't go through ToolbarController. Most toolbar setup lives in `ToolbarController::initialize` — the partial only carries handlers that need MainWindow state. | Volume slider, video pause toggle, zoom-reset handlers, the items-page mini-toolbar wiring. |
| `mainwindow_dialogs.cpp` | Dialog and wizard launchers — every `void MainWindow::*Interactive()` and the `show*` / `manage*` entry points that the menu controller calls to open a modal. **Charter: each function is a thin wrapper — construct the dialog/wizard, populate its inputs from MainWindow state, call `exec()`, write results back.** No setup/init code, no signal wiring, no multi-step tool pipelines: the per-collection tool flows live in `LibraryToolsController` (this file keeps one-line delegations) and the command-palette registry comes from `MenuController::buildPaletteCommands`. | `showAbout`, `showFirstRunWizard`, `promptCreateFirstCollectionInteractive`, `importThemeInteractive`, `exportThemeInteractive`, `manageLayoutProfilesInteractive`, `navigateToItem`, `openCommandPalette`, `showBindingVisualizer`, `runNewLibraryWizard`, `createCollectionForDat`, `managePresentationProfilesInteractive`, `showScraperProvidersInteractive`, plus the one-line delegations `showCollectionHealthInteractive` / `showVariantGroupingInteractive` / `bulkEditInteractive` / `reviewMissingMetadataInteractive` / `artworkWizardInteractive`. |

## Rules for new code

1. **Manager creation goes in `mainwindow_setup.cpp`.** Adding a new
   manager? It gets instantiated by `ApplicationManager`, but the
   `setupApplicationContext` call that hands it dependencies belongs to
   `setup.cpp`. Adding a new *controller* (a non-manager helper like
   `DialogController`) — also `setup.cpp`.

2. **Signal/slot wiring goes in `mainwindow_wiring.cpp`.** New
   high-frequency signals (one with multiple subscribers, or one whose
   double-fire would corrupt state) take `Qt::UniqueConnection` per the
   file's preamble convention.

3. **Slot handlers**: if the handler does substantive work, prefer
   adding it to the relevant controller (DbEventsController,
   ScrollEventsController, ToolbarController). Only keep the handler on
   MainWindow when it has to touch a MainWindow-only member, and even
   then move the body into a controller as soon as you can give the
   controller a context for that member.

4. **Debounce/throttle timer wiring goes in `mainwindow_timers.cpp`.**
   One-shot `QTimer::singleShot(N, this, ...)` calls live at the
   feature's own callsite — every singleShot must carry a "why" comment
   per `.scripts/check-singleshot-comments.py`. The timers partial is
   for *named* debouncers (GridWidthDebouncer wiring) and a handful of
   startup-deferred calls that don't have a natural home elsewhere.

5. **Prefer slimming over splitting.** If a chunk of work is big
   enough that it doesn't fit any of the eight above, that's a sign it
   should become its own controller in `src/core/` (no Manager suffix —
   Manager is reserved for the ApplicationManager-owned graph) or a new
   UI controller in `src/ui/controllers/`. The MainWindow split is
   intentionally finite; the next decomposition should slim a partial,
   not grow a ninth.

## Controller location convention

Existing controllers split across two paths, which is a historical
artifact rather than a rule:

| Path | Lives here when | Examples |
|---|---|---|
| `src/core/` | The controller is essentially "MainWindow without the QWidget" — it owns lifecycle / wiring logic and reads MainWindow state directly (or borrows it through a closure context). | `ToolbarController`, `DialogController`, `MenuController`, `ScrollEventsController`, `DbEventsController`, `ScraperController`, `DatAuditController`, `LibraryToolsController` |
| `src/ui/controllers/<name>/` | The controller owns or drives a specific UI surface and has no MainWindow-knowledge — only manager refs and Qt widgets. | `DetailsPaneManager`, `MetadataSidebar` controllers |

New controllers default to `src/core/` if they need to call
`mainWindow->...`. If you can express the controller's job purely in
terms of manager-pointers and Qt widgets, put it under
`src/ui/controllers/` so the layering check (`.scripts/check-layering.py`)
can enforce that it never reaches back up.

## In-flight migrations

- **External `MainWindow::getXxxManager()` callers** are being moved to
  `mainWindow->getApplicationManager()->getXxxManager()` or, preferably,
  to controllers that accept an `ApplicationManager*` in their setup.
  Internal callers inside `mainwindow*.cpp` retain the convenience
  `getXxxMgr()` helpers until the removal lands.

- **`mainwindow.cpp` slimming**: `refreshTitleCounts` and the few
  remaining handler bodies at the bottom of the file are the next
  extraction candidates. The target is a 600-line core TU; current is
  865.

## See also

- [Architecture](architecture.md) — module layout, manager graph,
  signal flow
- [.github/copilot-instructions.md](../../.github/copilot-instructions.md) —
  the manager-access rule and the `getXxxManager` deprecation
- [CONTRIBUTING.md](../../CONTRIBUTING.md) — submission protocol
