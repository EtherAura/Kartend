# Getting Started

A first-run walkthrough. After this you'll have one collection on screen,
artwork loading, and items launching. Once that works, jump to whichever
feature page you need next from the [Home index](Home.md).

For exhaustive option reference see
[Configuration Reference](Configuration-Reference.md). For build / install
instructions see the [project readme](../../readme.md) and
[building.md](../building.md).

## What is a collection?

A **collection** is the unit Kartend organizes around. Every collection
has, at minimum:

- a **name** (the label shown in the UI)
- usually a **media directory** (the folder of items to display) plus a
  **launcher** that opens an item (`xdg-open`, `mpv`, `retroarch`, your
  own shell script…) — but neither is strictly required: a
  [shell collection](Shell-Collections.md) with no media of its own can
  exist purely to group other collections under a named category

Optionally a collection also has an **artwork directory**, a **video
directory** for previews, and any number of per-collection appearance
and launcher overrides. All of those are covered on the feature pages
linked from [Home](Home.md).

## 1. First launch

After installing (see [building.md](../building.md)) launch Kartend from
your application menu, or run `kartend` from a terminal.

### The First-Run Wizard

The first launch opens a three-page setup wizard. Cancel it and
you'll land on the empty main window described below; complete it and
you'll start with one collection already configured.

| Page | What it asks for |
|------|------------------|
| **Welcome** | Nothing — just an intro to what Kartend is. **Next**. |
| **Pick a media folder** | A **Collection name** (e.g. *Videos*, *Audiobooks*, *Reference*) and a **Media folder** path. Use the **Browse…** button to pick a directory; the folder is scanned recursively (subfolders become subcollections). |
| **All set** | A confirmation page. **Finish** creates the collection, kicks off the initial scan, and writes `~/.config/kartend/kartend.cfg`. |

The wizard sets `firstRunComplete=true` in `[General]` after it
finishes *or* is cancelled, so it won't auto-launch again. To re-run
it later, choose **Help → Setup Wizard…** from the menu; the
`firstRunComplete` flag stays set (the auto-launch is intentionally
one-shot).

### Without the wizard

If you skipped or already completed the wizard, the main window is
empty and the **Empty State** widget points you toward Settings.
Kartend hasn't written any persistent state yet; that happens when
you save your first collection (next section, or section 2 below if
you skipped the wizard).

Per-user state lives at:

| Path | Purpose |
|------|---------|
| `~/.config/kartend/kartend.cfg` | Settings (INI) — global + per-collection |
| `~/.local/share/kartend/kartend.db` | SQLite database — items, metadata, history |
| `~/.cache/kartend/` | Pixmap cache, extracted-archive temp files, session state |

See [File Locations](File-Locations.md) for the full layout.

## 2. Add your first collection

1. Open the **Settings Dialog** with `Ctrl + ,` (or **File → Settings**).
2. Click **Add Collection** in the tree on the left. The **Add
   Collection** dialog asks for:
   - **Name** — display label (e.g. `Films`, `Albums`, `Manuals`). The
     only required field.
   - **Content Folder** — the folder containing the files to display.
     Paths support `~` for your home directory.
   - **Artwork Folder** — folder containing cover images that match the
     media filenames (more below).
   - **Launcher** — the executable that opens an item (e.g.
     `/usr/bin/mpv`, `/usr/bin/xdg-open`).
   - **Core** *(RetroArch only)* — appears when the launcher is
     RetroArch; the libretro core to load.
   - **Media Type** — a category preset (Video, Audio, Images,
     Documents, Games) or a custom value. Drives the type filter and
     the suggested scraper.
   - **Scraper** — the metadata provider. It follows the media type
     automatically; change it only to pin a different provider, which
     is mainly useful for custom media types.
   - **ScreenScraper System** *(games only)* — appears when the media
     type is a game category; overrides ScreenScraper.fr's per-system
     auto-detection. Leave on Auto-detect unless it picks wrong.

   Everything except the name can be left blank and adjusted later.
3. Switch to the **Paths & Extensions** tab to set **Extensions** — a
   comma-separated list of file extensions to scan (e.g. `pdf,epub` or
   `mkv,mp4,webm`). Leave blank to accept every file in the directory.
   The content and artwork folders you entered in the dialog already
   appear here.
4. Switch to the **Launcher** tab. The launcher and core from the
   dialog are already filled in; here you can also set **Launch
   Parameters** *(optional — extra arguments passed before the file
   path, e.g. `--fs` for fullscreen)*. See [Launchers](Launchers.md)
   for launcher-specific examples, presets, and additional launchers.
5. Click **Save**. Kartend scans the media directory and populates the
   grid.

> **Where to find this** — Settings Dialog → **Add Collection** dialog,
> then tabs **Paths & Extensions** and **Launcher**. Underlying INI
> keys: `name`, `type`, `scraperProviderId`, `mediaDirectory`,
> `artworkDirectory`, `extensions`, `launcherPath`, `launchParameters`,
> `corePath`. See
> [Configuration Reference](Configuration-Reference.md) for every key.

### Minimal example (config file)

If you'd rather edit the file directly, this is the smallest valid
configuration:

```ini
[General]
rememberSelection=true

[Movies]
name=Movies
mediaDirectory=~/Videos/Films
artworkDirectory=~/Videos/Films/_covers
launcherPath=/usr/bin/mpv
extensions=mkv,mp4,webm
```

Restart Kartend (or use **Settings → Reload** if available) to pick up
manual edits.

## 3. Browse and launch

Use the keyboard, mouse, or a gamepad — they all work concurrently and
all are configurable.

| Key                     | Action                                        |
|-------------------------|-----------------------------------------------|
| `←` `→` `↑` `↓`         | Move selection                                |
| `Enter`                 | Launch / enter subcollection                  |
| `Escape`                | Back / close overlay                          |
| `Home` / `End`          | Jump to first / last item                     |
| `Page Up` / `Page Down` | Alphabetic jump (previous / next letter)      |
| `/`                     | Focus the search bar                          |
| `Ctrl + +` / `Ctrl + -` | Increase / decrease grid columns              |
| `F1`                    | Show in-app keyboard shortcut reference       |
| `F11`                   | Toggle fullscreen                             |

The full shortcut list lives in [Input & Controls](Input-and-Controls.md)
and is rebindable under **Settings → General**.

Mouse defaults: single-click selects, double-click launches, middle-click
toggles a video preview in the sidebar, right-click opens a context menu.
See [Input & Controls](Input-and-Controls.md#mouse).

## 4. Add cover artwork

Kartend matches artwork to items by **base filename**. For a media file
`my-movie.mkv`, place a matching image — any of `.png`, `.jpg`, `.jpeg`,
`.webp` — in the artwork directory:

```
~/Videos/Films/Some Movie (2021).mkv          ← media
~/Videos/Films/_covers/Some Movie (2021).jpg  ← artwork
```

Items with no matching artwork render as a placeholder tile. Placeholders
remain selectable and launchable; they're just visually distinct. You
can:

- supply a **Placeholder Artwork** image (per-collection) used in place
  of the procedural hatch pattern, or
- enable **Show Title in Placeholder** under **Settings → Text & Fonts**
  to overlay the item filename on placeholder tiles.

For more ways to manage artwork — extra types like `boxfront` /
`screenshot` / `marquee`, manual per-item links, the sidebar gallery —
see [Artwork](Artwork.md).

## 5. Organize with subcollections

Collections can nest. From the Settings Dialog, set a collection's
**Parent Collection** to make it appear as a sub-grid tile inside its
parent. The corresponding INI snippet:

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
```

Pressing `Enter` on a subcollection tile opens it; `Escape` returns to
the parent. Drag-and-drop in the Settings tree reparents collections
visually.

A collection can also have **Linked Parents** — alias references that
make it appear under multiple parents simultaneously without copying it.
See [Collections](Collections.md#linked-parents).

### A tile-grid home across all your collections

By default, Kartend always opens *some* collection on launch — there is
no built-in landing page that shows every root collection as a tile
grid. Two ways to get one:

- **Built-in home view** — set **Settings → General → Use home view**
  (INI: `[General] useHomeView=true`). Kartend boots into a synthetic
  tile grid containing one tile per root collection. `Enter` opens a
  collection; `Escape` from any root-level collection returns to the
  home view.
- **Wrapper shell collection** — create a single shell at the root
  named e.g. `Library` and put every category under it. Set it as your
  startup collection (`[General] startupCollection=Library`). The
  wrapper *becomes* the home view. See
  [Shell Collections](Shell-Collections.md) for layout examples.

Both produce a tile grid you can navigate into and `Back` out of. The
built-in home view requires no config changes to your collections; the
wrapper shell gives you per-collection appearance overrides on the home
page itself (background, header logo, layout knobs).

## 6. Tune the look

Per-collection appearance — tile size, spacing, fonts, colors,
backgrounds — is editable from the Settings Dialog or directly in the
config file. The defaults are deliberately neutral; once you're happy
with one collection's look you can copy it to others using the
[Apply Settings](Settings-Dialog.md#apply-settings) workflow.

Highlights:

- **Backgrounds** — solid color, image wallpaper, or looping video.
  See [Themes & Appearance](Themes-and-Appearance.md).
- **Sidebar** — toggle with `F9`; choose **Overlay** (floats over the
  grid) or **Expand** (docks and shrinks the grid). See
  [Sidebar & Details Pane](Sidebar-and-Details-Pane.md).
- **View modes** — Grid (default), List, Cover Flow, Horizontal. Switch
  with `Ctrl+1`/`Ctrl+2`/`Ctrl+3`/`Ctrl+4` or the toolbar's view
  dropdown. See [View Modes](View-Modes.md).

## 7. Persistent selection and resume

Kartend remembers which item was last selected in each collection
(`rememberSelection=true` under `[General]`, default). When you re-open
or switch back to a collection, the last selected item is re-focused.
Disable this if you'd rather always start at the first item.

If **Resume Focus Splash** is enabled, returning to Kartend after
launching an item briefly displays a splash before the grid re-appears.
See [Splash Screens & Now Playing](Splash-and-Now-Playing.md).

## 8. Common next steps

Once you have one collection working, branching out is usually one of:

- **More launchers** — add additional launchers per collection (e.g.
  mpv *and* VLC for the same library), build reusable launcher presets,
  or override on a per-item basis. See
  [Launchers](Launchers.md).
- **More artwork** — add `boxfront`, `screenshot`, `marquee` artwork
  types and browse them in the sidebar gallery. See
  [Artwork](Artwork.md).
- **Playlists & Favorites** — build curated lists across collections.
  See [Playlists & Favorites](Playlists-and-Favorites.md).
- **Statistics** — track play counts, last-played dates, total time
  played. See [History & Statistics](History-and-Statistics.md).
- **Attract mode** — kiosk-style idle behavior. See
  [Attract Mode](Attract-Mode.md).
- **Theming** — colors, vignette, parallax, backdrop blur, fonts. See
  [Themes & Appearance](Themes-and-Appearance.md).

## Where to go next

- [Configuration Reference](Configuration-Reference.md) — every config
  key, with defaults
- [Troubleshooting](Troubleshooting.md) — fixes for common issues
- [Input & Controls](Input-and-Controls.md) — full keyboard / mouse /
  gamepad reference and how to rebind
- [Settings Dialog](Settings-Dialog.md) — anatomy of every tab
- [building.md](../building.md) — building from source
- [architecture.md](../architecture.md) — how Kartend is structured
  internally
