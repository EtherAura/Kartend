// Sibling translation unit for ScrollManager.
// After, the actual virtual-scrolling logic lives in
// VirtualScrollEngine (virtualscrollengine.{h,cpp}). The methods below remain
// on ScrollManager as the public/private API surface, but each one is now a
// thin forwarder to the engine. The engine accesses ScrollManager state via
// friendship; canonical state ownership stays here.
#include "coverflowcontroller.h"
#include "scrollmanager.h"
#include "virtualscrollengine.h"
#include <QApplication>
#include <QEvent>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QTimer>

void ScrollManager::updateVirtualView() {
  m_engine->updateVirtualView();
}

void ScrollManager::enforceScrollContentConstraints() {
  m_engine->enforceScrollContentConstraints();
}

bool ScrollManager::eventFilter(QObject *watched, QEvent *event) {
  if (m_mediaScrollArea && watched == m_mediaScrollArea->viewport() &&
      event->type() == QEvent::Resize && !m_destroying && !QApplication::closingDown()) {
    // The reposition must run SYNCHRONOUSLY, inside this event, before
    // anything paints. Deferring it let one frame render with the viewport
    // moved but the container not yet compensated — a per-tick fidget of
    // every item while dragging a sidebar (screencast, 2026-08-20).
    // Positioning writes gridContainer geometry, which can re-enter this
    // filter; the rollback guard breaks the loop.
    if (m_engine && !m_inViewportResizeSync) {
      QScopedValueRollback<bool> guard(m_inViewportResizeSync, true);
      // Snapshot the CELL layout first. A sidebar drag changes the viewport
      // width, but with a fixed column count it changes nothing about which
      // items exist or how big they are.
      const int wasPerRow = m_metrics.itemsPerRow;
      const int wasItemW = m_metrics.itemWidth;
      const int wasItemH = m_metrics.itemHeight;
      const int wasRows = m_metrics.totalRows;

      m_engine->preCalculateLayout();

      // Re-materializing recycles widgets through the pool and re-schedules a
      // viewport artwork pass, which is what made the artwork FLASH during a
      // drag (maintainer, 2026-08-20: "items flicker when resizing sidebars").
      // The container has already been repositioned above, and moving it does
      // not disturb the cells inside it — so unless the cell layout genuinely
      // changed there is nothing left to do.
      m_viewportResizeNeedsMaterialize =
          m_metrics.itemsPerRow != wasPerRow || m_metrics.itemWidth != wasItemW ||
          m_metrics.itemHeight != wasItemH || m_metrics.totalRows != wasRows;
    }
    if (m_viewportResizeNeedsMaterialize) {
      // Coalesced: a drag delivers a resize per tick and only the settled
      // geometry deserves a full materialization pass.
      if (!m_viewportResizeTimer) {
        m_viewportResizeTimer = new QTimer(this);
        m_viewportResizeTimer->setSingleShot(true);
        connect(m_viewportResizeTimer, &QTimer::timeout, this, [this]() {
          if (m_destroying || QApplication::closingDown() || !m_engine || !m_virtualContainer) {
            return;
          }
          m_viewportResizeNeedsMaterialize = false;
          m_engine->preCalculateLayout();
          m_engine->updateVirtualView();
        });
      }
      m_viewportResizeTimer->start(0);
    }
  }
  return QObject::eventFilter(watched, event);
}

void ScrollManager::recreateLayout() {
  m_engine->recreateLayout();
}

void ScrollManager::centerHorizontalScrollbar(int /*currentCollectionIndex*/,
                                              const QList<CollectionConfig> & /*collections*/) {
  m_engine->centerHorizontalScrollbar();
}

void ScrollManager::handleLayoutChange() {
  m_engine->handleLayoutChange();
}

void ScrollManager::recalculateContainerMetrics() {
  m_engine->recalculateContainerMetrics();
}

void ScrollManager::forceVirtualViewUpdate() {
  m_engine->forceVirtualViewUpdate();
}

void ScrollManager::preCalculateLayout() {
  m_engine->preCalculateLayout();
}

void ScrollManager::createVirtualContainer() {
  m_engine->createVirtualContainer();
}

void ScrollManager::primeLayoutFor(const CollectionConfig &config) {
  m_engine->primeLayoutFor(config);
}

void ScrollManager::positionVirtualContainer() {
  m_engine->positionVirtualContainer();
}

void ScrollManager::cleanupVirtualContainer() {
  m_engine->cleanupVirtualContainer();
}

void ScrollManager::calculateVirtualMetrics() {
  m_engine->calculateVirtualMetrics();
}

void ScrollManager::connectScrollEvents() {
  m_engine->connectScrollEvents();
}

void ScrollManager::disconnectScrollEvents() {
  m_engine->disconnectScrollEvents();
}

void ScrollManager::ensureWidgetForIndex(int visualIndex) {
  m_engine->ensureWidgetForIndex(visualIndex);
}

void ScrollManager::reconfigureArtworkForActiveWidgets() {
  m_engine->reconfigureArtworkForActiveWidgets();
  // Kartend-x7bn8: receiveItemsRange now patches only the chunk's own card
  // indices instead of rebuilding the whole carousel per chunk, so cards
  // whose artwork directory wasn't in the DirectoryCache at patch time
  // resolved to placeholder. This post-prewarm callback is the moment the
  // cache is warm — do the one full rebuild here (bounded: once per prewarm
  // completion, which is itself debounced upstream) so those cards pick up
  // their artwork, mirroring what this slot already does for grid widgets.
  m_coverFlow->rebuildCardsIfActive();
}

auto ScrollManager::virtualFolderPathForVisualIndex(int visualIndex) const -> QString {
  return m_engine->virtualFolderPathForVisualIndex(visualIndex);
}

auto ScrollManager::filePathForVisualIndex(int visualIndex) const -> QString {
  return m_engine->filePathForVisualIndex(visualIndex);
}
