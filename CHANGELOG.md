# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
