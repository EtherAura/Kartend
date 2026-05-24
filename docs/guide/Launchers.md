# Launchers

A **launcher** is the executable Kartend hands an item to when you
press `Enter` (or double-click). It can be `xdg-open`, `mpv`, a libretro
runtime, a wrapper script, or anything else that takes a file path on
the command line.

Each collection has at minimum one launcher (the **primary**) and can
add any number of **additional launchers** for the same library — useful
when one set of items can be opened with multiple tools (a video file
that you sometimes run in `mpv` and sometimes in `vlc`, for example).
Configurations can be saved as **presets** and reused across
collections, and individual items can override the default choice via
right-click.

> **Where to find this** — Settings Dialog → **Launcher** tab.
> INI keys: `launcherPath`, `launchParameters`, `corePath`,
> `launcherName`, `additionalLaunchers`, `defaultLauncherIndex`,
> and `[General] launcherPresets`.

## How launching works

When you trigger a launch, Kartend:

1. Resolves which launcher to use — primary, an additional, or a
   per-item override. See [Selection rules](#selection-rules).
2. Builds an **argument list** (no shell): `<launchParameters>`,
   followed by `-L <corePath>` if the launcher path matches a
   libretro-style binary, followed by the absolute path to the
   item file.
3. Spawns the process with `QProcess`. By default Kartend detaches and
   forgets it; with **Runtime Detection** enabled (see
   [Splash & Now Playing](Splash-and-Now-Playing.md#runtime-detection))
   Kartend tracks the child's lifetime instead.
4. Records the launch in the database (per-item `play_count` increments,
   `last_played` timestamp, optional history row). See
   [History & Statistics](History-and-Statistics.md).

### Argument list, not shell command

Arguments go through a `QProcess` argument list. There is **no shell
interpolation**: `~`, `$VAR`, `&`, `>`, quote rules, glob patterns —
none of them work the way they would in `bash`. If you need shell
behavior, point the launcher at a small wrapper script:

```bash
#!/bin/sh
# /usr/local/bin/kartend-mpv-fullscreen
exec /usr/bin/mpv --fs --really-quiet "$1"
```

```ini
launcherPath=/usr/local/bin/kartend-mpv-fullscreen
```

Spaces and ampersands in file paths are handled correctly without
quoting because each argument is passed as a separate array element.
You don't need to wrap launch parameters in quotes either.

### Where the file path goes

The selected item's absolute path is appended **last** by default. If
your launcher needs the path elsewhere — say, after a `--media` flag —
use a wrapper script:

```bash
#!/bin/sh
exec /usr/bin/some-tool --media "$1" --extra-flag
```

There's no built-in `{path}` placeholder syntax in `launchParameters`
today.

## Primary launcher

The required-by-default launcher. Set on the **Launcher** tab:

| Field | INI key | Notes |
|-------|---------|-------|
| Launcher Path | `launcherPath` | Executable. Absolute path recommended. |
| Launcher Name | `launcherName` | Display name in the multi-launcher chooser. Empty falls back to the basename of `launcherPath`. |
| Core Path | `corePath` | RetroArch / libretro core (only used for libretro-style launchers). |
| Launch Parameters | `launchParameters` | Arguments passed *before* the file path. |

Examples:

```ini
# xdg-open: opens with the user's default app for the file type
[Documents]
launcherPath=/usr/bin/xdg-open

# mpv with fullscreen
[Movies]
launcherPath=/usr/bin/mpv
launchParameters=--fs --really-quiet

# RetroArch + libretro core
[Retro Library]
launcherPath=/usr/bin/retroarch
corePath=/usr/lib/libretro/some_libretro.so
launchParameters=--fullscreen

# Standalone wrapper (no core)
[Audiobooks]
launcherPath=/usr/bin/mpv
launchParameters=--no-video --save-position-on-quit
```

## Additional launchers

A collection can have a list of **additional launchers** in addition to
the primary. They appear in the multi-launcher chooser dialog and let
you pick at launch time which one to use.

```ini
[Films]
launcherPath=/usr/bin/mpv
launcherName=mpv

# additionalLaunchers is serialized; one row per additional entry.
# In practice this is managed via the Settings → Launcher tab.
```

Settings dialog flow:

1. Open **Launcher** tab → **Additional Launchers** list.
2. Click **Add Launcher** to open the [Launcher Editor Dialog](#launcher-editor-dialog).
3. Fill in name / path / core / parameters (or pick a preset).
4. Save. The new launcher appears in the list.

The combined list (primary + additional) is what the multi-launcher
chooser presents and what per-item overrides reference. See
[Default launcher index](#default-launcher-index) for which one is
pre-selected.

### Default launcher index

`defaultLauncherIndex` (default `0` = primary) controls the
pre-selection in the chooser dialog and the launcher used for items
without a per-item override.

```ini
[Films]
launcherPath=/usr/bin/mpv
launcherName=mpv
defaultLauncherIndex=1   ; vlc (additional[0]) is the default instead
```

The Settings tab gives you a **Default Launcher** dropdown that lists
the same options.

## Multi-launcher chooser

When a collection has more than one launcher, you can launch with a
non-default by:

- **Right-click → Always launch with…** on a specific item — opens the
  [Launcher Chooser Dialog](#launcher-chooser-dialog) with the current
  default pre-selected. Picking a launcher creates a per-item override
  (stored in the database, *not* in the INI file).
- A future "ad hoc launch" workflow may let you pick once without
  saving an override; today every chooser pick saves.

To remove a per-item override, right-click → **Clear launcher
override**.

### Launcher Chooser Dialog

Modal radio-button list of all launchers for the current collection.

- Pre-selects the current default (or the existing override).
- Description below each entry shows the resolved name + path + core +
  parameters (preset references are resolved live).
- OK / Cancel.

## Launcher presets (global, reusable)

Presets are reusable launcher configs that live in `[General]`, not
per-collection. Useful if you have, say, the same `mpv --fs --really-quiet`
config on twelve different movie collections.

```ini
[General]
launcherPresets=...   ; serialized list of presets, managed via the UI
```

A preset has:

- `id` — UUID. Stable across rename. References use the id, not the
  name.
- `name` — display label.
- `launcherPath`, `corePath`, `launchParameters` — same shape as a
  regular launcher.

A launcher entry that **references** a preset stores the preset's `id`
on its `presetId` field. At launch time, Kartend resolves the preset
and uses its values; if the preset is deleted, Kartend falls back to
any inline values on the launcher entry.

### Managing presets

Settings → **Launcher** tab → **Global Launcher Presets** section.
Add, rename (safe — references are by id), edit, or delete.

### Renaming preset is safe

Renames update the display label everywhere instantly without breaking
any references — entries stored the `id`, not the name.

### Adding a launcher from a preset

In the Launcher Editor Dialog, set **Preset** to a preset id. The
inline path / core / parameters fields gray out — the preset
overrides them. Clear the preset reference to switch back to inline
config.

## RetroArch / libretro

RetroArch is supported as a special case because its CLI takes a core
via `-L`:

```
retroarch -L <corePath> <fullscreenFlags> <fileName>
```

Kartend detects "RetroArch-style" launchers by checking whether the
launcher path's filename contains `retroarch` (case-insensitive). If
so, `corePath` is automatically passed as `-L <core>`. Examples that
match: `/usr/bin/retroarch`, `~/bin/retroarch.AppImage`,
`retroarch.exe`. Examples that don't: `mednafen`, `pcsx2`, `dolphin`.

For non-RetroArch launchers `corePath` is ignored — leave it blank.

## Archive extraction

Some cores can't open zipped content directly. Set the extraction
options on the **Paths & Extensions** tab:

| Setting | INI key | Notes |
|---------|---------|-------|
| Extract Archives | `extractArchives` | Boolean toggle |
| Extracted Extension | `extractedExtension` | Which extension inside the archive to launch (e.g. `pdf`, `cbz`) |

When enabled and the selected item is an archive (`.zip`, `.7z`, …),
Kartend extracts to a temporary directory under `~/.cache/kartend/`,
finds the first file matching `extractedExtension`, and launches *that*
file instead of the archive. Temp directories are cleaned up on
shutdown.

If the archive contains nothing matching `extractedExtension`, the
launch fails with an error dialog.

## Per-item launcher override

Items remember which launcher you "always launch with" via the database,
not the INI:

- Right-click → **Always launch with…** opens the chooser; pick one to
  set the override.
- Right-click → **Clear launcher override** removes it; the collection
  default takes effect again.
- Override is keyed by `(collection_uuid, source_path)` so it survives
  rescans and persists across rename of the source file (assuming you
  don't move it).

The right-click options appear only when a collection has more than
one launcher (otherwise there's nothing to choose).

## Validation and security

Kartend performs basic validation before launching:

- The launcher path must point to an existing, executable file.
- The launcher path is re-validated immediately before `QProcess::start`
  to mitigate TOCTOU (time-of-check / time-of-use) attacks where the
  file is swapped between Kartend's first check and the actual launch.
- A blacklist blocks launches where the *item path* falls under known
  sensitive system directories (`/`, `/etc`, `/root`, `/proc`, `/sys`,
  `/dev`, etc.) — these are rejected with an error dialog. The launcher
  itself can still live anywhere.
- All arguments go through `QProcess`'s argument list, never through a
  shell. There's no command injection surface from item filenames or
  launch parameters.

If a launch fails, an error dialog reports which step failed (path
validation, executable check, process start) and the reason.

## Recipes

### Run an item in a terminal

If your launcher is CLI-only, wrap it in a terminal:

```bash
#!/bin/sh
# /usr/local/bin/kartend-cli-tool
exec konsole -e /usr/bin/some-cli-tool "$1"
```

### Pick a launcher based on filename

If the same collection has files of different types (e.g. `.pdf` and
`.cbz` mixed), use a dispatcher script as the launcher:

```bash
#!/bin/sh
# /usr/local/bin/kartend-multi-launcher
case "$1" in
  *.pdf) exec /usr/bin/okular "$1" ;;
  *.cbz) exec /usr/bin/krita "$1" ;;
  *)     exec xdg-open "$1" ;;
esac
```

### Pass shell-style flags through Kartend

`launchParameters` is split into argv on whitespace. To embed a literal
space inside one argument, use a wrapper:

```ini
launchParameters=--title MyApp
```

passes two args: `--title` and `MyApp`. To pass `My App` as a single
argument you'd need a wrapper script.

### Run a libretro core directly with a custom build of RetroArch

```ini
launcherPath=/home/me/builds/retroarch
corePath=/home/me/builds/cores/some_libretro.so
launchParameters=--fullscreen --verbose
```

## Launch command preview (dry-run)

When a launch fails silently or behaves unexpectedly, **Launch
Preview** shows you exactly what Kartend *would* run without actually
running it. Open via the per-item right-click menu → **Preview launch
command…**.

```
┌────────────────────────────────────────────────────────────┐
│ Launch preview                                             │
│                                                            │
│ Program: /usr/bin/mpv                                      │
│ Arguments:                                                 │
│   --fs                                                     │
│   --really-quiet                                           │
│   "/home/me/Videos/Films/Some Movie (2021).mkv"            │
│ Working directory: /home/me/Videos/Films                   │
│                                                            │
│ Warnings                                                   │
│   ⚠  Argument 2 contains shell metacharacters — Kartend    │
│      will pass it without quoting; the launcher must       │
│      handle it.                                            │
│                                                            │
│                                              [ Close ]     │
└────────────────────────────────────────────────────────────┘
```

What it shows:

- **Program** — the resolved launcher executable.
- **Arguments** — the full argv as Kartend would hand it to
  `QProcess::start`, with one argument per line. No shell expansion
  is performed; what you see is what gets passed.
- **Working directory** — the cwd `QProcess` will use (typically the
  item's media directory).
- **Archive extraction** — when `extractArchives=true` and the item
  is a `.zip` / `.7z` / `.rar`, the preview adds the resolved
  extracted-file path that Kartend would launch instead.
- **Warnings** — validation flags surfaced before the actual launch:
  missing file, launcher not found, suspicious characters,
  launcher-flag conflicts.

Useful when iterating on launcher arguments (you don't want to keep
firing a real launch just to verify the command line), and as a
self-service first stop when "the item won't launch" — the warnings
list usually explains why.

## Common pitfalls

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Nothing happens on `Enter` | Empty `launcherPath` | Set the launcher in Settings → Launcher. |
| "Launcher not executable" | Launcher path isn't `+x` | `chmod +x /path/to/launcher` or pick a different launcher. |
| Launcher opens but the file doesn't load | Launcher expects the file at a non-final position | Use a wrapper script that re-arranges arguments. |
| Item with `&` or spaces in name fails | (Unlikely — should work. Check the launcher's own escape rules.) | Confirm the launcher's CLI handles special characters; wrap if not. |
| RetroArch launches without a core | `corePath` empty or filename doesn't contain "retroarch" | Set `corePath`; verify the launcher path includes "retroarch". |
| Archive launches the `.zip` instead of the contained file | `extractArchives=false` or `extractedExtension` wrong | Enable extraction and set the right extension. |
| "Always launch with…" missing from menu | Collection only has one launcher | Add at least one additional launcher. |

## For developers

- Launch pipeline: [src/modules/input/launch/](../../src/modules/input/launch/)
  (`LaunchManager`, validators, `LaunchUtils`).
- Preset resolution: `LauncherUtils::resolvePreset(config, presets)` in
  [src/utils/app/collectionutils.h](../../src/utils/app/collectionutils.h)
  — call this before reading path/core/params from a launcher entry.
- libretro detection: `LauncherUtils::usesLibretroCore(path)`.
- Per-item override storage: `item_metadata.launcher_index` in SQLite
  (see [Item Metadata](Item-Metadata.md#for-developers)).
- TOCTOU re-validation: `LaunchManager::launchItem()` calls
  `validateLaunchPath()` twice — once at queue time, once immediately
  before `QProcess::start`.
- Sensitive-directory blacklist: `LaunchPathValidator` in the launch
  module.
- Adding a new launcher field (e.g. environment variables): extend
  `LauncherConfig` in
  [src/utils/app/collectionutils.h](../../src/utils/app/collectionutils.h),
  add UI in `launchereditordialog`, propagate through serialization in
  `settingsmanager`, and update
  [Configuration Reference](Configuration-Reference.md).
