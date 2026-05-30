# Marquee

The marquee is an optional, second always-on-top window pinned to a
secondary monitor that mirrors the currently-selected item (or the
current collection). It's designed for arcade-cabinet "topper"
displays, marquee LCD strips, and bartop setups where the main grid
runs on the primary screen and a second screen shows a complementary
image or video.

> **Where to find this** — Settings Dialog → **Application** →
> **Marquee**. The dialog populates the screen list from
> `QGuiApplication::screens()` so you'll see your monitors by name
> (`HDMI-A-1`, `DP-2`, …).

## What it shows

Three modes, picked from the **Mode** combo in Settings:

| Mode | Index | What it renders |
|------|-------|-----------------|
| **Item Artwork (follows selection)** | `0` | The cover artwork of the currently-selected item. Updates as you move the selection. |
| **Collection Icon** | `1` | The current collection's icon (the same image used on its subcollection tile). Doesn't change when you move the selection — only when you switch collections. |
| **Video / Attract Loop** | `2` | A looping, muted video. Source is the selected item's preview video (the same file used in the [sidebar video preview](Video-Previews.md)), with a fallback to the current collection's `backgroundVideo` when the item has none. |

The window is:

- **Frameless** — no title bar or borders
- **Always-on-top** of its own screen
- **Click-through** — input goes to whatever's behind it (it ignores
  mouse and keyboard focus)
- **Aspect-ratio-fit** — pixmap or video is letterboxed to fit the
  screen without distortion

## Setting it up

1. Plug in the second monitor and confirm it's recognized in your
   desktop environment.
2. Open **Settings → Marquee**.
3. **Enable** the marquee.
4. Pick the target **Screen** from the dropdown. **(Primary screen)**
   is the safe default if you only have one display; it puts the
   marquee window on top of the same screen the main grid is on
   (mostly useful for testing the visual).
5. Pick the **Mode**.
6. Save and watch the marquee window appear on the chosen screen.

If you later unplug the marquee monitor, Kartend falls back to the
primary screen and logs a warning. The setting itself is preserved so
re-plugging the monitor restores the marquee to its original screen on
the next save.

## INI keys

```ini
[General]
marqueeEnabled=true
marqueeScreenName=HDMI-A-1
marqueeMode=0
```

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `marqueeEnabled` | bool | `false` | Master toggle. |
| `marqueeScreenName` | string | empty | `QScreen::name()` of the target screen. Empty = primary screen. |
| `marqueeMode` | enum | `0` | `0` = item artwork, `1` = collection icon, `2` = video / attract loop. |

The screen name is the Qt-side identifier — exactly what
`QScreen::name()` returns for your display. On most Linux systems this
matches the kernel output name (`HDMI-A-1`, `DP-2`, `eDP-1`), on
macOS it tends to be the model name, and on Windows it's the
`\.\DISPLAY*` form.

## Tips

### Video mode falls back to artwork

If the selected item has no preview video *and* the current collection
has no `backgroundVideo` set, the marquee displays nothing in video
mode (a black screen). To keep it visually busy, leave at least one
fallback configured per collection.

### Letterboxing on widescreen toppers

True marquee LCDs are usually very wide and short (e.g. 1920×360). The
marquee window letterboxes — a square cover artwork on a wide marquee
will show with black bars on the sides. Pair the marquee with a
pre-cropped wide variant: link a 16:5-ish marquee image as a custom
artwork type per item (see
[Artwork → Manual per-item links](Artwork.md#manual-per-item-links)),
then set the marquee mode to **Item Artwork** so each item can supply
its own marquee-shaped image.

### Multiple monitors during attract

[Attract Mode](Attract-Mode.md) doesn't take over the marquee — it
keeps showing the live selection / collection / video stream as the
main grid scrolls. This is the intended kiosk behavior.

## Where to next

- [Themes & Appearance](Themes-and-Appearance.md) — backgrounds and
  videos on the main grid
- [Artwork](Artwork.md) — custom artwork types you can use for
  marquee-shaped images
- [Video Previews](Video-Previews.md) — the same preview-video file
  shows in the sidebar and (via the marquee's video mode) the topper
- [Configuration Reference](Configuration-Reference.md#marquee--secondary-display) —
  the INI keys

## For developers

- Controller:
  [src/core/marqueecontroller.cpp](../../src/core/marqueecontroller.cpp).
  Owns the `MarqueeWindow` and the artwork-refresh debounce timer;
  reacts to selection / collection changes.
- Window widget:
  [src/ui/widgets/overlays/marqueewindow.cpp](../../src/ui/widgets/overlays/marqueewindow.cpp).
  Frameless `QWidget` with a `QStackedLayout` swapping between a
  `QLabel` (image mode) and a lazy-instantiated `QVideoWidget` (video
  mode).
- Settings panel:
  [src/ui/dialogs/settings/behavior/marqueepanel.cpp](../../src/ui/dialogs/settings/behavior/marqueepanel.cpp).
  Lists screens via `QGuiApplication::screens()`; the combo index
  doubles as the persisted `marqueeMode` integer.
- Screen fallback logic: when `marqueeScreenName` doesn't match any
  live screen, the controller pins the window to `primaryScreen()`
  and emits a warning to the `kartend.marquee` logging category.
