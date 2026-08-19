#include "virtualcontainermanager.h"
#include "filtermanager.h"
#include "scrollhelpers.h"
#include "selectionoverlaymanager.h"
#include "uiconstants/grid.h"
#include <QLoggingCategory>
#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)

VirtualContainerManager::VirtualContainerManager(QObject *parent) : QObject(parent) {}

VirtualContainerManager::~VirtualContainerManager() {
  cleanupContainer();
}

void VirtualContainerManager::setOverlayManager(SelectionOverlayManager *overlay) {
  m_overlayManager = overlay;
}

void VirtualContainerManager::setFilterManager(FilterManager *filter) {
  m_filterManager = filter;
}

void VirtualContainerManager::createContainer() {
  cleanupContainer();

  if (!m_gridContainer) {
    qCWarning(lcScrollManager) << "VirtualContainerManager::createContainer: m_gridContainer "
                                  "is null, cannot create container";
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

  if (params.isHorizontal) {
    // horizontal layout pins the container to the top-left and
    // sizes the grid container to match the long-axis width so the
    // horizontal scrollbar reflects the full content. Vertical alignment /
    // centering is unused — the column always fits in the viewport.
    if (m_gridContainer) {
      m_gridContainer->setMinimumSize(params.totalWidth, params.totalHeight);
      m_gridContainer->setMaximumSize(params.totalWidth, params.totalHeight);
    }
    m_virtualContainer->setFixedSize(params.totalWidth, params.totalHeight);
    m_virtualContainer->move(0, 0);
    // Scrollbar policy is owned by ScrollManager::updateViewType /
    // applyHorizontalScrollbarSetting — don't override it here.
    if (m_scrollArea->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff) {
      if (auto *hbar = m_scrollArea->horizontalScrollBar()) {
        hbar->show();
      }
    }
    return;
  }

  int viewportWidth = getEffectiveViewportWidth();
  int scrollbarWidth = getScrollbarWidth();

  bool scrollbarVisible =
      m_scrollArea->verticalScrollBar() && m_scrollArea->verticalScrollBar()->isVisible();
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

  const int containerX = calculateContainerPosition(availableWidth, contentWidth, align);

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

HorizontalAlignment
VirtualContainerManager::getEffectiveAlignment(const ContainerPositionParams &params) const {
  return ScrollHelpers::effectiveAlignment(params.alignment, params.isFiltered, params.totalItems,
                                           params.itemsPerRow);
}

int VirtualContainerManager::calculateContainerPosition(int availableWidth, int contentWidth,
                                                        HorizontalAlignment align) const {
  // The whole contract (user, 2026-08-18): an OVERSIZED grid may clip —
  // that is unavoidable — but a grid that FITS must never clip, whatever
  // the alignment. Slack is what is left over; it is negative exactly when
  // the content is too wide, and the same three cases serve both.
  //
  // This replaces a pile of empirical nudges (a 20px shift, a -10 centre
  // tweak, hidden-scrollbar compensations of -5/-20/-10). Those pushed a
  // FITTING grid to a negative x, so the first column was clipped even
  // though there was room for every tile — and on overflow they collapsed
  // Left and Right onto the same position.
  const int slack = availableWidth - contentWidth;
  switch (align) {
  case HorizontalAlignment::Left:
    return 0; // left edge of the content at the left of the viewport
  case HorizontalAlignment::Right:
    return slack; // right edges flush; negative slack clips on the left
  case HorizontalAlignment::Center:
  default:
    return slack / 2; // centred; negative slack clips both sides evenly
  }
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
