// Media item widget displaying artwork, title, and selection state with pulse
// animation.
#include "itemwidget.h"
#include "artworkutils.h"
#include "propertyutils.h"
#include "uiconstants/animation.h"
#include "uiconstants/collectionicon.h"
#include "uiconstants/color.h"
#include "uiconstants/item.h"
#include "uiconstants/widget.h"
#include <algorithm>
#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QLinearGradient>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPolygon>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QStyle>
#include <QTimer>

Q_LOGGING_CATEGORY(lcItemWidget, "kartend.itemwidget")

// Static configuration members - initialized to UIConstants defaults
bool ItemWidget::s_titleTintEnabled = false;
int ItemWidget::s_titleTintSaturation = UIConstants::Color::TITLE_TINT_SATURATION;
int ItemWidget::s_titleTintLightness = UIConstants::Color::TITLE_TINT_LIGHTNESS;
QString ItemWidget::s_titleBaseColor;
QString ItemWidget::s_customFontFamily;
QString ItemWidget::s_primaryColor;
QString ItemWidget::s_tileColor;
QString ItemWidget::s_selectionColor;
QString ItemWidget::s_listRowColor;
QString ItemWidget::s_listAltRowColor;
bool ItemWidget::s_showTitleInPlaceholder = false;

// Per-item state-flag registry (Kartend-elte). Keyed by absolute filePath
// because that's what every ItemWidget carries; MainWindow populates the
// hash from IDatabaseManager::fetchItemStateFlagsForCollection on every
// collection switch. Function-local static lets the sibling TU
// (itemwidgetpaint.cpp) reach the same storage via stateFlagsRegistry()
// without the linker drama of an anonymous-namespace global.
QHash<QString, ItemWidget::StateFlags> &itemWidgetStateFlagsRegistry() {
  static QHash<QString, ItemWidget::StateFlags> table;
  return table;
}

void ItemWidget::setTitleTintSaturation(int saturation) {
  s_titleTintSaturation = qBound(0, saturation, 255);
}

void ItemWidget::setTitleTintLightness(int lightness) {
  s_titleTintLightness = qBound(0, lightness, 255);
}

void ItemWidget::setTitleBaseColor(const QString &hexColor) {
  s_titleBaseColor = hexColor;
}

void ItemWidget::setTitleTintEnabled(bool enabled) {
  s_titleTintEnabled = enabled;
}

void ItemWidget::setCustomFontFamily(const QString &fontFamily) {
  s_customFontFamily = fontFamily;
}

void ItemWidget::setPrimaryColor(const QString &hexColor) {
  s_primaryColor = hexColor;
}

void ItemWidget::setTileColor(const QString &hexColor) {
  s_tileColor = hexColor;
}

void ItemWidget::setSelectionColor(const QString &hexColor) {
  s_selectionColor = hexColor;
}

void ItemWidget::setListRowColor(const QString &hexColor) {
  s_listRowColor = hexColor;
}

void ItemWidget::setListAltRowColor(const QString &hexColor) {
  s_listAltRowColor = hexColor;
}

void ItemWidget::setShowTitleInPlaceholder(bool enabled) {
  s_showTitleInPlaceholder = enabled;
}

void ItemWidget::setStateFlagsRegistry(const QHash<QString, StateFlags> &flags) {
  itemWidgetStateFlagsRegistry() = flags;
  // Repaint every visible ItemWidget so a collection-switch / settings-save
  // refresh surfaces the new flags immediately. findChildren would scope
  // to a single parent; we walk the global widget list because the items
  // page rebuilds widgets out of a pool and they can land under different
  // parents over time.
  const auto topLevels = QApplication::topLevelWidgets();
  for (QWidget *top : topLevels) {
    const auto items = top->findChildren<ItemWidget *>();
    for (ItemWidget *w : items) {
      if (w->isVisible()) w->update();
    }
  }
}

void ItemWidget::clearStateFlagsRegistry() {
  setStateFlagsRegistry({});
}

ItemWidget::ItemWidget(QWidget *parent)
    : QWidget(parent)

      ,
      m_itemWidth(UIConstants::Item::DEFAULT_WIDTH),
      m_itemHeight(UIConstants::Item::DEFAULT_HEIGHT), triangleIndicator(nullptr) {
  Ui::ItemWidget itemUi;
  itemUi.setupUi(this);
  setMouseTracking(true);
  const QList<QWidget *> childWidgets = findChildren<QWidget *>();
  for (QWidget *child : childWidgets) {
    child->setMouseTracking(true);
  }
  imageLabel = itemUi.imageLabel;
  nameLabel = itemUi.nameLabel;
  triangleIndicator = itemUi.triangleIndicator;
  setFocusPolicy(Qt::NoFocus);
  // Allow children (nameLabel) to overflow widget bounds for long titles
  setAttribute(Qt::WA_TransparentForMouseEvents, false);
  if (imageLabel) {
    imageLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }
  if (nameLabel) {
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    nameLabel->setTextFormat(Qt::PlainText);
    nameLabel->setAutoFillBackground(false);
  }
  if (triangleIndicator) {
    triangleIndicator->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }
  applyTitleTint();

  m_pulseDelayTimer.setSingleShot(true);
  m_pulseDelayTimer.setInterval(UIConstants::Animation::PULSE_INACTIVITY_DELAY_MS);
  connect(&m_pulseDelayTimer, &QTimer::timeout, this, &ItemWidget::startPulseAnimation);
}

ItemWidget::~ItemWidget() {
  if (pulseAnimation) {
    pulseAnimation->stop();
    delete pulseAnimation;
    pulseAnimation = nullptr;
  }
  m_pulseDelayTimer.stop();
  storedPixmap = QPixmap();
}

// Compute title tint from highlight color with configurable
// saturation/lightness
// Kartend-bbcu6: the colour item TITLE TEXT is painted in.
//
// Distinct from titleTint() below, which is the accent derivation and stays
// unconditional because the placeholder tile's hatch pattern is keyed off it
// (currentTileTheme()) and must keep tracking the system accent whether or not
// titles are tinted. Collapsing the two broke exactly that — the hatch went
// achromatic and stopped following accent changes.
auto ItemWidget::titleTextColor() -> QColor {
  if (s_titleTintEnabled) {
    return titleTint();
  }
  // Default: the palette's text colour, like every other label in the app.
  // WindowText rather than Text because these captions sit on the window
  // background, not inside an item view — the roles differ under some schemes.
  //
  // Not merely a taste default. titleTint() forces ABSOLUTE saturation and
  // lightness onto the highlight's hue, so a pair that reads well on a light
  // background lands near background luminance on a dark one: the old
  // unconditional default measured 1.63:1 against Breeze Dark, failing both
  // WCAG AA (4.5:1) and the 3:1 large-text floor. A palette colour cannot fail
  // that way — the theme already guarantees it against its own background.
  return QApplication::palette().color(QPalette::WindowText);
}

auto ItemWidget::titleTint() -> QColor {
  // Title tint is NOT affected by per-collection primary color
  // Primary color only affects selection borders and placeholder patterns
  QColor baseColor;
  if (!s_titleBaseColor.isEmpty() && QColor::isValidColorName(s_titleBaseColor)) {
    baseColor = QColor(s_titleBaseColor);
  } else {
    baseColor = QApplication::palette().color(QPalette::Highlight);
  }

  int hue = 0;
  int saturation = 0;
  int lightness = 0;
  int alpha = 0;
  baseColor.getHsl(&hue, &saturation, &lightness, &alpha);

  int targetLightness = qBound(0, s_titleTintLightness, UIConstants::Color::CHANNEL_MAX);
  int targetSaturation = qBound(0, s_titleTintSaturation, UIConstants::Color::CHANNEL_MAX);

  QColor color;
  color.setHsl(hue, targetSaturation, targetLightness, UIConstants::Color::CHANNEL_MAX);
  return color;
}

void ItemWidget::setupPulseAnimation() {
  if (pulseAnimation) {
    pulseAnimation->stop();
    delete pulseAnimation;
  }
  pulseAnimation = new QPropertyAnimation(this, "pulseOpacity");
  pulseAnimation->setDuration(UIConstants::Animation::PULSE_DURATION_MS);
  pulseAnimation->setKeyValueAt(0.0, UIConstants::Animation::PULSE_OPACITY_LOW);
  pulseAnimation->setKeyValueAt(UIConstants::Animation::PULSE_KEYFRAME_MID_POS,
                                UIConstants::Animation::PULSE_OPACITY_HIGH);
  pulseAnimation->setKeyValueAt(1.0, UIConstants::Animation::PULSE_OPACITY_LOW);
  pulseAnimation->setLoopCount(-1);
  pulseAnimation->setEasingCurve(QEasingCurve::InOutSine);
}

void ItemWidget::startPulseAnimation() {
  if (!isSelectedState) {
    return;
  }

  if (!pulseAnimation) {
    setupPulseAnimation();
  }

  if ((pulseAnimation) && pulseAnimation->state() != QAbstractAnimation::Running) {
    pulseAnimation->start();
  }
}

// Apply title tint - caches the color for custom painting in paintEvent
// Qt 6.9.2 ignores QLabel stylesheets, so we paint the text manually
void ItemWidget::applyTitleTint() {
  m_titleTintColor = titleTextColor();
  if (nameLabel) {
    // Make label text transparent - we'll paint it ourselves in paintEvent
    nameLabel->setStyleSheet(
        QStringLiteral("QLabel { color: transparent; background: transparent; }"));
  }
  update(); // Trigger repaint to draw tinted text
}

// Handle mouse press - only tracks click position for double-click detection.
// EventManager intercepts all clicks before they reach the widget.
void ItemWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_lastClickPos = event->pos();
    m_lastClickTimer.start();
  }
  QWidget::mousePressEvent(event);
}

// Handle mouse double click
void ItemWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    // Trust Qt's double-click detection - it already validated timing.
    // Only check position tolerance to ensure clicks are on the same spot.
    bool validDoubleClick = true;

    if (m_lastClickTimer.isValid()) {
      QPoint currentPos = event->pos();
      int distance = (currentPos - m_lastClickPos).manhattanLength();
      if (distance > CLICK_POSITION_TOLERANCE) {
        validDoubleClick = false;
      }
    }

    if (validDoubleClick) {
      // Guard against widget deletion during signal handling -
      // subcollection navigation may destroy this widget
      QPointer<ItemWidget> guard = this;

      // Only subcollection double-clicks reach here - EventManager passes them
      // through
      if (m_isSubcollection) {
        emit subcollectionDoubleClicked(m_subcollectionIndex);
      } else if (m_isVirtualFolder) {
        emit virtualFolderDoubleClicked(m_virtualFolderPath);
      }
      // Media item double-clicks are handled by
      // EventManager::widgetDoubleClicked

      // Widget may have been deleted during navigation - don't access members
      if (!guard) {
        return;
      }
    }

    m_lastClickTimer.invalidate();
  }
  QWidget::mouseDoubleClickEvent(event);
}

// Enter event
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ItemWidget::enterEvent(QEnterEvent *event)
#else
void ItemWidget::enterEvent(QEvent *event)
#endif
{
  QWidget::enterEvent(event);
}

// Leave event
void ItemWidget::leaveEvent(QEvent *event) {
  QWidget::leaveEvent(event);
}

// Show event
void ItemWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
}

// Handles palette change to trigger artwork change respecting
// DeferArtworkUpdate
auto ItemWidget::event(QEvent *event) -> bool {
  bool handled = QWidget::event(event);
  if (event->type() == QEvent::PaletteChange) {
    applyTitleTint();
    // The subcollection triangle badge renders the Highlight fallback into a
    // child QLabel pixmap (only on (re)selection/resize otherwise), so a
    // repaint won't refresh it — rebuild it here. No-ops for non-subcollections.
    updateTriangleIndicator();
    if (imageLabel && !property(PropertyKeys::DeferArtworkUpdate).toBool() &&
        !m_paletteArtworkRefreshPending) {
      // A single accent change can deliver two PaletteChange events in one
      // event-loop turn (the re-broadcast forces the palette past Qt's no-op
      // check by cycling through a throwaway palette first). Coalesce them so
      // the artwork re-render runs once, with the final palette — re-rendering
      // every visible tile twice was a chunk of the perceived lag.
      m_paletteArtworkRefreshPending = true;
      QPointer<ItemWidget> ptr = this;
      // Defer artwork update until after the palette change fully propagates -
      // ensures consistent appearance when switching light/dark themes
      QTimer::singleShot(0, this, [ptr]() {
        if (ptr) {
          ptr->m_paletteArtworkRefreshPending = false;
          ptr->onArtworkChanged();
        }
      });
    }
  }
  return handled;
}

// Resize event
void ItemWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (m_isSubcollection) {
    updateTriangleIndicator();
  }
}

// Sets selected state and respects glide-animating property via PropertyKeys
void ItemWidget::setSelected(bool selected) {
  const bool glideActive = isGlideActive();
  if (isSelectedState == selected) {
    return;
  }

  isSelectedState = selected;
  if (selected) {
    applySelectedUiEffects();
  } else {
    applyDeselectedUiEffects();
  }

  if (!glideActive) {
    scheduleSelectionBorderUpdate();
  }
}

// Returns true when a glide animation is active up the parent chain
auto ItemWidget::isGlideActive() const -> bool {
  const QWidget *parentPtr = parentWidget();
  if ((parentPtr) && parentPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    return true;
  }
  const QWidget *grandparentPtr = (parentPtr) ? parentPtr->parentWidget() : nullptr;
  return (grandparentPtr) && grandparentPtr->property(PropertyKeys::GlideAnimating).toBool();
}

// Computes the selection border rectangle - around artwork in grid mode, full
// widget in list mode
auto ItemWidget::computeSelectionBorderRect() const -> QRect {
  // In list mode, selection covers the entire row
  if (m_isListMode) {
    return rect().adjusted(1, 1, -1, -1); // Slight inset for clean border
  }

  if (!imageLabel) {
    return {};
  }
  const QRect imageRect = imageLabel->geometry();

  const int left = imageRect.left() - UIConstants::CollectionIcon::ITEM_SPACING;
  const int top = imageRect.top() - UIConstants::CollectionIcon::ITEM_SPACING;
  const int right = imageRect.right() + UIConstants::CollectionIcon::ITEM_SPACING;
  const int bottom = imageRect.bottom() + UIConstants::CollectionIcon::ITEM_SPACING;

  return {left, top, right - left, bottom - top};
}

auto ItemWidget::selectionBorderRectInParent() const -> QRect {
  QRect localRect = computeSelectionBorderRect();
  if (!localRect.isValid()) {
    return {};
  }
  const QPoint topLeft = mapToParent(localRect.topLeft());
  const QPoint bottomRight = mapToParent(localRect.bottomRight());
  return QRect(topLeft, bottomRight);
}

// Applies visual effects for selection state, preserving animation behavior
void ItemWidget::applySelectedUiEffects() {
  raise();
  if (m_isSubcollection && (triangleIndicator)) {
    updateTriangleIndicator();
  }
  if (!pulseAnimation) {
    setupPulseAnimation();
  }
  if ((pulseAnimation) && pulseAnimation->state() != QAbstractAnimation::Running) {
    pulseAnimation->start();
  }
}

// Applies visual changes when deselected
void ItemWidget::applyDeselectedUiEffects() {
  // Stop pulse animation when deselected to prevent visual artifacts
  if (pulseAnimation && pulseAnimation->state() == QAbstractAnimation::Running) {
    pulseAnimation->stop();
  }
  if (m_pulseDelayTimer.isActive()) {
    m_pulseDelayTimer.stop();
  }
  m_pulseOpacity = UIConstants::Animation::PULSE_OPACITY_LOW;

  if (triangleIndicator) {
    triangleIndicator->hide();
  }
}

// Schedules repaint of selection border region
void ItemWidget::scheduleSelectionBorderUpdate() {
  if (imageLabel) {
    const QRect borderRect = computeSelectionBorderRect();
    update(borderRect.adjusted(
        -UIConstants::Widget::BORDER_WIDTH_SELECTION, -UIConstants::Widget::BORDER_WIDTH_SELECTION,
        UIConstants::Widget::BORDER_WIDTH_SELECTION, UIConstants::Widget::BORDER_WIDTH_SELECTION));
  } else {
    update();
  }
}

// Updates the artwork image label respecting DeferArtworkUpdate and
// ForcePlaceholder. Preserves device pixel ratio for crisp high-DPI rendering.
void ItemWidget::onArtworkChanged() {
  if (!imageLabel) {
    return;
  }

  bool shouldDefer = property(PropertyKeys::DeferArtworkUpdate).toBool();
  bool forcePlaceholder = property(PropertyKeys::ForcePlaceholder).toBool();
  if (shouldDefer || forcePlaceholder) {
    int width = imageLabel->width();
    int height = imageLabel->height();
    if (width <= 0 || height <= 0) {
      return;
    }
    QPixmap placeholder = buildPlaceholderPattern(width, height);
    drawTitleOnPlaceholder(placeholder);
    imageLabel->setPixmap(placeholder);
    imageLabel->setStyleSheet(QString());
    return;
  }

  int width = imageLabel->width();
  int height = imageLabel->height();

  // Guard against zero-sized widgets (can happen during layout transitions)
  if (width <= 0 || height <= 0) {
    return;
  }

  const QPixmap displayPixmap = storedPixmap.isNull() ? m_placeholderArtworkPixmap : storedPixmap;

  if (displayPixmap.isNull()) {
    QPixmap placeholder = buildPlaceholderPattern(width, height);
    drawTitleOnPlaceholder(placeholder);
    imageLabel->setPixmap(placeholder);
    imageLabel->setStyleSheet(QString());
  } else if (m_storedIsComposed && !storedPixmap.isNull() &&
             displayPixmap.deviceIndependentSize().toSize() == QSize(width, height)) {
    // Kartend-63wg: the worker already produced the final corner-masked card at
    // this tile's size — set it straight onto the label with no scale/composite.
    // The size guard matters on every entry path (deferred applyDimensions
    // refresh, DeferArtworkUpdate release, palette change): a card composed
    // for a previous tile size must not be set 1:1, and re-compositing it
    // would double its border and corner mask.
    imageLabel->setPixmap(displayPixmap);
    imageLabel->setStyleSheet(QString());
  } else if (m_storedIsComposed && !storedPixmap.isNull()) {
    // Stale-size composed card (tile dimensions changed after delivery). The
    // card is display-final — compositing it again doubles the border and
    // re-masks the corners — and the raw artwork is not held here, so scale
    // it as a stopgap. ArtworkManager::addPendingArtwork treats a stale-size
    // card as "needs re-delivery" (hasStaleComposedArtwork), so the next
    // configure pass replaces this with a card built for the current size.
    qreal dpr = 1.0;
    if (const QScreen *scr = screen()) {
      dpr = scr->devicePixelRatio();
    } else if (QGuiApplication::primaryScreen()) {
      dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    }
    QPixmap scaled = displayPixmap.scaled(qRound(width * dpr), qRound(height * dpr),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    imageLabel->setPixmap(scaled);
    imageLabel->setStyleSheet(QString());
  } else {
    // The DPR of the screen this widget is actually on (mixed-DPI setups),
    // falling back to the primary screen for a not-yet-shown widget.
    qreal dpr = 1.0;
    if (const QScreen *scr = screen()) {
      dpr = scr->devicePixelRatio();
    } else if (QGuiApplication::primaryScreen()) {
      dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    }

    // Kartend-63wg: scale-to-fit + center-on-background + corner-mask now lives
    // in ArtworkUtils::composeArtworkCard so the worker can produce the same
    // card off-thread; this GUI path is the placeholder + size-mismatch
    // fallback, kept pixel-identical by sharing that helper.
    const QImage card =
        ArtworkUtils::composeArtworkCard(displayPixmap.toImage(), width, height, dpr,
                                         m_cornerRadius, palette().color(QPalette::Mid));
    QPixmap resultPixmap = QPixmap::fromImage(card);

    // user-supplied placeholder image is also "placeholder art" —
    // overlay the title only when the real artwork is missing (storedPixmap
    // null), not on actual artwork hits.
    if (storedPixmap.isNull()) {
      drawTitleOnPlaceholder(resultPixmap, dpr);
    }

    imageLabel->setPixmap(resultPixmap);
    imageLabel->setStyleSheet(QString());
  }
  if (nameLabel) {
    nameLabel->raise();
  }
}

ItemWidget::ArtworkRenderSpec ItemWidget::artworkRenderSpec() const {
  // Kartend-63wg: snapshot the inputs onArtworkChanged composes from, so the
  // artwork worker can build the finished card off-thread. An unsized label
  // (not laid out yet) yields an empty labelSize → the worker skips compositing
  // and the GUI composites on delivery.
  ArtworkRenderSpec spec;
  if (imageLabel) {
    spec.labelSize = imageLabel->size();
  }
  spec.cornerRadius = m_cornerRadius;
  spec.background = palette().color(QPalette::Mid);
  // The DPR of the screen this widget actually sits on, so mixed-DPI setups
  // decode + compose for the right display; primary-screen fallback for a
  // widget not yet attached to a screen. GUI-thread only, like the rest of
  // this snapshot.
  if (const QScreen *scr = screen()) {
    spec.dpr = scr->devicePixelRatio();
  } else if (QGuiApplication::primaryScreen()) {
    spec.dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
  }
  return spec;
}

bool ItemWidget::hasStaleComposedArtwork() const {
  if (!m_storedIsComposed || storedPixmap.isNull() || !imageLabel) {
    return false;
  }
  // A worker-composed card is final (scaled, centered, corner-masked) and can
  // only be shown 1:1 — compare its logical size against the label it would
  // be set on. deviceIndependentSize folds the card's DPR back out, so the
  // comparison is DPR-agnostic.
  return storedPixmap.deviceIndependentSize().toSize() != imageLabel->size();
}
