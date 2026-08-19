#include "selectionindicator.h"

#include <cmath>

#include <QAbstractScrollArea>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>

namespace {

/// Ring geometry. Thin by request: the outline marks a region, it does not
/// frame it. The margin keeps the stroke off the target's own edge so a
/// scroll area's border and the ring do not merge into one thick line.
constexpr int kMargin = 3;
constexpr qreal kPenWidth = 2.0;
constexpr qreal kRadius = 8.0;

/// Pulse: ~30fps, a full breath every ~1.6s. Alpha only — animating the
/// pen WIDTH would make the ring visibly fatten and thin, which reads as
/// jitter next to text.
constexpr int kPulseIntervalMs = 33;
constexpr qreal kPhaseStep = 0.13;
constexpr int kAlphaFloor = 110;
constexpr int kAlphaRange = 145;

} // namespace

SelectionIndicator::SelectionIndicator(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("selectionIndicator"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_TranslucentBackground);
  setFocusPolicy(Qt::NoFocus);
  m_pulse.setInterval(kPulseIntervalMs);
  connect(&m_pulse, &QTimer::timeout, this, [this]() {
    if (!m_target || !m_target->isVisible()) {
      hideIndicator();
      return;
    }
    m_phase += kPhaseStep;
    syncGeometry();
    update();
  });
  hide();
}

SelectionIndicator::~SelectionIndicator() = default;

void SelectionIndicator::showFor(QWidget *target) {
  if (!target || !target->isVisible()) {
    hideIndicator();
    return;
  }
  m_target = target;
  syncGeometry();
  show();
  raise();
  if (!m_pulse.isActive()) {
    m_pulse.start();
  }
  update();
}

void SelectionIndicator::hideIndicator() {
  m_pulse.stop();
  m_target = nullptr;
  hide();
}

void SelectionIndicator::syncGeometry() {
  QWidget *host = parentWidget();
  if (!m_target || !host) {
    return;
  }
  // A target inside a scroll area can sit outside the visible viewport —
  // a thumbnail scrolled off the end of the artwork strip. Bring it back
  // first; an already-visible target makes this a no-op.
  for (QWidget *p = m_target->parentWidget(); p && p != host; p = p->parentWidget()) {
    auto *area = qobject_cast<QAbstractScrollArea *>(p);
    if (!area || !area->viewport()) {
      continue;
    }
    const QRect visible(m_target->mapTo(area->viewport(), QPoint(0, 0)), m_target->size());
    if (!area->viewport()->rect().contains(visible)) {
      if (auto *scroll = qobject_cast<QScrollArea *>(area)) {
        scroll->ensureWidgetVisible(m_target);
      }
    }
    break;
  }

  QRect ringRect = QRect(m_target->mapTo(host, QPoint(0, 0)), m_target->size())
                       .adjusted(-kMargin, -kMargin, kMargin, kMargin);
  // Clip to every scroll viewport above the target: without this the ring
  // for a still-off-screen tile floated outside the pane and drew over the
  // item grid (field report 2026-08-18).
  for (QWidget *p = m_target->parentWidget(); p && p != host; p = p->parentWidget()) {
    if (auto *area = qobject_cast<QAbstractScrollArea *>(p); area && area->viewport()) {
      const QRect viewportRect(area->viewport()->mapTo(host, QPoint(0, 0)),
                               area->viewport()->size());
      ringRect = ringRect.intersected(viewportRect);
    }
  }
  if (ringRect.width() < 2 * kMargin || ringRect.height() < 2 * kMargin) {
    hide(); // scrolled entirely out of sight: show nothing rather than a stray ring
    return;
  }
  if (geometry() != ringRect) {
    setGeometry(ringRect);
  }
  if (!isVisible()) {
    show();
    raise();
  }
}

void SelectionIndicator::paintEvent(QPaintEvent * /*event*/) {
  if (!m_target) {
    return;
  }
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  const auto wave = static_cast<qreal>(0.5 + 0.5 * std::sin(m_phase));
  QColor ink = palette().color(QPalette::Highlight);
  ink.setAlpha(kAlphaFloor + static_cast<int>(kAlphaRange * wave));

  painter.setPen(QPen(ink, kPenWidth));
  painter.setBrush(Qt::NoBrush);
  const QRectF stroke =
      QRectF(rect()).adjusted(kPenWidth / 2.0, kPenWidth / 2.0, -kPenWidth / 2.0, -kPenWidth / 2.0);
  painter.drawRoundedRect(stroke, kRadius, kRadius);
}
