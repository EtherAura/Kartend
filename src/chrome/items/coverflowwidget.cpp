// Cover-flow view mode implementation.

#include "coverflowwidget.h"

#include "coverflowgallerystrip.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QTransform>

namespace {

// Visual tuning. Side-card behavior is controlled by these scale curves and
// the per-step horizontal stride. Center card is always at offset 0 and full
// scale; side cards taper off symmetrically.
constexpr qreal kSideCardScaleStart = 0.78; ///< scale at offset = ±1
constexpr qreal kSideCardScaleStep = 0.10;  ///< additional shrink per step
constexpr qreal kSideCardMinScale = 0.30;   ///< floor
constexpr qreal kSideCardShear = 0.18;      ///< vertical shear amount
constexpr qreal kSideCardOpacityStart = 0.85;
constexpr qreal kSideCardOpacityStep = 0.12;
constexpr qreal kCenterStrideFactor = 0.62; ///< center-to-first-side stride / cardSize
constexpr qreal kSideStrideFactor = 0.34;   ///< inter-side stride / cardSize
constexpr int kGlideDurationMs = 240;
constexpr qreal kCardSizeFactor = 0.72; ///< card edge / min(viewportW, viewportH)
constexpr int kReflectionAlpha = 70;    ///< 0-255
// Gallery-strip layout constants moved to coverflowgallerystrip.h
// (Kartend-y3ia step 1). The remaining references in this TU read
// CoverFlowGalleryStripConstants::kStripHeight via the using-directive
// just below the namespace block.
using CoverFlowGalleryStripConstants::kStripHeight;

} // namespace

CoverFlowWidget::CoverFlowWidget(QWidget *parent) : QWidget(parent) {
  // The carousel renders its own opaque background gradient in paintEvent;
  // WA_OpaquePaintEvent + autoFillBackground=false skips Qt's pre-clear so
  // we don't double-paint the backdrop.
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAutoFillBackground(false);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  // Without an Expanding policy the widget collapses to its (zero) sizeHint
  // inside the items-page QVBoxLayout once gridContainer is hidden, leaving
  // the carousel invisibly small even though the rest of the wiring works.
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumSize(160, 160);

  m_glide = new QPropertyAnimation(this, "selectionPositionF", this);
  m_glide->setDuration(kGlideDurationMs);
  m_glide->setEasingCurve(QEasingCurve::OutCubic);

  // Gallery toolbar helper (Kartend-y3ia step 1). State (m_gallery,
  // m_galleryActiveIndex, m_galleryThumbCache) lives on this; the strip
  // is friend-of-host so it can reach those + palette() and
  // selectionColorOrFallback() without a public accessor surface.
  m_galleryStrip = new CoverFlowGalleryStrip(this);
  m_galleryStrip->setHost(this);

  // Video preview is positioned over the centered card whenever m_videoMode
  // is on AND the focused card has a videoPath. Reuses the same QLabel +
  // QVideoSink pipeline as the sidebar so we don't run into the QVideoWidget
  // / QScrollArea Wayland rendering quirks.
  m_videoPreview = new VideoPreviewWidget(this);
  m_videoPreview->hide();
  m_videoPreview->setAttribute(Qt::WA_TransparentForMouseEvents, false);

  // Hook into the shared video-thumbnail extractor so gallery thumbs for
  // video entries pick up the rendered first-frame as soon as it arrives.
  // The extractor caches by path, so re-requesting on every selection is
  // cheap after the first extraction.
  connect(VideoThumbnailExtractor::instance(), &VideoThumbnailExtractor::frameReady, this,
          [this](const QString &videoPath, const QPixmap &) {
            // Drop any cached null/placeholder thumb keyed by this path so
            // the next paint pulls the freshly-rendered frame from the
            // extractor's cache.
            for (auto it = m_galleryThumbCache.begin(); it != m_galleryThumbCache.end();) {
              if (it.key().startsWith(videoPath + QStringLiteral("::"))) {
                it = m_galleryThumbCache.erase(it);
              } else {
                ++it;
              }
            }
            update();
          });

  // Baseline name for the empty carousel; refreshed on every selection /
  // card-list change so screen readers track the centered card.
  updateAccessibleSelection();
}

CoverFlowWidget::~CoverFlowWidget() {
  // Detach pending workers — futures may still be running but their results
  // are dropped when the watchers are destroyed.
  cancelPendingLoads();
  cancelPendingScales();
}

void CoverFlowWidget::setCards(const QList<CoverFlowCardData> &cards) {
  m_cards = cards;
  const int count = static_cast<int>(m_cards.size());
  if (m_selectedIndex >= count) {
    m_selectedIndex = std::max(0, count - 1);
  }
  m_selectionPositionF = 0.0;
  if (m_glide && m_glide->state() == QAbstractAnimation::Running) {
    m_glide->stop();
  }
  prunePixmapCache();
  // The new collection's artwork paths may not overlap the previous list,
  // and the per-card layout slots will rebind to different sources — drop
  // every scaled entry rather than carry stale (path,size) hits, and cancel
  // the in-flight source decodes too: they were started against the old card
  // set, so a late result would just churn the cache (and could repopulate a
  // path the new set no longer shows) (Kartend-ktih).
  cancelPendingLoads();
  cancelPendingScales();
  m_scaledPixmapCache.clear();
  // Selected card may have moved into a different slot when filters or
  // sorting reshuffled the list; re-evaluate the video preview source so a
  // stale path doesn't keep playing. Center rect first — the card set (and
  // the possibly-clamped selection) shifts what sits at stage center.
  refreshCenterRect();
  applyVideoPreviewState();
  updateAccessibleSelection();
  update();
}

void CoverFlowWidget::updateCard(int index, const CoverFlowCardData &card) {
  if (index < 0 || index >= static_cast<int>(m_cards.size())) {
    return;
  }
  CoverFlowCardData &slot = m_cards[index];
  if (slot.title == card.title && slot.artworkPath == card.artworkPath) {
    return;
  }
  const QString oldPath = slot.artworkPath;
  slot.title = card.title;
  slot.artworkPath = card.artworkPath;
  // slot.videoPath is intentionally preserved: video paths are resolved
  // lazily via setVideoPathForIndex, not by the card-list builders, so a
  // chunk-arrival patch must not clobber an already-resolved preview.
  if (!oldPath.isEmpty() && oldPath != card.artworkPath) {
    // Drop only the scaled entries bound to the replaced artwork; every
    // other card's cache hits stay warm (this is the point of the
    // incremental path vs setCards' wholesale clear).
    for (auto it = m_scaledPixmapCache.begin(); it != m_scaledPixmapCache.end();) {
      if (it.key().path == oldPath) {
        it = m_scaledPixmapCache.erase(it);
      } else {
        ++it;
      }
    }
  }
  // The accessible name carries the centered card's title, so a patch that
  // retitles the selected slot must refresh it.
  if (index == m_selectedIndex) {
    updateAccessibleSelection();
  }
  // Repaint only when the patched card is inside the drawn window around the
  // (possibly animating) center — off-screen patches are pure data updates.
  if (std::abs(index - currentPositionF()) <= kVisibleSideCards + 1) {
    update();
  }
}

void CoverFlowWidget::setSelectedIndex(int index, bool animate) {
  if (m_cards.isEmpty()) {
    m_selectedIndex = 0;
    m_selectionPositionF = 0.0;
    updateAccessibleSelection();
    refreshCenterRect();
    update();
    return;
  }
  index = std::clamp(index, 0, static_cast<int>(m_cards.size()) - 1);
  if (index == m_selectedIndex && qFuzzyIsNull(m_selectionPositionF)) {
    return;
  }
  if (m_glide->state() == QAbstractAnimation::Running) {
    m_glide->stop();
  }
  // Snap rather than glide on large jumps (collection switch, search jump,
  // alphabetic seek) so the carousel doesn't ribbon-scroll through dozens
  // of cards. kVisibleSideCards * 2 is the diameter of cards drawn at any
  // given time — anything farther can't be visually interpolated anyway.
  const int delta = std::abs(index - m_selectedIndex);
  if (!animate || !isVisible() || delta > kVisibleSideCards * 2) {
    m_selectedIndex = index;
    m_selectionPositionF = 0.0;
    updateAccessibleSelection();
    // Fresh center rect before applyVideoPreviewState reads it to place
    // the preview over the newly-centered card.
    refreshCenterRect();
    applyVideoPreviewState();
    update();
    return;
  }
  // Animate from current visual position to the new index. We keep
  // m_selectedIndex as the *target* and drive m_selectionPositionF from
  // negative-of-distance toward 0 so paint code can interpolate without
  // worrying about the index changing mid-flight.
  qreal startPos = (m_selectedIndex - index) + m_selectionPositionF;
  m_selectedIndex = index;
  m_selectionPositionF = startPos;
  // Name tracks the logical selection immediately; the glide only animates
  // the visual position toward it.
  updateAccessibleSelection();
  m_glide->stop();
  m_glide->setStartValue(startPos);
  m_glide->setEndValue(0.0);
  m_glide->start();
  // Update preview source on every selection change — the glide animation
  // resolves to the new center index so the preview should track it. Hiding
  // the preview during glide avoids a stale-source frame floating off the
  // center card mid-animation.
  refreshCenterRect();
  applyVideoPreviewState();
  update();
}

void CoverFlowWidget::updateAccessibleSelection() {
  // The carousel is a single custom-painted widget: screen readers can't
  // enumerate the cards, so surface the centered card's title and position
  // through the widget-level accessible name. setAccessibleName() emits the
  // NameChanged accessibility event, so assistive tech re-announces on each
  // selection change.
  if (m_cards.isEmpty()) {
    setAccessibleName(tr("Cover flow, no items"));
    setAccessibleDescription(tr("Cover flow item carousel"));
    return;
  }
  const int count = static_cast<int>(m_cards.size());
  const int index = std::clamp(m_selectedIndex, 0, count - 1);
  setAccessibleName(tr("%1, item %2 of %3").arg(m_cards[index].title).arg(index + 1).arg(count));
  setAccessibleDescription(
      tr("Cover flow item carousel. Use the arrow keys to change the selection."));
}

void CoverFlowWidget::setGalleryForIndex(int index, const QList<CoverFlowGalleryEntry> &entries) {
  if (index != m_selectedIndex) {
    // Stale push — selection moved on before the resolver caught up. A
    // refresh for the current selection still passes (owner == selected
    // then); a late result for the previous owner must not resurrect that
    // card's gallery under the new selection.
    return;
  }
  m_gallery = entries;
  m_galleryOwnerIndex = index;
  m_galleryActiveIndex = -1;
  m_galleryThumbCache.clear();
  // Layout slot reservations changed (gallery may have appeared/disappeared)
  // so re-derive the center rect, then re-evaluate the preview against it.
  refreshCenterRect();
  applyVideoPreviewState();
  update();
}

void CoverFlowWidget::setVideoPathForIndex(int index, const QString &videoPath) {
  if (index < 0 || index >= static_cast<int>(m_cards.size())) {
    return;
  }
  if (m_cards[index].videoPath == videoPath) {
    return;
  }
  m_cards[index].videoPath = videoPath;
  if (index == m_selectedIndex) {
    applyVideoPreviewState();
  }
}

void CoverFlowWidget::setSelectionPositionF(qreal v) {
  if (qFuzzyCompare(v + 1.0, m_selectionPositionF + 1.0)) {
    return;
  }
  m_selectionPositionF = v;
  // Every glide frame moves the centered card; re-derive its rect here —
  // outside the paint path — so the video preview tracks the animation.
  refreshCenterRect();
  update();
}

void CoverFlowWidget::setCornerRadius(int radius) {
  if (radius == m_cornerRadius) {
    return;
  }
  m_cornerRadius = std::max(0, radius);
  update();
}

void CoverFlowWidget::setTileColor(const QString &color) {
  if (color == m_tileColor) {
    return;
  }
  m_tileColor = color;
  m_pixmapCache.clear(); // placeholders depend on tile color
  cancelPendingScales();
  m_scaledPixmapCache.clear(); // scaled entries snapshot placeholders too
  update();
}

void CoverFlowWidget::setSelectionColor(const QString &color) {
  if (color == m_selectionColor) {
    return;
  }
  m_selectionColor = color;
  update();
}

void CoverFlowWidget::setBackgroundColor(const QString &color) {
  if (color == m_backgroundColor) {
    return;
  }
  m_backgroundColor = color;
  update();
}

void CoverFlowWidget::setHideTitles(bool hide) {
  if (hide == m_hideTitles) {
    return;
  }
  m_hideTitles = hide;
  // The title strip reserves a vertical slot, so toggling it shifts the
  // carousel's center (and card size) — re-derive the center rect.
  refreshCenterRect();
  update();
}

void CoverFlowWidget::setFontSize(int size) {
  if (size == m_fontSize) {
    return;
  }
  m_fontSize = std::max(6, size);
  // Font size scales the title-strip slot, which shifts the carousel's
  // center (and card size) — re-derive the center rect.
  refreshCenterRect();
  update();
}

void CoverFlowWidget::setFontFamily(const QString &family) {
  if (family == m_fontFamily) {
    return;
  }
  m_fontFamily = family;
  update();
}

int CoverFlowWidget::cardSize() const {
  // Reserve vertical slots for the title strip and (when present) the
  // per-item gallery thumbnail row at the bottom edge.
  const int titleSlot = m_hideTitles ? 0 : m_fontSize * 3;
  const int gallerySlot = m_gallery.isEmpty() ? 0 : kStripHeight;
  int extent = std::min(width(), height() - titleSlot - gallerySlot);
  return std::max(64, static_cast<int>(extent * kCardSizeFactor));
}

QColor CoverFlowWidget::tileColorOrFallback() const {
  if (!m_tileColor.isEmpty() && QColor::isValidColorName(m_tileColor)) {
    return QColor(m_tileColor);
  }
  return palette().color(QPalette::Mid);
}

QColor CoverFlowWidget::selectionColorOrFallback() const {
  if (!m_selectionColor.isEmpty() && QColor::isValidColorName(m_selectionColor)) {
    return QColor(m_selectionColor);
  }
  return palette().color(QPalette::Highlight);
}

QColor CoverFlowWidget::backgroundColorOrFallback() const {
  if (!m_backgroundColor.isEmpty() && QColor::isValidColorName(m_backgroundColor)) {
    return QColor(m_backgroundColor);
  }
  return palette().color(QPalette::Window);
}

QList<CoverFlowWidget::CardLayout> CoverFlowWidget::computeVisibleLayout() const {
  QList<CardLayout> out;
  if (m_cards.isEmpty()) {
    return out;
  }
  const qreal pos = currentPositionF();
  const int center = static_cast<int>(std::floor(pos));
  const qreal frac = pos - center;
  const int sz = cardSize();
  const qreal centerX = width() / 2.0;
  // Carousel vertical center is offset upward to leave room for the title
  // strip and (when present) the gallery thumbnail row at the bottom.
  const int titleSlot = m_hideTitles ? 0 : m_fontSize * 3;
  const int gallerySlot = m_gallery.isEmpty() ? 0 : kStripHeight;
  const qreal centerY = (height() - titleSlot - gallerySlot) / 2.0;

  // Build painted-from-back-to-front order: farthest sides first, then center
  // last. We iterate symmetric pairs ±k from center, decreasing k → 0.
  auto cardOffsetX = [&](qreal delta) -> qreal {
    // delta is signed offset from "centered position". Stride starts wide
    // for the center→first-side step then narrows.
    qreal sign = (delta < 0) ? -1.0 : 1.0;
    qreal abs = std::abs(delta);
    qreal x = 0.0;
    if (abs <= 1.0) {
      x = sign * abs * sz * kCenterStrideFactor;
    } else {
      x = sign * (sz * kCenterStrideFactor + (abs - 1.0) * sz * kSideStrideFactor);
    }
    return x;
  };
  auto cardScale = [&](qreal delta) -> qreal {
    qreal abs = std::abs(delta);
    if (abs <= 0.001) {
      return 1.0;
    }
    qreal scale = 1.0 - (1.0 - kSideCardScaleStart) * std::min<qreal>(abs, 1.0);
    if (abs > 1.0) {
      scale -= kSideCardScaleStep * (abs - 1.0);
    }
    return std::max(kSideCardMinScale, scale);
  };

  // Painters order: from outermost back to innermost on each side, finishing
  // with the center card on top.
  const int total = static_cast<int>(m_cards.size());
  for (int k = kVisibleSideCards; k >= 1; --k) {
    for (int side : {-1, 1}) {
      int idx = center + side * k;
      if (idx < 0 || idx >= total) {
        continue;
      }
      qreal delta = static_cast<qreal>(idx - center) - frac;
      qreal x = centerX + cardOffsetX(delta);
      qreal s = cardScale(delta);
      int cw = static_cast<int>(sz * s);
      int ch = cw;
      QRect r(static_cast<int>(x - cw / 2.0), static_cast<int>(centerY - ch / 2.0), cw, ch);
      out.append({idx, delta, r});
    }
  }
  // Center card last so it paints on top
  if (center >= 0 && center < total) {
    qreal delta = -frac;
    qreal x = centerX + cardOffsetX(delta);
    qreal s = cardScale(delta);
    int cw = static_cast<int>(sz * s);
    int ch = cw;
    QRect r(static_cast<int>(x - cw / 2.0), static_cast<int>(centerY - ch / 2.0), cw, ch);
    out.append({center, delta, r});
  }
  // Also draw the card at center+1 if frac > 0 (or center-1 if frac < 0) on
  // top of its side neighbors but under the center card, so the "incoming"
  // card stays visually attached to the center during glide. That's already
  // handled by the symmetric loop above (k=1 covers both sides). If frac is
  // large enough that the card at center+sign(frac) is closer to the center
  // than center itself, swap their painted order so the closer one ends up
  // on top.
  ensureCenterCardOnTop(out, center, frac);
  return out;
}

void CoverFlowWidget::ensureCenterCardOnTop(QList<CardLayout> &layouts, int center,
                                            qreal frac) const {
  // Only relevant once the glide has carried more than halfway toward a
  // neighbour; below that the symmetric build order already has the center on
  // top. Mirrors the previous block's guards exactly.
  if (std::abs(frac) <= 0.5 || layouts.size() < 2) {
    return;
  }
  const int total = static_cast<int>(m_cards.size());
  const int neighborSide = frac > 0 ? 1 : -1;
  const int neighborIdx = center + neighborSide;
  if (neighborIdx < 0 || neighborIdx >= total) {
    return;
  }

  // Locate the center and neighbour entries without erase-then-re-find: copy
  // both out, drop them from their current positions in a single pass, then
  // re-append center (under) and neighbour (on top). Indices are unique per
  // layout, so a forward scan finds each at most once — equivalent to the old
  // rbegin/begin finds since center is appended last by the builder above.
  int centerPos = -1;
  int neighborPos = -1;
  for (int i = 0; i < layouts.size(); ++i) {
    if (centerPos < 0 && layouts[i].index == center) {
      centerPos = i;
    } else if (neighborPos < 0 && layouts[i].index == neighborIdx) {
      neighborPos = i;
    }
  }
  if (centerPos < 0 || neighborPos < 0) {
    return;
  }

  const CardLayout cnt = layouts[centerPos];
  const CardLayout neighbor = layouts[neighborPos];
  // Remove the higher index first so the lower index stays valid.
  const int first = std::max(centerPos, neighborPos);
  const int second = std::min(centerPos, neighborPos);
  layouts.removeAt(first);
  layouts.removeAt(second);
  layouts.append(cnt);
  layouts.append(neighbor);
}

void CoverFlowWidget::paintEvent(QPaintEvent * /*event*/) {
  QPainter painter(this);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

  // Stage backdrop: vertical gradient of background color so the cards have
  // something to sit on regardless of the parent's background.
  QColor bg = backgroundColorOrFallback();
  QLinearGradient bgGrad(0, 0, 0, height());
  bgGrad.setColorAt(0.0, bg);
  bgGrad.setColorAt(1.0, bg.darker(115));
  painter.fillRect(rect(), bgGrad);

  if (m_cards.isEmpty()) {
    painter.setPen(palette().color(QPalette::WindowText));
    QFont f = painter.font();
    f.setPointSize(m_fontSize + 2);
    painter.setFont(f);
    painter.drawText(rect(), Qt::AlignCenter, tr("No items"));
    return;
  }

  const auto layout = computeVisibleLayout();
  // The centered card's rect (m_lastCenterRect — video-preview positioning)
  // is maintained by refreshCenterRect() on the setters that move the center,
  // never here: a child setGeometry inside a paint pass schedules another
  // layout/paint round per glide frame (paint-driven layout).
  const bool videoCovering = m_videoPreview && m_videoPreview->isVisible();
  for (const auto &c : layout) {
    // Skip rendering the centered card when the video preview is sitting
    // on top — the QPainter pass would otherwise flicker behind the
    // QVideoSink-driven QLabel as frames arrive.
    if (videoCovering && c.index == m_selectedIndex && std::abs(c.offset) < 0.5) {
      continue;
    }
    QString pmSourcePath;
    QPixmap pm = const_cast<CoverFlowWidget *>(this)->pixmapForIndex(c.index, &pmSourcePath);
    if (pm.isNull()) {
      continue;
    }

    qreal absDelta = std::abs(c.offset);
    qreal opacity = 1.0;
    if (absDelta > 0.001) {
      opacity = kSideCardOpacityStart - kSideCardOpacityStep * std::max<qreal>(0.0, absDelta - 1.0);
      opacity = std::clamp(opacity, 0.25, 1.0);
    }

    painter.save();

    // Resolve the scaled pixmap, set the per-card opacity + perspective
    // transform + (on a cache miss) the fast-transform hint, and draw the
    // card. The reflection (drawn into a separate, non-overlapping rect below
    // the card) and the selection border reuse the resolved pixmap / draw
    // size returned via the out-params.
    QPixmap scaled;
    QSize scaledDrawSize;
    bool useFastTransform = false;
    renderCardPixmap(painter, c, pm, pmSourcePath, scaled, scaledDrawSize, useFastTransform);

    // Reflection underneath (a flipped, faded copy) for the iTunes look —
    // only for the center-ish band to keep paint cost low.
    if (absDelta < 1.5) {
      renderCardReflection(painter, c, scaled, opacity, bg);
    }

    // Selection border on the centered card.
    if (c.index == m_selectedIndex && absDelta < 0.5) {
      QRect target(QPoint(0, 0), scaledDrawSize);
      target.moveCenter(c.rect.center());
      QPen pen(selectionColorOrFallback(), 2);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      QRectF border = QRectF(target).adjusted(-1, -1, 1, 1);
      if (m_cornerRadius > 0) {
        painter.drawRoundedRect(border, m_cornerRadius, m_cornerRadius);
      } else {
        painter.drawRect(border);
      }
    }

    painter.restore();
  }

  // Title strip under the carousel for the centered card. Sits above the
  // optional gallery thumbnail row at the very bottom.
  renderTitleStrip(painter);

  // Per-item gallery toolbar.
  if (!m_gallery.isEmpty() && m_galleryStrip) {
    m_galleryStrip->paint(painter);
  }
}

void CoverFlowWidget::renderCardPixmap(QPainter &painter, const CardLayout &c, const QPixmap &pm,
                                       const QString &sourcePath, QPixmap &scaled,
                                       QSize &scaledDrawSize, bool &useFastTransform) {
  const qreal absDelta = std::abs(c.offset);
  qreal opacity = 1.0;
  if (absDelta > 0.001) {
    opacity = kSideCardOpacityStart - kSideCardOpacityStep * std::max<qreal>(0.0, absDelta - 1.0);
    opacity = std::clamp(opacity, 0.25, 1.0);
  }
  painter.setOpacity(opacity);

  // Build the perspective transform: translate to card center, apply a
  // vertical shear that points away from the stage center (cards on the
  // left tilt right, cards on the right tilt left), then translate back.
  // We render the (already-scaled) pixmap into c.rect via drawPixmap with
  // the transform set — Qt::SmoothPixmapTransform keeps it crisp.
  qreal shear = 0.0;
  if (absDelta > 0.001) {
    qreal sign = (c.offset < 0) ? 1.0 : -1.0; // left side: shear positive Y
    shear = sign * kSideCardShear * std::min<qreal>(1.5, absDelta);
  }
  QTransform t;
  t.translate(c.rect.center().x(), c.rect.center().y());
  if (!qFuzzyIsNull(shear)) {
    t.shear(0, shear);
  }
  t.translate(-c.rect.center().x(), -c.rect.center().y());
  painter.setTransform(t, true);

  // Look up a pre-scaled pixmap; if missing, schedule a worker-thread
  // scale and fall back to scaling the source pm at draw time for this
  // frame. The cache is invalidated on resize / setCards / setTileColor
  // and bounded by requestScaledPixmap. Steady-state paint is a hash
  // lookup + drawPixmap; only the first paint after a size change scales
  // the source on the main thread, and the worker delivers the cached
  // entry before the next paint (Kartend-g6ft).
  const QSize cardSize = c.rect.size();
  useFastTransform = false;
  // Kartend-ce0b4: key on what @p pm ACTUALLY is, not on the card's
  // artworkPath. Those differ whenever pixmapForIndex handed back a
  // placeholder because the decode had not landed yet — and they also differ
  // for the gallery override. Keying on artworkPath cached the placeholder
  // under the real artwork's key; the decode then completed, update() fired,
  // and every subsequent paint HIT that entry and redrew the placeholder,
  // discarding the real pixmap. The art only appeared once the card changed
  // SIZE (a selection move resizes it), which changed the key and forced a
  // miss — exactly the reported "not visible until the selection changes".
  //
  // A placeholder is never cached at all: it carries no path to key on, it is
  // cheap to draw, and buildPlaceholderPixmap already memoizes the unscaled
  // one. Skipping it also keeps pruneScaledPixmapCache's keep-set (built from
  // card artwork paths) from evicting entries it cannot account for.
  //
  // KeepAspectRatio on the miss path: the on-disk source pm may be wider or
  // taller than the card slot, so compute what its target size would be and
  // let the painter scale at draw time with FastTransformation (cheap
  // nearest-neighbour) while a worker delivers the Smooth entry for the next
  // paint (Kartend-g6ft). Briefly slightly pixelated; sub-frame in practice.
  const CoverFlowScaledKey scaledKey{sourcePath, cardSize.width(), cardSize.height()};
  const auto cacheIt = sourcePath.isEmpty() ? m_scaledPixmapCache.constEnd()
                                            : m_scaledPixmapCache.constFind(scaledKey);
  if (cacheIt != m_scaledPixmapCache.constEnd()) {
    scaled = cacheIt.value();
    scaledDrawSize = scaled.size();
  } else {
    scaled = pm;
    scaledDrawSize = pm.size().scaled(cardSize, Qt::KeepAspectRatio);
    useFastTransform = true;
    if (!sourcePath.isEmpty()) {
      requestScaledPixmap(scaledKey, pm, cardSize);
    }
  }

  // The outer painter.save() at the top of this card iteration captures
  // the SmoothPixmapTransform hint state, so the restore() there brings
  // it back to the default after the (cheap, slightly pixelated) miss
  // draw.
  if (useFastTransform) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  }

  // The card itself: same scaled pixmap, with corner radius.
  QRect target(QPoint(0, 0), scaledDrawSize);
  target.moveCenter(c.rect.center());
  if (m_cornerRadius > 0) {
    QPainterPath clip;
    clip.addRoundedRect(target, m_cornerRadius, m_cornerRadius);
    painter.setClipPath(clip, Qt::ReplaceClip);
  }
  painter.drawPixmap(target, scaled);
  if (m_cornerRadius > 0) {
    painter.setClipping(false);
  }
}

void CoverFlowWidget::renderCardReflection(QPainter &painter, const CardLayout &c,
                                           const QPixmap &scaled, qreal opacity, const QColor &bg) {
  QRect refl(c.rect.x(), c.rect.bottom(), c.rect.width(), c.rect.height() / 2);
  painter.save();
  painter.setOpacity(opacity * (kReflectionAlpha / 255.0));
  QTransform flip;
  flip.translate(refl.center().x(), refl.top());
  flip.scale(1.0, -0.5);
  flip.translate(-refl.center().x(), -refl.top());
  painter.setTransform(flip, true);
  QRect target = c.rect;
  target.moveCenter(c.rect.center());
  painter.drawPixmap(target, scaled);
  painter.restore();
  // Fade the reflection out toward the bottom
  QLinearGradient g(refl.topLeft(), refl.bottomLeft());
  g.setColorAt(0.0, QColor(0, 0, 0, 0));
  g.setColorAt(1.0, bg);
  painter.fillRect(refl, g);
}

void CoverFlowWidget::renderTitleStrip(QPainter &painter) {
  // Sits above the optional gallery thumbnail row at the very bottom.
  const int gallerySlotEnd = m_gallery.isEmpty() ? 0 : kStripHeight;
  if (!m_hideTitles && m_selectedIndex >= 0 && m_selectedIndex < m_cards.size()) {
    QFont f = painter.font();
    if (!m_fontFamily.isEmpty()) {
      f.setFamily(m_fontFamily);
    }
    f.setPointSize(m_fontSize);
    f.setBold(true);
    painter.setFont(f);
    painter.setPen(palette().color(QPalette::WindowText));
    int titleY = height() - m_fontSize * 3 - gallerySlotEnd;
    QRect titleRect(0, titleY, width(), m_fontSize * 3);
    QString title = m_cards[m_selectedIndex].title;
    QFontMetrics fm(f);
    QString elided = fm.elidedText(title, Qt::ElideRight, titleRect.width() - 32);
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, elided);
  }
}

void CoverFlowWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // Card target sizes are derived from widget size, so a resize invalidates
  // BOTH caches: the scaled entries (keyed on WxH) and the source pixmaps.
  // m_pixmapCache holds each source already decoded at the request-time
  // cardSize via loadAndScale, so after a resize the old smaller source would
  // be upscaled into larger cards, leaving soft/blurry covers until eviction
  // (Kartend-k48fl). Cancel in-flight workers first so their late-arriving
  // results don't repopulate stale-size entries.
  cancelPendingLoads();
  m_pixmapCache.clear();
  cancelPendingScales();
  m_scaledPixmapCache.clear();
  refreshCenterRect();
  update();
}

void CoverFlowWidget::hideEvent(QHideEvent *event) {
  // Cover flow is no longer the active view — drop the preview so we
  // don't keep a decoder alive (and don't keep audio playing) while
  // hidden. applyVideoPreviewState() picks the source back up next time
  // the widget is shown with m_videoMode true.
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
    m_videoPreviewIndex = -1;
  }
  QWidget::hideEvent(event);
}

void CoverFlowWidget::changeEvent(QEvent *e) {
  QWidget::changeEvent(e);
  // Placeholder cards (palette Mid) and gallery thumbnails (palette
  // Mid/HighlightedText) bake their colours into cached pixmaps that paint()
  // returns verbatim, so a repaint can't refresh them. On a system palette
  // change drop those caches — same set the tile-colour change clears — so
  // they re-render against the new accent. (Real decoded artwork in
  // m_pixmapCache is re-fetched; palette changes are rare.)
  if (e->type() == QEvent::PaletteChange) {
    m_pixmapCache.clear();
    cancelPendingScales();
    m_scaledPixmapCache.clear();
    m_galleryThumbCache.clear();
    update();
  }
}

void CoverFlowWidget::refreshCenterRect() {
  // Same center detection paintEvent used to do inline: the selected card's
  // rect while it is within half a step of the stage center. Empty while a
  // glide is mid-flight past that band (or when there are no cards), which
  // intentionally leaves the preview where it was — updateVideoPreviewGeometry
  // skips empty rects, matching the old paint-path behaviour.
  QRect newCenterRect;
  const auto layout = computeVisibleLayout();
  for (const auto &c : layout) {
    if (c.index == m_selectedIndex && std::abs(c.offset) < 0.5) {
      newCenterRect = c.rect;
    }
  }
  if (newCenterRect == m_lastCenterRect) {
    return;
  }
  m_lastCenterRect = newCenterRect;
  updateVideoPreviewGeometry();
}

void CoverFlowWidget::updateVideoPreviewGeometry() {
  if (!m_videoPreview || !m_videoPreview->isVisible()) {
    return;
  }
  if (m_lastCenterRect.isEmpty()) {
    return;
  }
  m_videoPreview->setGeometry(m_lastCenterRect);
  m_videoPreview->raise();
}

void CoverFlowWidget::applyVideoPreviewState() {
  if (!m_videoPreview) {
    return;
  }
  // Resolve the active video source: gallery override wins, otherwise fall
  // back to the centered card's auto-discovered videoPath (legacy
  // middle-click toggle).
  QString videoPath;
  if (m_galleryActiveIndex >= 0 && m_galleryActiveIndex < m_gallery.size() &&
      m_galleryOwnerIndex == m_selectedIndex && m_gallery[m_galleryActiveIndex].isVideo) {
    videoPath = m_gallery[m_galleryActiveIndex].path;
  } else if (m_videoMode && m_selectedIndex >= 0 &&
             m_selectedIndex < static_cast<int>(m_cards.size())) {
    videoPath = m_cards[m_selectedIndex].videoPath;
  }
  if (videoPath.isEmpty()) {
    if (m_videoPreview->isVisible() || !m_videoPreview->currentVideoPath().isEmpty()) {
      m_videoPreview->stop();
      m_videoPreview->hide();
      m_videoPreviewIndex = -1;
    }
    return;
  }
  if (m_videoPreviewIndex != m_selectedIndex || m_videoPreview->currentVideoPath() != videoPath) {
    m_videoPreview->playVideo(videoPath);
    m_videoPreviewIndex = m_selectedIndex;
  }
  if (!m_lastCenterRect.isEmpty()) {
    m_videoPreview->setGeometry(m_lastCenterRect);
  }
  m_videoPreview->show();
  m_videoPreview->raise();
}
