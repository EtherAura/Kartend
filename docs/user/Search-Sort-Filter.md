# Search, Sort & Filter

How Kartend narrows what's on screen. Three independent mechanisms that
combine: **search** (typed text), **sort** (display order), and
**filter** (a stack of "show only X" rules — by type, by title pattern,
by artwork presence, by subcollection scope).

> **Where to find this** — Toolbar (search bar, filter dropdown), Sort
> menu, Settings → General (collection-type filter, hide-subs, sort
> mode), per-collection Appearance tab (title patterns, hide-missing
> artwork).

## Search

### The search bar

The search bar is the rightmost control on the items-page toolbar.
`/` focuses it (key configurable as `keySearch`); `Escape` clears it
and returns focus to the grid.

| Aspect | Behavior |
|--------|----------|
| Match | Case-insensitive substring on the displayed item name |
| Live | Filters as you type, debounced |
| Scope | Current collection only — does not search across the library |
| Subcollections | Searched too if `showAllSubcollectionItems=true`; otherwise just direct items |
| Title cleanup | Search runs against the **displayed** name (after `titleExclusionPatterns` strips), so `A Film (US)` displayed as `A Film` matches `a film` search |

Press `Enter` while in the search bar to move focus to the filtered
grid; selection lands on the first match. From there `Ctrl+A` selects
the entire search text (e.g. to replace) and arrow keys move within
the filtered results.

### Search modes

The action embedded inside the search bar (LeadingPosition) toggles
between modes:

| Mode | Matches against |
|------|-----------------|
| **Name** (default) | Display name only |
| **All** | Display name + custom fields + manual paths + extended metadata |

Press `/` to focus, then click the mode action to cycle. There's no
keyboard shortcut to cycle modes today.

The available modes depend on the collection: a collection with no
custom fields and no manuals has only Name mode active.

### Structured search tokens

The search bar also recognises `key:value` filter tokens alongside the
plain-text query. Tokens combine — `played:true tag:soundtrack stage`
matches items that have been launched at least once, carry the
`soundtrack` tag, and contain `stage` in their name (or extended
metadata, in All mode). Keys are case-insensitive; values are taken
literally except for `tag:` which case-folds.

| Token | Matches |
|-------|---------|
| `played:true` / `played:false` | Items that have / haven't been launched at least once (uses `launch_history`). |
| `favorite:true` / `favorite:false` | Items present (or absent) in the reserved Favorites playlist. |
| `missing:artwork` | Items with no cover at all — none found in the collection's artwork folder when it was last scanned, and none [linked by hand](Artwork.md#manual-per-item-links). |
| `has:artwork` | Inverse of `missing:artwork` — items with a cover. Auto-discovered covers count, typed cover subfolders included, and so does a hand-linked one (immediately, and only while its file still exists). |
| `tag:NAME` | Items whose tag list contains `NAME` (case-insensitive). Repeat to AND multiple tags: `tag:rewatch tag:holiday`. |

Tokens you type but Kartend doesn't recognise (typos, unsupported
keys) are stripped from the free-text portion so they don't bleed
into FTS, and surfaced as a non-disruptive warning so you know the
filter didn't apply.

> **Where structured tokens come from** — the per-item tag list and
> Favorites flag live in `item_metadata` / the Favorites playlist;
> see [Item Metadata](Item-Metadata.md). Set tags via right-click →
> **Edit metadata…**.

### Cross-collection token search

Structured tokens also work across the whole library, not just the
current collection. Build a smart playlist with the
**By title search** kind for the plain-text component and chain
tokens in the search bar — e.g. `tag:must-watch played:false` inside
an *All Films* shell shows the unplayed must-watches across every
nested collection.

### Performance

Search is backed by SQLite FTS5 for collections with thousands of
items. Decoding artwork doesn't block the filter — you'll see the
filtered count update instantly while tiles continue to materialize.

For very large collections, the **debounce delay** prevents searching
on every keystroke. The debounce is small (sub-100 ms) so it's
imperceptible.

### Loading indicator

While a search is computing on a large collection, a subtle
semi-transparent overlay with a soft pulse fades in over the items
grid. It hides as soon as the filtered set is ready. Pre-filter
tiles fade behind the overlay so you don't see partial results
flicker as the index narrows. The overlay has no setting — it
appears whenever search needs more than a frame to settle.

## Sort

A single global sort mode applies to all collections (lives in
`[General]`). Switch it via the **Sort** menu — see
[Toolbar & Menus → Sort](Toolbar-and-Menus.md#sort).

| Mode | INI value | What it sorts by |
|------|-----------|------------------|
| Name (A → Z) | `NameAscending` | Display name, ascending |
| Name (Z → A) | `NameDescending` | Display name, descending |
| Date (Newest First) | `DateDescending` | File mtime, descending |
| Date (Oldest First) | `DateAscending` | File mtime, ascending |
| Size (Largest First) | `SizeDescending` | File size, descending |
| Size (Smallest First) | `SizeAscending` | File size, ascending |
| Random | `Random` | Pseudo-random per session |

### Random mode

`Random` shuffles items within each collection. The shuffle is
re-rolled every time you re-enter the collection (or click **Soft
Refresh / F5**) — which means the order changes each time, but the
order is stable while you're inside one viewing session.

If you want a different shuffle, exit and re-enter, or hit `F5`.

### Exclude subfolders from sort

The **Exclude Subfolders** option (sort menu, persistent) keeps
subcollection and virtual-folder tiles always at the top — they don't
participate in the sort order. Useful when you sort by date and don't
want subcollections sandwiched between media items.

```ini
[General]
sortMode=DateDescending
excludeSubfoldersFromSort=true
```

## Filters

Filters compose. The visible items are the intersection of:

1. The **type filter** (global)
2. The **hide subcollection tiles** toggle (global)
3. The **title pattern exclusion** (per-collection, on/off)
4. The **hide missing artwork** filter (per-collection)
5. The **search bar** content
6. The **subcollection scope** (when navigating into a subcollection)

You can hold any of them constant and toggle the others.

### Type filter

Each collection has a free-form `type` tag (e.g. `Video`, `Audio`,
`Documents`). The **collection type filter** restricts the visible
collections to ones whose type matches.

Where to set:

- Toolbar **Filter** dropdown → **Collection Type** submenu — pick a
  type from the radio list.
- Settings → General → **Collection Type Filter** — type any value (or
  pick one already in use).
- INI: `[General] collectionTypeFilter=Video`.

The filter affects which collections appear as tiles when you're at the
root or inside a parent. It does not affect already-open collections'
items.

To reset to "show all types," select **(All)** in the dropdown or set
the INI value to empty.

### Hide subcollection tiles

`hideSubcollectionTiles=true` (global) flattens the view by hiding
subcollection tiles — only direct media items render. Combine with the
type filter to make the app behave like a flat library:

```ini
[General]
collectionTypeFilter=Video
hideSubcollectionTiles=true
```

That setup hides the genre / sub-genre folders and shows just videos,
across all "Video"-typed collections.

### Title pattern exclusion

Per-collection regex patterns stripped from displayed item titles.
Useful for region tags, version markers, language codes embedded in
filenames:

```ini
[Films]
titleExclusionPatterns=\s*\(US\),\s*\(EU\),\s*\[!\],\s*\(Rev \d+\)
titleExclusionEnabled=true
```

`A Film (US) [!] (Rev 1).mkv` displays as `A Film`. The underlying file
is unchanged; only the tile text and search index use the cleaned name.

| Setting | INI key | Notes |
|---------|---------|-------|
| Patterns | `titleExclusionPatterns` | CSV of regex patterns. Each is a Qt-regex applied with `replace`. |
| Enabled | `titleExclusionEnabled` | Toggle without losing the pattern list. |

Where to edit: **Toolbar → Filter dropdown → Edit title patterns…**, or
Settings → Appearance → **Title Exclusion Patterns**. Invalid patterns
are skipped at compile time without aborting the rest of the list.

### Hide missing artwork

`hideMissingArtwork=true` (per-collection) hides items that have no
artwork found. Subcollection / virtual-folder tiles are unaffected
(they always render).

This one checks your artwork folder as it is now, not as it was at the
last scan — so it can disagree with `missing:artwork` and the *Missing
artwork* smart playlist for a short while after you add or delete covers,
until that collection is scanned again.

A cover you [linked by hand](Artwork.md#manual-per-item-links) keeps the item
visible. The name-matching pass above can't see a link — the image is usually
named nothing like the item and may not even live in the artwork folder — so
links are consulted separately, before it. That keeps this filter honest about
what the grid draws: it hides the tiles that would paint a placeholder, and a
hand-linked tile paints its cover.

Set per-collection in Settings → Appearance.

### Subcollection scope

Automatic. When you `Enter` into a subcollection, that subcollection's
items + its descendants' items (if `showAllSubcollectionItems=true`)
become the visible set; all other collections drop out.

`Escape` walks back up the hierarchy.

## How filters compose

The filter pipeline at any moment is:

```
all items in current view
  ↓ (type filter)
only collections matching the active type tag
  ↓ (hide-subcollection-tiles)
only media items if enabled
  ↓ (title-pattern exclusion)
displayed name = pattern-stripped name
  ↓ (hide-missing-artwork)
items without artwork dropped
  ↓ (search)
items whose displayed name (or, if All mode, metadata) matches search text
  ↓ (subcollection scope)
items inside the current subcollection only
```

The filtered count updates live in the toolbar's item position label
(`42 / 1000`).

### Clearing all filters

There's no single "clear filters" button. To reset:

- Search bar: click the `×` (or press `Escape` while focused).
- Type filter: pick **(All)** from the dropdown.
- Title patterns: open the editor, uncheck the toggle.
- Subcollection scope: press `Escape` until you're back at the root.
- Hide-subs: toggle off in the toolbar dropdown.

A future "reset filters" affordance is on the wishlist; file a feature
request if you'd like it.

## Recipes

### Find a specific item across many collections

Kartend's search is per-collection. To search globally, either:

1. Create an "All" parent collection with `showAllSubcollectionItems=true`
   and the rest of your collections nested under it. Searching inside
   it searches everything.
2. Use the **Recent** menu (File → Recent) for last-launched items, or
   **Most Launched** (File → Most Launched) for top-played.

### Hide adult / spoiler / WIP items without deleting them

Tag the items with a custom field (Right-click → **Edit custom
fields…** → add `tags=adult`), then exclude with the title-pattern
filter:

```ini
titleExclusionPatterns=\s*\[adult\]
titleExclusionEnabled=true
```

(Requires renaming files with the tag inline. There's no per-tag
hide-from-view today.)

### Default to "Random" sort and a specific type

```ini
[General]
sortMode=Random
collectionTypeFilter=Video
hideSubcollectionTiles=true
```

Boots into a random ordering of videos. Combine with attract mode for
a screensaver-like cycle.

### Show only items with backdrop artwork

There's no per-artwork-type filter. Closest: enable
`hideMissingArtwork=true` (filters items with no *primary* artwork).
For per-type filtering, file a feature request.

## Settings cheat sheet

| Setting | Scope | INI key |
|---------|-------|---------|
| Sort mode | Global | `[General] sortMode` |
| Exclude subfolders from sort | Global | `[General] excludeSubfoldersFromSort` |
| Collection type filter | Global | `[General] collectionTypeFilter` |
| Hide subcollection tiles | Global | `[General] hideSubcollectionTiles` |
| Title pattern list | Per-collection | `titleExclusionPatterns` |
| Title pattern enabled | Per-collection | `titleExclusionEnabled` |
| Hide missing artwork | Per-collection | `hideMissingArtwork` |
| Show all subcollection items | Per-collection | `showAllSubcollectionItems` |
| Show all subfolder items | Per-collection | `showAllSubfolderItems` |
| Search mode | Runtime only | (not persisted) |

## For developers

- Search: [src/modules/input/search/](../../src/modules/input/search/) (`SearchManager`)
  manages the search bar; FTS5 query layer is in
  [src/modules/data/query/](../../src/modules/data/query/) (`QueryManager`).
- Title pattern stripping: `TitleFilter`
  ([src/utils/text/titlefilter.h](../../src/utils/text/titlefilter.h)),
  per-collection.
- Sort: applied in `QueryManager` at SQL level for `sortMode`.
- Filter pipeline: `FilterManager`
  ([src/modules/input/filter/](../../src/modules/input/filter/))
  owns the active filter set; `ScrollManager` consumes the filtered
  index list and maps visual index ↔ source-item index for virtual
  scrolling.
- Search modes are an enum (`SearchMode`) defined in
  [src/utils/text/searchutils.h](../../src/utils/text/searchutils.h).
- Adding a new filter (e.g. tag-based): add a predicate in
  `FilterManager`, wire UI in the toolbar Filter dropdown, persist a
  new `[General]` key in `GeneralSettings`.
