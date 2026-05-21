#ifndef VIDEOTHUMBNAILEXTRACTOR_H
#define VIDEOTHUMBNAILEXTRACTOR_H

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QString>

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QMediaPlayer;
class QTimer;
class QVideoSink;
QT_END_NAMESPACE

/**
 * @brief Async extractor for video thumbnail frames.
 *
 * Renders a single frame from a video file (~1s in) and delivers it as a
 * QPixmap via the frameReady signal. Results are cached in-memory by absolute
 * path so re-requesting the same video returns instantly. Cached requests
 * still emit frameReady (deferred via singleShot) so callers can connect
 * before requesting and handle both paths uniformly.
 *
 * One QMediaPlayer is shared across the application; concurrent requests are
 * serialized through an internal queue. This avoids contention with the
 * sidebar's own VideoPreviewWidget and keeps the surface area small.
 */
class VideoThumbnailExtractor : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(VideoThumbnailExtractor)
public:
  static VideoThumbnailExtractor *instance();

  /// Returns a cached frame for @p videoPath, or an empty pixmap on miss.
  /// A cached *null* pixmap means a previous extraction failed; callers
  /// should fall back to a placeholder rather than re-requesting.
  [[nodiscard]] QPixmap cached(const QString &videoPath) const;

  /// Returns true when @p videoPath has a cache entry (even if the cached
  /// pixmap is null — that records a prior failure so we don't retry).
  [[nodiscard]] bool hasCacheEntry(const QString &videoPath) const;

  /// Asynchronously extract a frame for @p videoPath. Emits frameReady when
  /// done. Subsequent requests for the same path while one is in flight are
  /// coalesced. If the path is already cached, frameReady is emitted on the
  /// next event loop tick so callers can connect first.
  void requestFrame(const QString &videoPath);

  /// Reconfigure the per-extraction timeout. Used by the settings dialog so
  /// users on slow systems can extend the default 4s window. Applies to the
  /// next extraction; an in-flight request keeps its current timer interval.
  /// @p ms is clamped to [1000, 30000] to mirror the settings-loader bounds.
  void setExtractionTimeoutMs(int ms);

signals:
  /// Emitted when extraction completes for @p videoPath. @p pixmap is null
  /// on failure (file missing, decoder error, timeout).
  void frameReady(const QString &videoPath, const QPixmap &pixmap);

private:
  VideoThumbnailExtractor();
  ~VideoThumbnailExtractor() override;

  // See videothumbnailextractor.cpp — defers QtMultimedia backend init
  // until the first frame request actually arrives.
  void ensureMediaPipeline();
  void processNext();
  void onMediaStatusChanged(int status);
  void onVideoFrame();
  void onPlayerError();
  void finishCurrent(const QPixmap &pixmap);

  QMediaPlayer *m_player = nullptr;
  QAudioOutput *m_audioOutput = nullptr;
  QVideoSink *m_sink = nullptr;
  QHash<QString, QPixmap> m_cache;
  QQueue<QString> m_queue;
  QString m_currentPath;
  bool m_seekedForCurrent = false;
  int m_timeoutMs = 4000;
  QTimer *m_timeoutTimer = nullptr;
};

#endif // VIDEOTHUMBNAILEXTRACTOR_H
