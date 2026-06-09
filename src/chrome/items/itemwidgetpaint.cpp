// Painting and dimension layout methods extracted from itemwidget.cpp:
//   - paintEvent
//   - setItemDimensions
//   - applyDimensions
// These remain ItemWidget members and access existing class state and statics.
#include "itemwidget.h"
#include "propertyutils.h"
#include "uiconstants/animation.h"
#include "uiconstants/listview.h"
#include "uiconstants/widget.h"
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

// Refreshes the cached title measurement (full-string advance + right-elided
// text) only when the name, font, or width differs from the last paint, so a
// continuously-repainting tile (selection pulse, scroll) doesn't re-measure the
// string every frame.
void ItemWidget::ensureTextMeasure(const QString &name, const QFont &font, int width) const {
  if (m_textMeasureWidth == width && m_textMeasureName == name && m_textMeasureFont == font) {
    return;
  }
  const QFontMetrics fm(font);
  m_textMeasureAdvance = fm.horizontalAdvance(name);
  m_textMeasureElided = fm.elidedText(name, Qt::ElideRight, width);
  m_textMeasureName = name;
  m_textMeasureFont = font;
  m_textMeasureWidth = width;
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
      // hideTitles is a grid-mode concern. In list mode the row
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
        // List mode: elide text only if it exceeds available width. The
        // measurement is cached and recomputed only when name/font/width change.
        ensureTextMeasure(itemName, titleFont, textRect.width());
        if (m_textMeasureAdvance > textRect.width()) {
          painter.drawText(textRect, nameLabel->alignment(), m_textMeasureElided);
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
          ensureTextMeasure(itemName, painter.font(), textRect.width());
          int textWidth = m_textMeasureAdvance;

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

  // State-flag badges (Kartend-elte) — paint on top of the artwork +
  // title so glyphs stay visible regardless of tile contents. Skipped
  // for subcollections / virtual folders (no per-item flags) and for
  // unpopulated tiles (empty filePath after a reset-for-reuse pass).
  if (!m_isSubcollection && !m_isVirtualFolder && !filePath.isEmpty()) {
    paintStateBadges(painter);
  }
}

// Renders the pinned / hidden / continue-later badges (Kartend-elte) in
// the top-right corner of the artwork area. Badges are drawn as a stacked
// row of small circles with a glyph inside so a tile with two or three
// markers stays legible even at small grid widths. Empty registry → no
// badges; the widget pool's reuse pass leaves filePath empty on tiles
// that haven't been populated yet, which guards the lookup.
QHash<QString, ItemWidget::StateFlags> &itemWidgetStateFlagsRegistry();

void ItemWidget::paintStateBadges(QPainter &painter) {
  const auto &registry = itemWidgetStateFlagsRegistry();
  const auto it = registry.constFind(filePath);
  if (it == registry.cend() || !it->any()) return;
  const StateFlags &flags = it.value();

  // Place the badge strip against the artwork's top-right corner. List
  // mode renders artwork through the dedicated artworkColumnWidth, so
  // anchor against the column's right edge instead of the whole row.
  QRect artRect;
  if (m_isListMode) {
    const int x = m_collectionColumnWidth;
    artRect = QRect(x, 0, m_artworkColumnWidth, height());
  } else if (imageLabel) {
    artRect = imageLabel->geometry();
  } else {
    return;
  }

  const int badgeSize = std::max(14, std::min(22, artRect.width() / 8));
  const int margin = 4;
  int x = artRect.right() - badgeSize - margin;
  const int y = artRect.top() + margin;

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  QFont glyphFont = painter.font();
  glyphFont.setPixelSize(static_cast<int>(badgeSize * 0.7));
  glyphFont.setBold(true);
  painter.setFont(glyphFont);

  auto drawBadge = [&](const QColor &fill, QChar glyph) {
    QColor bg = fill;
    bg.setAlphaF(0.85);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawEllipse(x, y, badgeSize, badgeSize);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(x, y, badgeSize, badgeSize), Qt::AlignCenter, QString(glyph));
    x -= badgeSize + 2;
  };

  // Order: pinned (most prominent), continue-later (resume marker),
  // hidden (de-emphasised) — same precedence the Smart playlists in
  // Kartend-68ky use.
  if (flags.pinned) drawBadge(QColor(80, 130, 230), QChar(0x2605));      // ★
  if (flags.continueLater) drawBadge(QColor(220, 160, 60), QChar(u'⏵')); // resume
  if (flags.hidden) drawBadge(QColor(120, 120, 120), QChar(u'∅'));       // hidden

  painter.restore();
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
  // Defer artwork update until after all recycle property changes settle -
  // ensures the widget displays correctly after being reused from the pool.
  // Kartend-4hct5: coalesce — if a refresh is already queued for this widget
  // (e.g. several applyDimensions() calls in the same event-loop turn during a
  // layout change), don't queue another; one onArtworkChanged() after settling
  // is enough.
  if (!m_artworkRefreshPending) {
    m_artworkRefreshPending = true;
    QPointer<ItemWidget> ptr = this;
    // 0ms defer: run onArtworkChanged() after the current event-loop turn so
    // all the recycle/dimension property changes above have settled first.
    QTimer::singleShot(0, this, [ptr]() {
      if (ptr) {
        ptr->m_artworkRefreshPending = false;
        ptr->onArtworkChanged();
      }
    });
  }
}
