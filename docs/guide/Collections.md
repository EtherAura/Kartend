# Collections

A **collection** is the unit Kartend organizes around: a folder of media
files paired with a launcher, plus optional artwork, video previews, and
a few hundred per-collection appearance knobs. Collections can nest, can
share children with other collections (alias parents), and can be tagged
by free-form *type* for filter-based grouping.

If you've never built one, start with the
[Getting Started](Getting-Started.md) walkthrough. This page is the
"everything else there is to know" reference.

## Anatomy of a collection

| Field | Required | Notes |
|-------|----------|-------|
| **Name** | yes | Display label and the INI section header. Renaming updates references. The *only* strictly required field. |
| **Media Type** | no | Category tag — pick a preset (Video, Audio, Images, Documents, Games) from the dropdown or type a custom value. Drives the [collection-type filter](Search-Sort-Filter.md#type-filter) and the suggested scraper. |
| **Media Directory** | no | Folder of items. Supports `~`. Omitted on [shell collections](Shell-Collections.md) that exist purely to group other collections. |
| **Artwork Directory** | no | Folder of cover images matched by base filename. See [Artwork](Artwork.md). |
| **Video Directory** | no | Folder of preview videos for the sidebar. See [Video Previews](Video-Previews.md). |
| **Manual Directory** | no | Folder of manuals / docs for items. See [Item Metadata](Item-Metadata.md#manual-files). |
| **Launcher Path** | no | Executable that opens an item. Required when the collection has its own media; omitted on shell collections. See [Launchers](Launchers.md). |
| **Extensions** | no | Comma-separated allow-list. Empty = accept any file. |
| **Parent Collection** | no | Makes this collection a subcollection. |
| **Linked Parents** | no | Additional alias parents. See below. |
| **Collection Icon** | no | Image shown on the parent's tile when this collection is a subcollection. |
| **Placeholder Artwork** | no | Custom image for missing-artwork items. |
| **Header Logo** | no | Logo overlay painted at the top of the collection's grid. |

> **Where to find this** — Settings Dialog → tabs **Basic**,
> **Paths & Extensions**, **Launcher**, **Appearance**, **Sidebar**,
> **Colors**, **Text & Fonts**, **List View**.

A collection can also have no media directory of its own — a
[shell collection](Shell-Collections.md) used to group other
collections under a named category. Shells render their children as
tiles, open the matching subcollection on `Enter`, and are the
canonical way to build top-level categories like `Video` containing
Films / TV Shows / Documentaries.

## Adding, renaming, duplicating, deleting

The Settings Dialog's left-hand tree is the control surface.

- **Add** — toolbar button at the top of the tree. Opens the **Add
  Collection** dialog: name (required), content folder, artwork folder,
  launcher, media type, and scraper — with a ScreenScraper system
  selector shown for game media types and a libretro core selector
  shown for RetroArch launchers. Everything except the name can be left
  blank and filled in later from the **Configuration**/**Launcher**
  tabs. New collections are created at root by default; drag them into a
  parent or set the **Parent Collection** field afterward.

  For a step-by-step alternative, **File → New Library Wizard…** walks
  the same fields across multiple pages with installed-launcher
  detection. See [Getting Started → New Library Wizard](Getting-Started.md#new-library-wizard).
- **Rename** — double-click the name field on the **Basic** tab, or use
  the tree's right-click → **Rename**. Renaming updates `name`, the INI
  section header, and any linked-parent references in the
  `additionalParents` array of collections that use this one as an
  alias parent.
- **Duplicate** — right-click → **Duplicate**. Opens the Duplicate
  Collection dialog: choose the new name and the parent (sibling /
  child / root). All non-path settings (appearance, launcher config,
  etc.) are copied; paths are intentionally left blank so you can point
  the duplicate at a different folder.
- **Delete** — right-click → **Delete** with a confirmation prompt. The
  collection's children are reparented to its parent (or to root) — they
  are not deleted alongside it.

> **Caveat** — deleting a collection drops its INI section but does
> *not* clear per-item state from the database (custom fields, manual
> file links, launcher overrides, history). Re-adding a collection at
> the same name reattaches that history. Remove the database file at
> `~/.local/share/kartend/kartend.db` if you want a clean slate.

## Hierarchies (parents and subcollections)

A subcollection's parent is encoded in its **section header** — the
name follows the pattern `Parent > Child` (with `> ` space-padded as
the separator). That's the entire on-disk mechanism; there is no
separate parent-pointer key:

```ini
[Documents]
name=Documents
gridWidth=4

[Documents > Reports]
name=Reports
mediaDirectory=~/Documents/Reports
launcherPath=/usr/bin/xdg-open
extensions=pdf,docx

[Documents > Presentations]
name=Presentations
mediaDirectory=~/Documents/Presentations
launcherPath=/usr/bin/xdg-open
extensions=pptx,pdf,odp
```

The Settings Dialog rewrites these section headers when you reparent
a collection. To reparent by hand, rename the section header — for
example, `[Documents > Reports]` → `[Archive > Reports]` to move
`Reports` under `Archive`, then restart Kartend.

### Drag-and-drop reparenting

Drag a collection in the Settings tree onto another to set its parent.
Circular references are blocked at validation time — you can't drop a
collection onto one of its own descendants.

### Showing items from descendants inline

Set **Show all subcollection items** (`showAllSubcollectionItems=true`)
to mix items from every descendant collection into the parent's grid,
in addition to the subcollection tiles themselves. Useful for "Show me
everything in `Music`" without drilling into each sub-genre.

## Linked parents (alias parents)

A collection has **one** primary parent (encoded in its
`[Parent > Child]` section header) but can also have any number of
**linked parents** — alias references that make the collection appear
as a tile under each linked parent without duplicating its config or
items.

Use it for cross-cutting groupings:

```ini
[Video > Concert Recordings]
mediaDirectory=~/Videos/Concerts
additionalParents\1\name=Audio   ; also appears under "Audio"
additionalParents\size=1
launcherPath=/usr/bin/mpv
```

> **Where to find this** — Settings Dialog → **Basic** tab → **Linked
> Parents** (multi-select picker). Persisted as a QSettings array under
> the `additionalParents` key — each entry has a `\<n>\name` subkey
> plus a `\size` count:
> ```
> additionalParents\1\name=Audio
> additionalParents\2\name=Soundtracks
> additionalParents\size=2
> ```
> The dialog is the easier place to edit this — the on-disk form is
> shown here only so hand-editors recognize it.

Renaming a parent collection automatically rewrites every linked
reference; deleting one removes the reference from any aliases.

Appearance is rendered identically whichever parent you reach the
collection through (it's the same collection, just multiple paths).

## Media type, the type filter, and scrapers

Each collection has a **media type** — a category tag. The dropdown
offers five presets (Video, Audio, Images, Documents, Games) and stays
editable, so a custom value is still allowed:

```ini
[Films]
type=Video

[Albums]
type=Audio

[Manuals]
type=Documents
```

Then the global **collection type filter** (toolbar → filter button →
Type, or `[General] collectionTypeFilter=Video`) shows only collections
whose `type` matches. Useful for switching modes ("show me only video,
hide everything else") without rearranging the hierarchy.

Pair with **Hide Subcollection Tiles** (`hideSubcollectionTiles=true`)
to flatten the view further — type filter + hide-subs makes Kartend
behave like a flat library of media items, ignoring the tree.

See [Search, Sort & Filter](Search-Sort-Filter.md#type-filter) for
filter mechanics.

### Type-driven scraper selection

The media type also picks the default **metadata scraper**: Video →
TMDB, Audio → MusicBrainz, Documents → Open Library, Games →
ScreenScraper. The Add Collection dialog fills its **Scraper** field
from the chosen type automatically, and each collection's
**Configuration** tab carries a **Metadata Scraper** dropdown to change
it later. Leave it on **Automatic** to keep resolving the scraper from
the type at scrape time; pick a provider explicitly to pin it. That
override (`scraperProviderId`) is what lets a *custom*-typed collection
scrape — a custom tag matches no scraper category on its own. Image
collections have no scraper.

## Folder browsing (treating subfolders as collections)

If your media folder has its own internal hierarchy — say,
`~/Videos/Films/Action/`, `~/Videos/Films/Drama/` — you don't need to
create a Kartend subcollection for each subfolder. Enable **Include
Content Subfolders** (`includeContentSubfolders=true`) and Kartend
renders folders as virtual collection tiles right alongside media items.

Related toggles (all per-collection, on the **Paths & Extensions** tab):

| Setting | INI key | Effect |
|---------|---------|--------|
| Include Content Subfolders | `includeContentSubfolders` | Show subfolders as virtual tiles. |
| Include Artwork Subfolders | `includeArtworkSubfolders` | Match artwork from any subfolder. |
| Show All Subfolder Items | `showAllSubfolderItems` | Mix items from subfolders with the parent's items, instead of requiring you to enter the subfolder. |
| Show Hidden Folders | `showHiddenFolders` | Include dot-prefixed (`.config`-style) folders. |
| Hide Subfolder Titles | `hideSubfolderTitles` | Hide titles on virtual folder tiles. |

Generated tiles use any matching artwork in the artwork directory; if
none is found you can compose a placeholder by running the
[subfolder artwork generator](../subfolder-artwork.md).

> **Tip** — virtual folders are *runtime-only*. They don't get their
> own INI section, can't be reparented, and don't carry per-folder
> appearance. If you want richer per-folder control, promote the
> subfolder to a real Kartend subcollection (right-click in the
> Settings tree → **Add Collection**, set parent and media directory).

## Header logo overlay

Add a per-collection branding logo painted across the top of the items
grid:

| Field | INI key | Notes |
|-------|---------|-------|
| Header Logo Image | `headerLogoImage` | Path to PNG / JPG / WEBP / SVG |
| Header Logo Position | `headerLogoPosition` | `topleft` / `topcenter` / `topright` |

Distinct from the **Collection Icon** (`collectionIcon`) which is shown
on the *tile* of this collection when it's a subcollection of another.

## Filesystem watcher

Per-collection toggle that triggers an automatic rescan when the
media directory changes on disk. Useful for collections you sync from
elsewhere (rsync, Syncthing, a Steam library, a network mount) where
files appear and disappear outside Kartend.

| Setting | INI key | Default | Effect |
|---------|---------|---------|--------|
| Watch Filesystem | `watchFilesystem` | `false` | Register the media directory (and every subdirectory) with `QFileSystemWatcher` and rescan after a debounce window when a change fires. |

The watcher walks the media directory at startup and re-walks on
change so newly-created subdirectories are picked up automatically.
Rescans are debounced (default 2 s) to batch rapid filesystem
operations — bulk copies and `rsync` runs fire one rescan when the
dust settles, not one per file. Symlink loops are short-circuited.

> **Where to find this** — Settings Dialog → per-collection
> **Configuration** tab → **Watch filesystem for changes**.

If you don't need automatic rescans, leave it off — for collections
that change only when you explicitly edit them, the manual
**File → Rescan Collection** (`Ctrl+F5`) is cheaper. The watcher's
RAM cost scales with the number of subdirectories per collection.

## Variant inspector

**File → Duplicates and Variants…** opens a per-collection grouped
view of items that share a base filename — e.g. `Concert.mkv` and
`Concert.flac` both group under `Concert`. Each group expands to show
the absolute paths of every variant; a row carries **Launch** and
**Select** buttons so you can switch to a specific variant without
leaving the dialog.

Useful for sanity-checking duplicates after a library reorganisation,
or when one logical recording exists in multiple formats and you
want a quick map of which is which.

The view is read-only — there's no merge or remove affordance here.
Surfaced via the menu (and the [command palette](Toolbar-and-Menus.md#command-palette)).

## Collection Health Dashboard

**File → Collection Health…** opens a diagnostic dashboard listing:

- **Missing files** — items in the database whose `source_path` no
  longer resolves on disk (e.g. after a media-directory move that
  wasn't followed by a rescan).
- **Missing artwork** — items with no artwork file found.
- **Launcher issues** — collections whose primary launcher path
  doesn't resolve to an executable.

Each category shows a count plus up to 20 example paths so you can
spot the affected items without leaving the dialog. The view is
read-only — fixes happen elsewhere (Rescan, the Artwork Wizard, the
launcher field).

Useful as the first stop when something doesn't render correctly:
the dashboard usually reveals whether the cause is data drift
(`Rescan`), missing assets (use the Artwork Wizard), or
configuration drift (Settings → Launcher tab).

## The expand-mode "two-stage" launch

Per-collection **Expand Mode** (`expandMode=true`) gives you a preview
step between selecting and launching:

1. Press `Enter` once → full-screen artwork overlay appears.
2. Press `Enter` a second time → item launches (or `Escape` to cancel).

Useful for collections where you want to see the cover at full size
before committing — coffee-table books, art galleries, screenshot
showcases. Has no effect on subcollection tiles (they always open on
the first `Enter`).

## Configuration reference for collections

Every collection key, grouped by purpose:

### Identity & paths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `name` | string | section header | Display name. |
| `type` | string | empty | Media-type tag (used by the type filter and to pick a scraper). |
| `scraperProviderId` | string | empty | Pinned metadata scraper id (`tmdb`, `screenscraper`, `musicbrainz`, `openlibrary`). Empty = resolve from `type`. |
| `mediaDirectory` | path | empty | Folder of items. Empty = parent-only. |
| `artworkDirectory` | path | empty | Folder of cover images. |
| `videoDirectory` | path | empty | Folder of preview videos. |
| `manualDirectory` | path | empty | Folder of per-item manuals. |
| `extensions` | csv | empty | File extensions to scan. Empty = all. |
| `collectionIcon` | path | empty | Tile icon when this collection is a subcollection. |
| `placeholderArtwork` | path | empty | Image for missing-artwork tiles. |

### Hierarchy

The primary parent is encoded in the `[Parent > Child]` **section
header**, not in a key. The in-memory `parentCollectionIndex` and
`isSubcollection` fields you may see in source code are derived from
the section structure at load time and are not INI keys.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `additionalParents` | array | empty | Linked secondary parent collection names. Persisted as a QSettings array (`additionalParents\1\name=…`). |

### Content / scanning

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `includeContentSubfolders` | bool | `false` | Show subfolders as virtual tiles. |
| `includeArtworkSubfolders` | bool | `false` | Match artwork in subfolders. |
| `showAllSubfolderItems` | bool | `false` | Flatten subfolder items into parent grid. |
| `showHiddenFolders` | bool | `false` | Include dot-prefixed folders. |
| `showAllSubcollectionItems` | bool | `false` | Mix descendants' items into this collection. |
| `extractArchives` | bool | `false` | Auto-extract `.zip` / `.7z` etc. before launch. |
| `extractedExtension` | string | empty | Which extension inside the archive to launch. |
| `watchFilesystem` | bool | `false` | Auto-rescan on filesystem changes (debounced). See [Filesystem watcher](#filesystem-watcher). |

### Display options

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `expandMode` | bool | `false` | Two-stage activation (preview then launch). |
| `hideMissingArtwork` | bool | `false` | Hide items that have no artwork. |
| `titleExclusionPatterns` | csv (regex) | empty | Patterns stripped from displayed titles. |
| `titleExclusionEnabled` | bool | `false` | Toggle the pattern list. |
| `headerLogoImage` | path | empty | Logo painted across the top of the grid. |
| `headerLogoPosition` | enum | `topleft` | `topleft` / `topcenter` / `topright`. |

The remaining keys (appearance, sidebar styling, colors, list view) are
covered in [Themes & Appearance](Themes-and-Appearance.md),
[Sidebar & Details Pane](Sidebar-and-Details-Pane.md), and
[View Modes](View-Modes.md).

For the master list see [Configuration Reference](Configuration-Reference.md).

## For developers

- The C++ struct is `CollectionConfig` in
  [src/utils/app/collectionutils.h](../../src/utils/app/collectionutils.h).
  Every key in this page maps 1:1 to a struct member.
- Hierarchy traversal goes through `CollectionHierarchyCache`
  (`collectionutils.h`) and `NavigationStackManager`
  (`src/modules/input/navigation/`). Parent-index → name resolution
  happens there.
- Linked-parent rewrites on rename are in
  [src/ui/dialogs/settings/core/settingsdialogtree.cpp](../../src/ui/dialogs/settings/core/settingsdialogtree.cpp).
- Virtual subfolder collections are synthesized at scan time by
  `QueryManager`; their in-memory `isSubcollection` flag is set but
  they have no INI section and no UUID. Look for `currentSubfolder`
  in `collectionutils.h` to follow the runtime-only field that
  drives them.
- Playlists are also synthesized as virtual collections (with the
  in-memory `isPlaylist` flag set); see
  [Playlists & Favorites](Playlists-and-Favorites.md) for the schema.
