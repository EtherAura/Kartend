# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.0.6] - 2026-05-11

### Added

- Helper-namespace extractions for five manager classes so the pure
  decision logic is unit-testable without their QWidget / ApplicationContext
  graphs:
  - `DetailPagePayloadBuilder` (display title fallback, artwork-row override
    map + custom-type filter, file-info field assembly)
  - `SelectionRestoreHelpers` (search-active predicate, restore-decision
    AND-chain, total/selIdx clamp, collection+token validity check)
  - `AttractHelpers` (linear + direction-biased random advance-index,
    sub-pixel scroll accumulator, boundary-aware scroll position)
  - `OverlayHelpers` (distance-to-duration glide policy with min/max clamp,
    visibility-aware animation start rect, item rect inset geometry)
  - `GamepadHelpers` (axis-to-direction with on/off-threshold hysteresis,
    d-pad + stick vertical-priority combine, SDL Sint16 normalize,
    case-insensitive confirm/back/toggle button resolution)
- `+102` new unit tests covering the extracted helpers (every helper has
  edge-case coverage for empty inputs, hysteresis hold/release, the
  bounce-fallback at boundaries, defensive clamps, and priority ladders)
- `KARTEND_PORTABLE_RELEASE` CMake option for distro packaging
  (drops `-march=native`/`-O3`/fast-math, keeps LTO + hardening)
- `KARTEND_LINKER_MAP` CMake option (replaces fragile `_MAP_DIR`
  filesystem walk; build script flips it on for debug builds)
- `KARTEND_ENABLE_QT_GAMEPAD` / `KARTEND_ENABLE_SDL2_GAMEPAD` /
  `KARTEND_ENABLE_ZSTD` toggles so distro USE flags actually
  control auto-detection
- CMake `uninstall` target (reads `install_manifest.txt`); replaces
  the readme's shell-loop instructions
- `build.sh` flags: `--prefix=PATH`, `--jobs=N`, `--uninstall`,
  `--clang`
- CPack integration (`cpack -G TGZ` for source/binary tarballs)
- Tag-triggered GitHub Release workflow with sha256 publishing
- `.github/dependabot.yml` for weekly GitHub Actions bumps
- `.editorconfig` for cross-editor indent/EOL/whitespace
- PKGBUILD `check()` running ctest at package time

### Changed

- All project CMake options namespaced to `KARTEND_*`
  (`MAINTENANCE` → `KARTEND_MAINTENANCE`, `BUILD_TESTS` →
  `KARTEND_BUILD_TESTS`, `ENABLE_*`/`USE_PGO`/`PGO_*` likewise)
- Release flags split into always-on hardening
  (`-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `-fPIE`,
  `-Wl,-z,relro`, `-Wl,-z,now`) and Release-only performance flags
- Pinned minimum Qt version to 6.4 in `find_package`
- PKGBUILD: switched to tarball source; added `qt6-gamepad` dep
- Flatpak: added `--socket=pulseaudio` for video preview audio;
  switched to Release + `KARTEND_PORTABLE_RELEASE=ON`
- ebuild: `gamepad`/`sdl`/`zstd` USE flags now drive the matching
  CMake toggles (previously only affected RDEPEND)
- CI ccache key uses `github.run_id` instead of source-file hash
  (was rotating the cache on every change)
- `build.sh`: unknown flags now exit with code 2 instead of being
  silently ignored

### Fixed

- `PlaylistManager::exportToJson` / `exportToM3U` now use `QSaveFile`
  (temp file + atomic rename) plus `PathUtils::syncDirectory` for parent-dir
  durability — a crash or power loss mid-export no longer leaves a
  half-written `.json` / `.m3u` clobbering a previously good file at the
  user's chosen path
- `QueryManager::needsRescan` (three sites that previously used `(void)exec()`
  or bare `exec()` calls) now logs an `ErrorContext` warning with
  `lastError().text()` when the SQL fails, so a locked DB or constraint
  failure no longer desyncs scan metadata from disk in silence
- `DbMigrations::setUserVersion` now checks the `PRAGMA user_version` exec
  result and logs on failure; previously a failed PRAGMA after a successful
  migration body left the version stale and re-ran the migration on next
  launch with no log trail
- `SessionManager` now logs a `qCWarning` when the metadata file exists on
  disk but `open(ReadOnly)` fails (permissions, FS error) — previously
  silently fell back to default empty state with no signal
- Re-enabled `-Wunused-parameter` and `-Wunused-lambda-capture` in the
  base lib's `target_compile_options` and cleared the fallout: removed
  stale `this` captures in three lambdas (launch / kart / querymanager
  reconnect logging), removed a const-int capture clang correctly treats
  as constant-expression, and `Q_UNUSED` for four genuinely-dead
  parameters that survived prior refactors
- PGO instrumentation never reached the compiler — `list(APPEND
  RELEASE_COMPILE_OPTS ...)` ran after `target_compile_options()`
  had already substituted the variable text
- `option(PGO_PROFILE_DIR "..." "${path}")` was being coerced to a
  boolean; replaced with `set(... CACHE PATH ...)`
- Duplicate `-Wno-unused-lambda-capture` in compile flags
- Three identical `RELEASE_COMPILE_OPTS` branches collapsed to one
- `ENABLE_SANITIZERS=ON` with non-Debug config used to silently
  produce no instrumentation; now errors at configure time
- `docs/building.md` first line was a corrupt paste of an emerge
  command concatenated to the heading
- readme stale `--no-archive`/`--no-reports`/`--fast` references
- readme wrong icon install path (`pixmaps/` → `icons/hicolor/...`)
- Missing `Multimedia`/`MultimediaWidgets` from readme + CONTRIBUTING
  Qt component lists
- Metainfo XML missing 0.0.5 release entry
- ebuild duplicate `dobin`/`domenu` calls (CMake `install()` already
  handled them)
- CI workflow + PKGBUILD + ebuild missing `qt6-multimedia` runtime
  dep that v0.0.5's video preview / overlays / startup-video
  features require — every CI matrix job and downstream package
  failed configure with `Failed to find required Qt component
  "Multimedia"`
- `launchmanager.h` forward-declared `QProcess` for a
  `QPointer<QProcess>` member, which broke `mocs_compilation` on
  Ubuntu 24.04 / Qt 6.4 (older `qpointer.h` casts through the
  complete type); replaced with a real `#include <QProcess>`
- Replaced `QIcon::ThemeIcon` enum (Qt 6.7+) with XDG icon-name
  string literals across dialogs and `settingsdialog.ui` so
  builds work on Qt 6.4
- `usagestatsstore.cpp` used `QTimeZone::UTC` (Qt 6.5+); replaced
  with the equivalent `QTimeZone::utc()` accessor (also valid on
  later Qt) for Qt 6.4 compatibility
- `tests/test_kartreader.cpp` ASan global-buffer-overflow: the
  `QByteArray` ctor was told to read 28 bytes from a 26-byte
  literal; corrected to the real content length
- Added LSan suppressions for known-leak third-party paths in
  GStreamer / `QPlatformMediaIntegration::instance` so sanitizer
  CI runs aren't drowned in non-actionable noise

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

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.5...HEAD
[0.0.5]: https://github.com/EtherAura/Kartend/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/EtherAura/Kartend/compare/v0.0.3...v0.0.4
[0.0.3]: https://github.com/EtherAura/Kartend/compare/v0.0.2...v0.0.3
[0.0.2]: https://github.com/EtherAura/Kartend/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/EtherAura/Kartend/releases/tag/v0.0.1
