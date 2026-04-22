# Usage Guide

A first-run walkthrough for new users. For exhaustive option reference, see
[configuration.md](configuration.md).

## 1. First Launch

After installing (see [building.md](building.md)) launch Kartend from your
application menu or run `kartend` in a terminal.

On first launch you'll see an empty main screen. Kartend stores all per-user
state under:

```
~/.config/kartend/kartend.cfg
```

This file is created automatically the first time you save settings.

## 2. Add Your First Collection

A *collection* is a folder of media files paired with a folder of cover
artwork and a launcher application.

1. Open the **Settings Dialog** with `Ctrl + ,` (or **File → Settings**).
2. Click **Add Collection**.
3. Fill in the fields:
   - **Name** — display label (e.g. `Movies`, `Documents`, `SNES`).
   - **Media Directory** — folder containing the files to display.
   - **Artwork Directory** — folder containing cover images named to match
     the media files (e.g. `cool-game.sfc` ↔ `cool-game.png`).
   - **Launcher Path** — the executable that opens an item (e.g.
     `/usr/bin/xdg-open`, `/usr/bin/mpv`, `/usr/bin/retroarch`).
   - **Extensions** — comma-separated list of file extensions to scan
     (e.g. `pdf,epub` or `sfc,smc`). Leave blank to accept all files.
4. Click **OK**. Kartend will scan the media directory and populate the grid.

Paths support `~` for your home directory:

```
mediaDirectory=~/Documents/Reports
```

## 3. Browse and Launch

| Key                         | Action                                  |
|-----------------------------|-----------------------------------------|
| `←` `→` `↑` `↓`             | Move selection                          |
| `Enter`                     | Launch / enter subcollection            |
| `Escape`                    | Back / close overlay                    |
| `Home` / `End`              | First / last item                       |
| `Page Up` / `Page Down`     | Jump to previous/next letter            |
| `/`                         | Focus search bar                        |
| `Ctrl + +` / `Ctrl + -`     | Adjust grid width                       |
| `F1`                        | In-app shortcut reference               |
| `F11`                       | Toggle fullscreen                       |

The full default key map lives in the [readme](../readme.md#keyboard-shortcuts)
and is rebindable under **Settings → General**.

## 4. Add Cover Artwork

Kartend matches artwork by **base filename**. For a media file
`my-movie.mkv`, place a matching image (any of `.png`, `.jpg`, `.jpeg`,
`.webp`) named `my-movie.png` in the artwork directory.

If no matching artwork is found, Kartend renders a colored placeholder tile
showing the filename — items remain launchable.

To generate placeholder artwork for nested folders, see
[subfolder-artwork.md](subfolder-artwork.md).

## 5. Organize with Subcollections

Collections can be nested. From the Settings Dialog, set a collection's
**Parent Collection** to make it appear as a sub-grid inside its parent. In
the config file this is `parentCollectionIndex`:

```ini
[Documents]
mediaDirectory=
gridWidth=4

[Documents > Reports]
parentCollectionIndex=0
mediaDirectory=~/Documents/Reports
launcherPath=/usr/bin/xdg-open
extensions=pdf,docx
```

Pressing `Enter` on a subcollection tile opens it; `Escape` returns to the
parent.

## 6. Tune the Look

Per-collection appearance is controlled by tile size, spacing, fonts, and
colors — all editable from the Settings Dialog or directly in the config
file. See [configuration.md](configuration.md#appearance) for the complete
list.

## 7. Persistent Selection

Kartend remembers which item was last selected in each collection. Disable
this with `rememberSelection=false` under the `[General]` section if you
prefer to always start at the first item.

## Where to Go Next

- [configuration.md](configuration.md) — every config key, with defaults
- [troubleshooting.md](troubleshooting.md) — fixes for common issues
- [building.md](building.md) — building from source
- [architecture.md](architecture.md) — how Kartend is structured
