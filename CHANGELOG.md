# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Scraper — setup options inside the scrape window.** The scrape window
  gains an options panel for tuning the re-scrape policy, fallback region,
  and media speed/quality preset without a round-trip through Settings →
  Scraper; both places share the same preset logic, and the window gains a
  vertical splitter to fit the controls.
- **Scraper — video and manual media now download.** Media fetches negotiate
  content type and size per media kind, so video previews and manuals are
  retrieved correctly instead of being dropped.

### Changed

- **Scraper — smarter retry policy.** A provider answering 429 with a
  Retry-After header is treated as transient, with a three-strike escalation
  shared by batch, entity, and interactive scrapes; cancelling a scrape is no
  longer misclassified as a retryable failure; and per-media download
  failures are now counted in the completion summary and survive a resumed
  run.
- **Snappier UI under heavy work.** Drag-and-drop kart import and artwork
  preview decoding run on background workers (no more window stalls during
  large imports), filesystem-watcher reconciliation happens off the GUI
  thread, and rapid-fire settings writes (column drag, volume, text zoom)
  are debounced instead of hitting disk per tick.

### Fixed

- **Interactive scrapes skip completed items:** Skip and "download only
  what's missing" scrapes started interactively now pre-filter
  already-complete items the same way automatic mode does, instead of
  prompting for items that need nothing.
- **Settings:** in-progress edits are guarded by an unsaved-edit gate, so
  deselecting a field can no longer silently discard them.
- **Launching & navigation:** gamepad input and attract mode stay suspended
  while a detached launch is active; restoring the navigation stack can no
  longer resurrect stale entries; launch arguments that are empty are quoted
  correctly; and cancelling the chooser twice in quick succession is
  debounced.
- **Legacy INI import:** percent-encoded characters in legacy INI files now
  round-trip correctly, with a `.legacy.bak` backup written before rewrite.
- **Stability:** closed a use-after-free window in the artwork cache's size
  walk, bounded cache teardown so shutdown can't hang on a stuck worker,
  guarded truncated clrmamepro DAT files, and fixed a playlist row-reuse bug
  that could attach entries to the wrong scope.
- **Accessibility:** the cover flow view exposes proper accessible roles and
  names to screen readers.
- **Scraper — "download only what's missing":** now correctly skips items that are
  already complete. Metadata counts as done only when its core fields are filled
  (so partially-filled entries get completed instead of being skipped forever),
  and existing artwork is now recognised regardless of the media folder's letter
  case — previously mixed-case folders (e.g. `box-2D-back`) went unrecognised on
  case-sensitive filesystems, so every item was needlessly re-scraped on each run.
- **Scraper — "download only what's missing" no longer re-chases unavailable art:**
  when you request media types a provider doesn't supply for an item (e.g.
  ScreenScraper has no map/marquee for most PlayStation games), the item is now
  remembered as complete instead of being re-scraped on every run. The provider is
  asked once; if it later starts supplying the type, that's picked up automatically.

## [0.0.15] - 2026-07-04

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

## Older releases

Releases 0.0.1 through 0.0.12 are archived in
[docs/changelogs/v0.0.x.md](docs/changelogs/v0.0.x.md).

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.15...HEAD
[0.0.15]: https://github.com/EtherAura/Kartend/compare/v0.0.14...v0.0.15
[0.0.14]: https://github.com/EtherAura/Kartend/compare/v0.0.13...v0.0.14
[0.0.13]: https://github.com/EtherAura/Kartend/compare/v0.0.12...v0.0.13
