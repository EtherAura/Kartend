# Input & Controls

Kartend handles keyboard, mouse, and gamepad input concurrently — they
all drive the same selection model and you can switch between them
mid-session without any mode change. Every navigation key is rebindable;
gamepad button assignments are user-configurable; mouse modifier
combinations can be reassigned.

> **Where to find this** — Settings Dialog → **General** tab →
> **Keyboard Bindings**, **Gamepad Config**, **Mouse**. Underlying INI
> keys live under `[General]` — see
> [Configuration Reference](Configuration-Reference.md#keyboard-bindings).

## Keyboard

### Navigation defaults

| Key | Action | INI key (rebindable) |
|-----|--------|----------------------|
| `←` `→` `↑` `↓` | Move selection | `keyNavLeft`, `keyNavRight`, `keyNavUp`, `keyNavDown` |
| `Enter` / `Return` | Launch / enter subcollection / confirm | `keyConfirm` |
| `Escape` | Back / close overlay / cancel | `keyBack` |
| `Home` | Jump to first item | `keyJumpFirst` |
| `End` | Jump to last item | `keyJumpLast` |
| `Page Up` | Alphabetic jump backward (previous starting letter) | `keyAlphabeticBack` |
| `Page Down` | Alphabetic jump forward | `keyAlphabeticForward` |
| `/` | Focus search bar | `keySearch` |
| `I` | Show full-screen [item detail page](Item-Metadata.md#detail-page) | `keyItemDetails` |

### View / layout

| Key | Action |
|-----|--------|
| `Ctrl+1` | Switch to Grid view |
| `Ctrl+2` | Switch to List view |
| `Ctrl+3` | Switch to Cover Flow view |
| `Ctrl+4` | Switch to Horizontal view |
| `Ctrl+Shift++` | Increase grid columns (`gridWidth`) |
| `Ctrl+Shift+-` | Decrease grid columns |
| `F8` | Toggle toolbar (persistent, saved as `showToolbar`) |
| `F9` | Toggle sidebar / details pane (per-collection `sidebarVisible`) |
| `F10` | Toggle menu bar (persistent, saved as `showMenuBar`) |
| `F11` | Toggle fullscreen (persistent, saved as `fullscreen`) |

### Search bar

`/` focuses the search input. Once focused:

| Key | Action |
|-----|--------|
| `Ctrl+A` | Select all search text |
| `Escape` | Clear the search and return focus to the grid |
| `Enter` | Move selection into the filtered results (focus jumps to grid) |

Typing while the *grid* is focused does not auto-focus the search — you
have to press `/` first. This avoids accidentally search-typing when
you're trying to navigate by alphabetic jump.

### Zoom

Text zoom applies to the entire UI (toolbar, menus, sidebar, item
titles), persisted as `uiTextZoomPercent`:

| Key | Action |
|-----|--------|
| `Ctrl+=` | Increase text zoom 10% |
| `Ctrl+-` | Decrease text zoom 10% |
| `Ctrl+0` | Reset zoom to 100% |

A small HUD shows the current percentage briefly after each adjustment.

### Application-wide

| Key | Action |
|-----|--------|
| `F1` | Open the in-app keyboard shortcut reference dialog |
| `F5` | Soft refresh — reload the current collection without rescanning |
| `Ctrl+F5` | Full rescan — drop and rebuild the database for the current collection |
| `Ctrl+Shift+R` | Open a random item |
| `Ctrl+Shift+P` | Open the [Command Palette](Toolbar-and-Menus.md#command-palette) |
| `Ctrl+,` | Open Settings |
| `Ctrl+Q` | Quit |

### Rebinding keys

Settings → **General** → **Keyboard Bindings** → click the field for
the action you want to rebind, then press the new key. The dialog
captures the next key event and stores its `Qt::Key` code. Press
`Escape` to cancel a capture in progress.

Keys that are *not* rebindable today: the `Ctrl+1..4` view shortcuts,
view toggles (`F8`–`F11`), zoom (`Ctrl+=` / `Ctrl+-` / `Ctrl+0`),
quit (`Ctrl+Q`), settings (`Ctrl+,`), and refresh (`F5` / `Ctrl+F5`).
File a feature request if you need any of these to move.

### Key repeat

Held navigation keys repeat. Cadence is independently tunable for grid
and list views:

| Setting | INI key | Default |
|---------|---------|---------|
| Initial delay before repeat starts | `keyboardRepeatDelayMs` | `260` ms |
| Repeat interval (Grid / Cover Flow / Horizontal) | `keyboardRepeatIntervalMs` | `260` ms |
| Repeat interval (List) | `listKeyboardRepeatIntervalMs` | `50` ms |

List view's faster default is intentional — you're often scanning
linearly through hundreds of rows.

## Mouse

### Click defaults

| Action | Effect |
|--------|--------|
| Single-click on item | Select |
| Double-click on item | Launch (or enter subcollection / virtual folder) |
| Right-click on item | [Context menu](#context-menus) |
| Single-click outside any tile | Clear selection focus |
| Click and drag (no item) | (No action — drag-to-select is via hold-scroll, below) |

### Middle-click

| Action | Effect |
|--------|--------|
| Middle-click on item | Toggle the video preview in the sidebar (turns the preview on or off without changing selection) |
| Modifier + middle-click on item | Cycle the artwork type shown in the sidebar gallery (e.g. boxfront → backdrop → label → boxfront…). The modifier is configurable. |

The artwork-cycle modifier is set globally:

| Setting | INI key | Default |
|---------|---------|---------|
| Artwork cycle modifier | `artworkCycleModifier` | `Shift` |

Choices are `Shift`, `Control`, `Alt`, `Meta`. Pick the one that doesn't
conflict with your window manager's middle-click bindings.

### Hold scroll (click-and-hold dragging)

Press and hold the mouse button on a tile to start **hold scroll**.
Move the cursor while holding and selection follows the cursor — useful
for rapidly skimming through a large collection.

| Setting | INI key | Default |
|---------|---------|---------|
| Hold delay before activation | `clickHoldDelayMs` | `500` ms |
| Hold-scroll repeat (Grid / Cover Flow / Horizontal) | `clickHoldRepeatIntervalMs` | `320` ms |
| Hold-scroll repeat (List) | `listClickHoldRepeatIntervalMs` | `80` ms |

Move the cursor outside the viewport to cancel without launching.

### Wheel

| Action | Effect |
|--------|--------|
| Wheel up / down | Scroll the viewport |
| `Ctrl + wheel` | (No action by default — reserved) |
| `Shift + wheel` | (No action by default — reserved) |

| Setting | INI key | Default |
|---------|---------|---------|
| Rows scrolled per wheel tick | `mouseWheelRows` | `1` |
| Global scroll speed multiplier | `scrollVelocityMultiplier` | `1.0` (range 0.25–5.0) |

### Hover

When **Select Item on Hover** is enabled (`selectItemOnHover=true`,
default `false`), moving the cursor over a tile selects it without a
click — handy for kiosk setups. Combined with attract mode this gives
a "scroll-to-browse" experience.

### Double-click vs. expand mode

In a collection with `expandMode=true`, the *first* `Enter` or
double-click does not launch — it shows a full-screen artwork preview.
A second `Enter` / double-click launches; `Escape` cancels. See
[Collections → expand mode](Collections.md#the-expand-mode-two-stage-launch).

## Context menus

Right-click anywhere reveals a context menu. Contents depend on what
you clicked:

### On a media item

- **Launch** — run the item with the configured launcher
- **Properties** — toggle the sidebar
- **Refresh** — soft-reload the collection
- *(separator)*
- **Edit metadata…** — open the [Edit Metadata Dialog](Item-Metadata.md#edit-metadata-dialog) (notes, tags, rating, source URL, custom fields)
- **Pin item** / **Hide item** / **Mark continue later** — toggle the per-item [state flags](Item-Metadata.md#state-flags); the labels flip to **Unpin** / **Unhide** / **Clear continue later** when the flag is already set
- **Preview launch command…** — open the [Launch Preview](Launchers.md#launch-command-preview-dry-run) for this item
- **Set manual file…** — pick a manual / PDF / etc. for this item
- **Clear manual override** — appears only if a manual is set
- **Always launch with…** — appears only if the collection has more
  than one launcher; opens the [Launcher Chooser](Launchers.md#multi-launcher-chooser)
- **Clear launcher override** — appears only if a per-item override exists
- *(separator)*
- **Add to Playlist ▶** — submenu with each existing playlist + **New
  playlist…**
- **Add to Favorites** *(or **Remove from Favorites**)* — toggles
  membership in the built-in [Favorites](Playlists-and-Favorites.md#favorites)

### On a subcollection tile

- **Open** — enter the subcollection
- **Properties** — toggle the sidebar
- **Refresh** — soft-reload

### On a playlist tile

- **Rename Playlist…**
- **Delete Playlist** — hidden for reserved playlists like Favorites

### Inside a playlist (right-click on its items)

All media-item entries above, plus:

- **Remove from Playlist**

## Gamepad

Gamepad support is **optional** and gracefully degrades: if no gamepad
backend is available, all gamepad-related UI hides and the rest of the
app works normally.

### Backends

Kartend tries one of two backends, in order:

1. **Qt6::Gamepad** — preferred. Picked up automatically if
   `qt6-gamepad` is installed and was available at build time.
2. **SDL2** — fallback. Picked up if `libsdl2` is installed.

If neither is available the **Gamepad** section in Settings is hidden.
See [Building → Optional Qt6Gamepad / SDL2](../dev/building.md) and the
distro-specific [Launchers + dependencies](../../readme.md#dependencies).

### Default bindings

| Input | Action | INI key |
|-------|--------|---------|
| D-pad | Move selection | `gamepadUseDpad` (toggle) |
| Left stick | Move selection | `gamepadUseLeftStick` (toggle) |
| `A` (or equivalent) | Confirm / launch | `gamepadConfirmButton` |
| `B` (or equivalent) | Back / close | `gamepadBackButton` |
| `Y` (or equivalent) | Toggle sidebar | `gamepadToggleSidebarButton` |

D-pad and left stick can be enabled / disabled independently. Held
input repeats at the same cadence as keyboard repeat
(`keyboardRepeatIntervalMs`).

Other buttons (face buttons, shoulder buttons, triggers, stick clicks)
are passed through but unbound by default. Open Settings to bind them
to confirm/back/sidebar.

### Rebinding gamepad buttons

Settings → **General** → **Gamepad** → click a button picker, then
press the gamepad button you want. Live capture mode listens for the
next button event. Press `Escape` (or click outside) to cancel.

### Multiple gamepads

Kartend listens to the first connected device. Hot-plugging is
supported — connect or disconnect mid-session and the backend reattaches.

## Binding visualizer

**Help → Binding Visualizer…** opens an interactive reference that
shows every keyboard and gamepad mapping currently in effect, grouped
by surface (Navigation, Selection, Search, etc.). The killer feature
is **press-to-identify**: tap a key or gamepad button while the
dialog is open and the matching row highlights so you can answer
"what does this do?" without scanning the table.

```
┌──────────────────────────────────────────────────────────┐
│ Binding Visualizer                                       │
│ Press a key or button to identify the mapped action.     │
│                                                          │
│ ▾ Navigation                                             │
│     Move selection up         ↑     /  Pad Up            │
│     Move selection down       ↓     /  Pad Down          │
│     Move selection left       ←     /  Pad Left          │
│     Move selection right      →     /  Pad Right         │
│ ▾ Selection                                              │
│     Launch / confirm          Enter /  A                 │
│     Back / cancel             Esc   /  B          ← lit  │
│ ▾ Search                                                 │
│     Focus search bar          /     /  —                 │
│ …                                                        │
└──────────────────────────────────────────────────────────┘
```

Useful when you've rebound a few keys and forgotten which is which,
or when handing the input to someone unfamiliar with the layout.
Gamepad input is captured live while the dialog is open, so an
unfamiliar pad can be mapped to actions interactively.

The view is read-only — rebinding still happens through Settings →
**General** → **Keyboard Bindings** / **Gamepad Config**.

## Hover, focus, and keyboard reach

A few rules govern who's listening:

- The **search bar** captures keystrokes only while it has focus
  (yellow caret visible). `/` focuses it; `Escape` returns focus.
- The **grid** captures arrow keys and most other shortcuts whenever
  the search bar is *not* focused.
- Modal dialogs (Settings, Statistics, Shortcuts, Launcher Chooser…)
  capture all keys while open. `Escape` closes them.

Mouse hover never grabs keyboard focus. You can hover-select while
typing in the search bar without disrupting the search.

## Quick reference card

```
Selection ........... Arrow keys
Launch .............. Enter
Back ................ Escape
First / last ........ Home / End
Alphabetic jump ..... PgUp / PgDn
Search .............. /
Item details ........ I
Random item ......... Ctrl+Shift+R
View modes .......... Ctrl+1..4
Grid columns ........ Ctrl+Shift++ / Ctrl+Shift+-
Toolbar ............. F8
Sidebar ............. F9
Menu bar ............ F10
Fullscreen .......... F11
Soft refresh ........ F5
Full rescan ......... Ctrl+F5
Settings ............ Ctrl+,
Quit ................ Ctrl+Q
Shortcut help ....... F1
Zoom ................ Ctrl+= / Ctrl+- / Ctrl+0
```

## For developers

- Keyboard handling: [src/modules/input/keyboard/](../../src/modules/input/keyboard/) (`KeyboardManager`,
  `ArrowNavigationHandler`, `AlphabeticNavigationHandler`).
- Mouse handling: [src/modules/input/mouse/](../../src/modules/input/mouse/) (`MouseManager`).
- Gamepad: [src/modules/input/gamepad/](../../src/modules/input/gamepad/). Backend
  selection happens at compile time via
  `KARTEND_HAS_QT_GAMEPAD` / `KARTEND_HAS_SDL2_GAMEPAD` (see
  [CMakeLists.txt](../../CMakeLists.txt)).
- Rebinding storage: `GeneralSettings::keyNav*` etc. in
  [src/utils/app/collection/generalsettings.h](../../src/utils/app/collection/generalsettings.h).
- Repeat-interval logic for List view's special-case faster cadence:
  see `keyboardmanager*.cpp` for the view-aware path.
- Hold-scroll: `MouseManager::startHoldScroll()` / `holdScrollTimer`.
- Search-bar focus management: `EventManager` filters keystrokes that
  shouldn't reach the search line edit.
