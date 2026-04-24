# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.3] - 2026-04-24

### Added

- DatabaseManager lifecycle and path-resolution unit tests

### Changed

- Settings dialog UI polish (approved compact-layout tweaks)
- `build.sh`: `.backups` archive and reports are now opt-in; dropped
  redundant `--no-archive` / `--no-reports` / `--fast` flags
- Documentation sync: architecture, testing, and copilot instructions
  updated to match current module layout
- Readme build-script section expanded with a workflow table
- Ebuild `HOMEPAGE`/`SRC_URI` pointed at `EtherAura/Kartend`
- `SearchManager::currentMode` marked `[[nodiscard]]`; implicit-bool
  nullptr checks and `qCWarning` adoption in `configvalidation`

### Fixed

- Crash on shutdown: widget pool now uses `QPointer` for raw captures
  and `deleteLater` so queued callbacks cannot outlive their targets
- Scroll/artwork: synchronous artwork lookup fallback for list-mode
  rows and correct `hasArtwork` flag update from the prewarm
  reconfigure path

## [0.0.2] - 2026-04-21

### Added

- Unit tests for StringUtils and CollectionUtils helpers
- Prereleases included in the readme Release badge

### Changed

- Large LOC-reduction refactor: split `ScrollManager`, `QueryManager`,
  `SettingsManager`, `NavigationManager`, `InteractionManager`,
  `ViewportManager`, `ItemWidget`, `MainWindow`, and `SettingsDialog`
  into focused sibling translation units
- Extracted `DataSourceManager` and `SelectionDisplayManager` from
  `ScrollManager`
- `build.sh` now invokes clang-format with `--style=file` so the
  project `.clang-format` is honored

### Fixed

- UBSan vptr violation on `EventManager` during shutdown:
  `~InteractionManager` now detaches the qApp event filter before
  owned sub-managers are destroyed
- Missing `<set>` include in `settingsdialogtree.cpp` that broke the
  build on Ubuntu 24.04 libstdc++
- CI workflow YAML: removed orphan trailing `run:` line that caused
  GitHub Actions to schedule zero jobs
- Core-dump gitignore rules anchored to the repo root
- License section formatting in the readme

## [0.0.1] - 2026-04-18

### Added

- Virtual scrolling with widget pooling for large collections
- Async artwork loading with QtConcurrent and in-memory cache with disk persistence
- Collection hierarchy with parent-child relationships and subcollection navigation
- Session persistence for selection state and scroll position
- Configurable launchers (emulators, RetroArch cores, native applications)
- Full keyboard navigation (arrow keys, alphabetic jumping, search)
- Glide scroll animations with configurable easing
- Grid and list view modes with sortable columns
- Metadata sidebar with artwork display
- Settings dialog for collection configuration
- SQLite database with FTS5 search and worker thread queries
- Gamepad support (Qt6 Gamepad / SDL2 fallback)
- Gentoo ebuild packaging
- CI with build, test, sanitizer, and maintenance checks
