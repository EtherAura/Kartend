// Implementation for CoverFlowGalleryStrip — the bottom gallery toolbar
// of CoverFlowWidget. Same friend-of-host pattern as the DetailsPane
// helpers (Kartend-y3ia step 1).
#include "coverflowgallerystrip.h"

#include "coverflowwidget.h"
#include "extensionutils.h"
#include "uiconstants/artwork.h"
#include "videothumbnailextractor.h"

#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPointer>
#include <QPolygon>
#include <QSize>
#include <QtConcurrent>
#include <QWidget>

namespace {
using namespace CoverFlowGalleryStripConstants;
}

CoverFlowGalleryStrip::CoverFlowGalleryStrip(QObject *parent) : QObject(parent) {}

void CoverFlowGalleryStrip::setHost(CoverFlowWidget *host) {
  m_host = host;
}

QList<QRect> CoverFlowGalleryStrip::thumbRects() const {
  QList<QRect> rects;
  if (!m_host || m_host->m_gallery.isEmpty()) {
    return rects;
  }
  const int count = m_host->m_gallery.size();
  const int totalWidth = count * kThumbSize + (count - 1) * kThumbSpacing;
  const int startX = std::max(0, (m_host->width() - totalWidth) / 2);
  const int y = m_host->height() - kStripHeight + (kStripHeight - kThumbSize) / 2;
  for (int i = 0; i < count; ++i) {
    int x = startX + i * (kThumbSize + kThumbSpacing);
    rects.append(QRect(x, y, kThumbSize, kThumbSize));
  }
  return rects;
}

int CoverFlowGalleryStrip::hitTest(const QPoint &pt) const {
  const auto rects = thumbRects();
  for (int i = 0; i < rects.size(); ++i) {
    if (rects[i].contains(pt)) {
      return i;
    }
  }
  return -1;
}

QPixmap CoverFlowGalleryStrip::thumbPixmap(int entryIdx, int size) {
  if (!m_host || entryIdx < 0 || entryIdx >= m_host->m_gallery.size()) {
    return {};
  }
  const auto &entry = m_host->m_gallery[entryIdx];
  const QString key = entry.path + QStringLiteral("::") + QString::number(size);
  auto it = m_host->m_galleryThumbCache.constFind(key);
  if (it != m_host->m_galleryThumbCache.constEnd()) {
    return it.value();
  }

  if (entry.isVideo) {
    // Pull a real first-frame thumbnail from the shared extractor (same
    // path the sidebar gallery uses). Cached by absolute path; the first
    // request kicks off async extraction and we'll get a frameReady
    // signal that invalidates this slot for the next paint.
    auto *extractor = VideoThumbnailExtractor::instance();
    QPixmap raw = extractor->cached(entry.path);
    if (raw.isNull() && !extractor->hasCacheEntry(entry.path)) {
      extractor->requestFrame(entry.path);
    }
    QPixmap pm;
    if (!raw.isNull()) {
      pm = raw.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      // Don't cache the scaled-from-real frame here — the extractor cache
      // is the single source of truth, and we want subsequent paints to
      // re-pull on the off chance the extractor refines the frame.
      return pm;
    }
    // Frame not ready yet: paint a placeholder with a ▶ glyph so the slot
    // doesn't appear empty during extraction. Cache only the placeholder
    // so paintEvent doesn't re-render it every tick; the frameReady
    // handler clears this entry when the real frame arrives.
    pm = QPixmap(size, size);
    pm.fill(m_host->palette().color(QPalette::Mid).darker(120));
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setBrush(m_host->palette().color(QPalette::HighlightedText));
      p.setPen(Qt::NoPen);
      const int triH = size / 2;
      const int triW = size / 2;
      const int cx = size / 2;
      const int cy = size / 2;
      QPolygon tri;
      tri << QPoint(cx - triW / 2, cy - triH / 2) << QPoint(cx - triW / 2, cy + triH / 2)
          << QPoint(cx + triW / 2, cy);
      p.drawPolygon(tri);
    }
    m_host->m_galleryThumbCache.insert(key, pm);
    return pm;
  }

  // Artwork entry (Kartend-jga1): decode + scale off the GUI thread instead of
  // running QImageReader::read() inside paint() on a cache miss (first paint of
  // a populated row decoded N images mid-frame -> visible stutter). Cache a
  // placeholder now so paint() returns immediately and doesn't re-dispatch on
  // every frame; the async result overwrites the entry and repaints. Mirrors
  // the video branch above and the card-artwork worker decode path. The cache
  // is cleared on every setGalleryForIndex, so a result that lands after a
  // gallery switch just drops into a cache that's about to be cleared again.
  QPixmap placeholder(size, size);
  placeholder.fill(m_host->palette().color(QPalette::Mid));
  m_host->m_galleryThumbCache.insert(key, placeholder);

  const QString path = entry.path;
  QPointer<CoverFlowGalleryStrip> self = this;
  auto *watcher = new QFutureWatcher<QImage>(this);
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [self, watcher, key, size]() {
    watcher->deleteLater();
    if (!self || !self->m_host) {
      return;
    }
    const QImage img = watcher->result();
    QPixmap pm;
    if (!img.isNull()) {
      pm =
          QPixmap::fromImage(img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
      // Non-image entry (e.g. a .pdf manual) or a decode failure: keep the
      // neutral placeholder so the slot stays consistent.
      pm = QPixmap(size, size);
      pm.fill(self->m_host->palette().color(QPalette::Mid));
    }
    self->m_host->m_galleryThumbCache.insert(key, pm);
    self->m_host->update();
  });
  // Decode at 2x the thumb edge for crisp downscale, off the GUI thread.
  // Captures only value copies (path, size), so it's safe even if this strip
  // is destroyed before it finishes — the parented watcher is what carries the
  // result back, and it won't fire after destruction.
  watcher->setFuture(QtConcurrent::run([path, size]() -> QImage {
    if (!ExtensionUtils::isDecodableImagePath(path)) {
      return {};
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
    reader.setScaledSize(QSize(size * 2, size * 2));
    return reader.read();
  }));
  return placeholder;
}

void CoverFlowGalleryStrip::paint(QPainter &painter) {
  if (!m_host) return;
  const auto rects = thumbRects();
  if (rects.isEmpty()) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (int i = 0; i < rects.size(); ++i) {
    QRect r = rects[i];
    QPixmap pm = thumbPixmap(i, kThumbSize);
    if (!pm.isNull()) {
      QRect target = pm.rect();
      target.moveCenter(r.center());
      // Background rounded tile so partial-aspect-ratio thumbs sit on a
      // consistent surface instead of bleeding into the carousel backdrop.
      QPainterPath bg;
      bg.addRoundedRect(r, 6, 6);
      painter.setPen(Qt::NoPen);
      painter.setBrush(m_host->palette().color(QPalette::Base));
      painter.drawPath(bg);
      painter.setClipPath(bg);
      painter.drawPixmap(target, pm);
      painter.setClipping(false);
    }
    // Active outline: highlight the entry currently driving the centered
    // card's display.
    if (i == m_host->m_galleryActiveIndex) {
      QPen pen(m_host->selectionColorOrFallback(), 2);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 7, 7);
    }
  }
  painter.restore();
}
