# Splash Screens & Now Playing

Three transient overlays sit at the boundary between Kartend's idle
state and an active launch:

1. **Boot splash** — short overlay during application startup.
2. **Startup video** — optional one-time intro video on launch.
3. **Resume-focus splash** — short overlay when the window regains
   focus after a launched item exits.

And one persistent overlay during a launched item's lifetime:

4. **Now Playing overlay** — covers the grid while a launched item is
   running, *if* runtime detection is on.

> **Where to find this** — Settings Dialog → **General** tab → **Splash
> Screens**, **Startup Video**, **Runtime Detection** sections.

## Boot splash

A short overlay shown while Kartend initializes — it covers the brief
window between the executable starting and the first collection
rendering. Used to mask Qt's "white flash" startup and to signal that
the app is loading.

```ini
[General]
bootSplashEnabled=true
```

Default `true`. Disabled installs go straight to a black screen until
the first paint, then to the grid.

The overlay shows:

- App name (`Kartend`)
- Version
- Optional logo
- A subtle progress indicator

It auto-dismisses when the first collection has rendered, typically
within 200–500 ms.

There's no per-collection theming for the boot splash today (the
overlay is global and runs before any collection is selected).

## Startup video

A one-time intro video played at launch — branded splash, distributor
logo, retro-system bumper, etc. Plays full-screen with audio (unmuted).

```ini
[General]
startupVideoEnabled=false
startupVideoPath=~/themes/intro.mp4
```

Default disabled. When enabled, plays once on every launch.

| Behavior | Detail |
|----------|--------|
| Plays once | Per-launch; does not loop |
| Skippable | Any keypress, mouse click, or gamepad input dismisses |
| Audio | Unmuted (uses system volume; **not** the `previewVideoVolume` slider) |
| Format support | Same Qt Multimedia / GStreamer codec list as [Video Previews](Video-Previews.md) |

The startup video is **separate** from any per-collection background
video (`backgroundVideo`) — those are persistent looping wallpapers,
this is a one-shot intro.

If `startupVideoEnabled=true` but `startupVideoPath` is empty or the
file doesn't exist, Kartend logs a warning and skips silently.

## Resume-focus splash

A short overlay shown when the Kartend window regains focus after a
launched item closes (window manager re-focus event). Used to bridge
the moment between "external app exits" and "Kartend window comes back"
so the user sees a consistent surface.

```ini
[General]
resumeFocusSplashEnabled=true
```

Default `true`. Set to `false` for an instant return to the grid.

The overlay is brief (typically 200–600 ms) and skippable with any
input. It also serves as a moment for the layout / video-preview
pipeline to re-warm before the user starts navigating.

When **runtime detection** is enabled, the resume-focus splash is also
the transition out of the [Now Playing](#now-playing-overlay) overlay
when the launched process exits.

## Now Playing overlay

While a launched item is running and runtime detection is enabled,
Kartend renders a **Now Playing overlay** over the items grid. The
overlay shows what's running and optionally fades the grid behind it.

```ini
[General]
runtimeDetectionEnabled=false
```

Default `false`. Off = Kartend launches items detached and forgets
them; the grid stays interactive while the launched app runs.

### What runtime detection turns on

| Effect | Detail |
|--------|--------|
| Now Playing overlay | Renders while child process is alive |
| Time-played tracking | `launch_history.duration_seconds` is populated, sidebar shows accurate play times |
| Attract mode suspension | [Attract](Attract-Mode.md) won't scroll behind a launched window |
| Process-aware quit | Kartend can detect crashed launches and surface error dialogs |

### What runtime detection costs

| Cost | Detail |
|------|--------|
| Kartend stays focused | The window is in the foreground while the launched app runs (may be behind the launched app's window if the launched app is fullscreen) |
| Process tracking | One `QProcess` per launch; a small amount of memory and a file descriptor pair |
| Cleanup needed | Crashed Kartend can leave orphaned processes (rare) |

If your launchers always go fullscreen anyway and you don't return to
Kartend until they exit, runtime detection is a clear win — you get
accurate stats and the Now Playing overlay for "free." If you launch
items as background tasks (e.g. `xdg-open` for a PDF you read in
another window while continuing to browse Kartend), turn it off.

### Now Playing overlay contents

| Element | Detail |
|---------|--------|
| Item name | Centered, large |
| Item artwork | If available, large in the background |
| Status | "Running…" message; updates if the process emits any tracked output |
| Elapsed timer | Live counter |
| Progress indicator | Subtle pulse while alive |

`Escape` does **not** close the overlay (it would lose the runtime
detection link). The overlay closes when the child process exits — at
which point the [resume-focus splash](#resume-focus-splash) takes over
briefly before the grid returns.

### Multiple simultaneous launches

Today Kartend tracks one launched item at a time. If you launch a
second item while the first is still running, the first launch's
runtime tracking is detached and only the second is tracked. Concurrent
runtime tracking is on the wishlist.

## When to enable each

| Splash / overlay | Enable when |
|------------------|-------------|
| Boot splash | You want a polished startup transition (default — leave on). |
| Startup video | You want a branded intro / kiosk welcome. |
| Resume-focus splash | You want the same polished transition coming back from a launch (default — leave on). |
| Runtime detection | You want accurate time-played stats and the Now Playing overlay; your launchers usually go fullscreen. |

## Recipes

### Branded kiosk

```ini
[General]
bootSplashEnabled=true
startupVideoEnabled=true
startupVideoPath=~/themes/kiosk-intro.mp4
resumeFocusSplashEnabled=true
runtimeDetectionEnabled=true
attractModeEnabled=true
attractModeIdleTimeoutSec=60
fullscreen=true
showMenuBar=false
showToolbar=false
```

Boot splash → intro video → grid → attract mode after 60 s of idle →
Now Playing while running launched items → resume-focus splash on
return.

### Power-user, no transitions

```ini
[General]
bootSplashEnabled=false
startupVideoEnabled=false
resumeFocusSplashEnabled=false
runtimeDetectionEnabled=false
```

Cold start straight to the grid. Launches are detached; you can keep
browsing while a launched item runs.

### Stats-tracking but unobtrusive

```ini
[General]
bootSplashEnabled=true
resumeFocusSplashEnabled=true
runtimeDetectionEnabled=true
historyEnabled=true
historyMaxEntries=5000
```

Splashes for polish; runtime detection for accurate stats. The Now
Playing overlay only shows for the duration of a launch (which is
typically when you're not looking at Kartend anyway, since the
launched app is in front).

## Where to next

- [Attract Mode](Attract-Mode.md) — the idle-screensaver layer that
  composes with these overlays
- [History & Statistics](History-and-Statistics.md) — what runtime
  detection enables in the data layer
- [Themes & Appearance](Themes-and-Appearance.md) — for *persistent*
  background video (different from startup video)

## For developers

- Boot splash: `MainWindow` paints the splash widget before the first
  collection load completes; `bootSplashEnabled` gates the path.
- Startup video: [src/ui/widgets/overlays/startupvideooverlay.h](../../src/ui/widgets/overlays/)
  (`StartupVideoOverlay`). Reuses the `QMediaPlayer` infrastructure;
  unmuted (`QAudioOutput::setVolume(1.0)`).
- Resume-focus splash: same widget path, triggered by
  `QApplication::focusWindowChanged` after a known-launched-item exit.
- Now Playing overlay: [src/ui/widgets/overlays/nowplayingoverlay.h](../../src/ui/widgets/overlays/).
  Active while `LaunchManager`'s tracked `QProcess` is alive.
- Attract suspension hook:
  `AttractManager::setSuspended(true)` is called by `LaunchManager`
  when runtime detection is on; `setSuspended(false)` on exit.
- Runtime detection mode change: requires `LaunchManager` to use
  non-detached `QProcess::start()` instead of `QProcess::startDetached()`.
  See `LaunchManager::launchItem()`.
- Adding a new transient overlay (e.g. "loading collection" overlay):
  follow `StartupVideoOverlay`'s widget pattern, anchor to MainWindow,
  drive lifetime from the relevant manager.
