# Artwork

Kartend treats artwork as a separate concern from media — you point
each collection at a folder of images, and Kartend matches them to
items by filename. There can be many artwork *types* per item (cover,
backdrop, screenshots, marquee, label…), accessible through the
sidebar gallery and selectable as the tile face. Per-item manual links
let you override anything that auto-discovery gets wrong, and a
placeholder system handles items that have no artwork at all.

> **Where to find this** — Settings Dialog → **Paths & Extensions**
> tab → **Artwork Directory** / **Placeholder Artwork**, plus
> **Custom Artwork Types** on the same tab. Per-item links live in the
> right-click context menu.

## How matching works

For every item file in the media directory, Kartend looks in the
artwork directory for an image whose **base filename** (everything
before the last `.`) matches:

```
mediaDirectory/Some Movie (2021).mkv
artworkDirectory/Some Movie (2021).png   ← matches
artworkDirectory/Some Movie (2021).jpg   ← also matches
artworkDirectory/Some Movie (2021).webp  ← also matches
artworkDirectory/Some Movie (2021).jpeg  ← also matches
```

Recognized extensions: `.png`, `.jpg`, `.jpeg`, `.webp`. Match is
case-sensitive on case-sensitive filesystems (the typical Linux setup).

If multiple matches exist (`.png` *and* `.jpg`), the first one found
wins; you can force a specific image by deleting the others or by
[manually linking](#manual-per-item-links) the preferred one.

### Subfolders

Set `includeArtworkSubfolders=true` to recurse into subfolders. Useful
for collections where artwork is grouped per-item:

```
artworkDirectory/Some Movie (2021)/cover.png
artworkDirectory/Some Movie (2021)/backdrop.jpg
```

The first base-filename match within the entire artwork tree wins.

## Artwork types

Beyond the single "tile face" image, Kartend supports multiple artwork
*types* per item — covers, posters, screenshots, marquees, and the
like.

Standard types Kartend looks for at scan time:

| Type id | Conventional filename suffix |
|---------|------------------------------|
| `boxfront` | `A Title-boxfront.png` |
| `boxback` | `…-boxback.png` |
| `cartridge` | `…-cartridge.png` |
| `screenshot` | `…-screenshot.png` |
| `backdrop` | `…-backdrop.png` |
| `marquee` | `…-marquee.png` |
| `label` | `…-label.png` |
| `clearlogo` | `…-clearlogo.png` |
| `bezel` | `…-bezel.png` |

Kartend scans the artwork directory for every standard suffix and
attaches them to the matching item. The set is then browsable in the
[sidebar gallery](#sidebar-gallery).

The "primary" artwork (the one painted on the tile) is the first match
found by the un-suffixed base filename — see the previous section.
Suffix-based matches are gallery-only.

### Custom artwork types

Beyond the standard list, you can declare your own artwork types per
collection:

```ini
[Manuals]
customArtworkTypes=cover,quick-reference,catalog
```

> **Where to find this** — Settings Dialog → **Paths & Extensions** tab
> → **Custom Artwork Types** (free-form text list).

Custom types **do not** auto-discover. They show up as empty slots in
the gallery; populate them via per-item manual links (next section).

## Manual per-item links

When auto-discovery picks the wrong image, or when you want to attach
art to a custom type, use **manual links**:

- Right-click an item → **Edit artwork links…** opens the
  [Item Artwork Links Dialog](Item-Metadata.md#item-artwork-links-dialog).
- Add an entry per type: pick the type, browse to the file.
- Remove an entry to fall back to auto-discovery for that type.

Manual links live in the database (`item_artwork` table), keyed by
`(collection_uuid, source_path, artwork_type)`. They survive rescans,
collection renames, and reorderings.

To copy artwork links to a different filename, you'll need to re-link
through the dialog — there's no "rename source" workflow today.

## Sidebar gallery

The sidebar's **Item** tab shows every artwork type — standard +
custom — that has either an auto-discovered file or a manual link, as
a horizontally-scrollable gallery. The currently-displayed type is the
gallery's selected entry.

### Switching artwork types from the keyboard / mouse

- **Modifier + middle-click** on an item tile cycles through the
  available artwork types for that item. Modifier defaults to `Shift`
  and is configurable globally (`artworkCycleModifier`, see
  [Configuration Reference](Configuration-Reference.md#mouse)).
  Each cycle advances to the next type that has an image.
- The gallery in the sidebar lets you click directly on a type tab to
  jump.

The cycled type is *display-only*: it doesn't change the tile face for
other items, doesn't persist across sessions, and doesn't affect
launching.

### Video tile in the gallery

If the item has a preview video (auto-discovered in the collection's
`videoDirectory` — see [Video Previews](Video-Previews.md)), the
gallery includes a special **video tile** with a play-icon badge.
Clicking it expands the video player in the sidebar. The video tile is
always last in the gallery order.

## Placeholders

Items that have no auto-discovered or manually-linked artwork render as
a **placeholder tile**. Two settings control how those look:

| Setting | INI key | Effect |
|---------|---------|--------|
| Placeholder Artwork (per-collection) | `placeholderArtwork` | Path to an image used in place of the procedural placeholder. |
| Show Title in Placeholder (global) | `showTitleInPlaceholder` | Overlay the item's filename on the placeholder. |
| Hide Missing Artwork (per-collection) | `hideMissingArtwork` | Hide the items entirely instead of showing a placeholder. |

The procedural fallback is a hatched / cross-hatched pattern tinted
with the collection's `tileColor`. `placeholderArtwork` overrides it
completely — useful for branding or for "no artwork yet" indicator
imagery.

`hideMissingArtwork` is the nuclear option: if you'd rather the
collection look pristine than show empty slots, enable it. It only
hides *media items* without artwork; subcollection tiles (which always
render) are unaffected.

## Generating placeholder artwork for subfolders

When a collection has `includeContentSubfolders=true`, virtual folders
appear as tiles. Folders rarely have their own artwork — the
[subfolder artwork generator](../subfolder-artwork.md) is a Python
helper that composes a 2×2 (or 3×3) montage of the artwork *inside*
each folder, saving it as the folder's tile.

Workflow:

```bash
.scripts/subfolder_art_generator.py \
  --media ~/Videos/Films \
  --artwork ~/Videos/Films/_covers \
  --output ~/Videos/Films/_folder-art
```

Then point the collection at the new folder:

```ini
artworkDirectory=~/Videos/Films/_folder-art
includeArtworkSubfolders=true
```

See [docs/subfolder-artwork.md](../subfolder-artwork.md) for the full
script reference and how to tune the montage layout.

## Header logo (separate from artwork)

The **header logo** is a per-collection branding image painted at the
top of the items grid — independent of any per-item artwork. Set:

```ini
headerLogoImage=~/banners/films-logo.png
headerLogoPosition=topcenter   ; topleft / topcenter / topright
```

Use it for the collection's logo / banner. Distinct from the
**collection icon** (`collectionIcon`), which is the image painted on
the *tile* of this collection when it's a subcollection of another.

See [Themes & Appearance](Themes-and-Appearance.md#header-logo) for
positioning details.

## Loading and caching

Artwork loading is asynchronous and parallel. On collection open:

1. Kartend kicks off a parallel pipeline (`QtConcurrent` worker pool)
   to enumerate the artwork directory and match files to items.
2. As tiles enter the viewport, their primary artwork is decoded and
   scaled at priority. Off-screen tiles are deferred.
3. Decoded pixmaps are cached in a global LRU bounded by
   `pixmapCacheSizeMB` (default 50). Cache is shared across collections
   and views.
4. A disk cache under `~/.cache/kartend/` keeps scaled tiles between
   sessions to avoid redundant decode work.

You'll see tiles materialize a fraction of a second after they enter
view; that's the decode-on-demand pipeline.

If artwork *never* shows up, check
[Troubleshooting → artwork issues](Troubleshooting.md#artwork-issues).

## Recipes

### Standard "one cover per item" setup

```
~/Movies/                          ← mediaDirectory
~/Movies/_covers/                  ← artworkDirectory
~/Movies/Some Film (2010).mkv
~/Movies/_covers/Some Film (2010).jpg
```

```ini
[Movies]
mediaDirectory=~/Movies
artworkDirectory=~/Movies/_covers
extensions=mkv,mp4,webm
```

### Per-item folder structure (artwork lives next to media)

```
~/Videos/Films/A Film/
~/Videos/Films/A Film/A Film.mkv
~/Videos/Films/A Film/cover.png
~/Videos/Films/A Film/poster.png
```

```ini
[Films]
mediaDirectory=~/Videos/Films
artworkDirectory=~/Videos/Films
includeArtworkSubfolders=true
extensions=mkv,mp4
```

### Multiple artwork types in a flat directory

```
~/Videos/Films/_art/A Film.png
~/Videos/Films/_art/A Film-poster.png
~/Videos/Films/_art/A Film-screenshot.png
~/Videos/Films/_art/A Film-banner.png
```

The first one (no suffix) is the tile face; the suffixed images all
appear in the gallery.

### Adding a custom "manual" artwork type

```ini
customArtworkTypes=manual
```

Then per-item: right-click → **Edit artwork links…** → add a `manual`
entry pointing at the PDF cover image (or a thumbnail you generated
from the manual).

### Hiding items without artwork

```ini
[Cleaner Library]
hideMissingArtwork=true
```

Items without any artwork (auto or manual) won't appear at all.
Toggleable per-collection; combines with search and other filters.

## For developers

- Loading: [src/modules/media/artwork/](../../src/modules/media/artwork/)
  (`ArtworkManager` orchestrates), with worker functions in
  `artworkutils.cpp`. The pool is `QtConcurrent`-backed.
- Cache: [src/modules/data/cache/](../../src/modules/data/cache/) (`CacheManager`)
  hosts the in-memory LRU and disk persistence.
- Per-item manual links table: `item_artwork(collection_uuid, source_path,
  artwork_type, artwork_file)` in SQLite. See
  [src/utils/db/itemartwork.h](../../src/utils/db/itemartwork.h).
- Sidebar gallery layout: `DetailsPane` in
  [src/ui/widgets/panes/](../../src/ui/widgets/panes/). The gallery tabs are
  constructed dynamically from the union of standard types found and
  the collection's `customArtworkTypes` list.
- Cycle-on-middle-click: handled in `MouseManager::handleMiddleClick`
  with the modifier check using `GeneralSettings::artworkCycleModifier`.
- Subfolder artwork generator: a standalone Python script,
  [.scripts/subfolder_art_generator.py](../../.scripts/subfolder_art_generator.py)
  — see [docs/subfolder-artwork.md](../subfolder-artwork.md).
- Adding a new standard artwork type: extend the standard-types list
  in `artworkutils.h`, add the suffix-match logic, expose in the
  gallery widget. Custom types don't require code changes.
