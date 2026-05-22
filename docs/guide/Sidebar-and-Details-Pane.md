# Sidebar & Details Pane

The sidebar (also called the **details pane**) is Kartend's persistent
metadata surface — it shows the selected item's artwork, file info,
custom fields, video preview, and the active collection's summary.
You can dock it on any of the four edges, set it to **Overlay** (float)
or **Expand** (push the grid), restyle it per-collection, and toggle
its tabs.

> **Where to find this** — `F9` toggle, **View → Details Pane** menu,
> Settings Dialog → **Sidebar** tab. Persisted per-collection (visibility,
> mode, position, styling, font).

## Visibility

- `F9` toggles the sidebar. The state is persisted as
  `sidebarVisible` per-collection, so each collection remembers
  whether you had it open.
- **View → Show Details Pane** in the menu mirrors the toggle.
- **Right-click any item → Properties** also toggles.
- Gamepad **Y** button (default, rebindable) toggles too.

The sidebar is **forced hidden** in [Cover Flow view](View-Modes.md#cover-flow-view)
so the carousel takes the full viewport. When you switch away from
Cover Flow, your `sidebarVisible` preference returns. (The forced-hide
flag is in-memory only — it doesn't overwrite the saved preference.)

## Modes: Overlay vs. Expand

| Mode | Behavior | INI value |
|------|----------|-----------|
| **Overlay** | Floats over the grid. Grid layout doesn't change. The sidebar can have its own width and a translucent background. | `sidebarMode=overlay` |
| **Expand** | Docks permanently. The grid shrinks to make room. | `sidebarMode=expand` |

Pick **Overlay** when you want the sidebar visible occasionally without
constantly recalculating tile sizes. Pick **Expand** for a static,
docked layout.

### Expand-mode width adapters

In Expand mode the available grid area changes when the sidebar is
hidden vs. visible — toggling `F9` would otherwise resize tiles awkwardly.
Kartend supports **alternate `gridWidth`** values that take effect when
the sidebar is hidden:

```ini
gridWidth=8                  ; columns when sidebar is visible
gridWidthSidebarHidden=10    ; columns when sidebar is hidden

horizontalGridHeight=4
horizontalGridHeightSidebarHidden=5

gridHeightSidebarHidden=6    ; for top/bottom dock with Expand
```

Set both if you toggle the sidebar often — otherwise Kartend falls
back to a single shared `gridWidth`.

## Position (where on the screen)

The sidebar can dock on any of the four edges:

| Position | INI value | Driven by |
|----------|-----------|-----------|
| Right (default) | `right` | Width |
| Left | `left` | Width |
| Top | `top` | Height |
| Bottom | `bottom` | Height |

Switch via **View → Details Pane Orientation ▶** or Settings → Sidebar
tab → **Sidebar Position**.

Top / bottom positions use `sidebarHeight` instead of `sidebarWidth`,
and the alternate-grid-when-hidden key becomes
`gridHeightSidebarHidden` to handle items-per-column re-layout.

### Resizing

Drag the sidebar's edge to resize. Lock dragging with `sidebarWidthLocked=true`
(controls both width *and* height regardless of position).

| Setting | INI key | Effect |
|---------|---------|--------|
| Sidebar width | `sidebarWidth` | Pixels (right/left). |
| Sidebar height | `sidebarHeight` | Pixels (top/bottom). |
| Lock resize | `sidebarWidthLocked` | Disable drag-resizing. |

## Tabs

The sidebar has up to three tabs:

| Tab | Shown when | Contents |
|-----|------------|----------|
| **Item** (default) | An item is selected | Per-item artwork, metadata, video preview |
| **Collection** | Always | Per-collection summary stats |
| **File** | Reserved for future user customization | (placeholder today) |

Switch tabs by clicking the tab header. The active tab is remembered
per-collection in `sidebarActiveTab=item|collection|file`.

### Item tab

Per-item, updates as you move selection. Sections:

#### Artwork gallery

A horizontally-scrollable strip of every artwork type the item has —
auto-discovered standard types plus per-collection custom types plus
per-item manual links. See [Artwork](Artwork.md) for the type model.

- Each tab in the gallery is one type (`boxfront`, `screenshot`, etc.).
- **Click** a type tab to display it large above.
- **Modifier + middle-click** on the item tile cycles types
  (configurable modifier).
- A **video tile** appears as the last gallery entry if the item has a
  preview video — clicking expands the video player. See
  [Video Previews](Video-Previews.md).
- Missing artwork tiles show a placeholder image (or hatch pattern,
  per-collection `placeholderArtwork`).

#### Metadata fields

| Row | Source |
|-----|--------|
| Display name | Item's display name (after title-pattern cleanup) |
| File path | Item's file path (truncated; hover for tooltip with full path) |
| File size | Bytes / KiB / MiB / GiB |
| Last modified | File mtime |
| **Manual file** | Per-item manual link (PDF, EPUB, manual page). Clickable. See [Item Metadata](Item-Metadata.md#manual-files). |
| **Launcher override** | Per-item launcher choice if set. Shows the launcher name. |
| **Custom fields** | User-defined key/value pairs from the [Custom Fields Dialog](Item-Metadata.md#custom-fields). |
| **Play count** | From `play_count` in the database (Statistics dialog drives this too). |
| **Last played** | From `last_played`. Format: `5 minutes ago`, `Yesterday`, etc. |
| **Time played** | Sum from `launch_history` (only populated if [runtime detection](Splash-and-Now-Playing.md#runtime-detection) is enabled). |

#### Video preview player

If the item has a preview video (auto-discovered in `videoDirectory`),
clicking the video tile in the gallery expands an embedded player:

- Play / pause control
- Volume tied to global `previewVideoVolume` (toolbar slider)
- Loops by default
- Pauses when selection changes (so the next-item's video can take
  over)

See [Video Previews](Video-Previews.md) for formats and tuning.

### Collection tab

Per-collection summary, always available even when no item is selected.

| Section | Contents |
|---------|----------|
| Header | Collection name (and type, if set) |
| Stats | Item count (filtered if search active), subcollections count, total launches, total time played |
| Paths | Media / artwork / video / manual directories |
| Launcher | Default launcher name + path (truncated) |

Useful as a "what is this collection" dashboard — particularly for
collections you set up months ago and forgot about.

### File tab

Reserved for future customization. Today it renders a placeholder; the
intent is for advanced users to define their own template in a future
release.

## Styling per-collection

The sidebar inherits its colors from the collection's general theme by
default, but every aspect can be overridden per-collection.

> **Where to find this** — Settings Dialog → **Sidebar** tab →
> **Background**, **Bubbles**, **Typography** sections.

### Background

| Setting | INI key | Notes |
|---------|---------|-------|
| Background type | `sidebarBackgroundType` | `color` / `image` / `pattern`. |
| Background color | `sidebarBackgroundColor` | Hex; used when type = `color`. |
| Background image | `sidebarBackgroundImage` | Path; used when type = `image`. |
| Pattern | `sidebarPattern` | Currently only `crosshatch`. |
| Pattern intensity | `sidebarPatternIntensity` | 0–100 alpha. |
| Pattern color | `sidebarPatternColor` | Hex tint. |

### Bubbles

The sidebar uses translucent "bubble" backgrounds behind section
headers and bodies. You can color them and tune their opacity:

| Setting | INI key | Notes |
|---------|---------|-------|
| Header bubble color | `sidebarHeaderBgColor` | |
| Header bubble opacity | `sidebarHeaderBgOpacity` | 0–255 |
| Section bubble color | `sidebarSectionBgColor` | |
| Section bubble opacity | `sidebarSectionBgOpacity` | 0–255 |

### Text

| Setting | INI key | Notes |
|---------|---------|-------|
| Text color | `sidebarTextColor` | |
| Accent color | `sidebarAccentColor` | Used for links, highlighted values. |

### Typography

| Setting | INI key | Notes |
|---------|---------|-------|
| Font family | `sidebarFontFamily` | Empty = inherit from global / system. |
| Font point size | `sidebarFontPointSize` | `0` = inherit. |

The sidebar font can differ from the rest of the UI — useful for a
serif sidebar against a sans-serif grid, for example.

## Recipes

### Always show the sidebar in Expand mode on the right

```ini
[General]
showMenuBar=true
showToolbar=true

[Movies]
sidebarVisible=true
sidebarMode=expand
sidebarPosition=right
sidebarWidth=350
gridWidth=4
gridWidthSidebarHidden=5
```

### Discreet overlay sidebar

```ini
[Movies]
sidebarMode=overlay
sidebarPosition=right
sidebarBackgroundType=color
sidebarBackgroundColor=#1a1a2e
sidebarHeaderBgOpacity=160
sidebarSectionBgOpacity=120
```

The overlay floats; opacity-tuned bubbles let the grid show through.

### Top dock for wide-screen TV layouts

```ini
[Movies]
viewType=horizontal
sidebarMode=expand
sidebarPosition=top
sidebarHeight=180
horizontalGridHeight=3
gridHeightSidebarHidden=4
```

The sidebar lives across the top; horizontal scroll runs underneath.

### Custom serif sidebar font

```ini
sidebarFontFamily=EB Garamond
sidebarFontPointSize=12
```

Combine with `globalUiFontFamily=Inter` for a sans-serif main UI and a
serif details pane.

## Where to next

- [View Modes](View-Modes.md) — Cover Flow auto-hides the sidebar
- [Themes & Appearance](Themes-and-Appearance.md) — for backgrounds,
  fonts, and global theming
- [Item Metadata](Item-Metadata.md) — what shows up in the Item tab
- [Video Previews](Video-Previews.md) — the gallery's video tile

## For developers

- Sidebar UI: [src/ui/widgets/panes/detailspane.ui](../../src/ui/widgets/panes/) +
  [src/ui/controllers/detailspanemanager/](../../src/ui/controllers/detailspanemanager/)
  (`DetailsPaneManager`).
- Visibility & external-hide flag: `DetailsPaneManager` keeps both
  the persisted `sidebarVisible` and an in-memory "external hide"
  used by Cover Flow.
- Tab model: `DetailsPane` builds Item / Collection / File widgets
  on demand; `sidebarActiveTab` initial value persists per-collection.
- Bubble rendering: custom `QFrame` subclasses with painted rounded
  rectangles, alpha applied via `QColor::setAlpha()`.
- Resizing & locking: `sidebarWidthLocked` disables the splitter drag
  handle; the same flag controls height for top/bottom orientation.
- Adding a new sidebar section: extend `DetailsPane` with a new
  `QGroupBox` / `QFrame`, populate from `ItemMetadata` /
  `CollectionConfig`, and respect the `sidebarHeader*` / `sidebarSection*`
  styling keys.
