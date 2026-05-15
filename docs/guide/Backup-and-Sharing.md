# Backup & Sharing (`.kart` packages)

Kartend bundles collection configuration and item metadata into a
single portable archive — a **`.kart` package** — so you can back up
collections, transfer them between machines, or share them with another
Kartend user. The format is import-friendly with conflict policies
that handle name collisions on the receiving end.

`.kart` packages contain configuration and per-item state (custom
fields, manual paths, artwork links, history) but **not** the underlying
media or artwork files themselves — those stay where they are. Bundle
or sync the media separately if you're moving everything.

> **Where to find this** — File menu → **Import .kart Package…** /
> **Export Collection…**. Headless: `kartend --import-kart` /
> `--export-kart` (see [CLI Reference](CLI-Reference.md)).

## What's inside a `.kart` package

A `.kart` file is a zip-style container with:

| Section | Contents |
|---------|----------|
| Collection config | One INI section per included collection (same shape as `kartend.cfg`) |
| Per-item metadata | Custom fields, manual paths, launcher overrides — for items in the included collections |
| Per-item artwork links | Manual artwork-type → file links |
| Hierarchy | Parent / linked-parent relationships among included collections |
| Optional artwork (future) | Reserved for an opt-in artwork bundle (not enabled today) |

Not included:

- Media files
- Auto-discovered (filesystem-based) artwork — only manual links are
  preserved
- Launch history (`launch_history` rows)
- Play counts and last-played timestamps
- Global `[General]` settings (export is per-collection)

The omission of media and history is intentional — `.kart` packages
are *configuration-portable*, not *file-portable*. Use them to migrate
your library structure between machines without copying terabytes of
media.

> **Caveat** — paths inside the export are absolute by default. When
> importing on a machine with a different home directory or media
> layout, you'll need to update the imported collection's
> `mediaDirectory` / `artworkDirectory` (Settings Dialog → Paths tab).

## Exporting

### From the UI

1. **File → Export Collection…**
2. A file picker asks where to save the `.kart`.
3. Pick the target collection if prompted (or right-click a collection
   tile → **Export Collection** in some builds).
4. Save.

The exported file is named `<collection-name>.kart` by default.

### From the command line

```bash
kartend --export-kart "Films" --export-out ~/backups/films.kart
```

| Flag | Description |
|------|-------------|
| `--export-kart <name>` | The collection name to export (display name; case-sensitive). |
| `--export-out <path>` | Destination file. Required. |

The CLI form runs headlessly (no GUI window) and exits when done. Exit
code `0` on success; `1` if the named collection isn't found; `2` for
argument errors. Useful in cron jobs / backup scripts.

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
3. The [Kart Merge Dialog](#kart-merge-dialog) handles any conflicts.
4. After import, the new collection appears in the tree (typically at
   the destination you chose, or root by default).

A **destination directory** can be picked during import — Kartend may
update the imported collection's `mediaDirectory` to point at this
destination if it doesn't exist on the target system. Useful when
moving between machines with different paths.

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

Headless. Exit `0` on success; `1` on conflict (if policy is `skip` and
a conflict was hit); `2` for argument errors.

## Conflict policies

When the importing collection's name collides with an existing one,
the policy decides what to do:

| Policy | Effect |
|--------|--------|
| `skip` (default) | Don't import the conflicting collection. The existing one is untouched. |
| `overwrite` | Replace the existing collection's settings + per-item metadata with the imported values. Items whose paths exist on disk under the imported settings get the imported metadata; items only in the existing collection are dropped. |
| `merge` | Combine: existing settings stay, imported settings fill in any blanks. Per-item metadata: imported wins on conflict (so an imported manual link replaces an existing one for the same item). |

Each strategy applies per-collection in the import; a multi-collection
package can be partially overwritten and partially skipped (the dialog
in the GUI mode lets you pick per-conflict; CLI uses the same policy
for all conflicts in the import).

### Kart Merge Dialog

When importing through the UI and a conflict is detected, this dialog
appears:

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
unless the package is unusually large.

| Element | Detail |
|---------|--------|
| Progress bar | Determinate; reflects items processed |
| Status text | Current step (`Reading manifest`, `Importing items`, etc.) |
| Cancel | Aborts the operation; partial state is rolled back |

## Compression

`.kart` packages use **zstd compression** if zstd was available at
build time (recommended; see [building.md](../building.md)) or **zlib
fallback** otherwise. Files are interchangeable — Kartend's import
detects the compression format. zstd is faster and typically smaller;
zlib is universal.

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
rsync -avh ~/Videos/  user@new-machine:/home/user/Videos/
rsync -avh ~/Music/   user@new-machine:/home/user/Music/
rsync -avh ~/Documents/ user@new-machine:/home/user/Documents/
# Copy the .kart files
scp ~/*.kart user@new-machine:~/
```

On the target:

```bash
for k in ~/*.kart; do
  kartend --import-kart "$k" --to ~/Media  # or wherever
done
```

Then open Kartend → Settings → Paths tab on each imported collection
to fix `mediaDirectory` / `artworkDirectory` if they differ.

### Share a curated collection with a friend

Export, send the `.kart` file. The friend imports it. They'll need
matching media / artwork paths (or update them post-import).

For a fully self-contained share, bundle the `.kart` *plus* a tarball
of the media directory:

```bash
kartend --export-kart "My Curated Set" --export-out my-set.kart
tar czf my-set-media.tar.gz ~/Media/curated/
# Send my-set.kart + my-set-media.tar.gz to the friend.
```

The friend extracts the tar, then imports the `.kart` with `--to` set
to the extraction path.

### Versioned collection backups in git

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
  have their own export (M3U / JSON), separate from `.kart` packages

## For developers

- Manager: [src/modules/data/kart/](../../src/modules/data/kart/) (`KartManager`,
  `KartReader`, `KartWriter`, `KartManifest`, `KartMerge`).
- Compression: zstd via `KARTEND_HAS_ZSTD` (CMake-time toggle); zlib
  fallback uses Qt's built-in `qCompress`.
- Dialogs: [src/ui/dialogs/kartmergedialog.h](../../src/ui/dialogs/),
  `kartprogressdialog.h`.
- CLI entry point: [src/core/main.cpp](../../src/core/main.cpp);
  parsing in `cliargs.cpp`.
- File format: zip-style container with a manifest header
  (`manifest.json` or similar), one INI per collection, a metadata
  dump per collection, and an artwork-links dump.
- Adding a new payload section (e.g. media file inclusion, or playback
  history): extend the manifest schema, add a writer phase, add a
  matching reader phase, version-bump the manifest.
- Conflict-resolution merge logic: see `KartMerge::applyPolicy` for
  the field-by-field rules under each policy.
