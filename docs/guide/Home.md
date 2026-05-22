# Kartend Wiki

The user guide for [Kartend](../../readme.md) — a Qt6 / KDE frontend for
organizing and launching multimedia collections (films, music,
audiobooks, reference materials, anything you can hand to a launcher).

If you just want to get something on screen, jump straight to
[Getting Started](Getting-Started.md). If you've already built a library
and want to dig into a feature, the index below is grouped by purpose.

## Index

### Start here

- **[Getting Started](Getting-Started.md)** — first launch, your first
  collection, browsing and launching items
- **[Collections](Collections.md)** — naming, hierarchies, parents and
  alias parents, type metadata, reparenting, duplication, deletion
- **[Shell Collections](Shell-Collections.md)** — collections without
  media of their own, used to group other collections into top-level
  categories like `Video` → Films / TV Shows / Documentaries

### Browsing & navigating

- **[View Modes](View-Modes.md)** — Grid, List, Cover Flow, Horizontal —
  what each one does and the settings that affect it
- **[Search, Sort & Filter](Search-Sort-Filter.md)** — search bar,
  search modes, sort menu, type filter, regex title cleanup, hide-missing
- **[Input & Controls](Input-and-Controls.md)** — keyboard, mouse, gamepad;
  full shortcut reference; rebinding

### Items & launching

- **[Launchers](Launchers.md)** — primary launcher, additional launchers,
  reusable presets, RetroArch / libretro, archive extraction, per-item
  override
- **[Artwork](Artwork.md)** — auto-discovery, custom artwork types,
  manual links, sidebar gallery, artwork cycling, placeholder tiles,
  subfolder generator
- **[Video Previews](Video-Previews.md)** — sidebar video, video-first
  Cover Flow, supported formats, volume
- **[Item Metadata](Item-Metadata.md)** — custom user-defined fields,
  manual file links (PDFs, manuals), per-item launcher override,
  detail page

### Organizing your library

- **[Playlists & Favorites](Playlists-and-Favorites.md)** — built-in
  Favorites, custom playlists, M3U / JSON import-export
- **[History & Statistics](History-and-Statistics.md)** — launch history,
  per-item play counts, Statistics dialog, runtime detection

### Look & feel

- **[Themes & Appearance](Themes-and-Appearance.md)** — backgrounds
  (color / image / video), vignette, parallax, backdrop blur, fonts,
  text zoom, title tints
- **[Sidebar & Details Pane](Sidebar-and-Details-Pane.md)** — Item /
  Collection / File tabs, Overlay vs Expand, position, styling
- **[Splash Screens & Now Playing](Splash-and-Now-Playing.md)** — startup
  video, boot splash, resume-focus splash, the Now Playing overlay

### Special modes

- **[Attract Mode](Attract-Mode.md)** — idle screensaver-style behavior:
  autoscroll, advance selection, suspension during launch
- **[Backup & Sharing](Backup-and-Sharing.md)** — `.kart` package format,
  export, import, conflict policies, headless command-line workflow

### Reference

- **[Scraper Credentials & Keychain](Keychain.md)** — how Kartend stores
  scraper passwords/API keys in the OS keychain, what the `@keychain`
  sentinel in `settings.ini` means, how to install the dependency on each
  platform, plaintext fallback
- **[Settings Dialog](Settings-Dialog.md)** — anatomy of the dialog,
  scope selector, apply-to-selected, propagation rules, every tab
- **[Toolbar & Menus](Toolbar-and-Menus.md)** — items-page toolbar,
  hamburger fallback, full menu reference, customization
- **[Configuration Reference](Configuration-Reference.md)** — every INI
  key, type, default, description (per-collection and `[General]`)
- **[CLI Reference](CLI-Reference.md)** — all command-line flags
- **[File Locations](File-Locations.md)** — config, database, cache
- **[Logging & Diagnostics](Logging-and-Diagnostics.md)** — environment
  variables, `kartend.*` logging categories, useful invocations
- **[Troubleshooting](Troubleshooting.md)** — fixes for common issues

## Building, contributing, internals

The wiki is intentionally end-user-leaning. Each page has a short
*"For developers"* section at the bottom where it makes sense.
Standalone developer documentation lives one level up:

- **[Architecture](../architecture.md)** — module layout, manager
  ownership, threading model, signal flow
- **[Building](../building.md)** — dependencies, build script,
  CMake options
- **[Testing](../testing.md)** — unit and integration tests
- **[Constants](../constants.md)** — `UIConstants` reference
- **[Seed data](../seed-data.md)** — generating test libraries
- **[Subfolder artwork generator](../subfolder-artwork.md)** — placeholder
  composites for virtual folders

## Conventions used in this wiki

- **Bold** for UI labels (menus, buttons, dialog field names).
- `monospace` for INI keys, paths, commands, and code identifiers.
- Tables list defaults next to keys whenever a default exists.
- *"Where to find this"* boxes point you at the Settings tab and the
  matching INI key for any feature, so you can use either.
- Cross-references are inline links — no auto-generated TOC.

## Project links

- [Source repository](https://github.com/EtherAura/Kartend)
- [Issue tracker](https://github.com/EtherAura/Kartend/issues)
- [Changelog](../../CHANGELOG.md)
- [License (GPL-3.0-only)](../../LICENSE)
