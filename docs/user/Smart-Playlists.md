# Smart Playlists

Smart playlists are rule-driven playlists — they don't store a fixed
list of items, but instead re-evaluate a filter every time you open
them. A "Recently launched" smart playlist always shows the *most*
recently launched items in the database, not a snapshot from when the
playlist was created.

Smart playlists appear in the grid as tiles alongside
[user playlists and Favorites](Playlists-and-Favorites.md). They share
the same browsing and launching behavior; the difference is purely in
how membership is determined.

> **Where to find this** — Right-click any item → **Add to Playlist
> ▶ → New smart playlist…**. The right-clicked item is *not* added —
> smart playlists derive their members from rules, not from manual
> add/remove actions.

## Why smart playlists?

A user playlist is a curated, manually-maintained list — you add and
remove items by hand. A smart playlist is the opposite: you define a
rule and let Kartend keep the list current.

Some examples of what you can build:

- **What did I last play?** — *Recently launched, 50*
- **What's my comfort food?** — *Most played, 25*
- **What have I never opened?** — *Never launched, 100* (a backlog tile)
- **A folder browser for `.pdf`** — *By extension, pdf* (every PDF in
  every collection, regardless of which collection it lives in)
- **What's new this month?** — *Recently added, 30* (items added to the
  database in the last 30 days)
- **Show me only the items I've illustrated** — *Has artwork* (every
  item a cover was found for in the collection's artwork folder)
- **Items I've started but haven't finished** — *Continue later* (the
  in-progress queue, driven by the per-item flag)
- **A worklist for the artwork wizard** — *Missing artwork*
- **The favorites tile, but as a smart playlist** — *Favorite* (lets
  you treat the reserved Favorites list like any other smart-playlist
  view, e.g. for setting as the startup collection)

## Creating a smart playlist

1. Right-click any item in any collection (the right-clicked item is
   used to anchor the menu, but isn't added to the playlist).
2. Open **Add to Playlist ▶ → New smart playlist…**.
3. Pick a **Name** — must be unique across all playlists. The Save
   button stays disabled until the name is non-empty.
4. Pick a **Criterion** — the dropdown swaps in a different parameter
   pane below it.
5. Set the per-criterion parameters (see [filter kinds](#filter-kinds)).
6. Optionally **Add rule** for more criteria — see
   [multiple rules](#multiple-rules) below.
7. **OK** to create.

The new playlist appears at the root level of the collection grid
immediately and opens populated with its current matches.

### Multiple rules

**Add rule** appends another criterion. With two or more rules a
**Match** selector appears above them:

| Match | Meaning |
|-------|---------|
| **all of the rules** | An item must satisfy *every* rule — an intersection. "Never launched" **and** "By extension: pdf" gives unread PDFs. |
| **any of the rules** | An item must satisfy *at least one* — a union. "Pinned" **or** "Continue later" gives everything you've flagged either way. |

**Remove** drops a rule; the last one can't be removed, since a playlist
with no rules would have nothing to match on. The selector is hidden
while there is only one rule — there is nothing to combine yet.

**OK** stays disabled until every rule is complete, not just the first.
A "Match all" set containing one blank rule would match nothing at all,
which is a confusing way to end up with an empty playlist.

## Filter kinds

| Kind | Parameter | Selects |
|------|-----------|---------|
| **Recently launched** | Show top (1–1000, default 50) | The N most-recently-launched items, newest first. Reads the item's own `last_played` timestamp, not the launch-history log. |
| **Most played** | Show top (1–1000, default 50) | The N items with the highest launch count, ties broken by most-recently-played. |
| **Never launched** | Show first (1–1000, default 50) | The first N items with zero recorded launches. Ordered the same way the rest of the grid is by default. |
| **By extension** | Extensions (csv, lowercase, leading dot optional) | Every item whose file extension is in the list. Empty list = no matches. Example: `pdf,epub,cbz`. |
| **Has artwork** | *(none)* | Every item with a cover — one auto-discovered in `artworkDirectory` when the collection was last scanned, or one you [linked by hand](Artwork.md#manual-per-item-links). |
| **Missing artwork** | *(none)* | The complement of *Has artwork* — items no cover was found for on the last scan. Useful as a worklist for the [Artwork Wizard](Artwork.md#artwork-assignment-wizard). |
| **Recently added** | Window (1–3650 days, default 30) | Items whose `date_added` falls within the last N days, newest first. |
| **By collection** | Collection (picker) | Every item belonging to the chosen collection. The collection is referenced by UUID so renames don't break the filter. Empty selection yields zero matches. |
| **By title search** | Substring | Every item whose name contains the given substring (case-insensitive, `LIKE %?%`). Empty substring yields zero matches. |
| **Favorite** | *(none)* | Every item present in the reserved Favorites playlist. |
| **Pinned** | *(none)* | Items toggled pinned via [item state flags](Item-Metadata.md#state-flags). |
| **Hidden** | *(none)* | Items toggled hidden. Note that hidden items are de-emphasised in the regular grid, not filtered out of it, so this is a way to see them together rather than a way to see them at all. |
| **Continue later** | *(none)* | Items toggled continue-later — the in-progress queue. |

> **Counted vs uncounted** — *Recently launched*, *Most played*,
> *Never launched* are limited by a hard count. The rest are
> open-ended — the result set is whatever matches the rule, with no
> cap.

### Recently launched

Driven by each item's own `last_played` timestamp, newest first. Items
that have never been launched don't appear. Re-launching an item
promotes it to the top of the result the next time the playlist is
opened.

This is **not** affected by `[General] historyEnabled`. That setting
gates the `launch_history` log — the chronological record you browse in
the History dialog. The per-item `last_played` and `play_count` stamps
are written on every launch regardless, which is exactly why the
launch-based playlists keep working with history recording turned
off.

### Most played

Sorted by launch count first, then — for ties — by how recently the
item was last played. Total *play time* is tracked separately (and only
when runtime detection is on, see
[Splash & Now Playing](Splash-and-Now-Playing.md#now-playing-overlay)),
but this rule does not read it.

### Never launched

The complement of *Recently launched*: items whose `play_count` is
zero or unset. Useful for "what's left in my backlog?" tiles.

Turning history collection off doesn't affect this rule at all: it
reads `play_count`, which keeps incrementing on every launch whatever
`historyEnabled` is set to.

### By extension

A cross-collection extension filter. Extensions are matched
case-insensitively against the file's suffix (without the leading dot).
The list field accepts comma-separated values:

```
pdf,epub,cbz
```

Trims whitespace around each token. An empty list yields zero matches.

### Has artwork

Returns every item with a cover auto-discovered from `artworkDirectory` —
the same search the grid uses to paint the tile: the artwork folder and
its typed cover subfolders (`front/`, `box/`, …), matched on the item's
name, including the multi-disc case where a grouped item takes the art of
one of its discs. Items showing the procedural placeholder (no real
artwork) are excluded.

The match is recorded when the collection is scanned, so it reflects your
artwork folder as of that scan. Add or remove a cover without touching the
media folder and this rule catches up the next time that collection is
scanned; the grid, which looks at the folder directly, updates sooner.

A cover you [linked by hand](Artwork.md#manual-per-item-links) counts too,
and counts immediately — links are stored in the database rather than found
on disk, so saving or clearing one moves the item in and out of this rule
straight away rather than waiting for a scan. It is also what the tile paints:
a hand-linked cover shows on the item's tile and its cover-flow card, ahead of
anything auto-discovery found, so this rule and the grid agree. The link has to
still point at a file that exists; one whose target you have since deleted
doesn't count, and the item falls back to whatever auto-discovery finds for it.
Links on types that are never used as a cover — `logo`, and any custom type
you've added — are gallery-only and don't count here.

> **Note** — clearing a link is the one case that can lag. If the item also
> has an auto-discovered cover, it reports as missing artwork until that
> collection is next scanned, at which point the auto-discovered cover takes
> over again.

### Recently added

Uses the items table's `date_added` epoch. The "window" is rolling —
opening the playlist tomorrow with `Window=30` shifts the window
forward by one day, so the oldest items can drop off as newer ones are
added.

Hand-edited absurd values are clamped to `[1, 3650]` downstream so a
typo can't make the playlist scan a millennium of history.

## Editing a smart playlist

Right-click a smart-playlist tile → **Edit smart filter…**. The same
dialog opens, pre-populated with the current name and rules. Save
applies the new filter immediately — the open view re-evaluates on the
next open.

The **Remove from Playlist** action is hidden inside smart-playlist
views because removal wouldn't stick: the next open would re-derive
membership from the rule and the "removed" item would reappear.

## Deleting a smart playlist

Right-click a smart-playlist tile → **Delete Playlist**. Confirmation
prompt. Items aren't affected — only the playlist row and its filter.

## What you can do with a smart-playlist tile

Smart playlists behave like virtual collections, with one limitation:

| Action | Works? |
|--------|--------|
| Open it, browse, search, sort, launch | ✅ |
| Set as the [startup collection](Configuration-Reference.md#selection--navigation) | ✅ |
| Toggle the sidebar (`F9`) | ✅ |
| Add items via the context menu | ❌ — membership is rule-driven |
| Remove items via the context menu | ❌ — same reason |
| Export to JSON / M3U | ✅ (a snapshot of the current matches) |
| Apply [per-collection appearance](Settings-Dialog.md#apply-settings) | ❌ — playlists inherit the parent collection's styling |

## Tips & recipes

### Backlog tile

*Never launched*, limit 100. Becomes your "what should I open next?"
view. Pair with a custom field (`status=tried`) when you want to demote
an item from the backlog without actually launching it.

### Quick-access "currently playing"

A *Recently launched* tile, limit 5, set as your startup collection.
You land on the items you've been working through.

### Sort by playtime

*Most played*, limit 1000 — effectively "every item I've ever played,
sorted by how often". Useful for spotting your unintentional
mainstays.

### Cross-collection format browser

*By extension* with a single extension (e.g. `mp3`). Behaves like a
unified view across every audio collection in your library.

### Re-evaluation timing

Filters re-run **on open**, not continuously. If you launch an item,
then immediately re-open the smart playlist, the change is reflected.
Switching back and forth between two open playlists doesn't trigger a
re-evaluation in the meantime.

## Limitations

- **Rules combine one way per playlist** — a playlist is *all* of its
  rules or *any* of them, not a mix. There is no nesting, so
  "(A or B) and C" can't be expressed; build it as two playlists, or
  reach for the structured search tokens described in
  [Search](Search-Sort-Filter.md#structured-search-tokens).
- **Snapshot export is lossy** — exporting a smart playlist to JSON or
  M3U writes the matches at export time, not the rule. Importing
  produces a regular (non-smart) playlist. Use the
  [`.kart` package](Backup-and-Migration.md) format if you need the
  filter spec itself to round-trip (e.g. when migrating between your
  own machines).

## How smart playlists store data

Smart playlists live in the same `playlists` table as user playlists,
with two extra columns:

```
playlists                       playlist_items
─────────────────               ──────────────────────────
id (UUID)                       (unused for smart playlists)
name                            position
parent_collection_uuid          source_collection_uuid
reserved_kind                   source_path
is_smart  ← 1 for smart
smart_filter  ← JSON spec
```

The `playlist_items` rows are not populated for smart playlists —
membership is computed each time the playlist opens.

The JSON spec stored in `smart_filter` looks like:

```json
{
  "kind": "recently_launched",
  "limit": 50,
  "extensions": [],
  "days": 30,
  "collection_uuid": "",
  "title_search": ""
}
```

Per-kind fields irrelevant to the chosen `kind` are still emitted
with default values so the schema stays predictable for tooling.

A playlist with **more than one rule** adds `match` and `rules`
alongside those fields:

```json
{
  "kind": "never_played",
  "limit": 8,
  "extensions": [],
  "days": 30,
  "collection_uuid": "",
  "title_search": "",
  "match": "all",
  "rules": [
    { "kind": "never_played", "limit": 8, "…": "…" },
    { "kind": "by_extension", "extensions": ["pdf"], "…": "…" }
  ]
}
```

`match` is `"all"` or `"any"`. The top-level fields are not redundant
padding: they mirror the **first** rule, which is what lets a build
that predates multi-rule playlists open one and see its first rule
rather than fail outright. A single-rule playlist emits no `match` or
`rules` at all, so nothing already on disk changes shape.

Kind tags are stable across versions, so a smart playlist created in a
newer build can be re-opened in an older build if the kind tag is
recognized. Unknown tags surface as a load error and the playlist is
treated as inert (no matches) rather than corrupting the catalog.

## Where to next

- [Playlists & Favorites](Playlists-and-Favorites.md) — manual /
  curated playlists and the built-in Favorites
- [History & Statistics](History-and-Statistics.md) — what powers
  *Recently launched*, *Top played*, and *Never played*
- [Item Metadata](Item-Metadata.md) — custom fields, manual links

## For developers

- Filter spec: [src/utils/db/smartfilter.h](../../src/utils/db/smartfilter.h)
  — the discriminated `Filter` struct, JSON serialization, kind-tag
  strings.
- Evaluator: [src/utils/db/smartplaylistevaluator.cpp](../../src/utils/db/smartplaylistevaluator.cpp)
  — per-kind SQL queries against the items table on the worker
  connection.
- Create / edit dialog:
  [src/ui/dialogs/collection/createsmartplaylistdialog.cpp](../../src/ui/dialogs/collection/createsmartplaylistdialog.cpp).
- Storage: `playlists.is_smart` flag and `playlists.smart_filter` JSON
  column (added as ALTER TABLE ADD COLUMN — safe on upgrade).
- Context-menu entries are in
  [interactionmanager_contextmenu.cpp](../../src/modules/input/interaction/interactionmanager_contextmenu.cpp);
  the dialog is launched via
  [DialogController::runSmartPlaylistDialog](../../src/core/dialogcontroller.cpp).
- Adding a new filter kind: extend `SmartFilter::Kind`, add a tag in
  `kindToTag` / `tagToKind`, add a stack page in the create dialog, and
  add a query in the evaluator. The JSON shape stays the same — new
  per-kind parameters can be added as additional optional fields.
