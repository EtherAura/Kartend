# Input & Controls

Kartend handles keyboard, mouse, and gamepad input concurrently — they
all drive the same selection model and you can switch between them
mid-session without any mode change. The core navigation keys are
rebindable from the Settings Dialog; gamepad button assignments are
user-configurable; the mouse artwork-cycle modifier can be reassigned.

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
| `Ctrl+A` | Select all search text (this is `QLineEdit`'s own binding — Kartend simply lets keystrokes through to the field) |
| `Escape` | With text present: clears the text but **keeps focus in the search bar**, so you can retype immediately. Press it a second time to return focus to the grid. |
| `Enter` | Nothing. There is no binding for Return in the search bar — results update as you type, so there is nothing to submit. Press `Escape` twice, or click a tile, to get back to the grid. |

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

**Eight actions have a capture field:** the four navigation
directions, Confirm, Back, Search, and Home view.

The remaining bindable keys — `keyJumpFirst`, `keyJumpLast`,
`keyAlphabeticBack`, `keyAlphabeticForward`, `keyItemDetails` — are
real INI keys with no field in the dialog. To change them, edit
`kartend.cfg` by hand and restart. See
[Configuration Reference → Keyboard bindings](Configuration-Reference.md#keyboard-bindings).

Keys that are *not* bindable at all: the `Ctrl+1..4` view shortcuts,
view toggles (`F8`–`F11`), zoom (`Ctrl+=` / `Ctrl+-` / `Ctrl+0`),
quit (`Ctrl+Q`), settings (`Ctrl+,`), and refresh (`F5` / `Ctrl+F5`).
File a feature request if you need any of these to move.

> **Modifiers are ignored on bound keys.** The dispatcher compares the
> key code only, so `Ctrl+I` opens the detail page just as `I` does,
> and `Alt+/` focuses search just as `/` does. Bear this in mind if you
> rebind an action onto a key you also use in a chord.

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
| Middle-click on item | Open the **media preview overlay** — the same video-first preview expand-mode uses, falling back to artwork when the item has no video. Selection is not changed. |
| Modifier + middle-click on item | Cycle the artwork type shown on the tile (front → box → screenshot → … , skipping types with no image). The modifier is configurable. |

The artwork-cycle modifier is set globally:

| Setting | INI key | Default |
|---------|---------|---------|
| Artwork cycle modifier | `artworkCycleModifier` | Shift |

Choose from Shift / Ctrl / Alt / Meta in **Settings → General →
Controls**. Pick the one that doesn't conflict with your window
manager's middle-click bindings.

> **Don't hand-edit this one.** The INI stores the numeric
> `Qt::KeyboardModifier` value, not a name: `33554432` Shift,
> `67108864` Ctrl, `134217728` Alt, `268435456` Meta. Writing
> `artworkCycleModifier=Shift` reads back as `0`, which disables the
> chord entirely. Use the dropdown.

The held modifier must match the configured one **exactly** —
Ctrl+Shift+middle-click with Shift configured is not a match, and falls
through to the plain middle-click preview instead.

### Hold scroll (click-and-hold)

Press and hold the mouse button on a tile for `clickHoldDelayMs` to
start **hold scroll**: the selection then advances on its own at a
fixed cadence, so you can skim a large collection without repeated
clicks.

The direction is chosen once, when the hold starts, from where the
selected tile sits relative to the centre of the viewport — a tile in
the lower half scrolls down, one in the upper half scrolls up. It is
**not** cursor-following: moving the mouse during the hold does
nothing.

| Setting | INI key | Default |
|---------|---------|---------|
| Hold delay before activation | `clickHoldDelayMs` | `500` ms |
| Hold-scroll repeat (Grid / Cover Flow / Horizontal) | `clickHoldRepeatIntervalMs` | `320` ms |
| Hold-scroll repeat (List) | `listClickHoldRepeatIntervalMs` | `80` ms |

Release the button to stop. Moving the cursor outside the viewport does
not cancel it.

### Wheel

The wheel moves the **selection**, not the viewport. The viewport then
animates to re-centre on the newly selected item, which reads as
scrolling — but the selection is what actually changes, which is why a
wheel tick also stops [attract mode](Attract-Mode.md#what-stops-attract).

| Action | Effect |
|--------|--------|
| Wheel up / down | Move the selection by `mouseWheelRows` rows (one item in List and Cover Flow; one column in Horizontal) |
| `Ctrl + wheel` | Same as a plain wheel — modifiers are not read on the wheel path |
| `Shift + wheel` | Same as a plain wheel |

| Setting | INI key | Default |
|---------|---------|---------|
| Rows scrolled per wheel tick | `mouseWheelRows` | `1` (clamped 1–100) |
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
- **Pin to top** / **Hide** / **Mark as continue later** — toggle the per-item [state flags](Item-Metadata.md#state-flags); the labels flip to **Unpin** / **Unhide** / **Clear continue-later marker** when the flag is already set
- **Preview launch command…** — open the [Launch Preview](Launchers.md#launch-command-preview-dry-run) for this item
- **Set manual file…** — pick a manual / PDF / etc. for this item
- **Clear manual override** — appears only if a manual is set
- **Always launch with…** — appears only if the collection has more
  than one launcher; opens the [Launcher Chooser](Launchers.md#multi-launcher-chooser)
- **Clear launcher override** — appears only if a per-item override exists
- *(separator)*
- **Add to playlist ▶** — submenu with each existing playlist + **New
  playlist…**
- **Add to Favorites** *(or **Remove from Favorites**)* — toggles
  membership in the built-in [Favorites](Playlists-and-Favorites.md#favorites)

### On a subcollection tile

- **Open** — enter the subcollection
- **Properties** — toggle the sidebar
- **Refresh** — soft-reload

### On a playlist tile

- **Rename playlist…**
- **Delete playlist…** — hidden for reserved playlists like Favorites

### Inside a playlist (right-click on its items)

All media-item entries above, plus:

- **Remove from playlist**

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
| `R1` (or equivalent) | Toggle the details pane (right sidebar) | `gamepadToggleSidebarButton` |
| `L1` (or equivalent) | Toggle the collection tree (left sidebar) | `gamepadToggleCollectionTreeButton` |
| D-pad / left stick | Always move the grid selection (pulls focus back to the grid) | `gamepadUseDpad` / `gamepadUseLeftStick` |
| Directions (artwork expanded) | Cycle that item's artwork; past the ends, move to the previous/next item | — |
| Mouse wheel (artwork expanded) | Same as the directions — one artwork per flick, then steps items | — |
| Right stick up/down | Drive the details pane — scrolls the selected region and steps between artwork, description, and metadata; the selection is ringed | `gamepadRightStickSections` |
| Right stick left/right | Move focus between the sidebars and the grid | `gamepadRightStickSections` |
| Right stick (tree focused) | Up/down moves the collection tree's rows | `gamepadRightStickSections` |
| Right stick (toolbar focused) | Left/right steps across the toolbar buttons | `gamepadRightStickSections` |
| `Select` (held) + direction | Move focus between sections — the only way up reaches the toolbar. Shows the focus ring plus a dimmed, blurred backdrop | — |
| Confirm (A) | Activates the focused section — tree row or toolbar button — instead of launching the grid item | `gamepadConfirmButton` |

D-pad and left stick can be enabled / disabled independently. Held
input repeats at the same cadence as keyboard repeat
(`keyboardRepeatIntervalMs`).

X, Back, Start, Guide, the shoulder buttons and the stick clicks are
all read and dispatched, but resolve to no action until you bind them
to confirm / back / sidebar in Settings.

**Triggers are not readable.** Only the left stick's X and Y axes are
sampled, so the analogue triggers can't be bound to anything today.

### Rebinding gamepad buttons

Settings → **General** → **Gamepad** → click a button picker, then
press the gamepad button you want. Live capture mode listens for the
next button event. Press `Escape` (or click outside) to cancel.

### Multiple gamepads

Kartend listens to the first connected device. Hot-plugging is
supported — connect or disconnect mid-session and the backend reattaches.

## Binding visualizer

**Help → Binding Visualizer…** opens an interactive reference for the
**rebindable** bindings — the ones that live in `kartend.cfg` and can
therefore differ from install to install. It covers three groups,
Navigation, Actions and Search, and does **not** list the fixed
application shortcuts (`F1`, `F5`, `F8`–`F11`, `Ctrl+1..4`, zoom,
quit, settings, and the rest); those are in the tables above and never
change. The killer feature is **press-to-identify**: tap a key or
gamepad button while the dialog is open and the matching row
highlights, so you can answer "what does this do?" without scanning
the table.

```
┌──────────────────────────────────────────────────────────┐
│ Binding Visualizer                                       │
│ Press a key or button to identify the mapped action.     │
│                                                          │
│ ▾ Navigation                                             │
│     Move selection up         ↑     /  (none)            │
│     Move selection down       ↓     /  (none)            │
│     Move selection left       ←     /  (none)            │
│     Move selection right      →     /  (none)            │
│ ▾ Actions                                                │
│     Launch / confirm          Enter /  A                 │
│     Back / cancel             Esc   /  B          ← lit  │
│ ▾ Search                                                 │
│     Focus search bar          /     /  (none)            │
└──────────────────────────────────────────────────────────┘
```

Only Confirm, Back and Toggle-sidebar carry a gamepad button in the
table — directional movement comes from the D-pad and left stick as a
pair rather than from a single named button, so those rows show
**(none)** in the gamepad column.

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
- Hold-scroll: `MouseManager::startMouseHoldScrolling()` /
  `stopMouseHoldScrolling()`, driven by `m_clickHoldTimer` (the initial
  delay) and `m_mouseHoldTimer` (the repeat).
- Search-bar focus management: `EventManager` filters keystrokes that
  shouldn't reach the search line edit.
