// Manages viewport positioning, item centering, and scroll-to-visible
// operations.
#include "viewportmanager.h"
#include "animationmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include "interactionstateholder.h"
#include "scrollmanager.h"
#include "selectionmanager.h"

#include "gridutils.h"
#include "uiconstants.h"

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

// ViewportManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, ScrollManager *, ScrollManager, scrollManager)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, SelectionManager *, SelectionManager, selectionManager)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, AnimationManager *, AnimationManager, animationManager)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, ArtworkManager *, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, InteractionStateHolder *, InteractionState,
                      interactionState)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, int *, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, const bool *, IsShuttingDown, isShuttingDown)
SETUP_GETTER_DEF_SAME(ViewportManagerSetup, const GeneralSettings *, GeneralSettings,
                      generalSettings)

ViewportManager::ViewportManager(QObject *parent) : QObject(parent) {}

ViewportManager::~ViewportManager() = default;

void ViewportManager::setupReferences(const ViewportManagerSetup &setup) {
  m_generalSettings = setup.getGeneralSettings();
  m_itemScrollArea = setup.getItemScrollArea();
  m_scrollManager = setup.getScrollManager();
  m_selectionManager = setup.getSelectionManager();
  m_animationManager = setup.getAnimationManager();
  m_artworkManager = setup.getArtworkManager();
  m_state = setup.getInteractionState();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_isShuttingDown = setup.getIsShuttingDown();

  // Connect to AnimationManager's finished signal
  if (m_animationManager) {
    connect(m_animationManager, &AnimationManager::verticalAnimationFinished, this,
            &ViewportManager::onVScrollAnimationFinished);
  }
}

// Delegate restore state to SelectionManager (single source of truth)
void ViewportManager::setRestoringSelection(bool restoring) {
  if (m_selectionManager) {
    m_selectionManager->setRestoringSelection(restoring);
  }
}

bool ViewportManager::isRestoringSelection() const {
  return m_selectionManager ? m_selectionManager->isRestoringSelection() : false;
}

void ViewportManager::setTargetRestoreIndex(int index) {
  if (m_selectionManager) {
    m_selectionManager->setTargetRestoreIndex(index);
  }
}

int ViewportManager::targetRestoreIndex() const {
  return m_selectionManager ? m_selectionManager->targetRestoreIndex() : -1;
}

double ViewportManager::getScrollScale() const {
  if (m_scrollManager) {
    return m_scrollManager->getMetrics().scrollScale;
  }
  return 1.0;
}

int ViewportManager::toWidgetScrollY(int logicalScrollY) const {
  if (!m_scrollManager || !m_itemScrollArea) {
    return logicalScrollY;
  }
  const auto &metrics = m_scrollManager->getMetrics();
  int viewportHeight = m_itemScrollArea->viewport()->height();
  return metrics.toWidgetScrollY(logicalScrollY, viewportHeight);
}

int ViewportManager::getCurrentGridWidth() const {
  // Prefer ScrollManager's value for filtered/nested views
  if (m_scrollManager) {
    int width = m_scrollManager->getCurrentGridWidth();
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
    itemHeight = (*m_collections)[*m_currentCollectionIndex].itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].verticalSpacing;
  }
  int speedLevel = m_generalSettings ? m_generalSettings->scrollAnimationDurationMs : 1500;
  return AnimationManager::computeVerticalCenterDuration(distance, itemHeight, vSpacing,
                                                         repeatActive, speedLevel);
}

void ViewportManager::setProgrammaticScrollGuarded(bool enable) {
  if (!m_state) {
    return;
  }
  if (enable) {
    m_state->scroll().programmaticScroll = true;
    if (m_scrollManager) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  } else {
    QPointer<ScrollManager> scrollMgrPtr = m_scrollManager;
    QPointer<InteractionStateHolder> statePtr = m_state;
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
  if (m_scrollManager) {
    // When wrapping, clear all widgets to prevent stale artwork from showing
    // at wrong positions after the large scroll jump
    if (m_isWrappingNavigation) {
      m_scrollManager->cleanupActiveWidgets();
    }
    m_scrollManager->updateVirtualView();
    int idxDyn =
        (m_state && m_state->isSelectionSuppressed()) ? m_state->pendingSelectionIndex() : index;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
}

void ViewportManager::clearArtworkSuppressionViewportUpdateIfNeeded() {
  if (m_state && m_state->artwork().suppressArtwork && !m_repeating) {
    m_state->artwork().suppressArtwork = false;
    m_state->artwork().allowDuringSelection = true;
    // Defer artwork update to allow selection animation to complete smoothly
    QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
      if (!QApplication::closingDown() && m_artworkManager) {
        m_artworkManager->updateViewportArtwork();
      }
    });
  }
}

void ViewportManager::clearArrowCenterSuppressionWhenDue() {
  if (!m_state) {
    return;
  }
  qint64 until = m_state->arrow().suppressArrowCenterUntilMs;
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (until > now) {
    qint64 delay = until - now;
    InteractionStateHolder *statePtr = m_state;
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
    m_state->arrow().suppressArrowCenter = false;
  }
}

void ViewportManager::finalizeImmediateCenteringState(int index, int currentRow) {
  // Check and finalize restore via SelectionManager (single source of truth)
  if (m_selectionManager) {
    m_selectionManager->checkAndFinalizeRestore(index);
  }
  m_isWrappingNavigation = false;
  m_forceImmediateCenter = false;
  if (m_wrapSequenceActive) {
    m_wrapSequenceActive = false;
    m_continuousScrollActive = true;
  }
  if (m_state && m_state->scroll().clickScroll) {
    m_state->scroll().clickScroll = false;
  }
  if (!m_repeating && !m_physicalKeyDown && m_state && !m_state->scroll().clickContinuous &&
      !m_state->scroll().keyContinuous) {
    m_continuousScrollActive = false;
  }
  m_instantPositioning = false;
  m_lastSelectedRow = currentRow;
  if (m_selectionManager) {
    m_selectionManager->setLastSelectedRow(currentRow);
  }
}

void ViewportManager::onVScrollAnimationFinished() {
  if (m_state) {
    m_state->click().clickForceAnim = false;
  }
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
    int idxDyn =
        (m_state && m_state->isSelectionSuppressed()) ? m_state->pendingSelectionIndex() : -1;
    // Signal that we need a selection update (InteractionManager will handle)
    emit requestSelectionUpdate(idxDyn);
  }
  if (!m_repeating && !m_physicalKeyDown && m_state && !m_state->scroll().clickContinuous &&
      !m_state->scroll().keyContinuous) {
    m_continuousScrollActive = false;
  }
  if (m_state) {
    m_state->scroll().programmaticScroll = false;
    if (m_scrollManager) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  if (m_state && !m_repeating && !m_physicalKeyDown) {
    m_state->artwork().suppressArtwork = false;
    m_state->artwork().allowDuringSelection = true;
    // Defer artwork update to allow selection animation to complete smoothly
    QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
      if (!QApplication::closingDown() && m_artworkManager) {
        m_artworkManager->updateViewportArtwork();
      }
    });
  }
  if (m_state) {
    m_state->scroll().clickScroll = false;
  }
  m_instantPositioning = false;
  int gridWidthLocal = getCurrentGridWidth();
  int idxDyn =
      (m_state && m_state->isSelectionSuppressed()) ? m_state->pendingSelectionIndex() : -1;
  if (gridWidthLocal > 0 && idxDyn >= 0) {
    m_lastSelectedRow = idxDyn / gridWidthLocal;
    if (m_selectionManager) {
      m_selectionManager->setLastSelectedRow(m_lastSelectedRow);
    }
  }
}
