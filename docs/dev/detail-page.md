# Detail page architecture

The full-window "detail page" view (press `I` on a selected item)
splits its responsibilities across three components — a manager, a
neutral overlay widget, and a separate `DetailsPaneManager` for the
collapsible sidebar that *also* shows item details. This page maps
who owns what.

## The three components

```
        ┌─────────────────────────────────┐
        │  MainWindow                     │
        │   ├─ creates DetailPageOverlay  │
        │   │  parented to central widget │
        │   └─ hands it to DetailPageManager
        │      via IDetailPageOverlay     │
        └─────────────────────────────────┘
                       │
        ┌──────────────┴──────────────┐
        ▼                             ▼
DetailPageManager              DetailPageOverlay (UI)
 (modules/media/detailpage/)    (ui/widgets/overlays/)
 - Listens for "show details"   - QWidget covering the central
   keyboard / toolbar trigger     widget
 - Pulls resolved item context  - Renders artwork, metadata,
   from DetailsPaneManager        action buttons
 - Loads metadata + artwork +   - Emits signals on launch /
   usage stats from the DB        edit-fields / set-manual /
 - Pushes assembled payload      edit-artwork-links / close
   to the overlay
```

And on the side, the **sidebar pane**:

```
DetailsPaneManager (ui/controllers/detailspanemanager/)
 - Owns the collapsible sidebar's visibility, dock, gallery
 - Provides the resolved-item-context call DetailPageManager reads
 - Lives at the ui/ layer because it owns the sidebar widget
```

## Why a separate manager from the overlay

The overlay is a "dumb" widget — it draws whatever you push at it
and emits signals on user interaction. The manager:

- Knows **when** to show: subscribes to the `keyItemDetails` action
  + the toolbar `ℹ` button.
- Knows **what** to show: resolves the current selection through
  `DetailsPaneManager`, loads the heavy data (metadata, artwork
  cycle list, usage stats) via `DatabaseManager`.
- Knows **how to handle results**: launches the item, opens custom-
  fields / artwork-links sub-dialogs, propagates close.

Keeping the widget neutral lets us:

- Unit-test the manager without spinning up a `QApplication`.
- Swap the overlay implementation (e.g. a future
  `QtQuick`-based version) without rewriting the manager.

The interface contract is the role header
[idetailpageoverlay.h](../../src/api/idetailpageoverlay.h) — both sides
talk through it.

## Why `DetailsPaneManager` is separate from `DetailPageManager`

They display the **same** item data in different surfaces:

| Manager | Surface | Visibility | Lives at |
|---------|---------|------------|----------|
| `DetailsPaneManager` | Collapsible sidebar (overlay or docked) | Toggleable per-collection (`F9`) | `src/ui/controllers/detailspanemanager/` |
| `DetailPageManager` | Full-window modal overlay | One-shot per `I` press | `src/modules/media/detailpage/` |

They overlap on **data resolution** (both need the resolved item
context — file path, collection UUID, title, etc.) but diverge on:

- **Lifecycle**: the sidebar persists; the detail page is modal /
  one-shot.
- **Layout**: the sidebar squeezes alongside the grid; the detail
  page covers the grid entirely.
- **Data depth**: the detail page shows a bigger artwork render, all
  custom fields formatted spaciously, action buttons. The sidebar
  is the same data formatted compactly with a gallery strip.

The cross-cutting "resolved item context" lives on
`DetailsPaneManager`, and `DetailPageManager` reads from it via the
shared `ApplicationContext`. That avoids two managers racing on
which item is "current."

### Why `DetailsPaneManager` is at `ui/`, not `modules/media/`

It owns a ui-layer widget (`DetailsPane`), so per the layering rules
it has to live at `ui/`. The historical name carried "Manager"
because that was the established pattern when it was extracted, but
structurally it's a controller for a ui widget. It was moved out of
`modules/media/` and now lives at
[`src/ui/controllers/detailspanemanager/`](../../src/ui/controllers/detailspanemanager/).

`DetailPageManager`'s widget (`DetailPageOverlay`) is owned by
MainWindow, parented to the central widget so it can span the full
window. The manager itself only holds an `IDetailPageOverlay *`
back-reference — no owning relationship, no `QWidget` member.

## Signal flow

Open:

```
User: presses I (keyItemDetails)
        │
        ▼
KeyboardManager → InteractionManager::showItemDetails()
        │
        ▼
DetailPageManager::onShowRequested(itemContext)
        │
        ├─ DatabaseManager::loadMetadataAndArtwork(itemContext) (async)
        │       │
        │       ▼
        │   DetailPageManager::onDataLoaded(payload)
        │       │
        │       ▼
        ▼   overlay->showWithPayload(payload)
SidebarManager::yield() — sidebar hidden while detail page is up
        │
        ▼
DetailPageOverlay::paintEvent → renders
```

Close:

```
User: presses Esc / clicks Close
        │
        ▼
DetailPageOverlay::closeRequested signal
        │
        ▼
DetailPageManager::onCloseRequested()
        │
        ├─ overlay->hide()
        │
        ▼
SidebarManager::restore() — sidebar comes back
```

Action buttons emit specific signals that the manager routes:

| Overlay signal | Manager handler | What happens |
|----------------|-----------------|--------------|
| `launchRequested` | `onLaunch(itemContext)` | `InteractionManager::launchItem(...)` |
| `editFieldsRequested` | `onEditFields(itemContext)` | Open `CustomFieldsDialog`, refresh payload on accept |
| `setManualRequested` | `onSetManual(itemContext)` | Open file picker, save into `ItemMetadataStore` |
| `editArtworkLinksRequested` | `onEditArtworkLinks(itemContext)` | Open `ItemArtworkLinksDialog`, refresh payload on accept |

The manager handles the modal dialogs **above** the overlay (Qt's
modal-on-top-of-modal stacking works because the overlay isn't a
modal `QDialog` — it's a custom `QWidget`).

## Lifetime

- `DetailPageOverlay` is created once at MainWindow init, parented
  to the central widget. Stays hidden until first show; never
  destroyed.
- `DetailPageManager` is owned by `ApplicationManager` in the same
  controlled destruction order as the rest of the manager fleet.
  Destruction order matters because the manager holds a back-
  reference to the overlay — the manager goes first.
- The role-interface pointer (`IDetailPageOverlay *`) is a non-
  owning view; MainWindow owns the concrete widget.

## State sharing with `DetailsPaneManager`

The single source of truth for "what's the current item context" is
`DetailsPaneManager::currentItemContext()`. `DetailPageManager` reads
it on show and pushes the same identity through to the database
load. When the user navigates while the detail page is open, the
detail page **stays on the originally-shown item** — closing it
returns the focus to wherever the sidebar's current item is.

## Adding a new action button

1. Add the button to
   [detailpageoverlay.cpp](../../src/ui/widgets/overlays/detailpageoverlay.cpp)
   (the concrete widget).
2. Add a corresponding signal on
   [idetailpageoverlay.h](../../src/api/idetailpageoverlay.h).
3. Add an `onXxxRequested` handler on
   [detailpagemanager.cpp](../../src/modules/media/detailpage/detailpagemanager.cpp)
   wired to that signal.
4. If the action opens a sub-dialog, route through
   [DialogController](../../src/core/dialogcontroller.cpp) — the
   manager shouldn't take a `QWidget *parent` directly (avoids a
   ui-layer dependency in `modules/media/`).

## Related code

| Concern | File |
|---------|------|
| Manager | [src/modules/media/detailpage/detailpagemanager.{h,cpp}](../../src/modules/media/detailpage/) |
| Overlay widget | [src/ui/widgets/overlays/detailpageoverlay.{h,cpp}](../../src/ui/widgets/overlays/) |
| Role interface | [src/api/idetailpageoverlay.h](../../src/api/idetailpageoverlay.h) |
| Sidebar pane controller | [src/ui/controllers/detailspanemanager/](../../src/ui/controllers/detailspanemanager/) |
| Resolved item context | `DetailsPaneManager::currentItemContext()` |
| Dialog routing | [src/core/dialogcontroller.{h,cpp}](../../src/core/) |
| Architecture overview | [architecture.md](architecture.md) |
