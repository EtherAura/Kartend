# Troubleshooting

Solutions to common issues. Symptom-organized — find your symptom, jump
to the fix. If your problem isn't listed, file an issue at
[github.com/EtherAura/Kartend/issues](https://github.com/EtherAura/Kartend/issues)
with the relevant log excerpt (see [Logging & Diagnostics](Logging-and-Diagnostics.md)
for capturing useful output).

## Quick logging refresher

```bash
# Capture stderr to a file
kartend 2>&1 | tee ~/kartend.log

# Verbose tracing
KARTEND_LOG_RULES="kartend.*=true" kartend 2>&1 | tee ~/kartend.log

# Specific category only (search example)
KARTEND_LOG_RULES="kartend.searchdiag.debug=true" kartend
```

For specific debug categories see [Logging & Diagnostics →
Categories](Logging-and-Diagnostics.md#logging-categories).

For a debug build with assertions and full debug output:

```bash
.scripts/build.sh --debug
./build/ninja-debug/kartend
```

## Collection / scanning issues

### My collection is empty after pointing to a folder

| Likely cause | Fix |
|---|---|
| Wrong **Extensions** filter | Settings → Paths → set extensions to match your files (e.g. `pdf,epub`) or leave blank to accept any. |
| Folder is empty or contains only subfolders | Enable `includeContentSubfolders=true` to surface nested folders as virtual subcollections. See [Collections → Folder browsing](Collections.md#folder-browsing-treating-subfolders-as-collections). |
| Hidden folders | Enable `showHiddenFolders=true` if your media lives in dot-prefixed directories. |
| Path typo or bad permissions | Verify the path exists and is readable: `ls -la "<mediaDirectory>"`. |
| Path has environment variables | `$HOME`, `$XDG_*` are **not** expanded — use `~` or absolute paths. |

### "Media directory not found" error

The path saved in the config no longer exists, or `~` expansion failed.

- Confirm the directory still exists: `ls -d ~/path/from/config`.
- Re-pick the directory from the Settings Dialog so the absolute path
  is re-saved.
- If you renamed the folder, update `mediaDirectory` to the new path.
- Avoid environment variables (`$HOME`, `$XDG_*`) in the config —
  only `~` is expanded.

### Force a rescan

Press **Ctrl+F5** (or **File → Rescan Collection**) to drop the
database state for this collection and rebuild from disk. Use after
adding/removing files outside Kartend.

For a less destructive refresh that keeps database state, **F5** does
a soft reload (re-renders without re-scanning).

### Subcollections / virtual folders missing

| Cause | Fix |
|---|---|
| `includeContentSubfolders` is off | Settings → Paths → enable. |
| `hideSubcollectionTiles=true` is set | Settings → General → disable, or unset the toolbar's hide-subs toggle. |
| Type filter is hiding them | Toolbar → Filter dropdown → select **(All)**. |

## Artwork issues

### Tiles show colored placeholders instead of cover art

| Cause | Fix |
|---|---|
| Filename mismatch | Artwork is matched by base filename (case-sensitive on case-sensitive filesystems). `a-film.mkv` needs `a-film.png` (or `.jpg` / `.jpeg` / `.webp`) in the artwork directory. |
| Wrong artwork directory | Settings → Paths → **Artwork Directory**. |
| Unsupported image format | Convert to `png`, `jpg`, `jpeg`, or `webp`. |
| Artwork in nested folders | Enable `includeArtworkSubfolders=true`. |
| Large artwork directory still loading | Artwork loads asynchronously in batches. Wait a few seconds; the in-memory + disk cache speeds up subsequent opens. |
| Custom artwork type | Standard auto-discovery doesn't apply. Add a manual link via right-click → **Edit artwork links…**. See [Artwork → Custom artwork types](Artwork.md#custom-artwork-types). |

### Artwork looks fuzzy or pixelated

Tile size is set per-collection. Increase `itemWidth` / `itemHeight`
(or adjust in the Settings Dialog) — Kartend will re-render at the new
size on next view.

If you have very small source artwork, the issue is upscaling. Re-scrape
or replace with higher-resolution images.

### Generate placeholders for subfolders

See [docs/subfolder-artwork.md](../subfolder-artwork.md) for the
script that composes folder-art montages.

### Sidebar gallery shows empty / placeholder slots

- Standard artwork types only auto-discover when the file is suffix-
  matched (`a-film-boxfront.png`, `a-film-screenshot.png`, etc.). If
  you have just one image per item, only the unsuffixed slot fills.
- Custom artwork types never auto-discover; populate via right-click →
  **Edit artwork links…**.
- The video tile shows only when the item has a matching file in
  `videoDirectory`.

## Launching issues

### Nothing happens when I press Enter

| Cause | Fix |
|---|---|
| Empty `launcherPath` | Settings → Launcher → set the launcher executable. |
| Launcher not executable | `chmod +x /path/to/launcher`, or use an absolute path to a known-good binary. |
| Launcher requires extra args | Set `launchParameters` (e.g. `--fullscreen`) and/or `corePath` for RetroArch-style launchers. |
| Item file path contains spaces / special characters | Kartend passes arguments as an array to `QProcess`, no shell escaping needed. If your launcher itself mishandles spaces, wrap in a shell script. |
| Item path falls under a sensitive system directory (`/`, `/etc`, `/proc`, `/sys`, `/dev`, `/root`, etc.) | Kartend blocks these by design. Move the items elsewhere. |

### Launcher opens but my file isn't loaded

Kartend appends the selected file's absolute path as the **last**
argument to the launcher. If your launcher expects the path elsewhere
(e.g. after `--media`), wrap in a small shell script:

```bash
#!/bin/sh
exec /path/to/weird-launcher --media "$1"
```

Then point `launcherPath` at the script. See [Launchers →
Recipes](Launchers.md#recipes).

### RetroArch launches without a core

Use both `launcherPath` (the RetroArch binary) and `corePath` (the
loadable libretro core `.so`). Kartend will pass `-L <corePath>
<fileName>` to the launcher.

The launcher path must contain `retroarch` (case-insensitive) for the
`-L` injection to apply: `/usr/bin/retroarch`,
`~/bin/retroarch.AppImage`, etc. Other launchers ignore `corePath`.

### Archive items launch the `.zip` instead of the contained file

Enable `extractArchives=true` and set `extractedExtension` to the file
type inside the archive (e.g. `pdf`, `cbz`). Kartend extracts to a
temp dir under `~/.cache/kartend/`, finds the matching file, and
launches that.

If the archive contains nothing matching `extractedExtension`, the
launch fails with an error dialog — typically a sign the archive has
unexpected content (different extension, nested zip).

### "Always launch with…" doesn't appear in the context menu

The action only shows when a collection has more than one launcher
(primary + at least one additional). Add an additional launcher under
Settings → Launcher to enable per-item overrides.

### Per-item launcher override won't clear

Right-click → **Clear launcher override**. Appears only if an override
is set. If the menu item doesn't appear, no override is in place.

## Video preview issues

### Video tile appears in the gallery but won't play

Codec missing. Qt Multimedia uses GStreamer on Linux; install the
relevant `gst-plugins-*` package:

| Distro | Install |
|--------|---------|
| Debian / Ubuntu | `sudo apt install gstreamer1.0-libav gstreamer1.0-plugins-good gstreamer1.0-plugins-bad` |
| Arch | `sudo pacman -S gst-libav gst-plugins-good gst-plugins-bad` |
| Gentoo | Pull in `media-libs/gst-plugins-libav` etc. |

Verify a codec's available:

```bash
gst-inspect-1.0 | grep h264
```

### No video tile in the gallery at all

The item has no matching file in `videoDirectory`. Drop a video file
named the same as the media file (different extension):

```
mediaDirectory/A Film.mkv
videoDirectory/A Film.mp4
```

See [Video Previews](Video-Previews.md).

### Audio plays but video is black

GPU-acceleration mismatch in Qt Multimedia. Try:

```bash
QT_MEDIA_BACKEND=ffmpeg kartend
```

(Qt 6.5+; if you're on an earlier Qt, this option doesn't exist —
update GPU drivers or check GStreamer plugin install.)

### Cover Flow shows artwork instead of video

Cover Flow auto-plays the center item's video if available; falls
back to artwork otherwise. To get video in Cover Flow, add a matching
file to `videoDirectory`.

### Audio plays at unexpected volume

The toolbar **Preview Volume** slider controls all sidebar / Cover
Flow video. Drag to adjust. Persisted as `[General] previewVideoVolume`.

Startup video uses the *system* volume, not the slider. To mute the
intro video, edit your config:

```ini
startupVideoEnabled=false
```

## Performance

### Scrolling is sluggish on a huge collection

- Reduce `itemWidth` / `itemHeight` so more tiles fit in cache.
- Make sure you're running a **release** build, not a debug build:
  `.scripts/build.sh` (no `--debug`).
- Lower scroll-animation duration: `[General] scrollAnimationDurationMs=500`
  (default `1500`).
- Tune scroll multiplier: `[General] scrollVelocityMultiplier=1.5`.

### Artwork loading hammers the disk

Artwork loads in parallel batches. On slow disks, the first scan can
saturate the I/O queue. Wait for the initial scan to settle —
subsequent navigation hits the in-memory + disk cache.

For mechanical disks especially, decreasing the simultaneous batch
size could help — currently this is a `UIConstants` tuneable, requires
a rebuild. See [docs/constants.md](../constants.md).

### Cache grows too large

In-memory cache: bounded by `pixmapCacheSizeMB` (default `50`).
On-disk cache: under `~/.cache/kartend/`, bounded only by your
filesystem. Safe to delete when Kartend isn't running:

```bash
rm -rf ~/.cache/kartend/
```

Kartend will rebuild the cache on demand on next open.

### Boot is slow

- Disable `bootSplashEnabled` and `startupVideoEnabled` if you don't
  need them.
- Disable `runtimeDetectionEnabled` if you don't need accurate time-
  played stats — Kartend won't track child processes.
- Profile with `KARTEND_LOG_RULES="kartend.scanflow.debug=true;kartend.perftrace.debug=true" kartend`
  and look for the slowest phase.

### High idle CPU

If Kartend is sitting at the grid and using non-trivial CPU:

- Disable any **background video** (`backgroundType=video`) on the
  active collection. Video wallpapers are the most CPU-expensive
  background.
- Disable parallax (`wallpaperParallax=false`).
- Disable backdrop blur (`toolbarBackdropBlur=false`).
- Disable [attract mode](Attract-Mode.md) if it's running (it
  re-paints the viewport).

If idle CPU is still high, file an issue with the active collection's
config and a `kartend.*=true` log excerpt.

## Database / session

### "Database connection lost" warning

`DatabaseManager` retries automatically (up to 3 attempts with
exponential backoff). If it persists:

- Check `~/.local/share/kartend/` for permission / disk-full issues.
- Quit Kartend, move the `.db` file aside (or delete it), let Kartend
  rebuild on next launch. You'll lose per-item state (custom fields,
  manuals, history, playlists) but media + artwork are preserved.

### Selection or scroll position resets every launch

- Ensure `[General] rememberSelection=true` is set.
- Verify `~/.config/kartend/` is writable — if Kartend can't save
  state, it can't restore it.
- Try `KARTEND_LOG_RULES="kartend.selectionrestoremanager.debug=true" kartend`
  to see why restore is failing.

### Custom fields disappeared

Custom fields live in the `item_metadata` table in the database. If
the database was deleted (or Kartend can't write to it):

- Custom fields don't survive database deletion.
- They aren't included in `.kart` exports (configuration only — see
  [Backup & Sharing](Backup-and-Sharing.md)).
- If you have a backup of the database file, restore it.

## Settings issues

### Edits to `kartend.cfg` get overwritten

If you edit the config file by hand while Kartend is running, the
Settings Dialog can rewrite it on save. Workflow:

1. Quit Kartend.
2. Edit `~/.config/kartend/kartend.cfg`.
3. Restart Kartend.

For round-tripping changes to/from version control, edit only when
Kartend is closed.

### Save button stays glowing after I clicked Save

That's a bug — please file an issue with reproduction steps.
Workaround: close and reopen the dialog.

### "Apply To Selected" doesn't propagate the field I expected

Some fields are non-propagatable on purpose: paths (`mediaDirectory`,
`artworkDirectory`, etc.), launcher path, parent linkage, extensions.
The Apply Settings Dialog grays these out regardless of the category
checkbox. See [Settings Dialog → Apply Settings](Settings-Dialog.md#apply-settings)
for the propagatable-field list.

## Keyboard / input

### A shortcut doesn't work

- Check it hasn't been rebound under **Settings → General → Keyboard
  Bindings**.
- Some shortcuts only apply when the items grid has focus; others are
  global (`F1`, `Ctrl+,`, `Ctrl+Q`, `Ctrl+1..4`).
- Press `F1` for the in-app reference of currently active bindings.
- View toggles (`F8`–`F11`), zoom (`Ctrl+=` / `Ctrl+-` / `Ctrl+0`),
  Quit, and Settings are not rebindable.

### Search bar won't open

`/` focuses the search bar — it must be visible. If you've toggled
the toolbar off (`F8`), turn it back on or use the menu's **View**
entry.

If `toolbarShowSearchBar=false`, the bar is hidden. Set to `true`.

### Gamepad not detected

- Check that a gamepad backend is available: Qt6 Gamepad
  (`qt6-gamepad`) or SDL2 (`libsdl2`). Without either, gamepad UI is
  hidden.
- Verify the gamepad is detected by the system:
  - `evtest /dev/input/eventX` (find the right device with
    `cat /proc/bus/input/devices`)
  - or check `lsusb` / `hidraw`
- Plug-and-replug — Kartend's backend supports hot-plug in most cases.
- Verify `gamepadUseDpad=true` and / or `gamepadUseLeftStick=true`.

### Hold-scroll doesn't activate

- Ensure `clickHoldDelayMs > 0` (default `500` ms).
- Verify you're holding the *primary* mouse button on a tile. Hold
  outside any tile starts no action.
- Increase `clickHoldRepeatIntervalMs` if your machine struggles with
  the default repeat rate.

### Artwork-cycle modifier conflicts with my window manager

If `Shift + middle-click` is bound to something at the window-manager
level (e.g. text selection paste), change Kartend's modifier:

```ini
[General]
artworkCycleModifier=Control
```

Choices: `Shift`, `Control`, `Alt`, `Meta`.

## CLI / headless

### `--export-kart` returns "no collection named '...'"

The name match is case-sensitive and uses the **display name**, not the
filesystem path. Verify the name in `kartend.cfg` (the section header)
or the Settings Dialog tree. Quote the name if it has spaces.

### `--import-kart` fails silently in scripts

Add `2>&1` to capture stderr:

```bash
kartend --import-kart suspect.kart 2>&1
```

Exit code `2` indicates argument or operation error; check the stderr
output.

### Headless invocation hangs

Possible causes:

- Kartend tried to display a dialog (e.g. an unhandled error). Run
  with `KARTEND_LOG_RULES="kartend.*=true"` to see what.
- A `.kart` package is corrupted. Try a smaller / known-good package.
- Filesystem permissions (writing to `--to` directory).

If you're on a headless server with no X / Wayland display, set
`QT_QPA_PLATFORM=offscreen`:

```bash
QT_QPA_PLATFORM=offscreen kartend --import-kart pkg.kart --to ~/data
```

## Build / install

For build, dependency, and install issues see
[building.md](../building.md). Quick pointers:

| Issue | Doc |
|-------|-----|
| Missing Qt6 / dependencies | [building.md](../building.md) |
| `--install` fails | [readme.md → Installation](../../readme.md#installation) |
| Uninstall didn't remove everything | [readme.md → Uninstalling](../../readme.md#uninstalling) |
| CI passes locally but fails on GitHub | [building.md → CI parity](../building.md) |

## Where to next

- [Logging & Diagnostics](Logging-and-Diagnostics.md) — every diagnostic
  toggle
- [File Locations](File-Locations.md) — where state lives, recovery paths
- [Configuration Reference](Configuration-Reference.md) — every config key
- [Building](../building.md) — build dependencies, common build errors

## Still stuck?

File an issue at
[github.com/EtherAura/Kartend/issues](https://github.com/EtherAura/Kartend/issues)
with:

- Kartend version (`kartend --version`)
- Distro and Qt version
- Reproduction steps
- Relevant log excerpt — ideally with `KARTEND_LOG_RULES="kartend.*=true"`

For security issues see [SECURITY.md](../../SECURITY.md).
