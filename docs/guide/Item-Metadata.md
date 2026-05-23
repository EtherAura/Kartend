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
| Custom fields | Database (`item_metadata`) | User-defined key-value pairs. |
| Manual file path | Database (`item_metadata.manual_path`) | Per-item link to a PDF / EPUB / etc. |
| Launcher override | Database (`item_metadata.launcher_index`) | Per-item launcher choice. |
| Artwork links | Database (`item_artwork`) | Per-type, per-item manual artwork file. |

Per-item state lives in `~/.local/share/kartend/kartend.db` (see
[File Locations](File-Locations.md#database)). Survives collection
rename and rescans, keyed by `(collection_uuid, source_path)`.

## Custom fields

User-defined key-value metadata attached to a single item. Useful for
information no auto-scraper would know:

- Personal ratings
- Completion status
- Notes / memorable quotes
- Cross-references

Open via right-click → **Edit custom fields…** (Custom Fields Dialog).

### Custom Fields Dialog

```
┌────────────────────────────────────────────┐
│ Custom Fields — <item name>                │
│                                            │
│  Field          │  Value                   │
│  ───────────────┼─────────────────────     │
│  rating         │  ★★★★☆                  │
│  status         │  in progress             │
│  notes          │  great soundtrack        │
│                                            │
│  + Add field    × Remove                   │
│                                            │
│            [ Cancel ]    [ Save ]          │
└────────────────────────────────────────────┘
```

- **Add field** — appends a new blank row.
- **Remove** — drops the selected row.
- **Save** — persists to the database; the sidebar refreshes
  immediately.

Field names are case-sensitive; values are free-form text. There's no
validation — store URLs, multi-line text (limited rendering), star
ratings as Unicode characters, whatever. The sidebar's metadata
section displays each pair as a row.

### Persistence

Stored as `(collection_uuid, source_path, field_name, field_value)`
rows in the `item_metadata` table. Unique constraint on `(collection,
path, field_name)` — saving a duplicate field name overwrites the
previous value.

To query custom fields from outside Kartend:

```sql
SELECT field_name, field_value FROM item_metadata
WHERE collection_uuid = '...' AND source_path = '/path/to/item.sfc'
  AND field_name NOT IN ('manual_path', 'launcher_index');
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

## Detail page

Press `I` (or click the toolbar's `ℹ` button) on a selected item to
open the **full-screen detail page** — a larger, more deliberate view
of the item than the sidebar provides.

The detail page shows:

- A larger artwork display (cycles through types using the same
  modifier+middle-click as the gallery).
- All custom fields, formatted spaciously.
- Manual file link (if set).
- Action buttons: **Launch**, **Edit custom fields…**, **Set manual…**,
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

## Recipes

### Tag completed items

Right-click each completed item → **Edit custom fields…** → add
`status=completed`, `completed_date=2025-04-12`, `notes=loved the
ending`.

Sidebar will display these every time you select the item. Search
mode **All** will match against `completed` if you search that text.

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
- Custom fields use the same `item_metadata` table as `manual_path`
  and `launcher_index` — they're stored as ordinary `(field_name,
  field_value)` rows. The "system" fields (`manual_path`,
  `launcher_index`) are well-known names; custom fields are anything
  else.
- Detail page: [src/modules/media/detailpage/](../../src/modules/media/detailpage/)
  (`DetailPageManager`).
- Custom Fields Dialog: [src/ui/dialogs/customfieldsdialog.h](../../src/ui/dialogs/).
- Item Artwork Links Dialog: [src/ui/dialogs/collection/itemartworklinksdialog.h](../../src/ui/dialogs/collection/).
- Launcher chooser: [src/ui/dialogs/launcher/launcherchooserdialog.h](../../src/ui/dialogs/launcher/).
- Adding a new well-known item field: extend the relevant store with a
  typed accessor (e.g. `setLastPlayed`, `lastPlayedFor`) rather than
  treating it as an arbitrary custom field — the typed accessor lets
  the sidebar render it specially.
