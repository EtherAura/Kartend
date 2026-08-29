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

> **Looking for the other one?** The *navigation sidebar* — the collection
> tree — is a different panel with its own settings. See
> [the collection tree](#the-other-sidebar-the-collection-tree).

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

## The other sidebar: the collection tree

Kartend has **two** sidebars and they are easy to confuse. The details
pane described on this page shows *the selected item*. The **navigation
sidebar** — the collection tree — shows *where you can go*: your
collections and subcollections as an expandable tree, so you can jump
between them without walking back up through the grid.

It has its own settings, its own dock side, and its own toggle:

| | Details pane | Collection tree |
|---|---|---|
| Shows | The selected item | Your collections |
| Toggle | `F9` | **View → Show Collection Tree** |
| Dock side | Any of four edges | Left or Right |
| Settings | Settings Dialog → **Sidebar** tab | Same tab, its own group |

**Toggling it.** There is no default keyboard shortcut — use **View →
Show Collection Tree**, or bind one under Settings Dialog → **Controls**
→ *Toggle Collection Tree*. On a gamepad it is **L1** by default,
rebindable in the same place.

**Per-collection, like everything else here.** Visibility, dock side,
width, and every appearance option below are remembered per collection,
so a games library can carry the tree while a film library does not.

**Dock side** is **View → Collection Tree Position → Left / Right**, or
the same tab in the settings dialog. Only left and right are offered —
a tree of names wants height, and a horizontal dock would give it none.

**Width** is set by dragging the panel's inner edge, and is clamped to
140–600px so a hand-edited config cannot collapse it to nothing.

**Layout options**, all in the settings dialog:

| Option | What it does |
|--------|--------------|
| **Overlay / Expand** | Whether the panel floats above the grid or pushes it aside. Expand is the default. Overlay leaves the grid completely still when the panel opens. |
| **Full-height / Below toolbar** | Whether the panel spans the whole window with the toolbar stopping at its edge, or sits under a full-width toolbar. Full-height is the default here — unlike the details pane. |
| **Scrollbar** | Show / Auto-hide / Hide, same three choices as the details pane. |

**Row appearance**:

| Option | What it does |
|--------|--------------|
| **Row display** | *Text only* (default), *Icon and text*, or *Icon only* — what a row shows when it has artwork. Rows without artwork always show their name; a blank row would be unusable. |
| **Icon size** | Row artwork edge length in pixels. |
| **Icon style** | Normal, Monochrome (dark or light), or Tinted. Tinted is the default and recolours silhouette art to the row's text colour. |
| **Branch lines** | Connector lines between parents and children. Off by default — the chevrons already carry the structure. |
| **Scroll names that do not fit** | A narrow sidebar elides long platform names down to a shared prefix, hiding exactly the part that tells them apart. Two independent switches: scroll every clipped row (off by default — unprompted movement beside an artwork grid is intrusive), and scroll the row under the pointer (on by default, since pointing at a row is a deliberate "what is this?"). |

Rows can also carry a small **system glyph** taken from a local
RetroArch install — see [System icon](#system-icon-from-retroarch)
below, which covers both sidebars.

## Modes: Overlay vs. Expand

> The table below describes the **details pane**. The collection tree
> has the same two modes, set separately — see
> [the collection tree](#the-other-sidebar-the-collection-tree) above.

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

### Scrollbar behaviour

Settings Dialog → **Sidebar** tab → *Details pane* → **Scrollbar** offers
three choices, and the navigation sidebar (collection tree) has its own in
the group below it. Both are per-collection and independent.

| Mode | What you get |
|------|--------------|
| **Show** | Normal — the bar fades in while scrolling or hovering. |
| **Auto-hide** | Nothing until the pointer comes near the edge the bar lives on, then it appears. |
| **Hide** | Never drawn, and the strip it reserved goes back to your content. |

Only the *indicator* changes — the wheel, the arrow keys and drag-scrolling
work in every mode.

> **Auto-hide needs the slim overlay scrollbars** (Settings → General).
> Proximity is a property of the drawn handle, so with overlay scrollbars
> switched off Auto-hide behaves as Show rather than hiding a bar that could
> never come back.

| Setting | INI key | Values |
|---------|---------|--------|
| Details-pane scrollbars | `sidebarHideScrollbar` | `show` / `autohide` / `hide` |
| Navigation-sidebar scrollbars | `collectionTreeHideScrollbar` | `show` / `autohide` / `hide` |

The keys keep their old `hide…` names so existing configs migrate in place:
a previously saved `true` reads as `hide`, `false` as `show`.

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
| **Notes** | Free-form notes from the [Edit Metadata Dialog](Item-Metadata.md#edit-metadata-dialog). Multi-line; hidden when unset. |
| **Tags** | Tag list set via the same dialog; chip-style. Searchable via the `tag:` [structured token](Search-Sort-Filter.md#structured-search-tokens). |
| **Rating** | Half-star rating from the same dialog. Hidden when unrated. |
| **Source URL** | Clickable source URL from the same dialog. |
| **Custom fields** | User-defined key/value pairs from the [Edit Metadata Dialog](Item-Metadata.md#edit-metadata-dialog). |
| **Play count** | From `play_count` in the database (Statistics dialog drives this too). |
| **Last played** | From `last_played`. Format: `5 minutes ago`, `Yesterday`, etc. |
| **Time played** | Sum from `launch_history` (only populated if [runtime detection](Splash-and-Now-Playing.md#what-runtime-detection-turns-on) is enabled). |

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

## System icon (from RetroArch)

A game collection can carry a small **console, controller or cartridge icon
to the left of its name** in the navigation sidebar, taken from a local
RetroArch installation. Nothing is downloaded: if RetroArch is installed, its
icons are already on disk, they match the version you actually have, and they
cost no network.

> **Where to find this** — Settings Dialog → Appearance → **Sidebars** →
> **System Icon (from RetroArch)**. New game collections also get a
> **Sidebar Icon** row in the create-collection dialog.

This is a **separate setting from *Rows show*** above. That one governs the
collection's *artwork* — the logo a scrape fetched. This is a small fixed
glyph that says *what machine this is*, and it appears in every *Rows show*
mode that draws a name, including the default *Name only*. (It is skipped in
*Icon only*, where the row deliberately **is** the picture and there is no
name for it to sit beside.)

| Setting | INI key | Notes |
|---------|---------|-------|
| Show an icon beside this collection's name | `systemIconEnabled` | Off by default. |
| Show | `systemIconSubject` | *Controller*, *Console*, or *Cartridge / disc*. |
| System | `systemIconName` | Which machine. Type to search — the list is long. |
| Icon set | `systemIconPack` | *Automatic* suits the subject; or name a set. |
| Icon style | `systemIconStyle` | Normal, monochrome dark/light, or tinted. |
| Position | `systemIconPlacement` | Before the name, after it, or at the panel edge. |
| Icon height | `systemIconSize` | 8–64 px. Small by intent. |

### Choosing a subject

RetroArch ships several icon sets, and each holds exactly **one icon per
system** — so whether you get a console or a controller is decided by the set,
not chosen within it. `monochrome`, `retrosystem`, `flatux`, `flatui`, `pixel`
and `daite` draw the **controller**; `automatic`, `systematic` and `dot-art`
draw the **console**.

Because of that, **the Icon set list only offers sets that actually hold art
for the subject you picked**. Choosing *Console* will not list `monochrome`:
it has no console art, so picking it there could only ever hand back the
controller. For a plain outline console, pick *Console* and then `automatic`.

If a set saved earlier no longer matches the subject — from an older config,
or a hand-edited one — the subject wins and the icon comes from a set that can
draw it, rather than quietly giving you the wrong kind.

One wrinkle worth knowing: for a handheld, a home computer or an arcade board
there is no separate controller, so **every** set draws the machine itself. A
Game Boy is a Game Boy in `monochrome` just as much as in `systematic`. The
filter still applies there, so you get the machine from a console set instead
— the same subject, a different style.

*Cartridge / disc* is different again — it is the media icon that ships
beside every system icon, so it works in whichever set is in play.

Sets differ enormously in how many systems they cover — from a few dozen to a
few hundred — so the picker shows the count beside each name. If you name a
set that has no icon for your system, the row simply shows no icon and the
settings page says so; it will not quietly substitute a different set's art.

### Where the icon sits

*Position* offers three placements. **Before the name** and **After the name**
keep the icon next to the text, so it travels with the name — on a centred
row the pair stays centred together. **At the panel edge** pins the icon to
the sidebar's inner edge instead, so a column of icons lines up regardless of
how long the names are; the trade is that on a short name the icon sits
outside the row's highlight rather than inside it.

Icons are never drawn in *Icon only* mode — there is no name for them to
accompany there.

### Getting the icon onto a collection

New game collections pick their system up automatically: type the collection
name in the create dialog and the **Sidebar Icon** row fills itself in, using
the same detection that fills the ScreenScraper System row beside it.
Shorthand works — a collection called *SNES* finds *Nintendo - Super
Nintendo Entertainment System*.

For collections you already have, open Settings → Appearance → Sidebars, tick
the box and press **Detect** to guess from the collection's name, or pick the
system yourself. Detection deliberately gives up rather than guess between
two equally good candidates, so an ambiguous name leaves the list on *None*.

To do a whole tree at once, set it up on the parent collection and switch
**Mode** (above the collections list) to *Current + subcollections* before
saving. The look — subject, set, position, size — is copied down to every
subcollection, but the *system* is not: each one detects its own from its own
name, so a shelf of platforms ends up with a different icon each rather than
the parent's repeated. Subcollections that already name a system keep it.

Two kinds of collection are skipped by detection entirely, because neither is
a machine:

- **Launcher imports** — Steam, Flatpak, Lutris and the rest are storefronts,
  and RetroArch's sets have no icon for them.
- **Shells** — a collection with children, like a *Nintendo* or *Sega* group,
  organises systems rather than being one. Matching a bare manufacturer name
  against system names could only pick one of its children at random.

The **System** list starts with two answers that are not systems at all:
*None (no icon)* leaves the row bare, and *This collection's own artwork* draws
whatever art the collection already has. Those are how you clear or override a
row individually — useful where a manufacturer's logo just repeats the name
next to it.

Choosing a collection's own artwork puts it at the same size
and position as the icon — so a manufacturer shell shows the company logo a
scrape fetched for it, and a shelf of platforms and their manufacturers reads
as one list rather than two different treatments. **Icon style** inks both kinds of art the same way — system icons and
collection artwork alike — so a manufacturer logo matches the platform icons
beneath it instead of being the one thing in colour. *Tinted* follows the
navigation sidebar's tint colour. Whatever you pick, the luminance of the
original is preserved, so a logo keeps its internal detail rather than
flattening into a blob.

Re-running the apply **corrects** these rows: a shell or launcher import that
picked up a wrong system in an earlier run has it cleared, not preserved. A
system you chose yourself on an ordinary collection is never overwritten.

### Right-click a row

The navigation sidebar has a context menu, and it acts on **the row you
right-clicked** rather than the collection you are currently in — so you can
fix a row without navigating to it first.

| Action | What it does |
|--------|--------------|
| **Set Custom Icon…** | Pick any image file for that row. It becomes the collection's icon and is shown at the same size and position as the system icons. |
| **Detect System Icon** | Guess the system from the row's name. Says so if nothing matches, rather than appearing to do nothing. |
| **Remove Icon** | Turns the icon off for that row. The system and artwork are kept, so switching it back on restores them. |

Detection may revise a system **it** guessed earlier — so if an earlier version
put the wrong icon on a row, re-running detection now corrects it. A system you
picked yourself is never overwritten.

### If RetroArch is not found

The whole section is disabled and says so. Kartend looks in the standard
per-OS locations, and honours the RetroArch path you may already have set
under Settings → Launchers — the same one the libretro core picker uses, so
you never point at your install twice.

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
