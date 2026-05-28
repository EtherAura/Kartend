# Settings hot-reload signal coverage

Kartend's `SettingsManager` exposes a set of "hot reload" signals that fire
after a successful save so listeners can update their cached state without
restarting the app. This doc maps each signal to its **emit site**, **expected
receivers**, **wiring status** as of this audit, and the **integration test**
that exercises it. Drives the contract on Kartend-xsyt.

## Audit scope

Every `Q_SIGNAL` declared on `ISettingsManager` (src/api/isettingsmanager.h) is
covered below. The two emit sites are:

- `SettingsManager::saveCollections(...)` — per-collection signals + the coarse
  `collectionsModified()` lifecycle signal.
- `SettingsManager::saveGeneralSettings(...)` — `scraperOptionsChanged` (the
  pilot domain signal for global settings).

## Signal matrix

| Signal | Emit site | Should fire when … | Expected receivers | Currently connected | Tests |
|---|---|---|---|---|---|
| `collectionsModified()` | end of every `saveCollections` | always (coarse lifecycle) | `MainWindow::rebuildHierarchyCache`, `DetailsPaneManager::refreshCollectionSummary` | ✅ both wired in `mainwindow_wiring.cpp` | `tests/modules/settings/test_perdomainsignals.cpp::collectionsModified_firesOnEverySave` |
| `scraperOptionsChanged(opts)` | end of `saveGeneralSettings` when ScraperOptions diffs | only on real change (operator!= guard) | ScreenScraperProvider re-reads per-fetch (no startup cache to invalidate) | 🟢 covered by per-fetch read; no connect needed today. Re-evaluate if a future runner caches options at construction. | n/a (covered by per-fetch path) |
| `gridLayoutChanged(idx, GridLayoutPreferences)` | per-collection diff in `emitPerCollectionDiffs` | `c.gridLayout != lastSaved.gridLayout` | ScrollManager (relayout grid + restore selection) | ✅ wired to `ScrollManager::onGridLayoutChanged`. Active-collection-guarded. | emission: `TestPerDomainSignals::gridLayoutChange_emitsOnlyGridLayoutSignal`; end-to-end: `TestScrollManager::gridLayoutChanged_*` |
| `sidebarAppearanceChanged(idx, SidebarAppearance)` | per-collection diff | `c.sidebar != lastSaved.sidebar` | DetailsPaneManager (apply colors / mode / size) | ✅ wired to `DetailsPaneManager::onSidebarAppearanceChanged`. Active-collection-guarded; reuses `applySidebarStateForCollection` + `updateSidebarLayout`. | emission: `TestPerDomainSignals::sidebarChange_emitsOnlySidebarSignal` |
| `collectionBackgroundChanged(idx, CollectionBackground)` | per-collection diff | `c.background != lastSaved.background` | NavigationManager → CollectionBackgroundController (wallpaper / vignette / parallax / blur / primary color) | ✅ wired to `NavigationManager::onCollectionBackgroundChanged`. Active-collection-guarded; reuses `applyBackgroundForCollection` + `applyPrimaryColorForCollection`. | emission: `TestPerDomainSignals::multipleDomainChanges_eachEmitsOnce` |
| `listViewOptionsChanged(idx, ListViewOptions)` | per-collection diff | `c.listView != lastSaved.listView` | ScrollManager (forces a list-view repaint with new font / row height / row colors) | ✅ wired to `ScrollManager::onListViewOptionsChanged`. Active-collection-guarded; pushes row colors into `ItemWidget` static palette + `updateVirtualView()` to repaint when active view is List. | emission: `TestPerDomainSignals::listViewChange_emitsOnlyListViewSignal` |
| `archiveOptionsChanged(idx, ArchiveOptions)` | per-collection diff | `c.archive != lastSaved.archive` | LaunchManager reads `ArchiveOptions` from `m_collections` per-launch (no startup cache) | 🟢 covered by per-launch read; no connect needed today. | emission: `TestPerDomainSignals::multipleDomainChanges_eachEmitsOnce` |
| `folderBrowsingOptionsChanged(idx, FolderBrowsingOptions)` | per-collection diff | `c.folderBrowsing != lastSaved.folderBrowsing` | NavigationManager (logs diagnostic; a force-rescan integration is a follow-up) | 🟡 connect wired to `NavigationManager::onFolderBrowsingOptionsChanged` for diagnostic visibility. ScanService's actual scan signature uses the persisted value on the next scan trigger; QueryManager re-reads per-query. Auto-rescan-on-toggle is a follow-up issue. | emission: `TestPerDomainSignals::folderBrowsingChange_emitsOnlyFolderBrowsingSignal` |
| `collectionFilterPreferencesChanged(idx, CollectionFilterPreferences)` | per-collection diff | `c.filter != lastSaved.filter` | TitleFilter (rebuild patterns) | ✅ wired to `NavigationManager::onCollectionFilterPreferencesChanged`. Calls `TitleFilter::rebuildFromCollections`; complements the existing inline rebuild in `saveCollections` for alternate emission paths. | emission: `TestPerDomainSignals::collectionFilterChange_emitsOnlyFilterSignal` |
| `scraperOverridesChanged(idx, ScraperOverrides)` | per-collection diff | `c.scraperOverrides != lastSaved.scraperOverrides` | ScreenScraperProvider re-reads via ctx per-fetch (no startup cache) | 🟢 covered by per-fetch read; no connect needed today. | emission: `TestPerDomainSignals::scraperOverridesChange_emitsOnlyScraperOverridesSignal` |
| `launcherProfileChanged(idx, LauncherProfile)` | per-collection diff | `c.launcher != lastSaved.launcher` | LaunchManager reads `LauncherProfile` from `m_collections` per-launch (no startup cache) | 🟢 covered by per-launch read; no connect needed today. | emission: `TestPerDomainSignals::launcherProfileChange_emitsOnlyLauncherProfileSignal` |

## Suppression-on-no-diff contract

`saveCollections` builds a `m_lastSavedCollections` snapshot at the end of every
successful write. The next save's `emitPerCollectionDiffs` walk compares the
new `collections` against that snapshot **by `(name, mediaDirectory)` UUID** —
not by index — so reordering an unchanged collection does **not** fire the
per-cluster signals. New collections (no match in `oldByUuid`) skip the diff
entirely; `collectionsModified()` covers the add/remove lifecycle.

This is why the integration test asserts both the positive case
(modify cluster → signal fires) **and** the negative case (modify an unrelated
field → signal does not fire) — silent over-firing would defeat the design
intent of the per-cluster split.

## Coverage status (Kartend-2hzy)

- **6 of 10 per-cluster + general signals fully wired**: grid layout,
  sidebar appearance, collection background, list view options, collection
  filter preferences, folder browsing (diagnostic-only — see below).
- **4 covered by per-call reads**: scraper options, archive options, scraper
  overrides, launcher profile. The respective receivers consult
  `m_collections` / `ctx->generalSettings()` on every launch / fetch, so a
  persisted diff is already visible to the next operation without a signal.
  No connect needed today; revisit if a future refactor caches the values
  at object-construction time.
- **Follow-up**: folderBrowsing wiring is currently log-only — actually
  triggering a `ScanService::forceRescan` when `includeContentSubfolders`
  flips is filed as a child of Kartend-2hzy.

## How to add a new hot-reload signal

1. Declare on `ISettingsManager` (src/api/isettingsmanager.h) — keep the
   `(int collectionIndex, const Cluster &cluster)` shape so consumers don't
   need to re-read from the collections list.
2. Emit from `SettingsManager::emitPerCollectionDiffs` in
   `settingsmanagercollections.cpp`, gated on `oldCfg.X != newCfg.X`.
3. Add a row to the matrix above (Currently connected = ❌ if no receiver
   exists yet).
4. Add an integration test in `tests/modules/settings/test_hotreloadsignals.cpp`
   covering: fires-on-diff, suppressed-on-no-diff, suppressed-on-reorder.
5. File a follow-up bd issue for the missing receiver wiring, labelled
   `hot-reload-wiring`.
