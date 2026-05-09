# File Locations

Kartend follows the [XDG Base Directory specification](https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html)
for state. Three directories on Linux:

| Purpose | Default path | XDG variable | Notes |
|---------|--------------|--------------|-------|
| Configuration (INI) | `~/.config/kartend/` | `$XDG_CONFIG_HOME` | Settings file. Safe to back up / version. |
| Data (database, durable state) | `~/.local/share/kartend/` | `$XDG_DATA_HOME` | SQLite database with collection contents and per-item metadata. |
| Cache (transient state) | `~/.cache/kartend/` | `$XDG_CACHE_HOME` | Pixmap cache, extracted-archive temp directories, session state. Safe to delete when Kartend isn't running. |

If the corresponding `$XDG_*` variable is set, Kartend uses
`$XDG_*/kartend/` instead. Consistent with Qt's `QStandardPaths` —
this is also where Qt looks for `QSettings` files and similar.

## Configuration

```
~/.config/kartend/
└── kartend.cfg
```

INI format. Created on first save (not at first launch). Holds:

- `[General]` — global settings
- `[Collection Name]` — one section per collection
- `[Parent > Child]` — subcollections, where `> ` is the literal
  hierarchy separator

See [Configuration Reference](Configuration-Reference.md) for every
key.

### Backing up

Plain text, version-controllable, dotfile-friendly:

```bash
cp ~/.config/kartend/kartend.cfg ~/dotfiles/kartend.cfg
```

The file uses the user's own paths (`mediaDirectory`, `artworkDirectory`)
which are typically absolute or `~`-prefixed. Restoring on another
machine usually means tweaking those paths.

### Editing by hand

Kartend reads the file on launch (and on **Settings → Reload** if
present) but the Settings Dialog rewrites it on save. To edit by hand:

1. Quit Kartend.
2. Edit `~/.config/kartend/kartend.cfg`.
3. Restart Kartend.

If you edit while Kartend is running, your changes will be overwritten
the next time the dialog saves.

## Database

```
~/.local/share/kartend/
└── kartend.db
```

SQLite v3, single file. Kartend creates it on first scan of any
collection and migrates the schema as needed (migrations are forward-
only — older Kartend versions may refuse to read a newer database).

### What's in there

| Table | Contents |
|-------|----------|
| `items` | One row per scanned item (collection_uuid, source_path, display name, mtime, size, play_count, last_played) |
| `item_metadata` | Custom fields, manual file paths, launcher overrides, keyed by (collection_uuid, source_path, field_name) |
| `item_artwork` | Per-item manual artwork links (collection_uuid, source_path, artwork_type, artwork_file) |
| `launch_history` | Chronological log of launches (timestamp, collection_uuid, source_path, duration_seconds) |
| `playlists` | Playlist definitions (id, name, parent_collection_uuid, reserved_kind) |
| `playlist_items` | Playlist memberships (playlist_id, position, source_collection_uuid, source_path) |
| `db_meta` | Schema version and other internal markers |

### Backing up

The whole database is one file:

```bash
cp ~/.local/share/kartend/kartend.db ~/backups/kartend-$(date +%F).db
```

Quit Kartend first, or use SQLite's online backup mechanism:

```bash
sqlite3 ~/.local/share/kartend/kartend.db ".backup ~/backups/kartend-$(date +%F).db"
```

`.kart` packages do **not** include the database (configuration only)
— see [Backup & Sharing](Backup-and-Sharing.md). For a complete backup,
copy both `kartend.cfg` and `kartend.db`.

### Querying directly

```bash
sqlite3 ~/.local/share/kartend/kartend.db "
SELECT name, COUNT(items.id) AS items
FROM (SELECT DISTINCT collection_uuid FROM items) c
JOIN items ON c.collection_uuid = items.collection_uuid
GROUP BY c.collection_uuid
ORDER BY items DESC;
"
```

Useful for ad-hoc reports the Statistics dialog doesn't cover. See
[History & Statistics → Quick recipes](History-and-Statistics.md#quick-recipes)
for more SQL recipes.

### Resetting the database

To drop everything Kartend knows:

```bash
# Quit Kartend first.
rm ~/.local/share/kartend/kartend.db
```

Next launch, Kartend rebuilds the schema and re-scans your collections
on first open. You lose:

- Play counts and last-played timestamps
- Launch history
- Custom fields per item
- Manual file paths
- Manual artwork links
- Per-item launcher overrides
- Playlists (including Favorites — Favorites is auto-recreated empty)

Configuration in `kartend.cfg` is preserved.

## Cache

```
~/.cache/kartend/
├── (scaled artwork pixmaps, structured by hash)
├── (extracted archives, per-launch temp dirs)
└── (session state — selection, scroll position)
```

Transient. Anything in here can be deleted when Kartend isn't running
without breaking anything; Kartend will rebuild what it needs.

### Pixmap cache

Scaled tile artwork is cached on disk so repeat opens don't decode
PNGs again. The in-memory size is bounded by `pixmapCacheSizeMB`
([General], default 50 MB); the on-disk size is bounded only by
available space, with stale entries swept on boot.

For libraries with thousands of items, expect tens to hundreds of MB
in `~/.cache/kartend/`. Safe to delete; Kartend will re-decode on
demand.

### Extracted archives

Collections with `extractArchives=true` extract zip / 7z / etc. to a
temp directory under `~/.cache/kartend/extract/<hash>/` before
launching the contained file. Cleaned up on Kartend shutdown.

If Kartend crashes mid-run, you may find leftover extract directories.
Safe to delete manually:

```bash
rm -rf ~/.cache/kartend/extract/
```

### Session state

A small set of files records the last-selected item per collection
and scroll position. Driven by `rememberSelection=true`. Safe to
delete; you'll just start fresh in each collection.

## User-managed data (not in Kartend's directories)

Kartend doesn't store your media or artwork — you tell it where they
are:

- `mediaDirectory` — wherever you keep your files (`~/Videos`,
  `~/Documents/Reports`, `/mnt/nas/movies`, etc.)
- `artworkDirectory` — alongside or separate from media
- `videoDirectory` — for [video previews](Video-Previews.md)
- `manualDirectory` — for [per-item manuals](Item-Metadata.md#manual-files)
- `placeholderArtwork`, `collectionIcon`, `headerLogoImage` — single
  asset paths
- `backgroundImage`, `backgroundVideo` — single asset paths
- `startupVideoPath` — single asset path

All of these can be `~`-prefixed or absolute. Environment variables
(`$HOME`, `$XDG_*`) are **not** expanded in Kartend's config — use `~`
or absolute paths.

Kartend never modifies these directories. Reading only.

## Logs and crash dumps

Kartend logs to **standard error** by default (no log file). To
capture:

```bash
kartend 2>&1 | tee ~/kartend.log
```

For debug output, see [Logging & Diagnostics](Logging-and-Diagnostics.md)
— enable categories with `KARTEND_LOG_RULES` or `QT_LOGGING_RULES`.

Crash dumps (where the system collects them, e.g. `systemd-coredump`
on systemd-based distros) are stored by your system, not by Kartend.
On a typical Arch / Fedora / Debian setup:

```bash
coredumpctl list kartend
coredumpctl info <pid>
coredumpctl dump <pid> --output=core.kartend
```

## Recovery

### Lost configuration

If `kartend.cfg` is gone (deleted, disk corruption), Kartend launches
into the empty state. Your collections need to be re-added from
scratch — unless you have a backup or a `.kart` export to import.

### Lost database

If `kartend.db` is gone (deleted, schema corruption Kartend can't
recover from), Kartend rebuilds it on first scan. Configuration is
preserved (it's in `kartend.cfg`); per-item state (custom fields,
manuals, launcher overrides, history, playlists) is lost.

If you have a `.kart` backup: re-import each collection. The
configuration restores; per-item metadata restores; history doesn't
(it's not in the package).

### Disk full

Kartend's cache is the most likely culprit. Clear it:

```bash
rm -rf ~/.cache/kartend/
```

(Kartend should not be running when you do this.) Subsequent launches
rebuild the cache on demand.

If your *database* directory is full, that's more serious — the
database file may have failed to commit. Free space and let Kartend
rebuild what it needs.

## Sandbox / Flatpak paths

Inside the Flatpak sandbox, Kartend's directories are namespaced:

| Purpose | Sandbox path | Host visibility |
|---------|--------------|-----------------|
| Config | `~/.var/app/io.github.EtherAura.Kartend/config/kartend/` | Yes (it's still in your home) |
| Data | `~/.var/app/io.github.EtherAura.Kartend/data/kartend/` | Yes |
| Cache | `~/.var/app/io.github.EtherAura.Kartend/cache/kartend/` | Yes |

Media / artwork directories you point at must be reachable through the
Flatpak's `--filesystem` permissions. Defaults bundle `--filesystem=home`,
`--filesystem=/media`, `--filesystem=/run/media` — most personal
libraries work without further config. Add `--filesystem=...` to the
manifest if your library lives elsewhere.

## Where to next

- [Configuration Reference](Configuration-Reference.md) — keys and
  defaults inside `kartend.cfg`
- [Backup & Sharing](Backup-and-Sharing.md) — `.kart` package format
  for portable transfers
- [History & Statistics](History-and-Statistics.md) — what's in the
  database
- [Troubleshooting](Troubleshooting.md) — recovery paths for common
  data issues

## For developers

- Path resolution: [src/utils/configutils.h](../../src/utils/) +
  `settingsutils.cpp`. Uses `QStandardPaths::writableLocation()` for
  each XDG type.
- Database setup: [src/modules/database/](../../src/modules/database/)
  (`DatabaseManager`). Schema migrations live next to it.
- Cache lifecycle: [src/modules/cache/](../../src/modules/cache/)
  (`CacheManager`). Disk cache uses content-addressable hashing
  (SHA-256 of source path + dimensions).
- Atomic file writes: see `architecture.md` —
  [Atomic File Writes](../architecture.md#atomic-file-writes) pattern
  used for `kartend.cfg` to avoid corruption on crash mid-write.
- Fresh-install migration tests: `tests/utils/test_dbmigrations.cpp`.
