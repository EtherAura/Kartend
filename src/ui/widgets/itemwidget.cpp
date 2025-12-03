// Media item widget displaying artwork, title, and selection state with pulse animation.
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
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPolygon>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <algorithm>

ItemWidget::ItemWidget(QWidget *parent)
    : QWidget(parent)

      ,
      m_itemWidth(UIConstants::Item::DEFAULT_WIDTH),
      m_itemHeight(UIConstants::Item::DEFAULT_HEIGHT),
      triangleIndicator(nullptr), m_pulseDelayTimer(nullptr) {
  Ui::ItemWidget itemUi;
  itemUi.setupUi(this);
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
  connect(m_pulseDelayTimer, &QTimer::timeout, this,
          &ItemWidget::startPulseAnimation);
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

// Compute title tint from highlight color with configurable saturation/lightness
auto ItemWidget::titleTint() -> QColor {
  QColor highlight = QApplication::palette().color(QPalette::Highlight);
  int hue = 0;
  int saturation = 0;
  int lightness = 0;
  int alpha = 0;
  highlight.getHsl(&hue, &saturation, &lightness, &alpha);
  
  int targetLightness = qBound(0, UIConstants::Color::TITLE_TINT_LIGHTNESS, 
                               UIConstants::Color::CHANNEL_MAX);
  int targetSaturation = qBound(0, UIConstants::Color::TITLE_TINT_SATURATION,
                                UIConstants::Color::CHANNEL_MAX);
  
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

  if ((pulseAnimation) &&
      pulseAnimation->state() != QAbstractAnimation::Running) {
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
  update();  // Trigger repaint to draw tinted text
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
      // Only subcollection double-clicks reach here - EventManager passes them through
      if (m_isSubcollection) {
        emit subcollectionDoubleClicked(m_subcollectionIndex);
      } else if (m_isVirtualFolder) {
        emit virtualFolderDoubleClicked(m_virtualFolderPath);
      }
      // Media item double-clicks are handled by EventManager::widgetDoubleClicked
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
void ItemWidget::leaveEvent(QEvent *event) { QWidget::leaveEvent(event); }

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
  if ((parentPtr) &&
      parentPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    return true;
  }
  const QWidget *grandparentPtr =
      (parentPtr) ? parentPtr->parentWidget() : nullptr;
  return (grandparentPtr) &&
         grandparentPtr->property(PropertyKeys::GlideAnimating).toBool();
}

// Computes the selection border rectangle around the artwork and title area
auto ItemWidget::computeSelectionBorderRect() const -> QRect {
  if (!imageLabel) {
    return {};
  }
  const QRect imageRect = imageLabel->geometry();

  const int left = imageRect.left() - UIConstants::CollectionIcon::ITEM_SPACING;
  const int top = imageRect.top() - UIConstants::CollectionIcon::ITEM_SPACING;
  const int right = imageRect.right() + UIConstants::CollectionIcon::ITEM_SPACING;

  int bottom;
  // Show title if: regular item with titles visible, OR subcollection with subcollection titles visible
  bool shouldShowTitle = (!m_isSubcollection && !m_hideTitles) || (m_isSubcollection && !m_hideSubcollectionTitles);

  if (!shouldShowTitle) {
    // If titles are hidden, only surround the artwork
    bottom = imageRect.bottom() + UIConstants::CollectionIcon::ITEM_SPACING;
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
    int spacing = (effectiveTextHeight > 0) ? UIConstants::Widget::SPACING : 0;
    const int contentBottom =
        imageRect.bottom() + spacing + effectiveTextHeight;
    
    // Use tighter bottom padding for text
    int bottomPadding = (effectiveTextHeight > 0) ? UIConstants::Metadata::VALUE_PADDING : UIConstants::CollectionIcon::ITEM_SPACING;
    bottom = contentBottom + bottomPadding;
  }

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
  if ((pulseAnimation) &&
      pulseAnimation->state() != QAbstractAnimation::Running) {
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
    update(borderRect.adjusted(-UIConstants::Widget::BORDER_WIDTH_SELECTION,
                               -UIConstants::Widget::BORDER_WIDTH_SELECTION,
                               UIConstants::Widget::BORDER_WIDTH_SELECTION,
                               UIConstants::Widget::BORDER_WIDTH_SELECTION));
  } else {
    update();
  }
}

// Renders the selection border with pulsing opacity when selected; suppressed
// during glide animations
void ItemWidget::paintEvent(QPaintEvent *event) {
  if ((!event) || !isVisible()) {
    return;
  }

  QWidget::paintEvent(event);

  bool glideActive = false;
  QWidget *parentWidgetPtr = parentWidget();
  if ((parentWidgetPtr) &&
      parentWidgetPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    glideActive = true;
  }
  QWidget *grandparentWidgetPtr =
      (parentWidgetPtr) ? parentWidgetPtr->parentWidget() : nullptr;
  if ((grandparentWidgetPtr) &&
      grandparentWidgetPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    glideActive = true;
  }

  QPainter painter(this);
  if (!painter.isActive()) {
    return;
  }

  // Paint selection border when selected
  if (isSelectedState && (imageLabel) && !glideActive) {
    painter.setRenderHint(QPainter::Antialiasing);

    double alpha = qBound(UIConstants::Animation::PULSE_OPACITY_LOW,
                          static_cast<double>(m_pulseOpacity),
                          UIConstants::Animation::PULSE_OPACITY_HIGH);

    QColor borderColor = palette().color(QPalette::Highlight);
    borderColor.setAlphaF(alpha);

    QPen pen(borderColor);
    pen.setWidth(UIConstants::Widget::BORDER_WIDTH_SELECTION);
    painter.setPen(pen);

    // Use the centralized computation for the border rectangle
    QRect borderRect = computeSelectionBorderRect();
    painter.drawRoundedRect(borderRect, UIConstants::Widget::BORDER_RADIUS,
                            UIConstants::Widget::BORDER_RADIUS);
  }

  // Paint title text with tint color - bypasses broken QLabel stylesheet in Qt 6.9
  if (nameLabel && !itemName.isEmpty() && nameLabel->isVisible()) {
    bool shouldShowTitle = false;
    if (m_isVirtualFolder) {
      shouldShowTitle = !m_hideSubfolderTitle;
    } else if (m_isSubcollection) {
      shouldShowTitle = !m_hideSubcollectionTitles;
    } else {
      shouldShowTitle = !m_hideTitles;
    }
    if (shouldShowTitle) {
      painter.setRenderHint(QPainter::TextAntialiasing);
      painter.setPen(m_titleTintColor);
      painter.setFont(nameLabel->font());
      
      // Draw text in the same rect as the nameLabel
      QRect textRect = nameLabel->geometry();
      painter.drawText(textRect, nameLabel->alignment() | Qt::TextWordWrap, itemName);
    }
  }
}

// Set item dimensions
void ItemWidget::setItemDimensions(int width, int height) {
  if (m_itemWidth == width && m_itemHeight == height) {
    return;
  }
  m_itemWidth = width;
  m_itemHeight = height;
  applyDimensions();
}

void ItemWidget::applyDimensions() {
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

  int availableHeight = m_itemHeight - UIConstants::Widget::PADDING -
                        UIConstants::Widget::SPACING - reservedTextHeight;
  int availableWidth = m_itemWidth - UIConstants::Widget::PADDING;
  int artworkSize = qMin(availableWidth, availableHeight);
  
  // Show title if: regular item with titles visible, OR subcollection with subcollection titles visible,
  // OR virtual folder with subfolder titles visible
  bool shouldShowTitle = false;
  if (m_isVirtualFolder) {
    shouldShowTitle = !m_hideSubfolderTitle;
  } else if (m_isSubcollection) {
    shouldShowTitle = !m_hideSubcollectionTitles;
  } else {
    shouldShowTitle = !m_hideTitles;
  }
  
  if (imageLabel) {
    imageLabel->setFixedSize(artworkSize, artworkSize);
  }
  if (nameLabel) {
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
  QPointer<ItemWidget> ptr = this;
  // Defer artwork update until after all recycle property changes settle -
  // ensures the widget displays correctly after being reused from the pool
  QTimer::singleShot(0, this, [ptr]() {
    if (ptr) {
      ptr->onArtworkChanged();
    }
  });
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
    imageLabel->setPixmap(buildPlaceholderPattern(width, height));
    return;
  }

  int width = imageLabel->width();
  int height = imageLabel->height();
  if (storedPixmap.isNull()) {
    imageLabel->setPixmap(buildPlaceholderPattern(width, height));
    imageLabel->setStyleSheet(QString());
  } else {
    // Get device pixel ratio for HiDPI rendering
    qreal dpr = 1.0;
    if (QGuiApplication::primaryScreen()) {
      dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    }
    const int actualWidth = qRound(width * dpr);
    const int actualHeight = qRound(height * dpr);

    // Create background at actual pixel size
    QPixmap backgroundPixmap(actualWidth, actualHeight);
    backgroundPixmap.setDevicePixelRatio(dpr);
    backgroundPixmap.fill(palette().color(QPalette::Mid));

    // Scale the stored pixmap to fit the label's actual pixel dimensions
    // Use the raw pixel dimensions for scaling since we're drawing to actual pixels
    QPixmap scaledArtwork = storedPixmap.scaled(
        actualWidth, actualHeight,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);

    // Center the scaled artwork on the background
    {
      QPainter painter(&backgroundPixmap);
      painter.setRenderHints(QPainter::Antialiasing |
                             QPainter::SmoothPixmapTransform);
      int offsetX = (actualWidth - scaledArtwork.width()) / 2;
      int offsetY = (actualHeight - scaledArtwork.height()) / 2;
      painter.drawPixmap(offsetX, offsetY, scaledArtwork);
    }

    // Apply corner radius masking at the end (consistent with placeholder approach)
    if (m_cornerRadius > 0) {
      QPixmap maskedPixmap(actualWidth, actualHeight);
      maskedPixmap.setDevicePixelRatio(dpr);
      maskedPixmap.fill(Qt::transparent);
      
      QPainter maskPainter(&maskedPixmap);
      maskPainter.setRenderHint(QPainter::Antialiasing, true);
      
      // Use logical coordinates (width x height) since devicePixelRatio is set
      QPainterPath clipPath;
      clipPath.addRoundedRect(QRectF(0, 0, width, height),
                              m_cornerRadius, m_cornerRadius);
      maskPainter.setClipPath(clipPath);
      maskPainter.drawPixmap(0, 0, backgroundPixmap);
      maskPainter.end();
      
      backgroundPixmap = maskedPixmap;
    }

    imageLabel->setPixmap(backgroundPixmap);
    imageLabel->setStyleSheet(QString());
  }
  if (nameLabel) {
    nameLabel->raise();
  }
}

// Set file path
void ItemWidget::setFilePath(const QString &path) { filePath = path; }

// Reset widget state for reuse from pool
void ItemWidget::resetForReuse() {
  // Clear selection state and stop any running pulse animation
  // This prevents stale selection rectangles when widgets are recycled
  if (isSelectedState) {
    isSelectedState = false;
    if (pulseAnimation &&
        pulseAnimation->state() == QAbstractAnimation::Running) {
      pulseAnimation->stop();
    }
    m_pulseOpacity = UIConstants::Animation::PULSE_OPACITY_LOW;
  }
  if (m_pulseDelayTimer && m_pulseDelayTimer->isActive()) {
    m_pulseDelayTimer->stop();
  }

  m_isSubcollection = false;
  m_subcollectionIndex = -1;
  m_isVirtualFolder = false;
  m_virtualFolderPath.clear();
  m_hideSubfolderTitle = false;
  filePath.clear();
  itemName.clear();
  storedPixmap = QPixmap();  // Clear stored artwork
  if (imageLabel) {
    // Set placeholder pattern instead of clearing - widget dimensions may not
    // be set yet, so use current label size or fallback to reasonable defaults
    int width = imageLabel->width() > 0 ? imageLabel->width() : 100;
    int height = imageLabel->height() > 0 ? imageLabel->height() : 100;
    imageLabel->setPixmap(buildPlaceholderPattern(width, height));
  }
  if (triangleIndicator) {
    triangleIndicator->hide();
  }
}

// Set as subcollection
void ItemWidget::setAsSubcollection(int index, const QString &name) {
  m_isSubcollection = true;
  m_subcollectionIndex = index;
  setItemName(QStringLiteral("📁 ") + name);
  applyDimensions();
}

// Set as virtual folder (subfolder navigation without subcollection)
void ItemWidget::setAsVirtualFolder(const QString &folderPath, const QString &displayName, bool hideTitle) {
  m_isVirtualFolder = true;
  m_virtualFolderPath = folderPath;
  m_hideSubfolderTitle = hideTitle;
  // Use folder icon (🗂️) to distinguish from subcollection icon (📂)
  setItemName(QStringLiteral("🗂️ ") + displayName);
  applyDimensions();
}

// Set item name
void ItemWidget::setItemName(const QString &name) {
  itemName = name;
  if (nameLabel) {
    // Show title if: regular item with titles visible, OR subcollection with subcollection titles visible,
    // OR virtual folder with subfolder titles visible
    bool shouldShowTitle = false;
    if (m_isVirtualFolder) {
      shouldShowTitle = !m_hideSubfolderTitle;
    } else if (m_isSubcollection) {
      shouldShowTitle = !m_hideSubcollectionTitles;
    } else {
      shouldShowTitle = !m_hideTitles;
    }
    
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
void ItemWidget::setArtworkPixmap(const QPixmap &pixmap) {
  if (!imageLabel) {
    return;
  }

  bool shouldDefer = property(PropertyKeys::DeferArtworkUpdate).toBool();
  if (shouldDefer) {
    storedPixmap = pixmap;
    return;
  }

  storedPixmap = pixmap;
  QPointer<ItemWidget> ptr = this;
  // Defer artwork display until next event loop iteration - allows
  // the pixmap to be stored before triggering the visual update
  QTimer::singleShot(0, this, [ptr]() {
    if (ptr) {
      ptr->onArtworkChanged();
    }
  });
  if (nameLabel) {
    nameLabel->raise();
  }
}

// Set pulse opacity
void ItemWidget::setPulseOpacity(qreal opacity) {
  m_pulseOpacity = opacity;
  scheduleSelectionBorderUpdate();
}

// Set font size
void ItemWidget::setFontSize(int fontSize) {
  if (m_fontSize == fontSize) {
    return;
  }
  m_fontSize = fontSize;
  applyDimensions();
}

// Set hide titles
void ItemWidget::setHideTitles(bool hide) {
  if (m_hideTitles == hide) {
    return;
  }
  m_hideTitles = hide;
  applyDimensions();
}

void ItemWidget::setHideSubcollectionTitles(bool hide) {
  if (m_hideSubcollectionTitles == hide) {
    return;
  }
  m_hideSubcollectionTitles = hide;
  applyDimensions();
}

// Set corner radius for artwork clipping
void ItemWidget::setCornerRadius(int radius) {
  if (m_cornerRadius == radius) {
    return;
  }
  m_cornerRadius = radius;
  onArtworkChanged();
}

// Update triangle indicator
void ItemWidget::updateTriangleIndicator() {
  if ((!triangleIndicator) || (!imageLabel) ||
      !m_isSubcollection) {
    return;
  }
  if (isSelectedState) {
    QRect imageRect = imageLabel->geometry();
    int borderSpacing = UIConstants::CollectionIcon::ITEM_SPACING;
    int indicatorX =
        imageRect.right() + borderSpacing -
        (UIConstants::Widget::TRIANGLE_SIZE + UIConstants::Metadata::VALUE_PADDING);
    int indicatorY =
        imageRect.top() - borderSpacing + UIConstants::Metadata::VALUE_PADDING;
    triangleIndicator->setGeometry(indicatorX, indicatorY,
                                   UIConstants::Widget::TRIANGLE_SIZE,
                                   UIConstants::Widget::TRIANGLE_SIZE);
    triangleIndicator->show();
    triangleIndicator->raise();
    paintTriangleIndicator();
  } else {
    triangleIndicator->hide();
  }
}

// Paint triangle indicator
void ItemWidget::paintTriangleIndicator() {
  if ((!triangleIndicator) || !triangleIndicator->isVisible()) {
    return;
  }
  QPixmap pixmap(UIConstants::Widget::TRIANGLE_SIZE, UIConstants::Widget::TRIANGLE_SIZE);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  QColor highlight = palette().color(QPalette::Highlight);
  painter.setBrush(highlight);
  painter.setPen(
      QPen(highlight.darker(UIConstants::Widget::HIGHLIGHT_DARKEN_FACTOR), 1));
  QPolygon triangle;
  triangle << QPoint(UIConstants::Metadata::VALUE_PADDING,
                     UIConstants::Metadata::VALUE_PADDING)
           << QPoint(UIConstants::Widget::TRIANGLE_SIZE -
                         UIConstants::Metadata::VALUE_PADDING,
                     UIConstants::Metadata::VALUE_PADDING)
           << QPoint(UIConstants::Widget::TRIANGLE_SIZE -
                         UIConstants::Metadata::VALUE_PADDING,
                     UIConstants::Widget::TRIANGLE_SIZE -
                         UIConstants::Metadata::VALUE_PADDING);
  painter.drawPolygon(triangle);
  auto *indicatorLabel = qobject_cast<QLabel *>(triangleIndicator);
  if (!indicatorLabel) {
    indicatorLabel = new QLabel(triangleIndicator);
    indicatorLabel->setGeometry(0, 0, UIConstants::Widget::TRIANGLE_SIZE,
                                UIConstants::Widget::TRIANGLE_SIZE);
    indicatorLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    indicatorLabel->show();
  }
  indicatorLabel->setPixmap(pixmap);
}

// Build placeholder pattern
auto ItemWidget::buildPlaceholderPattern(int width, int height) const
    -> QPixmap {
  if (width <= 0 || height <= 0) {
    return {};
  }
  static QPixmap cache;
  static quint64 cacheKey = 0;
  static int cachedCornerRadius = 0;
  QColor base = palette().color(QPalette::Mid);
  constexpr int kKeyWidthShiftBits = 48;
  constexpr int kKeyHeightShiftBits = 32;
  constexpr quint64 kRgbaMask32 = 0xffffffffULL;
  quint64 key = (static_cast<quint64>(width) << kKeyWidthShiftBits) |
                (static_cast<quint64>(height) << kKeyHeightShiftBits) |
                (static_cast<quint64>(base.rgba()) & kRgbaMask32);
  if (!cache.isNull() && cacheKey == key && cachedCornerRadius == m_cornerRadius) {
    return cache;
  }

  QPixmap pixmap(width, height);
  pixmap.fill(Qt::transparent);

  int baseLightness = base.lightness();
  constexpr int kLightnessDarkThreshold = 128;
  bool dark = (baseLightness < kLightnessDarkThreshold);

  int primaryDelta = dark ? UIConstants::Placeholder::PRIMARY_DELTA_DARK
                          : UIConstants::Placeholder::PRIMARY_DELTA_LIGHT;
  int secondaryDelta = dark ? UIConstants::Placeholder::SECONDARY_DELTA_DARK
                            : UIConstants::Placeholder::SECONDARY_DELTA_LIGHT;

  int hHue = 0;
  int hSat = 0;
  int hLight = 0;
  int hAlpha = 0;
  QColor primary = base;
  primary.getHsl(&hHue, &hSat, &hLight, &hAlpha);
  primary.setHsl(
      hHue, hSat / 2,
      qBound(0, hLight + primaryDelta, UIConstants::Color::CHANNEL_MAX),
      UIConstants::Color::CHANNEL_MAX);
  QColor titleTintColor = titleTint();
  primary.setRed(
      (primary.red() * UIConstants::Placeholder::PRIMARY_TINT_NUM +
       titleTintColor.red() * (UIConstants::Placeholder::PRIMARY_TINT_DEN -
                               UIConstants::Placeholder::PRIMARY_TINT_NUM)) /
      UIConstants::Placeholder::PRIMARY_TINT_DEN);
  primary.setGreen(
      (primary.green() * UIConstants::Placeholder::PRIMARY_TINT_NUM +
       titleTintColor.green() * (UIConstants::Placeholder::PRIMARY_TINT_DEN -
                                 UIConstants::Placeholder::PRIMARY_TINT_NUM)) /
      UIConstants::Placeholder::PRIMARY_TINT_DEN);
  primary.setBlue(
      (primary.blue() * UIConstants::Placeholder::PRIMARY_TINT_NUM +
       titleTintColor.blue() * (UIConstants::Placeholder::PRIMARY_TINT_DEN -
                                UIConstants::Placeholder::PRIMARY_TINT_NUM)) /
      UIConstants::Placeholder::PRIMARY_TINT_DEN);
  primary.setAlpha(UIConstants::Placeholder::PRIMARY_ALPHA);

  QColor secondary = base;
  secondary.setHsl(
      hHue, hSat / 3,
      qBound(0, hLight + secondaryDelta, UIConstants::Color::CHANNEL_MAX),
      UIConstants::Color::CHANNEL_MAX);
  secondary.setRed(
      (secondary.red() * UIConstants::Placeholder::SECONDARY_TINT_NUM +
       titleTintColor.red() * (UIConstants::Placeholder::SECONDARY_TINT_DEN -
                               UIConstants::Placeholder::SECONDARY_TINT_NUM)) /
      UIConstants::Placeholder::SECONDARY_TINT_DEN);
  secondary.setGreen(
      (secondary.green() * UIConstants::Placeholder::SECONDARY_TINT_NUM +
       titleTintColor.green() * (UIConstants::Placeholder::SECONDARY_TINT_DEN -
                                 UIConstants::Placeholder::SECONDARY_TINT_NUM)) /
      UIConstants::Placeholder::SECONDARY_TINT_DEN);
  secondary.setBlue(
      (secondary.blue() * UIConstants::Placeholder::SECONDARY_TINT_NUM +
       titleTintColor.blue() * (UIConstants::Placeholder::SECONDARY_TINT_DEN -
                                UIConstants::Placeholder::SECONDARY_TINT_NUM)) /
      UIConstants::Placeholder::SECONDARY_TINT_DEN);
  secondary.setAlpha(UIConstants::Placeholder::SECONDARY_ALPHA);

  int step = qBound(UIConstants::Placeholder::STEP_MIN,
                    qMin(width, height) / UIConstants::Placeholder::STEP_DIVISOR,
                    UIConstants::Placeholder::STEP_MAX);

  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    
    // Fill background
    painter.fillRect(0, 0, width, height, base);
    
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
      static_cast<quint32>(key ^ UIConstants::Placeholder::NOISE_SEED));
  const int noiseAmp = UIConstants::Placeholder::NOISE_AMPLITUDE;
  const int stride = UIConstants::Placeholder::NOISE_STRIDE;
  if (noiseAmp > 0 && stride > 0) {
    for (int yPos = 0; yPos < height; yPos += stride) {
      QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(yPos));
      for (int xPos = 0; xPos < width; xPos += stride) {
        QRgb pixel = row[xPos];
        int red = qRed(pixel);
        int green = qGreen(pixel);
        int blue = qBlue(pixel);
        int noiseDelta = static_cast<int>(generator.generate() &
                                          UIConstants::Placeholder::NOISE_MASK) -
                         UIConstants::Placeholder::NOISE_BIAS;
        noiseDelta = std::min(noiseDelta, noiseAmp);
        noiseDelta = std::max(noiseDelta, -noiseAmp);
        red = qBound(0, red + noiseDelta, UIConstants::Color::CHANNEL_MAX);
        green = qBound(0, green + noiseDelta, UIConstants::Color::CHANNEL_MAX);
        blue = qBound(0, blue + noiseDelta, UIConstants::Color::CHANNEL_MAX);
        row[xPos] = qRgb(red, green, blue);
      }
    }
  }
  pixmap = QPixmap::fromImage(img);

  {
    QPainter painter(&pixmap);
    QLinearGradient gradient(0, 0, 0, height);
    QColor top(base.red(), base.green(), base.blue(),
               UIConstants::Placeholder::GRADIENT_TOP_ALPHA);
    QColor bottom(base.red(), base.green(), base.blue(),
                  UIConstants::Placeholder::GRADIENT_BOTTOM_ALPHA);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    painter.fillRect(0, 0, width, height, gradient);
  }

  // Apply corner radius masking at the end (after all drawing/processing)
  if (m_cornerRadius > 0) {
    QPixmap maskedPixmap(width, height);
    maskedPixmap.fill(Qt::transparent);
    
    QPainter maskPainter(&maskedPixmap);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(0, 0, width, height),
                            m_cornerRadius, m_cornerRadius);
    maskPainter.setClipPath(clipPath);
    maskPainter.drawPixmap(0, 0, pixmap);
    maskPainter.end();
    
    pixmap = maskedPixmap;
  }

  cache = pixmap;
  cacheKey = key;
  cachedCornerRadius = m_cornerRadius;
  return pixmap;
}
