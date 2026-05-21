#ifndef STARTUPVIDEOOVERLAY_H
#define STARTUPVIDEOOVERLAY_H

#include <QImage>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QHideEvent;
class QKeyEvent;
class QLabel;
class QMediaPlayer;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QVideoFrame;
class QVideoSink;
QT_END_NAMESPACE

/**
 * @brief One-shot fullscreen startup video overlay.
 *
 * Plays a configurable startup clip on top of the main window when the
 * application launches. Dismisses on any key press, mouse click, or when
 * the clip reaches end-of-media. Unlike VideoPreviewWidget the player is
 * non-looping and the volume tracks the same global slider so the user
 * isn't surprised by full-blast audio at startup.
 *
 * Lives only as long as it's needed — emit dismissed() and the parent
 * deletes it. Self-deletes if no observer connects, by calling
 * deleteLater() in dismiss().
 */
class StartupVideoOverlay : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(StartupVideoOverlay)
public:
  explicit StartupVideoOverlay(QWidget *parent = nullptr);
  ~StartupVideoOverlay() override;

  /// Begin playback of the video at @p absolutePath. Returns true if the
  /// player accepted the source (path resolves to an existing file); false
  /// when the path is empty or unreadable so the caller can skip showing
  /// an empty black overlay.
  bool playVideo(const QString &absolutePath);

signals:
  /// Emitted exactly once when the overlay decides it should go away —
  /// either the user dismissed it or the clip ended naturally. The widget
  /// schedules its own deleteLater() after this signal fires; observers
  /// must not call delete on it.
  void dismissed();

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void renderFrame(const QVideoFrame &frame);
  void paintCurrentImageScaled();
  void dismiss();

  QMediaPlayer *m_player = nullptr;
  QAudioOutput *m_audioOutput = nullptr;
  QVideoSink *m_sink = nullptr;
  QLabel *m_imageLabel = nullptr;
  QImage m_currentImage;
  bool m_dismissed = false;
};

#endif // STARTUPVIDEOOVERLAY_H
