#include "starratingwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <array>
#include <cmath>

namespace {

constexpr int kStarCount = 5;
constexpr int kStarSize = 22;
constexpr int kSpacing = 4;
constexpr int kPadding = 2;

constexpr int kTotalWidth = kStarCount * kStarSize + (kStarCount - 1) * kSpacing + 2 * kPadding;
constexpr int kTotalHeight = kStarSize + 2 * kPadding;

/// 5-pointed star polygon centred in the unit square (0..1). Pre-computed
/// once so paintEvent doesn't recompute trig on every repaint.
QPolygonF buildUnitStarPolygon() {
  QPolygonF poly;
  poly.reserve(10);
  constexpr double kOuter = 0.5;
  constexpr double kInner = 0.5 * 0.382; // Golden-ratio inner radius.
  constexpr double kPi = 3.14159265358979323846;
  for (int i = 0; i < 10; ++i) {
    const double angle = -kPi / 2.0 + i * kPi / 5.0;
    const double radius = (i % 2 == 0) ? kOuter : kInner;
    poly << QPointF(0.5 + radius * std::cos(angle), 0.5 + radius * std::sin(angle));
  }
  return poly;
}

const QPolygonF &unitStarPolygon() {
  static const QPolygonF poly = buildUnitStarPolygon();
  return poly;
}

QRectF starRect(int index) noexcept {
  const double x = kPadding + index * (kStarSize + kSpacing);
  return {x, double(kPadding), double(kStarSize), double(kStarSize)};
}

} // namespace

StarRatingWidget::StarRatingWidget(QWidget *parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_Hover, true);
  setCursor(Qt::PointingHandCursor);
}

void StarRatingWidget::setRating(int rating) {
  // Clamp into the [-1, 10] range so callers can pass raw DB values without
  // a separate sanitization step. -1 stays -1 (unset); anything else clamps.
  const int clamped = rating < 0 ? -1 : std::min(10, std::max(0, rating));
  if (clamped == m_rating) {
    return;
  }
  m_rating = clamped;
  update();
  emit ratingChanged(m_rating);
}

QSize StarRatingWidget::sizeHint() const {
  return {kTotalWidth, kTotalHeight};
}

QSize StarRatingWidget::minimumSizeHint() const {
  return sizeHint();
}

int StarRatingWidget::ratingAtPosition(int x) const noexcept {
  if (x < kPadding) {
    return -1;
  }
  // Step covers one full star plus its trailing gap. Within each step, the
  // first half of the star pixels = half-rating, the second half = full.
  const int step = kStarSize + kSpacing;
  const int offset = x - kPadding;
  const int starIndex = offset / step;
  if (starIndex < 0 || starIndex >= kStarCount) {
    return -1;
  }
  const int localX = offset - starIndex * step;
  if (localX >= kStarSize) {
    // In the inter-star gap — treat as the full value of the previous star
    // so the user can comfortably click "between" stars and get the obvious
    // intent.
    return (starIndex + 1) * 2;
  }
  const bool isHalf = localX < kStarSize / 2;
  return starIndex * 2 + (isHalf ? 1 : 2);
}

void StarRatingWidget::mouseMoveEvent(QMouseEvent *event) {
  const int hover = ratingAtPosition(int(event->position().x()));
  if (hover != m_hover) {
    m_hover = hover;
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void StarRatingWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::RightButton) {
    setRating(-1);
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton) {
    const int target = ratingAtPosition(int(event->position().x()));
    if (target >= 0) {
      // Clicking the same value twice in a row clears the rating, matching
      // the toggle behavior most media apps converge on.
      setRating(target == m_rating ? -1 : target);
    }
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void StarRatingWidget::leaveEvent(QEvent *event) {
  if (m_hover != -1) {
    m_hover = -1;
    update();
  }
  QWidget::leaveEvent(event);
}

void StarRatingWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Hover takes precedence over the committed rating so the preview is
  // immediate. -1 hover = no hover, fall back to committed.
  const int displayed = m_hover >= 0 ? m_hover : (m_rating >= 0 ? m_rating : 0);

  const QColor filled = palette().color(QPalette::Highlight);
  const QColor empty = palette().color(QPalette::Mid);
  const QColor outline = palette().color(QPalette::WindowText);

  for (int i = 0; i < kStarCount; ++i) {
    const QRectF rect = starRect(i);
    QPolygonF poly;
    poly.reserve(10);
    for (const QPointF &p : unitStarPolygon()) {
      poly << QPointF(rect.x() + p.x() * rect.width(), rect.y() + p.y() * rect.height());
    }

    QPainterPath path;
    path.addPolygon(poly);
    path.closeSubpath();

    // Empty fill baseline; the filled portion is drawn over it via clipping
    // so half-stars come out clean.
    painter.fillPath(path, empty);

    const int starValue = (i + 1) * 2; // 2, 4, 6, 8, 10
    const int starStart = i * 2;
    if (displayed >= starValue) {
      painter.fillPath(path, filled);
    } else if (displayed > starStart) {
      // Half-fill: clip to the left half of the star rect, then fill.
      painter.save();
      const QRectF half(rect.x(), rect.y(), rect.width() / 2.0, rect.height());
      painter.setClipRect(half);
      painter.fillPath(path, filled);
      painter.restore();
    }

    // Outline keeps the star recognizable against the window background even
    // for the "empty" portion of a half-filled star.
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
  }
}
