// Media item widget displaying artwork, title, and selection state with pulse
// animation.
#include "itemwidget.h"
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

void ItemWidget::setTitleTintSaturation(int saturation) {
  s_titleTintSaturation = qBound(0, saturation, 255);
}

void ItemWidget::setTitleTintLightness(int lightness) {
  s_titleTintLightness = qBound(0, lightness, 255);
}

void ItemWidget::setTitleBaseColor(const QString &hexColor) {
  s_titleBaseColor = hexColor;
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

ItemWidget::ItemWidget(QWidget *parent)
    : QWidget(parent)

      ,
      m_itemWidth(UIConstants::Item::DEFAULT_WIDTH),
      m_itemHeight(UIConstants::Item::DEFAULT_HEIGHT), triangleIndicator(nullptr),
      m_pulseDelayTimer(nullptr) {
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

  m_pulseDelayTimer = new QTimer(this);
  m_pulseDelayTimer->setSingleShot(true);
  m_pulseDelayTimer->setInterval(UIConstants::Animation::PULSE_INACTIVITY_DELAY_MS);
  connect(m_pulseDelayTimer, &QTimer::timeout, this, &ItemWidget::startPulseAnimation);
}

ItemWidget::~ItemWidget() {
  if (pulseAnimation) {
    pulseAnimation->stop();
    delete pulseAnimation;
    pulseAnimation = nullptr;
  }
  if (m_pulseDelayTimer) {
    m_pulseDelayTimer->stop();
    delete m_pulseDelayTimer;
    m_pulseDelayTimer = nullptr;
  }
  storedPixmap = QPixmap();
}

// Compute title tint from highlight color with configurable
// saturation/lightness
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
  m_titleTintColor = titleTint();
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
    if (imageLabel && !property(PropertyKeys::DeferArtworkUpdate).toBool()) {
      QPointer<ItemWidget> ptr = this;
      // Defer artwork update until after the palette change fully propagates -
      // ensures consistent appearance when switching light/dark themes
      QTimer::singleShot(0, this, [ptr]() {
        if (ptr) {
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
  if (m_pulseDelayTimer && m_pulseDelayTimer->isActive()) {
    m_pulseDelayTimer->stop();
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
  } else {
    // Get the screen DPR for the final output
    qreal dpr = 1.0;
    if (QGuiApplication::primaryScreen()) {
      dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    }

    // Physical dimensions for the output
    int physicalW = qRound(width * dpr);
    int physicalH = qRound(height * dpr);

    // Create a copy of source with DPR=1 so we work in raw physical pixels
    QPixmap sourceNoDpr = displayPixmap;
    sourceNoDpr.setDevicePixelRatio(1.0);

    // Scale to fit within physical target size
    QPixmap scaledArtwork =
        sourceNoDpr.scaled(physicalW, physicalH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Create result at physical size
    QPixmap resultPixmap(physicalW, physicalH);
    resultPixmap.fill(palette().color(QPalette::Mid));

    // Center using physical pixel coordinates
    {
      QPainter painter(&resultPixmap);
      painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
      int offsetX = (physicalW - scaledArtwork.width()) / 2;
      int offsetY = (physicalH - scaledArtwork.height()) / 2;
      painter.drawPixmap(offsetX, offsetY, scaledArtwork);
    }

    // Apply corner radius masking (in physical pixels)
    if (m_cornerRadius > 0) {
      int physicalRadius = qRound(m_cornerRadius * dpr);
      QPixmap maskedPixmap(physicalW, physicalH);
      maskedPixmap.fill(Qt::transparent);

      QPainter maskPainter(&maskedPixmap);
      maskPainter.setRenderHint(QPainter::Antialiasing, true);

      QPainterPath clipPath;
      clipPath.addRoundedRect(QRectF(0, 0, physicalW, physicalH), physicalRadius, physicalRadius);
      maskPainter.setClipPath(clipPath);
      maskPainter.drawPixmap(0, 0, resultPixmap);
      maskPainter.end();

      resultPixmap = maskedPixmap;
    }

    // user-supplied placeholder image is also "placeholder art" —
    // overlay the title only when the real artwork is missing (storedPixmap
    // null), not on actual artwork hits.
    if (storedPixmap.isNull()) {
      drawTitleOnPlaceholder(resultPixmap, dpr);
    }

    // Set DPR on final result for proper display
    resultPixmap.setDevicePixelRatio(dpr);

    imageLabel->setPixmap(resultPixmap);
    imageLabel->setStyleSheet(QString());
  }
  if (nameLabel) {
    nameLabel->raise();
  }
}
