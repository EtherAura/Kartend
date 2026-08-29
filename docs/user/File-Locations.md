# File Locations

Kartend follows the [XDG Base Directory specification](https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html)
for state. Three directories on Linux:

| Purpose | Default path | XDG variable | Notes |
|---------|--------------|--------------|-------|
| Configuration (INI) | `~/.config/kartend/` | `$XDG_CONFIG_HOME` | Settings file. Safe to back up / version. |
| Data (database, durable state) | `~/.local/share/kartend/` | `$XDG_DATA_HOME` | SQLite database with collection contents and per-item metadata. |
| Cache (transient state) | `~/.cache/kartend/` | `$XDG_CACHE_HOME` | Pixmap cache, DAT cache, session state. Safe to delete when Kartend isn't running. |

Extracted archives are the one thing that does *not* live in any of
these — they go to the system temp directory. See
[Extracted archives](#extracted-archives).

If the corresponding `$XDG_*` variable is set, Kartend uses
`$XDG_*/kartend/` instead. Consistent with Qt's `QStandardPaths` —
this is also where Qt looks for `QSettings` files and similar.

## Configuration

```
~/.config/kartend/
├── kartend.cfg
├── layout_profiles.json        (saved grid/list layouts)
├── presentation_profiles.json  (attract, marquee, splash bundles)
└── search_presets.json         (saved filters)
```

INI format. The directory is created eagerly on first launch; the file
itself only appears on the first save, so a fresh install with no
`kartend.cfg` is a normal state, not a fault. Holds:

- `[General]` — global settings
- `[Collection Name]` — one section per collection
- `[Parent > Child]` — subcollections, where `> ` is the literal
  hierarchy separator

See [Configuration Reference](Configuration-Reference.md) for every
key.

The three `.json` files beside it are named-registry sidecars, each
written only once you save your first entry: layout profiles and
presentation profiles from the settings dialog, and
[saved filters](Search-Sort-Filter.md#saved-filters) from the toolbar's
filter dropdown. Deleting one loses those saved entries and nothing
else.

### Backing up

Plain text, version-controllable, dotfile-friendly:

```bash
cp ~/.config/kartend/kartend.cfg ~/dotfiles/kartend.cfg
```

The file uses the user's own paths (`mediaDirectory`, `artworkDirectory`)
which are typically absolute or `~`-prefixed. Restoring on another
machine usually means tweaking those paths.

### Editing by hand

Kartend reads the file once, at launch, and the Settings Dialog
rewrites it on save. There is no "reload config from disk" command. To
edit by hand:

1. Quit Kartend.
2. Edit `~/.config/kartend/kartend.cfg`.
3. Restart Kartend.

If you edit while Kartend is running, your changes will be overwritten
the next time the dialog saves.

## Database

```
~/.local/share/kartend/
└── media.db
```

SQLite v3, single file. Kartend creates it on first scan of any
collection and migrates the schema as needed. Migrations are
forward-only: there are no down-migrations. An older Kartend opening a
newer database logs a warning, skips the migration ladder, and carries
on — it will not refuse to start, but columns it doesn't know about are
invisible to it, so treat the situation as "recover the matching
version", not "safe to run indefinitely".

### What's in there

Most per-item tables key on `(collection_uuid, path)` rather than on
the numeric item id, so entries survive the id renumbering that happens
when a collection is rescanned.

| Table | Contents |
|-------|----------|
| `collections` | One row per scanned collection (`id`, `name`, `last_scanned`, `ext_signature`, `uuid`) |
| `items` | One row per scanned item (`collection_id`, `path`, `name`, `artwork_path`, `last_modified`, `file_size`, `play_count`, `last_played`, `rating`, `collection_uuid`) |
| `items_fts` | FTS5 search index over `items` — indexes `name`, `path`, `collection_uuid` |
| `item_metadata` | One row per item: `title`, `description`, `genre`, `developer`, `publisher`, `release_date`, `content_rating`, `players`, `runtime_seconds`, `tags`, `custom_fields`, `manual_path`, `source`. Cumulative play time lives here, in `runtime_seconds` |
| `item_artwork` | Per-item manual artwork links (`collection_uuid`, `path`, `artwork_type`, `manual_path`) — one row per linked type |
| `launch_history` | Chronological log of launches (`collection_uuid`, `path`, `name`, `launched_at`). `name` is denormalized at insert so rows stay readable after the item is deleted. No duration column — see `item_metadata.runtime_seconds` |
| `playlists` | Playlist definitions (`id`, `name`, `icon`, `parent_collection_uuid`, `reserved_kind`, `created_at`, `updated_at`) |
| `playlist_items` | Playlist memberships (`playlist_id`, `position`, `source_collection_uuid`, `source_path`, `added_at`) |
| `meta` | Internal key/value markers (FTS index bookkeeping). The schema version itself is *not* here — it lives in SQLite's `PRAGMA user_version` |

### Backing up

The whole database is one file:

```bash
cp ~/.local/share/kartend/media.db ~/backups/kartend-$(date +%F).db
```

Quit Kartend first, or use SQLite's online backup mechanism:

```bash
sqlite3 ~/.local/share/kartend/media.db ".backup ~/backups/kartend-$(date +%F).db"
```

`.kart` packages bundle per-item metadata (custom fields, notes,
tags, ratings, manual links, artwork links, state flags) and
playlists in addition to collection configuration, but exclude
`launch_history` (and play counts / last-played timestamps). See
[Backup & Migration](Backup-and-Migration.md). For a complete
backup including history, copy both `kartend.cfg` and `media.db`.

### Querying directly

```bash
sqlite3 ~/.local/share/kartend/media.db "
SELECT c.name, COUNT(i.id) AS items
FROM collections c
JOIN items i ON i.collection_id = c.id
GROUP BY c.id
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
rm ~/.local/share/kartend/media.db
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

## Generated playlists

```
~/.local/share/kartend/
└── multi-disc/
    └── <collection-uuid>/
        └── (one .m3u per grouped release)
```

Collections with
[multi-disc grouping](Collections.md#multi-disc-grouping) enabled
(`groupMultiDisc=true`) get one generated `.m3u` per grouped release.
They live here, under Kartend's own data directory, and **never
alongside your media** — the feature reads your folders without
writing to them.

Each collection keeps its own subdirectory keyed by collection UUID,
so two collections covering the same folder with different settings
can't disturb each other's playlists.

Regenerated on every scan, so safe to delete while Kartend isn't
running — the next scan rewrites them. Turning `groupMultiDisc` off
removes that collection's directory on the following scan.

## Cache

```
~/.cache/kartend/
├── artwork/
│   └── <md5>.png            (scaled artwork pixmaps)
├── metadata/
│   ├── artwork_cache.json
│   ├── artwork_cache.sqlite
│   └── session.json         (selection state)
└── datcache.sqlite          (parsed DAT catalogues)
```

Transient. Anything in here can be deleted when Kartend isn't running
without breaking anything; Kartend will rebuild what it needs.

### Pixmap cache

Scaled tile artwork is cached on disk so repeat opens don't decode
images again. The cache key is an MD5 of the source artwork path, and
the stored file is the *already-scaled* decode, not a copy of the
original.

Two budgets apply. In memory, `pixmapCacheSizeMB` ([General], default
50 MB) is split between Qt's process-wide pixmap cache and Kartend's
own artwork cache, so each gets roughly half. On disk,
`artworkDiskCacheBudgetMB` ([General], default 2048 MB) caps the
directory; a background walk evicts the oldest-touched entries once the
budget is exceeded. Set it to `0` to disable eviction entirely.

The sweep is opportunistic — it piggybacks on the background
cache-size walk rather than running at startup — so the directory can
sit slightly over budget for a while after a big scan. That's expected.

Safe to delete; Kartend will re-decode on demand.

### Extracted archives

Collections with `extractArchives=true` extract zip / 7z / etc. before
launching the contained file. This is the one piece of Kartend state
that does **not** live under `~/.cache/` — it goes to the system temp
directory:

```
/tmp/kartend_extract/<archive base name>/
```

The directory is created owner-only (0700). If a `kartend_extract`
already exists and isn't private to you — which on a shared machine
could mean someone else pre-created it — Kartend refuses to trust it
and falls back to an unguessable per-run directory
(`/tmp/kartend_extract_XXXXXX`) instead. You still get your launch;
you just lose the cross-run reuse for that session.

Because two archives with the same base name map to the same directory,
each extraction drops a `.kartend-source` marker recording the exact
archive it came from (path, size, mtime). A cache hit is only honoured
when the marker matches, so `RomsA/disc.zip` can never serve the
contents of `RomsB/disc.zip`.

It is **not** cleaned up on shutdown — that is deliberate, since the
directory is a cross-run extraction cache and re-extracting a large
archive on every launch would be slow. The OS reclaims the temp root at
reboot. To reclaim the space sooner:

```bash
rm -rf /tmp/kartend_extract/
```

### Session state

One file, `~/.cache/kartend/metadata/session.json`, records the
last-selected item per collection. Read back when
`rememberSelection=true`. Safe to delete; you'll just start fresh in
each collection.

## User-managed data (not in Kartend's directories)

Kartend doesn't store your media or artwork — you tell it where they
are:

- `mediaDirectory` — wherever you keep your files (`~/Videos`,
  `~/Documents/Reports`, `/mnt/nas/movies`, etc.)
- `artworkDirectory` — alongside or separate from media. Doubles as the
  root for [video previews](Video-Previews.md) (`<artworkDirectory>/video/`)
  and [per-item manuals](Item-Metadata.md#manual-files)
  (`<artworkDirectory>/manual/`)
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

The one exception is the scraper. With `[ScraperOptions]
scrapeLogging=true`, scrape diagnostics are also teed to a size-capped
`~/.config/kartend/scrape.log`. It lives in the config directory rather
than the cache directory because it is the only way to capture scrape
output from a GUI build, and a cache wipe shouldn't take the evidence
with it — but it does mean the "safe to back up / version" config
directory can contain a log. Delete it when you're done.

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

If `media.db` is gone (deleted, schema corruption Kartend can't
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
- [Backup & Migration](Backup-and-Migration.md) — `.kart` package
  format for moving libraries between your own machines
- [History & Statistics](History-and-Statistics.md) — what's in the
  database
- [Troubleshooting](Troubleshooting.md) — recovery paths for common
  data issues

## For developers

- There is no central path module — each subsystem resolves its own
  root. The config path is
  [src/utils/app/settingsutils.cpp](../../src/utils/app/settingsutils.cpp)
  (`ConfigLocation`); the data root is resolved independently in
  `databasemanager.cpp`, `playlistmanager.cpp` and `kartdb.cpp`
  (`AppDataLocation`); the cache root in `cachediskstorage.cpp` and
  `sessionmanager.cpp` (`GenericCacheLocation + "/kartend"`). Adding a
  new state file means picking the right `QStandardPaths` flavour
  yourself.
- Database setup: [src/modules/data/database/](../../src/modules/data/database/)
  (`DatabaseManager`). Schema migrations live in
  [src/utils/db/dbmigrations.cpp](../../src/utils/db/dbmigrations.cpp),
  keyed on `PRAGMA user_version`.
- Cache lifecycle: [src/modules/data/cache/](../../src/modules/data/cache/)
  (`CacheManager`). The disk cache is content-addressed by an MD5 of the
  source artwork path (`CacheDiskStorage::artworkCachePath`).
- Atomic file writes: see `architecture.md` —
  [Atomic File Writes](../dev/architecture.md#atomic-file-writes) pattern
  used for `kartend.cfg` to avoid corruption on crash mid-write.
- Fresh-install migration tests: `tests/utils/db/test_dbmigrations.cpp`.
