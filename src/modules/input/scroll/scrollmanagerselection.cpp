// Selection facade methods. Real logic lives in SelectionDisplayManager
// These thin wrappers preserve the ScrollManager public API
// for external callers (viewport, keyboard, selection, interaction modules).
#include "coverflowwidget.h"
#include "scrollmanager.h"
#include "selectiondisplaymanager.h"
#include "timerutils.h"

void ScrollManager::onArrowKeyViewUpdate() {
  if (m_selectionDisplay) {
    m_selectionDisplay->onArrowKeyViewUpdate();
  }
}

void ScrollManager::updateSelectionForIndex(int selectedIndex) {
  if (m_selectionDisplay) {
    m_selectionDisplay->updateSelectionForIndex(selectedIndex);
  }
  // keep the cover-flow widget in sync with the canonical
  // selection — animate the glide unless we're not actually displayed yet.
  if (m_coverFlowWidget && coverFlowActive()) {
    m_coverFlowWidget->setSelectedIndex(selectedIndex, true);
    // Lazy video-path + gallery resolution: scanning the video directory
    // and loading per-item artwork rows for every one of N items at
    // rebuild time freezes the UI for large collections, so the per-item
    // lookup happens here on selection landing. Debounced so a wheel
    // sweep across the carousel only triggers one DB+FS pass at the
    // trailing edge instead of one per ~10ms tick.
    m_pendingCoverFlowVisualIndex = selectedIndex;
    if (m_coverFlowResolveDebouncer) {
      m_coverFlowResolveDebouncer->trigger();
    } else {
      // Debouncer not constructed yet (very-early calls during setup).
      // Fall back to synchronous resolution so behaviour matches the
      // pre-debounce path during init.
      resolveAndPushCoverFlowVideo(selectedIndex);
      resolveAndPushCoverFlowGallery(selectedIndex);
    }
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
