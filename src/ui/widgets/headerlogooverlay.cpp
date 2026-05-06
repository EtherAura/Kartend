// Kartend-guo5: per-collection toolbar logo. Layout member of the items top
// bar — the chosen anchor (TopLeft / TopCenter / TopRight) decides where in
// the QHBoxLayout NavigationManager inserts the widget; the widget itself
// just paints whatever the active source last produced.
//
// Source dispatch by extension:
//   - .mp4 / .webm / .mkv / .mov / .avi / .m4v → QMediaPlayer + QVideoSink
//     (muted, infinite loop). Frames arrive on videoFrameChanged.
//   - .gif / .apng / .webp / .mng → QMovie. Frames arrive on frameChanged.
//   - everything else → QPixmap.
//
// All three paths funnel into m_pixmap + update(); paintEvent paints a
// scaled copy, KeepAspectRatio, centered in whatever rect the layout
// granted.
#include "headerlogooverlay.h"

#include <QFileInfo>
#include <QHideEvent>
#include <QImage>
#include <QMediaPlayer>
#include <QMovie>
#include <QPainter>
#include <QPixmap>
#include <QShowEvent>
#include <QStringList>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace {
// Toolbar interior height ≈ toolbar maxHeight (50) - top/bottom margins
// (10+10) = 30. Cap a touch above that so a slightly taller source isn't
// shrunk awkwardly when there's room.
constexpr int kMaxLogoHeight = 32;

bool extensionMatches(const QString &ext, const QStringList &candidates) {
  for (const QString &c : candidates) {
    if (ext.compare(c, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}
} // namespace

HeaderLogoOverlay::HeaderLogoOverlay(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

HeaderLogoOverlay::~HeaderLogoOverlay() { teardownMedia(); }

HeaderLogoOverlay::MediaKind HeaderLogoOverlay::detectKind(const QString &path) {
  static const QStringList videoExt = {"mp4", "webm", "mkv", "mov", "avi", "m4v"};
  static const QStringList animatedExt = {"gif", "apng", "webp", "mng"};
  const QString ext = QFileInfo(path).suffix();
  if (extensionMatches(ext, videoExt)) {
    return MediaKind::Video;
  }
  if (extensionMatches(ext, animatedExt)) {
    return MediaKind::Animated;
  }
  return MediaKind::Static;
}

void HeaderLogoOverlay::setLogo(const QString &filePath, HeaderLogoPosition position) {
  m_position = position;
  if (filePath.isEmpty()) {
    clear();
    return;
  }
  // Same path AND same kind already loaded → just keep playing whatever's
  // running. Avoids restarting a video/animation every navigation.
  if (filePath == m_currentPath && m_kind != MediaKind::None) {
    return;
  }
  teardownMedia();
  m_currentPath = filePath;
  m_kind = detectKind(filePath);
  switch (m_kind) {
  case MediaKind::Static:
    loadStatic(filePath);
    break;
  case MediaKind::Animated:
    loadAnimated(filePath);
    break;
  case MediaKind::Video:
    loadVideo(filePath);
    break;
  case MediaKind::None:
    break;
  }
  updateGeometry();
  update();
}

void HeaderLogoOverlay::clear() {
  teardownMedia();
  m_currentPath.clear();
  m_kind = MediaKind::None;
  m_pixmap = QPixmap();
  m_naturalSize = QSize();
  updateGeometry();
  update();
}

void HeaderLogoOverlay::teardownMedia() {
  if (m_movie) {
    m_movie->stop();
    m_movie->deleteLater();
    m_movie = nullptr;
  }
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
    m_player->deleteLater();
    m_player = nullptr;
  }
  if (m_sink) {
    m_sink->deleteLater();
    m_sink = nullptr;
  }
}

void HeaderLogoOverlay::loadStatic(const QString &path) {
  QPixmap loaded(path);
  if (loaded.isNull()) {
    // Failed load → fall back to clear so the toolbar slot collapses
    // instead of reserving width for a phantom logo.
    m_kind = MediaKind::None;
    m_currentPath.clear();
    return;
  }
  m_pixmap = loaded;
  m_naturalSize = loaded.size();
}

void HeaderLogoOverlay::loadAnimated(const QString &path) {
  m_movie = new QMovie(path, QByteArray(), this);
  if (!m_movie->isValid()) {
    teardownMedia();
    // QMovie can't decode this format despite the extension — try the
    // static path instead so a misnamed PNG-as-gif still renders.
    loadStatic(path);
    if (m_pixmap.isNull()) {
      m_kind = MediaKind::None;
      m_currentPath.clear();
    } else {
      m_kind = MediaKind::Static;
    }
    return;
  }
  // Pull the first frame so we have a natural size for sizeHint() before
  // the animation has had a chance to start emitting frames.
  m_movie->jumpToFrame(0);
  m_pixmap = m_movie->currentPixmap();
  m_naturalSize = m_pixmap.isNull() ? m_movie->frameRect().size() : m_pixmap.size();
  connect(m_movie, &QMovie::frameChanged, this, [this](int) {
    if (m_movie) {
      m_pixmap = m_movie->currentPixmap();
      update();
    }
  });
  if (isVisible()) {
    m_movie->start();
  }
}

void HeaderLogoOverlay::loadVideo(const QString &path) {
  m_player = new QMediaPlayer(this);
  m_sink = new QVideoSink(this);
  m_player->setVideoSink(m_sink);
  m_player->setLoops(QMediaPlayer::Infinite);
  // No QAudioOutput attached on purpose — toolbar logos are silent and the
  // decoder shouldn't spin up an audio path we'd just have to mute.

  connect(m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) { onVideoFrame(frame); });

  m_player->setSource(QUrl::fromLocalFile(path));
  if (isVisible()) {
    m_player->play();
  }
}

void HeaderLogoOverlay::onVideoFrame(const QVideoFrame &frame) {
  if (!frame.isValid() || m_currentPath.isEmpty()) {
    return;
  }
  QImage img = frame.toImage();
  if (img.isNull()) {
    return;
  }
  m_pixmap = QPixmap::fromImage(img);
  if (m_naturalSize != img.size()) {
    // First frame (or a stream-aspect change) — refresh the layout's
    // sizeHint so the toolbar can reserve the right width.
    m_naturalSize = img.size();
    updateGeometry();
  }
  update();
}

QSize HeaderLogoOverlay::sizeHint() const {
  if (!m_naturalSize.isValid() || m_naturalSize.isEmpty()) {
    return QSize(0, 0);
  }
  const int h = qMin(m_naturalSize.height(), kMaxLogoHeight);
  if (m_naturalSize.height() <= 0) {
    return QSize(0, h);
  }
  const int w = static_cast<int>(static_cast<qreal>(m_naturalSize.width()) * h /
                                 static_cast<qreal>(m_naturalSize.height()));
  return QSize(qMax(1, w), qMax(1, h));
}

void HeaderLogoOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  if (m_pixmap.isNull() || rect().isEmpty()) {
    return;
  }
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  QPixmap scaled =
      m_pixmap.scaled(rect().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const int x = (rect().width() - scaled.width()) / 2;
  const int y = (rect().height() - scaled.height()) / 2;
  painter.drawPixmap(x, y, scaled);
}

void HeaderLogoOverlay::hideEvent(QHideEvent *event) {
  // Pause animation/video while hidden so a logo on a non-active stack
  // page doesn't burn decoder cycles in the background.
  if (m_movie && m_movie->state() == QMovie::Running) {
    m_movie->setPaused(true);
  }
  if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState) {
    m_player->pause();
  }
  QWidget::hideEvent(event);
}

void HeaderLogoOverlay::showEvent(QShowEvent *event) {
  if (m_movie) {
    if (m_movie->state() == QMovie::Paused) {
      m_movie->setPaused(false);
    } else if (m_movie->state() == QMovie::NotRunning) {
      m_movie->start();
    }
  }
  if (m_player && m_player->playbackState() != QMediaPlayer::PlayingState &&
      !m_currentPath.isEmpty()) {
    m_player->play();
  }
  QWidget::showEvent(event);
}
