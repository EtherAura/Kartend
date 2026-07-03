// Cover-flow view mode — artwork loading and scaled-pixmap cache.

#include "coverflowwidget.h"

#include "extensionutils.h"
#include "uiconstants/artwork.h"

#include <algorithm>
#include <QColor>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QtConcurrent>

namespace {

// Runs on a QtConcurrent worker, so it must stay in QImage domain — QPixmap
// construction is GUI-thread-only (same contract as the gallery-strip thumb
// decode); the finished slot converts with QPixmap::fromImage.
QImage loadAndScale(const QString &path, int targetSize) {
  // Extension guard: never hand a non-image file (e.g. a scraped .pdf
  // manual) to QImageReader — Qt's PDF image plugin abort()s the process.
  if (path.isEmpty() || !ExtensionUtils::isDecodableImagePath(path)) {
    return {};
  }
  QImageReader reader(path);
  reader.setAutoTransform(true);
  reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
  // Cap the decoded size so very large source images don't waste memory.
  const int decodeSize = targetSize * UIConstants::Artwork::COVERFLOW_DECODE_OVERSAMPLE;
  QSize bounded(decodeSize, decodeSize);
  if (reader.size().isValid() && reader.size().width() > bounded.width()) {
    reader.setScaledSize(reader.size().scaled(bounded, Qt::KeepAspectRatio));
  }
  return reader.read();
}

} // namespace

QPixmap CoverFlowWidget::pixmapForIndex(int idx) {
  if (idx < 0 || idx >= static_cast<int>(m_cards.size())) {
    return {};
  }
  // The centered card honors the gallery override when the active entry is
  // an image; video entries are handled by applyVideoPreviewState which
  // hides the painted card so the QVideoSink-backed preview shows through.
  QString path;
  if (idx == m_selectedIndex && idx == m_galleryOwnerIndex && m_galleryActiveIndex >= 0 &&
      m_galleryActiveIndex < m_gallery.size() && !m_gallery[m_galleryActiveIndex].isVideo) {
    path = m_gallery[m_galleryActiveIndex].path;
  } else {
    path = m_cards[idx].artworkPath;
  }
  if (path.isEmpty()) {
    int sz = cardSize();
    return buildPlaceholderPixmap(sz, sz);
  }
  auto it = m_pixmapCache.constFind(path);
  if (it != m_pixmapCache.constEnd()) {
    return it.value();
  }
  // Schedule async load and return placeholder for now.
  if (path == m_cards[idx].artworkPath) {
    requestArtworkLoad(idx);
  } else {
    // Gallery override path — load it directly into the cache.
    if (!m_pendingLoads.contains(path)) {
      startArtworkLoad(path);
    }
  }
  int sz = cardSize();
  return buildPlaceholderPixmap(sz, sz);
}

void CoverFlowWidget::requestArtworkLoad(int idx) {
  if (idx < 0 || idx >= static_cast<int>(m_cards.size())) {
    return;
  }
  const QString path = m_cards[idx].artworkPath;
  if (path.isEmpty() || m_pendingLoads.contains(path) || m_pixmapCache.contains(path)) {
    return;
  }
  startArtworkLoad(path);
}

void CoverFlowWidget::startArtworkLoad(const QString &path) {
  // Capture path in the finished lambda so we don't have to reverse-look-up
  // the watcher from sender() — QFutureWatcher<T> isn't Q_OBJECT, so a
  // qobject_cast doesn't work and the previous code linearly scanned
  // m_pendingLoads to recover the typed pointer.
  auto *watcher = new QFutureWatcher<QImage>(this);
  m_pendingLoads.insert(path, watcher);
  const int target = cardSize();
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, path]() {
    m_pendingLoads.remove(path);
    const QImage img = watcher->result();
    watcher->deleteLater();
    if (!img.isNull()) {
      // QPixmap conversion happens here on the GUI thread; the worker only
      // ever hands back a QImage.
      m_pixmapCache.insert(path, QPixmap::fromImage(img));
      update();
    }
  });
  watcher->setFuture(QtConcurrent::run([path, target]() { return loadAndScale(path, target); }));
}

void CoverFlowWidget::requestScaledPixmap(const CoverFlowScaledKey &key, const QPixmap &sourcePm,
                                          const QSize &targetSize) {
  if (sourcePm.isNull() || targetSize.isEmpty() || m_pendingScales.contains(key)) {
    return;
  }
  // QPixmap is GUI-thread-only, so snapshot the source as a QImage here
  // (still on the GUI thread) and run the Smooth scale in QImage domain on
  // the worker; the finished slot converts back with QPixmap::fromImage
  // (same QImage-on-worker / QPixmap-on-GUI split as startArtworkLoad above
  // and the gallery-strip thumb decode). The toImage() copy only happens on
  // a cache miss, never in steady-state paint.
  const QImage sourceImg = sourcePm.toImage();
  auto *watcher = new QFutureWatcher<QImage>(this);
  m_pendingScales.insert(key, watcher);
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key]() {
    m_pendingScales.remove(key);
    const QImage scaled = watcher->result();
    watcher->deleteLater();
    if (!scaled.isNull()) {
      m_scaledPixmapCache.insert(key, QPixmap::fromImage(scaled));
      pruneScaledPixmapCache();
      update();
    }
  });
  watcher->setFuture(QtConcurrent::run([sourceImg, targetSize]() {
    return sourceImg.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }));
}

void CoverFlowWidget::pruneScaledPixmapCache() {
  // Bound the scaled cache the same way prunePixmapCache bounds the source
  // cache: drop entries whose path no longer corresponds to any card near
  // the current selection. Without this the cache can grow to thousands of
  // entries as the user scrolls through a large collection (the profile
  // run that motivated Kartend-g6ft saw it climb to 2200 entries
  // ≈ 500MB before any teardown).
  const int budget =
      (kVisibleSideCards * 2 + 1) * UIConstants::Artwork::COVERFLOW_PIXMAP_CACHE_PER_CARD;
  if (static_cast<int>(m_scaledPixmapCache.size()) <= budget) {
    return;
  }
  QSet<QString> keepPaths;
  int from = std::max(0, m_selectedIndex - kVisibleSideCards * 2);
  int to = std::min(static_cast<int>(m_cards.size()) - 1, m_selectedIndex + kVisibleSideCards * 2);
  for (int i = from; i <= to; ++i) {
    keepPaths.insert(m_cards[i].artworkPath);
  }
  for (auto it = m_scaledPixmapCache.begin(); it != m_scaledPixmapCache.end();) {
    // Kartend-el0fr: the key now carries the path directly (no string parse).
    if (!keepPaths.contains(it.key().path)) {
      it = m_scaledPixmapCache.erase(it);
    } else {
      ++it;
    }
  }
}

void CoverFlowWidget::cancelPendingScales() {
  // Disconnect each watcher so its finished lambda doesn't fire a stale
  // insert into m_scaledPixmapCache after we've cleared it (the worker
  // may still be running; we can't cancel QtConcurrent::run, but we can
  // drop the result on the floor).
  for (auto it = m_pendingScales.begin(); it != m_pendingScales.end(); ++it) {
    if (auto *w = it.value()) {
      w->disconnect(this);
      w->deleteLater();
    }
  }
  m_pendingScales.clear();
}

void CoverFlowWidget::cancelPendingLoads() {
  // Disconnect each in-flight source-decode watcher so its finished lambda
  // can't insert a stale (old-cardSize) source into m_pixmapCache after a
  // resize has cleared it (Kartend-k48fl). The QtConcurrent worker can't be
  // cancelled, but dropping the watcher discards its result.
  for (auto it = m_pendingLoads.begin(); it != m_pendingLoads.end(); ++it) {
    if (auto *w = it.value()) {
      w->disconnect(this);
      w->deleteLater();
    }
  }
  m_pendingLoads.clear();
}

void CoverFlowWidget::prunePixmapCache() {
  // Bound the cache so collections with thousands of items don't keep all
  // their pixmaps resident. Keep at most 4× the visible window worth.
  const int budget =
      (kVisibleSideCards * 2 + 1) * UIConstants::Artwork::COVERFLOW_PIXMAP_CACHE_PER_CARD;
  if (static_cast<int>(m_pixmapCache.size()) <= budget) {
    return;
  }
  // Drop entries whose path no longer corresponds to any card near the
  // current selection. Cheap heuristic: clear everything not in [sel-N..sel+N].
  QSet<QString> keep;
  int from = std::max(0, m_selectedIndex - kVisibleSideCards * 2);
  int to = std::min(static_cast<int>(m_cards.size()) - 1, m_selectedIndex + kVisibleSideCards * 2);
  for (int i = from; i <= to; ++i) {
    keep.insert(m_cards[i].artworkPath);
  }
  for (auto it = m_pixmapCache.begin(); it != m_pixmapCache.end();) {
    if (!keep.contains(it.key())) {
      it = m_pixmapCache.erase(it);
    } else {
      ++it;
    }
  }
}

QPixmap CoverFlowWidget::buildPlaceholderPixmap(int width, int height) const {
  if (width <= 0 || height <= 0) {
    return {};
  }
  QColor base = tileColorOrFallback();
  quint64 key = (static_cast<quint64>(width) << 48) | (static_cast<quint64>(height) << 32) |
                (static_cast<quint64>(base.rgba()) & 0xffffffffULL);
  if (!m_placeholderCache.isNull() && m_placeholderCacheKey == key &&
      m_placeholderCachedRadius == m_cornerRadius && m_placeholderCachedTile == m_tileColor) {
    return m_placeholderCache;
  }

  QPixmap pixmap(width, height);
  pixmap.fill(Qt::transparent);
  bool dark = base.lightness() < 128;
  int primaryDelta = dark ? 18 : -22;
  int secondaryDelta = dark ? 10 : -12;
  QColor primary = base;
  int hHue = 0;
  int hSat = 0;
  int hLight = 0;
  int hAlpha = 0;
  primary.getHsl(&hHue, &hSat, &hLight, &hAlpha);
  primary.setHsl(hHue, hSat / 2, std::clamp(hLight + primaryDelta, 0, 255), 255);
  primary.setAlpha(170);
  QColor secondary = base;
  secondary.setHsl(hHue, hSat / 3, std::clamp(hLight + secondaryDelta, 0, 255), 255);
  secondary.setAlpha(120);

  int step = std::clamp(std::min(width, height) / 16, 6, 24);

  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(0, 0, width, height, base);
    painter.setPen(QPen(primary, 1));
    for (int diag = -height; diag < width; diag += step) {
      painter.drawLine(diag, 0, diag + height, height);
    }
    painter.setPen(QPen(secondary, 1));
    for (int diag = -height; diag < width; diag += step * 2) {
      painter.drawLine(diag, height, diag + height, 0);
    }
    QLinearGradient g(0, 0, 0, height);
    g.setColorAt(0.0, QColor(base.red(), base.green(), base.blue(), 0));
    g.setColorAt(1.0, QColor(base.red(), base.green(), base.blue(), 110));
    painter.fillRect(0, 0, width, height, g);
  }

  if (m_cornerRadius > 0) {
    QPixmap masked(width, height);
    masked.fill(Qt::transparent);
    QPainter mp(&masked);
    mp.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, width, height), m_cornerRadius, m_cornerRadius);
    mp.setClipPath(clip);
    mp.drawPixmap(0, 0, pixmap);
    mp.end();
    pixmap = masked;
  }

  m_placeholderCache = pixmap;
  m_placeholderCacheKey = key;
  m_placeholderCachedRadius = m_cornerRadius;
  m_placeholderCachedTile = m_tileColor;
  return pixmap;
}
