# Backup & Migration (`.kart` packages)

Kartend bundles collection configuration, per-item metadata, playlists,
and (optionally) the underlying media into a single portable archive —
a **`.kart` package** — so you can back up your library or move it
between your own machines.

> **`.kart` packages are a personal backup format.** They embed
> absolute paths, scraper credentials are tied to the originating
> install's keychain, and item metadata may include private notes,
> ratings, and source URLs. The import path also accepts arbitrary
> launcher executables and file paths from the package, so opening a
> `.kart` you didn't produce is equivalent to running an unknown
> script. Treat your karts the same way you'd treat your config
> directory — keep them on machines you own.

`.kart` packages contain configuration, per-item metadata (custom
fields, notes, tags, rating, manual paths, artwork links, pinned /
hidden / continue-later flags), static and smart playlists, and — when
asked — the actual media, artwork, video, and manual files keyed under
the bundle's virtual layout. Each file payload is hashed (SHA-256) and
typically zstd-compressed; readers verify the hash before extracting.

> **Where to find this** — File menu → **Import .kart Package…** /
> **Export Collection…**. Headless: `kartend --import-kart` /
> `--export-kart` (see [CLI Reference](CLI-Reference.md)).

## What's inside a `.kart` package

A `.kart` file is a length-prefixed, append-only container. The on-disk
spec is in [docs/kart-format.md](../kart-format.md); the high-level
shape is:

| Section | Contents |
|---------|----------|
| Magic + version | 8-byte header; reader rejects mismatched magic |
| Manifest | UTF-8 JSON: collection config, launcher presets, item list, static + smart playlists, format/schema versions |
| Entries | Per-file payloads (media, artwork, video, manual). Each has flags, compression code, path, hashes, and size headers |

Bundled per item:

- Per-item metadata: custom fields, notes, tags, rating, source URL,
  scraper-sourced fields (title, description, genre, developer, …)
- State flags: pinned, hidden, continue-later
- Manual file overrides + artwork links

Bundled per collection:

- Full `CollectionConfig` — every leaf cluster (grid layout, sidebar
  appearance, background, list-view options, scraper overrides,
  launcher profile)
- Launcher presets referenced by the collection
- Static playlists (with item references resolved against the bundle's
  paths)
- Smart playlists (the filter spec round-trips verbatim)

Not bundled:

- Launch history (`launch_history` rows)
- Play counts and last-played timestamps
- Global `[General]` settings (export is per-collection)
- Auto-discovered artwork unless its file is included in the payload
  (manual links are preserved by reference)
- Scraper credentials (stored in the OS keychain — see [Keychain](Keychain.md))

> **Path caveat** — paths inside the export are absolute by default.
> When importing on a machine with a different home directory or
> media layout, you'll need to point the imported collection at the
> new location (Settings Dialog → Paths tab, or the **Destination**
> field in the import dialog).

## Exporting

### From the UI

1. **File → Export Collection…**
2. A file picker asks where to save the `.kart`.
3. Pick the target collection if prompted (or right-click a collection
   tile → **Export Collection**).
4. Save.

The exported file is named `<collection-name>.kart` by default. A
progress dialog runs while entries are written; cancellation rolls
back to the original on-disk state via `QSaveFile`.

### From the command line

```bash
kartend --export-kart "Films" --export-out ~/backups/films.kart
```

| Flag | Description |
|------|-------------|
| `--export-kart <name>` | The collection name to export (display name; case-sensitive). |
| `--export-out <path>` | Destination file. Required. |

Runs headlessly (no GUI window) and exits when done. Exit code `0` on
success; `1` if the named collection isn't found; `2` for argument
errors. Useful in cron jobs and backup scripts.

```bash
# Daily backup to a dated archive
DATE=$(date +%Y-%m-%d)
mkdir -p ~/backups/kartend
for collection in "Films" "Albums" "Manuals"; do
  kartend --export-kart "$collection" \
          --export-out ~/backups/kartend/${collection// /_}-$DATE.kart
done
```

### Export scope

Exporting a collection includes its **subcollections** automatically.
There's no way today to export just a parent without its children;
this matches how `.kart` packages are designed (collection groups
travel together).

Linked-parent references to *other* collections that aren't in the
export are flattened — the imported collection has its primary parent
preserved (if also in the export, otherwise reparented to root) but
loses cross-references that would dangle.

## Importing

### From the UI

1. **File → Import .kart Package…**
2. File picker for the `.kart`.
3. The [Kart Preflight Dialog](#kart-preflight-dialog) opens, showing
   bundle metadata and any concerns; review and confirm.
4. If the bundle's collection names collide with existing ones, the
   [Kart Merge Dialog](#kart-merge-dialog) resolves per-collection.
5. After import, the new collection appears in the tree (typically at
   the destination you chose, or root by default).

A **destination directory** can be picked during import — Kartend may
update the imported collection's `mediaDirectory` to point at this
destination if it doesn't exist on the target system. Useful when
moving between your own machines with different paths.

### From the command line

```bash
kartend --import-kart ~/backups/films.kart \
        --to ~/Videos/Films \
        --on-conflict overwrite
```

| Flag | Description |
|------|-------------|
| `--import-kart <path>` | Path to the `.kart` file. |
| `--to <dir>` | (Optional) Destination directory; updates `mediaDirectory` to this path if originally pointed elsewhere. |
| `--on-conflict <policy>` | `skip` (default) / `overwrite` / `merge`. See [policies](#conflict-policies). |

Headless. Exit `0` on success; `1` on conflict (if policy is `skip`
and a conflict was hit); `2` for argument errors. The CLI path skips
the preflight dialog — it still validates the bundle, but a refusal
shows up in the exit code instead of a dialog.

## Kart Preflight Dialog

New on this branch. Sits between picking a `.kart` and committing the
import so you can review the bundle before any disk writes happen.

The dialog shows:

| Section | Contents |
|---------|----------|
| Bundle summary | Name, author (if set), description, schema version, source machine identifier, created-at timestamp, manifest UUID |
| Collections | Display name + item count for each collection in the bundle |
| Launchers | Resolved launcher paths referenced by the bundle and whether they exist on this machine |
| Concerns | Warnings flagged by the preflight check (see below) |

Validation surfaces these concerns when present:

- **Missing launcher** — a referenced launcher executable isn't on
  this machine
- **Suspicious paths** — file paths inside the bundle that escape the
  collection's content roots (e.g. `..` traversal, absolute paths
  outside the safe prefix). The bundle's payload extractor refuses
  these regardless of the dialog choice
- **Name conflict** — a collection with the same name already exists
  (the merge dialog handles this if you proceed)
- **Schema mismatch** — bundle was written by a newer Kartend than
  this one understands; affected fields are listed

You can **Accept** to continue (which then hands off to the merge
dialog if needed) or **Reject** to cancel without touching the
database or filesystem.

```
┌────────────────────────────────────────────────────────────┐
│ Review .kart Package                                       │
│                                                            │
│  Bundle: Films (1.0)                                       │
│  Created: 2026-05-21 14:02:00Z   Items: 248                │
│                                                            │
│  Concerns                                                  │
│    ⚠  Launcher /usr/bin/mpv-old not found on this system   │
│    ⚠  Collection name 'Films' already exists               │
│                                                            │
│  [ Reject ]                                  [ Continue ]  │
└────────────────────────────────────────────────────────────┘
```

## Conflict policies

When the importing collection's name collides with an existing one,
the policy decides what to do:

| Policy | Effect |
|--------|--------|
| `skip` (default) | Don't import the conflicting collection. The existing one is untouched. |
| `overwrite` | Replace the existing collection's settings + per-item metadata with the imported values. Items whose paths exist on disk under the imported settings get the imported metadata; items only in the existing collection are dropped. |
| `merge` | Combine: existing settings stay, imported settings fill in any blanks. Per-item metadata: imported wins on conflict (so an imported note replaces an existing one for the same item). |

Each strategy applies per-collection in the import; a multi-collection
package can be partially overwritten and partially skipped (the dialog
in the GUI mode lets you pick per-conflict; CLI uses the same policy
for all conflicts in the import).

### Kart Merge Dialog

When importing through the UI and a conflict is detected after
preflight, this dialog appears:

```
┌────────────────────────────────────────────────────────────┐
│ Conflicts found in import                                  │
│                                                            │
│ The package contains collections with the same name as     │
│ existing ones:                                             │
│                                                            │
│   ☑ Films               [ Skip ▾ ]                         │
│   ☑ Albums              [ Overwrite ▾ ]                    │
│   ☐ Audiobooks          [ — / new — ]                      │
│                                                            │
│         [ Cancel ]              [ Continue ]               │
└────────────────────────────────────────────────────────────┘
```

- Each conflicting collection has its own dropdown (Skip / Overwrite /
  Merge).
- Non-conflicting collections (greyed checkbox) are imported as new.
- Cancel aborts the entire import.

## Kart Progress Dialog

For long-running imports / exports, a progress dialog with a status
message appears. Operations are typically fast (seconds, not minutes)
unless the package is unusually large or bundles a lot of media.

| Element | Detail |
|---------|--------|
| Progress bar | Determinate; reflects items / entries processed |
| Status text | Current step (`Reading manifest`, `Importing items`, `Restoring playlists`, etc.) |
| Cancel | Aborts the operation; partial state is rolled back |

## Playlists in `.kart` packages

New on this branch. Static and smart playlists are now serialized
into the manifest and restored on import:

- **Static playlists** are stored with the parent collection's UUID
  plus each item's source-collection UUID and source path. On import,
  Kartend re-keys them to the rebuilt collection UUIDs and matches
  members against the newly imported items.
- **Smart playlists** carry their full `smart_filter` JSON spec. The
  filter round-trips verbatim — re-evaluation happens on the next
  open, against the imported items.
- **Favorites** is treated as a reserved playlist; its membership
  rows ride along.

Items that can't be matched on import (because the source-collection
UUID isn't in the bundle, or because no file resolved to the recorded
path) are silently dropped — they don't become ghost rows.

## Compression

`.kart` packages use **zstd compression** if zstd was available at
build time (recommended; see [building.md](../building.md)) or **zlib
fallback** otherwise. Readers detect the codec per-entry from the
header byte. zstd is faster and typically smaller; zlib is universal.

Distro packagers should add zstd as a dependency to enable the faster
path. See [readme.md](../../readme.md#dependencies).

## Recipes

### Daily backup of all collections

There's no "export everything" command today. Closest workaround:

```bash
#!/bin/sh
# Get a list of collection names from the config file
collections=$(awk -F'[][]' '/^\[/ && !/^\[General\]/ {print $2}' \
              ~/.config/kartend/kartend.cfg)

DATE=$(date +%Y-%m-%d)
mkdir -p ~/backups/kartend/$DATE
for c in $collections; do
  kartend --export-kart "$c" \
          --export-out ~/backups/kartend/$DATE/"${c// /_}".kart
done
```

A bulk-export feature is on the wishlist.

### Migrate to a new machine

On the source:

```bash
# Export each collection
for c in "Films" "Albums" "Manuals"; do
  kartend --export-kart "$c" --export-out ~/$c.kart
done
# Copy media + artwork directories separately (rsync / external drive)
rsync -avh ~/Videos/    new-machine.local:/home/me/Videos/
rsync -avh ~/Music/     new-machine.local:/home/me/Music/
rsync -avh ~/Documents/ new-machine.local:/home/me/Documents/
# Copy the .kart files
scp ~/*.kart new-machine.local:~/
```

On the target:

```bash
for k in ~/*.kart; do
  kartend --import-kart "$k" --to ~/Media  # or wherever
done
```

Then open Kartend → Settings → Paths tab on each imported collection
to fix `mediaDirectory` / `artworkDirectory` if they differ.

### Restore from a snapshot

After a database wipe or a bad rescan:

```bash
# Re-import yesterday's backup, overwriting the (broken) current state
kartend --import-kart ~/backups/kartend/2026-05-23/Films.kart \
        --on-conflict overwrite
```

Per-item state (notes, tags, ratings, custom fields, manual links,
artwork links, playlists) returns intact. Launch history is *not*
backed up by the kart format, so play counts and timestamps stay
whatever they are on the current database.

### Versioned configuration backups in git

`.kart` files are binary archives — not git-friendly. To version
configuration, copy `~/.config/kartend/kartend.cfg` into a git repo
instead. Per-item state lives in the database; for git-friendly state
backups you'd need to dump SQLite to text (e.g. via `.dump`) and
commit that.

A future plain-text export format is a wishlist item.

## Where to next

- [CLI Reference](CLI-Reference.md) — full headless command list
- [Configuration Reference](Configuration-Reference.md) — what's in
  the INI portion of a `.kart`
- [File Locations](File-Locations.md) — where the source data lives
- [Playlists & Favorites](Playlists-and-Favorites.md) — playlists
  ride along inside `.kart` packages; they also have their own
  JSON / M3U export for the items-only case

## For developers

- Manager: [src/modules/data/kart/](../../src/modules/data/kart/) (`KartManager`,
  `KartReader`, `KartWriter`, `KartManifest`, `KartMerge`, `KartPreflight`).
- Preflight: `KartPreflight::inspect` collects bundle summary +
  concerns; `KartSuspiciousPaths` flags `..`-traversal and
  outside-prefix targets. The dialog is
  [src/ui/dialogs/kart/kartpreflightdialog.h](../../src/ui/dialogs/kart/).
- Compression: zstd via `KARTEND_HAS_ZSTD` (CMake-time toggle); zlib
  fallback uses Qt's built-in `qCompress`.
- Dialogs: [src/ui/dialogs/kart/](../../src/ui/dialogs/kart/) —
  `kartmergedialog.h`, `kartprogressdialog.h`, `kartpreflightdialog.h`.
- CLI entry point: [src/core/main.cpp](../../src/core/main.cpp);
  parsing in `cliargs.cpp`.
- File format spec: [docs/kart-format.md](../kart-format.md).
- Playlist round-trip: see `KartManifest` (static + smart playlist
  serialization) and `KartManager::importBundle` for the rebind step
  that re-keys members against the imported collection UUIDs.
- Adding a new payload section (e.g. launch history): extend the
  manifest schema, add a writer phase, add a matching reader phase,
  version-bump the manifest.
- Conflict-resolution merge logic: see `KartMerge::applyPolicy` for
  the field-by-field rules under each policy.
