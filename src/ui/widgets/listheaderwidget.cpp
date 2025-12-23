// List header widget for list view mode with sortable column headers.
#include "listheaderwidget.h"
#include "uiconstants.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

ListHeaderWidget::ListHeaderWidget(QWidget *parent) : QWidget(parent) {
  setupUI();
}

void ListHeaderWidget::setupUI() {
  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(UIConstants::ListView::TEXT_LEFT_PADDING, 4,
                               UIConstants::ListView::TEXT_RIGHT_PADDING, 4);
  m_layout->setSpacing(0);

  // Name column label (clickable)
  m_nameLabel = new QLabel(tr("Name ▲"), this);
  m_nameLabel->setCursor(Qt::PointingHandCursor);
  m_nameLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_nameLabel->installEventFilter(this);

  // Collection column label (clickable)
  m_collectionLabel = new QLabel(tr("Collection"), this);
  m_collectionLabel->setCursor(Qt::PointingHandCursor);
  m_collectionLabel->setStyleSheet("QLabel { font-weight: bold; }");
  m_collectionLabel->installEventFilter(this);

  m_layout->addWidget(m_nameLabel, 1);  // Stretch
  m_layout->addWidget(m_collectionLabel, 0);

  setLayout(m_layout);
  setFixedHeight(UIConstants::ListView::DEFAULT_ROW_HEIGHT);
}

void ListHeaderWidget::setSortColumn(ListSortColumn column, bool ascending) {
  m_sortColumn = column;
  m_sortAscending = ascending;
  updateSortIndicators();
}

void ListHeaderWidget::setNameColumnWidth(int width) {
  m_nameColumnWidth = width;
  if (m_nameLabel) {
    m_nameLabel->setMinimumWidth(width);
  }
}

void ListHeaderWidget::updateSortIndicators() {
  QString nameText = tr("Name");
  QString collectionText = tr("Collection");

  QString upArrow = " ▲";
  QString downArrow = " ▼";

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
}

void ListHeaderWidget::paintEvent(QPaintEvent *event) {
  QWidget::paintEvent(event);

  QPainter painter(this);
  
  // Draw bottom border line
  painter.setPen(palette().color(QPalette::Mid));
  painter.drawLine(0, height() - 1, width(), height() - 1);
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
