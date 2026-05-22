// Implementation for CoverFlowGalleryStrip — the bottom gallery toolbar
// of CoverFlowWidget. Same friend-of-host pattern as the DetailsPane
// helpers (Kartend-y3ia step 1).
#include "coverflowgallerystrip.h"

#include "coverflowwidget.h"
#include "extensionutils.h"
#include "uiconstants/artwork.h"
#include "videothumbnailextractor.h"

#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPolygon>
#include <QSize>
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

  // Artwork entry: load + scale to thumb size, with caching. The extension
  // guard keeps non-image entries (e.g. a .pdf manual) away from QImageReader
  // — leaving `img` null routes them to the placeholder branch below.
  QImage img;
  if (ExtensionUtils::isDecodableImagePath(entry.path)) {
    QImageReader reader(entry.path);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
    reader.setScaledSize(QSize(size * 2, size * 2));
    img = reader.read();
  }
  QPixmap pm;
  if (!img.isNull()) {
    pm = QPixmap::fromImage(img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  } else {
    pm = QPixmap(size, size);
    pm.fill(m_host->palette().color(QPalette::Mid));
  }
  m_host->m_galleryThumbCache.insert(key, pm);
  return pm;
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
