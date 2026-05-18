# Configuration Reference

Every INI key Kartend reads, grouped by purpose. The Settings Dialog
covers the same surface; this page is for hand-editing, scripting,
diff-tracking dotfiles, or just orientation.

The configuration file lives at `~/.config/kartend/kartend.cfg` (see
[File Locations](File-Locations.md)). Format is standard INI:

- `[General]` — global settings.
- `[Collection Name]` — one section per top-level collection.
- `[Parent > Child]` — subcollections, where `> ` (with space) is the
  literal hierarchy separator.

Restart Kartend after editing the file by hand. Changes through the
Settings Dialog are saved immediately.

> **Path expansion** — paths support `~` for the user home directory.
> Environment variables like `$HOME` and `$XDG_*` are *not* expanded;
> use `~` or absolute paths.

> **Config-only keys** — most keys on this page have a matching
> control in the [Settings Dialog](Settings-Dialog.md), but a handful
> are intentionally hand-edit only. They're flagged inline below
> with **(config-only)**. To change them, edit `kartend.cfg`
> directly and restart Kartend. Today: `hideSubcollectionTiles`,
> `listCollectionColumnWidth`, `listArtworkColumnWidth`.

## Conventions

| Type | Format | Examples |
|------|--------|----------|
| `bool` | `true` / `false` | `rememberSelection=true` |
| `int` | integer | `gridWidth=6` |
| `float` | decimal | `attractModeScrollSpeed=1.5` |
| `path` | filesystem path with optional `~` | `mediaDirectory=~/Videos` |
| `csv` | comma-separated values, no spaces around commas | `extensions=mkv,mp4,webm` |
| `hex` | `#RRGGBB` color | `backgroundColor=#1a1a2e` |
| `enum` | one of a fixed value list (documented inline) | `viewType=list` |
| `int (0–255)` | 8-bit integer (used for alpha and tint) | `sidebarHeaderBgOpacity=200` |

## `[General]` — global settings

### Selection & navigation

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `rememberSelection` | bool | `true` | Persist per-collection selection across sessions. |
| `wrapNavigation` | bool | `false` | Wrap selection at grid edges. |
| `selectItemOnHover` | bool | `false` | Auto-select when the pointer enters a tile. |
| `startupCollection` | string | empty | Open this collection on launch. Empty = first root collection. |
| `useHomeView` | bool | `false` | Open a synthetic Home view at startup that shows one tile per root collection. Takes effect when `startupCollection` is empty. `Back` from any root-level collection returns here. See [Shell Collections](Shell-Collections.md#nesting-shells). |

### Performance & caching

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `pixmapCacheSizeMB` | int | `50` | Qt pixmap cache budget. |
| `scrollAnimationDurationMs` | int | `1500` | Scroll ease duration. |
| `scrollVelocityMultiplier` | float | `1.0` | Global scroll speed multiplier (0.25–5.0). |

### Keyboard repeat

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `keyboardRepeatIntervalMs` | int | `260` | Key-repeat cadence in grid views. |
| `keyboardRepeatDelayMs` | int | `260` | Initial delay before repeat. |
| `listKeyboardRepeatIntervalMs` | int | `50` | Key-repeat cadence in List view. |
| `clickHoldDelayMs` | int | `500` | Hold duration before click-hold scrolling activates. |
| `clickHoldRepeatIntervalMs` | int | `320` | Hold-scroll cadence in grid views. |
| `listClickHoldRepeatIntervalMs` | int | `80` | Hold-scroll cadence in List view. |
| `mouseWheelRows` | int | `1` | Rows scrolled per wheel tick. |

### Keyboard bindings

All values are `Qt::Key` numeric codes. The Settings Dialog provides a
key-capture widget so you don't need to look these up.

| Key | Default | Description |
|-----|---------|-------------|
| `keyNavLeft` | `Qt::Key_Left` | Move selection left. |
| `keyNavRight` | `Qt::Key_Right` | Move selection right. |
| `keyNavUp` | `Qt::Key_Up` | Move selection up. |
| `keyNavDown` | `Qt::Key_Down` | Move selection down. |
| `keyConfirm` | `Qt::Key_Return` | Launch / enter subcollection. |
| `keyBack` | `Qt::Key_Escape` | Back / close overlay. |
| `keySearch` | `Qt::Key_Slash` | Focus search bar. |
| `keyAlphabeticBack` | `Qt::Key_PageUp` | Jump to previous starting letter. |
| `keyAlphabeticForward` | `Qt::Key_PageDown` | Jump to next starting letter. |
| `keyJumpFirst` | `Qt::Key_Home` | Jump to first item. |
| `keyJumpLast` | `Qt::Key_End` | Jump to last item. |
| `keyItemDetails` | `Qt::Key_I` | Open the full-screen detail page. |

See [Input & Controls](Input-and-Controls.md#keyboard) for the user-
facing names and rebinding workflow.

### Gamepad

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `gamepadUseDpad` | bool | `true` | Enable D-pad navigation. |
| `gamepadUseLeftStick` | bool | `true` | Enable left-stick navigation. |
| `gamepadConfirmButton` | string | `A` | Confirm / launch button name. |
| `gamepadBackButton` | string | `B` | Back / escape button. |
| `gamepadToggleSidebarButton` | string | `Y` | Toggle sidebar button. |

Button names are the SDL / Qt6::Gamepad standard labels (`A`, `B`, `X`,
`Y`, `LB`, `RB`, etc.).

### Mouse

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `artworkCycleModifier` | enum | `Shift` | Modifier paired with middle-click to cycle artwork types. One of `Shift`, `Control`, `Alt`, `Meta`. |

### Sorting

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `sortMode` | enum | `NameAscending` | One of `NameAscending`, `NameDescending`, `DateAscending`, `DateDescending`, `SizeAscending`, `SizeDescending`, `Random`. |
| `excludeSubfoldersFromSort` | bool | `false` | Keep subcollection / virtual-folder tiles at the top regardless of sort. |

### Type filter & subcollection visibility

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `collectionTypeFilter` | string | empty | Limit the visible collections to ones with this `type` value. |
| `hideSubcollectionTiles` | bool | `false` | Hide subcollection tiles, show only direct media items. **(config-only)** |

### View toggles (persistent)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `showMenuBar` | bool | `true` | Persistent state of `F10`. |
| `showToolbar` | bool | `true` | Persistent state of `F8`. |
| `fullscreen` | bool | `false` | Persistent state of `F11`. |

### Toolbar customization

Visibility and label of each items-page toolbar control. All boolean
visibility keys default to `true`; text overrides default to empty
(meaning "use the .ui default").

| Key | Type | Description |
|-----|------|-------------|
| `toolbarShowGridViewButton` | bool | Show **Grid** view button. |
| `toolbarShowListViewButton` | bool | Show **List** view button. |
| `toolbarShowCoverFlowViewButton` | bool | Show **Cover Flow** button. |
| `toolbarShowHorizontalViewButton` | bool | Show **Horizontal** button. |
| `toolbarShowHideSubcollectionsButton` | bool | Show **Hide Subcollections** toggle. |
| `toolbarShowTypeFilter` | bool | Show **Type Filter** dropdown. |
| `toolbarShowTitleFilter` | bool | Show **Title Filter** entry. |
| `toolbarShowSearchModeButton` | bool | Show search-mode toggle. |
| `toolbarShowSearchBar` | bool | Show the search input. |
| `toolbarGridViewButtonText` | string | Override **Grid** button label. |
| `toolbarListViewButtonText` | string | Override **List** label. |
| `toolbarCoverFlowViewButtonText` | string | Override **Cover Flow** label. |
| `toolbarHorizontalViewButtonText` | string | Override **Horizontal** label. |
| `toolbarHideSubcollectionsButtonText` | string | Override **Hide Subcollections** label. |
| `toolbarTitleFilterText` | string | Override **Filter** label. |

### List view column widths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `listCollectionColumnWidth` | int | `150` | Width of the collection / icon column. **(config-only)** |
| `listArtworkColumnWidth` | int | `32` | Width of the artwork thumbnail column. **(config-only)** |

### Text, fonts, zoom

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `globalUiFontFamily` | string | empty | Font family for the entire UI. Empty = platform default. |
| `globalUiFontPointSize` | int | `0` | Point size for the entire UI. `0` = platform default. |
| `uiTextZoomPercent` | int | `100` | Text zoom percentage; bound to `Ctrl+=` / `Ctrl+-` / `Ctrl+0`. |
| `titleTintSaturation` | int (0–255) | `180` | Tile-title tint saturation. |
| `titleTintLightness` | int (0–255) | `60` | Tile-title tint lightness. |
| `titleBaseColor` | hex | empty | Tile-title base color. Empty = use selection color. |
| `showTitleInPlaceholder` | bool | `false` | Overlay item title on placeholder tiles. |

### Splash screens & startup video

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bootSplashEnabled` | bool | `true` | Show splash overlay on launch. |
| `resumeFocusSplashEnabled` | bool | `true` | Show splash when window regains focus after a launched item exits. |
| `startupVideoEnabled` | bool | `false` | Play an intro video on launch. |
| `startupVideoPath` | path | empty | Path to startup video. |

### Preview video

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `previewVideoVolume` | int (0–100) | `100` | Volume for sidebar / overlay video previews. |

### Runtime detection & history

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `runtimeDetectionEnabled` | bool | `false` | Track when launched items run; show **Now Playing** overlay. See [Splash & Now Playing](Splash-and-Now-Playing.md). |
| `historyEnabled` | bool | `true` | Record launch history. |
| `historyMaxEntries` | int | `500` | Soft cap on history rows. `≤ 0` = unlimited. |

### Attract mode

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `attractModeEnabled` | bool | `false` | Master toggle. See [Attract Mode](Attract-Mode.md). |
| `attractModeIdleTimeoutSec` | int | `120` | Seconds of inactivity before attract starts. |
| `attractModeAutoScrollEnabled` | bool | `true` | Smoothly scroll the viewport during attract. |
| `attractModeScrollSpeed` | float | `1.0` | Pixels-per-tick scroll speed (0.1–10). |
| `attractModeAdvanceSelectionEnabled` | bool | `false` | Periodically move the selection. |
| `attractModeAdvanceSelectionIntervalSec` | int | `5` | Seconds between selection advances. |
| `attractModeAdvanceSelectionRandom` | bool | `false` | Random vs. sequential advance. |

### Launcher presets

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `launcherPresets` | serialized | empty | Reusable launcher configs (id / name / path / core / params). Managed via the Settings → Launcher tab. |

### Session state

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `lastSelectedItems` | hash | empty | Auto-managed map of collection → last selected item index. |

## `[<Collection Name>]` — per-collection settings

Every collection has its own section. The section header is the
display name; renaming a collection rewrites the section header.

### Identity & paths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `name` | string | section header | Display name. |
| `type` | string | empty | Media-type tag — a preset (Video / Audio / Images / Documents / Games) or a custom value. Used by the [type filter](Search-Sort-Filter.md#type-filter) and to pick a scraper. |
| `scraperProviderId` | string | empty | Pinned metadata scraper id (`tmdb`, `screenscraper`, `musicbrainz`, `openlibrary`). Empty = resolve automatically from `type`. |
| `mediaDirectory` | path | empty | Folder of items. Empty = parent-only. |
| `artworkDirectory` | path | empty | Folder of cover images. |
| `videoDirectory` | path | empty | Folder of preview videos. |
| `manualDirectory` | path | empty | Folder of per-item manuals. |
| `extensions` | csv | empty | File extensions to scan. Empty = all. |
| `collectionIcon` | path | empty | Subcollection tile icon. |
| `placeholderArtwork` | path | empty | Custom placeholder image. |

### Hierarchy

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `parentCollectionIndex` | int | `-1` | Index of primary parent. -1 = root. |
| `additionalParentNames` | csv | empty | Linked-parent names. See [Collections → Linked Parents](Collections.md#linked-parents). |
| `isSubcollection` | bool | derived | Auto-set from parent. |

### Launcher

See [Launchers](Launchers.md) for the complete model.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `launcherPath` | path | empty | Primary launcher executable. |
| `launcherName` | string | empty | Display name. Empty = filename of `launcherPath`. |
| `corePath` | path | empty | LibRetro core (RetroArch only). |
| `launchParameters` | string | empty | Extra arguments passed before the file path. |
| `additionalLaunchers` | serialized | empty | Secondary launchers (id / name / path / core / params / presetId). |
| `defaultLauncherIndex` | int | `0` | Default selection index (0 = primary). |

### Content / scanning

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `includeContentSubfolders` | bool | `false` | Show subfolders as virtual-folder tiles. |
| `includeArtworkSubfolders` | bool | `false` | Match artwork in subfolders too. |
| `showAllSubfolderItems` | bool | `false` | Mix subfolder items into the parent grid. |
| `showAllSubcollectionItems` | bool | `false` | Mix descendant collection items into this grid. |
| `showHiddenFolders` | bool | `false` | Include dot-prefixed folders. |
| `extractArchives` | bool | `false` | Auto-extract archives before launch. |
| `extractedExtension` | string | empty | Which extension to launch from inside an archive. |

### Grid layout

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `viewType` | enum | `grid` | `grid` / `list` / `coverflow` / `horizontal`. |
| `gridWidth` | int | `4` | Items per row in Grid / List views. |
| `gridWidthSidebarHidden` | int | derived | Override `gridWidth` when the **Expand** sidebar is hidden. |
| `horizontalGridHeight` | int | `4` | Items per column in Horizontal view. |
| `horizontalGridHeightSidebarHidden` | int | derived | Override for Horizontal when sidebar hidden. |
| `gridHeightSidebarHidden` | int | derived | Override when **Expand** sidebar is docked top/bottom. |
| `itemWidth` | int | `200` | Tile width (pixels). |
| `itemHeight` | int | `200` | Tile height (pixels). |
| `fontSize` | int | `12` | Tile-title font size. |
| `cornerRadius` | int | `8` | Tile rounded-corner radius. |
| `horizontalSpacing` | int | `20` | Pixel gap between columns. |
| `verticalSpacing` | int | `20` | Pixel gap between rows. |
| `horizontalAlignment` | enum | `center` | `left` / `center` / `right`. |
| `hideTitles` | bool | `false` | Hide tile titles. |
| `hideSubcollectionTitles` | bool | `false` | Hide titles on subcollection tiles. |
| `hideSubfolderTitles` | bool | `false` | Hide titles on virtual-folder tiles. |
| `hideMissingArtwork` | bool | `false` | Hide items with no artwork. |
| `hideHorizontalScrollbar` | bool | `false` | Hide horizontal scrollbar. |
| `hideVerticalScrollbar` | bool | `false` | Hide vertical scrollbar. |

### List view

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `listFontSize` | int | `11` | Row text size. |
| `listRowHeight` | int | `40` | Row height in pixels. |
| `listRowColor` | hex | derived | Row background. |
| `listAltRowColor` | hex | derived | Alternating row background. |

### Background & visual effects

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `backgroundType` | enum | `color` | `color` / `image` / `video`. |
| `backgroundColor` | hex | `#1a1a2e` | Solid background color. |
| `backgroundImage` | path | empty | Wallpaper image. |
| `backgroundVideo` | path | empty | Looping muted video wallpaper. |
| `primaryColor` | hex | derived | Toolbar / menu bar / chrome color. |
| `tileColor` | hex | derived | Item tile / placeholder color. |
| `selectionColor` | hex | derived | Selection rectangle color. |
| `vignetteEnabled` | bool | `false` | Darken the viewport corners. |
| `vignetteIntensity` | int (0–100) | `50` | Vignette strength. |
| `wallpaperParallax` | bool | `false` | Background image scrolls slower than items. |
| `parallaxStrength` | int (0–100) | `30` | Parallax scrolling factor. |
| `toolbarBackdropBlur` | bool | `false` | Blur the toolbar background. |
| `backdropBlurRadius` | int | `12` | Blur radius (pixels). |

### Title cleanup

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `titleExclusionPatterns` | csv (regex) | empty | Patterns to strip from displayed titles. |
| `titleExclusionEnabled` | bool | `false` | Toggle the pattern list without losing it. |

### Header logo

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `headerLogoImage` | path | empty | Logo image painted at the top of the grid. |
| `headerLogoPosition` | enum | `topleft` | `topleft` / `topcenter` / `topright`. |

### Sidebar (per-collection styling)

See [Sidebar & Details Pane](Sidebar-and-Details-Pane.md) for behavior.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `sidebarVisible` | bool | `false` | Sidebar shown by default. |
| `sidebarMode` | enum | `overlay` | `overlay` (floating) / `expand` (docked). |
| `sidebarPosition` | enum | `right` | `right` / `left` / `top` / `bottom`. |
| `sidebarWidth` | int | `300` | Preferred width (pixels). |
| `sidebarHeight` | int | `200` | Preferred height for top/bottom dock. |
| `sidebarWidthLocked` | bool | `false` | Disable resize dragging. |
| `sidebarBackgroundType` | enum | `color` | `color` / `image` / `pattern`. |
| `sidebarBackgroundColor` | hex | derived | Sidebar background color. |
| `sidebarBackgroundImage` | path | empty | Sidebar background image. |
| `sidebarPattern` | enum | `crosshatch` | Pattern fill (only `crosshatch` today). |
| `sidebarPatternIntensity` | int (0–100) | `50` | Pattern opacity. |
| `sidebarPatternColor` | hex | derived | Pattern tint color. |
| `sidebarTextColor` | hex | derived | Sidebar text color. |
| `sidebarAccentColor` | hex | derived | Sidebar highlight color. |
| `sidebarHeaderBgColor` | hex | derived | Section-header bubble color. |
| `sidebarHeaderBgOpacity` | int (0–255) | `200` | Section-header bubble alpha. |
| `sidebarSectionBgColor` | hex | derived | Body-section bubble color. |
| `sidebarSectionBgOpacity` | int (0–255) | `200` | Body-section bubble alpha. |
| `sidebarFontFamily` | string | empty | Sidebar font family. Empty = inherit. |
| `sidebarFontPointSize` | int | `0` | Sidebar font size. `0` = inherit. |
| `sidebarActiveTab` | enum | `item` | Active tab on first show: `item` / `collection` / `file`. |

### Per-collection display behavior

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `customFontFamily` | string | empty | Per-collection font family override. |
| `expandMode` | bool | `false` | Two-stage activation (preview, then launch). |
| `customArtworkTypes` | csv | empty | Free-form custom artwork-type ids. See [Artwork](Artwork.md). |

### Runtime-only fields (not normally written)

These appear in `kartend.cfg` only as session state — don't set them
manually.

| Key | Description |
|-----|-------------|
| `currentSubfolder` | Relative path to the active subfolder when virtual folder navigation is in use. |
| `isPlaylist` | Marker for synthesized playlist collections. |
| `playlistId` | UUID of the underlying playlist row. |
| `playlistReservedKind` | Empty for user playlists, `favorites` for the built-in favorites playlist. |

## Example: a complete media collection

```ini
[General]
rememberSelection=true
attractModeEnabled=true
attractModeIdleTimeoutSec=180
historyEnabled=true
runtimeDetectionEnabled=true

[Films]
name=Films
type=Video
mediaDirectory=~/Videos/Films
artworkDirectory=~/Videos/Films/_covers
videoDirectory=~/Videos/Films/_previews
extensions=mkv,mp4,zip
launcherPath=/usr/bin/mpv
launchParameters=--fullscreen
extractArchives=true
extractedExtension=mkv

gridWidth=8
itemWidth=180
itemHeight=240
horizontalSpacing=12
verticalSpacing=16
backgroundType=image
backgroundImage=~/Pictures/wallpapers/films.jpg
wallpaperParallax=true
parallaxStrength=40
vignetteEnabled=true
vignetteIntensity=60
toolbarBackdropBlur=true
sidebarVisible=true
sidebarMode=overlay
sidebarPosition=right
titleExclusionPatterns=\s*\(USA\),\s*\[!\]
titleExclusionEnabled=true
hideMissingArtwork=true
```

## Where to next

- [Settings Dialog](Settings-Dialog.md) — the GUI equivalent of this
  reference, with field-by-field tour
- [Apply Settings](Settings-Dialog.md#apply-settings) — propagate keys
  across multiple collections
- [File Locations](File-Locations.md) — where this config file lives
- [Backup & Sharing](Backup-and-Sharing.md) — `.kart` packages bundle a
  collection's config + metadata for transfer

## For developers

- The two structs are `GeneralSettings` and `CollectionConfig` in
  [src/utils/app/collectionutils.h](../../src/utils/app/collectionutils.h).
- INI read/write happens in
  [src/modules/data/settings/settingsmanager*.cpp](../../src/modules/data/settings/);
  validation is in
  [src/utils/fs/configvalidation.cpp](../../src/utils/fs/configvalidation.cpp).
- Adding a new key: extend the struct, add load/save in
  `settingsmanager`, add UI in
  [src/ui/dialogs/settings/](../../src/ui/dialogs/settings/), update
  `applysettingsdialog` if the key should be propagatable, and add a
  row to this page.
- Keys that should *not* propagate to other collections (paths,
  extensions, parent linkage, launchers) are gated in
  `applysettingsdialog`.
