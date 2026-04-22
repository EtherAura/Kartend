# Troubleshooting

Solutions to common issues. If your problem isn't listed, file an issue at
https://github.com/EtherAura/Kartend/issues with the relevant log excerpt.

## Logs

Kartend logs to standard error. To capture output:

```bash
kartend 2>&1 | tee ~/kartend.log
```

For verbose `qDebug()` / `qWarning()` output, run a debug build:

```bash
.scripts/build.sh --debug
./build/ninja-debug/kartend
```

## Collection / Scanning Issues

### My collection is empty after pointing to a folder

| Likely cause | Fix |
|---|---|
| Wrong **Extensions** filter | Settings → Collection → set extensions to match your files (e.g. `pdf,epub`) or leave blank to accept all. |
| Folder is empty or contains only subfolders | Enable `includeContentSubfolders=true` to surface nested folders as virtual subcollections. |
| Hidden folders | Enable `showHiddenFolders=true` if your media lives in dot-prefixed directories. |
| Path typo or bad permissions | Verify the path exists and is readable: `ls -la "<mediaDirectory>"`. |

### "Media directory not found" error

The path saved in the config no longer exists, or `~` expansion failed.

- Confirm the directory still exists.
- Re-pick it from the Settings Dialog so the absolute path is re-saved.
- Avoid environment variables (`$HOME`, `$XDG_*`) in the config — only `~`
  is expanded.

### Force a rescan

Press `Ctrl + F5` to rescan a collection from disk. Use this after
adding/removing files outside Kartend.

## Artwork Issues

### Tiles show colored placeholders instead of cover art

| Cause | Fix |
|---|---|
| Filename mismatch | Artwork is matched by base filename. `game.sfc` needs `game.png` (or `.jpg` / `.jpeg` / `.webp`) in the artwork directory. |
| Wrong artwork directory | Settings → Collection → **Artwork Directory**. |
| Unsupported image format | Convert to `png`, `jpg`, `jpeg`, or `webp`. |
| Artwork in nested folders | Enable `includeArtworkSubfolders=true`. |
| Large artwork directory still loading | Artwork loads asynchronously in batches. Wait a few seconds after first opening a large collection. |

### Artwork looks fuzzy or pixelated

Tile size is set per-collection. Increase `itemWidth` / `itemHeight` (or
adjust in the Settings Dialog) — Kartend will re-render at the new size on
next view.

### Generate placeholders for subfolders

See [subfolder-artwork.md](subfolder-artwork.md).

## Launching Issues

### Nothing happens when I press Enter

| Cause | Fix |
|---|---|
| Empty `launcherPath` | Settings → Collection → set the launcher executable. |
| Launcher not executable | `chmod +x /path/to/launcher`, or use an absolute path to a known-good binary. |
| Launcher requires extra args | Set `launchParameters` (e.g. `--fullscreen`) and/or `corePath` for RetroArch-style launchers. |
| Item file path contains spaces | Kartend quotes the path automatically — no escaping needed in `launchParameters`. |

### Launcher opens but my file isn't loaded

Kartend appends the selected file's absolute path as the final argument to
the launcher. If your launcher expects the path elsewhere, wrap it in a small
shell script and point `launcherPath` at the script:

```bash
#!/bin/sh
exec /path/to/weird-launcher --media "$1"
```

### RetroArch / emulator cores

Use both `launcherPath` (the emulator binary) and `corePath` (the loadable
core / `.so` file). Kartend will pass `-L <corePath> <mediaFile>` to the
launcher.

## Performance

### Scrolling is sluggish on a huge collection

- Reduce `itemWidth` / `itemHeight` so more tiles fit in cache.
- Turn down animation: shorter durations live in
  [docs/constants.md](constants.md) and the source `uiconstants.h`.
- Make sure you're running a **release** build, not a debug build:
  `.scripts/build.sh` (no `--debug`).

### Artwork loading hammers the disk

The artwork pipeline batches loads (see `UIConstants::Artwork`). If you have
a slow disk, wait for the initial scan to settle — subsequent navigation
will hit the in-memory + disk cache.

### Cache grows too large

Pixmap cache size is bounded by `UIConstants::Cache::MEMORY_LIMIT_MB`. The
on-disk cache lives under `~/.cache/kartend/`; safe to delete when Kartend
isn't running.

## Database / Session

### "Database connection lost" warning

`QueryManager` will automatically reconnect (up to 3 attempts). If it
persists:

- Check `~/.local/share/kartend/` (or wherever your collection's SQLite file
  lives) for permission/disk-full issues.
- Close Kartend, move the `.db` file aside, and let Kartend rebuild on next
  launch (you'll lose the cached scan but no media or config).

### Selection or scroll position resets every launch

Set `rememberSelection=true` under `[General]`. If it's already true, ensure
`~/.config/kartend/` is writable.

## Keyboard / Input

### A shortcut doesn't work

- Check it hasn't been rebound under **Settings → General**.
- Some shortcuts only apply when an item is focused (move-selection keys);
  others are global (`F1`, `Ctrl+,`, `Ctrl+Q`).
- Press `F1` for the in-app reference of currently active bindings.

### Search bar won't open

`/` focuses the search bar — it must be visible. If you've toggled the
toolbar off (`F10`), turn it back on or use the menu's **View** entry.

## Diagnostic Logging

Kartend uses Qt logging categories so verbose tracing can be toggled at
runtime without rebuilding.

| Category | Default | Purpose |
|----------|---------|---------|
| `kartend.scanflow` | on (warning) | Always-visible flow markers for scan/load lifecycle |
| `kartend.searchdiag` | off | Search/filter pipeline diagnostics |
| `kartend.perftrace` | off | Per-operation timing samples |

Enable a category:

```bash
QT_LOGGING_RULES="kartend.searchdiag.debug=true" ./kartend
```

Multiple rules separated by `;`:

```bash
QT_LOGGING_RULES="kartend.perftrace.debug=true;kartend.searchdiag.debug=true" ./kartend
```

Or set everything at once:

```bash
KARTEND_LOG_RULES="kartend.*=true" ./kartend
```

Legacy environment variables remain supported and are bridged at startup:

| Env var | Equivalent rule |
|---------|-----------------|
| `KARTEND_PERF_TRACE=1` | `kartend.perftrace.debug=true` |
| `KARTEND_SEARCH_DIAG=1` | `kartend.searchdiag.debug=true` |
| `KARTEND_SCAN_DIAG=1` | `kartend.scanflow.debug=true` |

## Build / Install

For build, dependency, and install issues see [building.md](building.md).
