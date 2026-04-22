// Selection facade methods. Real logic lives in SelectionDisplayManager
// (Kartend-p79). These thin wrappers preserve the ScrollManager public API
// for external callers (viewport, keyboard, selection, interaction modules).
#include "scrollmanager.h"
#include "selectiondisplaymanager.h"

void ScrollManager::onArrowKeyViewUpdate() {
  if (m_selectionDisplay) {
    m_selectionDisplay->onArrowKeyViewUpdate();
  }
}

void ScrollManager::updateSelectionForIndex(int selectedIndex) {
  if (m_selectionDisplay) {
    m_selectionDisplay->updateSelectionForIndex(selectedIndex);
  }
}

void ScrollManager::refreshSelectionOverlayState() {
  if (m_selectionDisplay) {
    m_selectionDisplay->refreshSelectionOverlayState();
  }
}

void ScrollManager::setForceSelectionOverlayVisible(bool force) {
  if (m_selectionDisplay) {
    m_selectionDisplay->setForceSelectionOverlayVisible(force);
  }
}
