#include "itemwidget.h"
#include "propertyutils.h"
#include "uiconstants.h"
#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPolygon>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>

MediaItemWidget::MediaItemWidget(QWidget *parent)
    : QWidget(parent)

      ,
      m_itemWidth(UIConstants::DEFAULT_ITEM_WIDTH),
      m_itemHeight(UIConstants::DEFAULT_ITEM_HEIGHT),
      triangleIndicator(nullptr), m_pulseDelayTimer(nullptr) {
  Ui::ItemWidget itemUi;
  itemUi.setupUi(this);
  imageLabel = itemUi.imageLabel;
  nameLabel = itemUi.nameLabel;
  triangleIndicator = itemUi.triangleIndicator;
  setFocusPolicy(Qt::NoFocus);
  // Allow children (nameLabel) to overflow widget bounds for long titles
  setAttribute(Qt::WA_TransparentForMouseEvents, false);
  if (imageLabel != nullptr) {
    imageLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }
  if (nameLabel != nullptr) {
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    nameLabel->setTextFormat(Qt::PlainText);
    nameLabel->setAutoFillBackground(false);
  }
  if (triangleIndicator != nullptr) {
    triangleIndicator->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }
  applyTitleTint();

  m_pulseDelayTimer = new QTimer(this);
  m_pulseDelayTimer->setSingleShot(true);
  m_pulseDelayTimer->setInterval(UIConstants::PULSE_INACTIVITY_DELAY_MS);
  connect(m_pulseDelayTimer, &QTimer::timeout, this,
          &MediaItemWidget::startPulseAnimation);
}

MediaItemWidget::~MediaItemWidget() {
  if (pulseAnimation != nullptr) {
    pulseAnimation->stop();
    delete pulseAnimation;
    pulseAnimation = nullptr;
  }
  if (m_pulseDelayTimer != nullptr) {
    m_pulseDelayTimer->stop();
    delete m_pulseDelayTimer;
    m_pulseDelayTimer = nullptr;
  }
  storedPixmap = QPixmap();
}

// Compute title tint
auto MediaItemWidget::titleTint() -> QColor {
  QColor highlight = QApplication::palette().color(QPalette::Highlight);
  int hue = 0;
  int saturation = 0;
  int lightness = 0;
  int alpha = 0;
  highlight.getHsl(&hue, &saturation, &lightness, &alpha);
  saturation = qBound(0, UIConstants::TITLE_TINT_SATURATION,
                      UIConstants::COLOR_CHANNEL_MAX);
  lightness = qBound(0, lightness + UIConstants::TITLE_TINT_LIGHTNESS_OFFSET,
                     UIConstants::COLOR_CHANNEL_MAX);
  QColor color;
  color.setHsl(hue, saturation, lightness, UIConstants::COLOR_CHANNEL_MAX);
  return color;
}

void MediaItemWidget::setupPulseAnimation() {
  if (pulseAnimation != nullptr) {
    pulseAnimation->stop();
    delete pulseAnimation;
  }
  pulseAnimation = new QPropertyAnimation(this, "pulseOpacity");
  pulseAnimation->setDuration(UIConstants::PULSE_ANIMATION_DURATION);
  pulseAnimation->setKeyValueAt(0.0, UIConstants::PULSE_OPACITY_LOW);
  pulseAnimation->setKeyValueAt(UIConstants::PULSE_KEYFRAME_MID_POS,
                                UIConstants::PULSE_OPACITY_HIGH);
  pulseAnimation->setKeyValueAt(1.0, UIConstants::PULSE_OPACITY_LOW);
  pulseAnimation->setLoopCount(-1);
  pulseAnimation->setEasingCurve(QEasingCurve::InOutSine);
}

void MediaItemWidget::startPulseAnimation() {
  if (!isSelectedState) {
    return;
  }

  if (pulseAnimation == nullptr) {
    setupPulseAnimation();
  }

  if ((pulseAnimation != nullptr) &&
      pulseAnimation->state() != QAbstractAnimation::Running) {
    pulseAnimation->start();
  }
}

// Apply title tint
void MediaItemWidget::applyTitleTint() const {
  if (nameLabel == nullptr) {
    return;
  }
  QColor color = titleTint();
  nameLabel->setStyleSheet(
      QString("QLabel { color:#%1%2%3; background:transparent; padding:0; "
              "margin:0; }")
          .arg(color.red(), 2, UIConstants::HEX_BASE, QChar('0'))
          .arg(color.green(), 2, UIConstants::HEX_BASE, QChar('0'))
          .arg(color.blue(), 2, UIConstants::HEX_BASE, QChar('0')));
  nameLabel->update();
}

// Handle mouse press
void MediaItemWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (m_isSubcollection) {
      setSelected(true);
      emit subcollectionClicked(m_subcollectionIndex);
    }
    emit clicked();

    m_lastClickPos = event->pos();
    m_lastClickTimer.start();
  }
  QWidget::mousePressEvent(event);
}

// Handle mouse double click
void MediaItemWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    bool validDoubleClick = false;

    if (m_lastClickTimer.isValid()) {
      qint64 elapsed = m_lastClickTimer.elapsed();
      QPoint currentPos = event->pos();
      int distance = (currentPos - m_lastClickPos).manhattanLength();

      if (elapsed <= DOUBLE_CLICK_TIMEOUT_MS &&
          distance <= CLICK_POSITION_TOLERANCE) {
        validDoubleClick = true;
      }
    }

    if (validDoubleClick) {
      if (m_isSubcollection) {
        emit subcollectionDoubleClicked(m_subcollectionIndex);
      } else {
        emit doubleClicked();
      }
    }

    m_lastClickTimer.invalidate();
  }
  QWidget::mouseDoubleClickEvent(event);
}

// Enter event
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void MediaItemWidget::enterEvent(QEnterEvent *event)
#else
void MediaItemWidget::enterEvent(QEvent *event)
#endif
{
  QWidget::enterEvent(event);
}

// Leave event
void MediaItemWidget::leaveEvent(QEvent *event) { QWidget::leaveEvent(event); }

// Show event
void MediaItemWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
}

// Handles palette change to trigger artwork change respecting
// DeferArtworkUpdate
auto MediaItemWidget::event(QEvent *event) -> bool {
  bool handled = QWidget::event(event);
  if (event->type() == QEvent::PaletteChange) {
    applyTitleTint();
    if ((imageLabel != nullptr) &&
        !property(PropertyKeys::DeferArtworkUpdate).toBool()) {
      QPointer<MediaItemWidget> ptr = this;
  QTimer::singleShot(0, this, [ptr]() {
    if (ptr) { ptr->onArtworkChanged();
}
  });
    }
  }
  return handled;
}

// Resize event
void MediaItemWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (m_isSubcollection) {
    updateTriangleIndicator();
  }
}

// Sets selected state and respects glide-animating property via PropertyKeys
void MediaItemWidget::setSelected(bool selected) {
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
auto MediaItemWidget::isGlideActive() const -> bool {
  const QWidget *parentPtr = parentWidget();
  if ((parentPtr != nullptr) &&
      parentPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    return true;
  }
  const QWidget *grandparentPtr =
      (parentPtr != nullptr) ? parentPtr->parentWidget() : nullptr;
  return (grandparentPtr != nullptr) &&
         grandparentPtr->property(PropertyKeys::GlideAnimating).toBool();
}

// Computes the selection border rectangle around the artwork and title area
auto MediaItemWidget::computeSelectionBorderRect() const -> QRect {
  if (imageLabel == nullptr) {
    return {};
  }
  const QRect imageRect = imageLabel->geometry();

  const int left = imageRect.left() - UIConstants::COLLECTION_ITEM_SPACING;
  const int top = imageRect.top() - UIConstants::COLLECTION_ITEM_SPACING;
  const int right = imageRect.right() + UIConstants::COLLECTION_ITEM_SPACING;

  int bottom;
  // Show title if: regular item with titles visible, OR subcollection with subcollection titles visible
  bool shouldShowTitle = (!m_isSubcollection && !m_hideTitles) || (m_isSubcollection && !m_hideSubcollectionTitles);

  if (!shouldShowTitle) {
    // If titles are hidden, only surround the artwork
    bottom = imageRect.bottom() + UIConstants::COLLECTION_ITEM_SPACING;
  } else {
    // Calculate reserved text height using the same logic as applyDimensions
    QFont referenceFont = this->font();
    referenceFont.setPointSize(std::max(8, m_fontSize));
    QFontMetrics referenceFm(referenceFont);
    int textLines = 2;
    int singleLineHeight = referenceFm.ascent() + referenceFm.descent();
    int reservedTextHeight = singleLineHeight * textLines;

    int effectiveTextHeight = reservedTextHeight;

    // Compute actual text height to fit selection rect around visible text
    if (!itemName.isEmpty()) {
      QFont actualFont = this->font();
      actualFont.setPointSize(m_fontSize);
      QFontMetrics actualFm(actualFont);
      int maxWidth = imageRect.width();
      int flags = Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap;
      QRect textRect =
          actualFm.boundingRect(QRect(0, 0, maxWidth, 0), flags, itemName);
      
      int textHeight = textRect.height();
      // For single lines, use tighter height (exclude leading)
      if (textHeight <= actualFm.lineSpacing()) {
          textHeight = actualFm.ascent() + actualFm.descent();
      }
      
      // Use actual text height - don't cap at reserved height so selection
      // encompasses titles that wrap to 3+ lines
      effectiveTextHeight = textHeight;
    } else {
      effectiveTextHeight = 0;
    }

    // Include the text area in the selection border
    int spacing = (effectiveTextHeight > 0) ? UIConstants::WIDGET_SPACING : 0;
    const int contentBottom =
        imageRect.bottom() + spacing + effectiveTextHeight;
    
    // Use tighter bottom padding for text
    int bottomPadding = (effectiveTextHeight > 0) ? UIConstants::METADATA_VALUE_PADDING : UIConstants::COLLECTION_ITEM_SPACING;
    bottom = contentBottom + bottomPadding;
  }

  return {left, top, right - left, bottom - top};
}

auto MediaItemWidget::selectionBorderRectInParent() const -> QRect {
  QRect localRect = computeSelectionBorderRect();
  if (!localRect.isValid()) {
    return {};
  }
  const QPoint topLeft = mapToParent(localRect.topLeft());
  const QPoint bottomRight = mapToParent(localRect.bottomRight());
  return QRect(topLeft, bottomRight);
}

// Applies visual effects for selection state, preserving animation behavior
void MediaItemWidget::applySelectedUiEffects() {
  raise();
  if (m_isSubcollection && (triangleIndicator != nullptr)) {
    updateTriangleIndicator();
  }
  if (pulseAnimation == nullptr) {
    setupPulseAnimation();
  }
  if ((pulseAnimation != nullptr) &&
      pulseAnimation->state() != QAbstractAnimation::Running) {
    pulseAnimation->start();
  }
}

// Applies visual changes when deselected
void MediaItemWidget::applyDeselectedUiEffects() const {
  if (triangleIndicator != nullptr) {
    triangleIndicator->hide();
  }
}

// Schedules repaint of selection border region
void MediaItemWidget::scheduleSelectionBorderUpdate() {
  if (imageLabel != nullptr) {
    const QRect borderRect = computeSelectionBorderRect();
    update(borderRect.adjusted(-UIConstants::BORDER_WIDTH_SELECTION,
                               -UIConstants::BORDER_WIDTH_SELECTION,
                               UIConstants::BORDER_WIDTH_SELECTION,
                               UIConstants::BORDER_WIDTH_SELECTION));
  } else {
    update();
  }
}

// Renders the selection border with pulsing opacity when selected; suppressed
// during glide animations
void MediaItemWidget::paintEvent(QPaintEvent *event) {
  if ((event == nullptr) || !isVisible()) {
    return;
  }

  QWidget::paintEvent(event);

  bool glideActive = false;
  QWidget *parentWidgetPtr = parentWidget();
  if ((parentWidgetPtr != nullptr) &&
      parentWidgetPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    glideActive = true;
  }
  QWidget *grandparentWidgetPtr =
      (parentWidgetPtr != nullptr) ? parentWidgetPtr->parentWidget() : nullptr;
  if ((grandparentWidgetPtr != nullptr) &&
      grandparentWidgetPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    glideActive = true;
  }

  if (isSelectedState && (imageLabel != nullptr) && !glideActive) {
    QPainter painter(this);
    if (!painter.isActive()) {
      return;
    }

    painter.setRenderHint(QPainter::Antialiasing);

    double alpha = qBound(UIConstants::PULSE_OPACITY_LOW,
                          static_cast<double>(m_pulseOpacity),
                          UIConstants::PULSE_OPACITY_HIGH);

    QColor borderColor = palette().color(QPalette::Highlight);
    borderColor.setAlphaF(alpha);

    QPen pen(borderColor);
    pen.setWidth(UIConstants::BORDER_WIDTH_SELECTION);
    painter.setPen(pen);

    // Use the centralized computation for the border rectangle
    QRect borderRect = computeSelectionBorderRect();
    painter.drawRoundedRect(borderRect, UIConstants::BORDER_RADIUS,
                            UIConstants::BORDER_RADIUS);
  }
}

// Set item dimensions
void MediaItemWidget::setItemDimensions(int width, int height) {
  if (m_itemWidth == width && m_itemHeight == height) {
    return;
  }
  m_itemWidth = width;
  m_itemHeight = height;
  applyDimensions();
}

void MediaItemWidget::applyDimensions() {
  QString currentName = itemName;
  QString currentPath = filePath;
  QPixmap currentPixmap = storedPixmap;
  setFixedSize(m_itemWidth, m_itemHeight);

  // Use the current font size for layout calculations to ensure titles don't overlap artwork
  // But ensure we don't reserve less space than the default 12pt to maintain consistent artwork sizing/spacing
  QFont referenceFont = this->font();
  referenceFont.setPointSize(std::max(12, m_fontSize));
  QFontMetrics referenceFm(referenceFont);
  // Reserve space for 3 lines of text to accommodate longer titles
  int textLines = 3;
  int singleLineHeight = referenceFm.ascent() + referenceFm.descent();
  int reservedTextHeight = singleLineHeight * textLines;

  int availableHeight = m_itemHeight - UIConstants::WIDGET_PADDING -
                        UIConstants::WIDGET_SPACING - reservedTextHeight;
  int availableWidth = m_itemWidth - UIConstants::WIDGET_PADDING;
  int artworkSize = qMin(availableWidth, availableHeight);
  
  // Show title if: regular item with titles visible, OR subcollection with subcollection titles visible
  bool shouldShowTitle = (!m_isSubcollection && !m_hideTitles) || (m_isSubcollection && !m_hideSubcollectionTitles);
  
  if (imageLabel != nullptr) {
    imageLabel->setFixedSize(artworkSize, artworkSize);
  }
  if (nameLabel != nullptr) {
    nameLabel->setVisible(true);
    nameLabel->setMaximumWidth(artworkSize);
    nameLabel->setFixedHeight(reservedTextHeight);
    
    if (!shouldShowTitle) {
      nameLabel->setText("");
    } else {
      nameLabel->setText(itemName);
      QFont font = this->font();
      font.setPointSize(m_fontSize);
      nameLabel->setFont(font);
    }
  }
  if (!currentName.isEmpty()) {
    setItemName(currentName);
  }
  if (!currentPath.isEmpty()) {
    setFilePath(currentPath);
  }
  if (!currentPixmap.isNull()) {
    setArtworkPixmap(currentPixmap);
  }
  QPointer<MediaItemWidget> ptr = this;
  QTimer::singleShot(0, this, [ptr]() {
    if (ptr) {
      ptr->onArtworkChanged();
    }
  });
}

// Updates the artwork image label respecting DeferArtworkUpdate and
// ForcePlaceholder
void MediaItemWidget::onArtworkChanged() {
  if (imageLabel == nullptr) {
    return;
  }

  bool shouldDefer = property(PropertyKeys::DeferArtworkUpdate).toBool();
  bool forcePlaceholder = property(PropertyKeys::ForcePlaceholder).toBool();
  if (shouldDefer || forcePlaceholder) {
    int width = imageLabel->width();
    int height = imageLabel->height();
    imageLabel->setPixmap(buildPlaceholderPattern(width, height));
    return;
  }

  int width = imageLabel->width();
  int height = imageLabel->height();
  if (storedPixmap.isNull()) {
    imageLabel->setPixmap(buildPlaceholderPattern(width, height));
    imageLabel->setStyleSheet(QString());
  } else {
    QPixmap backgroundPixmap(width, height);
    backgroundPixmap.fill(palette().color(QPalette::Mid));
    QPainter painter(&backgroundPixmap);
    painter.setRenderHints(QPainter::Antialiasing |
                           QPainter::SmoothPixmapTransform);
    QPixmap scaled = storedPixmap.scaled(width, height, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    int offsetX = (width - scaled.width()) / 2;
    int offsetY = (height - scaled.height()) / 2;
    painter.drawPixmap(offsetX, offsetY, scaled);
    painter.end();
    imageLabel->setPixmap(backgroundPixmap);
    imageLabel->setStyleSheet(QString());
  }
  if (nameLabel != nullptr) {
    nameLabel->raise();
  }
}

// Set file path
void MediaItemWidget::setFilePath(const QString &path) { filePath = path; }

// Set as subcollection
void MediaItemWidget::setAsSubcollection(int index, const QString &name) {
  m_isSubcollection = true;
  m_subcollectionIndex = index;
  setItemName(QStringLiteral("📁 ") + name);
  applyDimensions();
}

// Set item name
void MediaItemWidget::setItemName(const QString &name) {
  itemName = name;
  if (nameLabel != nullptr) {
    // Show title if: regular item with titles visible, OR subcollection with subcollection titles visible
    bool shouldShowTitle = (!m_isSubcollection && !m_hideTitles) || (m_isSubcollection && !m_hideSubcollectionTitles);
    if (!shouldShowTitle) {
      // Keep the label visible but empty to reserve layout space
      nameLabel->setText("");
      nameLabel->setVisible(true);
    } else {
      nameLabel->setText(itemName);
      QFont font = this->font();
      font.setPointSize(m_fontSize);
      nameLabel->setFont(font);
      applyTitleTint();
      nameLabel->setVisible(true);
      nameLabel->raise();
    }
  }
}

// Sets the current pixmap while respecting DeferArtworkUpdate
void MediaItemWidget::setArtworkPixmap(const QPixmap &pixmap) {
  if (imageLabel == nullptr) {
    return;
  }

  bool shouldDefer = property(PropertyKeys::DeferArtworkUpdate).toBool();
  if (shouldDefer) {
    storedPixmap = pixmap;
    return;
  }

  storedPixmap = pixmap;
  QPointer<MediaItemWidget> ptr = this;
  QTimer::singleShot(0, this, [ptr]() {
    if (ptr) { ptr->onArtworkChanged();
}
  });
  if (nameLabel != nullptr) {
    nameLabel->raise();
  }
}

// Set pulse opacity
void MediaItemWidget::setPulseOpacity(qreal opacity) {
  m_pulseOpacity = opacity;
  scheduleSelectionBorderUpdate();
}

// Set font size
void MediaItemWidget::setFontSize(int fontSize) {
  if (m_fontSize == fontSize) {
    return;
  }
  m_fontSize = fontSize;
  applyDimensions();
}

// Set hide titles
void MediaItemWidget::setHideTitles(bool hide) {
  if (m_hideTitles == hide) {
    return;
  }
  m_hideTitles = hide;
  applyDimensions();
}

void MediaItemWidget::setHideSubcollectionTitles(bool hide) {
  if (m_hideSubcollectionTitles == hide) {
    return;
  }
  m_hideSubcollectionTitles = hide;
  applyDimensions();
}

// Update triangle indicator
void MediaItemWidget::updateTriangleIndicator() {
  if ((triangleIndicator == nullptr) || (imageLabel == nullptr) ||
      !m_isSubcollection) {
    return;
  }
  if (isSelectedState) {
    QRect imageRect = imageLabel->geometry();
    int borderSpacing = UIConstants::COLLECTION_ITEM_SPACING;
    int indicatorX =
        imageRect.right() + borderSpacing -
        (UIConstants::TRIANGLE_SIZE + UIConstants::METADATA_VALUE_PADDING);
    int indicatorY =
        imageRect.top() - borderSpacing + UIConstants::METADATA_VALUE_PADDING;
    triangleIndicator->setGeometry(indicatorX, indicatorY,
                                   UIConstants::TRIANGLE_SIZE,
                                   UIConstants::TRIANGLE_SIZE);
    triangleIndicator->show();
    triangleIndicator->raise();
    paintTriangleIndicator();
  } else {
    triangleIndicator->hide();
  }
}

// Paint triangle indicator
void MediaItemWidget::paintTriangleIndicator() {
  if ((triangleIndicator == nullptr) || !triangleIndicator->isVisible()) {
    return;
  }
  QPixmap pixmap(UIConstants::TRIANGLE_SIZE, UIConstants::TRIANGLE_SIZE);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  QColor highlight = palette().color(QPalette::Highlight);
  painter.setBrush(highlight);
  painter.setPen(
      QPen(highlight.darker(UIConstants::HIGHLIGHT_DARKEN_FACTOR), 1));
  QPolygon triangle;
  triangle << QPoint(UIConstants::METADATA_VALUE_PADDING,
                     UIConstants::METADATA_VALUE_PADDING)
           << QPoint(UIConstants::TRIANGLE_SIZE -
                         UIConstants::METADATA_VALUE_PADDING,
                     UIConstants::METADATA_VALUE_PADDING)
           << QPoint(UIConstants::TRIANGLE_SIZE -
                         UIConstants::METADATA_VALUE_PADDING,
                     UIConstants::TRIANGLE_SIZE -
                         UIConstants::METADATA_VALUE_PADDING);
  painter.drawPolygon(triangle);
  auto *indicatorLabel = qobject_cast<QLabel *>(triangleIndicator);
  if (indicatorLabel == nullptr) {
    indicatorLabel = new QLabel(triangleIndicator);
    indicatorLabel->setGeometry(0, 0, UIConstants::TRIANGLE_SIZE,
                                UIConstants::TRIANGLE_SIZE);
    indicatorLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    indicatorLabel->show();
  }
  indicatorLabel->setPixmap(pixmap);
}

// Build placeholder pattern
auto MediaItemWidget::buildPlaceholderPattern(int width, int height) const
    -> QPixmap {
  if (width <= 0 || height <= 0) {
    return {};
  }
  static QPixmap cache;
  static quint64 cacheKey = 0;
  QColor base = palette().color(QPalette::Mid);
  constexpr int kKeyWidthShiftBits = 48;
  constexpr int kKeyHeightShiftBits = 32;
  constexpr quint64 kRgbaMask32 = 0xffffffffULL;
  quint64 key = (static_cast<quint64>(width) << kKeyWidthShiftBits) |
                (static_cast<quint64>(height) << kKeyHeightShiftBits) |
                (static_cast<quint64>(base.rgba()) & kRgbaMask32);
  if (!cache.isNull() && cacheKey == key) {
    return cache;
  }

  QPixmap pixmap(width, height);
  pixmap.fill(base);

  int baseLightness = base.lightness();
  constexpr int kLightnessDarkThreshold = 128;
  bool dark = (baseLightness < kLightnessDarkThreshold);

  int primaryDelta = dark ? UIConstants::PLACEHOLDER_PRIMARY_DELTA_DARK
                          : UIConstants::PLACEHOLDER_PRIMARY_DELTA_LIGHT;
  int secondaryDelta = dark ? UIConstants::PLACEHOLDER_SECONDARY_DELTA_DARK
                            : UIConstants::PLACEHOLDER_SECONDARY_DELTA_LIGHT;

  int hHue = 0;
  int hSat = 0;
  int hLight = 0;
  int hAlpha = 0;
  QColor primary = base;
  primary.getHsl(&hHue, &hSat, &hLight, &hAlpha);
  primary.setHsl(
      hHue, hSat / 2,
      qBound(0, hLight + primaryDelta, UIConstants::COLOR_CHANNEL_MAX),
      UIConstants::COLOR_CHANNEL_MAX);
  QColor titleTintColor = titleTint();
  primary.setRed(
      (primary.red() * UIConstants::PLACEHOLDER_PRIMARY_TINT_NUM +
       titleTintColor.red() * (UIConstants::PLACEHOLDER_PRIMARY_TINT_DEN -
                               UIConstants::PLACEHOLDER_PRIMARY_TINT_NUM)) /
      UIConstants::PLACEHOLDER_PRIMARY_TINT_DEN);
  primary.setGreen(
      (primary.green() * UIConstants::PLACEHOLDER_PRIMARY_TINT_NUM +
       titleTintColor.green() * (UIConstants::PLACEHOLDER_PRIMARY_TINT_DEN -
                                 UIConstants::PLACEHOLDER_PRIMARY_TINT_NUM)) /
      UIConstants::PLACEHOLDER_PRIMARY_TINT_DEN);
  primary.setBlue(
      (primary.blue() * UIConstants::PLACEHOLDER_PRIMARY_TINT_NUM +
       titleTintColor.blue() * (UIConstants::PLACEHOLDER_PRIMARY_TINT_DEN -
                                UIConstants::PLACEHOLDER_PRIMARY_TINT_NUM)) /
      UIConstants::PLACEHOLDER_PRIMARY_TINT_DEN);
  primary.setAlpha(UIConstants::PLACEHOLDER_PRIMARY_ALPHA);

  QColor secondary = base;
  secondary.setHsl(
      hHue, hSat / 3,
      qBound(0, hLight + secondaryDelta, UIConstants::COLOR_CHANNEL_MAX),
      UIConstants::COLOR_CHANNEL_MAX);
  secondary.setRed(
      (secondary.red() * UIConstants::PLACEHOLDER_SECONDARY_TINT_NUM +
       titleTintColor.red() * (UIConstants::PLACEHOLDER_SECONDARY_TINT_DEN -
                               UIConstants::PLACEHOLDER_SECONDARY_TINT_NUM)) /
      UIConstants::PLACEHOLDER_SECONDARY_TINT_DEN);
  secondary.setGreen(
      (secondary.green() * UIConstants::PLACEHOLDER_SECONDARY_TINT_NUM +
       titleTintColor.green() * (UIConstants::PLACEHOLDER_SECONDARY_TINT_DEN -
                                 UIConstants::PLACEHOLDER_SECONDARY_TINT_NUM)) /
      UIConstants::PLACEHOLDER_SECONDARY_TINT_DEN);
  secondary.setBlue(
      (secondary.blue() * UIConstants::PLACEHOLDER_SECONDARY_TINT_NUM +
       titleTintColor.blue() * (UIConstants::PLACEHOLDER_SECONDARY_TINT_DEN -
                                UIConstants::PLACEHOLDER_SECONDARY_TINT_NUM)) /
      UIConstants::PLACEHOLDER_SECONDARY_TINT_DEN);
  secondary.setAlpha(UIConstants::PLACEHOLDER_SECONDARY_ALPHA);

  int step = qBound(UIConstants::PLACEHOLDER_STEP_MIN,
                    qMin(width, height) / UIConstants::PLACEHOLDER_STEP_DIVISOR,
                    UIConstants::PLACEHOLDER_STEP_MAX);

  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(primary, 1));
    for (int diag = -height; diag < width; diag += step) {
      painter.drawLine(diag, 0, diag + height, height);
    }
    painter.setPen(QPen(secondary, 1));
    for (int diag = -height; diag < width; diag += step * 2) {
      painter.drawLine(diag, height, diag + height, 0);
    }
  }

  QImage img = pixmap.toImage();
  QRandomGenerator generator(
      static_cast<quint32>(key ^ UIConstants::PLACEHOLDER_NOISE_SEED));
  const int noiseAmp = UIConstants::PLACEHOLDER_NOISE_AMPLITUDE;
  const int stride = UIConstants::PLACEHOLDER_NOISE_STRIDE;
  if (noiseAmp > 0 && stride > 0) {
    for (int yPos = 0; yPos < height; yPos += stride) {
      QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(yPos));
      for (int xPos = 0; xPos < width; xPos += stride) {
        QRgb pixel = row[xPos];
        int red = qRed(pixel);
        int green = qGreen(pixel);
        int blue = qBlue(pixel);
        int noiseDelta = static_cast<int>(generator.generate() &
                                          UIConstants::PLACEHOLDER_NOISE_MASK) -
                         UIConstants::PLACEHOLDER_NOISE_BIAS;
        noiseDelta = std::min(noiseDelta, noiseAmp);
        noiseDelta = std::max(noiseDelta, -noiseAmp);
        red = qBound(0, red + noiseDelta, UIConstants::COLOR_CHANNEL_MAX);
        green = qBound(0, green + noiseDelta, UIConstants::COLOR_CHANNEL_MAX);
        blue = qBound(0, blue + noiseDelta, UIConstants::COLOR_CHANNEL_MAX);
        row[xPos] = qRgb(red, green, blue);
      }
    }
  }
  pixmap = QPixmap::fromImage(img);

  {
    QPainter painter(&pixmap);
    QLinearGradient gradient(0, 0, 0, height);
    QColor top(base.red(), base.green(), base.blue(),
               UIConstants::PLACEHOLDER_GRADIENT_TOP_ALPHA);
    QColor bottom(base.red(), base.green(), base.blue(),
                  UIConstants::PLACEHOLDER_GRADIENT_BOTTOM_ALPHA);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    painter.fillRect(0, 0, width, height, gradient);
  }

  cache = pixmap;
  cacheKey = key;
  return pixmap;
}
