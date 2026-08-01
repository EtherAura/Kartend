// Manages viewport positioning, item centering, and scroll-to-visible
// operations.
#include "viewportmanager.h"
#include "animationmanager.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/validationhelpers.h"
#include "gridlayoutcalculator.h"
#include "iartworkmanager.h"
#include "igridlayoutscroll.h"
#include "interactionstateholder.h"
#include "iscrollmanager.h"
#include "iselectionmanager.h"
#include "iselectionoverlayscroll.h"

#include "gridutils.h"
#include "uiconstants/timing.h"

#include <QApplication>
#include <QDateTime>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcViewportManager, "kartend.viewportmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcViewportManager().isDebugEnabled()) {                                                    \
      qCDebug(lcViewportManager) << msg;                                                           \
    }                                                                                              \
  } while (0)

// ViewportManagerSetup getter definitions (non-manager fields only).
SETUP_GETTER_DEF_UI_SAME(ViewportManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_COL_SAME(ViewportManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_COL_SAME(ViewportManagerSetup, const int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_COL_SAME(ViewportManagerSetup, const bool *, IsShuttingDown, isShuttingDown)
SETUP_GETTER_DEF_COL_SAME(ViewportManagerSetup, const GeneralSettings *, GeneralSettings,
                          generalSettings)

ViewportManager::ViewportManager(QObject *parent) : QObject(parent) {}

ViewportManager::~ViewportManager() = default;

bool ViewportManager::shuttingDown() const {
  return QApplication::closingDown() || (m_isShuttingDown && *m_isShuttingDown);
}

void ViewportManager::setupReferences(const ViewportManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_generalSettings = setup.getGeneralSettings();
  m_itemScrollArea = setup.getItemScrollArea();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_isShuttingDown = setup.getIsShuttingDown();

  // Connect to AnimationManager's finished signal. animMgr() yields the
  // IAnimationManager role interface (no signals); the verticalAnimationFinished
  // signal lives on the concrete class. The ctx-registered object is always an
  // AnimationManager, so static_cast back to the concrete type for the connect.
  if (auto *animIface = animMgr()) {
    auto *anim = static_cast<AnimationManager *>(animIface);
    connect(anim, &AnimationManager::verticalAnimationFinished, this,
            &ViewportManager::onVScrollAnimationFinished);
  }
}

// Delegate restore state to SelectionManager (single source of truth)
void ViewportManager::setRestoringSelection(bool restoring) {
  if (selectionMgr()) {
    selectionMgr()->setRestoringSelection(restoring);
  }
}

bool ViewportManager::isRestoringSelection() const {
  return selectionMgr() ? selectionMgr()->isRestoringSelection() : false;
}

void ViewportManager::setTargetRestoreIndex(int index) {
  if (selectionMgr()) {
    selectionMgr()->setTargetRestoreIndex(index);
  }
}

int ViewportManager::targetRestoreIndex() const {
  return selectionMgr() ? selectionMgr()->targetRestoreIndex() : -1;
}

double ViewportManager::getScrollScale() const {
  if (scrollGrid()) {
    return scrollGrid()->getMetrics().scrollScale;
  }
  return 1.0;
}

int ViewportManager::toWidgetScrollY(int logicalScrollY) const {
  if (!scrollGrid() || !m_itemScrollArea) {
    return logicalScrollY;
  }
  const auto &metrics = scrollGrid()->getMetrics();
  int viewportHeight = m_itemScrollArea->viewport()->height();
  return metrics.toWidgetScrollY(logicalScrollY, viewportHeight);
}

int ViewportManager::getCurrentGridWidth() const {
  // Prefer ScrollManager's value for filtered/nested views
  if (scrollGrid()) {
    int width = scrollGrid()->getCurrentGridWidth();
    if (width > 0) {
      return width;
    }
  }
  return CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
}

int ViewportManager::computeVerticalCenterDuration(int distance, bool repeatActive) const {
  int itemHeight = 0;
  int vSpacing = 0;
  if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    itemHeight = (*m_collections)[*m_currentCollectionIndex].gridLayout.itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].gridLayout.verticalSpacing;
  }
  int speedLevel = m_generalSettings ? m_generalSettings->input.scrollAnimationDurationMs : 1500;
  return AnimationManager::computeVerticalCenterDuration(distance, itemHeight, vSpacing,
                                                         repeatActive, speedLevel);
}

void ViewportManager::setProgrammaticScrollGuarded(bool enable) {
  if (!state()) {
    return;
  }
  if (enable) {
    state()->scroll().programmaticScroll = true;
    if (scrollOverlay()) {
      scrollOverlay()->refreshSelectionOverlayState();
    }
  } else {
    QPointer<IScrollManager> scrollMgrPtr = m_ctx ? m_ctx->scrollManager() : nullptr;
    QPointer<InteractionStateHolder> statePtr = state();
    // Defer clearing ProgrammaticScroll flag until after Qt processes pending
    // scroll events - prevents selection overlay flicker during programmatic
    // scrolls. Use QPointer for both captures so the lambda is lifetime-safe
    // if either object is destroyed before the timer fires.
    QTimer::singleShot(0, this, [statePtr, scrollMgrPtr]() {
      if (statePtr) {
        statePtr->scroll().programmaticScroll = false;
      }
      if (scrollMgrPtr) {
        scrollMgrPtr->refreshSelectionOverlayState();
      }
    });
  }
}

void ViewportManager::setScrollValueAndUpdateSelection(QScrollBar *verticalScrollBar, int targetY,
                                                       int index) {
  verticalScrollBar->setValue(targetY);
  if (scrollGrid()) {
    // When wrapping, clear all widgets to prevent stale artwork from showing
    // at wrong positions after the large scroll jump
    if (m_isWrappingNavigation) {
      scrollGrid()->cleanupActiveWidgets();
    }
    scrollGrid()->updateVirtualView();
    int idxDyn =
        (state() && state()->isSelectionSuppressed()) ? state()->pendingSelectionIndex() : index;
    if (idxDyn >= 0) {
      scrollOverlay()->updateSelectionForIndex(idxDyn);
    }
  }
}

void ViewportManager::clearArtworkSuppressionViewportUpdateIfNeeded() {
  if (state() && state()->artwork().suppressArtwork && !m_repeating) {
    state()->artwork().suppressArtwork = false;
    state()->artwork().allowDuringSelection = true;
    // Defer artwork update to allow selection animation to complete smoothly
    QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
      if (!QApplication::closingDown() && artworkMgr()) {
        artworkMgr()->updateViewportArtwork();
      }
    });
  }
}

void ViewportManager::clearArrowCenterSuppressionWhenDue() {
  if (!state()) {
    return;
  }
  qint64 until = state()->arrow().suppressArrowCenterUntilMs;
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (until > now) {
    qint64 delay = until - now;
    // QPointer (matching the sibling guard in setProgrammaticScrollGuarded) so
    // the deferred lambda is lifetime-safe even if the member-declaration order
    // in interactionmanager.h ever changes (Kartend audit L-03).
    QPointer<InteractionStateHolder> statePtr = state();
    constexpr qint64 kMaxArrowCenterSuppressClearMs = 1000;
    // Clear arrow center suppression after calculated delay expires -
    // caps at max to prevent indefinite suppression on clock issues
    QTimer::singleShot(static_cast<int>(qMin<qint64>(delay, kMaxArrowCenterSuppressClearMs)), this,
                       [statePtr]() {
                         if (statePtr) {
                           statePtr->arrow().suppressArrowCenter = false;
                         }
                       });
  } else {
    state()->arrow().suppressArrowCenter = false;
  }
}

void ViewportManager::finalizeImmediateCenteringState(int index, int currentRow) {
  // Check and finalize restore via SelectionManager (single source of truth)
  if (selectionMgr()) {
    selectionMgr()->checkAndFinalizeRestore(index);
  }
  m_isWrappingNavigation = false;
  m_forceImmediateCenter = false;
  if (m_wrapSequenceActive) {
    m_wrapSequenceActive = false;
    m_continuousScrollActive = true;
  }
  if (state() && state()->scroll().clickScroll) {
    state()->scroll().clickScroll = false;
  }
  if (!m_repeating && !m_physicalKeyDown && state() && !state()->scroll().clickContinuous &&
      !state()->scroll().keyContinuous) {
    m_continuousScrollActive = false;
  }
  m_instantPositioning = false;
  m_lastSelectedRow = currentRow;
  if (selectionMgr()) {
    selectionMgr()->setLastSelectedRow(currentRow);
  }
}

void ViewportManager::onVScrollAnimationFinished() {
  if (state()) {
    state()->click().clickForceAnim = false;
  }
  if (scrollGrid()) {
    scrollGrid()->updateVirtualView();
    int idxDyn =
        (state() && state()->isSelectionSuppressed()) ? state()->pendingSelectionIndex() : -1;
    // Signal that we need a selection update (InteractionManager will handle)
    emit requestSelectionUpdate(idxDyn);
  }
  if (!m_repeating && !m_physicalKeyDown && state() && !state()->scroll().clickContinuous &&
      !state()->scroll().keyContinuous) {
    m_continuousScrollActive = false;
  }
  if (state()) {
    state()->scroll().programmaticScroll = false;
    if (scrollOverlay()) {
      scrollOverlay()->refreshSelectionOverlayState();
    }
  }
  if (state() && !m_repeating && !m_physicalKeyDown) {
    state()->artwork().suppressArtwork = false;
    state()->artwork().allowDuringSelection = true;
    // Defer artwork update to allow selection animation to complete smoothly
    QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
      if (!QApplication::closingDown() && artworkMgr()) {
        artworkMgr()->updateViewportArtwork();
      }
    });
  }
  if (state()) {
    state()->scroll().clickScroll = false;
  }
  m_instantPositioning = false;
  int gridWidthLocal = getCurrentGridWidth();
  int idxDyn =
      (state() && state()->isSelectionSuppressed()) ? state()->pendingSelectionIndex() : -1;
  if (gridWidthLocal > 0 && idxDyn >= 0) {
    m_lastSelectedRow = idxDyn / gridWidthLocal;
    if (selectionMgr()) {
      selectionMgr()->setLastSelectedRow(m_lastSelectedRow);
    }
  }
}
