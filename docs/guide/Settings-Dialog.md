# Settings Dialog

The Settings Dialog is the front door to nearly every per-collection
and global option Kartend supports. Open with `Ctrl+,` or **File →
Settings**.

This page is a tour of the dialog's structure: what each tab covers,
how the **scope selector** propagates changes across collections, the
**Apply Settings** workflow for selectively copying fields, and how to
duplicate / reparent / delete collections from the tree.

For a flat list of every config key see
[Configuration Reference](Configuration-Reference.md). Most field-level
descriptions repeat there; this page focuses on the *workflow*.

## Anatomy

```
┌────────────────────────────────────────────────────────────────┐
│ Scope:  Current ▼     [Save *]  Revert  Cancel                 │
├──────────┬─────────────────────────────────────────────────────┤
│  ┌────┐  │  Basic | Paths | Launcher | Appearance | Colors |   │
│  │tree│  │  Sidebar | List View | Text & Fonts | General       │
│  │    │  │ ──────────────────────────────────────────────────  │
│  └────┘  │                                                     │
│ + Add    │  (tab content)                                      │
└──────────┴─────────────────────────────────────────────────────┘
```

- **Top bar**: Scope selector + Save / Revert / Cancel buttons.
- **Left pane**: Collection tree with add / context menu.
- **Right pane**: Tabbed editor for the selected collection (and
  global settings under the **General** tab).

The **Save** button pulses with a drop-shadow glow while there are
unsaved changes — an at-a-glance reminder. Clicking it commits all
changes in the current scope; **Revert** discards them; **Cancel**
closes the dialog without saving (with a confirm prompt if there are
unsaved edits).

## Collection tree

Lists every collection (and subcollection) hierarchically. From here
you can:

- **Add** — toolbar `+` button. Creates a new top-level collection.
- **Reparent** — drag and drop a collection onto another.
- **Rename** — double-click the name on the **Basic** tab (or the
  context menu **Rename**).
- **Duplicate** — right-click → **Duplicate** opens a dialog asking
  for the new name and destination parent.
- **Delete** — right-click → **Delete**, with a confirm prompt.
- **Expand / Collapse** — context menu shortcuts to fully
  expand / collapse a subtree.

Selection in the tree drives the right pane: the tab editor reflects
the highlighted collection. Multi-select is not supported — use
**Apply To Selected** for batch operations.

## Tabs (per-collection)

### Basic

| Field | Notes |
|-------|-------|
| **Name** | Display name. Renaming updates references everywhere. |
| **Type** | Free-form classification used by the [type filter](Search-Sort-Filter.md#type-filter). |
| **Parent Collection** | Dropdown of all candidate parents (excluding the current collection and its descendants). |
| **Linked Parents** | Multi-select picker for [alias parents](Collections.md#linked-parents). |

### Paths & Extensions

| Field | Notes |
|-------|-------|
| **Media Directory** | Path browser. Empty = parent-only collection. |
| **Artwork Directory** | Path browser. See [Artwork](Artwork.md). |
| **Video Directory** | Path browser. See [Video Previews](Video-Previews.md). |
| **Manual Directory** | Path browser. Auto-discovered manuals show in the sidebar. |
| **Placeholder Artwork** | File browser. Custom missing-art image. |
| **Collection Icon** | File browser. Image used for this collection's tile when it's a subcollection. |
| **Header Logo** | File + position dropdown (topleft / topcenter / topright). |
| **Extensions** | CSV. Empty = accept all files. |
| **Include Content Subfolders** | Show subfolders as virtual tiles. |
| **Include Artwork Subfolders** | Recurse into artwork subfolders. |
| **Show All Subfolder Items** | Mix subfolder items into the parent's grid. |
| **Show Hidden Folders** | Include `.dot-prefixed` directories. |
| **Custom Artwork Types** | CSV of free-form custom type ids. |
| **Extract Archives** | Auto-extract archive items. |
| **Extracted Extension** | Which extension to launch from inside an archive. |

### Launcher

See [Launchers](Launchers.md) for the full launcher model.

| Section | Notes |
|---------|-------|
| **Primary Launcher** | Path / Name / Core / Parameters fields. |
| **Additional Launchers** | List with Add / Edit / Remove buttons. |
| **Default Launcher** | Dropdown selecting the pre-selected launcher. |
| **Global Launcher Presets** | Reusable presets (lives in `[General]` but managed here). |

### Appearance

| Section | Notes |
|---------|-------|
| **View Type** | `grid` / `list` / `coverflow` / `horizontal`. See [View Modes](View-Modes.md). |
| **Grid sizing** | `gridWidth`, alternate grid widths for hidden sidebar, horizontal grid height. |
| **Tile sizing** | `itemWidth`, `itemHeight`, `fontSize`, `cornerRadius`, spacing. |
| **Alignment** | `left` / `center` / `right`. |
| **Hide Titles / Hide Subcollection Titles** | |
| **Hide Horizontal / Vertical Scrollbar** | |
| **Hide Missing Artwork** | |
| **Title Exclusion Patterns** | Regex patterns stripped from displayed titles, with on/off toggle. |

### Colors

| Section | Notes |
|---------|-------|
| **Background Type** | `color` / `image` / `video`. |
| **Background Color / Image / Video** | Tie-ins to the chosen type. |
| **Primary / Tile / Selection Colors** | Hex pickers. |
| **Vignette** | Enable + intensity slider. |
| **Wallpaper Parallax** | Enable + strength slider. |
| **Toolbar Backdrop Blur** | Enable + radius. |

### Sidebar

See [Sidebar & Details Pane](Sidebar-and-Details-Pane.md).

Field groups:

- **Visibility & Layout** — visible, mode (overlay / expand), position
  (right / left / top / bottom), width / height, locked-resize.
- **Background** — color / image / pattern + crosshatch intensity.
- **Bubbles** — header & section bubble colors with per-bubble opacity.
- **Typography** — text color, accent color, font family, font size.
- **Active Tab** — initial tab on first show.

### List View

| Field | Notes |
|-------|-------|
| **List Font Size** | Row text size. |
| **List Row Height** | Pixels. |
| **List Row Color / Alternate Row Color** | Stripe colors. |

Column widths (`listCollectionColumnWidth`, `listArtworkColumnWidth`)
are global and apply to the List view across all collections, but
they have no Settings Dialog control today — hand-edit `kartend.cfg`
in `[General]`.

### Text & Fonts

| Field | Notes |
|-------|-------|
| **Custom Font Family** | Per-collection font override. |
| **Title Tint Saturation / Lightness** | Tile-title HSV adjustments. |
| **Title Base Color** | Hex; empty = use selection color. |
| **Show Title in Placeholder** | Global. |

## The General tab (global settings)

The **General** tab applies to the entire app, not the selected
collection. It groups:

- **Selection & Navigation** — remember selection, wrap, hover-select.
- **Performance** — pixmap cache, scroll animation duration, scroll
  velocity multiplier.
- **Keyboard** — key bindings (rebindable), repeat intervals, hold
  delays.
- **Mouse** — wheel rows, artwork-cycle modifier.
- **Gamepad** — D-pad / left-stick toggles, button bindings (live
  capture).
- **Sorting** — sort mode, exclude-subfolders.
- **Filtering** — collection type filter. (`hideSubcollectionTiles`
  has no UI control today; hand-edit `[General] hideSubcollectionTiles`
  in `kartend.cfg`.)
- **View toggles** — show menu bar, show toolbar, fullscreen
  (persistent).
- **Toolbar customization** — visibility & label per item-page toolbar
  control.
- **Typography** — global font family / size, UI text zoom percent,
  title tints.
- **Splash screens** — boot, resume-focus, startup video.
- **Preview video volume** — global.
- **Runtime detection** — toggle.
- **Launch history** — enabled, max entries.
- **Attract mode** — enabled, idle timeout, autoscroll, advance
  selection. See [Attract Mode](Attract-Mode.md).
- **Startup collection** — name of the collection opened on launch.

Each section is a fold-down so the tab stays scannable.

> **Config-only globals** — a few `[General]` keys have no Settings
> Dialog control and must be edited in `kartend.cfg` by hand:
> `hideSubcollectionTiles`, `listCollectionColumnWidth`,
> `listArtworkColumnWidth`. See
> [Configuration Reference](Configuration-Reference.md) for full
> descriptions.

## Scope selector

The **Scope** dropdown (top bar) determines what saves apply to:

| Scope | Effect |
|-------|--------|
| **Current** | Save changes only to the selected collection. |
| **Current and Subcollections** | Save propagates to the selected collection and all of its descendants. |
| **All** | Save propagates to every collection. |

When scope is wider than `Current`, fields that **cannot** propagate
sensibly (paths, extensions, parent linkage, launcher path) are grayed
out — you can't accidentally overwrite all collections' media
directories with one. The grayed list is determined by a category
allow-list inside the dialog.

Save with a wider scope is a one-shot: the next time you open the
dialog the scope resets to **Current**.

> **Watch out** — propagating "All" is fast and *not* reversible from
> within Kartend. Use it intentionally, especially for appearance
> sweeps. The [Apply Settings](#apply-settings) workflow is safer for
> targeted batch updates.

## Apply Settings

For finer control than the global scope selector, the **Apply To
Selected** workflow lets you copy specific *categories* of fields from
the current collection to a chosen subset of others.

Open via the action button in the dialog header (or the Settings →
Apply To… menu in some builds). The Apply Settings Dialog appears:

1. **Source** — the collection currently selected in the tree.
2. **Categories** — a checkbox grid: Basic, Paths, Appearance, Colors,
   Sidebar, List View, Text & Fonts. Pick which categories to copy.
3. **Targets** — a tree of collections with multi-select; pick which
   ones to receive the copied fields.
4. **Mode** — Pull (overwrite targets with source values) or Propagate
   (a sub-mode that respects per-collection differences for some
   fields). The dialog explains the difference inline.
5. **OK** — applies and closes; **Cancel** discards.

Fields that can't be copied (paths, parent linkage) are excluded
regardless of the category checkboxes.

Workflow tip: use this when you've spent an hour tuning one
collection's appearance and want every other collection to match.

## Duplicate Collection

Right-click a collection in the tree → **Duplicate** opens a small
dialog:

| Field | Notes |
|-------|-------|
| **New Name** | Required, must be unique. |
| **Parent** | Sibling of the source / child of the source / root. |

On OK, all non-path settings are copied. Paths (`mediaDirectory`,
`artworkDirectory`, `videoDirectory`, `manualDirectory`) are deliberately
left blank so you don't accidentally fork into the same folder twice.

## Delete Collection

Right-click → **Delete** prompts for confirmation. Subcollections of
the deleted collection are reparented to its parent (or to root if
deleting a root collection). Per-item state remains in the database.

## Save / Revert / Cancel

- **Save** — commits all in-memory changes to disk, applying the
  current scope. The dialog stays open afterward.
- **Revert** — discards in-memory changes and re-loads from disk.
  Useful when you've experimented and want to back out.
- **Cancel** — closes the dialog. If there are unsaved changes, you
  get a confirm prompt.

The Save button glows with a pulsing drop shadow while there are
unsaved changes. Hovering over **Revert** flags fields that would be
restored.

## "Restart required" fields

A handful of settings only take effect after restart. The Settings
Dialog tags these inline; they're also called out here:

- `globalUiFontFamily` and `globalUiFontPointSize`
- `pixmapCacheSizeMB`
- Some startup ordering knobs (boot splash, startup video)

Most other settings apply live as you click through tabs, including
view-mode and sidebar position changes.

## Recipes

### Apply one collection's appearance to all collections

1. Tune one collection's **Appearance**, **Colors**, **Sidebar**, and
   **Text & Fonts** tabs to taste.
2. Click **Apply To Selected** in the dialog header.
3. Check **Appearance**, **Colors**, **Sidebar**, **Text & Fonts**
   under Categories.
4. Select all collections under Targets (Ctrl+A or use the **Select
   All** button).
5. OK.

### Add a launcher preset and reuse it everywhere

1. **Launcher** tab → **Global Launcher Presets** → **Add**.
2. Fill in name, path, core, parameters.
3. Save.
4. For each collection that should use the preset: **Launcher** tab →
   edit any launcher → set the **Preset** dropdown to the new preset.
   The inline path/core/params fields gray out — preset values take
   over.

### Reparent half a tree

Drag and drop in the tree. For larger reorganizations, edit the INI
file directly: change `parentCollectionIndex` for each collection and
restart.

### Disable propagation for a specific tab

The Apply Settings Dialog gates by **category**, not by tab. If a tab
crosses categories (e.g. Appearance + Colors), uncheck the categories
you don't want to propagate.

## For developers

- Settings UI: [src/ui/dialogs/settingsdialog\*](../../src/ui/dialogs/)
  — split into multiple translation units (`settingsdialogtree`,
  `settingsdialogfields`, `settingsdialogapply`, etc.) for build
  speed.
- Persistence: [src/modules/settings/settingsmanager\*](../../src/modules/settings/)
  drives INI read/write, with `configvalidation*` for live validation.
- Apply rules: `applysettingsdialog.cpp` enumerates which fields
  propagate per category. Non-propagatable fields (paths, parent
  linkage, extensions, launchers) are listed in the source.
- Save signal flow: `SettingsManager::settingsChanged` → all managers
  re-read the parts of `CollectionConfig` and `GeneralSettings` they
  care about.
- Adding a new field: define on `CollectionConfig` (or
  `GeneralSettings`), serialize in the manager, add UI in the
  appropriate `settingsdialog*` file, classify into a category in
  `applysettingsdialog`, and update
  [Configuration Reference](Configuration-Reference.md).
- Save-button pulse animation: `SettingsDialog::onUnsavedChanges` and
  the `QGraphicsDropShadowEffect` it drives.
