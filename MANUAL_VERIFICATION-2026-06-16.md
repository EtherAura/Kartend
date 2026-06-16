# Manual verification — 2026-06-16 (live system-accent propagation)

Runtime-gated checks for Kartend-a0d6c — make **all** application colors update
when the KDE system accent color changes at runtime (e.g. switching Plasma
activities whose Picture-of-the-Day wallpapers yield different accents).

**Status: VERIFIED** on KDE (2026-06-16). All surfaces below update live; a
second consecutive accent change still works; responsiveness is good after the
re-theme/re-render were de-duplicated.

How it works: Qt does **not** dispatch a palette-change event to a running app
for an accent-only change on KDE, so a `SystemThemeWatcher` watches `kdeglobals`
(which KDE rewrites on any accent/scheme change) and, on a debounced change,
`MainWindow::onSystemThemeChanged()` re-broadcasts the (already-fresh)
application palette + re-polishes the global stylesheet + re-applies the derived
theming. The detection half is unit-tested (`SystemThemeWatcher`), as are the
title-tint accent-follow path and placeholder cache invalidation
(`ItemPlaceholderTint`); the re-broadcast itself is GUI-/KDE-gated, hence the
manual checklist below.

## Setup
Run on KDE Plasma with the Qt KDE platform theme active
(`QT_QPA_PLATFORMTHEME=kde`). Default Kartend appearance (colors follow the
current accent: General → appearance `titleBaseColor` empty; per-collection
sidebar text/accent left blank). Trigger a **pure-accent** change without
touching Kartend, either:
- switch to a Plasma **Activity** whose wallpaper yields a different accent, or
- System Settings → Colors → change the accent / "Accent color from wallpaper"
  and roll the Picture-of-the-Day wallpaper.

Each surface below should re-tint **live**, with no navigation, reselection, or
window reopen.

## Surfaces that froze before this fix (verify they now update live)
- **Items-page breadcrumb** title links + subfolder-path links re-tint.
- **Loading overlay**: spinner arc *and* progress-bar fill; **empty/loading
  state** spinner (navigate into an empty/scanning collection to show it).
- **Details pane** (sidebar): the accent-colored metadata key prefixes
  (e.g. "Genre:"), section/header bubbles, and the resize-grip handle.
- **Search bar** placeholder text tint (focus away so the placeholder shows).
- **Coverflow** gallery-strip thumbnail placeholders (the neutral tiles shown
  before real frames decode) and missing-artwork card tiles.
- **Subcollection triangle badge** on folder tiles (no reselect needed).
- **Settings dialog** save-button glow — open Settings, make a change so the
  glow appears, then change the accent: the glow re-tints.
- **Binding Visualizer** (Help → Visualize Bindings): press a key/gamepad
  button to highlight a row, change the accent → the highlighted row re-tints.
- **Item → Artwork Links** dialog: the auto-discovered (italic, muted) hint
  rows re-tint to the new disabled-text color.

## Already-live surfaces (confirm no regression)
- Item-tile **title tint** + **selection border**, list-row backgrounds, splash
  / now-playing glows, list header — these read the palette at paint time and
  already tracked the accent; confirm they still do.

## Custom-color override still wins (confirm not broken)
- Set a per-collection **sidebar accent color** (non-empty) on one collection.
  After a system-accent change, that collection's sidebar accent must stay the
  **custom** color (system change must NOT override an explicit pick).
- Switch from that collection to one with a **blank** accent: the blank one must
  show the **current system** accent (not the previous collection's custom one —
  this also fixes a latent stale-accent-on-switch bug).

## Full scheme flip + stability
- Repeat a **light ↔ dark** switch: the same handler covers it; confirm all of
  the above update and nothing flickers or loops.
- Switch Plasma activities back and forth several times quickly: the re-theme is
  debounced (one pass per change) — confirm no visible thrash and CPU stays flat
  between switches (validates the `singleShot(0)` coalescing).
