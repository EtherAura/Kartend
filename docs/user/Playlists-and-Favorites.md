# Playlists & Favorites

Playlists are curated lists of items that can span multiple
collections. They appear as virtual collection tiles alongside your
real collections and behave like collections in most respects — you
can browse them, search them, set them as the startup collection, and
launch items directly from them.

**Favorites** is a built-in playlist that's always present and can't
be deleted. Beyond Favorites, you can create any number of user
playlists, populate them via the right-click context menu, and import
or export to / from M3U and JSON.

> **Looking for rule-driven playlists?** Smart playlists derive their
> members from a filter (recently launched, most played, by extension,
> …) and re-evaluate every time you open them. See
> [Smart Playlists](Smart-Playlists.md).

> **Where to find this** — Right-click any item → **Add to Playlist ▶**
> or **Add to Favorites**. Playlist tiles appear at the top level of
> the collection grid (or wherever they're parented).

## Favorites

A built-in, reserved playlist. Auto-created on first access. It cannot
be deleted — the **Delete playlist…** entry is simply absent for it —
but it *can* be renamed, so you can localize the label or call it
something else.

| Action | Where |
|--------|-------|
| Add an item | Right-click → **Add to Favorites** |
| Remove an item | Right-click an item inside Favorites → **Remove from Favorites** |

The toggle in the context menu is contextual: it shows **Add to
Favorites** if the item isn't yet in Favorites, **Remove from
Favorites** if it is.

There is no visual badge for favorite items today. Use the `favorite:true` [search token](Search-Sort-Filter.md#structured-search-tokens) or a *Favorite* [smart playlist](Smart-Playlists.md) rule to pick them out.

## User playlists

### Creating a playlist

Three ways:

1. **From the context menu** — right-click any item → **Add to Playlist
   ▶ → New playlist…**. A small dialog asks for a name; the new
   playlist is created and the item added.
2. **Empty playlist** — there isn't a dedicated "create empty playlist"
   action today. The shortest path is option #1 with any item, then
   remove the item afterward.
3. **From an import** — see [Importing](#importing).

### Naming and renaming

Rename by opening the playlist and right-clicking inside it →
**Rename playlist…**. Names must be non-empty and at most 512
characters. They are **not** required to be unique — nothing stops two
playlists sharing a name, which is worth knowing before you import the
same file twice.

### Adding items

Right-click any item in any collection → **Add to Playlist ▶ → [name]**.
Adding an item that's already in the playlist is a no-op.

### Removing items

Right-click an item *inside the playlist* → **Remove from Playlist**.
The item is removed from the playlist; the underlying item file and
its database state are untouched.

### Deleting a playlist

Open the playlist, then right-click inside it → **Delete playlist…**.
Confirmation prompt. (Playlist-level actions live inside the playlist,
not on its tile in the parent view.)
Items aren't deleted — only the playlist itself.

Reserved playlists (Favorites) hide the **Delete Playlist** option.

## Behavior of playlist tiles

Playlists are **virtual collections** — they appear in the grid as
tiles like real collections, but they don't have an INI section, can't
be parented elsewhere via the Settings tree, and don't carry their own
appearance settings.

What you can do with a playlist tile:

- Open it (`Enter`) — browse / search / sort / launch its items just
  like any collection
- Make it the startup collection — set
  `[General] startupCollection=Playlist Name` (the name shown in the
  UI)
- Toggle the sidebar inside it (`F9`) — sidebar inherits the active
  collection's styling
- Apply title-pattern cleanup — playlists do not have their own
  per-collection settings, but the sidebar / grid layout follows the
  parent context

What you can't do:

- Set per-collection appearance (no INI section to edit)
- Add subcollections under a playlist
- Use the [Apply Settings](Settings-Dialog.md#apply-settings) workflow
  to/from a playlist

## Import / export

### Exporting

Open the playlist, then right-click inside it → **Export playlist ▶**
→ **As JSON…** or **As M3U…**. Use this for the
items-only formats (JSON / M3U) below; for a full-fidelity backup
that includes every playlist alongside the collection it lives in,
export the parent collection as a [`.kart` package](Backup-and-Migration.md)
instead — playlists now round-trip inside karts.

Two formats:

| Format | Lossy? | Notes |
|--------|--------|-------|
| **JSON** | No | Preserves source collection UUID + path. Round-trip safe. |
| **M3U** | Yes | Path-per-line. Compatible with media players. Loses cross-collection metadata. |

#### JSON shape

```json
{
  "kartend_playlist_version": 2,
  "name": "Playlist Name",
  "icon": "",
  "parent_collection_uuid": "abcd-1234",
  "created_at": "2026-05-21T14:02:00Z",
  "updated_at": "2026-05-21T14:02:00Z",
  "is_smart": false,
  "smart_filter": "",
  "items": [
    {
      "source_collection_uuid": "abcd-1234",
      "source_path": "/home/me/Videos/Films/a-film.mkv",
      "added_at": "2026-05-21T14:02:00Z"
    }
  ]
}
```

`kartend_playlist_version` is **mandatory** on import — a file without
it is rejected with "Missing or unsupported kartend_playlist_version",
so a hand-written playlist needs the field. Versions 1 and 2 are
accepted. `reserved_kind` is deliberately not exported: importing
Favorites gives you an ordinary playlist, not a second reserved one.

The `source_collection_uuid` is a hash of the collection's name and
media directory, so it *changes* when you rename a collection or move
its folder. Playlist membership survives those because Kartend re-keys
every referencing table as part of the rename — not because the uuid is
stable.

#### M3U shape

```m3u
#EXTM3U
#EXTINF:-1,a-film
/home/me/Videos/Films/a-film.mkv
#EXTINF:-1,another-album
/home/me/Music/Albums/another-album.flac
```

Extended M3U — each entry gets an `#EXTINF` line carrying the item's
base name. Will play in any media player but loses the collection
binding. When re-importing into Kartend, items match by absolute path
against the live items table. Skipped lines are **counted and
reported** in an "Import Complete" dialog, not silently dropped. A path
present in more than one collection is ambiguous; Kartend resolves it
deterministically and logs the choice.

One limitation worth knowing: any line beginning with `#` is treated as
a comment on import, so a media path that starts with `#` cannot
round-trip.

### Importing

Right-click any item → **Add to playlist ▶** → **Import playlist from
file…**. Pick a `.json` or `.m3u` file:

- **JSON import** — creates a new playlist and stores every reference
  verbatim. No matching happens at import time. The join to real items
  happens when the playlist is *opened*, so a reference with no live
  item simply produces no tile. There are no "ghost rows", and a
  reference can start working later if the matching item appears.
- **M3U import** — creates a new playlist; paths are resolved to
  `(collection, item)` at import time, and unresolved lines are counted
  and reported.

Importing a file whose playlist name already exists does **not**
prompt: you get a second playlist with the same name. Rename or delete
one afterwards.

## Recipes

### Curated "10 best" tile

Create a playlist called `Top Ten`. Add items from various
collections. Set as your startup collection:

```ini
[General]
startupCollection=Top Ten
```

When you launch Kartend, you land directly on the playlist.

### Cross-collection "currently playing" list

Create a playlist `Currently Playing`. Add the items you're actively
working through. Drop them when you finish.

Pair with a custom field (`status=in progress`) to mark items
individually so you can recover the list state from custom fields if
you ever delete the playlist.

### Moving a playlist between your own machines

Two options:

- **JSON export** preserves the source-collection UUID plus each
  item's path. On import, items match by `(uuid, path)` against the
  live items table on the target machine. Works cleanly when both
  machines share the same collection layout (the typical sync case).
- **`.kart` export** of the parent collection bundles every playlist
  (static and smart) belonging to it, plus the items themselves. The
  preflight + merge dialogs at import handle path remapping and any
  conflicts. See [Backup & Migration](Backup-and-Migration.md).

M3U export is intentionally lossy — it's intended for handing the
list to a media player, not for round-tripping inside Kartend.

### Backup all playlists at once

Two options:

- Copy the SQLite file `~/.local/share/kartend/media.db` — it
  contains every playlist and membership row.
- Export each owning collection as a `.kart` — playlists ride along
  inside the bundle. See [Backup & Migration](Backup-and-Migration.md).

A bulk-JSON-export across all playlists is on the wishlist.

## How playlists store data

```
playlists                       playlist_items
─────────────────               ──────────────────────────
id (UUID)                       playlist_id (FK)
name                            position
parent_collection_uuid          source_collection_uuid
reserved_kind ('' or            source_path
  'favorites')
```

- Items are referenced by `(source_collection_uuid, source_path)`,
  which makes them stable across rename and rescan.
- `position` is dense (re-densified on remove) so playlist order is
  preserved.
- `reserved_kind = 'favorites'` marks the built-in Favorites playlist.
  Other reserved kinds may be added in the future (e.g. "Recently
  Played" as a virtual playlist).

## Where to next

- [Smart Playlists](Smart-Playlists.md) — rule-driven playlists that
  re-evaluate on open
- [Item Metadata](Item-Metadata.md) — custom fields, manual files,
  artwork links per item
- [Backup & Migration](Backup-and-Migration.md) — `.kart` package
  format for full-collection (and playlist) backup or transfer
  between your own machines
- [Search, Sort & Filter](Search-Sort-Filter.md) — searching inside a
  playlist works the same as inside a collection

## For developers

- Manager: [src/modules/data/playlist/](../../src/modules/data/playlist/)
  (`PlaylistManager`).
- Schema: `playlists` and `playlist_items` tables, defined in the
  database migrations.
- Virtual collection synthesis: at MainWindow init, `PlaylistManager`
  builds a `CollectionConfig` for each playlist (`isPlaylist=true`,
  `playlistId=<uuid>`, `playlistReservedKind` empty or `favorites`).
  These appear in the live `m_collections` list alongside real
  collections.
- Membership checks: `PlaylistManager::containsItem(playlistId, srcUUID,
  srcPath)` runs a `SELECT … LIMIT 1` per call, served by
  `idx_playlist_items_lookup`. It is index-backed, not cached in
  memory — only the Favorites playlist id is memoized.
- Add operations are idempotent; remove re-densifies positions.
- M3U / JSON serialization: `playlistserializer.cpp` (`playlistio.cpp`
  holds the database halves) (lossy / lossless paths
  are separate methods).
- Adding a new reserved playlist (e.g. "Recently Played"): pick a
  reserved-kind string, add the auto-create logic to
  `PlaylistManager::ensureFavoritesPlaylist()`, gate context-menu
  Delete to skip it.
