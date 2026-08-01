// Async video thumbnail extractor.
#include "videothumbnailextractor.h"

#include <QAudioOutput>
#include <QCoreApplication>
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

// Kartend-i9ydp: LRU bound on the frame cache. Each entry costs 1, so this is
// a cap on the number of cached frames (incl. null-failure markers) — enough
// to keep a scrolled video-heavy collection warm without growing for the
// process lifetime.
constexpr int kMaxCachedThumbnails = 256;

} // namespace

VideoThumbnailExtractor *VideoThumbnailExtractor::instance() {
  static VideoThumbnailExtractor s_instance;
  return &s_instance;
}

VideoThumbnailExtractor::VideoThumbnailExtractor() {
  m_cache.setMaxCost(kMaxCachedThumbnails);
  // QMediaPlayer / QAudioOutput / QVideoSink + the timeout timer are
  // constructed lazily on the first requestFrame() call; see
  // ensureMediaPipeline(). Eager construction would spin up the
  // QtMultimedia + PulseAudio backend at first DetailPageOverlay
  // instantiation (which happens during MainWindow setup), even when no
  // thumbnails are ever requested — wasteful in normal use, and a source
  // of TSan-flagged libgstreamer races / Release+gcc SIGILL on CI runners
  // without a real audio device.
}

void VideoThumbnailExtractor::ensureMediaPipeline() {
  if (m_player || m_pipelineRetired) {
    return;
  }
  // First pipeline construction: arrange for the media objects to be torn
  // down on aboutToQuit, while QApplication and the QtMultimedia backend
  // still exist. This object is a function-local static — without the early
  // teardown, the QMediaPlayer / QAudioOutput / QVideoSink children would be
  // destroyed during static destruction, after the application is gone,
  // which the multimedia backends do not survive. Context = this (the
  // singleton), so the connection's lifetime is well-defined.
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
          [this]() { teardownMediaPipeline(); });
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

  m_timeoutTimer.setSingleShot(true);
  m_timeoutTimer.setInterval(m_timeoutMs);
  connect(&m_timeoutTimer, &QTimer::timeout, this, [this]() { finishCurrent({}); });
}

void VideoThumbnailExtractor::setExtractionTimeoutMs(int ms) {
  m_timeoutMs = qBound(1000, ms, 30000);
  // The pipeline may not have been built yet; ensureMediaPipeline() reads
  // m_timeoutMs at construction time, so deferred extractors pick up the
  // value automatically. Update an existing timer in-place so a user nudging
  // the spinbox sees the change on the next request.
  m_timeoutTimer.setInterval(m_timeoutMs);
}

void VideoThumbnailExtractor::teardownMediaPipeline() {
  m_pipelineRetired = true;
  m_timeoutTimer.stop();
  m_queue.clear();
  m_currentPath.clear();
  m_seekedForCurrent = false;
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
    // Detach the sink and audio output before deleting them so the player
    // never holds a pointer to a dead object, even transiently.
    m_player->setVideoSink(nullptr);
    m_player->setAudioOutput(nullptr);
  }
  delete m_player;
  m_player = nullptr;
  delete m_sink;
  m_sink = nullptr;
  delete m_audioOutput;
  m_audioOutput = nullptr;
}

VideoThumbnailExtractor::~VideoThumbnailExtractor() {
  // Normally a no-op: teardownMediaPipeline() already destroyed the media
  // members on aboutToQuit, leaving this static-destruction-time destructor
  // trivial. The guarded stop covers the exotic path where the process exits
  // without QCoreApplication ever emitting aboutToQuit.
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }
}

QPixmap VideoThumbnailExtractor::cached(const QString &videoPath) const {
  const QPixmap *pix = m_cache.object(videoPath);
  return pix ? *pix : QPixmap();
}

bool VideoThumbnailExtractor::hasCacheEntry(const QString &videoPath) const {
  return m_cache.contains(videoPath);
}

void VideoThumbnailExtractor::requestFrame(const QString &videoPath) {
  if (videoPath.isEmpty()) {
    return;
  }

  if (const QPixmap *cachedPix = m_cache.object(videoPath)) {
    const QPixmap pix = *cachedPix;
    // Emit asynchronously even on a cache hit so callers see the same
    // sync/async signal contract as a miss — otherwise a slot connected
    // with AutoConnection that mutates the gallery (e.g. adds another tile)
    // would reenter requestFrame() before this stack frame unwinds.
    QTimer::singleShot(0, this, [this, videoPath, pix]() { emit frameReady(videoPath, pix); });
    return;
  }

  // Post-quit: the media pipeline was torn down on aboutToQuit and must not
  // be resurrected — uncached requests quietly no-op from here on.
  if (m_pipelineRetired) {
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
  ensureMediaPipeline();
  if (!m_player) {
    // Retired pipeline (teardown ran between enqueue and this deferred
    // call): nothing can serve the queue anymore, drop it.
    m_queue.clear();
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
  m_timeoutTimer.start();
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
  m_timeoutTimer.stop();
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }

  const QString path = m_currentPath;
  m_currentPath.clear();
  m_seekedForCurrent = false;

  // Cache even null pixmaps so failed extractions don't keep retrying on
  // every selection change. Kartend-i9ydp: the bounded QCache owns each entry
  // and evicts the least-recently-used once kMaxCachedThumbnails is exceeded,
  // so a transient failure no longer sticks for the whole process lifetime —
  // it just ages out like any other entry.
  m_cache.insert(path, new QPixmap(pixmap));
  emit frameReady(path, pixmap);

  if (!m_queue.isEmpty()) {
    // Yield between items so the event loop drains frameReady connections
    // (gallery-tile repaints) before we tear down the QMediaPlayer for the
    // next extraction — back-to-back synchronous extraction starved the UI
    // thread of paint events on populous galleries.
    QTimer::singleShot(0, this, [this]() { processNext(); });
  }
}
