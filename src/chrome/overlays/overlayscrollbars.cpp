#include "overlayscrollbars.h"

#include <QApplication>
#include "kdecolorscheme.h"
#include <QAbstractScrollArea>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <QWidget>

namespace {

constexpr auto kAttachedProperty = "kartendOverlayScrollbars";
constexpr auto kSavedVerticalProperty = "kartendSavedVerticalPolicy";
constexpr auto kSavedHorizontalProperty = "kartendSavedHorizontalPolicy";

/// Slim by request. The handle is the whole design — there is no groove
/// and no dividing line, so the surface underneath reads as continuous.
constexpr int kThickness = 6;
constexpr int kEdgeMargin = 3;
constexpr int kMinHandlePx = 28;
constexpr int kFadeMs = 90; // snappier show/hide (user request 2026-08-18)
constexpr int kIdleHideMs = 700;
constexpr int kHandleAlpha = 210; // the titlebar colour, only slightly translucent

/// ONE colour for every overlay bar, wherever it lives (user request
/// 2026-08-18: "all scrollbars should be same color so it blends in").
/// Taking each widget's own palette gave the tree, the grid and the pane
/// visibly different handles, because the tree panel restyles its palette.
/// Derived from the desktop titlebar so it sits in the same family as the
/// rest of the chrome, with the app palette as the off-KDE fallback.
QColor handleColor() {
  // The TITLEBAR colour itself (user decision 2026-08-18) — not lightened,
  // not a neutral ink. Same source as the toolbar and the tree selection,
  // so every scrollbar belongs to the same chrome family wherever it is
  // drawn. Palette highlight is the off-KDE fallback.
  static const QColor cached = []() {
    const QColor titlebar = KdeColorScheme::activeTitlebarColor();
    return titlebar.isValid() ? titlebar : QApplication::palette().color(QPalette::Highlight);
  }();
  QColor ink = cached;
  ink.setAlpha(kHandleAlpha);
  return ink;
}

/// One painted handle, parented to a scroll area's viewport so it sits
/// above the content and moves with it. Mouse-transparent: it reports
/// position, it is not a drag target, which keeps it from stealing clicks
/// meant for the items underneath.
class OverlayBar : public QWidget {
public:
  OverlayBar(QAbstractScrollArea *area, Qt::Orientation orientation)
      : QWidget(area->viewport()), m_area(area), m_orientation(orientation) {
    // Clickable and draggable (field report 2026-08-18: "overlay
    // scrollbars do not respond to clicks"). It was mouse-transparent to
    // avoid stealing item clicks, but the handle is only a few pixels
    // wide at the very edge, so taking presses there costs nothing.
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    m_fade.setDuration(kFadeMs);
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
      m_opacity = v.toReal();
      update();
      if (m_opacity <= 0.01) {
        hide();
      }
    });
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleHideMs);
    connect(&m_idle, &QTimer::timeout, this, [this]() {
      if (!m_hovered) {
        fadeTo(0.0);
      }
    });
    if (QScrollBar *bar = source()) {
      connect(bar, &QScrollBar::valueChanged, this, [this]() { flash(); });
      connect(bar, &QScrollBar::rangeChanged, this, [this]() { syncGeometry(); });
    }
    hide();
  }

  void setHovered(bool hovered) {
    m_hovered = hovered;
    if (hovered) {
      flash();
    } else {
      m_idle.start();
    }
  }

  void syncGeometry() {
    QScrollBar *bar = source();
    QWidget *viewport = m_area ? m_area->viewport() : nullptr;
    if (!bar || !viewport) {
      return;
    }
    if (bar->maximum() <= bar->minimum()) {
      hide(); // nothing to scroll: no handle at all
      return;
    }
    const QRect vp = viewport->rect();
    if (m_orientation == Qt::Vertical) {
      setGeometry(vp.right() - kThickness - kEdgeMargin + 1, 0, kThickness, vp.height());
    } else {
      setGeometry(0, vp.bottom() - kThickness - kEdgeMargin + 1, vp.width(), kThickness);
    }
    raise();
    update();
  }

protected:
  void mousePressEvent(QMouseEvent *event) override {
    m_dragging = true;
    scrollToPosition(event->position());
    flash();
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (m_dragging) {
      scrollToPosition(event->position());
      flash();
      event->accept();
    }
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    m_dragging = false;
    event->accept();
  }

  void paintEvent(QPaintEvent * /*event*/) override {
    QScrollBar *bar = source();
    if (!bar || bar->maximum() <= bar->minimum() || m_opacity <= 0.01) {
      return;
    }
    const int span = m_orientation == Qt::Vertical ? height() : width();
    const int range = bar->maximum() - bar->minimum() + bar->pageStep();
    if (range <= 0 || span <= 0) {
      return;
    }
    const int handleLen =
        qMax(kMinHandlePx, static_cast<int>(static_cast<qreal>(span) * bar->pageStep() / range));
    const int travel = span - handleLen;
    const qreal progress = bar->maximum() > bar->minimum()
                               ? static_cast<qreal>(bar->value() - bar->minimum()) /
                                     (bar->maximum() - bar->minimum())
                               : 0.0;
    const int offset = static_cast<int>(travel * progress);

    QColor ink = handleColor();
    ink.setAlpha(static_cast<int>(ink.alpha() * m_opacity));
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    const QRectF handle = m_orientation == Qt::Vertical
                              ? QRectF(0, offset, kThickness, handleLen)
                              : QRectF(offset, 0, handleLen, kThickness);
    painter.drawRoundedRect(handle, kThickness / 2.0, kThickness / 2.0);
  }

private:
  [[nodiscard]] QScrollBar *source() const {
    if (!m_area) {
      return nullptr;
    }
    return m_orientation == Qt::Vertical ? m_area->verticalScrollBar()
                                         : m_area->horizontalScrollBar();
  }

  /// Map a click along the bar to a scrollbar value, centring the handle
  /// on the cursor so a click jumps where the user pointed.
  void scrollToPosition(const QPointF &pos) {
    QScrollBar *bar = source();
    if (!bar || bar->maximum() <= bar->minimum()) {
      return;
    }
    const int span = m_orientation == Qt::Vertical ? height() : width();
    if (span <= 0) {
      return;
    }
    const qreal along = m_orientation == Qt::Vertical ? pos.y() : pos.x();
    const qreal fraction = qBound(0.0, along / span, 1.0);
    bar->setValue(bar->minimum() +
                  static_cast<int>((bar->maximum() - bar->minimum()) * fraction));
  }

  void flash() {
    syncGeometry();
    QScrollBar *bar = source();
    if (!bar || bar->maximum() <= bar->minimum()) {
      return;
    }
    show();
    fadeTo(1.0);
    m_idle.start();
  }

  void fadeTo(qreal target) {
    if (qFuzzyCompare(m_opacity, target)) {
      return;
    }
    m_fade.stop();
    m_fade.setStartValue(m_opacity);
    m_fade.setEndValue(target);
    m_fade.start();
  }

  QPointer<QAbstractScrollArea> m_area;
  Qt::Orientation m_orientation;
  QVariantAnimation m_fade;
  QTimer m_idle;
  qreal m_opacity = 0.0;
  bool m_hovered = false;
  bool m_dragging = false;
};

/// Keeps the two bars glued to the viewport and relays hover.
class OverlayController : public QObject {
public:
  explicit OverlayController(QAbstractScrollArea *area)
      : QObject(area), m_vertical(new OverlayBar(area, Qt::Vertical)),
        m_horizontal(new OverlayBar(area, Qt::Horizontal)) {
    if (QWidget *viewport = area->viewport()) {
      viewport->installEventFilter(this);
    }
    area->installEventFilter(this);
    // Pin the native bars to ZERO SIZE rather than hiding them. Several
    // places re-assert ScrollBarAsNeeded at runtime (selection restore,
    // viewport visibility, cover flow); hiding on every show turned into a
    // show/hide fight, and each toggle changed the viewport width — which
    // relaid the grid and made items visibly jump between frames (field
    // report 2026-08-18: "grid alignment issues"). A zero-width bar can be
    // "shown" as often as anything likes: it reserves no space, paints
    // nothing, and the viewport never changes size.
    if (QScrollBar *bar = area->verticalScrollBar()) {
      bar->setFixedWidth(0);
    }
    if (QScrollBar *bar = area->horizontalScrollBar()) {
      bar->setFixedHeight(0);
    }
  }

  /// The bars are children of the VIEWPORT (they must be, to paint over
  /// it) while this controller is a child of the scroll AREA — so during
  /// teardown Qt destroys the viewport's children BEFORE this runs, and
  /// deleting them again segfaulted (caught by the styling test, 2026-08-18).
  /// QPointer makes that ordering irrelevant: whichever goes first, the
  /// other sees null.
  ~OverlayController() override {
    delete m_vertical.data();
    delete m_horizontal.data();
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (!m_vertical || !m_horizontal) {
      return QObject::eventFilter(watched, event); // torn down already
    }
    switch (event->type()) {
    case QEvent::Resize:
      m_vertical->syncGeometry();
      m_horizontal->syncGeometry();
      break;
    case QEvent::Enter:
      m_vertical->setHovered(true);
      m_horizontal->setHovered(true);
      break;
    case QEvent::Leave:
      m_vertical->setHovered(false);
      m_horizontal->setHovered(false);
      break;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<OverlayBar> m_vertical;
  QPointer<OverlayBar> m_horizontal;
};

} // namespace

namespace OverlayScrollbars {

void apply(QAbstractScrollArea *area, bool enabled) {
  if (!area) {
    return;
  }
  auto *existing = area->property(kAttachedProperty).value<QObject *>();
  if (enabled == (existing != nullptr)) {
    return; // already in the requested state
  }
  if (enabled) {
    area->setProperty(kSavedVerticalProperty, static_cast<int>(area->verticalScrollBarPolicy()));
    area->setProperty(kSavedHorizontalProperty,
                      static_cast<int>(area->horizontalScrollBarPolicy()));
    // OFF, not hidden-on-demand: a policy that flips at runtime is what
    // resizes the viewport and shuffles every item sideways.
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *controller = new OverlayController(area);
    area->setProperty(kAttachedProperty, QVariant::fromValue<QObject *>(controller));
    return;
  }
  if (QScrollBar *bar = area->verticalScrollBar()) {
    bar->setMinimumWidth(0);
    bar->setMaximumWidth(QWIDGETSIZE_MAX);
  }
  if (QScrollBar *bar = area->horizontalScrollBar()) {
    bar->setMinimumHeight(0);
    bar->setMaximumHeight(QWIDGETSIZE_MAX);
  }
  existing->deleteLater();
  area->setProperty(kAttachedProperty, QVariant());
  area->setVerticalScrollBarPolicy(
      static_cast<Qt::ScrollBarPolicy>(area->property(kSavedVerticalProperty).toInt()));
  area->setHorizontalScrollBarPolicy(
      static_cast<Qt::ScrollBarPolicy>(area->property(kSavedHorizontalProperty).toInt()));
}

void applyToSurfaces(QWidget *itemScrollArea, QWidget *collectionTree, QWidget *detailsPane,
                     bool enabled) {
  apply(qobject_cast<QAbstractScrollArea *>(itemScrollArea), enabled);
  apply(qobject_cast<QAbstractScrollArea *>(collectionTree), enabled);
  if (detailsPane) {
    // The pane holds several: the artwork strip, the description and the
    // metadata card.
    const auto areas = detailsPane->findChildren<QAbstractScrollArea *>();
    for (QAbstractScrollArea *area : areas) {
      apply(area, enabled);
    }
  }
}

} // namespace OverlayScrollbars
