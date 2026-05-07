#ifndef BACKGROUNDVIDEOWIDGET_H
#define BACKGROUNDVIDEOWIDGET_H

#include <QImage>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QHideEvent;
class QMediaPlayer;
class QShowEvent;
class QVideoFrame;
class QVideoSink;
QT_END_NAMESPACE

/// Kartend-vbs: looping muted video painted to fill the items viewport, used
/// as a per-collection background. Renders frames via QVideoSink into the
/// widget's own paintEvent (cover-fit, KeepAspectRatioByExpanding) so the
/// video covers the rect regardless of source aspect ratio. Transparent for
/// mouse events so the items grid layered above stays interactive.
///
/// Distinct from VideoPreviewWidget (which is sidebar/overlay-shaped, plays
/// audio, and uses a QLabel pipeline tuned for letterboxed previews).
class BackgroundVideoWidget : public QWidget {
  Q_OBJECT
public:
  explicit BackgroundVideoWidget(QWidget *parent = nullptr);
  ~BackgroundVideoWidget() override;

  /// Begin (or restart) playback of the video at @p filePath. Empty path is
  /// equivalent to clear(). Calling with the same path while already playing
  /// is a no-op.
  void setVideoPath(const QString &filePath);

  /// Stop playback and release the QMediaPlayer source.
  void clear();

  [[nodiscard]] QString currentVideoPath() const { return m_currentPath; }

protected:
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QMediaPlayer *m_player = nullptr;
  QVideoSink *m_sink = nullptr;
  // Last decoded frame kept as a CPU image so paintEvent can re-scale on
  // resize without waiting for the next frame from the decoder.
  QImage m_currentImage;
  QString m_currentPath;
};

#endif // BACKGROUNDVIDEOWIDGET_H
