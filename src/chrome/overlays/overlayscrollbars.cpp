#include "overlayscrollbars.h"

#include "propertyutils.h"

#include "kdecolorscheme.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <QWidget>

namespace {

constexpr auto kAttachedProperty = PropertyKeys::OverlayScrollbarsAttached;
constexpr auto kSavedVerticalProperty = "kartendSavedVerticalPolicy";
constexpr auto kSavedHorizontalProperty = "kartendSavedHorizontalPolicy";

/// The user's per-surface, per-axis ScrollbarMode, stored as an int. Named
/// strings rather than a typed API because SettingsUtils sets the same
/// properties on the item grid from the utils layer, which sits BELOW this
/// header and cannot include it — the property name is the contract between
/// the two. Absent property reads as 0 == ScrollbarMode::Show.
constexpr auto kVerticalModeProperty = "kartendVerticalScrollbarMode";
constexpr auto kHorizontalModeProperty = "kartendHorizontalScrollbarMode";
/// How near the lane the pointer must come for an Autohide bar to appear.
/// Generous enough to be discoverable without a deliberate hunt, narrow
/// enough that crossing the grid does not summon it.
constexpr int kAutohideProximityPx = 56;
constexpr int kProximityPollMs = 60;
/// Policies captured when the hide took hold. Kept separate from the kSaved*
/// pair above so an overlay attach/detach and a hide/unhide cannot restore
/// each other's snapshot.
constexpr auto kHiddenSavedVerticalProperty = "kartendHiddenSavedVerticalPolicy";
constexpr auto kHiddenSavedHorizontalProperty = "kartendHiddenSavedHorizontalPolicy";
/// True while the hide is what is holding the native bars off — as opposed
/// to the overlay doing it, or the surface having been AlwaysOff all along.
/// The two mechanisms hand the policies back and forth (a user can toggle
/// slim overlay bars with a pane already hidden, in either order), and this
/// flag is what keeps exactly one of them holding the snapshot at a time.
constexpr auto kNativeHideAppliedProperty = "kartendNativeHideApplied";

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
QColor computeHandleColor() {
  // The TITLEBAR colour itself. It was briefly lightened because the tree's
  // selection ran under the handle and swallowed it; the selection now stops
  // at the lane instead, so the handle can be the colour the user actually
  // asked for. Palette highlight is the off-KDE fallback.
  // Re-read every paint, NOT cached for the session: Plasma changes these
  // colours under a running app (activity switch with a per-activity
  // wallpaper, 2026-08-19), and a static cache would hold the colour the
  // app happened to start with.
  const QColor titlebar = KdeColorScheme::activeTitlebarColor();
  QColor ink = titlebar.isValid() ? titlebar : QApplication::palette().color(QPalette::Highlight);
  ink.setAlpha(kHandleAlpha);
  return ink;
}

/// Width of the lane kept clear for the handle. RESERVED PERMANENTLY, not
/// while the bar happens to be visible: content must never sit under the
/// handle (field report 2026-08-19 — "the grid scrollbar also overlaps the
/// items"), and a lane that came and went with the bar would move every
/// item each time it faded, which is exactly what the overlay design was
/// meant to stop.
constexpr int kGutter = kThickness + kEdgeMargin * 2;

/// Force the native bars off on behalf of a hide intent, remembering what
/// they were. No-op if the hide is already holding them.
void engageNativeHide(QAbstractScrollArea *area) {
  if (area->property(kNativeHideAppliedProperty).toBool()) {
    return;
  }
  area->setProperty(kHiddenSavedVerticalProperty,
                    static_cast<int>(area->verticalScrollBarPolicy()));
  area->setProperty(kHiddenSavedHorizontalProperty,
                    static_cast<int>(area->horizontalScrollBarPolicy()));
  area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area->setProperty(kNativeHideAppliedProperty, true);
}

/// Hand the native policies back to whatever they were before the hide took
/// them. Restoring the SNAPSHOT rather than assuming ScrollBarAsNeeded is
/// the whole point: the details pane's content view is deliberately
/// AlwaysOff, and a blanket restore would give it a bar it never had.
void releaseNativeHide(QAbstractScrollArea *area) {
  if (!area->property(kNativeHideAppliedProperty).toBool()) {
    return;
  }
  area->setVerticalScrollBarPolicy(
      static_cast<Qt::ScrollBarPolicy>(area->property(kHiddenSavedVerticalProperty).toInt()));
  area->setHorizontalScrollBarPolicy(
      static_cast<Qt::ScrollBarPolicy>(area->property(kHiddenSavedHorizontalProperty).toInt()));
  area->setProperty(kNativeHideAppliedProperty, false);
}

/// One painted handle, parented to the VIEWPORT and drawn at its right
/// edge. Parenting it to the scroll area instead put it beyond the
/// viewport, where the narrow nav pane clipped it away entirely (field
/// report 2026-08-19: "the nav pane scrollbar is not visible"). Content is
/// kept out from under it by reservedGutter(), which is a LAYOUT value —
/// the handle floats over its own lane, which is how it stays visible.
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

  [[nodiscard]] ScrollbarMode mode() const {
    if (!m_area) {
      return ScrollbarMode::Show;
    }
    const char *key =
        m_orientation == Qt::Vertical ? kVerticalModeProperty : kHorizontalModeProperty;
    return static_cast<ScrollbarMode>(m_area->property(key).toInt());
  }

  /// THE choke point. EVERY path that can make the handle visible consults
  /// this — not only syncGeometry(). flash() used to call syncGeometry()
  /// (which hides) and then show() unconditionally, so a hidden handle was
  /// resurrected on the next hover or scroll tick; and because show()/hide()
  /// make Qt synthesise enter/leave events that come straight back through
  /// the controller's filter into flash(), the two fought until the stack ran
  /// out (SIGSEGV, 2026-08-19, Kartend-axlod). Autohide is a third answer
  /// here rather than a parallel mechanism precisely so it cannot reopen that.
  [[nodiscard]] bool shouldStayHidden() const {
    switch (mode()) {
    case ScrollbarMode::Hide:
      return true;
    case ScrollbarMode::Autohide:
      return !m_nearLane;
    case ScrollbarMode::Show:
      break;
    }
    return false;
  }

  /// Pointer proximity to this bar's lane, pushed by the controller. Only
  /// Autohide consults it; the other modes ignore it entirely.
  void setNearLane(bool near) {
    if (m_nearLane == near) {
      return;
    }
    m_nearLane = near;
    if (mode() != ScrollbarMode::Autohide) {
      return;
    }
    if (near) {
      flash();
    } else {
      fadeTo(0.0);
    }
  }

  void setHovered(bool hovered) {
    m_hovered = hovered;
    if (shouldStayHidden()) {
      return; // hidden outright, or autohide with the pointer still far away
    }
    if (hovered) {
      flash();
    } else {
      m_idle.start();
    }
  }

  void syncGeometry() {
    // Re-entrancy guard: the hide() and setGeometry() below both make Qt
    // deliver synthetic enter/leave and resize events, which the controller's
    // filter routes back here. One round trip must not land on a frame that
    // is still running.
    if (m_syncing) {
      return;
    }
    QScopedValueRollback<bool> guard(m_syncing, true);
    QScrollBar *bar = source();
    QWidget *viewport = m_area ? m_area->viewport() : nullptr;
    if (!bar || !viewport) {
      return;
    }
    if (bar->maximum() <= bar->minimum()) {
      hide(); // nothing to scroll: no handle at all
      return;
    }
    if (shouldStayHidden()) {
      hide();
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
    const qreal progress =
        bar->maximum() > bar->minimum()
            ? static_cast<qreal>(bar->value() - bar->minimum()) / (bar->maximum() - bar->minimum())
            : 0.0;
    const int offset = static_cast<int>(travel * progress);

    QColor ink = OverlayScrollbars::handleColor();
    ink.setAlpha(static_cast<int>(ink.alpha() * m_opacity));
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    const QRectF handle = m_orientation == Qt::Vertical ? QRectF(0, offset, kThickness, handleLen)
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
    bar->setValue(bar->minimum() + static_cast<int>((bar->maximum() - bar->minimum()) * fraction));
  }

  void flash() {
    // FIRST, before syncGeometry() — showing a handle the user hid is the
    // bug, and doing it via a hide-then-show pair is what made it a crash.
    if (shouldStayHidden()) {
      return;
    }
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
  /// True while syncGeometry() is running — see the guard there.
  bool m_syncing = false;
  /// Pointer is within kAutohideProximityPx of this bar's lane. Autohide only.
  bool m_nearLane = false;
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
    // setFixedWidth alone is NOT enough. QAbstractScrollArea reserves space
    // from the bar's SIZE HINT, and the platform style (Breeze here) reports a
    // hint independent of the widget's fixed size. So the bar was pinned to
    // 0px and drew nothing while the area went on reserving its styled width —
    // measured 2026-08-20: viewport 1345 inside a 1366 area, vbarW=0, 21px of
    // reserved-but-empty lane sitting between the grid and the details pane.
    // That band survived every alignment change because it is not part of the
    // grid at all. A style rule zeroes the hint as well as the widget.
    static constexpr auto kNoNativeBar =
        "QScrollBar:vertical { width: 0px; min-width: 0px; max-width: 0px; }"
        "QScrollBar:horizontal { height: 0px; min-height: 0px; max-height: 0px; }";
    if (QScrollBar *bar = area->verticalScrollBar()) {
      bar->setStyleSheet(kNoNativeBar);
      bar->setFixedWidth(0);
    }
    if (QScrollBar *bar = area->horizontalScrollBar()) {
      bar->setStyleSheet(kNoNativeBar);
      bar->setFixedHeight(0);
    }
    // Autohide needs the pointer's position, not just enter/leave. POLLED
    // rather than tracked via mouse-move events: turning on mouse tracking
    // for the viewport would start delivering moves to whatever else is
    // listening on these widgets, and this only has to be accurate to about
    // a frame. Runs solely between Enter and Leave, so an idle window costs
    // nothing.
    m_proximity.setInterval(kProximityPollMs);
    connect(&m_proximity, &QTimer::timeout, this, [this] { updateProximity(); });
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

  /// Re-evaluate both handles now. A hide toggle is not a scroll and not a
  /// resize, so without this the handle lingers until the next such event —
  /// the setting would look like it took a scroll to apply.
  void syncNow() {
    if (m_vertical) m_vertical->syncGeometry();
    if (m_horizontal) m_horizontal->syncGeometry();
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
      updateProximity();
      m_proximity.start();
      break;
    case QEvent::Leave:
      m_proximity.stop();
      m_vertical->setNearLane(false);
      m_horizontal->setNearLane(false);
      m_vertical->setHovered(false);
      m_horizontal->setHovered(false);
      break;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  /// Near the lane means near the EDGE the bar lives on, measured in the
  /// viewport's own coordinates. The bar itself is only kThickness wide, far
  /// too thin to aim at, which is the whole reason autohide needs a band.
  void updateProximity() {
    auto *area = qobject_cast<QAbstractScrollArea *>(parent());
    QWidget *vp = area ? area->viewport() : nullptr;
    if (!vp || !m_vertical || !m_horizontal) {
      return;
    }
    const QPoint p = vp->mapFromGlobal(QCursor::pos());
    const bool inside = vp->rect().contains(p);
    m_vertical->setNearLane(inside && p.x() >= vp->width() - kAutohideProximityPx);
    m_horizontal->setNearLane(inside && p.y() >= vp->height() - kAutohideProximityPx);
  }

  QPointer<OverlayBar> m_vertical;
  QPointer<OverlayBar> m_horizontal;
  QTimer m_proximity;
};

} // namespace

namespace OverlayScrollbars {

QColor handleColor() {
  return computeHandleColor();
}

bool isAttached(const QWidget *area) {
  return area && area->property(kAttachedProperty).value<QObject *>() != nullptr;
}

int reservedGutter(const QWidget *area) {
  // Zero unless overlay bars are actually attached, so the layout only pays
  // for the lane when a handle can appear in it.
  if (!area || !area->property(kAttachedProperty).value<QObject *>()) {
    return 0;
  }
  // The gutter is the VERTICAL bar's lane (callers subtract it from usable
  // width), so the vertical mode is the one that decides.
  //
  // Only SHOW reserves it. A permanently visible handle over the items was
  // the original complaint (2026-08-19: "the grid scrollbar also overlaps
  // the items"), so Show keeps its lane. Hide reclaims it — no handle can
  // ever appear. AUTOHIDE now reclaims it too (2026-08-20: "still seeing a
  // large margin between the details pane and the grid. or could it be due
  // to the scrollbar area?" — it was): the lane stood empty except for the
  // moment the pointer neared the edge, and covering item edges briefly is
  // the same trade the maintainer keeps choosing — chrome may overlap
  // content, it must never move it. The reservation is still CONSTANT per
  // mode, so nothing shifts when the handle fades in or out.
  switch (static_cast<ScrollbarMode>(area->property(kVerticalModeProperty).toInt())) {
  case ScrollbarMode::Show:
    return kGutter;
  case ScrollbarMode::Autohide:
  case ScrollbarMode::Hide:
    return 0;
  }
  return kGutter;
}

void apply(QAbstractScrollArea *area, bool enabled) {
  if (!area) {
    return;
  }
  auto *existing = area->property(kAttachedProperty).value<QObject *>();
  if (enabled == (existing != nullptr)) {
    return; // already in the requested state
  }
  if (enabled) {
    // Let the hide give the policies back first, so the snapshot below
    // captures what the surface really wants rather than the hide's
    // AlwaysOff. The handles honour the hide intent by themselves from here.
    releaseNativeHide(area);
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
    bar->setStyleSheet(QString()); // drop the zero-width rule with the pin
    bar->setMinimumWidth(0);
    bar->setMaximumWidth(QWIDGETSIZE_MAX);
  }
  if (QScrollBar *bar = area->horizontalScrollBar()) {
    bar->setStyleSheet(QString());
    bar->setMinimumHeight(0);
    bar->setMaximumHeight(QWIDGETSIZE_MAX);
  }
  existing->deleteLater();
  area->setProperty(kAttachedProperty, QVariant());
  area->setVerticalScrollBarPolicy(
      static_cast<Qt::ScrollBarPolicy>(area->property(kSavedVerticalProperty).toInt()));
  area->setHorizontalScrollBarPolicy(
      static_cast<Qt::ScrollBarPolicy>(area->property(kSavedHorizontalProperty).toInt()));
  // The handles are gone, so a standing Hide has to go back to holding the
  // native bars off — otherwise turning slim bars OFF hands a scrollbar back
  // to a pane the user hid. Autohide deliberately does NOT re-engage: without
  // a handle it degrades to Show, per setScrollbarMode.
  if (static_cast<ScrollbarMode>(area->property(kVerticalModeProperty).toInt()) ==
      ScrollbarMode::Hide) {
    engageNativeHide(area);
  }
}

void setScrollbarMode(QAbstractScrollArea *area, ScrollbarMode mode) {
  if (!area) {
    return;
  }
  area->setProperty(kVerticalModeProperty, static_cast<int>(mode));
  area->setProperty(kHorizontalModeProperty, static_cast<int>(mode));

  // Only this TU ever writes kAttachedProperty, so the cast is sound.
  if (auto *controller =
          static_cast<OverlayController *>(area->property(kAttachedProperty).value<QObject *>())) {
    // The overlay owns the native policies while attached (both forced OFF)
    // and its handles consult the properties just set — nothing to do to the
    // policies here, just make the handles re-read now.
    controller->syncNow();
    return;
  }
  // No overlay attached: the native bars are all there is. Autohide has no
  // native equivalent — proximity is a property of the painted handle — so it
  // falls back to Show rather than hiding a bar that could never return.
  if (mode == ScrollbarMode::Hide) {
    engageNativeHide(area);
  } else {
    releaseNativeHide(area);
  }
}

void setPaneScrollbarMode(QWidget *root, ScrollbarMode mode) {
  if (!root) {
    return;
  }
  if (auto *self = qobject_cast<QAbstractScrollArea *>(root)) {
    setScrollbarMode(self, mode);
  }
  const auto areas = root->findChildren<QAbstractScrollArea *>();
  for (QAbstractScrollArea *area : areas) {
    setScrollbarMode(area, mode);
  }
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
