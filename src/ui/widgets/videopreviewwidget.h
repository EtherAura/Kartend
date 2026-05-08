#ifndef VIDEOPREVIEWWIDGET_H
#define VIDEOPREVIEWWIDGET_H

#include <QImage>
#include <QList>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QHideEvent;
class QLabel;
class QMediaPlayer;
class QShowEvent;
class QVideoFrame;
class QVideoSink;
QT_END_NAMESPACE

/**
 * @brief Widget that plays a single, looping preview video with audio.
 *
 * Owns a QMediaPlayer and a QVideoSink, then paints decoded frames into a
 * plain QLabel via QVideoSink::videoFrameChanged. Going through QLabel
 * (instead of QVideoWidget) avoids Qt6's native-window rendering quirks
 * inside a QScrollArea, where QVideoWidget often plays audio without ever
 * showing video on Wayland / certain GL drivers. Performance is more than
 * adequate for the small sidebar / overlay preview sizes we use.
 *
 * Audio plays at full volume — selection-change debouncing in callers
 * (DetailsPane uses 500ms) governs how often it kicks in. Stops
 * playback automatically on hide and on destruction.
 */
class VideoPreviewWidget : public QWidget {
  Q_OBJECT
public:
  explicit VideoPreviewWidget(QWidget *parent = nullptr);
  ~VideoPreviewWidget() override;

  /**
   * @brief Set the global preview-audio volume.
   *
   * @p percent is clamped to [0, 100]. The value is applied to every live
   * VideoPreviewWidget immediately and stored as the default for any
   * subsequently constructed instances. This is the only audio knob that
   * matters in the app — preview videos are the only video surfaces with
   * a QAudioOutput attached (background video and toolbar logos are silent
   * by design).
   */
  static void setGlobalVolume(int percent);

  /// Read the current global preview-audio volume as a 0-100 percent.
  [[nodiscard]] static int globalVolume() { return s_globalVolume; }

  /**
   * @brief Begin (or restart) playback of the video at @p filePath.
   *
   * The same path may be passed repeatedly; if it matches the currently
   * playing video the call is a no-op.
   */
  void playVideo(const QString &filePath);

  /**
   * @brief Stop playback and clear the current source.
   */
  void stop();

  /**
   * @brief Toggle between playing and paused states.
   *
   * No-op when no source is loaded. Unlike stop(), the source is preserved
   * so playback resumes from the same position. Returns the new paused
   * state (true = now paused, false = now playing).
   */
  bool togglePauseResume();

  /**
   * @brief Whether playback is currently paused.
   *
   * Distinguishes "paused with source loaded" from "stopped / no source"
   * — togglePauseResume only acts on the former.
   */
  [[nodiscard]] bool isPaused() const;

  /**
   * @brief Whether a source is currently loaded (playing OR paused).
   *
   * Lets callers decide whether a pause-toggle keystroke is meaningful
   * for this widget instance.
   */
  [[nodiscard]] bool hasLoadedSource() const { return !m_currentPath.isEmpty(); }

  /**
   * @brief The path of the video currently loaded (empty if none).
   */
  [[nodiscard]] QString currentVideoPath() const { return m_currentPath; }

protected:
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void renderFrame(const QVideoFrame &frame);
  void paintCurrentImageScaled();
  // Lazy-construct the QMediaPlayer/QAudioOutput/QVideoSink chain. Deferred
  // out of the constructor so widgets that are never asked to play (the
  // common case in DetailsPane / CoverflowWidget when the user has no
  // preview videos) don't spin up the QtMultimedia + PulseAudio backend
  // at all — saves threads, avoids a TSan-flagged libpulsecommon race in
  // tests, and dodges a Qt6 / gcc-Release SIGILL we hit on CI runners
  // without a real audio device.
  void ensureMediaPipeline();

  QMediaPlayer *m_player = nullptr;
  QAudioOutput *m_audioOutput = nullptr;
  QVideoSink *m_sink = nullptr;
  QLabel *m_imageLabel = nullptr;
  // Last decoded frame as a CPU image, kept so resizeEvent can re-scale
  // it without waiting for the next frame to arrive (otherwise the label
  // shows a stale, wrong-aspect-ratio scaled pixmap until playback
  // produces another frame).
  QImage m_currentImage;
  QString m_currentPath;
  // distinguishes user-initiated pause from the visibility
  // pause done by hideEvent(). showEvent() must not silently resume a
  // user-paused video — the user's intent should survive a sidebar
  // collapse/expand cycle.
  bool m_userPaused = false;

  // shared volume state. Each instance pushes the current
  // s_globalVolume into its QAudioOutput at construction; setGlobalVolume()
  // walks s_instances to update every live widget when the toolbar slider
  // moves. The list is the only registry — there's no signal indirection.
  void applyGlobalVolume();
  static int s_globalVolume;
  static QList<VideoPreviewWidget *> s_instances;
};

#endif // VIDEOPREVIEWWIDGET_H
