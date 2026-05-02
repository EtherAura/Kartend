#ifndef VIDEOPREVIEWWIDGET_H
#define VIDEOPREVIEWWIDGET_H

#include <QImage>
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
 * (MetadataSidebar uses 500ms) governs how often it kicks in. Stops
 * playback automatically on hide and on destruction.
 */
class VideoPreviewWidget : public QWidget {
  Q_OBJECT
public:
  explicit VideoPreviewWidget(QWidget *parent = nullptr);
  ~VideoPreviewWidget() override;

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
};

#endif // VIDEOPREVIEWWIDGET_H
