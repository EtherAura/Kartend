#include "focussectionoverlay.h"

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>

namespace {

/// How far the frozen backdrop is pushed toward grey, and how much it is
/// dimmed. Strong enough that the live cut-out reads as "the focused one"
/// at a glance, gentle enough that the layout stays recognisable.
constexpr qreal kDesaturation = 0.68;
constexpr int kDimPercent = 86;
/// Downscale factor for the blur: shrinking and re-expanding with smooth
/// scaling is a cheap gaussian, and unlike QGraphicsBlurEffect it costs
/// one pass over a shrunken image rather than a live effect on the whole
/// widget tree (user request 2026-08-18: "add a bit of blurring").
constexpr int kBlurDownscale = 5;

constexpr int kPillHeight = 40;
constexpr int kPillPadding = 18;
constexpr int kPillBottomMargin = 28;

QPixmap desaturatedSnapshot(QWidget *content) {
  QPixmap grabbed = content->grab();
  if (grabbed.isNull()) {
    return {};
  }
  // Composite, NOT a per-pixel loop: this runs on a full-window grab, and
  // at 4K a hand-rolled loop over ~8M pixels is a visible hitch on every
  // modifier press. Qt's greyscale conversion is optimised, and painting a
  // sliver of the original back on top restores just enough colour.
  const QImage original = grabbed.toImage();
  const QImage grey =
      original.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32);
  const QSize small(qMax(1, grey.width() / kBlurDownscale), qMax(1, grey.height() / kBlurDownscale));
  const QImage blurred = grey.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .scaled(grey.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QPixmap out(grabbed.size());
  out.setDevicePixelRatio(grabbed.devicePixelRatio());
  {
    QPainter painter(&out);
    painter.drawImage(0, 0, blurred);
    // A little of the sharp original back on top keeps the layout legible
    // and restores some colour — the backdrop should read as "behind the
    // glass", not "gone".
    painter.setOpacity(1.0 - kDesaturation);
    painter.drawImage(0, 0, original);
    painter.setOpacity(1.0);
    painter.fillRect(out.rect(), QColor(0, 0, 0, 255 - (255 * kDimPercent / 100)));
  }
  return out;
}

} // namespace

FocusSectionOverlay::FocusSectionOverlay(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("focusSectionOverlay"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setFocusPolicy(Qt::NoFocus);
  hide();
}

FocusSectionOverlay::~FocusSectionOverlay() = default;

void FocusSectionOverlay::activate(QWidget *content, QWidget *focused, const QString &label) {
  if (!content) {
    return;
  }
  if (parentWidget() != content) {
    setParent(content);
  }
  // Grab BEFORE showing: the overlay is a child of the widget being
  // grabbed, so a visible overlay would capture its own previous frame.
  hide();
  m_snapshot = desaturatedSnapshot(content);
  if (m_snapshot.isNull()) {
    return;
  }
  setGeometry(content->rect());
  m_focused = focused;
  m_label = label;
  m_active = true;
  rebuildMask();
  show();
  raise();
  update();
}

void FocusSectionOverlay::updateFocus(QWidget *focused, const QString &label) {
  if (!m_active) {
    return;
  }
  m_focused = focused;
  m_label = label;
  rebuildMask();
  update();
}

void FocusSectionOverlay::deactivate() {
  if (!m_active) {
    return;
  }
  m_active = false;
  m_focused = nullptr;
  m_snapshot = QPixmap();
  clearMask();
  hide();
}

QRect FocusSectionOverlay::pillRect() const {
  QFont f = font();
  f.setBold(true);
  const QFontMetrics fm(f);
  const int w = fm.horizontalAdvance(m_label) + kPillPadding * 2;
  const int x = (width() - w) / 2;
  const int y = height() - kPillHeight - kPillBottomMargin;
  return {x, qMax(0, y), w, kPillHeight};
}

void FocusSectionOverlay::rebuildMask() {
  QRegion region(rect());
  if (m_focused && m_focused->isVisible()) {
    // Cut the focused section out entirely: the real widgets underneath
    // stay live and full-colour, with no repaint cost here.
    const QRect r(m_focused->mapTo(parentWidget(), QPoint(0, 0)), m_focused->size());
    region -= r.intersected(rect());
  }
  // …but the indicator always survives the cut-out.
  region += pillRect();
  setMask(region);
}

void FocusSectionOverlay::paintEvent(QPaintEvent * /*event*/) {
  if (!m_active || m_snapshot.isNull()) {
    return;
  }
  QPainter painter(this);
  painter.drawPixmap(0, 0, m_snapshot);

  const QRect pill = pillRect();
  painter.setRenderHint(QPainter::Antialiasing);
  QColor bg = palette().color(QPalette::Window);
  bg.setAlpha(235);
  painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
  painter.setBrush(bg);
  painter.drawRoundedRect(pill, kPillHeight / 2.0, kPillHeight / 2.0);

  QFont f = font();
  f.setBold(true);
  painter.setFont(f);
  painter.setPen(palette().color(QPalette::WindowText));
  painter.drawText(pill, Qt::AlignCenter, m_label);
}
