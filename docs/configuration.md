# Configuration

Kartend stores its configuration in an INI file at:

```
~/.config/kartend/kartend.cfg
```

You can configure collections either through the **Settings Dialog** (accessed via the UI) or by editing the configuration file directly.

## File Format

The configuration file uses standard INI format with sections for general settings and individual collections.

### General Settings

```ini
[General]
MainScreen_gridWidth=6
rememberSelection=true
```

| Key | Type | Description |
|-----|------|-------------|
| `MainScreen_gridWidth` | int | Default grid width for the main screen |
| `rememberSelection` | bool | Persist selection state across sessions |

### Collection Settings

Each collection is defined as a separate section. The section name becomes the collection name displayed in the UI.

For subcollections (nested under a parent), use ` > ` as the hierarchy separator:

```ini
[Photos]
...

[Photos > Vacation]
parentCollectionIndex=0
...
```

## Collection Properties

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `name` | string | — | Display name for the collection |
| `mediaDirectory` | string | — | Path to media files |
| `artworkDirectory` | string | — | Path to artwork/cover images |
| `launcherPath` | string | — | Path to application executable |
| `corePath` | string | — | Path to loadable core/plugin (optional) |
| `launchParameters` | string | — | Additional launch parameters |
| `extensions` | string | — | Comma-separated file extensions (e.g., `pdf,jpg,mp4`) |
| `collectionIcon` | string | — | Path to collection icon image |
| `gridWidth` | int | `4` | Number of items per row |
| `parentCollectionIndex` | int | `-1` | Index of parent collection (-1 = top-level) |

### Appearance

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `itemWidth` | int | `200` | Width of item tiles in pixels |
| `itemHeight` | int | `200` | Height of item tiles in pixels |
| `fontSize` | int | `12` | Font size for item titles |
| `cornerRadius` | int | `8` | Border radius for item tiles |
| `horizontalSpacing` | int | `20` | Horizontal gap between items |
| `verticalSpacing` | int | `20` | Vertical gap between rows |
| `horizontalAlignment` | string | `center` | Grid alignment: `left`, `center`, `right` |
| `hideTitles` | bool | `false` | Hide item titles |
| `hideSubcollectionTitles` | bool | `false` | Hide titles on subcollection items |

### Colors

| Key | Type | Description |
|-----|------|-------------|
| `backgroundType` | string | `color` or `image` |
| `backgroundColor` | string | Background color (hex, e.g., `#1a1a2e`) |
| `backgroundImage` | string | Path to background image |
| `primaryColor` | string | Primary UI color for toolbar/menubar |
| `tileColor` | string | Color for item tiles/placeholders |
| `selectionColor` | string | Color for selection rectangle |

### Sidebar

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `sidebarVisible` | bool | `false` | Show metadata sidebar by default |
| `sidebarMode` | string | `overlay` | `overlay` or `fixed` |

### Scrollbars

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `hideHorizontalScrollbar` | bool | `false` | Hide horizontal scrollbar |
| `hideVerticalScrollbar` | bool | `false` | Hide vertical scrollbar |

### Folder Browsing

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `includeContentSubfolders` | bool | `false` | Show subfolders as navigable virtual folders |
| `includeArtworkSubfolders` | bool | `false` | Match artwork from subfolders |
| `showAllSubfolderItems` | bool | `false` | Mix subfolder items with parent |
| `hideSubfolderTitles` | bool | `false` | Hide titles on virtual folder widgets |
| `showHiddenFolders` | bool | `false` | Show hidden folders (starting with dot) |

### Subcollection Behavior

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `showAllSubcollectionItems` | bool | `false` | Show items from all child collections |

## Example Configuration

Copy and paste this template to create a new collection:

```ini
[My Collection]
name=My Collection
mediaDirectory=/home/user/Media/MyCollection
artworkDirectory=/home/user/Artwork/MyCollection
launcherPath=/usr/bin/xdg-open
corePath=
launchParameters=
extensions=pdf,jpg,png,mp4
gridWidth=6
itemWidth=200
itemHeight=280
fontSize=11
cornerRadius=8
horizontalSpacing=20
verticalSpacing=24
horizontalAlignment=center
hideTitles=false
sidebarVisible=false
sidebarMode=overlay
backgroundType=color
backgroundColor=#1a1a2e
primaryColor=#2d2d44
tileColor=#3d3d5c
selectionColor=#6366f1
```

## Subcollection Example

To create a subcollection under an existing parent:

```ini
[Documents]
name=Documents
mediaDirectory=
artworkDirectory=
gridWidth=4
collectionIcon=/home/user/icons/documents.png

[Documents > Reports]
name=Reports
parentCollectionIndex=0
mediaDirectory=/home/user/Documents/Reports
artworkDirectory=/home/user/Thumbnails/Reports
launcherPath=/usr/bin/xdg-open
corePath=
extensions=pdf,docx,xlsx
gridWidth=6

[Documents > Presentations]
name=Presentations
parentCollectionIndex=0
mediaDirectory=/home/user/Documents/Presentations
artworkDirectory=/home/user/Thumbnails/Presentations
launcherPath=/usr/bin/xdg-open
corePath=
extensions=pptx,pdf,odp
gridWidth=6
```

## Path Variables

Paths support the `~` shorthand for the home directory:

```ini
mediaDirectory=~/Documents/Reports
artworkDirectory=~/Thumbnails/Reports
```

## Applying Changes

After editing the configuration file manually:

1. **Restart Kartend** to reload the configuration
2. Or use **Settings → Reload Configuration** from the menu (if available)

Changes made through the Settings Dialog are saved immediately.
