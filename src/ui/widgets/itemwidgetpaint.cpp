// Painting and dimension layout methods extracted from itemwidget.cpp:
//   - paintEvent
//   - setItemDimensions
//   - applyDimensions
// These remain ItemWidget members and access existing class state and statics.
#include "itemwidget.h"
#include "propertyutils.h"
#include "uiconstants.h"
#include <algorithm>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QLoggingCategory>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcItemWidget)

// Renders the selection border with pulsing opacity when selected; suppressed
// during glide animations
void ItemWidget::paintEvent(QPaintEvent *event) {
  if ((!event) || !isVisible()) {
    return;
  }

  QWidget::paintEvent(event);

  bool glideActive = false;
  QWidget *parentWidgetPtr = parentWidget();
  if ((parentWidgetPtr) && parentWidgetPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    glideActive = true;
  }
  QWidget *grandparentWidgetPtr = (parentWidgetPtr) ? parentWidgetPtr->parentWidget() : nullptr;
  if ((grandparentWidgetPtr) &&
      grandparentWidgetPtr->property(PropertyKeys::GlideAnimating).toBool()) {
    glideActive = true;
  }

  QPainter painter(this);
  if (!painter.isActive()) {
    return;
  }

  // Paint alternating row background in list mode
  if (m_isListMode && m_rowIndex >= 0) {
    QColor rowColor;
    if (m_rowIndex % 2 == 1) {
      // Odd rows - use custom alt color if set, otherwise system AlternateBase
      if (!s_listAltRowColor.isEmpty() && QColor::isValidColorName(s_listAltRowColor)) {
        rowColor = QColor(s_listAltRowColor);
      } else {
        rowColor = palette().color(QPalette::AlternateBase);
      }
    } else {
      // Even rows - use custom color if set, otherwise system Base
      if (!s_listRowColor.isEmpty() && QColor::isValidColorName(s_listRowColor)) {
        rowColor = QColor(s_listRowColor);
      } else {
        rowColor = palette().color(QPalette::Base);
      }
    }
    painter.fillRect(rect(), rowColor);
  }

  // Paint selection border when selected (grid mode needs imageLabel, list mode
  // uses full widget)
  bool canPaintSelection = m_isListMode || imageLabel;
  if (isSelectedState && canPaintSelection && !glideActive) {
    painter.setRenderHint(QPainter::Antialiasing);

    double alpha =
        qBound(UIConstants::Animation::PULSE_OPACITY_LOW, static_cast<double>(m_pulseOpacity),
               UIConstants::Animation::PULSE_OPACITY_HIGH);

    // Use per-collection selection color if set, otherwise system highlight
    QColor borderColor;
    if (!s_selectionColor.isEmpty() && QColor::isValidColorName(s_selectionColor)) {
      borderColor = QColor(s_selectionColor);
    } else {
      borderColor = palette().color(QPalette::Highlight);
    }
    borderColor.setAlphaF(alpha);

    QPen pen(borderColor);
    pen.setWidth(UIConstants::Widget::BORDER_WIDTH_SELECTION);
    painter.setPen(pen);

    // Use the centralized computation for the border rectangle
    QRect borderRect = computeSelectionBorderRect();
    painter.drawRoundedRect(borderRect, UIConstants::Widget::BORDER_RADIUS,
                            UIConstants::Widget::BORDER_RADIUS);
  }

  // Paint title text with tint color - bypasses broken QLabel stylesheet in
  // Qt 6.9
  if (nameLabel && !itemName.isEmpty() && nameLabel->isVisible()) {
    bool shouldShowTitle = false;
    if (m_isVirtualFolder) {
      shouldShowTitle = !m_hideSubfolderTitle;
    } else if (m_isSubcollection) {
      shouldShowTitle = !m_hideSubcollectionTitles;
    } else {
      // Kartend-029m: hideTitles is a grid-mode concern. In list mode the row
      // IS the title -- suppressing it leaves a blank row with no fallback.
      shouldShowTitle = m_isListMode || !m_hideTitles;
    }
    if (shouldShowTitle) {
      painter.setRenderHint(QPainter::TextAntialiasing);
      painter.setPen(m_titleTintColor);

      // Apply custom font if configured
      QFont titleFont = nameLabel->font();
      if (!s_customFontFamily.isEmpty()) {
        titleFont.setFamily(s_customFontFamily);
      }
      painter.setFont(titleFont);

      // Draw text in the same rect as the nameLabel
      QRect textRect = nameLabel->geometry();
      // Offset for folder icon in subcollections/virtual folders
      // The nameLabel is layout-managed so move() doesn't work - offset the
      // paint rect instead
      bool hasFolderIcon = (m_isSubcollection || m_isVirtualFolder) && m_folderIconLabel;
      if (m_isListMode) {
        if (hasFolderIcon) {
          textRect.setLeft(textRect.left() + UIConstants::ListView::FOLDER_ICON_COLUMN_WIDTH);
        }
        // List mode: elide text only if it exceeds available width
        QFontMetrics fm(titleFont);
        int textWidth = fm.horizontalAdvance(itemName);
        if (textWidth > textRect.width()) {
          QString elidedText = fm.elidedText(itemName, Qt::ElideRight, textRect.width());
          painter.drawText(textRect, nameLabel->alignment(), elidedText);
        } else {
          painter.drawText(textRect, nameLabel->alignment(), itemName);
        }
      } else {
        // Grid mode: allow word wrap for multi-line titles
        // For subcollections/virtual folders, draw small icon to the left of
        // centered title
        int flags = nameLabel->alignment() | Qt::TextWordWrap;
        if (hasFolderIcon && m_folderIconLabel) {
          constexpr int gridFolderIconSize = 12; // Smaller icon for grid mode
          constexpr int iconSpacing = 3;
          QPixmap iconPix = m_folderIconLabel->pixmap();
          QFontMetrics fm(painter.font());
          int lineHeight = fm.height();
          int textWidth = fm.horizontalAdvance(itemName);

          // Calculate total width of icon+spacing+text to center as a unit
          int totalWidth = gridFolderIconSize + iconSpacing + textWidth;
          int availableWidth = textRect.width();

          // Center the combined icon+text block, clamping to available width
          int blockStartX =
              textRect.left() + (availableWidth - qMin(totalWidth, availableWidth)) / 2;

          // Draw folder icon vertically centered with the first line of text
          if (!iconPix.isNull()) {
            int iconY = textRect.top() + (lineHeight - gridFolderIconSize) / 2;
            painter.drawPixmap(blockStartX, iconY, gridFolderIconSize, gridFolderIconSize, iconPix);
          }

          // Draw text immediately after the icon (left-aligned from that point)
          QRect textDrawRect = textRect;
          textDrawRect.setLeft(blockStartX + gridFolderIconSize + iconSpacing);
          flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
          painter.drawText(textDrawRect, flags, itemName);
        } else {
          painter.drawText(textRect, flags, itemName);
        }
      }
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

  // List mode: horizontal layout with name, collection, and artwork icon
  if (m_isListMode) {
    m_artworkSize = 0; // No artwork display in list mode (icon only)

    // Remove layout margins in list mode for proper vertical centering
    if (layout()) {
      layout()->setContentsMargins(UIConstants::ListView::TEXT_LEFT_PADDING, 0,
                                   UIConstants::ListView::TEXT_RIGHT_PADDING, 0);
      layout()->setSpacing(0);
    }

    if (imageLabel) {
      imageLabel->setVisible(false);
      // Clear min/max constraints from .ui file (200x200) to allow zero size in
      // list mode
      imageLabel->setMinimumSize(0, 0);
      imageLabel->setMaximumSize(0, 0);
      imageLabel->setFixedSize(0, 0);
    }

    // Calculate column widths for list mode
    // Layout: [name.....................] [collection] [artwork_icon]
    constexpr int columnSpacing = 8;
    constexpr int folderIconWidth = UIConstants::ListView::FOLDER_ICON_COLUMN_WIDTH;

    int leftPadding = UIConstants::ListView::TEXT_LEFT_PADDING;
    int rightPadding = UIConstants::ListView::TEXT_RIGHT_PADDING;

    int xOffset = leftPadding;

    // Position folder icon for subcollections/virtual folders
    bool hasFolderIcon = (m_isSubcollection || m_isVirtualFolder) && m_folderIconLabel;
    if (hasFolderIcon) {
      m_folderIconLabel->setFixedSize(folderIconWidth, m_itemHeight);
      m_folderIconLabel->move(xOffset, 0);
      m_folderIconLabel->show();
      xOffset += folderIconWidth;
    } else if (m_folderIconLabel) {
      m_folderIconLabel->hide();
    }

    // Calculate name width (remaining space minus collection and artwork
    // columns)
    int nameWidth = m_itemWidth - xOffset - rightPadding - m_collectionColumnWidth -
                    m_artworkColumnWidth - (columnSpacing * 2);

    if (nameLabel) {
      nameLabel->setVisible(true);
      nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      nameLabel->setWordWrap(false); // No word wrap in list mode - use elided text
      // Clear min/max constraints from .ui file to allow proper sizing in list
      // mode
      nameLabel->setMinimumSize(0, 0);
      nameLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      nameLabel->setFixedSize(nameWidth, m_itemHeight); // Use fixed size for both dimensions
      nameLabel->move(xOffset, 0);
      nameLabel->setText(currentName);

      QFont font = this->font();
      font.setPointSize(m_fontSize);
      nameLabel->setFont(font);
    }
    xOffset += nameWidth + columnSpacing;

    // Position collection label after name
    if (m_collectionLabel) {
      m_collectionLabel->setGeometry(xOffset, 0, m_collectionColumnWidth, m_itemHeight);

      QFont font = this->font();
      font.setPointSize(m_fontSize);
      m_collectionLabel->setFont(font);

      if (!m_collectionName.isEmpty()) {
        m_collectionLabel->show();
        qCDebug(lcItemWidget) << "applyDimensions: collection label at x=" << xOffset
                              << "width=" << m_collectionColumnWidth << "text=" << m_collectionName;
      }
    }
    xOffset += m_collectionColumnWidth + columnSpacing;

    // Position artwork button on the right (only visible if has artwork)
    if (m_artworkButton) {
      m_artworkButton->setFixedSize(m_artworkColumnWidth, m_itemHeight - 4);
      m_artworkButton->move(xOffset, 2);
      if (m_hasArtwork) {
        m_artworkButton->show();
      } else {
        m_artworkButton->hide();
      }
    }

    if (!currentPath.isEmpty()) {
      setFilePath(currentPath);
    }
    return;
  }

  // Hide list mode elements in grid mode
  if (m_collectionLabel) {
    m_collectionLabel->hide();
  }
  if (m_artworkButton) {
    m_artworkButton->hide();
  }

  // Grid mode: restore standard margins
  if (layout()) {
    layout()->setContentsMargins(UIConstants::Widget::MARGIN, UIConstants::Widget::MARGIN,
                                 UIConstants::Widget::MARGIN, UIConstants::Widget::MARGIN);
    layout()->setSpacing(UIConstants::Widget::SPACING);
  }
  // Use the current font size for layout calculations to ensure titles don't
  // overlap artwork But ensure we don't reserve less space than the default
  // 12pt to maintain consistent artwork sizing/spacing
  QFont referenceFont = this->font();
  referenceFont.setPointSize(std::max(12, m_fontSize));
  QFontMetrics referenceFm(referenceFont);
  // Reserve space for 3 lines of text to accommodate longer titles
  int textLines = 3;
  int singleLineHeight = referenceFm.ascent() + referenceFm.descent();
  int reservedTextHeight = singleLineHeight * textLines;

  int availableHeight = m_itemHeight - UIConstants::Widget::PADDING - UIConstants::Widget::SPACING -
                        reservedTextHeight;
  int availableWidth = m_itemWidth - UIConstants::Widget::PADDING;
  int artworkSize = qMin(availableWidth, availableHeight);
  m_artworkSize = artworkSize;

  // Show title if: regular item with titles visible, OR subcollection with
  // subcollection titles visible, OR virtual folder with subfolder titles
  // visible
  bool shouldShowTitle = false;
  if (m_isVirtualFolder) {
    shouldShowTitle = !m_hideSubfolderTitle;
  } else if (m_isSubcollection) {
    shouldShowTitle = !m_hideSubcollectionTitles;
  } else {
    shouldShowTitle = !m_hideTitles;
  }

  if (imageLabel) {
    imageLabel->setVisible(true); // Ensure visible in grid mode
    // Reset min/max constraints that may have been zeroed in list mode
    imageLabel->setMinimumSize(0, 0);
    imageLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    imageLabel->setFixedSize(artworkSize, artworkSize);
  }

  // In grid mode, folder icons are drawn in paintEvent for correct positioning
  // relative to nameLabel geometry (layout-managed). Hide the QLabel here.
  if (m_folderIconLabel) {
    m_folderIconLabel->hide();
  }

  if (nameLabel) {
    nameLabel->setVisible(true);
    nameLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop); // Reset alignment for grid mode
    nameLabel->setWordWrap(true);                             // Re-enable word wrap for grid mode
    // Reset min/max constraints that may have been set in list mode
    nameLabel->setMinimumSize(0, 0);
    nameLabel->setMaximumSize(artworkSize, reservedTextHeight);
    nameLabel->setFixedHeight(reservedTextHeight);
    // Let layout manage horizontal positioning (centered below artwork)

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
