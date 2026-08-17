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

`.kart` packages contain one collection's configuration, a subset of
its per-item metadata, its static and smart playlists, and the actual
media, artwork, video and manual files keyed under the bundle's virtual
layout. Media is always included — there is no "config only" export
today. Each file payload is hashed (SHA-256) and, where the file type
is worth compressing, zstd- or zlib-compressed. The reader accumulates
the hash while streaming the payload into a temporary file and only
commits it once the hash matches, so a corrupted entry never lands on
disk.

> **Where to find this** — File menu → **Import .kart Package…** /
> **Export Collection…**. Headless: `kartend --import-kart` /
> `--export-kart` (see [CLI Reference](CLI-Reference.md)).

## What's inside a `.kart` package

A `.kart` file is a length-prefixed, append-only container. The on-disk
spec is in [docs/dev/kart-format.md](../dev/kart-format.md); the high-level
shape is:

| Section | Contents |
|---------|----------|
| Magic + version | 8-byte header; reader rejects mismatched magic |
| Manifest | UTF-8 JSON: collection config, launcher presets, item list, static + smart playlists, format/schema versions |
| Entries | Per-file payloads (media, artwork, video, manual). Each has flags, compression code, path, hashes, and size headers |

Bundled per item — exactly these fields, no others:

- `title`, `description`, `genre`, `developer`, `publisher`,
  `release_date`, `content_rating`, `players`, `runtime_seconds`
- `tags`, `custom_fields`
- `manual_path`, `launcher_index`, `source`

> **Not bundled per item, and lost on a round-trip:** notes, rating,
> source URL, and the pinned / hidden / continue-later state flags.
> Per-item **artwork links** are not bundled either — the `item_artwork`
> table is not touched by the kart format at all. If any of those
> matter to you, back up `media.db` as well; a `.kart` is not a
> substitute. (Tracked as a defect, not a design choice.)

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
- Per-item artwork links (`item_artwork` rows) — see the box above
- Scraper credentials (stored in the OS keychain — see [Keychain](Keychain.md))

Auto-discovered artwork *is* bundled: for each item the exporter looks
for one sibling image in the artwork directory's **root** and includes
it. Note that scraped artwork lands in typed subdirectories
(`front/`, `screenshot/`, …), which that root-only search does not
reach — so a collection whose covers came from the scraper exports
without them.

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
success, `2` on any failure — including an unknown collection name
(the match is case-sensitive on the display name). Kartend itself never
returns `1`. Useful in cron jobs and backup scripts.

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

Export is **strictly one collection per package**. Subcollections are
*not* included — the exporter takes a single `CollectionConfig` and
enumerates its media directory non-recursively. To move a tree, export
each collection in it separately.

All hierarchy is flattened on the way out: the primary parent link and
any linked-parent references are cleared at export and again at
import, so an imported collection always arrives at the root. Rebuild
the hierarchy afterwards; see
[Collections → Hierarchies](Collections.md#hierarchies-parents-and-subcollections).

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

Headless. Exit `0` on success, `2` on failure (bad path, unknown
policy, read error). A skipped item is **not** a failure — it is the
policy working as asked, so a `skip` run that skipped everything still
exits `0`. The CLI path skips the preflight dialog; the reader's own
validation (magic, per-entry hash, size caps, suspicious-path refusal)
still runs, and a refusal shows up in the exit code instead of a
dialog.

## Kart Preflight Dialog

New on this branch. Sits between picking a `.kart` and committing the
import so you can review the bundle before any disk writes happen.

The dialog shows:

| Section | Contents |
|---------|----------|
| Bundle summary | Collection name, collection type, item count, launcher count, and whether the bundle carries artwork overrides or per-item metadata |
| Launcher configuration | Every launcher setting the bundle brings — launcher path, core path and launch parameters, for the primary launcher and each additional one — with the value shown exactly as it will be used |
| Concerns | Warnings flagged by the preflight check (see below) |

Validation surfaces these concerns when present:

- **Launcher configuration** — listed whenever the bundle carries one,
  because a `.kart` chooses both the program to start and the arguments
  it is started with. Each row says why it is there: *Chosen by the
  bundle* for the ordinary case, or one of *Outside the safe allowlist*
  (`$HOME`, `/usr/bin`, `/usr/local/bin`, `/opt`), *Shell or
  interpreter*, *Runs an inline command* and *Shipped inside the bundle*
  when the setting reads as unusual. The last of those covers launcher
  paths, cores and path-like arguments alike.
- **Missing launcher** — a referenced launcher executable isn't on
  this machine
- **Suspicious paths** — icon and placeholder paths in the bundle's
  manifest that fall outside the safe-prefix allowlist
- **Name conflict** — a collection with the same name already exists
  (the merge dialog handles this if you proceed)

A bundle that carries a launcher configuration also asks you to confirm
it explicitly before the collection is registered, on every import route
— including drag-and-drop, which does not open the preflight dialog. The
confirmation lists the same rows and defaults to **Cancel**.

> **A clean preflight is not a safety guarantee.**
>
> The preflight check is an **advisory summary, not a sandbox**. It
> reports what it recognises, and passing it means "nothing
> known-suspicious was spotted" — not "this bundle has been vetted".
> The all-clear banner is only ever shown for a bundle that asks to run
> nothing at all; a bundle with any launcher setting is always shown to
> you rather than approved on your behalf.
>
> The extractor does independently refuse unsafe *payload* file paths:
> `..` traversal, absolute paths and symlink escapes are rejected
> regardless of the dialog choice. That protects where the bundle's
> files land. What the bundle asks Kartend to **run** is your decision
> to make at the confirmation above — see the warning at the top of this
> page.
>
> Prefer an empty destination directory, so an import cannot land on
> top of files you already have.

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

The conflict policy decides what happens to **per-item metadata rows**
that already exist for the same item. It does **not** decide anything
about the collection itself.

A colliding *collection name* is never skipped and never replaced. The
importer always creates a new collection, de-duplicating the name —
`Films` becomes `Films (2)`, then `Films (3)`. Your existing collection
is untouched under every policy. If you meant to replace one, delete it
first and then import.

| Policy | Effect on an item that already has metadata |
|--------|---------------------------------------------|
| `skip` (default) | Leave the existing row alone; discard the incoming one. |
| `overwrite` | Replace the existing row wholesale with the incoming one. |
| `merge` | Combine field by field. |

The CLI applies one policy to every item in the import. The GUI asks
per item, with an "apply to all remaining" escape hatch.

### Kart Merge Dialog

When importing through the UI and an item already carries metadata,
this dialog appears — **once per conflicting item**, not once per
collection:

```
┌────────────────────────────────────────────────────────────┐
│ Item already exists                                        │
│                                                            │
│ /home/me/Videos/Films/Some Film.mkv                        │
│                                                            │
│           existing            incoming        use incoming │
│  Title    Some Film           Some Film (2021)      ☑      │
│  Genre    Drama               Drama, Thriller       ☐      │
│  Tags     watched             watched, hd           ☑      │
│  …                                                         │
│                                                            │
│  ☐ Apply this choice to all remaining conflicts            │
│                                                            │
│   [ Skip ]        [ Overwrite ]        [ Merge… ]          │
└────────────────────────────────────────────────────────────┘
```

- The grid has one row per metadata field (title, description, genre,
  developer, publisher, release date, content rating, players,
  runtime, tags, custom fields, manual path, launcher override,
  source), each with a "use incoming" checkbox that **Merge…** honours.
- **Skip** and **Overwrite** ignore the checkboxes and take one side
  whole.
- **Apply this choice to all remaining conflicts** turns your answer
  into the policy for the rest of the import.

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
build time (recommended; see [building.md](../dev/building.md)) or **zlib
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

This does **not** replace the broken collection — it imports alongside
it as `Films (2)`. Delete the broken one first if you want the name
back; `--on-conflict` governs per-item metadata rows, not collections.

Tags, custom fields, scraper-sourced fields, manual paths and playlists
return intact. Notes, ratings, state flags and artwork links do
**not** — see [What's inside a `.kart` package](#whats-inside-a-kart-package). Launch history is
not backed up by the kart format either, so play counts and timestamps
stay
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
- Preflight: `KartPreflight::buildReport(manifest, trustedLauncherPaths,
  existingCollectionNames)` collects the summary + concerns; `KartSuspiciousPaths` flags `..`-traversal and
  outside-prefix targets. The dialog is
  [src/ui/dialogs/kart/kartpreflightdialog.h](../../src/ui/dialogs/kart/).
- Compression: zstd via `KARTEND_HAS_ZSTD` (CMake-time toggle); zlib
  fallback uses Qt's built-in `qCompress`.
- Dialogs: [src/ui/dialogs/kart/](../../src/ui/dialogs/kart/) —
  `kartmergedialog.h`, `kartprogressdialog.h`, `kartpreflightdialog.h`.
- CLI entry point: [src/core/main.cpp](../../src/core/main.cpp). The
  kart flags are registered inline there, not in
  [src/utils/app/cliargs.cpp](../../src/utils/app/cliargs.cpp) (that
  shim covers the startup-argument path).
- File format spec: [docs/dev/kart-format.md](../dev/kart-format.md).
- Playlist round-trip: see `KartManifest` (static + smart playlist
  serialization) and `KartManager::importBundle` for the rebind step
  that re-keys members against the imported collection UUIDs.
- Adding a new payload section (e.g. launch history): extend the
  manifest schema, add a writer phase, add a matching reader phase,
  version-bump the manifest.
- Conflict-resolution merge logic: `kart::mergeItemMetadata()` for the
  field-by-field rules and `kart::persistImportedMetadata()` for the
  per-row dispatch, both in `kartmerge.cpp`.
