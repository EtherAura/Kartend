# CLI Reference

Kartend is primarily a GUI app, but it accepts a small set of
command-line options for headless workflows: opening a specific
collection on launch, importing or exporting `.kart` packages from a
script, and the standard Qt help / version flags.

For environment variables (logging, diagnostics) see
[Logging & Diagnostics](Logging-and-Diagnostics.md).

## Synopsis

```
kartend [options]
```

| Option | Form | Description |
|--------|------|-------------|
| `--help` / `-h` | flag | Print usage and exit. |
| `--version` / `-v` | flag | Print version and exit. |
| `-c <name>` / `--collection <name>` | takes value | Open the named collection on launch (bypasses `[General] startupCollection`). Falls back to the default if unknown. |
| `--import-kart <path>` | takes value | Import a `.kart` package headlessly. Implies exit on completion. |
| `--to <dir>` | takes value | Destination directory for `--import-kart`. Default: `~/imported-kart`. |
| `--on-conflict <policy>` | takes value | Conflict policy for `--import-kart`: `skip` (default) / `overwrite` / `merge`. |
| `--export-kart <name>` | takes value | Export the named collection headlessly. Implies exit on completion. |
| `--export-out <path>` | takes value | Output path for `--export-kart`. **Required** when exporting. |
| `--allow-untrusted-launcher` | flag | Permit `--import-kart` to register a launcher setting that points *inside* the extracted kart tree — a package that ships the thing it wants run, whether that is named as the launcher, as the core, or as a path argument. Off by default: a headless import has no one to ask, so it refuses rather than silently wiring up a stranger's file. |

Standard Qt options (`--platform`, `--style`, `--stylesheet`, etc.) are
also accepted but rarely used in practice. See the Qt documentation
for the full list.

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success — including a normal interactive launch ending with `Ctrl+Q` or window-close, and a run terminated by `KARTEND_SMOKE_TEST_EXIT_MS` (the smoke-test hook quits gracefully so sanitizers see a normal teardown). |
| `1` | Emitted by Qt's own argument parser for an unrecognized option, before Kartend's code runs. Kartend itself never returns 1. |
| `2` | Kartend's own validation or a headless operation failed: collection not found, unknown `--on-conflict` policy, missing `--export-out`, unsafe path, kart read/write error. |

## Examples

### Open Kartend straight into a specific collection

```bash
kartend --collection "Films"
kartend -c "Albums"
```

Equivalent to setting `[General] startupCollection=Films`, but
applies for one launch only without modifying the config file.

If the named collection doesn't exist, Kartend logs a warning and
falls back to the configured default.

### Headless: export a collection

```bash
kartend --export-kart "Films" --export-out ~/backups/films.kart
```

Both flags required. Exit `0` on success; `2` on failure (collection
not found, output path unwritable, etc.).

Progress lines and the closing summary both go to **stderr**, not
stdout — so `2>/dev/null` silences them, and a script that only reads
stdout will see nothing at all. Check the exit code, not the output.

### Headless: import a collection

```bash
kartend --import-kart ~/backups/gb.kart
```

Default destination: `~/imported-kart`. Default conflict policy:
`skip`. Override with `--to` and `--on-conflict`:

```bash
kartend --import-kart ~/backups/films.kart \
        --to ~/Videos/Films \
        --on-conflict overwrite
```

| Policy | Effect on name collision |
|--------|--------------------------|
| `skip` | Don't import the conflicting collection. Existing one preserved. |
| `overwrite` | Replace the existing collection. |
| `merge` | Combine — existing fields stay, imported fills in blanks. |

See [Backup & Migration → Conflict policies](Backup-and-Migration.md#conflict-policies)
for the full semantics.

### Standard Qt options

```bash
# Use a specific Qt platform plugin (rarely needed)
QT_QPA_PLATFORM=wayland kartend

# Force a specific Qt style
kartend -style fusion

# Use offscreen for testing (no display required)
QT_QPA_PLATFORM=offscreen kartend
```

`QT_QPA_PLATFORM=offscreen` is what CI uses for unit and integration
tests — see [testing.md](../dev/testing.md).

## Scripts and integration

### Daily backup

```bash
#!/bin/sh
set -euo pipefail

DATE=$(date +%Y-%m-%d)
DEST=~/backups/kartend/$DATE
mkdir -p "$DEST"

# Pull collection names from the config file
collections=$(awk -F'[][]' '/^\[/ && !/^\[General\]/ {print $2}' \
              ~/.config/kartend/kartend.cfg)

while IFS= read -r c; do
  [ -z "$c" ] && continue
  out="$DEST/${c// /_}.kart"
  kartend --export-kart "$c" --export-out "$out"
done <<< "$collections"

echo "Backup complete in $DEST"
```

A daily-cron invocation produces a dated tree of `.kart` exports.

### Bulk import on a fresh install

```bash
#!/bin/sh
set -euo pipefail

for k in ~/restore/*.kart; do
  kartend --import-kart "$k" --to "$HOME/Media" --on-conflict skip
done
```

Iterates through `.kart` files; skip any that conflict (so reruns are
idempotent — already-present collections are left alone).

### Per-collection desktop entry

If you want a launcher icon that goes straight into a specific
collection:

```desktop
[Desktop Entry]
Name=Kartend — Films
Exec=kartend --collection "Films"
Icon=io.github.EtherAura.Kartend
Type=Application
Categories=Utility;Qt;
```

Drop in `~/.local/share/applications/`. Shows up in your application
menu as a separate entry.

### Verify a `.kart` file before importing

There's no `--validate` flag today. Closest workaround: import to a
throwaway directory with `skip` policy:

```bash
TMP=$(mktemp -d)
kartend --import-kart suspect.kart --to "$TMP" --on-conflict skip
status=$?
echo "Exit: $status"
rm -rf "$TMP"
```

Exit `0` means the package was readable. Note this is a real import,
not a dry run — `--on-conflict skip` only governs *name collisions*, so
the package genuinely lands in `$TMP`. That's why the recipe throws the
directory away afterwards.

### Headless mode and HOME

Headless invocations still read your config and database from
`~/.config/kartend/` and `~/.local/share/kartend/`. To run against a
sandbox `HOME`:

```bash
HOME=/tmp/kartend-test kartend --import-kart suspect.kart
```

Useful for dry-runs that don't touch your real configuration.

## Limitations

- No `--validate` / dry-run mode.
- No bulk export ("export all collections").
- No JSON output for stats / collection listings (everything is
  imperative).
- No flag to skip the splash screens for headless.
- No `--config <path>` flag (Kartend honors `XDG_CONFIG_HOME` if you
  want to direct the config search; that's the workaround).

For any of these, file a feature request — the parser is
[QCommandLineParser](https://doc.qt.io/qt-6/qcommandlineparser.html)
based and cheap to extend.

## For developers

- Parsing: [src/core/main.cpp](../../src/core/main.cpp). Uses
  `QCommandLineParser::process()` so the standard Qt help / version /
  unknown-option semantics apply.
- A unit-testable parser shim lives in
  [src/utils/app/cliargs.cpp](../../src/utils/app/cliargs.cpp) — the inline
  parser in `main.cpp` is meant to mirror it but uses `process()` for
  real CLI behavior. The two are kept in sync by hand, so check both
  when auditing the option set; `--allow-untrusted-launcher` is
  currently registered only in `main.cpp`.
- Adding a new flag:
  1. Add the `QCommandLineOption` definition to `main.cpp`.
  2. Mirror it in `cliargs.cpp` so it can be unit-tested without
     spinning up `QApplication`.
  3. Wire the new behavior (call into the relevant manager).
  4. Add a row to the table at the top of this page and a worked
     example below it.
- Headless export is built inline in `main.cpp`: it resolves the
  collection UUID via `CollectionUtils::computeCollectionUuid`, then
  calls `KartWriter::prepareFromCollection()` and
  `KartWriter::Writer::writeKart()`. `KartManager` is not involved.
- Headless import entry point: `KartManager::importKartHeadless`.
- Tests for arg parsing: `tests/utils/app/test_cliargs.cpp`.
