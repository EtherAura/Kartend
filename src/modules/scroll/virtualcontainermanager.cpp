#include "virtualcontainermanager.h"
#include "filtermanager.h"
#include "selectionoverlaymanager.h"
#include "uiconstants.h"
#include <QWidget>
#include <QScrollArea>
#include <QScrollBar>

VirtualContainerManager::VirtualContainerManager(QObject *parent)
    : QObject(parent) {}

VirtualContainerManager::~VirtualContainerManager() {
  cleanupContainer();
}

void VirtualContainerManager::createContainer() {
  cleanupContainer();

  if (!m_gridContainer) {
    qWarning() << "VirtualContainerManager::createContainer: m_gridContainer is null, cannot create container";
    return;
  }

  m_virtualContainer = new QWidget(m_gridContainer);
  m_virtualContainer->setObjectName("virtualContainer");
  m_virtualContainer->setAttribute(Qt::WA_TransparentForMouseEvents, false);
  m_virtualContainer->setStyleSheet("background: transparent;");

  // Update overlay manager's parent widget
  if (m_overlayManager) {
    m_overlayManager->setParentWidget(m_virtualContainer);
    m_overlayManager->setGridContainer(m_gridContainer);
  }
}

void VirtualContainerManager::cleanupContainer() {
  if (!m_virtualContainer) {
    return;
  }
  m_virtualContainer->deleteLater();
  m_virtualContainer = nullptr;
}

int VirtualContainerManager::getEffectiveViewportWidth() const {
  if (!m_scrollArea || !m_scrollArea->viewport()) {
    return 0;
  }
  static constexpr int MIN_EFFECTIVE_VIEWPORT_WIDTH = 200;
  return qMax(MIN_EFFECTIVE_VIEWPORT_WIDTH, m_scrollArea->viewport()->width());
}

int VirtualContainerManager::getScrollbarWidth() const {
  if (!m_scrollArea) {
    return 0;
  }
  QScrollBar *verticalScrollbar = m_scrollArea->verticalScrollBar();
  if (!verticalScrollbar) {
    return 0;
  }
  if (verticalScrollbar->isVisible()) {
    return verticalScrollbar->width();
  }
  // Estimate scrollbar width if not visible but will be needed
  int barWidth = verticalScrollbar->sizeHint().width();
  static constexpr int DEFAULT_SCROLLBAR_WIDTH = 16;
  return barWidth > 0 ? barWidth : DEFAULT_SCROLLBAR_WIDTH;
}

bool VirtualContainerManager::willNeedVerticalScrollbar(int totalHeight) const {
  if (!m_scrollArea || !m_scrollArea->viewport()) {
    return false;
  }
  return totalHeight > m_scrollArea->viewport()->height();
}

void VirtualContainerManager::positionContainer(const ContainerPositionParams &params) {
  if (!m_virtualContainer || !m_scrollArea) {
    return;
  }

  int viewportWidth = getEffectiveViewportWidth();
  int scrollbarWidth = getScrollbarWidth();

  bool scrollbarVisible = m_scrollArea->verticalScrollBar() &&
                          m_scrollArea->verticalScrollBar()->isVisible();
  int availableWidth = viewportWidth;
  if (!scrollbarVisible) {
    availableWidth -= scrollbarWidth;
  }

  if (availableWidth < 0) {
    availableWidth = viewportWidth;
  }

  int contentWidth = params.totalWidth;
  bool overflow = contentWidth > availableWidth;

  setupContainerSizes(availableWidth, contentWidth, params.totalHeight, overflow);

  HorizontalAlignment align = getEffectiveAlignment(params);

  bool verticalBarHidden = m_scrollArea->verticalScrollBar() &&
                          !m_scrollArea->verticalScrollBar()->isVisible();
  int leftOffset = 0;
  int rightOffset = 0;
  int centerOffset = 0;
  calculateScrollbarOffsets(verticalBarHidden, leftOffset, rightOffset, centerOffset);

  int containerX = calculateContainerPosition(availableWidth, contentWidth, overflow,
                                             align, leftOffset, rightOffset, centerOffset);

  configureHorizontalScrollbar(overflow);
  m_virtualContainer->move(containerX, 0);
  m_virtualContainer->resize(params.totalWidth, params.totalHeight);
}

void VirtualContainerManager::setupContainerSizes(int availableWidth, int contentWidth,
                                                  int totalHeight, bool overflow) {
  if (!m_gridContainer || !m_virtualContainer) {
    return;
  }

  if (overflow) {
    m_gridContainer->setMinimumSize(availableWidth, totalHeight);
    m_gridContainer->setMaximumWidth(availableWidth);
  } else {
    m_gridContainer->setMinimumSize(qMax(availableWidth, contentWidth), totalHeight);
    m_gridContainer->setMaximumWidth(QWIDGETSIZE_MAX);
  }
  m_gridContainer->setMaximumHeight(totalHeight);
  m_virtualContainer->setFixedSize(contentWidth, totalHeight);
}

HorizontalAlignment VirtualContainerManager::getEffectiveAlignment(
    const ContainerPositionParams &params) const {
  HorizontalAlignment align = params.alignment;
  if (params.isFiltered && params.totalItems > 0 && params.itemsPerRow > 0) {
    if (params.totalItems < (params.itemsPerRow - 2)) {
      align = HorizontalAlignment::Center;
    }
  }
  return align;
}

void VirtualContainerManager::calculateScrollbarOffsets(bool verticalBarHidden,
                                                        int &leftOffset,
                                                        int &rightOffset,
                                                        int &centerOffset) const {
  static constexpr int HIDDEN_SCROLLBAR_LEFT_OFFSET = -5;
  static constexpr int HIDDEN_SCROLLBAR_RIGHT_OFFSET = -20;
  static constexpr int HIDDEN_SCROLLBAR_CENTER_OFFSET = -10;

  leftOffset = verticalBarHidden ? HIDDEN_SCROLLBAR_LEFT_OFFSET : 0;
  rightOffset = verticalBarHidden ? HIDDEN_SCROLLBAR_RIGHT_OFFSET : 0;
  centerOffset = verticalBarHidden ? HIDDEN_SCROLLBAR_CENTER_OFFSET : 0;
}

int VirtualContainerManager::calculateContainerPosition(int availableWidth, int contentWidth,
                                                        bool overflow, HorizontalAlignment align,
                                                        int leftOffset, int rightOffset,
                                                        int centerOffset) const {
  static constexpr int CONTAINER_EXTRA_SHIFT = 20;
  int extraShift = CONTAINER_EXTRA_SHIFT;
  int containerX = 0;

  if (overflow) {
    int overflowAmount = contentWidth - availableWidth;
    containerX = -(overflowAmount / 2) + centerOffset + extraShift;
    if (align == HorizontalAlignment::Center) {
      static constexpr int CENTER_ALIGNMENT_ADJUSTMENT = 10;
      containerX -= CENTER_ALIGNMENT_ADJUSTMENT;
    }
  } else if (contentWidth <= availableWidth) {
    switch (align) {
    case HorizontalAlignment::Left:
      containerX = -UIConstants::Grid::CONTAINER_OFFSET -
                   UIConstants::Grid::CONTAINER_LEFT_OFFSET + leftOffset;
      break;
    case HorizontalAlignment::Right:
      containerX = availableWidth - contentWidth +
                   UIConstants::Grid::CONTAINER_RIGHT_OFFSET + rightOffset + 10;
      break;
    case HorizontalAlignment::Center:
    default:
      containerX = ((availableWidth - contentWidth) / 2) +
                   centerOffset + extraShift;
      break;
    }
  } else {
    int overflowAmount = contentWidth - availableWidth;
    switch (align) {
    case HorizontalAlignment::Left:
      containerX = -UIConstants::Grid::CONTAINER_OFFSET - 
                   UIConstants::Grid::CONTAINER_LEFT_OFFSET + leftOffset;
      break;
    case HorizontalAlignment::Right:
      containerX = -overflowAmount +
                   UIConstants::Grid::CONTAINER_RIGHT_OFFSET + rightOffset;
      break;
    case HorizontalAlignment::Center:
    default:
      containerX = -(overflowAmount / 2) + centerOffset + extraShift;
      break;
    }
  }

  return containerX;
}

void VirtualContainerManager::configureHorizontalScrollbar(bool overflow) {
  if (overflow && m_scrollArea) {
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (auto *horizontalScrollbar = m_scrollArea->horizontalScrollBar()) {
      horizontalScrollbar->setValue(0);
      horizontalScrollbar->hide();
    }
  }
}
