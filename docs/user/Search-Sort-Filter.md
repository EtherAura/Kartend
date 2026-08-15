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
| Match | **Per-token prefix**, case-insensitive. Each word you type gets an implicit `*` and the words are ANDed, so `key note` finds `Keynote Session` but `ynote` finds nothing. |
| Fields | The item's stored **name and path**. Metadata, custom fields and manual paths are not searched. |
| Live | Filters as you type, debounced |
| Scope | Set by the search-mode toggle — see below. Defaults to the current collection. |
| Title cleanup | Search runs against the **stored** name, not the displayed one. `titleExclusionPatterns` is applied for display only, after results come back, so `A Film (US)` shown as `A Film` still matches a search for `US`. |

`Escape` in the search bar clears the text but keeps focus there;
press it again to return to the grid. `Ctrl+A` selects the entire
search text. There is no Return binding — results are already live.

### Search modes

The action embedded inside the search bar toggles **how wide** the
search reaches. It is a scope selector, not a field selector — no mode
searches metadata.

| Mode | Scope |
|------|-------|
| **Current collection** (default) | Direct items in the open collection |
| **Current + subcollections** | Also items in every collection nested beneath it |
| **All collections** | The whole library |

Click the mode action to cycle, or press `/` again while the search bar
is already focused and empty — that also cycles.

The available modes depend on what's actually there: the cycle is built
from whether the collection has subcollections, whether it has real
direct items, and whether any other root collection has items. A flat,
only collection therefore offers one mode.

Searching from the Home / root view always covers all collections.

### Structured search tokens

The search bar also recognises `key:value` filter tokens alongside the
plain-text query. Tokens combine — `played:true tag:soundtrack stage`
matches items that have been launched at least once, carry the
`soundtrack` tag, and have a name or path with a word starting
`stage`. Keys are case-insensitive, and so are the values for every
token except the `tag:` name, which is matched case-insensitively as a
whole word.

| Token | Matches |
|-------|---------|
| `played:true` / `played:false` | Items that have / haven't been launched at least once. Reads the item's own `play_count`, not the launch-history log, so it still works with history recording turned off. |
| `favorite:true` / `favorite:false` | Items present (or absent) in the reserved Favorites playlist. `favourite:` is accepted as an alias. |
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
on every keystroke. The debounce adapts to your typing speed within
80–250 ms (120 ms baseline), so it's
imperceptible.

### Loading indicator

While a search is computing on a large collection, a subtle
semi-transparent overlay with a soft pulse fades in over the items
grid. It hides as soon as the filtered set is ready. Pre-filter
tiles fade behind the overlay so you don't see partial results
flicker as the index narrows. The overlay has no setting — it
is shown at every search dispatch, then dismissed when results land —
so a fast search flashes it briefly rather than skipping it.

## Sort

A single global sort mode applies to all collections (lives in
`[General]`). Switch it via the **Sort** menu — see
[Toolbar & Menus → Sort](Toolbar-and-Menus.md#sort).

`sortMode` is stored as an **integer**, not a name. Writing
`sortMode=DateDescending` into the INI reads back as `0` (Name A→Z),
silently.

| Mode | INI value | What it sorts by |
|------|-----------|------------------|
| Name (A → Z) | `0` | Stored name, ascending |
| Name (Z → A) | `1` | Stored name, descending |
| Collection (A → Z) | `2` | Owning collection name, ascending |
| Collection (Z → A) | `3` | Owning collection name, descending |
| Artwork first | `4` | Items with a cover before items without |
| Artwork last | `5` | The inverse |
| Random | `6` | Shuffled — see below |
| Date (Newest First) | `7` | File mtime, descending |
| Date (Oldest First) | `8` | File mtime, ascending |
| Size (Largest First) | `9` | File size, descending |
| Size (Smallest First) | `10` | File size, ascending |

Name is the tie-break for the date and size modes.

Modes `2`–`5` are not in the Sort menu — Collection and Artwork
ordering are reachable by clicking the List view's column headers. They
persist like any other sort mode.

### Random mode

`Random` shuffles items within each collection. The permutation is
computed once and cached against the collection's item set, filter and
sort mode, so it is stable — re-entering the collection or pressing
`F5` reproduces the *same* order, not a new one.

The shuffle re-rolls when that cache is invalidated: after a scan, and
after anything that changes usage data (launching an item, toggling a
favorite, resetting stats). There is no "reshuffle now" gesture.

### Exclude subfolders from sort

The **Exclude Subfolders** option (sort menu, persistent) keeps
subcollection and virtual-folder tiles always at the top — they don't
participate in the sort order. Useful when you sort by date and don't
want subcollections sandwiched between media items.

```ini
[General]
sortMode=7
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
is unchanged, and so is the search index — cleanup is applied to the
tile text only, after results come back. Searching for a fragment you
stripped from the display still finds the item.

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
(`42 / 1000`). The left number is the **selected item's position**, not
a count of matches; the right one is the filtered total.

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
- Sort: `QueryManager` builds the `ORDER BY` for the name, date, size
  and collection modes. `Random` is materialised into
  `sorted_items_cache` by a Fisher-Yates shuffle in C++, and
  `ArtworkFirst` / `ArtworkLast` fall through to `ORDER BY name` in SQL
  and are re-sorted in `ScrollDataStore`.
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
