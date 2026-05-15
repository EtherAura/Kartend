#ifndef MARQUEEWINDOW_H
#define MARQUEEWINDOW_H

#include <QPixmap>
#include <QStackedLayout>
#include <QString>
#include <QUrl>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QScreen;
class QVideoWidget;
QT_END_NAMESPACE

/// Secondary-monitor "marquee" / topper window. Frameless, fullscreen on
/// a user-chosen QScreen, displays a single pixmap or a looping video
/// centred + uniformly scaled to fit. The window swallows nothing — it
/// doesn't accept focus or input, so the user driving the main Kartend
/// window on the primary monitor isn't interrupted by it.
///
/// Three artwork sources are supported:
///   - **Item mode** (mode 0): the cover of the currently-selected item.
///   - **Collection mode** (mode 1): the current collection's icon.
///   - **Video/attract mode** (mode 2): a looping, muted video. The
///     caller picks the video source via `setVideo` (typically item's
///     videoPreview, falling back to the collection's backgroundVideo).
class MarqueeWindow : public QWidget {
  Q_OBJECT

public:
  /// Constructs the window on `targetScreen` (use the primary screen
  /// when nullptr). The window is created but not shown — call `show()`
  /// or `pinToScreen()` to make it visible. Parent is intentionally
  /// nullptr so the window is a true top-level on its own screen.
  explicit MarqueeWindow(QScreen *targetScreen = nullptr);
  ~MarqueeWindow() override;

  /// Move + resize to the given screen's geometry and show. Falls back
  /// to the primary screen when `screen` is null (e.g. the configured
  /// screen was unplugged after settings were saved).
  void pinToScreen(QScreen *screen);

  /// Replace the displayed pixmap and switch to image rendering. An
  /// empty / null pixmap clears the window to its background colour —
  /// useful as a "no artwork yet" placeholder while the user navigates
  /// to an item that has none. Stops any active video playback.
  void setPixmap(const QPixmap &pixmap);

  /// Replace the playing video and switch to video rendering. An empty
  /// `videoPath` stops playback and clears to background. Loops
  /// indefinitely while playing; muted (no audio output).
  void setVideo(const QString &videoPath);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  /// Re-render `m_sourcePixmap` into the label at the current widget
  /// size, preserving aspect ratio. Called from setPixmap() and from
  /// resizeEvent() so window-size changes don't squash the image.
  void rescaleLabel();

  /// Lazily build the QMediaPlayer + QVideoWidget plumbing the first
  /// time setVideo() is called with a non-empty path. Keeps the cold-
  /// start cost of image-only marquees zero (no Qt6::Multimedia
  /// instantiation when the user never picks the video mode).
  void ensureVideoWidget();

  /// Index sentinels for the stacked layout. Image and video modes
  /// each own a child widget; the active one is raised by setPixmap /
  /// setVideo.
  static constexpr int STACK_IMAGE = 0;
  static constexpr int STACK_VIDEO = 1;

  QStackedLayout *m_stack = nullptr;
  QLabel *m_label = nullptr;
  QPixmap m_sourcePixmap;

  QVideoWidget *m_videoWidget = nullptr;
  QMediaPlayer *m_mediaPlayer = nullptr;
  /// Audio output kept alive but volume zeroed. QMediaPlayer leaks
  /// audio device handles when constructed without a sink, so we
  /// instantiate one and mute it rather than skipping audio entirely.
  QAudioOutput *m_audioOutput = nullptr;
  /// Source we're currently playing (or empty when stopped). Used by
  /// the looping handler so we can re-set the source on EndOfMedia
  /// without holding the path elsewhere.
  QString m_currentVideoPath;
};

#endif // MARQUEEWINDOW_H
