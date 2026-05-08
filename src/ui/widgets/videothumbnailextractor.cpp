// Async video thumbnail extractor.
#include "videothumbnailextractor.h"

#include <QAudioOutput>
#include <QFile>
#include <QImage>
#include <QMediaPlayer>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace {
// Seek roughly past the typical title-card / black-frame intro. Short videos
// fall back to a quarter of their duration in onMediaStatusChanged.
constexpr int kSeekPositionMs = 1000;

// Hard cap per-extraction so a stuck decode never blocks the queue. The
// timer fires in finishCurrent({}) which advances to the next request.
constexpr int kExtractionTimeoutMs = 4000;
} // namespace

VideoThumbnailExtractor *VideoThumbnailExtractor::instance() {
  static VideoThumbnailExtractor s_instance;
  return &s_instance;
}

VideoThumbnailExtractor::VideoThumbnailExtractor() {
  m_player = new QMediaPlayer(this);
  m_audioOutput = new QAudioOutput(this);
  m_audioOutput->setMuted(true);
  m_audioOutput->setVolume(0.0);
  m_player->setAudioOutput(m_audioOutput);

  m_sink = new QVideoSink(this);
  m_player->setVideoSink(m_sink);

  connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus s) { onMediaStatusChanged(static_cast<int>(s)); });
  connect(m_player, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString &) { onPlayerError(); });
  connect(m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &) { onVideoFrame(); });

  m_timeoutTimer = new QTimer(this);
  m_timeoutTimer->setSingleShot(true);
  m_timeoutTimer->setInterval(kExtractionTimeoutMs);
  connect(m_timeoutTimer, &QTimer::timeout, this, [this]() { finishCurrent({}); });
}

VideoThumbnailExtractor::~VideoThumbnailExtractor() {
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }
}

QPixmap VideoThumbnailExtractor::cached(const QString &videoPath) const {
  return m_cache.value(videoPath);
}

bool VideoThumbnailExtractor::hasCacheEntry(const QString &videoPath) const {
  return m_cache.contains(videoPath);
}

void VideoThumbnailExtractor::requestFrame(const QString &videoPath) {
  if (videoPath.isEmpty()) {
    return;
  }

  if (m_cache.contains(videoPath)) {
    const QPixmap pix = m_cache.value(videoPath);
    QTimer::singleShot(0, this, [this, videoPath, pix]() { emit frameReady(videoPath, pix); });
    return;
  }

  if (videoPath == m_currentPath || m_queue.contains(videoPath)) {
    return;
  }

  m_queue.enqueue(videoPath);
  if (m_currentPath.isEmpty()) {
    processNext();
  }
}

void VideoThumbnailExtractor::processNext() {
  if (m_queue.isEmpty()) {
    return;
  }
  m_currentPath = m_queue.dequeue();
  m_seekedForCurrent = false;

  if (!QFile::exists(m_currentPath)) {
    finishCurrent({});
    return;
  }

  m_player->stop();
  m_player->setSource(QUrl::fromLocalFile(m_currentPath));
  m_timeoutTimer->start();
}

void VideoThumbnailExtractor::onMediaStatusChanged(int status) {
  if (m_currentPath.isEmpty()) {
    return;
  }
  // LoadedMedia / BufferedMedia means we have enough buffered to seek + play
  // a frame. Earlier statuses (LoadingMedia, StalledMedia) still need time.
  if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
    if (!m_seekedForCurrent) {
      m_seekedForCurrent = true;
      const qint64 dur = m_player->duration();
      const qint64 seek =
          (dur > 0 && dur < kSeekPositionMs * 2) ? (dur / 4) : qint64{kSeekPositionMs};
      m_player->setPosition(seek);
      m_player->play();
    }
  } else if (status == QMediaPlayer::InvalidMedia) {
    finishCurrent({});
  }
}

void VideoThumbnailExtractor::onVideoFrame() {
  if (m_currentPath.isEmpty() || !m_seekedForCurrent || !m_sink) {
    return;
  }
  const QVideoFrame frame = m_sink->videoFrame();
  if (!frame.isValid()) {
    return;
  }
  QImage image = frame.toImage();
  if (image.isNull()) {
    return;
  }
  finishCurrent(QPixmap::fromImage(image));
}

void VideoThumbnailExtractor::onPlayerError() {
  if (!m_currentPath.isEmpty()) {
    finishCurrent({});
  }
}

void VideoThumbnailExtractor::finishCurrent(const QPixmap &pixmap) {
  if (m_currentPath.isEmpty()) {
    return;
  }
  m_timeoutTimer->stop();
  m_player->stop();
  m_player->setSource(QUrl());

  const QString path = m_currentPath;
  m_currentPath.clear();
  m_seekedForCurrent = false;

  // Cache even null pixmaps so failed extractions don't keep retrying on
  // every selection change. The cost is that a transient failure (e.g. file
  // briefly inaccessible) sticks until the application restarts; acceptable
  // for an initial implementation.
  m_cache.insert(path, pixmap);
  emit frameReady(path, pixmap);

  if (!m_queue.isEmpty()) {
    QTimer::singleShot(0, this, [this]() { processNext(); });
  }
}
