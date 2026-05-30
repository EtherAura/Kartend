# View Modes

Each collection can be displayed in one of four view modes. The choice
is per-collection (it's part of the collection's settings) but you can
switch on the fly — the toolbar's view dropdown and `Ctrl+1`–`Ctrl+4`
swap modes immediately.

| Mode | Shortcut | Best for |
|------|----------|----------|
| **Grid** (default) | `Ctrl+1` | Cover-art-driven libraries: games, movies, photos. |
| **List** | `Ctrl+2` | Long, text-heavy libraries: documents, music tracks, tools. |
| **Cover Flow** | `Ctrl+3` | Showcase / kiosk / slideshow-style browsing. |
| **Horizontal** | `Ctrl+4` | TV-style row layouts and wide displays. |

> **Where to find this** — Settings Dialog → **Appearance** tab →
> **View Type**, or toolbar **View** dropdown. Also persisted as
> `viewType=grid|list|coverflow|horizontal` in the collection's INI
> section.

Selection state is preserved across mode switches (assuming
`rememberSelection=true`, the default), so you can flip between Grid
and List without losing your place.

---

## Grid view

The default. Tiles flow left-to-right, wrapping into rows. Vertical
scroll is primary.

| Setting | INI key | Notes |
|---------|---------|-------|
| Items per row | `gridWidth` | Default `4`. Adjust live with `Ctrl++` / `Ctrl+-`. |
| Tile width | `itemWidth` | Pixels. |
| Tile height | `itemHeight` | Pixels. |
| Tile font size | `fontSize` | Title text size. |
| Tile corner radius | `cornerRadius` | Rounded-corner radius. |
| Horizontal gap | `horizontalSpacing` | Pixel gap between columns. |
| Vertical gap | `verticalSpacing` | Pixel gap between rows. |
| Alignment | `horizontalAlignment` | `left`, `center`, `right`. |
| Hide titles | `hideTitles` | Hide the title line under each tile. |
| Hide subcollection titles | `hideSubcollectionTitles` | Hide titles only on subcollection tiles. |
| Hide horizontal scrollbar | `hideHorizontalScrollbar` | |
| Hide vertical scrollbar | `hideVerticalScrollbar` | |

### Sidebar interaction

When the **Expand** sidebar is docked (see
[Sidebar & Details Pane](Sidebar-and-Details-Pane.md)), the available
grid area shrinks. To avoid awkwardly-sized tiles, Kartend supports an
**alternate `gridWidth`** that takes effect when the sidebar is hidden:

| Key | Effect |
|-----|--------|
| `gridWidthSidebarHidden` | Replaces `gridWidth` while the **Expand** sidebar is hidden. |
| `gridHeightSidebarHidden` | Replaces vertical fit when the sidebar is docked top/bottom. |

Set both if you toggle the sidebar often.

### Performance

Grid view uses [virtual scrolling](Search-Sort-Filter.md#performance):
only the visible rows are materialized. A 100,000-item collection
renders the same as a 100-item one. The pixmap cache budget
(`pixmapCacheSizeMB` global, default 50) bounds memory.

---

## List view

One item per row. Columns: collection icon (or item type icon), item
name, optional artwork thumbnail. Vertical scroll is primary; horizontal
scroll appears only when columns overflow.

| Setting | INI key | Notes |
|---------|---------|-------|
| Row height | `listRowHeight` | Pixels. Default `40`. |
| Font size | `listFontSize` | Default `11`. |
| Row color | `listRowColor` | Hex. |
| Alternate row color | `listAltRowColor` | Hex. Stripes every other row. |
| Collection-icon column width | `[General] listCollectionColumnWidth` | Global, default `150`. Config-only — no Settings Dialog control. |
| Artwork column width | `[General] listArtworkColumnWidth` | Global, default `32`. Config-only — no Settings Dialog control. |

List view honors keyboard repeat tuned for fast scrolling: see
`listKeyboardRepeatIntervalMs` (default `50`) and
`listClickHoldRepeatIntervalMs` (default `80`) under
[Configuration Reference](Configuration-Reference.md#keyboard-repeat).
Holding `↓` in List view scrolls roughly 5× faster than in Grid by
default — tune to taste.

### When to use List

- Long collections of similar-looking items (text files, manuals, music
  tracks) where artwork doesn't help discrimination
- Discoverability via item name rather than cover art
- Side-by-side comparison of metadata in the sidebar (Item tab updates
  per row)

---

## Cover Flow view

3D-style center-focus carousel. The selected item is rendered large in
the center; nearby items angle off to either side. Driven by horizontal
scroll.

### Notable behavior

- **Sidebar is automatically hidden** while in Cover Flow so the
  carousel takes the full viewport. When you switch away from Cover
  Flow, the sidebar returns to whatever state it was in beforehand
  (your `sidebarVisible` preference is preserved, just temporarily
  overridden).
- **Video-first**: if the selected item has a preview video in the
  collection's `videoDirectory`, it plays full-screen in the Cover Flow
  center tile instead of artwork. Falls back to artwork when no video
  is available. See [Video Previews](Video-Previews.md).
- Keyboard: `←` / `→` move selection along the carousel. `Enter`
  launches. `Ctrl+1`–`4` switch to another mode.

### Settings

Most Grid-view appearance keys (item width / height, corner radius,
font size, background) apply identically. The carousel's animation
timing is global and lives in `UIConstants`; tuning it requires a
rebuild (see [docs/dev/constants.md](../dev/constants.md)).

### When to use Cover Flow

- Showcase displays, kiosk setups, attract-mode walls
- Collections where video previews are the primary content (e.g. movie
  reels, demoscene productions)
- A movie-poster vibe where one cover gets full attention at a time

---

## Horizontal view

Like Grid, but the wrap axis is flipped: items flow top-to-bottom,
wrapping into columns. Horizontal scroll is primary.

| Setting | INI key | Notes |
|---------|---------|-------|
| Items per column | `horizontalGridHeight` | Default `4`. Re-uses the `gridWidth` UI; just renamed conceptually. |
| Items per column when sidebar hidden | `horizontalGridHeightSidebarHidden` | Override for **Expand** sidebar interactions. |

All other Grid keys (tile width / height, fonts, colors, spacing,
backgrounds, parallax, vignette) apply.

### When to use Horizontal

- Wide displays where vertical scroll feels cramped
- TV-row layouts mimicking streaming-app shelves (one long row of
  items)
- Collections where you want a *very* shallow vertical fit (one or two
  rows) — set `horizontalGridHeight=1` or `2`

---

## Switching between views

Three ways:

1. **Toolbar** — click the **View** dropdown. Each option triggers an
   immediate switch and writes the new `viewType` for the collection.
2. **Keyboard** — `Ctrl+1` (Grid), `Ctrl+2` (List), `Ctrl+3` (Cover
   Flow), `Ctrl+4` (Horizontal).
3. **Settings Dialog** — Appearance tab → **View Type** dropdown.
   Useful when you want to flip multiple collections at once via the
   [scope selector](Settings-Dialog.md#scope-selector).

### Hiding view buttons in the toolbar

Each view button can be hidden globally:

```ini
[General]
toolbarShowGridViewButton=true
toolbarShowListViewButton=true
toolbarShowCoverFlowViewButton=false
toolbarShowHorizontalViewButton=false
```

The keyboard shortcuts and the **View → Layout** menu still work; only
the toolbar buttons are hidden. See [Toolbar & Menus](Toolbar-and-Menus.md#customization).

You can also rename buttons:

```ini
toolbarGridViewButtonText=Tiles
toolbarListViewButtonText=Rows
```

---

## Per-collection vs. global default

There is no global "default view" setting per se — each collection
remembers its own `viewType`. To apply one mode to many collections at
once:

1. Set the desired view on a representative collection.
2. Open Settings → use the **Apply To Selected** workflow with the
   **Appearance** category checked. See
   [Settings Dialog](Settings-Dialog.md#apply-settings).

---

## Selection indicator

Across all view modes, the selected item is marked by a translucent
**selection overlay** painted on top of the tile, tinted by
`selectionColor` (per-collection, see
[Themes & Appearance](Themes-and-Appearance.md#colors)). When you
move selection — arrow keys, mouse click, gamepad, alphabetic jump
— the overlay **glides** from the previous tile to the new one
rather than jump-cutting. The animation is short (matching the
scroll ease) so rapid keypresses still feel responsive.

Specifics:

- During click-and-hold scrolling the overlay is force-visible so
  you can see where the cursor is even as the grid scrolls
  underneath.
- In Cover Flow the carousel itself moves the center slot; the
  overlay is suppressed in favor of the carousel's depth/scale
  cues.
- The animation timing lives in `UIConstants` and is not a
  user-tunable setting today. If you need the indicator to jump
  instantly (e.g. for accessibility or recording), file a feature
  request.

## Comparison table

| Aspect | Grid | List | Cover Flow | Horizontal |
|--------|------|------|------------|------------|
| Scroll axis | Vertical | Vertical | Horizontal (carousel) | Horizontal |
| Sidebar default | Available | Available | Forced hidden | Available |
| Video previews | Sidebar only | Sidebar only | Full-screen center tile | Sidebar only |
| Best for | Cover-driven libraries | Text-heavy libraries | Kiosk / attract | TV / wide displays |
| Preserves selection across mode switch | ✓ | ✓ | ✓ | ✓ |

---

## For developers

- View modes are a `ViewType` enum in
  [src/utils/app/collectiontypes.h](../../src/utils/app/collectiontypes.h).
- Each mode is wired through `ScrollManager` and a corresponding
  view-engine class under [src/modules/input/scroll/](../../src/modules/input/scroll/):
  - Grid / Horizontal share the `VirtualScrollEngine`.
  - List has its own row-rendering path.
  - Cover Flow lives in the `coverflow` view engine and has its own
    interaction handler that overrides default arrow-key behavior.
- Sidebar suppression in Cover Flow is an **external hide flag** set on
  `SidebarManager` — it bypasses the persisted `sidebarVisible` and is
  cleared on view-mode change so the user's preference returns.
- Adding a new view mode involves: extending `ViewType`, adding a view
  engine, wiring it into `ScrollManager::switchViewMode()`, adding the
  toolbar button + menu entry + keyboard shortcut, adding visibility
  & label keys to `GeneralSettings`, and updating
  [Configuration Reference](Configuration-Reference.md).
