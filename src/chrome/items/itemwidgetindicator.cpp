// Triangle indicator update + paint extracted from itemwidget.cpp.
// These remain ItemWidget members; this is a translation-unit split.
#include "itemwidget.h"
#include "uiconstants/collectionicon.h"
#include "uiconstants/metadata.h"
#include "uiconstants/widget.h"
#include <algorithm>
#include <QColor>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <Qt>

// Set pulse opacity
void ItemWidget::updateTriangleIndicator() {
  if ((!triangleIndicator) || !m_isSubcollection || m_artworkSize <= 0) {
    return;
  }
  if (isSelectedState) {
    // Force layout update to get accurate geometry
    if (layout()) {
      layout()->activate();
    }

    // Use imageLabel geometry if valid, otherwise calculate from known
    // dimensions
    QRect imageRect = imageLabel ? imageLabel->geometry() : QRect();
    if (imageRect.width() != m_artworkSize || imageRect.height() != m_artworkSize) {
      // Geometry not yet updated - calculate expected position
      // Layout has 10px margins, imageLabel is centered horizontally
      int leftMargin = (m_itemWidth - m_artworkSize) / 2;
      int topMargin = UIConstants::Widget::MARGIN; // 10px from .ui file
      imageRect = QRect(leftMargin, topMargin, m_artworkSize, m_artworkSize);
    }

    int borderSpacing = UIConstants::CollectionIcon::ITEM_SPACING;
    int indicatorX = imageRect.right() + borderSpacing -
                     (UIConstants::Widget::TRIANGLE_SIZE + UIConstants::Metadata::VALUE_PADDING);
    int indicatorY = imageRect.top() - borderSpacing + UIConstants::Metadata::VALUE_PADDING;
    triangleIndicator->setGeometry(indicatorX, indicatorY, UIConstants::Widget::TRIANGLE_SIZE,
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

  // Use per-collection tile color if set, otherwise system highlight
  QColor badgeColor;
  if (!s_tileColor.isEmpty() && QColor::isValidColorName(s_tileColor)) {
    badgeColor = QColor(s_tileColor);
  } else {
    badgeColor = palette().color(QPalette::Highlight);
  }
  painter.setBrush(badgeColor);
  painter.setPen(QPen(badgeColor.darker(UIConstants::Widget::HIGHLIGHT_DARKEN_FACTOR), 1));
  QPolygon triangle;
  triangle << QPoint(UIConstants::Metadata::VALUE_PADDING, UIConstants::Metadata::VALUE_PADDING)
           << QPoint(UIConstants::Widget::TRIANGLE_SIZE - UIConstants::Metadata::VALUE_PADDING,
                     UIConstants::Metadata::VALUE_PADDING)
           << QPoint(UIConstants::Widget::TRIANGLE_SIZE - UIConstants::Metadata::VALUE_PADDING,
                     UIConstants::Widget::TRIANGLE_SIZE - UIConstants::Metadata::VALUE_PADDING);
  painter.drawPolygon(triangle);
  // triangleIndicator is a plain QWidget in the .ui file, so the pixmap is
  // hosted on a child QLabel created lazily on the first paint. Reuse it on
  // every subsequent paint — allocating a fresh child per call would
  // accumulate stacked labels for the widget's lifetime. It hides/shows with
  // its parent, so the update path above needs no extra bookkeeping.
  auto *indicatorLabel =
      triangleIndicator->findChild<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
  if (!indicatorLabel) {
    indicatorLabel = new QLabel(triangleIndicator);
    indicatorLabel->setGeometry(0, 0, UIConstants::Widget::TRIANGLE_SIZE,
                                UIConstants::Widget::TRIANGLE_SIZE);
    indicatorLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    indicatorLabel->show();
  }
  indicatorLabel->setPixmap(pixmap);
}
