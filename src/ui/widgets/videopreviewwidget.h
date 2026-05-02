#ifndef VIDEOPREVIEWWIDGET_H
#define VIDEOPREVIEWWIDGET_H

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QHideEvent;
class QMediaPlayer;
class QShowEvent;
class QVideoWidget;
QT_END_NAMESPACE

/**
 * @brief Widget that plays a single, looping, muted preview video.
 *
 * Owns one QMediaPlayer + QVideoWidget and reuses them across calls to
 * playVideo(). Stops playback automatically on hide and on destruction.
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

private:
  QMediaPlayer *m_player = nullptr;
  QAudioOutput *m_audioOutput = nullptr;
  QVideoWidget *m_videoWidget = nullptr;
  QString m_currentPath;
};

#endif // VIDEOPREVIEWWIDGET_H
