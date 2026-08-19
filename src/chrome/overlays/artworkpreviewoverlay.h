#ifndef ARTWORKPREVIEWOVERLAY_H
#define ARTWORKPREVIEWOVERLAY_H

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QImage;
class QLabel;
class QPushButton;
class QWheelEvent;
template <typename T> class QFutureWatcher;
QT_END_NAMESPACE

class OverlayZOrderRegistry;
class VideoPreviewWidget;

/**
 * @brief Modal overlay widget that displays artwork or video for a selected
 * item in a centered popup.
 *
 * Originally artwork-only; extended to host a
 * looping muted video preview as well. The two display modes are mutually
 * exclusive — switching media stops/clears the inactive widget. The overlay
 * covers the parent widget and dismisses on Escape, click-outside, or the
 * close button.
 */
class ArtworkPreviewOverlay : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ArtworkPreviewOverlay)

public:
  explicit ArtworkPreviewOverlay(QWidget *parent = nullptr);
  ~ArtworkPreviewOverlay() override;

  /// One entry in the overlay's thumb strip — the same shape as the
  /// sidebar gallery uses. Set via `setGalleryEntries` after showing
  /// the overlay so Left/Right + thumb clicks can cycle the main
  /// preview between scraped artwork types without dismissing.
  struct GalleryEntry {
    QString label;
    QString path;
    bool isVideo = false;
  };

  /// Populate the overlay's bottom thumb strip with the related
  /// artworks for the currently-previewed item. An empty list hides
  /// the strip entirely (single-image preview, no related media to
  /// cycle through). Called by the caller right after one of the
  /// show*() methods so the strip's entries are in lockstep with
  /// the main preview.
  void setGalleryEntries(const QList<GalleryEntry> &entries);

  /// Show the overlay with artwork for the given file path.
  void showArtworkForFile(const QString &filePath, const QString &artworkDirectory);

  /// Show the overlay with the artwork file at the given absolute path.
  /// Used by the sidebar artwork gallery when the path was
  /// resolved by ItemArtworkStore and there is no need to re-search a
  /// directory. `launchRequested` is still emitted on Enter / double-click;
  /// callers that only want preview behavior should leave that signal
  /// unconnected.
  void showArtworkAtPath(const QString &absoluteArtworkPath);

private:
  /// Render the grid's hatched placeholder, titled from @p filePath, when
  /// an item has no artwork to show (user request 2026-08-18).
  void showPlaceholderForFile(const QString &filePath);
  /// True when this wheel event begins a NEW gesture (the wheel has been
  /// still long enough). Always updates the gesture clock, so a coasting
  /// stream keeps returning false until it stops.
  [[nodiscard]] bool wheelGestureIsNew();

public:
  /// Show the overlay playing the video at @p absoluteVideoPath. The video
  /// loops muted at ~80% of the parent size. Closing the overlay (Escape,
  /// click-outside, or the close button) stops playback.
  void showVideoAtPath(const QString &absoluteVideoPath);

  /// Convenience entry point for video-first preview. Tries
  /// VideoUtils::findVideoForFile() against @p videoDirectory first; falls
  /// back to artwork lookup if no video is found. Returns true if either
  /// medium was shown, false if neither resolved.
  bool showMediaForFile(const QString &filePath, const QString &artworkDirectory,
                        const QString &videoDirectory);

  /// Hide the overlay and stop any video playback.
  void hideOverlay();

  /// toggle the overlay's preview video between playing and
  /// paused. Returns true if a toggle occurred (i.e. video is loaded and
  /// active), false otherwise so callers can fall back to other handlers.
  bool togglePreviewVideoPause();

  /// File path of the currently-previewed item, or empty if none. Set by
  /// showArtworkForFile and showMediaForFile (gallery-style showAtPath
  /// callers leave it empty since they don't have an associated file).
  [[nodiscard]] QString currentFilePath() const { return m_currentFilePath; }

  /// See OverlayZOrderRegistry. Routes displayPixmap / displayVideo raise()
  /// through the central z-order coordinator when installed.
  void setLayerManager(OverlayZOrderRegistry *manager) { m_layerManager = manager; }

signals:
  /// Emitted when the user activates (Enter / double-click) while the
  /// overlay is visible. Used by expand-mode to launch the previewed item
  /// on the second activation. @p filePath is the path of the item the
  /// overlay is displaying (may be empty for gallery thumbnail previews
  /// that don't carry an associated media path).
  void launchRequested(const QString &filePath);

  /// Emitted instead of wrapping when the user cycles past the first or
  /// last artwork (user decision 2026-08-18: "instead of looping the
  /// selected item's artwork ... go back/next to the previous/next item").
  /// The overlay itself has no notion of the item list, so the interaction
  /// layer decides what stepping means. @p direction is -1 or +1.
  void galleryBoundaryReached(int direction);

  /// bug #7: emitted from showEvent / hideEvent so consumers
  /// (specifically DetailsPaneManager) can lower the sidebar while the overlay
  /// is on top. Without this, raise() calls during the overlay's lifetime
  /// (e.g. window resize triggers updateSidebarLayout) would re-stack the
  /// sidebar above the overlay.
  void visibilityChanged(bool visible);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  QLabel *m_artworkLabel = nullptr;
  QPushButton *m_closeButton = nullptr;
  VideoPreviewWidget *m_videoPreview = nullptr;
  // Whichever of m_artworkLabel / m_videoPreview is currently displaying
  // content. Drives geometry centering, click-hit testing, and the close
  // button's anchor.
  QWidget *m_displayWidget = nullptr;
  QString m_currentFilePath;
  /// Absolute path of the artwork / video file currently displayed in the
  /// main preview. Distinct from m_currentFilePath, which is the source
  /// *media* file (the game / video / audio the overlay was opened for).
  /// Used so setGalleryEntries() can snap m_galleryIndex to whichever
  /// entry is already on screen, making the first Left/Right key step
  /// land on the adjacent entry rather than position 0.
  QString m_currentArtworkPath;
  /// Bottom-anchored thumb strip + scroll container. Populated via
  /// `setGalleryEntries`. m_galleryEntries holds the source data;
  /// m_galleryStrip is its visible container (or null when no
  /// entries — strip is hidden in that state).
  class QWidget *m_galleryStrip = nullptr;
  class QHBoxLayout *m_galleryLayout = nullptr;
  class QScrollArea *m_galleryScroll = nullptr;
  QList<GalleryEntry> m_galleryEntries;
  /// Index into m_galleryEntries of whichever artwork is currently
  /// shown as the main preview. -1 when the preview is a one-shot
  /// (e.g. opened via the old sidebar gallery click path that didn't
  /// supply entries). Cycle wraps modulo m_galleryEntries.size().
  int m_galleryIndex = -1;
  /// Wheel rate limit (user request 2026-08-18): ONE artwork per gesture,
  /// however hard the wheel is spun. A single physical flick emits a burst
  /// of wheel events — high-resolution wheels and trackpads especially —
  /// and honouring each one skated through the whole gallery. Ticks inside
  /// the cooldown are swallowed, not queued, so the artwork never keeps
  /// moving after the user stops.
  QElapsedTimer m_wheelCooldown;
  OverlayZOrderRegistry *m_layerManager = nullptr;
  /// In-flight main-preview decode (mirrors MarqueeController's watcher).
  /// Null when no decode is pending. A newer request supersedes the current
  /// one via cancelPreviewLoad(), so rapid Left/Right cycling never queues a
  /// backlog — the last request wins.
  QFutureWatcher<QImage> *m_previewLoadWatcher = nullptr;

  void setupUI();
  void ensureVideoPreview();
  void centerContent();
  /// Decode the image at @p artworkPath off the GUI thread (at display size)
  /// and hand the result to displayPixmap when it lands. On the initial open
  /// the overlay appears immediately with a loading placeholder; while
  /// cycling, the previous image/video stays up until the new decode lands.
  void startPreviewLoad(const QString &artworkPath);
  /// Discard any in-flight main-preview decode so its late result can't
  /// overwrite a newer request or re-show a closed overlay.
  void cancelPreviewLoad();
  /// Present the overlay with a neutral "Loading…" box while the first decode
  /// runs — the click responds instantly instead of blocking on the decode.
  void showLoadingPlaceholder();
  /// Shared presentation tail: cover the parent, center content, show, raise
  /// through the layer manager, and take focus.
  void presentOverlay();
  void displayPixmap(const QPixmap &pixmap);
  void displayVideo(const QString &absoluteVideoPath);
  /// Swap the main preview to the given gallery entry (image → load
  /// pixmap into m_artworkLabel; video → playVideo on m_videoPreview)
  /// and updates m_galleryIndex. No-op when index is out of range.
  void showGalleryEntry(int index);
  void rebuildGalleryStrip();
};

#endif // ARTWORKPREVIEWOVERLAY_H
