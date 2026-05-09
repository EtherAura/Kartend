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
| **Type** | no | Free-form tag (e.g. `Games`, `Movies`, `PDFs`). Used by the [collection-type filter](Search-Sort-Filter.md#type-filter). |
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
canonical way to build top-level categories like `Games` containing
SNES / PS1 / N64.

## Adding, renaming, duplicating, deleting

The Settings Dialog's left-hand tree is the control surface.

- **Add** — toolbar button at the top of the tree. New collections are
  created at root by default; drag them into a parent or set the
  **Parent Collection** field afterward.
- **Rename** — double-click the name field on the **Basic** tab, or use
  the tree's right-click → **Rename**. Renaming updates `name`, INI
  section header, and any `additionalParentNames` references in
  collections that use this one as an alias parent.
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

A subcollection's INI section name is `Parent > Child`:

```ini
[Documents]
name=Documents
gridWidth=4

[Documents > Reports]
name=Reports
parentCollectionIndex=0
mediaDirectory=~/Documents/Reports
launcherPath=/usr/bin/xdg-open
extensions=pdf,docx

[Documents > Presentations]
name=Presentations
parentCollectionIndex=0
mediaDirectory=~/Documents/Presentations
launcherPath=/usr/bin/xdg-open
extensions=pptx,pdf,odp
```

`parentCollectionIndex` is the **0-based index** of the parent in the
collection list. The Settings Dialog manages this for you; you only
need to know about it if you're hand-editing.

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

A collection has **one** primary parent (`parentCollectionIndex`) but
can also have any number of **linked parents** — alias references that
make the collection appear as a tile under each linked parent without
duplicating its config or items.

Use it for cross-cutting groupings:

```ini
[Sega Saturn Favorites]
mediaDirectory=~/games/saturn/favorites
parentCollectionIndex=2          ; primary: under "Games"
additionalParentNames=Favorites  ; also appears under "Favorites"
launcherPath=/usr/bin/yabasanshiro
```

> **Where to find this** — Settings Dialog → **Basic** tab → **Linked
> Parents** (multi-select picker). INI key:
> `additionalParentNames=Comma,Separated,Names`.

Renaming a parent collection automatically rewrites every linked
reference; deleting one removes the reference from any aliases.

Appearance is rendered identically whichever parent you reach the
collection through (it's the same collection, just multiple paths).

## Type metadata and the type filter

Each collection has a **Type** — a free-form text tag. It's purely a
classification you choose:

```ini
[Game Boy]
type=Games

[Movies]
type=Films

[Manuals]
type=Documents
```

Then the global **collection type filter** (toolbar → filter button →
Type, or `[General] collectionTypeFilter=Games`) shows only collections
whose `type` matches. Useful for switching modes ("show me only games,
hide everything else") without rearranging the hierarchy.

Pair with **Hide Subcollection Tiles** (`hideSubcollectionTiles=true`)
to flatten the view further — type filter + hide-subs makes Kartend
behave like a flat library of media items, ignoring the tree.

See [Search, Sort & Filter](Search-Sort-Filter.md#type-filter) for
filter mechanics.

## Folder browsing (treating subfolders as collections)

If your media folder has its own internal hierarchy — say,
`~/Games/SNES/Action/`, `~/Games/SNES/RPG/` — you don't need to create
a Kartend subcollection for each subfolder. Enable **Include Content
Subfolders** (`includeContentSubfolders=true`) and Kartend renders
folders as virtual collection tiles right alongside media items.

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
| `type` | string | empty | Free-form type tag (used by the type filter). |
| `mediaDirectory` | path | empty | Folder of items. Empty = parent-only. |
| `artworkDirectory` | path | empty | Folder of cover images. |
| `videoDirectory` | path | empty | Folder of preview videos. |
| `manualDirectory` | path | empty | Folder of per-item manuals. |
| `extensions` | csv | empty | File extensions to scan. Empty = all. |
| `collectionIcon` | path | empty | Tile icon when this collection is a subcollection. |
| `placeholderArtwork` | path | empty | Image for missing-artwork tiles. |

### Hierarchy

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `parentCollectionIndex` | int | `-1` | Index of primary parent. -1 = root. |
| `additionalParentNames` | csv | empty | Linked-parent collection names. |
| `isSubcollection` | bool | derived | Set automatically when parent set. |

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
  [src/utils/collectionutils.h](../../src/utils/collectionutils.h).
  Every key in this page maps 1:1 to a struct member.
- Hierarchy traversal goes through `CollectionHierarchyCache`
  (`collectionutils.h`) and `NavigationStackManager`
  (`src/modules/navigation/`). Parent-index → name resolution happens
  there.
- Linked-parent rewrites on rename are in
  [src/modules/settings/settingsdialogtree.cpp](../../src/modules/settings/).
- Virtual subfolder collections are synthesized at scan time by
  `QueryManager`; they have `isSubcollection=true` but no INI section
  and no UUID. Look for `currentSubfolder` in
  `collectionutils.h` to follow the runtime-only field that drives them.
- Playlists are also synthesized as virtual collections (with
  `isPlaylist=true`); see
  [Playlists & Favorites](Playlists-and-Favorites.md) for the schema.
