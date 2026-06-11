// Sibling translation unit for ScrollManager.
// After, the actual virtual-scrolling logic lives in
// VirtualScrollEngine (virtualscrollengine.{h,cpp}). The methods below remain
// on ScrollManager as the public/private API surface, but each one is now a
// thin forwarder to the engine. The engine accesses ScrollManager state via
// friendship; canonical state ownership stays here.
#include "coverflowcontroller.h"
#include "scrollmanager.h"
#include "virtualscrollengine.h"

void ScrollManager::updateVirtualView() {
  m_engine->updateVirtualView();
}

void ScrollManager::enforceScrollContentConstraints() {
  m_engine->enforceScrollContentConstraints();
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
