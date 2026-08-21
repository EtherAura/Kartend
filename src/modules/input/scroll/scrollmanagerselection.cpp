// Selection facade methods. Real logic lives in SelectionDisplayManager
// These thin wrappers preserve the ScrollManager public API
// for external callers (viewport, keyboard, selection, interaction modules).
#include "coverflowcontroller.h"
#include "scrollmanager.h"
#include "selectiondisplaymanager.h"
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)

void ScrollManager::onArrowKeyViewUpdate() {
  if (m_selectionDisplay) {
    m_selectionDisplay->onArrowKeyViewUpdate();
  }
}

void ScrollManager::updateSelectionForIndex(int selectedIndex) {
  // TEMPORARY DIAGNOSTIC (2026-08-21): the layer ABOVE SelectionDisplayManager.
  // Pairing the two shows whether the stale index (1491 in the report) enters
  // here — i.e. some caller genuinely asks for it — or is invented downstream.
  qCWarning(lcScrollManager).nospace()
      << "SELREQ ScrollManager::updateSelectionForIndex=" << selectedIndex;
  if (m_selectionDisplay) {
    m_selectionDisplay->updateSelectionForIndex(selectedIndex);
  }
  // keep the cover-flow carousel in sync with the canonical selection.
  m_coverFlow->onSelectionChanged(selectedIndex);
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
