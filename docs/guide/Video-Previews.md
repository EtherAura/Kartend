# Video Previews

Per-item video previews — short clips, gameplay footage, trailers — can
be attached to any item by dropping a video file with a matching base
filename into the collection's video directory. Previews show up in
two places: the **sidebar** as a player, and **Cover Flow view** as the
full-screen center tile.

> **Where to find this** — Settings Dialog → **Paths & Extensions**
> tab → **Video Directory**. Volume on the toolbar
> (`previewVideoVolume` global). Backed by Qt Multimedia
> (`QMediaPlayer` + `QVideoSink`).

## How matching works

For every item file Kartend looks in the collection's `videoDirectory`
for a video whose base filename matches:

```
mediaDirectory/My Game (USA).sfc
videoDirectory/My Game (USA).mp4   ← matches
videoDirectory/My Game (USA).webm  ← also matches
```

Same matching rules as artwork — case-sensitive on case-sensitive
filesystems, base-filename only, recognized extensions:

- `.mp4` (H.264 / H.265 / AV1, depending on codec availability)
- `.webm` (VP8 / VP9 / AV1)
- `.avi`
- `.mov`
- `.mkv` (depends on codec install)

The first matching file wins.

If a video isn't found, the sidebar simply doesn't show a video tile
in the gallery and Cover Flow falls back to the item's primary
artwork. No warnings, no error tiles.

## In the sidebar

The sidebar's **Item** tab includes a video tile in the artwork gallery
(always last in the order). Clicking the video tile expands the
embedded player.

| Element | Behavior |
|---------|----------|
| Play / pause button | Standard transport |
| Volume | Tied to global `previewVideoVolume` (toolbar slider, 0–100, default `100`) |
| Loop | Always looping |
| Pause on selection change | When you move selection to a different item, the previous video pauses (the new item's video, if any, takes over) |

The video tile in the gallery shows a play-icon badge so you can tell
it apart from artwork tiles. Cycling artwork types
(modifier+middle-click) skips the video tile — only artwork types
participate in the cycle.

### Volume

The toolbar's **Preview Volume** slider controls all video previews
globally. Mute by dragging to 0; full volume at 100. Persisted as
`previewVideoVolume` in `[General]`.

```ini
[General]
previewVideoVolume=80
```

The slider is **focus-policy NoFocus** — clicking it doesn't steal
focus from the items grid. Drag away.

### Hiding the slider

If you don't want the volume slider on the toolbar:

```ini
[General]
toolbarShowSearchModeButton=true   ; the slider lives between this and the search bar
```

There's no per-control hide for the volume slider today; if you want
it gone, file a feature request — or hide the entire toolbar with
`F8`.

## In Cover Flow view

Cover Flow auto-plays the **center item's** preview video full-screen
in the carousel center tile, *if* a video is available. Items off to
the sides still render with artwork.

| Behavior | When |
|----------|------|
| Video plays | Center item has a matching video |
| Falls back to artwork | Center item has no video |
| Pauses | Selection changes (the new center item's video starts) |
| Volume | Same global slider as sidebar |

Cover Flow auto-hides the sidebar entirely (see
[View Modes → Cover Flow](View-Modes.md#cover-flow-view)) so the
carousel takes the full viewport.

## Toggling video preview from a tile

In Grid / List / Horizontal views: **middle-click** any item to toggle
its sidebar video preview. This works whether the sidebar is visible
or hidden:

- If the sidebar is hidden, middle-click shows it and starts the video.
- If the sidebar is visible and the video is playing, middle-click
  pauses (or hides the video; behavior depends on current state).

Modifier + middle-click cycles artwork types instead — the modifier is
what differentiates "toggle video" from "cycle artwork". Modifier
defaults to `Shift` and is set by `artworkCycleModifier` (see
[Configuration Reference](Configuration-Reference.md#mouse)).

## Still thumbnails for video tiles

Anywhere a video preview appears as a non-playing thumbnail — the
sidebar gallery's video tile, the Cover Flow off-center slots, the
detail page's media strip — Kartend extracts a real frame from the
video and uses it as a still. The frame is taken roughly **1 second
in** (so a black opening frame doesn't become the thumbnail) and is
cached in memory by absolute path; subsequent paints reuse the same
frame instantly.

Behavior:

- While extraction is running you'll see a placeholder tile with a
  `▶` glyph. The real frame swaps in as soon as the decoder hands it
  back, with no further click required.
- Extractions are serialized through one shared `QMediaPlayer` and
  share the codec stack with sidebar / Cover Flow playback, so codec
  availability is the same (see below).
- A failed extraction (missing file, decoder error, timeout) is
  cached as a null pixmap so Kartend doesn't retry endlessly. Restart
  the app or force a re-scan if you replace a previously-bad video.

There's no setting — extraction runs automatically whenever a still
representation of a video is needed.

## Codec availability

Video playback depends on Qt Multimedia's backend, which on Linux
typically uses **GStreamer**. Codec availability depends on which
GStreamer plugin packages your distro has installed:

| Distro | Packages typically needed |
|--------|---------------------------|
| Debian / Ubuntu | `gstreamer1.0-libav`, `gstreamer1.0-plugins-good`, `gstreamer1.0-plugins-bad` (and `-ugly` for some patent-encumbered codecs) |
| Arch | `gst-libav`, `gst-plugins-good`, `gst-plugins-bad`, `gst-plugins-ugly` |
| Gentoo | Pull in `media-libs/gst-plugins-libav` and the relevant `gst-plugins-*` packages |
| Flatpak | Bundled with the KDE runtime — should work out of the box |

If a video doesn't play but the file is fine in `mpv`:

```bash
gst-inspect-1.0 | grep <codec-name>
```

If the codec isn't installed, install the matching plugin package and
restart Kartend.

## Storage tips

Video previews can be large. Some practical advice:

- Aim for **2–10 second clips** at moderate bitrate. Long previews
  encourage staring at one tile instead of browsing.
- **Loop-friendly** content (no harsh cuts at the start/end) feels
  better when looping.
- Compress to **WebM (VP9)** or **MP4 (H.264)** for broad
  compatibility. AV1 may need recent codec packages.
- Store in a separate `videoDirectory` so backups can exclude videos
  if you want a smaller tarball.

A typical setup:

```
~/games/snes/                 ← media
~/games/snes/_covers/         ← artwork
~/games/snes/_videos/         ← video previews (one per game)
```

```ini
[SNES]
mediaDirectory=~/games/snes
artworkDirectory=~/games/snes/_covers
videoDirectory=~/games/snes/_videos
```

## Format and decoding gotchas

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Video tile appears but doesn't play | Codec missing | Install the relevant `gst-plugins-*` package. |
| Sidebar shows artwork only, no video tile | No matching video file in `videoDirectory` | Check the filename — base must match the media file exactly (case-sensitive). |
| Audio plays but video is black | GPU acceleration mismatch | Try `QT_MEDIA_BACKEND=ffmpeg` (Qt 6.5+) or update GPU drivers. |
| Video stutters at high resolution | Decode is CPU-bound on this codec | Re-encode to a more efficient codec (VP9, H.264) or lower resolution. |
| Audio plays even when slider is at 0 | Slider isn't bound to this player instance | Should not happen — file an issue with reproduction steps. |
| Cover Flow shows artwork instead of video | The selected item doesn't have a matching video | Cover Flow falls back to artwork; this is expected. Drop a matching video file in `videoDirectory`. |

For systematic playback issues see
[Troubleshooting → Video previews don't play](Troubleshooting.md).

## Where to next

- [Artwork](Artwork.md) — how the artwork gallery + video tile compose
- [View Modes → Cover Flow](View-Modes.md#cover-flow-view) — where
  videos take the full viewport
- [Sidebar & Details Pane](Sidebar-and-Details-Pane.md) — where the
  embedded player lives
- [Splash & Now Playing](Splash-and-Now-Playing.md) — startup video
  is a separate, single-shot mechanism

## For developers

- Player widget: [src/ui/widgets/videopreviewwidget.h](../../src/ui/widgets/).
  Wraps `QMediaPlayer` + `QVideoSink` for sidebar embedding.
- Cover Flow video integration: in the Cover Flow view engine under
  [src/modules/scroll/](../../src/modules/scroll/) — the center tile
  delegate paints the `QVideoSink` frame instead of the artwork
  pixmap.
- Volume binding: `MainWindow::onPreviewVolumeChanged` writes
  `previewVideoVolume` and broadcasts to all live `QMediaPlayer`
  instances.
- Selection-change pause: `DetailsPaneManager` pauses the previous
  player and starts the new one when `SelectionManager::selectionChanged`
  fires.
- Loop: `QMediaPlayer::setLoops(QMediaPlayer::Infinite)`.
- Adding a new "preview" media type (e.g. animated GIFs as previews):
  extend the auto-discovery list in `videoutils.h` and the player
  widget; gallery already iterates dynamically over found types.
