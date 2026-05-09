# Themes & Appearance

Kartend has no concept of "themes" as installable bundles — instead,
every visual element is a per-collection or global setting. This page
covers the visual surface: backgrounds (solid / image / video),
vignette, parallax, blur, fonts, text zoom, title tints, and the
header logo overlay.

For sidebar-specific styling see [Sidebar & Details Pane](Sidebar-and-Details-Pane.md).
For per-tile / per-item-grid layout see [View Modes](View-Modes.md).

> **Where to find this** — Settings Dialog → **Colors** and **Text &
> Fonts** tabs (per-collection), **General** tab (global). All keys
> documented in [Configuration Reference](Configuration-Reference.md).

## Color palette

Every collection has a small palette that drives most of the chrome:

| Setting | INI key | Used for |
|---------|---------|----------|
| Primary color | `primaryColor` | Toolbar / menu / chrome accents |
| Tile color | `tileColor` | Item placeholders, missing-artwork tiles |
| Selection color | `selectionColor` | Selection rectangle, focus indicators |

These three colors propagate to derivative UI elements (sidebar bubbles
inherit, button hover states inherit, etc.) unless overridden by a
more-specific setting.

> **Tip** — start with these three and only override individual
> sub-settings if the derived defaults don't suit you.

## Backgrounds

### Solid color

```ini
backgroundType=color
backgroundColor=#1a1a2e
```

The fastest path. Any hex color works.

### Image wallpaper

```ini
backgroundType=image
backgroundImage=~/themes/films-wallpaper.jpg
wallpaperParallax=true
parallaxStrength=40
```

Images are scaled to cover the viewport (preserving aspect ratio,
cropped if needed). Performance is excellent — the image is decoded
once on collection open and held as a QPixmap.

#### Parallax

| Setting | INI key | Effect |
|---------|---------|--------|
| Wallpaper Parallax | `wallpaperParallax` | Boolean toggle |
| Parallax Strength | `parallaxStrength` | 0–100; 0 = no parallax, 100 = matches scroll speed |

When enabled, scrolling the items grid moves the background at a
percentage of the scroll speed (default 30%). Gives a sense of depth.

Parallax has no effect when the background type is `color` or `video`.

### Video wallpaper

```ini
backgroundType=video
backgroundVideo=~/themes/synthwave-loop.mp4
```

Looping muted video as the background. Plays continuously — there's no
"play on idle" mode. Performance depends on Qt Multimedia codec
availability for the file format; `.mp4`, `.webm`, `.avi` typically
work out of the box.

> **Caveat** — video backgrounds are GPU-driven and add per-frame
> overhead. On low-end hardware prefer image wallpapers. Parallax does
> *not* apply to video backgrounds.

## Vignette

A radial darkening overlay on the viewport edges. Subtle by default,
useful for focusing the eye on the center.

| Setting | INI key | Effect |
|---------|---------|--------|
| Vignette Enabled | `vignetteEnabled` | Boolean |
| Vignette Intensity | `vignetteIntensity` | 0–100 (0 = invisible, 100 = pitch-black corners) |

Compatible with all background types. A value around 30–60 looks
natural without being distracting.

## Backdrop blur

The toolbar background can be blurred over the items grid (akin to
macOS Vibrancy / Windows Acrylic).

| Setting | INI key | Effect |
|---------|---------|--------|
| Toolbar Backdrop Blur | `toolbarBackdropBlur` | Boolean |
| Backdrop Blur Radius | `backdropBlurRadius` | Pixels (default `12`) |

Performance: a cheap downscale-upscale blur. No effect on video
backgrounds (the GPU shader path isn't compatible). Looks best with
image wallpapers.

## Header logo

A per-collection branding logo painted across the top of the items
viewport, *above* the grid items but below the toolbar:

| Setting | INI key | Effect |
|---------|---------|--------|
| Header Logo Image | `headerLogoImage` | Path to PNG / JPG / WEBP / SVG |
| Header Logo Position | `headerLogoPosition` | `topleft` / `topcenter` / `topright` |

Distinct from:

- **`collectionIcon`** — the small icon shown on this collection's
  *tile* when it appears as a subcollection of another.
- The platform application icon (`io.github.EtherAura.Kartend.svg`) —
  the global app icon shown in your application menu and window
  decorations.

Use the header logo for collection-level branding (e.g. a "Films" logo
across the top of the Films collection).

## Tile titles

The text rendered under each item tile. Three knobs:

| Setting | INI key | Notes |
|---------|---------|-------|
| Title base color | `titleBaseColor` | Hex; empty = use `selectionColor` |
| Title tint saturation | `titleTintSaturation` | 0–255 (HSV) |
| Title tint lightness | `titleTintLightness` | 0–255 (HSV) |
| Show title in placeholder | `showTitleInPlaceholder` | Overlay title text on placeholder tiles |
| Hide titles | `hideTitles` (per-coll.) | Hide all tile titles |
| Hide subcollection titles | `hideSubcollectionTitles` (per-coll.) | Hide titles only on subcollection tiles |
| Hide subfolder titles | `hideSubfolderTitles` (per-coll.) | Hide titles on virtual-folder tiles |

Tints are applied via HSV adjustment to the base color — you can leave
`titleBaseColor` empty and tune saturation/lightness instead.

## Fonts

Fonts work at three levels: global, per-collection, sidebar.

### Global

| Setting | INI key | Notes |
|---------|---------|-------|
| Global UI font family | `globalUiFontFamily` | Empty = platform default |
| Global UI font size | `globalUiFontPointSize` | `0` = platform default |

Affects the entire UI: toolbar, menus, dialogs, buttons.
**Restart required** to take effect.

### Per-collection

| Setting | INI key | Notes |
|---------|---------|-------|
| Custom font family | `customFontFamily` | Empty = inherit global |

Only affects items grid text (tile titles, list rows). Lets you have
a serif typeface in your books collection and a monospace one in your
tools collection without restart.

### Sidebar

See [Sidebar typography](Sidebar-and-Details-Pane.md#typography) —
overrides global / per-collection fonts for the sidebar specifically.

### Text zoom

A global zoom factor that scales text everywhere (toolbar, menus,
sidebar, tile titles, dialog labels). Independent of font size.

| Action | INI key | Effect |
|--------|---------|--------|
| `Ctrl+=` | `uiTextZoomPercent` | +10% |
| `Ctrl+-` | `uiTextZoomPercent` | -10% |
| `Ctrl+0` | `uiTextZoomPercent` | Reset to 100% |

A small HUD shows the percentage briefly after each adjustment. Range
is 50%–300%. Persisted across sessions.

## Splash & startup video

Splash overlays sit at the boundary between "appearance" and
"behavior." See [Splash & Now Playing](Splash-and-Now-Playing.md) for
the full surface — short version:

| Splash | Toggle | Notes |
|--------|--------|-------|
| Boot splash | `bootSplashEnabled` | On launch |
| Resume-focus splash | `resumeFocusSplashEnabled` | When window regains focus after launching an item |
| Startup video | `startupVideoEnabled` + `startupVideoPath` | One-time intro video on launch (skippable) |

## Recipes

### Dark cinematic look

```ini
[Movies]
backgroundType=image
backgroundImage=~/themes/cinema-dark.jpg
wallpaperParallax=true
parallaxStrength=30
vignetteEnabled=true
vignetteIntensity=70
toolbarBackdropBlur=true
backdropBlurRadius=20
primaryColor=#0e0e10
tileColor=#202024
selectionColor=#e94560
sidebarVisible=true
sidebarMode=overlay
sidebarBackgroundColor=#0e0e10
sidebarHeaderBgOpacity=180
```

### High-contrast / accessibility

```ini
[General]
uiTextZoomPercent=130
globalUiFontFamily=Atkinson Hyperlegible
globalUiFontPointSize=12

[Documents]
backgroundType=color
backgroundColor=#000000
primaryColor=#ffffff
tileColor=#1a1a1a
selectionColor=#ffd700
titleBaseColor=#ffffff
titleTintSaturation=0
titleTintLightness=255
hideMissingArtwork=false
showTitleInPlaceholder=true
```

### Synthwave video background

```ini
[Synthwave]
backgroundType=video
backgroundVideo=~/themes/synthwave-grid.mp4
vignetteEnabled=true
vignetteIntensity=40
primaryColor=#ff006e
tileColor=#1d0f3a
selectionColor=#00ffff
toolbarBackdropBlur=false
```

### Per-collection font swap

```ini
[General]
globalUiFontFamily=Inter
globalUiFontPointSize=10

[Books]
customFontFamily=Crimson Pro

[Tools]
customFontFamily=JetBrains Mono
```

The toolbar / menus stay Inter; the books grid uses Crimson Pro and
the tools grid uses JetBrains Mono.

## Where to next

- [Sidebar & Details Pane](Sidebar-and-Details-Pane.md) — sidebar-specific
  styling
- [View Modes](View-Modes.md) — tile sizing and grid layout
- [Splash & Now Playing](Splash-and-Now-Playing.md) — startup video
  and splash overlays
- [Configuration Reference](Configuration-Reference.md#background--visual-effects)
  — every appearance INI key

## For developers

- Background painting: `MainWindow::paintEvent` and `BackgroundManager`
  helpers; image and color paths are different code paths from the
  video path (which uses a `QVideoSink`).
- Vignette / parallax / backdrop blur: composited in
  `MainWindow::paintEvent`. Backdrop blur uses a downscaled copy of
  the background pixmap with a fast box blur (no shader).
- Font cascade: `Qt::ApplicationFontFamily` is set globally on app
  startup from `globalUiFontFamily`; per-collection custom fonts apply
  on the items widget. Sidebar fonts apply via stylesheet.
- Text zoom: `Qt::AA_DisableHighDpiScaling` interacts oddly with text
  zoom; the implementation walks all widgets and adjusts font point
  sizes by the zoom percentage. See `MainWindow::applyTextZoom`.
- Adding a new visual effect: add a `[General]` or per-collection key,
  read it in the relevant manager, hook into `paintEvent` (for static
  effects) or the per-frame composition path (for animated ones).
