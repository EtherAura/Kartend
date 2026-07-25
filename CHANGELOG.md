# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **DAT Manager — the audit status filter can now be translated.** The ten
  entries in the Audit page's status-filter dropdown ("All", "Files I own",
  "Catalogue completeness", and the individual statuses) were marked for
  translation in a way the string extractor rejected, so they never reached
  the translator seed and stayed English in every locale. They are now
  extracted under the same context the UI looks them up in.

## [0.0.17] - 2026-07-19

### Fixed

- **Windows packaging:** the Windows `.zip` and installer failed to start
  ("archive.dll was not found") ever since the archive engine became a hard
  dependency in 0.0.14 — the packaging step's hand-maintained DLL list was
  never updated. Runtime DLLs are now staged from the executable's actual
  import table, so the package stays complete by construction. Linux
  packages were unaffected.

## [0.0.16] - 2026-07-18

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

## Older releases

Releases 0.0.1 through 0.0.14 are archived in
[docs/changelogs/v0.0.x.md](docs/changelogs/v0.0.x.md).

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.17...HEAD
[0.0.17]: https://github.com/EtherAura/Kartend/compare/v0.0.16...v0.0.17
[0.0.16]: https://github.com/EtherAura/Kartend/compare/v0.0.15...v0.0.16
[0.0.15]: https://github.com/EtherAura/Kartend/compare/v0.0.14...v0.0.15
