// List header widget for list view mode with sortable column headers.
#include "listheaderwidget.h"
#include "uiconstants.h"

#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

Q_LOGGING_CATEGORY(lcListHeaderWidget, "kartend.listheaderwidget")

ListHeaderWidget::ListHeaderWidget(QWidget *parent) : QWidget(parent) {
  // Enable background painting so header occludes scrolling content beneath it
  setAutoFillBackground(true);
  setMouseTracking(true);            // For resize cursor updates
  setCursor(Qt::PointingHandCursor); // Show clickable cursor over header
  setupUI();
}

void ListHeaderWidget::setupUI() {
  m_layout = new QHBoxLayout(this);
  // Header spans full width including grid margins, so add grid margins to text
  // padding to align with item columns which are positioned at (grid margin +
  // text padding)
  int leftMargin = UIConstants::Grid::MARGINS + UIConstants::ListView::TEXT_LEFT_PADDING;
  int rightMargin = UIConstants::Grid::MARGINS + UIConstants::ListView::TEXT_RIGHT_PADDING;
  m_layout->setContentsMargins(leftMargin, 4, rightMargin, 4);
  m_layout->setSpacing(8);

  // Name column label (clickable, expandable)
  m_nameLabel = new QLabel(tr("Name ▲"), this);
  m_nameLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents,
                            true); // Pass clicks to parent

  // Collection column label (clickable, resizable width)
  m_collectionLabel = new QLabel(tr("Collection"), this);
  m_collectionLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_collectionLabel->setFixedWidth(m_collectionColumnWidth);
  m_collectionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_collectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents,
                                  true); // Pass clicks to parent

  // Artwork column label (clickable, fixed width, on the right)
  m_artworkLabel = new QLabel(this);
  QIcon artworkIcon = UIConstants::Icons::fromTheme(UIConstants::Icons::IMAGE);
  if (!artworkIcon.isNull()) {
    m_artworkLabel->setPixmap(artworkIcon.pixmap(16, 16));
  }
  m_artworkLabel->setFixedWidth(m_artworkColumnWidth);
  m_artworkLabel->setAlignment(Qt::AlignCenter);
  m_artworkLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_artworkLabel->setToolTip(tr("Sort by artwork"));
  m_artworkLabel->setAttribute(Qt::WA_TransparentForMouseEvents,
                               true); // Pass clicks to parent

  m_layout->addWidget(m_nameLabel, 1); // Stretch
  m_layout->addWidget(m_collectionLabel,
                      0);                 // Fixed width (but resizable via drag)
  m_layout->addWidget(m_artworkLabel, 0); // Fixed width, rightmost

  setLayout(m_layout);
  setFixedHeight(UIConstants::ListView::DEFAULT_ROW_HEIGHT);
}

void ListHeaderWidget::setSortColumn(ListSortColumn column, bool ascending) {
  m_sortColumn = column;
  m_sortAscending = ascending;
  updateSortIndicators();
}

void ListHeaderWidget::setCollectionColumnWidth(int width) {
  m_collectionColumnWidth = qBound(MIN_COLLECTION_WIDTH, width, MAX_COLLECTION_WIDTH);
  if (m_collectionLabel) {
    m_collectionLabel->setFixedWidth(m_collectionColumnWidth);
  }
}

void ListHeaderWidget::setArtworkColumnWidth(int width) {
  m_artworkColumnWidth = qBound(MIN_ARTWORK_WIDTH, width, MAX_ARTWORK_WIDTH);
  if (m_artworkLabel) {
    m_artworkLabel->setFixedWidth(m_artworkColumnWidth);
  }
}

void ListHeaderWidget::updateSortIndicators() {
  QString nameText = tr("Name");
  QString collectionText = tr("Collection");

  QString upArrow = " ▲";
  QString downArrow = " ▼";

  bool artworkSorted = (m_sortColumn == ListSortColumn::Artwork);

  if (m_sortColumn == ListSortColumn::Name) {
    nameText += m_sortAscending ? upArrow : downArrow;
  } else if (m_sortColumn == ListSortColumn::Collection) {
    collectionText += m_sortAscending ? upArrow : downArrow;
  }

  if (m_nameLabel) {
    m_nameLabel->setText(nameText);
  }
  if (m_collectionLabel) {
    m_collectionLabel->setText(collectionText);
  }
  if (m_artworkLabel) {
    // Update artwork label with icon + optional sort indicator
    QIcon icon = UIConstants::Icons::fromTheme(UIConstants::Icons::IMAGE);
    if (!icon.isNull()) {
      m_artworkLabel->setPixmap(icon.pixmap(16, 16));
    }
    // For artwork column sort indicator, we add text after the icon
    if (artworkSorted) {
      QString arrow = m_sortAscending ? upArrow : downArrow;
      m_artworkLabel->setText(arrow);
      // Show both pixmap and text - clear pixmap, show text with arrow only
      // Actually simpler: just show arrow text when sorted by artwork
      m_artworkLabel->setPixmap(QPixmap());
      QIcon artIcon = UIConstants::Icons::fromTheme(UIConstants::Icons::IMAGE);
      if (!artIcon.isNull()) {
        // Can't easily combine icon + text in QLabel, so just show arrow when
        // sorted
        m_artworkLabel->setText(arrow);
      }
    } else {
      m_artworkLabel->setText(QString());
    }
  }
}

bool ListHeaderWidget::isOverResizeHandle(int x) const {
  if (!m_collectionLabel) {
    return false;
  }
  // The resize handle is at the left edge of the collection column
  int separatorX = m_collectionLabel->x();
  return x >= separatorX - RESIZE_HANDLE_WIDTH && x <= separatorX + RESIZE_HANDLE_WIDTH;
}

bool ListHeaderWidget::isOverArtworkResizeHandle(int x) const {
  if (!m_artworkLabel) {
    return false;
  }
  // The resize handle is at the left edge of the artwork column
  int separatorX = m_artworkLabel->x();
  return x >= separatorX - RESIZE_HANDLE_WIDTH && x <= separatorX + RESIZE_HANDLE_WIDTH;
}

void ListHeaderWidget::paintEvent(QPaintEvent *event) {
  QPainter painter(this);

  // Fill background to occlude scrolling content beneath header
  painter.fillRect(rect(), palette().color(QPalette::Window));

  // Draw bottom border line
  painter.setPen(palette().color(QPalette::Mid));
  painter.drawLine(0, height() - 1, width(), height() - 1);

  // Draw column separator/resize handle between Name and Collection
  if (m_collectionLabel) {
    int separatorX = m_collectionLabel->x();
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(separatorX - 4, 4, separatorX - 4, height() - 5);
  }

  // Draw column separator/resize handle between Collection and Artwork
  if (m_artworkLabel) {
    int separatorX = m_artworkLabel->x();
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(separatorX - 4, 4, separatorX - 4, height() - 5);
  }

  QWidget::paintEvent(event);
}

void ListHeaderWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    int x = event->pos().x();
    if (isOverArtworkResizeHandle(x)) {
      m_draggingColumn = true;
      m_draggingArtworkColumn = true;
      m_dragStartX = x;
      m_dragStartWidth = m_artworkColumnWidth;
      setCursor(Qt::SplitHCursor);
    } else if (isOverResizeHandle(x)) {
      m_draggingColumn = true;
      m_draggingArtworkColumn = false;
      m_dragStartX = x;
      m_dragStartWidth = m_collectionColumnWidth;
      setCursor(Qt::SplitHCursor);
    }
    // Track press position for click detection
    m_pressPos = event->pos();
  }
  // Always accept mouse press events to prevent pass-through to list items
  // below
  event->accept();
}

void ListHeaderWidget::mouseMoveEvent(QMouseEvent *event) {
  int x = event->pos().x();
  if (m_draggingColumn) {
    // Dragging left (negative delta) increases column width
    // Dragging right (positive delta) decreases column width
    int delta = x - m_dragStartX;
    int newWidth = m_dragStartWidth - delta;
    if (m_draggingArtworkColumn) {
      setArtworkColumnWidth(newWidth);
      emit artworkColumnWidthChanged(m_artworkColumnWidth);
    } else {
      setCollectionColumnWidth(newWidth);
      emit columnWidthChanged(m_collectionColumnWidth);
    }
  } else {
    // Update cursor when hovering over resize handles
    if (isOverArtworkResizeHandle(x) || isOverResizeHandle(x)) {
      setCursor(Qt::SplitHCursor);
    } else {
      unsetCursor();
    }
  }
  // Always accept to prevent pass-through
  event->accept();
}

void ListHeaderWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    if (m_draggingColumn) {
      m_draggingColumn = false;
      m_draggingArtworkColumn = false;
      unsetCursor();
    } else {
      // Determine which column was clicked based on x position
      int x = event->pos().x();
      int pressX = m_pressPos.x();

      // Get column boundaries from labels (left edge of collection column)
      int collectionLeft = m_collectionLabel ? m_collectionLabel->x() : (width() - 150 - 32);
      int artworkLeft = m_artworkLabel ? m_artworkLabel->x() : (width() - 32);

      qCDebug(lcListHeaderWidget) << "Header click: x=" << x << "pressX=" << pressX
                                  << "collectionLeft=" << collectionLeft
                                  << "artworkLeft=" << artworkLeft;

      // Check if press and release are in the same column
      bool pressInName = pressX < collectionLeft - RESIZE_HANDLE_WIDTH;
      bool releaseInName = x < collectionLeft - RESIZE_HANDLE_WIDTH;
      bool pressInCollection = pressX >= collectionLeft && pressX < artworkLeft;
      bool releaseInCollection = x >= collectionLeft && x < artworkLeft;
      bool pressInArtwork = pressX >= artworkLeft;
      bool releaseInArtwork = x >= artworkLeft;

      qCDebug(lcListHeaderWidget) << "pressInName=" << pressInName
                                  << "releaseInName=" << releaseInName
                                  << "pressInCollection=" << pressInCollection
                                  << "releaseInCollection=" << releaseInCollection;

      if (pressInName && releaseInName) {
        qCDebug(lcListHeaderWidget) << "Emitting columnClicked(Name)";
        emit columnClicked(ListSortColumn::Name);
      } else if (pressInCollection && releaseInCollection) {
        qCDebug(lcListHeaderWidget) << "Emitting columnClicked(Collection)";
        emit columnClicked(ListSortColumn::Collection);
      } else if (pressInArtwork && releaseInArtwork) {
        qCDebug(lcListHeaderWidget) << "Emitting columnClicked(Artwork)";
        emit columnClicked(ListSortColumn::Artwork);
      }
    }
  }
  // Always accept to prevent pass-through
  event->accept();
}

bool ListHeaderWidget::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::MouseButtonRelease) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      if (watched == m_nameLabel) {
        emit columnClicked(ListSortColumn::Name);
        return true;
      } else if (watched == m_collectionLabel) {
        emit columnClicked(ListSortColumn::Collection);
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}
