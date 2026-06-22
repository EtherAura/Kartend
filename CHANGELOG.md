# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **DAT audit:** when one file's content is listed by more than one catalogue
  entry (e.g. a non-merged clone set that re-lists a shared entry), the audit
  no longer reports the duplicate entry as Missing while the file is present.

## [0.0.14] - 2026-06-20

### Added

- **DAT Manager.** A new tool for auditing your collections against
  Logiqx / clrmamepro DAT files. Associate a DAT with a collection (or a
  linked launcher profile) and Kartend reports which items are present,
  wrong, or missing — reading inside `.zip` / `.7z` archives so compressed
  sets audit correctly whatever their internal layout. DATs can be
  downloaded, watched for updates, and re-downloaded in one click, and
  `.zip`-packed DAT files are read transparently.
- **Audit browser.** A dedicated browser for audit results: a collapsible
  tree with two detail panes, status filters (Complete / Partial / Empty /
  Fixes / MIA), an optional folder-as-item rollup view, optional grouping
  by category, and named view presets. Splitter sizes, column widths,
  filter states, and expanded rows persist across sessions, and a ZipIndex
  column shows each member's position within its archive.
- **Clone-aware auditing.** Sets are audited under a configurable merge
  mode (Split / Merged / Non-merged): clones resolve through the full
  parent chain, a parent that already contains its clones is reported
  once, and set membership is scoped per folder.
- **One-click Fix.** Applicable fixes — repacking a set into a single,
  correctly-named archive and renaming inner entries — can be applied
  straight from the browser, with inline progress while the re-audit runs.
  Files whose contents don't match (not just unrecognised files) are moved
  to a global or per-collection quarantine folder.

### Changed

- The audit scan folder can be overridden on linked launcher profiles, and
  the linked-profile DAT hint is now actionable.

### Fixed

- **Dialogs:** the mouse wheel now scrolls the focused dialog instead of
  the content behind it.
- **Theming:** changing the system accent colour at runtime live-updates
  every colour in the app instead of needing a restart.
- **Stability:** fixed data-scan completion races flagged by the thread
  sanitizer; the audit browser no longer crashes on first open.

## [0.0.13] - 2026-06-11

### Added

- **Plaintext-credential warning banner.** When a transient keychain
  failure forces the scraper password into unencrypted INI storage, both
  scraper credential panels now show a persistent, non-modal warning with
  the failure reason. The banner clears automatically once a later
  keychain write succeeds and the secret moves back (which already
  happened silently on the next settings save).
- **Cancellable, off-thread archive extraction at launch.** Extracting an
  archive-backed item no longer freezes the UI (previously up to 30
  seconds); a busy overlay shows progress and the extraction can be
  cancelled. Extraction is also bounded at 4 GiB decompressed to stop
  zip-bomb / tmpfs-exhaustion archives.
- **Full-text-search index self-healing.** Damaged or partially built
  search indexes (from older versions) are detected and rebuilt
  automatically on launch.

### Changed

- Collection deletion/purge now also removes the collection's scraped
  metadata, artwork overrides, launch history, and playlist entries
  instead of stranding them in the database forever.
- The artwork timestamp cache moved from a single multi-megabyte JSON
  file (fully rewritten on every save) to an SQLite store with
  incremental writes and automatic pruning — faster startup and far less
  disk churn during background artwork loading.
- Emptying a collection's media directory now correctly removes its
  items on the next rescan (previously ghost entries persisted and the
  directory was re-scanned on every load); a temporarily unmounted
  directory still never triggers removal.

### Fixed

- **Scanning:** cancelling a collection scan no longer blocks that
  collection from being rescanned until the app is restarted; and a scan
  whose final apply loses a database-lock race is now retried instead of
  being silently dropped until the next full pass.
- **Search:** results matching literal `_` or `%` characters no longer
  over-match (an underscore in a subfolder name matched sibling folders);
  clearing a search quickly no longer snaps the view back to the
  abandoned query's results.
- **Gamepad (SDL2):** a controller that disconnects or sleeps mid-session
  is detected and re-attached on reconnect — input no longer stays dead
  until restart.
- **Launching:** rapid double-presses (bouncy Enter, gamepad chatter) can
  no longer spawn two child processes — the launch debounce now covers
  every launch surface, not just mouse double-click; pressing Enter with
  nothing selected no longer launches the first item; play counts are
  recorded only when the child process actually starts.
- **Usage stats:** launch/play-session writes are no longer silently lost
  when quitting right after a launch, when a background scan holds the
  database write lock, or before the first query of a session; stats
  shown right after launching an item no longer stay stale for the
  session.
- **Scanning:** a database hiccup mid-scan can no longer silently delete
  items that exist on disk; cancelling a scan no longer wedges the scan
  worker (intermittent shutdown aborts); scan results survive write-lock
  contention with bounded retries; extension-filtered collections scan
  correctly again after settings round-trips; file sizes and added-dates
  are now recorded by every scan path (Size sort and "recently added"
  smart playlists).
- **Stability:** several shutdown-window use-after-free races fixed
  (cache teardown ordering, superseded artwork catalog builds, event
  filters during teardown, worker-thread pointer races); cover flow no
  longer rebuilds every card on each data chunk while scrolling large
  collections; per-tile artwork lookups reuse the warm directory cache
  instead of re-probing the filesystem during scroll.

### Security

- All third-party GitHub Actions are pinned to commit SHAs and release
  workflow permissions are scoped to the publishing jobs only; `main` is
  now protected by required CI checks. Windows release dependencies are
  pinned to a vcpkg baseline for reproducible builds.

### Removed

### Deprecated

## [0.0.12] - 2026-05-30

### Added

- **Per-collection hot-reload signals are now wired to real receivers.**
  Six per-cluster `*Changed` signals (grid layout, sidebar appearance,
  collection background, list-view options, collection-filter
  preferences, folder-browsing options) now drive live updates on the
  active collection from alternate save paths (kart import, right-click
  edits, toolbar inline edits) that previously only the settings-dialog
  apply flow covered. Four remaining signals (archive, launcher,
  scraper-overrides, scraper-options) are documented as covered by
  per-call reads in `docs/settings-hotreload.md`.
- **Persistent launcher-path warnings on imported collections.** When a
  `.kart` bundle from another machine has launcher paths that don't
  resolve on the importing host, the import flow now (1) logs each
  unresolvable path with the human-readable status, (2) shows a
  one-shot informational dialog at finalize time listing every bad
  path with selectable text, and (3) surfaces a persistent
  "⚠ Launcher path" row per failing entry in the sidebar info panel
  for the active collection. The rows recompute on every refresh so
  they clear the moment the user fixes the underlying path.
- **Toolbar launcher-warning badge + launch gate.** The items toolbar
  now shows a warning glyph next to the active collection's title
  whenever that collection has an unresolvable launcher path; its
  tooltip lists each bad path. Attempting to launch an item from such
  a collection surfaces an explanatory "Launcher unavailable" dialog
  listing the issues instead of failing silently a moment later.
  Clicking the badge opens the settings dialog directly on the
  Launchers page. The badge recomputes on every collection switch and
  after each settings save, so it clears as soon as the path is fixed.
- **Settings-dialog warning glyph for path fields that don't resolve.**
  The launcher / artwork-directory / placeholder-artwork / additional-
  launcher / core path fields in the settings dialog now show a small
  trailing warning icon + tooltip whenever the entered path fails the
  PathStatus probe (missing file, not executable, wrong type, etc.).
  Empty paths and OK paths are silent — the glyph is purely
  informational and never blocks save.
- **Cross-machine kart import scraper-region detection.** Imported
  manifests with unknown enum values (alignment, view type, sidebar
  background type, sidebar active tab, header logo position) now log a
  named warning before falling back to the default, matching the INI
  side's behaviour. Catches typos in hand-edited `.kart` manifests
  before they silently change a collection's look.

### Changed

- **Settings dialog modeling files now decompose along clear seams.**
  `settingsmanagercollections.cpp` shrank from 958 → 406 LOC by moving
  each `CollectionConfig` sub-cluster's INI load/save into its own
  `*_persistence.{h,cpp}` pair alongside the struct definition.
  `settingsdialogtree.cpp` shrank from 795 → 70 LOC across three
  sibling TUs (`settingsdialogtreesync.cpp`,
  `settingsdialogtreemutation.cpp`, `settingsdialogtreedragdrop.cpp`).
  No user-facing behaviour change; the INI + kart-manifest round-trip
  is identical.
- **Unified failed-test diagnostics across Linux + Windows CI.**
  Replaces the Windows-only PowerShell fallback that re-ran failing
  test binaries with `.scripts/print-failed-tests.py`, a portable
  helper invoked on all five ctest sites. Failure logs now have
  identical shape on every OS / sanitizer config and bypass the
  broken-on-Windows ctest stdout pipe via QTest's `-o file,txt`.

### Fixed

- **Stale `mainwindow_dbevents.cpp` references in `mainwindow_wiring.cpp`
  comments.** The file was renamed to `dbeventscontroller.cpp` in an
  earlier refactor; the residual five doc-block mentions are now
  updated. Pure-comment change — no behavioural effect.

- **Settings dialog Cancel button now reverts live-saved panels.** The
  base color, fonts, and splash panels apply changes immediately for
  instant preview; Cancel previously left those edits committed.
  Cancel now restores the last-saved baseline on those panels while
  preserving the instant-feedback behaviour while the dialog is open.

- **Scrape result dialog no longer crashes on dedup.** The dedup loop
  used to call `finishCurrentApply()` mid-iteration on a dialog that
  may have already been accepted/destroyed; it now tallies hits and
  finishes once after the loop completes.

- **Bounded correctness / lifecycle pass across batch, artwork,
  playlist, settings I/O, and the scroll/navigation pipeline.** Closes
  sixteen latent bugs that were individually subtle but collectively
  significant: silent `SQLITE_BUSY` edit failures on the playlist DB
  (per-connection `busy_timeout` now configured), artwork PNG cache
  writes are atomic via `QSaveFile` + `fsync`, settings export/import
  is atomic with round-trip test coverage, `PlaylistManager::addItem`'s
  read-modify-write wrapped in a transaction, parentless
  `MarqueeWindow` deleted on controller destruction, lock acquired
  before `AdaptiveBatcher`'s early-return state read, `QPointer` (not
  raw `this`) captured in `ArtworkManager`'s re-dispatch, the
  `hideMissingArtwork` field included in `CollectionConfig` equality,
  null-guarded `ArtworkManager` dereferences in navigation, recycled
  virtual-folder widgets no longer accumulate duplicate double-click
  connections, wheel-vertical scroll animation re-fires the canonical
  completion slot, `navigateToItem`'s Connection handle owned via
  `shared_ptr` (no leak on never-matched results), `m_appManager`
  declared before its dependents so it outlives them at teardown,
  `ConfigValidation` expands `~/%collection%` via the same `PathUtils`
  helper as runtime (no more spurious "media directory does not
  exist" warnings on `%collection%` collections), artwork
  suppressed-result requeue funneled through `ArtworkWidgetRegistry`'s
  coalesce + max-pending cap, and empty-chunk slots recover after a
  bounded number of re-requests instead of sticking on "Loading…"
  forever.

### Security

- **Path traversal blocked across configuration and CLI seams.** The
  `%collection%` substitution in `PathUtils::expandPath` validates the
  collection name and refuses to expand traversal-unsafe values (the
  placeholder stays literal and a warning is emitted), so a malicious
  collection name can no longer escape the configured root. The
  generic `PathUtils::validatePathSecurity` check now rejects any
  `..` path segment — closing a CLI seam where
  `--import-kart ../../etc/foo` slipped through (a literal `..`
  inside a single filename stays allowed). And the Create Collection
  dialog security-validates its folder, launcher, and core path
  fields at acceptance, mirroring the settings dialog so a bad path
  is rejected at creation time rather than only on a later re-save.

- **Scraper providers hardened against untrusted remote data.** TMDB
  bearer token and ScreenScraper passwords now travel in request
  headers instead of query strings (no longer surfaced in logs or
  referer leaks). Provider IDs are validated before being
  interpolated into request URL paths — MBID must match the UUID
  format, OpenLibrary work-keys must match `^OL[0-9]+W$`, TMDB
  media-type and id are re-checked at the `fetchDetail` seam.
  ScreenScraper assets whose remote `type` isn't a safe path
  component are dropped (with a defense-in-depth re-check at the
  on-disk write seam, since `type` becomes a directory name). And
  ScreenScraper group/company scope keys are allowlist-validated at
  parse time (downgrading to per-game scope when invalid), preventing
  a malicious scope key from traversing out of `_shared/` as a
  filename.

### Removed

### Deprecated

## Older releases

Releases 0.0.1 through 0.0.11 are archived in
[docs/changelogs/v0.0.x.md](docs/changelogs/v0.0.x.md).

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.14...HEAD
[0.0.14]: https://github.com/EtherAura/Kartend/compare/v0.0.13...v0.0.14
[0.0.13]: https://github.com/EtherAura/Kartend/compare/v0.0.12...v0.0.13
[0.0.12]: https://github.com/EtherAura/Kartend/compare/v0.0.11...v0.0.12
