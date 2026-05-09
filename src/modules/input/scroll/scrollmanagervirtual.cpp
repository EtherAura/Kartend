// Sibling translation unit for ScrollManager.
// After, the actual virtual-scrolling logic lives in
// VirtualScrollEngine (virtualscrollengine.{h,cpp}). The methods below remain
// on ScrollManager as the public/private API surface, but each one is now a
// thin forwarder to the engine. The engine accesses ScrollManager state via
// friendship; canonical state ownership stays here.
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
}

auto ScrollManager::virtualFolderPathForVisualIndex(int visualIndex) const -> QString {
  return m_engine->virtualFolderPathForVisualIndex(visualIndex);
}

auto ScrollManager::filePathForVisualIndex(int visualIndex) const -> QString {
  return m_engine->filePathForVisualIndex(visualIndex);
}
