// Cover-flow view mode implementation.

#include "coverflowwidget.h"

#include "coverflowgallerystrip.h"
#include "extensionutils.h"
#include "uiconstants/artwork.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSet>
#include <QtConcurrent>
#include <QTransform>
#include <QWheelEvent>

namespace {

// Visual tuning. Side-card behavior is controlled by these scale curves and
// the per-step horizontal stride. Center card is always at offset 0 and full
// scale; side cards taper off symmetrically.
constexpr int kVisibleSideCards = 5;        ///< cards drawn each side of center
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

QPixmap loadAndScale(const QString &path, int targetSize) {
  // Extension guard: never hand a non-image file (e.g. a scraped .pdf
  // manual) to QImageReader — Qt's PDF image plugin abort()s the process.
  if (path.isEmpty() || !ExtensionUtils::isDecodableImagePath(path)) {
    return {};
  }
  QImageReader reader(path);
  reader.setAutoTransform(true);
  reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
  // Cap the decoded size so very large source images don't waste memory.
  QSize bounded(targetSize * 2, targetSize * 2);
  if (reader.size().isValid() && reader.size().width() > bounded.width()) {
    reader.setScaledSize(reader.size().scaled(bounded, Qt::KeepAspectRatio));
  }
  QImage img = reader.read();
  if (img.isNull()) {
    return {};
  }
  return QPixmap::fromImage(img);
}

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
}

CoverFlowWidget::~CoverFlowWidget() {
  // Detach pending loads — futures may still be running but their results
  // are dropped when the watcher is destroyed.
  for (auto *watcher : m_pendingLoads) {
    if (watcher) {
      watcher->disconnect(this);
      watcher->deleteLater();
    }
  }
  m_pendingLoads.clear();
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
  // Selected card may have moved into a different slot when filters or
  // sorting reshuffled the list; re-evaluate the video preview source so a
  // stale path doesn't keep playing.
  applyVideoPreviewState();
  update();
}

void CoverFlowWidget::setSelectedIndex(int index, bool animate) {
  if (m_cards.isEmpty()) {
    m_selectedIndex = 0;
    m_selectionPositionF = 0.0;
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
  m_glide->stop();
  m_glide->setStartValue(startPos);
  m_glide->setEndValue(0.0);
  m_glide->start();
  // Update preview source on every selection change — the glide animation
  // resolves to the new center index so the preview should track it. Hiding
  // the preview during glide avoids a stale-source frame floating off the
  // center card mid-animation.
  applyVideoPreviewState();
  update();
}

void CoverFlowWidget::setGalleryForIndex(int index, const QList<CoverFlowGalleryEntry> &entries) {
  if (index != m_selectedIndex && index != m_galleryOwnerIndex) {
    // Stale push — selection moved on before the resolver caught up.
    return;
  }
  m_gallery = entries;
  m_galleryOwnerIndex = index;
  m_galleryActiveIndex = -1;
  m_galleryThumbCache.clear();
  // Layout slot reservations changed (gallery may have appeared/disappeared)
  // so re-evaluate cards and the video preview's geometry.
  applyVideoPreviewState();
  updateVideoPreviewGeometry();
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
  update();
}

void CoverFlowWidget::setFontSize(int size) {
  if (size == m_fontSize) {
    return;
  }
  m_fontSize = std::max(6, size);
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
  if (std::abs(frac) > 0.5 && out.size() >= 2) {
    int neighborSide = frac > 0 ? 1 : -1;
    int neighborIdx = center + neighborSide;
    if (neighborIdx >= 0 && neighborIdx < total) {
      // Find center entry (last) and neighbor entry, swap if needed
      auto centerIt = std::find_if(out.rbegin(), out.rend(),
                                   [&](const CardLayout &c) { return c.index == center; });
      auto neighborIt = std::find_if(out.begin(), out.end(),
                                     [&](const CardLayout &c) { return c.index == neighborIdx; });
      if (centerIt != out.rend() && neighborIt != out.end()) {
        // Move neighbor to the end (top), center just before it
        CardLayout neighbor = *neighborIt;
        CardLayout cnt = *centerIt;
        out.erase(neighborIt);
        // re-find center after erase
        auto centerIt2 = std::find_if(out.begin(), out.end(),
                                      [&](const CardLayout &c) { return c.index == center; });
        if (centerIt2 != out.end()) {
          out.erase(centerIt2);
        }
        out.append(cnt);
        out.append(neighbor);
      }
    }
  }
  return out;
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
  // Track the centered card's rect so updateVideoPreviewGeometry can
  // position the QLabel-based preview over it on the next layout round.
  QRect newCenterRect;
  for (const auto &c : layout) {
    if (c.index == m_selectedIndex && std::abs(c.offset) < 0.5) {
      newCenterRect = c.rect;
    }
  }
  if (newCenterRect != m_lastCenterRect) {
    m_lastCenterRect = newCenterRect;
    if (m_videoPreview && m_videoPreview->isVisible() && !newCenterRect.isEmpty()) {
      m_videoPreview->setGeometry(newCenterRect);
      m_videoPreview->raise();
    }
  }
  const bool videoCovering = m_videoPreview && m_videoPreview->isVisible();
  for (const auto &c : layout) {
    // Skip rendering the centered card when the video preview is sitting
    // on top — the QPainter pass would otherwise flicker behind the
    // QVideoSink-driven QLabel as frames arrive.
    if (videoCovering && c.index == m_selectedIndex && std::abs(c.offset) < 0.5) {
      continue;
    }
    QPixmap pm = const_cast<CoverFlowWidget *>(this)->pixmapForIndex(c.index);
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

    // Scale the source pixmap once and reuse it for both the reflection
    // and the card itself. Was being computed twice with identical args
    // — meaningful at kVisibleSideCards=5 cards * 60fps glide animation.
    const QPixmap scaled = pm.scaled(c.rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Paint reflection underneath (a flipped, faded copy) for the iTunes
    // look — only for the center-ish band to keep paint cost low.
    if (absDelta < 1.5) {
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

    // The card itself: same scaled pixmap, with corner radius.
    QRect target = scaled.rect();
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

    // Selection border on the centered card.
    if (c.index == m_selectedIndex && absDelta < 0.5) {
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

  // Per-item gallery toolbar.
  if (!m_gallery.isEmpty() && m_galleryStrip) {
    m_galleryStrip->paint(painter);
  }
}

void CoverFlowWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  prunePixmapCache();
  updateVideoPreviewGeometry();
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

void CoverFlowWidget::wheelEvent(QWheelEvent *event) {
  if (m_cards.isEmpty()) {
    event->ignore();
    return;
  }
  // angleDelta().y() is +120 per notch on a typical wheel. Treat any
  // direction (vertical or horizontal) as a navigation step — cover flow
  // is one-dimensional regardless of input axis.
  int delta = event->angleDelta().y();
  if (delta == 0) {
    delta = event->angleDelta().x();
  }
  if (delta == 0) {
    event->ignore();
    return;
  }
  int step = delta > 0 ? -1 : 1;
  int newIdx = std::clamp(m_selectedIndex + step, 0, static_cast<int>(m_cards.size()) - 1);
  if (newIdx != m_selectedIndex) {
    emit selectionChangeRequested(newIdx);
  }
  event->accept();
}

void CoverFlowWidget::mousePressEvent(QMouseEvent *event) {
  if (m_cards.isEmpty()) {
    QWidget::mousePressEvent(event);
    return;
  }
  // Gallery toolbar takes priority over card hit-test so clicks near the
  // bottom edge can switch artwork variants without first focusing some
  // background card.
  if (event->button() == Qt::LeftButton) {
    int galleryHit = m_galleryStrip ? m_galleryStrip->hitTest(event->pos()) : -1;
    if (galleryHit >= 0) {
      const auto &entry = m_gallery[galleryHit];
      m_galleryActiveIndex = galleryHit;
      // Video gallery entries auto-enable video mode and route the preview
      // through the chosen entry's path; image entries swap the centered
      // card's displayed pixmap and turn video off.
      m_videoMode = entry.isVideo;
      m_galleryThumbCache.remove(QStringLiteral("__active__")); // future use
      applyVideoPreviewState();
      update();
      event->accept();
      return;
    }
  }
  int hit = hitTestCard(event->pos());
  // Middle click on the centered card toggles between artwork and the
  // video preview, mirroring the modifier+middle-click cycle the grid uses
  // for artwork-type swaps. A middle click on a side card focuses it
  // first; the next middle click (now on the new center) toggles preview.
  if (event->button() == Qt::MiddleButton) {
    if (hit < 0) {
      QWidget::mousePressEvent(event);
      return;
    }
    if (hit != m_selectedIndex) {
      emit selectionChangeRequested(hit);
      event->accept();
      return;
    }
    m_videoMode = !m_videoMode;
    applyVideoPreviewState();
    update();
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  if (hit < 0) {
    QWidget::mousePressEvent(event);
    return;
  }
  // Side card: focus it. Center card: no-op — launching requires a
  // double-click or Enter, matching grid-mode semantics so a stray click
  // on the centered cover doesn't fire a launcher.
  if (hit != m_selectedIndex) {
    emit selectionChangeRequested(hit);
  }
  event->accept();
}

void CoverFlowWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || m_cards.isEmpty()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  int hit = hitTestCard(event->pos());
  if (hit >= 0) {
    if (hit != m_selectedIndex) {
      emit selectionChangeRequested(hit);
    }
    emit itemActivated(hit);
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void CoverFlowWidget::keyPressEvent(QKeyEvent *event) {
  if (m_cards.isEmpty()) {
    QWidget::keyPressEvent(event);
    return;
  }
  switch (event->key()) {
  case Qt::Key_Left:
  case Qt::Key_Up: {
    int newIdx = std::max(0, m_selectedIndex - 1);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_Right:
  case Qt::Key_Down: {
    int newIdx = std::min(static_cast<int>(m_cards.size()) - 1, m_selectedIndex + 1);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_PageUp: {
    int newIdx = std::max(0, m_selectedIndex - kVisibleSideCards);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_PageDown: {
    int newIdx =
        std::min(static_cast<int>(m_cards.size()) - 1, m_selectedIndex + kVisibleSideCards);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_Home:
    emit selectionChangeRequested(0);
    event->accept();
    return;
  case Qt::Key_End:
    emit selectionChangeRequested(static_cast<int>(m_cards.size()) - 1);
    event->accept();
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    emit itemActivated(m_selectedIndex);
    event->accept();
    return;
  default:
    break;
  }
  QWidget::keyPressEvent(event);
}

int CoverFlowWidget::hitTestCard(const QPoint &pt) const {
  // Walk the layout in reverse paint order (front-most first) so the
  // centered/topmost card wins ties with side cards.
  const auto layout = computeVisibleLayout();
  for (auto it = layout.rbegin(); it != layout.rend(); ++it) {
    if (it->rect.contains(pt)) {
      return it->index;
    }
  }
  return -1;
}

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
  auto *watcher = new QFutureWatcher<QPixmap>(this);
  m_pendingLoads.insert(path, watcher);
  const int target = cardSize();
  connect(watcher, &QFutureWatcher<QPixmap>::finished, this, [this, watcher, path]() {
    m_pendingLoads.remove(path);
    const QPixmap pm = watcher->result();
    watcher->deleteLater();
    if (!pm.isNull()) {
      m_pixmapCache.insert(path, pm);
      update();
    }
  });
  watcher->setFuture(QtConcurrent::run([path, target]() { return loadAndScale(path, target); }));
}

void CoverFlowWidget::prunePixmapCache() {
  // Bound the cache so collections with thousands of items don't keep all
  // their pixmaps resident. Keep at most 4× the visible window worth.
  const int budget = (kVisibleSideCards * 2 + 1) * 4;
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
