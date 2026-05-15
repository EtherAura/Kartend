# Attract Mode

Attract mode is Kartend's screensaver-meets-arcade-attract feature. After
a configurable period of idle time, Kartend starts smoothly scrolling
the items grid and (optionally) advancing the selection on its own.
Any user input — keyboard, mouse, gamepad — instantly stops attract
and resets the idle timer.

> **Where to find this** — Settings Dialog → **General** tab →
> **Attract Mode** section. INI keys all under `[General]`, prefixed
> `attractMode*`.

## Master toggle and idle timeout

```ini
[General]
attractModeEnabled=true
attractModeIdleTimeoutSec=120
```

| Setting | INI key | Default | Description |
|---------|---------|---------|-------------|
| Master toggle | `attractModeEnabled` | `false` | Off by default. |
| Idle timeout | `attractModeIdleTimeoutSec` | `120` | Seconds of inactivity before attract starts. |

Below ~30 s tends to feel jumpy; 120–600 s is comfortable for kiosks;
longer if you frequently park the cursor and read.

## What attract does

Two independent behaviors compose:

### Autoscroll

Smoothly scrolls the viewport. Default direction follows the view's
scroll axis (vertical in Grid / List / Horizontal-isn't-this-vertical
…wait, in Horizontal it scrolls horizontally; in Cover Flow it scrolls
along the carousel).

| Setting | INI key | Default | Description |
|---------|---------|---------|-------------|
| Autoscroll enabled | `attractModeAutoScrollEnabled` | `true` | Master toggle for autoscroll. |
| Scroll speed | `attractModeScrollSpeed` | `1.0` | Pixels per tick (sub-pixel via accumulator). Range `0.1`–`10`. |

Autoscroll is **sub-pixel** — speeds below 1.0 actually move at 1 px
every N ticks, not at "1 px per tick floored to 0." Makes slow scrolls
look smooth.

The viewport bounces at the top/bottom (or left/right) of the scrollable
range — direction reverses when it hits the end. The grid layout is
unchanged; only the scroll offset moves.

### Advance selection

Periodically jumps the selection to a new item. Pairs nicely with
autoscroll for a "show off the library" effect.

| Setting | INI key | Default | Description |
|---------|---------|---------|-------------|
| Advance selection enabled | `attractModeAdvanceSelectionEnabled` | `false` | Master toggle. |
| Advance interval | `attractModeAdvanceSelectionIntervalSec` | `5` | Seconds between selection moves. |
| Random advance | `attractModeAdvanceSelectionRandom` | `false` | If true, pick random items; otherwise sequential. |

Sequential advance walks the visible item list (taking the active
filters into account); random picks any visible item.

The selection-change is wrapped in an internal flag so it doesn't
register as user activity — attract keeps running. If the user
*genuinely* changes selection (via keyboard / mouse), attract stops.

## What stops attract

Any of the following resets the idle timer and stops attract:

- A keyboard key press (any rebound navigation key, Enter, Escape, /,
  etc.)
- A mouse movement (configurable; default reacts to actual movement,
  not just events)
- A mouse click
- A wheel scroll
- A gamepad button press or stick deflection

Attract resumes after `attractModeIdleTimeoutSec` of new idleness.

## Suspension during a launch

When [runtime detection](Splash-and-Now-Playing.md#runtime-detection)
is on, attract is **suspended** while a launched item is running. This
prevents Kartend from autoscrolling unseen behind a fullscreen
launched app.

```ini
[General]
attractModeEnabled=true
runtimeDetectionEnabled=true
```

When the launched process exits, attract's idle timer is re-seeded from
"now" — so it doesn't immediately fire on return. You get the full
`attractModeIdleTimeoutSec` after the resume-focus splash before
attract starts again.

Without runtime detection on, Kartend doesn't know the launched item
has started and exited, so attract keeps running its idle timer in the
background — which can result in attract starting *while* a launched
app is running, even though Kartend is in the background. Usually
harmless (Kartend is behind the launched app's window) but can cause
audio playback if a per-collection background video is configured.
Turn on runtime detection to fix.

## Compose with other features

| With… | Effect |
|-------|--------|
| Background video (`backgroundType=video`) | Both run; the looping wallpaper animates while attract scrolls. |
| Splash overlays | Boot splash plays first, then attract's idle timer starts. |
| Search bar focus | While search has focus, attract doesn't fire (search-bar focus is a form of "user activity"). |
| Sidebar visible | Sidebar updates as selection advances — useful as a "showroom" mode. |
| Cover Flow view | Carousel rotates through the center selection. With advance-selection-random on, this is essentially "demo mode." |
| Hold-scroll | Conflicts: hold-scroll is user input, so attract stops the moment hold-scroll starts. |

## Recipes

### Kiosk demo

```ini
[General]
attractModeEnabled=true
attractModeIdleTimeoutSec=30
attractModeAutoScrollEnabled=true
attractModeScrollSpeed=0.6
attractModeAdvanceSelectionEnabled=true
attractModeAdvanceSelectionIntervalSec=5
attractModeAdvanceSelectionRandom=true
runtimeDetectionEnabled=true
```

After 30 s idle, the grid slowly scrolls while a random item is
highlighted every 5 s. When someone presses anything, attract stops
instantly. Pair with [fullscreen + chromeless toolbar](Toolbar-and-Menus.md#minimal-kiosk-hide-everything-chrome).

### Subtle "still here" indicator

```ini
[General]
attractModeEnabled=true
attractModeIdleTimeoutSec=300
attractModeAutoScrollEnabled=true
attractModeScrollSpeed=0.3
attractModeAdvanceSelectionEnabled=false
```

5-minute idle timeout, very slow scroll, no selection movement.
Communicates "the app is alive and the screen isn't burned" without
actively distracting.

### Selection rotation only (no scroll)

```ini
[General]
attractModeEnabled=true
attractModeIdleTimeoutSec=120
attractModeAutoScrollEnabled=false
attractModeAdvanceSelectionEnabled=true
attractModeAdvanceSelectionIntervalSec=10
attractModeAdvanceSelectionRandom=false
```

Selection walks through items every 10 s; the viewport doesn't move.
Use Cover Flow to make this feel like a slideshow.

### Disable attract for a focused work session

Attract is a global toggle, not per-collection. Quickest disable:

```ini
attractModeEnabled=false
```

Re-enable when you want kiosk mode again. The other `attractMode*`
keys keep their values, so the configuration is preserved.

## Settings cheat sheet

| Setting | INI key | Default |
|---------|---------|---------|
| Master toggle | `attractModeEnabled` | `false` |
| Idle timeout (sec) | `attractModeIdleTimeoutSec` | `120` |
| Autoscroll enabled | `attractModeAutoScrollEnabled` | `true` |
| Scroll speed (px/tick) | `attractModeScrollSpeed` | `1.0` |
| Advance selection enabled | `attractModeAdvanceSelectionEnabled` | `false` |
| Advance interval (sec) | `attractModeAdvanceSelectionIntervalSec` | `5` |
| Random advance | `attractModeAdvanceSelectionRandom` | `false` |

## Where to next

- [Splash & Now Playing](Splash-and-Now-Playing.md) — runtime
  detection (controls attract suspension)
- [Toolbar & Menus → Minimal kiosk recipe](Toolbar-and-Menus.md#minimal-kiosk-hide-everything-chrome)
- [Themes & Appearance](Themes-and-Appearance.md) — composing with
  background video and parallax for "showroom" displays
- [View Modes → Cover Flow](View-Modes.md#cover-flow-view) — Cover
  Flow + attract = automatic slideshow

## For developers

- Manager: [src/modules/media/attract/](../../src/modules/media/attract/)
  (`AttractManager`).
- Idle timer: a single `QTimer` that resets on any tracked input
  event. Tracked events come from `EventManager` filters, plus a
  `signalIsActiveActivity()` API for selection changes that *should*
  count as activity.
- Selection-advance suppression flag: `AttractManager::isDrivingSelection`
  set true while attract is moving the selection itself, so the
  resulting `selectionChanged` signal isn't treated as user activity.
- Suspension: `setSuspended(true)` blocks all timer firings;
  `setSuspended(false)` re-seeds the idle timer.
- Sub-pixel scroll: an accumulator pattern — fractional speeds add
  to a remainder, integer pixel offsets are emitted when the
  remainder crosses 1.
- Bouncing at scroll bounds: implemented in `attractmanager.cpp` —
  reverses sign of the scroll speed when at min / max scroll position.
- Adding a new attract behavior (e.g. zoom pulse, color cycling):
  add a new sub-timer on `AttractManager` and signal the relevant
  manager.
