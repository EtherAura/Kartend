# Toolbar & Menus

Kartend's chrome — the items-page toolbar across the top of the grid,
the menu bar, the hamburger fallback that replaces the menu bar when
it's hidden, and the keyboard-first **command palette** for jumping
straight to an action by name. This page is a tour of every control
and how to customize / hide each.

> **Where to find this** — Settings Dialog → **General** tab →
> **Toolbar Customization** (visibility / labels). Also `F8`, `F9`,
> `F10` toggles, and `Ctrl+Shift+P` for the command palette.

## Items-page toolbar

A single horizontal strip across the top of the grid. Default contents,
left to right:

| Control | Effect |
|---------|--------|
| **Hamburger** (`≡`) | Opens the menu (only visible when the menu bar is hidden). |
| **Collection title** | Bold label of the active collection. Updates as you navigate. |
| **Subfolder path** | Italic label, shown only when navigating inside a virtual subfolder. |
| **Item position** | Right-aligned counter, e.g. `42 / 1000`. Shows the current selection's position in the filtered list. |
| **View Mode** dropdown | Switches between Grid / List / Cover Flow / Horizontal. See [View Modes](View-Modes.md). |
| **Detail Page** (`ℹ`) | Opens the [item detail page](Item-Metadata.md#detail-page) for the selected item. |
| **Filter** dropdown | Opens a popup with: type filter, hide-subcollections toggle, title-pattern editor. |
| **Preview Volume** slider | Global volume for sidebar and overlay video previews (`previewVideoVolume`). |
| **Search Mode** action | Embedded inside the search bar — toggles between Name and All search. See [Search, Sort & Filter](Search-Sort-Filter.md#search-modes). |
| **Search bar** | Filter items by name (case-insensitive substring). `/` focuses. |

All controls except the search bar have **focus policy NoFocus** — they
don't steal focus from the items grid when clicked, so keyboard
navigation keeps working. The search bar is the exception, since
typing into it requires focus.

Maximum height: 50 px. Tall toolbar elements (labels, sliders) are
sized to fit.

### Customization

Every toolbar control is hideable and many have user-overrideable text:

```ini
[General]
toolbarShowGridViewButton=true
toolbarShowListViewButton=true
toolbarShowCoverFlowViewButton=false
toolbarShowHorizontalViewButton=false
toolbarShowHideSubcollectionsButton=true
toolbarShowTypeFilter=true
toolbarShowTitleFilter=true
toolbarShowSearchModeButton=true
toolbarShowSearchBar=true

toolbarGridViewButtonText=Tiles
toolbarListViewButtonText=Rows
toolbarCoverFlowViewButtonText=Carousel
toolbarHorizontalViewButtonText=Wide
toolbarHideSubcollectionsButtonText=Hide subs
toolbarTitleFilterText=Filter
```

Empty text values fall back to the `.ui`-defined defaults (which are
icon + label combinations chosen to match KDE's Breeze icon theme).

UI access: Settings → **General** tab → **Toolbar Customization**
section.

### Hiding the toolbar entirely

`F8` toggles the toolbar (persistent in `[General] showToolbar`). Hide
it for a chromeless display. The keyboard shortcuts and menu items
that the toolbar buttons mirror still work.

When hidden, the **hamburger** falls back into a corner of the grid
viewport — see below.

## Menu bar

Top of the window. Default visibility persists in `[General]
showMenuBar`; toggle with `F10`.

Menus, in order:

### File

| Item | Shortcut | Effect |
|------|----------|--------|
| **Soft Refresh** | `F5` | Re-render the current view without re-scanning the database. |
| **Rescan Collection** | `Ctrl+F5` | Drop and rebuild the database for the current collection (re-reads the media + artwork directories). |
| (separator) |
| **Recent ▶** | (submenu) | Most recently launched 10 items. Click to launch directly. |
| **Most Launched ▶** | (submenu) | Top 10 items by play count. Click to launch directly. |
| **Open Random Item** | `Ctrl+Shift+R` | Pick and launch a random item from the current collection. |
| (separator) |
| **Import Kart…** | | Opens file picker → import a `.kart` backup. A preflight dialog reviews the bundle before any disk writes. See [Backup & Migration](Backup-and-Migration.md). |
| **Export Collection as Kart…** | | Opens file picker → export the active collection as `.kart`, bundling its config, items, per-item metadata, and playlists. |
| (separator) |
| **Import Theme…** | | Apply a `*.kartend-theme.json` theme preset to the active collection — with a per-field change preview before anything writes. See [Themes & Appearance → Theme presets](Themes-and-Appearance.md#theme-presets). |
| **Export Current Theme…** | | Save the active collection's appearance as a `*.kartend-theme.json` preset. |
| **Layout Profiles…** | | Save / apply / delete named layout snapshots. See [Themes & Appearance → Layout profiles](Themes-and-Appearance.md#layout-profiles). |
| **Presentation Profiles…** | | Save / apply / delete named attract / marquee / splash bundles. See [Themes & Appearance → Presentation profiles](Themes-and-Appearance.md#presentation-profiles). |
| (separator) |
| **New Library Wizard…** | | Multi-step guided creation of a new collection. See [Getting Started → New Library Wizard](Getting-Started.md#new-library-wizard). |
| **Assign Missing Artwork…** | | Walks items missing artwork through a fuzzy-ranked candidate picker. See [Artwork → Artwork Assignment Wizard](Artwork.md#artwork-assignment-wizard). |
| **Review Missing Metadata…** | | One-item-at-a-time queue for items with empty title / description / genre / artwork. See [Item Metadata → Missing-metadata review](Item-Metadata.md#missing-metadata-review). |
| **Bulk Edit Items…** | | Collection-wide tag / pin / hide / continue-later / rating changes. See [Item Metadata → Bulk edit](Item-Metadata.md#bulk-edit-dialog). |
| **Duplicates and Variants…** | | Group same-basename items per collection (e.g. the FLAC + MKV pairing of a concert recording) and launch a chosen variant. See [Collections → Variant Inspector](Collections.md#variant-inspector). |
| **Collection Health…** | | Diagnostic dashboard listing missing files, missing artwork, and launcher issues across collections. See [Collections → Collection Health Dashboard](Collections.md#collection-health-dashboard). |
| **Scraper…** | | Unified scrape dialog (tree of collections + items, media-type checkboxes, auto/interactive toggle). Replaces the older Help → *Batch Scrape Current Collection* entry. See [Scraper](Scraper.md). |
| (separator) |
| **Exit** | `Ctrl+Q` | Quit. |

### View

| Item | Shortcut | Effect |
|------|----------|--------|
| **Show Menu Bar** | `F10` (checkable) | Persistent toggle for the menu bar. |
| **Show Toolbar** | `F8` (checkable) | Persistent toggle for the items-page toolbar. |
| **Show Details Pane** | `F9` (checkable) | Persistent toggle for the sidebar (per-collection). |
| (separator) |
| **Layout ▶** | (submenu) | Choose Grid / List / Cover Flow / Horizontal. Mirrors `Ctrl+1..4`. |
| **Details Pane Orientation ▶** | (submenu) | Right / Left / Top / Bottom. See [Sidebar & Details Pane](Sidebar-and-Details-Pane.md). |
| **Fullscreen** | `F11` | Toggle fullscreen (persistent). Added dynamically; some platforms expose a window-manager equivalent. |

### Sort

| Item | Effect |
|------|--------|
| **Name (A → Z)** | `sortMode=NameAscending`. |
| **Name (Z → A)** | `sortMode=NameDescending`. |
| (separator) |
| **Date (Newest First)** | `sortMode=DateDescending`. Uses file mtime. |
| **Date (Oldest First)** | `sortMode=DateAscending`. |
| (separator) |
| **Size (Largest First)** | `sortMode=SizeDescending`. |
| **Size (Smallest First)** | `sortMode=SizeAscending`. |
| (separator) |
| **Random** | `sortMode=Random`. |
| (separator) |
| **Exclude Subfolders** | Subcollection / virtual-folder tiles always render at the top regardless of sort. Persistent. |

All sort options are checkable radio entries — exactly one is active.

### Settings

| Item | Shortcut | Effect |
|------|----------|--------|
| **Settings…** | `Ctrl+,` | Open the [Settings Dialog](Settings-Dialog.md). |

### Help

| Item | Shortcut | Effect |
|------|----------|--------|
| **Keyboard Shortcuts** | `F1` | Open the in-app shortcuts reference dialog. |
| **Usage Statistics…** | | Open the [Statistics Dialog](History-and-Statistics.md). Double-clicking a row navigates to the item. |
| **Binding Visualizer…** | | Interactive keyboard + gamepad binding reference with press-to-identify. See [Input & Controls → Binding Visualizer](Input-and-Controls.md#binding-visualizer). |
| **Setup Wizard…** | | Re-run the first-run setup wizard. The auto-launch on first run is one-shot (controlled by `[General] firstRunComplete`); this entry lets you re-enter the flow manually. |
| **Scraper Providers…** | | Read-only registry of built-in metadata providers, their categories, and credential status. See [Scraper → Provider Registry](Scraper.md#provider-registry). |
| **Scraper Credentials…** | | Per-provider credential editor. Same dialog as Settings → Scrapers → Credentials. |
| (separator) |
| **About** | | App info dialog (name, version, license). |
| **About Qt** | | Standard Qt info dialog. |

### When to hide menu items

There's currently no per-item visibility toggle in the menu bar — all
items are always shown. If you want a chromeless display, hide the
**entire menu bar** with `F10`; the hamburger fallback exposes
abbreviated versions of the most common items.

## Command palette

`Ctrl+Shift+P` opens the **Command Palette** — a keyboard-first
overlay that fuzzy-matches every menu action plus every collection in
the live tree, so you can jump to a command (or a collection) without
navigating menus.

```
┌────────────────────────────────────────────────────────────┐
│ ┌──────────────────────────────────────────────────────┐   │
│ │ scrap                                                │   │
│ └──────────────────────────────────────────────────────┘   │
│  File      Scraper…                                        │
│  Help      Scraper Credentials…                            │
│  Help      Scraper Providers…                              │
└────────────────────────────────────────────────────────────┘
```

| Key | Effect |
|-----|--------|
| Type | Filter the list (subsequence + word-boundary scoring, case-insensitive). |
| `↑` / `↓` | Move selection in the result list. |
| `Enter` | Invoke the selected command. |
| `Escape` | Close without doing anything. |

Each row carries a short **category** label (File, View, Help, Sort,
Collection, …) so commands from different surfaces stay
distinguishable when ranked together. The matcher considers the
category text too, so typing `view fullscreen` narrows fast.

The list is rebuilt each time the palette opens, so newly added
collections and dynamic submenu entries (Recent, Most Launched) show
up without restart.

## Hamburger fallback

When the menu bar is hidden (`F10` off), a hamburger button (`≡`)
appears at the start of the items-page toolbar. Click it to access:

- **File** items (Soft Refresh, Rescan, Recent / Most Launched /
  Random, Import / Export Kart, Theme presets, Layout / Presentation
  Profiles, New Library Wizard, Artwork / Metadata / Bulk / Variants /
  Health dialogs, Scraper, Exit)
- **View** items (Layout, Sidebar Orientation, Show Toolbar, Show
  Details Pane, Fullscreen — but not "Show Menu Bar" since you'd lock
  yourself out)
- **Sort** items
- **Help** items (Shortcuts, Usage Statistics, Binding Visualizer, Setup Wizard, Scraper Providers / Credentials, About)

The hamburger menu is intentionally abbreviated — it's the fallback,
not a perfect mirror. To access everything, restore the menu bar
(`F10`).

If both the menu bar *and* the toolbar are hidden, the hamburger falls
back further into a small floating button anchored in the upper-left
of the grid. The button auto-hides after a few seconds of mouse
inactivity (and reappears on movement) to keep the chromeless look.

## Customization recipes

### Minimal kiosk: hide everything chrome

```ini
[General]
showMenuBar=false
showToolbar=false
fullscreen=true
attractModeEnabled=true
attractModeIdleTimeoutSec=60
```

The hamburger floating button gives you a way back. Combine with
[attract mode](Attract-Mode.md) for a kiosk display.

### Streamlined toolbar: search and one view button only

```ini
[General]
toolbarShowGridViewButton=true
toolbarShowListViewButton=false
toolbarShowCoverFlowViewButton=false
toolbarShowHorizontalViewButton=false
toolbarShowHideSubcollectionsButton=false
toolbarShowTypeFilter=false
toolbarShowTitleFilter=false
toolbarShowSearchModeButton=false
toolbarShowSearchBar=true
```

`Ctrl+1..4` still switches view modes from the keyboard.

### Localized toolbar labels

```ini
toolbarGridViewButtonText=Quadrícula
toolbarListViewButtonText=Lista
toolbarCoverFlowViewButtonText=Cover Flow
toolbarHorizontalViewButtonText=Horizontal
toolbarHideSubcollectionsButtonText=Ocultar subc.
toolbarTitleFilterText=Filtre
```

There's no built-in i18n yet — these manual labels are the way to
localize. Filed under "future Qt Linguist integration."

### Custom button icons

Not yet supported via config — icons come from the bundled KDE Breeze
theme. Replacing icons requires a recompile. The `toolbar*Text`
overrides change the *label*, not the icon.

## Where to next

- [Settings Dialog](Settings-Dialog.md) — the dialog reached via
  **Settings → Settings…**
- [Input & Controls](Input-and-Controls.md) — full keyboard / mouse /
  gamepad reference, including how to rebind navigation keys
- [View Modes](View-Modes.md) — what the toolbar's view dropdown
  switches between
- [Search, Sort & Filter](Search-Sort-Filter.md) — what the toolbar's
  search bar and filter dropdown control

## For developers

- Toolbar definition: [src/core/mainwindow.ui](../../src/core/mainwindow.ui)
  + [src/core/toolbarcontroller.h](../../src/core/) (which wires
  visibility & label overrides from `GeneralSettings`).
- Menu definition: same `mainwindow.ui` + `menucontroller.cpp`.
- Hamburger fallback: `MainWindow` swaps the menu bar's items into the
  hamburger menu when the menu bar hides; the floating-button mode is
  in the same controller.
- Adding a new toolbar control: extend `mainwindow.ui` with the widget,
  add `toolbarShow*` and `toolbar*Text` keys to `GeneralSettings`, wire
  visibility / label updates in `toolbarcontroller.cpp`, add UI rows in
  the Settings Dialog's General tab.
- Item position label: updated by `ScrollManager` whenever selection or
  filter changes.
