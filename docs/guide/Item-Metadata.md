# Item Metadata

Every item in a collection carries a small set of **system metadata**
(file path, size, modified date, play count, last played) and any
amount of **user-defined metadata** (custom fields, manual file links,
artwork links, launcher overrides).

This page covers everything you can attach to an individual item and
the dialogs / context menus that put it there. The matching
right-click menu reference is in
[Input & Controls → Context menus](Input-and-Controls.md#context-menus).

> **Where to find this** — Right-click on any item. The Settings
> Dialog has no direct per-item editor; everything per-item lives in
> the context menu.

## What's stored per item

| Source | Where | Notes |
|--------|-------|-------|
| Filename, size, mtime | Filesystem | Auto-discovered on scan. |
| Display name | Computed | After title-pattern cleanup ([title patterns](Search-Sort-Filter.md#title-pattern-exclusion)). |
| Play count | Database (`items.play_count`) | Incremented on every launch. |
| Last played | Database (`items.last_played`) | Timestamp. |
| Time played | Database (`launch_history.duration_seconds`) | Sum across history rows. Only populated if [runtime detection](Splash-and-Now-Playing.md#runtime-detection) is on. |
| Notes | Database (`item_metadata.notes`) | Free-form multi-line text. |
| Tags | Database (`item_metadata.tags`) | JSON array; surfaced in search via `tag:` token. |
| Rating | Database (`item_metadata.rating`) | 0–10 in half-star steps; `-1` = unrated. |
| Source URL | Database (`item_metadata.source_url`) | Where you got the item (e.g. purchase link). |
| Custom fields | Database (`item_metadata.custom_fields`) | User-defined key-value pairs. |
| Pinned / Hidden / Continue later | Database (`item_metadata.is_pinned` / `is_hidden` / `continue_later`) | Boolean state flags, surfaced as tile badges. |
| Manual file path | Database (`item_metadata.manual_path`) | Per-item link to a PDF / EPUB / etc. |
| Launcher override | Database (`item_metadata.launcher_index`) | Per-item launcher choice. |
| Artwork links | Database (`item_artwork`) | Per-type, per-item manual artwork file. |

Per-item state lives in `~/.local/share/kartend/kartend.db` (see
[File Locations](File-Locations.md#database)). Survives collection
rename and rescans, keyed by `(collection_uuid, source_path)`. The
metadata, tags, rating, source URL, custom fields, manual links,
artwork links, and state flags all ride along inside
[`.kart` exports](Backup-and-Migration.md).

## Personal metadata: notes, tags, rating, source URL, custom fields

The **Edit Metadata** dialog is the one-stop editor for everything
you'd want to attach to an item by hand — notes, a tag list, a
half-star rating, a source URL, and any number of free-form
key/value custom fields. It replaces the older custom-fields-only
dialog.

Open via right-click → **Edit metadata…**, the **Edit Metadata**
button in the [details pane title row](#details-pane), or the same
button on the [detail page](#detail-page).

### Edit Metadata Dialog

```
┌────────────────────────────────────────────────────┐
│ Edit metadata                                      │
│ <item name>                                        │
│                                                    │
│ Notes ┌────────────────────────────────────────┐   │
│       │ Loved the second act; reminded me of   │   │
│       │ the older director cut.                │   │
│       └────────────────────────────────────────┘   │
│ Tags  [comfort, rewatch, holiday               ]   │
│ Rating ★★★★☆   [Clear]   (4.0 / 5)                 │
│ Source URL [https://www.example.com/…          ]   │
│                                                    │
│ Custom fields                                      │
│ ┌────────────┬─────────────────────────────────┐   │
│ │ Key        │ Value                           │   │
│ ├────────────┼─────────────────────────────────┤   │
│ │ runtime    │ 142m                            │   │
│ │ language   │ en, fr, de                      │   │
│ └────────────┴─────────────────────────────────┘   │
│ [Add row] [Remove row]                             │
│                              [Cancel]   [Save]     │
└────────────────────────────────────────────────────┘
```

| Field | Behavior |
|-------|----------|
| **Notes** | Free-form multi-line text. Empty = unset. Surfaced in the sidebar's Item tab and, when [search mode All](Search-Sort-Filter.md#search-modes) is active, in search. |
| **Tags** | Comma-separated. Whitespace is trimmed and case-insensitive duplicates are dropped on save, so `comfort, comfort,COMFORT` becomes `comfort`. Searchable via the `tag:NAME` [structured token](Search-Sort-Filter.md#structured-search-tokens). |
| **Rating** | Half-star widget (5 stars, 0–10 internal scale). Click to set; right-click on a star to clear back to "unrated". The numeric label next to the widget shows the value out of 5. |
| **Source URL** | Optional. Plain text; surfaced as a clickable link in the sidebar / detail page. |
| **Custom fields** | Same key/value table as before — directly-editable cells, **Add row** / **Remove row** buttons. Empty rows are dropped silently on save. |

The dialog operates on an in-memory payload; **Save** persists every
field to the database in a single transaction and the sidebar
refreshes immediately. Partial input survives a round-trip without
clobbering other rows — leaving Notes empty doesn't clear an
existing rating, for example.

### Persistence

Stored on the `item_metadata` table, keyed by
`(collection_uuid, source_path)`. The hand-edited fields are:

| Column | Type | Notes |
|--------|------|-------|
| `notes` | TEXT | Empty = NULL. |
| `tags` | TEXT | Compact JSON array of strings. |
| `rating` | INTEGER | 0–10 in half-star steps; NULL = unrated. |
| `source_url` | TEXT | Empty = NULL. |
| `custom_fields` | TEXT | Compact JSON object of `{ "key": "value" }` pairs. |

Survives collection rename and rescans. Round-trips inside
[`.kart` exports](Backup-and-Migration.md).

To query from outside Kartend:

```sql
SELECT notes, tags, rating, source_url, custom_fields
FROM item_metadata
WHERE collection_uuid = '...' AND path = '/path/to/item.mkv';
```

## Manual files

Attach a PDF / EPUB / manual page / strategy guide / readme to any
item. Useful when your media item is the file but the *associated
documentation* is something else.

Right-click → **Set manual file…** opens a file picker. Recognized
formats:

- Documents: `.pdf`, `.epub`, `.cbr`, `.cbz`, `.djvu`
- Text: `.txt`, `.md`, `.rtf`
- Web: `.html`, `.htm`
- Office: `.doc`, `.docx`, `.odt`
- Images: `.png`, `.jpg`, `.jpeg`

Once set, the sidebar's Item tab shows a clickable **Manual file** row.
Clicking opens the file with `xdg-open` (the system's default app for
the type). Right-click → **Clear manual override** removes the link.

### Manual directory and auto-discovery

If the collection has a **Manual Directory** set
(`manualDirectory=...`), Kartend will *also* auto-discover manuals by
filename (same base-filename matching as artwork). Auto-discovered
manuals show up the same way; per-item manual links override
auto-discovery.

```ini
[Books]
mediaDirectory=~/library
manualDirectory=~/library/manuals
extensions=mobi,epub
```

```
~/library/Some Book.mobi
~/library/manuals/Some Book.pdf   ← auto-discovered as the manual
```

## DAT-file identification (ROM collections)

For collections of ROMs or other hash-identifiable media, Kartend can
read **DAT files** (No-Intro / Redump / TOSEC Logiqx, or MAME
`listxml`) and use the canonical title from the DAT in place of the
filename. Useful when:

- Your files are stored with cryptic short names (`smb1u.nes`) and
  you want clean titles in the grid.
- You're scraping ScreenScraper.fr and want hash-based matching for
  region/revision accuracy (the [Scraper](Scraper.md) page covers the
  scrape side).

### Configuring DAT files

Per-collection, in **Settings → Collection → Scraper** (or by hand in
`kartend.cfg`):

```ini
[Arcade ROMs]
datFilePaths\1\path=~/dats/MAME 0.265.dat
datFilePaths\2\path=~/dats/No-Intro NES.dat
```

Each entry is one DAT file. Order matters: DATs are walked in list
order and **the first hash hit wins**. Put your most-specific DATs
first, fallbacks last.

The legacy single-path key `datFilePath` is still read for
backward-compat, but new writes go to the `datFilePaths` array — see
[Configuration Reference](Configuration-Reference.md#scraper-overrides).

### What gets matched

Kartend hashes each item file (CRC32 / MD5 / SHA-1, whichever the DAT
exposes) and looks up the hash in the parsed DAT cache. A match
contributes:

- **Canonical title** — replaces the filename-derived display title.
- **Region / revision tags** — surfaced in metadata where the DAT
  provides them.
- **Hash anchor** — feeds the scraper's hash-based search when one is
  available (e.g. ScreenScraper).

A miss is silent — the item keeps its filename-derived title and
proceeds normally through any other scrape steps.

### Performance

The DAT parse cache (on disk, per-DAT-file mtime) means parsing only
happens when a DAT file changes; subsequent launches use the cached
representation. For very large MAME `listxml` DATs the first launch
after a DAT update can take several seconds while the cache rebuilds.

## Item artwork links

Per-item manual links for artwork *types*. Useful when:

- Auto-discovery picks the wrong file (multiple matches with different
  suffixes).
- You want to attach artwork to a custom type that doesn't auto-discover.
- You want to point at artwork in a folder Kartend wouldn't normally
  scan.

Open via right-click → **Edit artwork links…** (in some builds, this
is on the **Set manual file…** path or accessible from the sidebar
gallery).

### Item Artwork Links Dialog

The dialog presents one row per artwork type (standard + custom),
each with a path field, a **Browse** button, and a **Clear** button.
See [Artwork → Manual per-item links](Artwork.md#manual-per-item-links)
for the full walkthrough — opening, linking, clearing, and how
overrides interact with auto-discovery.

Stored in the `item_artwork` table:
`(collection_uuid, source_path, artwork_type, artwork_file)`.

## Per-item launcher override

If a collection has more than one launcher (primary + additional),
right-click → **Always launch with…** opens the
[Launcher Chooser Dialog](Launchers.md#launcher-chooser-dialog) with
the current default pre-selected. Picking a launcher creates an
override stored against this item.

| Action | Effect |
|--------|--------|
| **Always launch with…** | Open chooser; selection saves per-item. |
| **Clear launcher override** | Remove the override; revert to collection default. (Appears only if an override is set.) |

Override is persisted as `item_metadata.launcher_index` (the index
into the collection's combined launcher list — primary at 0,
additionals at 1..N). Survives collection rename and rescans.

The right-click menu shows **Always launch with…** only when the
collection has more than one launcher (otherwise there's nothing to
choose).

## State flags

Three per-item booleans that surface in the UI as small badges on the
tile and as smart-playlist filters:

| Flag | INI / DB | Badge | Effect |
|------|----------|-------|--------|
| **Pinned** | `item_metadata.is_pinned` | ★ (top-right of tile) | Highlights the item as a personal favorite for that collection. Pair with the **Pinned** smart-playlist kind for a curated tile. |
| **Hidden** | `item_metadata.is_hidden` | ∅ | Filtered out of the regular grid by default. Surface them via the **Hidden** smart-playlist kind when you want to review or unhide. |
| **Continue later** | `item_metadata.continue_later` | ⏵ | Marks the item as in-progress / on the resume list. Pair with the **Continue later** smart-playlist kind for a "pick up where I left off" view. |

> **Where to find this** — right-click an item to toggle each flag.
> Bulk-toggle via the [Bulk Edit Dialog](#bulk-edit-dialog).

Badges paint on top of the tile artwork in the top-right corner; they
repaint without database queries (the flag set is loaded once per
collection and cached). Hidden items don't render in the grid by
default, so you won't see the badge unless you're inside a smart
playlist that surfaces them.

### Bulk Edit Dialog

For collection-wide changes there's a **Bulk Edit Dialog**: pick an
action (Add Tag / Remove Tag / Set Pinned / Set Hidden / Set Continue
Later / Clear Rating) and a parameter (the tag name, when applicable),
then apply to every item in the active collection. Useful for marking
an entire backlog as Continue Later in one shot, or stripping a tag you
no longer want.

Accessed via **File → Bulk Edit Items…** when a collection is active.
Confirmation prompt shows the affected item count before any database
writes happen.

## Missing-metadata review

For working through a partially-populated collection one item at a
time, **File → Review Missing Metadata…** opens a queue dialog that
walks every item in the active collection with empty
title / description / genre / artwork.

```
┌────────────────────────────────────────────────────────────┐
│ Review missing metadata — 14 of 92 remaining               │
│                                                            │
│ Item: Some Album.flac                                      │
│ Missing: Description, Genre                                │
│                                                            │
│ [ Edit metadata… ]   [ Skip ]   [ Stop ]                   │
└────────────────────────────────────────────────────────────┘
```

| Action | Effect |
|--------|--------|
| **Edit metadata…** | Opens the [Edit Metadata Dialog](#edit-metadata-dialog) on this item; after save, re-evaluates whether the item still belongs in the queue and either dismisses it (now complete) or keeps it (still missing fields). |
| **Skip** | Leave the item alone and advance to the next. |
| **Stop** | Close the dialog. Items you've completed stay completed; the rest stay in the queue for next time. |

The queue is rebuilt each time the dialog opens, so newly-added
items (or items that lost metadata via a partial rescrape) show up
automatically.

## Detail page

Press `I` (or click the toolbar's `ℹ` button) on a selected item to
open the **full-screen detail page** — a larger, more deliberate view
of the item than the sidebar provides.

The detail page shows:

- A larger artwork display (cycles through types using the same
  modifier+middle-click as the gallery).
- Notes, tags, rating, source URL, and all custom fields, formatted
  spaciously.
- Manual file link (if set).
- Action buttons: **Launch**, **Edit metadata…** (opens the unified
  [Edit Metadata Dialog](#edit-metadata-dialog)), **Set manual…**,
  **Edit artwork links…**.
- File path / size / mtime / play count / last played.

Press `Escape` to close. The sidebar is hidden while the detail page
is open and restored when you close it.

The detail page is a kiosk-friendly information surface — it's where
you'd land when an item's been selected for a while in attract mode,
and the surface a remote user can navigate via gamepad to "see more"
without learning the sidebar's mechanics.

> **Where to find this** — `I` key (rebindable as `keyItemDetails`),
> or toolbar **Detail Page** button (`ℹ`).

## Details pane

The sidebar's Details Pane title row carries an inline **Edit
Metadata** button (new on this branch) — same dialog as the
right-click and detail-page entry points, but reachable in one click
from the current selection without leaving the grid. Pair with the
**Edit Links…** button (artwork) to make the sidebar a self-contained
editor surface.

## Recipes

### Tag completed items

Right-click each completed item → **Edit metadata…** → add `completed`
to the tag list. Then search with `tag:completed` to surface the
completed pile, or build a smart playlist using the
**By title search** kind on a custom field if you want a persistent
view.

For a richer set of states (completed / in-progress / dropped),
combine the tag list with the **Continue later** state flag —
toggling it from the right-click menu is faster than editing a
custom field per item.

### Add manual links to a books collection

```ini
[Library]
mediaDirectory=~/books
manualDirectory=~/books/translations  ; auto-discover translation PDFs
extensions=epub,mobi
```

Items get translation PDFs linked automatically by base-filename match.
Override on a per-item basis if needed via right-click.

### Override a single item's launcher to use a different player

The collection uses `mpv` by default. For one specific file (a video
that plays better in `vlc`):

1. Add `vlc` as an additional launcher (Settings → Launcher).
2. Right-click the problem item → **Always launch with… → vlc**.

That item launches in `vlc`; everything else stays on `mpv`.

### Add per-item screenshots without scraping

```ini
[Films]
mediaDirectory=~/Videos/Films
artworkDirectory=~/Videos/Films/_art
customArtworkTypes=my-screenshot
```

Right-click each item → **Edit artwork links…** → add `my-screenshot`
type, browse to your screenshot. Sidebar gallery shows it next to the
auto-discovered cover.

## Where to next

- [Artwork](Artwork.md) — auto-discovery rules, types, gallery
- [Launchers](Launchers.md) — multi-launcher and overrides
- [History & Statistics](History-and-Statistics.md) — play count,
  last played, time played
- [Sidebar & Details Pane](Sidebar-and-Details-Pane.md) — where this
  metadata is rendered

## For developers

- Database schema: `item_metadata`, `item_artwork`, `items` tables.
  See [src/utils/db/](../../src/utils/db/) for per-store namespaces
  (`ItemMetadataStore`, `ItemArtworkStore`), and
  [src/modules/data/database/](../../src/modules/data/database/) for
  the `DatabaseManager` facade.
- Hand-edited per-item state on `item_metadata`: `notes` (TEXT),
  `tags` (compact JSON array), `rating` (INTEGER 0–10, half-star),
  `source_url` (TEXT), `custom_fields` (compact JSON object),
  `is_pinned` / `is_hidden` / `continue_later` (INTEGER 0/1).
  Migrations v14 and v15 add these columns to the existing table.
- Edit Metadata Dialog:
  [src/ui/dialogs/item/editmetadatadialog.{h,cpp}](../../src/ui/dialogs/item/);
  the rating widget is `StarRatingWidget` in the same directory.
  The dialog operates on an `EditMetadataPayload` struct
  (`src/utils/db/itemmetadata.h`); the caller persists via
  `DatabaseManager::saveItemMetadata`.
- Bulk Edit Dialog: [src/ui/dialogs/item/bulkeditdialog.{h,cpp}](../../src/ui/dialogs/item/);
  the bulk mutation primitives live in
  [src/utils/db/bulkedit.{h,cpp}](../../src/utils/db/).
- State-flag badges paint in
  [src/chrome/items/itemwidgetpaint.cpp](../../src/chrome/items/itemwidgetpaint.cpp);
  the flag set is loaded once per collection by
  `ItemWidget::applyStateFlags` and cached.
- Detail page: [src/modules/media/detailpage/](../../src/modules/media/detailpage/)
  (`DetailPageManager`).
- Item Artwork Links Dialog: [src/ui/dialogs/collection/itemartworklinksdialog.h](../../src/ui/dialogs/collection/).
- Launcher chooser: [src/ui/dialogs/launcher/launcherchooserdialog.h](../../src/ui/dialogs/launcher/).
- Adding a new well-known item field: extend `ItemMetadataStore::ItemMetadata`
  with the typed column, bump the migration table in
  [src/utils/db/dbmigrations.cpp](../../src/utils/db/dbmigrations.cpp),
  surface it in `EditMetadataPayload` if user-editable, and render it
  in the sidebar's metadata view.
