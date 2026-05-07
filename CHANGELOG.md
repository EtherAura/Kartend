# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.5] - 2026-05-06

### Added

- Sorting by date and file size
- Optional, configurable splash screens (startup + focus-return)
  with per-collection placeholder artwork and title overlays
- Per-collection expand mode with two-stage activation
- Per-collection 'hide items without artwork' toggle
- Per-collection regex title-exclusion filter
- Per-collection multi-launcher with chooser dialog, per-item
  launcher overrides, and global launcher presets
- Per-collection appearance settings, sidebar tabs, sidebar
  promoted to a top-level Settings tab, and per-collection
  sidebar font override
- Settings: scope selector with gating for non-propagatable
  fields, duplicate-collection + apply-to-selected picker,
  collection-tree context menu with drag-drop reparenting,
  alias-parent links with linked-appearance rendering,
  collection type metadata + toolbar filter + hide-subs,
  config import/export via Backup section
- Playlists: data model + virtual collection foundation,
  built-in Favorites playlist, M3U + JSON import/export,
  documented playlist→source launcher chooser contract
- Chronological launch history log
- Per-item usage tracking and Statistics dialog
- Item metadata: extended fields with sidebar Details section,
  user-defined custom fields, per-item manual links
- Artwork: item_artwork storage with standard-type
  auto-discovery, custom artwork types, per-item manual-link
  editor, sidebar artwork gallery, cycle item artwork type
  with modifier+middle-click
- Video: per-item video previews in sidebar, video-first
  overlay with middle-click peek, gallery video tile,
  unmuted sidebar/overlay audio
- Optional runtime detection with Now Playing overlay
- Coverflow 3D carousel view mode with gallery toolbar,
  horizontal scrolling view mode
- Customizable items-page toolbar (kde-breeze icons, view
  dropdown, in-field search, consolidated filter)
- Hamburger fallback when the menu bar is hidden;
  randomize / recent / layout menu entries
- Sidebar: collection summary when no item is selected,
  upper width limit removed
- Typography: global UI font family + size, runtime text
  zoom (Ctrl+/-/0)
- Attract mode: advance-selection + sub-pixel scroll
- Contextual empty-state widget
- Details pane (renamed from sidebar): orientation menu and
  view-aware grid sizing
- Packaging: Arch PKGBUILD and Flatpak manifest
- Tests: integration test harness for UI-coordinator managers,
  ApplicationManager + NavigationManager lifecycle coverage,
  and unit tests for new metadata, artwork, history, playlist,
  usage-stats, kart, title-filter, video, and grid-layout modules

### Changed

- Refactor: split `ApplicationContext` into ui/managers/collection
  sub-structs
- Refactor: promoted `scrollmanagervirtual.cpp` into a
  `VirtualScrollEngine` class
- Project-wide clang-format pass

### Fixed

- Allow ampersands in launch paths
- Render video-preview frames via `QVideoSink` instead of
  `QVideoWidget`
- Sidebar: expand video/artwork/manual paths and apply
  owner-aware lookup; center artwork/name region, scale
  previews, wrap long values
- Filter toolbar: always-enabled with explicit 'Filter' label
- Toolbar position label refreshes on wheel scroll
- Treat launch as attract activity to suppress spurious
  attract-mode triggers

## [0.0.4] - 2026-04-25

### Added

- Attract mode settings UI under General → Attract Mode (enable toggle,
  idle timeout, scroll speed)

### Changed

- Attract mode timeout reset behavior now reacts to item selection changes
  instead of generic mouse movement/activity

### Fixed

- Attract idle timer interval synchronization when attract mode is toggled
  from disabled to enabled
- Attract scrolling stop behavior when mode is disabled mid-scroll

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
