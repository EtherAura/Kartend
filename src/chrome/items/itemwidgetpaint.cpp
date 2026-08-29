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
    if (shouldPaintTitle()) {
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

void ItemWidget::setGridPitch(int pitchWidth, int pitchHeight) {
  if (m_gridPitchWidth == pitchWidth && m_gridPitchHeight == pitchHeight) {
    return;
  }
  m_gridPitchWidth = pitchWidth;
  m_gridPitchHeight = pitchHeight;
  applyDimensions();
}

void ItemWidget::applyDimensions() {
  QString currentName = itemName;
  QString currentPath = filePath;
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

  // Resolved BEFORE the layout margins and the artwork are sized, because both
  // depend on it. The list-mode arm above already returned, so this is the grid
  // answer either way — it shares shouldPaintTitle() so the rule has a single
  // definition rather than a third hand-copy to drift out of.
  const bool shouldShowTitle = shouldPaintTitle();

  // Kartend-hxly2: the configured item size names the TILE THE USER SEES, so a
  // cell with no caption in it hands its whole area to the artwork. Both the
  // margin and the layout spacing exist to hold a caption block off the cell
  // edge and off the artwork; with no caption there is nothing for either to
  // separate, and charging for them anyway is what made "Item Width: 325" draw
  // a 277px tile at best. An untitled tile therefore fills its cell exactly,
  // and the gap between neighbours becomes the spacing the user configured
  // rather than that plus 48px of invisible reservation.
  const int gridMargin = shouldShowTitle ? UIConstants::Widget::MARGIN : 0;
  const int gridSpacing = shouldShowTitle ? UIConstants::Widget::SPACING : 0;
  if (layout()) {
    layout()->setContentsMargins(gridMargin, gridMargin, gridMargin, gridMargin);
    layout()->setSpacing(gridSpacing);
  }
  // Use the current font size for layout calculations to ensure titles don't
  // overlap artwork But ensure we don't reserve less space than the default
  // 12pt to maintain consistent artwork sizing/spacing
  QFont referenceFont = this->font();
  referenceFont.setPointSize(std::max(12, m_fontSize));
  QFontMetrics referenceFm(referenceFont);

  // Three lines, so a long wrapped title never overlaps the art. Reserved only
  // when a title is actually drawn — hiding titles gives the band back to the
  // artwork, which is what it is there for (Kartend-ari1x).
  //
  // THIS WAS REVERTED ONCE, on 2026-08-20, after it broke a real library with
  // artwork drawn overlapping artwork and rows colliding. Both explanations
  // recorded at the time were wrong, and the correct one was the first, which
  // had been retracted: the reporter's config DOES use negative grid spacing
  // (horizontalSpacing = verticalSpacing = -80 on all 18 collections), and
  // their cells are HEIGHT-limited, not width-limited. At 325x325 the numbers
  // are availableWidth 285, availableHeight 226 -> art 226, against an
  // effective tile pitch of 325 + (-80) = 245px. Freeing the band took the art
  // to 277 and so 32px PAST the pitch on both axes — neighbouring tiles, not
  // this widget's own box, which never leaves its cell.
  //
  // Hence the pitch clamp below. It is what makes freeing the band safe, and
  // it must not be removed independently of this.
  const int textLines = 3;
  const int singleLineHeight = referenceFm.ascent() + referenceFm.descent();
  const int reservedTextHeight = shouldShowTitle ? singleLineHeight * textLines : 0;

  // What the cell actually spends, rather than a hand-kept tally that drifts
  // from it: the margins the layout was just given, plus — only when a caption
  // is drawn — the artwork-to-label gap and the text band itself.
  //
  // The old arithmetic also subtracted Widget::PADDING (20px) on both axes.
  // Nothing spends it. The layout's contents margins are set from Widget::MARGIN
  // directly above, and ARTWORK_BACKDROP_INSET happens to equal that same
  // MARGIN — so the two of them were double-counting the ONE margin the layout
  // charges, and PADDING was a third helping on top. That is the bulk of the
  // 48px a 325px tile was losing (Kartend-hxly2).
  const int marginTotal = 2 * gridMargin;
  int availableWidth = m_itemWidth - marginTotal;
  int availableHeight =
      m_itemHeight - marginTotal - (shouldShowTitle ? gridSpacing + reservedTextHeight : 0);
  int artworkSize = qMin(availableWidth, availableHeight);

  // Clamp to what the GRID actually gives this tile. m_itemWidth/m_itemHeight
  // are the cell box, but with negative spacing the drawn envelope per tile is
  // narrower than its box — the neighbour's cell starts before this one ends.
  // The widget cannot see that, so the layer that owns GridMetrics feeds it in
  // via setGridPitch(). SPACING is held back as a visible gap so adjacent
  // covers do not end up flush against each other.
  //
  // With zero or positive spacing the pitch is >= the cell, so this never
  // binds and no ordinary configuration changes behaviour.
  // Clamped to the pitch ITSELF, holding nothing back. Spacing is the gap the
  // user asked for (Kartend-hxly2), so subtracting a further gutter here would
  // make a requested 0 render as 8 and every other value read 8 too small.
  // With spacing >= 0 the pitch is at least the cell and this never binds; it
  // stays as the guard that stops a tile ever crossing into its neighbour.
  const int pitchLimit = qMin(m_gridPitchWidth > 0 ? m_gridPitchWidth : artworkSize,
                              m_gridPitchHeight > 0 ? m_gridPitchHeight : artworkSize);
  if (m_gridPitchWidth > 0 || m_gridPitchHeight > 0) {
    artworkSize = qMin(artworkSize, qMax(1, pitchLimit));
  }
  m_artworkSize = artworkSize;

  if (imageLabel) {
    imageLabel->setVisible(true); // Ensure visible in grid mode
    // Reset min/max constraints that may have been zeroed in list mode
    imageLabel->setMinimumSize(0, 0);
    imageLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    imageLabel->setFixedSize(artworkSize, artworkSize);
    // CENTRE the art box in the cell. artworkSize is the SQUARE that fits the
    // cell, so on any cell wider than it is tall the label is narrower than
    // the cell — and a QVBoxLayout parks a Fixed-width child at the left
    // margin. Measured 2026-08-19: a 200px cell held its 106px art at x=10,
    // leaving 10px dead on the left and 84px on the right. That asymmetry is
    // what made every horizontal alignment look wrong while the grid
    // container was in fact placed exactly: right-aligned art stopped 94px
    // short of the edge its cell was flush against.
    // AlignCenter on the label itself only centres the pixmap INSIDE the
    // label; centring the label inside the cell is the layout's call.
    if (QLayout *box = layout()) {
      box->setAlignment(imageLabel, Qt::AlignHCenter);
    }
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
    // FIXED width, not a maximum: giving the layout an alignment flag below
    // makes it hand the label its sizeHint instead of the cell width, and a
    // word-wrapping label's hint is narrower than the cap — which pushed
    // titles onto a third line that then spilled into the row beneath.
    // Pinning the width keeps the wrap identical to before while still
    // letting the label be centred as a block.
    nameLabel->setMaximumSize(artworkSize, reservedTextHeight);
    nameLabel->setFixedWidth(artworkSize);
    nameLabel->setFixedHeight(reservedTextHeight);
    // Centre the title band in the cell, the same way the art box above is
    // centred — this is the "centered below artwork" the line below always
    // claimed. The label is capped to artworkSize, so like the art it is
    // narrower than the cell, and a QVBoxLayout parks it at the LEFT margin
    // unless told otherwise. Both were left-parked before, so they agreed
    // with each other by accident; centring only the art broke that and put
    // the title off to one side of its own cover.
    if (QLayout *box = layout()) {
      box->setAlignment(nameLabel, Qt::AlignHCenter);
    }

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
  // storedPixmap is intentionally NOT re-fed through setArtworkPixmap here:
  // it already holds the artwork, and routing a worker-composed card back
  // through the raw-artwork setter cleared m_storedIsComposed — the deferred
  // refresh below then re-composited the already-composed card (doubled
  // border, re-masked corners at the wrong scale). The coalesced
  // onArtworkChanged() below re-renders the stored artwork for the new
  // dimensions either way, with the composed-card size guard deciding
  // whether it can be set 1:1.
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
