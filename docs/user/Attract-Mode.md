# Attract Mode

Attract mode is Kartend's screensaver-meets-arcade-attract feature. After
a configurable period of idle time, Kartend starts smoothly scrolling
the items grid and (optionally) advancing the selection on its own.
Anything that **moves the selection** stops attract and resets the idle
timer. That is narrower than "any input", and deliberately so — see
[What stops attract](#what-stops-attract).

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

Smoothly scrolls the viewport along the view's scroll axis: vertically
in Grid and List, horizontally in Horizontal view.

Cover Flow is the exception. Its carousel isn't backed by the scroll
area attract drives — that area is hidden with both scrollbars forced
off — so autoscroll has nothing to move there and does nothing. Use
**advance selection** instead if you want a Cover Flow demo.

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
filters into account). Random is *directional* rather than uniform: it
draws from the half of the list ahead of the current position in the
direction attract is scrolling, flipping only when that half is empty.
This keeps the jumps roughly aligned with the scroll instead of
whipsawing the viewport end to end.

The selection-change is wrapped in an internal flag so it doesn't
register as user activity — attract keeps running. If the user
*genuinely* changes selection, attract stops.

## What stops attract

Exactly two things stop attract: **a selection change you caused**, and
**launching an item**. That's it.

This is a deliberate design choice, not an oversight. Attract mode is
built for cabinets and kiosks, where stray mouse movement, a bumped
table, focus churn from another application, or a cat on the keyboard
would otherwise cancel the demo constantly. Tying the cancel to an
actual selection change means attract survives noise and yields to
intent.

The practical consequences, by input:

| Input | Stops attract? |
|-------|----------------|
| Arrow keys / any rebound navigation key | Yes — they move the selection |
| Mouse wheel | Yes — the wheel moves the selection |
| Clicking a *different* tile | Yes |
| Clicking the *already-selected* tile | No — nothing changed |
| Moving the mouse | No, unless `selectItemOnHover` is on and the hover dwell lands on a different tile |
| Gamepad D-pad or left stick | Yes |
| Gamepad A / B / Y (confirm, back, sidebar) | No — they change no selection |
| `Enter`, `Escape`, `/`, `I`, `F8`–`F11`, zoom chords | No |
| Hold-scroll | Yes, but only on the first repeat *step* — not at button-down, so there's a `clickHoldDelayMs` gap first |
| Launching an item | Yes — attract is suspended for the duration |

Attract resumes after `attractModeIdleTimeoutSec` of new idleness.

`attractModeIdleTimeoutSec` is clamped to 10–3600 on load, and
`attractModeAdvanceSelectionIntervalSec` to 1–600, so a hand-edited
value outside those ranges is quietly pulled back in.

> Attract is **not** suppressed while a modal dialog is open or while a
> preview video is playing. If you are building a kiosk, don't rely on a
> dialog to hold it off.

## Suspension during a launch

Attract is **suspended** while a launched item is running, so Kartend
never autoscrolls unseen behind a fullscreen app. When the process
exits, the idle timer is re-seeded from "now" — you get the full
`attractModeIdleTimeoutSec` after the resume-focus splash before
attract starts again.

This holds **whether or not
[runtime detection](Splash-and-Now-Playing.md#what-runtime-detection-turns-on)
is enabled.** Detached, fire-and-forget launches — the default, with
runtime detection off — get the same suspension via a session
start/end signal pair, backstopped by window focus and a probe for the
case where the launched app never takes focus at all. Runtime detection
buys you the Now Playing overlay and play-time tracking; it is not
required to keep attract out of the way.

## Compose with other features

| With… | Effect |
|-------|--------|
| Background video (`backgroundType=video`) | Both run; the looping wallpaper animates while attract scrolls. |
| Splash overlays | Boot splash plays first, then attract's idle timer starts. |
| Search bar focus | No interaction. Attract has no knowledge of the search bar and will start with it focused, as long as the selection hasn't moved for the timeout. |
| Sidebar visible | Sidebar updates as selection advances — useful as a "showroom" mode. |
| Cover Flow view | Advance-selection works and rotates the carousel. **Autoscroll does not** — Cover Flow hides the scroll area that attract drives, so there is nothing for it to scroll. Enable advance-selection if you want motion in Cover Flow. |
| Hold-scroll | Attract stops on the first hold-scroll *step*, not at button-down — so there is a `clickHoldDelayMs` window where both are notionally live. |

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

- Manager: [src/modules/input/attract/](../../src/modules/input/attract/)
  (`AttractManager`).
- Idle timer: a single `QTimer`, reset by `onActivityDetected()`. That
  method has exactly two callers — the `SelectionManager::selectionChanged`
  lambda wired in `interactionmanager_wiring.cpp`, and the launch path.
  `EventManager::activityDetected` exists and is emitted, but nothing
  connects to it; the wiring comment states the policy outright ("mouse
  movement/focus churn must not count as activity for attract mode").
  If you are tempted to hook raw input up to the idle timer, read that
  comment first.
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
