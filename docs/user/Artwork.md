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

Recognized extensions: `.png`, `.jpg`, `.jpeg`, `.bmp`, `.gif`,
`.webp`, and `.avif` where your Qt build has an AVIF image plugin.
Match is
case-sensitive on case-sensitive filesystems (the typical Linux setup).

If multiple matches exist (`.png` *and* `.jpg`), one of them wins and
which one is not defined — the cached directory index keeps whichever
the filesystem listed first. Don't rely on extension precedence: delete
the ones you don't want, or [link the preferred one by
hand](#manual-per-item-links).

### Artwork for multi-disc releases

An image whose own name carries a disc marker also answers to the
release it belongs to:

```
artworkDirectory/Recital (Disc 1).png   ← art for the item "Recital"
```

This is what makes [multi-disc grouping](Collections.md#multi-disc-grouping)
work against an art folder filed per disc: the grouped item is named
`Recital`, with the disc tag stripped, and nothing on disk carries
that name exactly.

The rule, in order:

1. An image named for the item exactly (`Recital.png`) always wins —
   anywhere in the search, including the typed subfolders below.
2. Only if there is none does a disc-marked image stand in, and the
   **lowest disc** is the one taken: disc 1 before disc 2, numbered
   discs before lettered sides. `Recital (CD 2).png` is used when
   disc 1 has no art at all.
3. A [manual per-item link](#manual-per-item-links) overrides both.

The markers recognised are the ones
[multi-disc grouping](Collections.md#what-counts-as-a-disc-marker)
uses — `(Disc 1)`, `(Disk 2)`, `(CD3)`, `[Side A]`, inside brackets,
case-insensitive. Any other parenthesised tag is part of the title, so
`Some Movie (2021).png` never stands in for an item called
`Some Movie`.

Nothing about this requires grouping to be on, and it never replaces a
match an item already had.

### Subfolders

Set `includeArtworkSubfolders=true` when your artwork folder is
arranged in the same shape as your content folder. An item's cover is
then looked for in the artwork subfolder matching the item's own
subfolder, rather than at the artwork folder's root:

```
mediaDirectory/Live/Recital.flac
artworkDirectory/Live/Recital.png     ← the cover for that item
```

Nesting is followed to any depth, and the match is still on the item's
base filename — the artwork subfolder mirrors *where* the item lives,
not *what* its cover is called.

Pointing `artworkDirectory` at the same folder as `mediaDirectory`
mirrors the same way without the setting, since art kept beside your
content is already arranged that way by definition.

An item that does not live under `mediaDirectory` has no subfolder to
mirror — a [grouped multi-disc release](Collections.md#multi-disc-grouping)
is the usual case, since the playlist standing in for it lives in
Kartend's own data folder. Its cover is looked for in the artwork
folder itself.

## Artwork types

Beyond the single "tile face" image, Kartend supports multiple artwork
*types* per item — covers, posters, screenshots, marquees, and the
like.

Types are **subdirectories of `artworkDirectory`**, not filename
suffixes. An image for type `screenshot` belonging to item `A Title`
lives at `<artworkDirectory>/screenshot/A Title.png`. There is no
`-screenshot` suffix convention anywhere in Kartend.

```
<artworkDirectory>/
├── A Title.png          ← flat root: the plain cover
├── front/A Title.png
├── box/A Title.png
├── screenshot/A Title.png
├── marquee/A Title.png
└── logo/A Title.png
```

Two lists matter, and they are not the same list.

**Cover types** — the ones that can supply the image painted on a grid
tile or cover-flow card, in the order the search tries them:

`front` · `box` · `box-3d` · `mixrbv1` · `mixrbv2` · `screenshot` ·
`title` · `fanart` · `marquee`

**Gallery types** — what the sidebar gallery offers, in display order:

`front` · `box` · `screenshot` · `title` · `marquee` · `fanart` ·
`logo`

`logo` is the difference: it is a gallery type but not a cover type, so
a logo never becomes the tile face and a hand-linked logo does not make
an item count as "has artwork".

The **primary** artwork — the one painted on the tile — is the first
hit of this cascade:

1. A [per-item manual link](#manual-per-item-links) on any cover type.
2. `<artworkDirectory>/<Base name>.<ext>` — the flat root.
3. Each cover-type subdirectory in the order listed above.
4. The [disc-marker fallback](#artwork-for-multi-disc-releases).

So a typed-subdirectory image *can* be the tile face — that is
deliberate, and it is what lets hand-dropped gallery art surface on the
grid rather than sitting unused. It is only reached when nothing
matched at the flat root.

### Custom artwork types

Beyond the standard list, you can declare your own artwork types per
collection:

```ini
[Manuals]
customArtworkTypes=cover,quick-reference,catalog
```

> **Where to find this** — Settings Dialog → **Paths & Extensions** tab
> → **Custom Artwork Types** (free-form text list).

Custom types **do not** auto-discover — there is no
`<artworkDirectory>/<custom type>/` search for them. They also do not
appear in the sidebar gallery until they have something to show: the
gallery is built from the item's actual artwork links, so an unlinked
custom type is invisible there. Where it *does* appear is the
[Artwork links dialog](#manual-per-item-links), which seeds its rows
from the collection's declared type list — that is where you populate
one.

## Manual per-item links

When auto-discovery picks the wrong image, or when you want to attach
art to a custom type, use **manual links**.

### Opening the dialog

Open the **Artwork links** dialog from the sidebar gallery's edit
affordance, or from the **Edit** button in the horizontal details pane.
There is no item-context-menu entry for it.

### What it shows

A table with one row per artwork type — both standard
(`cover`, `box`, `screenshot`, `fanart`, `logo`, …) and any
[custom types](#custom-artwork-types) you've added to this collection.
Each row has:

| Column | Purpose |
|--------|---------|
| **Type** | Display label of the artwork type. Read-only; ordered standard-types-first. |
| **Override path** | The file Kartend uses for this type. Editable directly (paste an absolute path), or use **Browse…** to pick. Empty for unlinked rows. |
| **Browse** | Opens a file picker rooted at the collection's `artworkDirectory` when possible. |
| **Clear** | Removes the override for that row. |

### Linking a file

1. Find the row for the type you want to override.
2. Click **Browse** and pick the image, *or* type/paste an absolute
   path into the **Override path** cell.
3. Click **Save**. The sidebar gallery refreshes immediately.

The image doesn't have to live anywhere near the collection's
`artworkDirectory` — overrides accept any absolute path.

### Clearing a link

Click **Clear** on the row, then **Save**. The effect depends on the
row's type:

- **Standard types** fall back to auto-discovery for that type — if a
  matching file exists in `artworkDirectory`, it reappears in the
  gallery.
- **Custom types** stay hidden until a file is set again (custom types
  have no auto-discovery).

### How overrides interact with auto-discovery

An override always wins over auto-discovery for the same `(item,
type)` pair. The interaction summary:

| Type kind | Override set | Override cleared |
|-----------|--------------|------------------|
| Standard | Override wins | Auto-discovery resumes |
| Custom | Override wins | Slot disappears from the gallery |

### Where a link counts as "having artwork"

A link on a cover type — `front`, `box`, `screenshot`, `title`, `fanart`,
`marquee`, and the scraped `box-3d` / `mixrbv1` / `mixrbv2` variants — makes
the item count as having artwork everywhere: the *Has artwork* and *Missing
artwork* [smart playlists](Smart-Playlists.md), the `has:artwork` /
`missing:artwork` [search tokens](Search-Sort-Filter.md#structured-search-tokens), the
Collection Health missing-artwork count, the
[wizard's](#artwork-assignment-wizard) queue, and the **hide missing artwork**
view filter. It is also what the item paints — the grid tile and the cover-flow
card show a hand-linked cover, not the placeholder, and show it ahead of
anything auto-discovery found for that item. Saving or clearing a link takes
effect immediately; no scan needed.

Two things that don't count:

- A link whose file you have since deleted. Kartend checks that the target
  still exists, so a broken link doesn't claim a cover that can't be shown;
  the item falls back to whatever auto-discovery finds for it.
- A link on `logo`, or on a [custom type](#custom-artwork-types). Those are
  gallery slots, never covers — they show in the sidebar gallery and on the
  detail page, and leave the tile to auto-discovery.

Manual links live in the database (`item_artwork` table), keyed by
`(collection_uuid, source_path, artwork_type)`. They survive rescans,
collection renames, and reorderings.

To copy artwork links to a different filename, you'll need to re-link
through the dialog — there's no "rename source" workflow today.

## Artwork assignment wizard

For collections with a lot of items missing artwork, the per-item
"Browse… → Save" loop gets tedious. The **Artwork Wizard** (Tools →
**Assign Missing Artwork…**) walks the missing pile one item at a
time and ranks candidate images from the artwork directory's **root**
(it does not look inside the typed subdirectories) by fuzzy name
match.

```
┌────────────────────────────────────────────────────────────┐
│ Assign Artwork — 12 of 47 remaining                        │
│                                                            │
│ Item: Some Movie (2021).mkv                                │
│                                                            │
│ Candidates                                                 │
│   1. ▣ Some Movie (2021).jpg            (score 100)        │
│   2. ▣ some-movie-2021.png              (score 84)         │
│   3. ▣ Some_Movie.jpg                   (score 62)         │
│   …                                                        │
│                                                            │
│ [ Browse… ]  [ Skip ]  [ Close ]        [ Pick selected ]  │
└────────────────────────────────────────────────────────────┘
```

| Action | Effect |
|--------|--------|
| **Pick selected** | Save the highlighted candidate as the item's manual `front` link and advance to the next item. |
| **Browse…** | Open a file picker for cases where no candidate fits. Unlike the Artwork links dialog, it does not start in `artworkDirectory`. |
| **Skip** | Leave the item alone and advance. |
| **Close** | Close the wizard. Already-assigned items keep their new links. |

Candidates are ranked by a subsequence-based fuzzy score:
case-insensitive, consecutive characters score higher, word-boundary
matches score higher, and items already-matched by the standard
auto-discovery process are skipped (the wizard only surfaces the
*missing* pile). The top 12 candidates render with thumbnails.

The assigned image lands in the `item_artwork` table as a manual
override on the **primary** artwork type — identical to using **Edit
artwork links…** by hand — so the choice survives rescans and
collection renames.

> **Tip** — pair with the **Missing artwork**
> [smart-playlist kind](Smart-Playlists.md#filter-kinds) for a persistent
> worklist tile. Both it and the wizard's own pile count an assigned link
> straight away, so an item drops out of the worklist — and out of the
> wizard's queue the next time you run it — the moment you pick a cover for
> it. Auto-discovered covers still settle on the collection's next scan.

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

If the item has a preview video — looked for in
`<artworkDirectory>/video/` (see [Video Previews](Video-Previews.md)) —
the gallery includes a special **video tile** with a play-icon badge.
Clicking it expands the video player in the sidebar. The video tile
comes **first** in the gallery order: the gallery is video-first by
design, on the reasoning that a moving preview tells you more about an
item than a still does.

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
[subfolder artwork generator](../dev/subfolder-artwork.md) is a Python
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

See [docs/dev/subfolder-artwork.md](../dev/subfolder-artwork.md) for the full
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

### Subcollection tile artwork

The tile a subcollection shows inside its parent is resolved in two
steps, in this order:

1. **`collectionIcon` on the child** — an absolute path to an image.
   This is the explicit choice and always wins.
2. **An image named after the child, in the *parent's*
   `artworkDirectory`** — so a subcollection called `Documentaries`
   picks up `Documentaries.png` sitting alongside the parent's item
   covers. Same name matching as per-item artwork.

If neither resolves, the tile falls back to `placeholderArtwork`, and
then to the generated cross-hatch placeholder.

Grid, List, and Cover Flow all do both steps, in this order.

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
~/Videos/Films/A Film/A Film.png          ← must match the item's name
~/Videos/Films/A Film/screenshot/A Film.png
```

Matching is always on the item's base filename. There is no
`cover.png` / `folder.jpg` convention — a file called `cover.png` is
just an artwork entry named `cover`, and nothing will look for it.

```ini
[Films]
mediaDirectory=~/Videos/Films
artworkDirectory=~/Videos/Films
includeArtworkSubfolders=true
extensions=mkv,mp4
```

### Multiple artwork types in a flat directory

Types are subdirectories, so a "flat" directory holds only the plain
covers; the extra types go one level down:

```
~/Videos/Films/_art/A Film.png              ← tile face
~/Videos/Films/_art/screenshot/A Film.png
~/Videos/Films/_art/fanart/A Film.png
~/Videos/Films/_art/logo/A Film.png
```

All four appear in the gallery. Filename suffixes such as
`A Film-poster.png` do nothing — that file indexes under the name
`A Film-poster` and is never associated with `A Film`.

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
  — `ArtworkManager` orchestrates, `ArtworkLoadDispatcher` owns the
  dedicated `QtConcurrent` pool, `ViewportArtworkScheduler` owns
  viewport prioritisation. The pure lookup helpers live outside the
  module in
  [src/utils/view/artworkutils.cpp](../../src/utils/view/artworkutils.cpp).
- Cache: [src/modules/data/cache/](../../src/modules/data/cache/) (`CacheManager`)
  hosts the in-memory LRU and disk persistence.
- Per-item manual links table: `item_artwork(collection_uuid, path,
  artwork_type, manual_path)` in SQLite, unique on the first three. See
  [src/utils/db/itemartwork.h](../../src/utils/db/itemartwork.h).
- Sidebar gallery layout: `DetailsPane` in
  [src/ui/widgets/panes/](../../src/ui/widgets/panes/). It is a flat row
  of entries, not tabs: the standard half comes from probing
  `{artwork}/<type>/` per `ItemArtworkStore::standardTypes()`, the
  custom half from the item's actual `item_artwork` rows. The
  collection's `customArtworkTypes` list feeds the links *dialog*, not
  the gallery.
- Cycle-on-middle-click: the button and modifier check are in
  `EventManager`'s mouse filter
  ([eventmanagermouse.cpp](../../src/modules/input/event/eventmanagermouse.cpp)),
  which emits `artworkTypeCycleRequested`. The modifier must equal
  `GeneralSettings::artworkCycleModifier` exactly.
- Subfolder artwork generator: a standalone Python script,
  [.scripts/subfolder_art_generator.py](../../.scripts/subfolder_art_generator.py)
  — see [docs/dev/subfolder-artwork.md](../dev/subfolder-artwork.md).
- Adding a new standard artwork type: extend
  `ItemArtworkStore::standardTypes()` in
  [src/utils/db/itemartwork.cpp](../../src/utils/db/itemartwork.cpp)
  (gallery + links dialog), and add it to
  `ArtworkUtils::coverSubdirPriority()` in
  [src/utils/view/artworkutils.cpp](../../src/utils/view/artworkutils.cpp)
  only if it should be able to become a tile face. Both strings double
  as on-disk subdirectory names, so renaming one needs a migration.
  Custom types don't require code changes.
