#ifndef HEADERLOGOOVERLAY_H
#define HEADERLOGOOVERLAY_H

#include "collectionutils.h"
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QHideEvent;
class QMediaPlayer;
class QMovie;
class QPaintEvent;
class QShowEvent;
class QVideoFrame;
class QVideoSink;
QT_END_NAMESPACE

/// per-collection toolbar logo. Lives as a real layout member
/// of the items top bar so the chosen anchor — leftmost / center / rightmost
/// — pushes neighbouring controls aside rather than overlapping them.
///
/// Three media kinds are supported transparently behind a single setLogo()
/// call:
///   - Static images (PNG, JPG, BMP, SVG, etc.) — QPixmap.
///   - Animated containers (GIF, animated WebP, APNG, MNG) — QMovie.
///   - Video files (mp4, webm, mkv, mov, avi, m4v) — muted, infinite-looping
///     QMediaPlayer with a QVideoSink. No QAudioOutput is attached so the
///     decoder doesn't spool up an audio stream we'd just have to mute.
///
/// All three kinds funnel into the same paint pipeline: each new frame
/// updates `m_pixmap` and triggers an `update()`. sizeHint() reports a
/// width sized so the logo fits the toolbar's interior height while
/// preserving aspect ratio.
class HeaderLogoOverlay : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(HeaderLogoOverlay)
public:
  explicit HeaderLogoOverlay(QWidget *parent = nullptr);
  ~HeaderLogoOverlay() override;

  /// Set the logo source and remember the chosen anchor. NavigationManager
  /// uses position() to decide where to insert the widget in the toolbar
  /// layout. Empty path clears the logo.
  void setLogo(const QString &filePath, HeaderLogoPosition position);

  void clear();

  [[nodiscard]] QString currentPath() const { return m_currentPath; }
  [[nodiscard]] HeaderLogoPosition position() const { return m_position; }

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override { return QSize(0, 0); }

protected:
  void paintEvent(QPaintEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  enum class MediaKind { None, Static, Animated, Video };

  static MediaKind detectKind(const QString &path);
  void loadStatic(const QString &path);
  void loadAnimated(const QString &path);
  void loadVideo(const QString &path);
  void teardownMedia();
  void onVideoFrame(const QVideoFrame &frame);

  // Latest frame to paint. For Static this is the loaded image; for
  // Animated/Video it's whatever the underlying decoder last delivered.
  QPixmap m_pixmap;
  QString m_currentPath;
  HeaderLogoPosition m_position = HeaderLogoPosition::TopCenter;
  MediaKind m_kind = MediaKind::None;
  // Natural source size — drives sizeHint() so the toolbar layout reserves
  // a width that matches the source's aspect ratio.
  QSize m_naturalSize;

  QMovie *m_movie = nullptr;
  QMediaPlayer *m_player = nullptr;
  QVideoSink *m_sink = nullptr;
};

#endif // HEADERLOGOOVERLAY_H
