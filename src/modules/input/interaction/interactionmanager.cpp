// Orchestrates user interactions, delegating to specialized managers for input
// handling.
#include "interactionmanager.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

// Include full headers for forward-declared owned managers
#include "alphabeticnavigationhandler.h"
#include "animationmanager.h"
#include "arrownavigationhandler.h"
#include "attractmanager.h"
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "itemmetadataactioncontroller.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "mousemanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "collection/hierarchyhelpers.h"
#include "collection/validationhelpers.h"
#include "collectiontypes.h"
#include "databasemanager.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "idetailspane.h"
#include "itemwidget.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/timing.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcInteractionManager, "kartend.interactionmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcInteractionManager().isDebugEnabled()) {                                                 \
      qCDebug(lcInteractionManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

InteractionManager::InteractionManager(QObject *parent) : QObject(parent) {
  // Sub-managers owned solely by their std::unique_ptr members (Kartend-d70s,
  // re-attempted after Kartend-3v92).
  m_searchManager = std::make_unique<SearchManager>(nullptr);
  m_selectionManager = std::make_unique<SelectionManager>(nullptr);
  m_keyboardManager = std::make_unique<KeyboardManager>(nullptr);
  m_gamepadManager = std::make_unique<GamepadManager>(nullptr);
  m_arrowHandler = std::make_unique<ArrowNavigationHandler>(nullptr);
  m_alphabeticHandler = std::make_unique<AlphabeticNavigationHandler>(nullptr);
  m_animationManager = std::make_unique<AnimationManager>(nullptr);
  m_mouseManager = std::make_unique<MouseManager>(nullptr);
  m_launchManager = std::make_unique<LaunchManager>(nullptr);
  m_viewportManager = std::make_unique<ViewportManager>(nullptr);
  m_eventManager = std::make_unique<EventManager>(nullptr);
  m_attractManager = std::make_unique<AttractManager>(nullptr);

  m_viewportManager->setContinuousScrollActive(true);
}

// Destructor: stop timers/animations and clear selection
//
// DESTRUCTION-ORDER ANCHOR (Kartend-wxtx6 / Kartend-gutqx). The safety of
// this teardown depends on TWO orderings that are encoded in separate files
// and are NOT enforced by the compiler — keep them in sync:
//   1. (local) The detach-then-destroy sequence below relies on the owned
//      sub-managers being destroyed AFTER this body runs, which is implicit
//      member-declaration order in interactionmanager.h (m_eventManager and
//      the other unique_ptr members are destroyed in reverse declaration
//      order once this destructor returns). Reordering those members can
//      move m_eventManager's destruction relative to the filter detach.
//   2. (cross-file) ApplicationManager::destroyManagersAndClearContextSlots()
//      (applicationmanager.cpp) nulls each ctx->managers.* slot and resets
//      m_interactionManager.reset() in a reverse-of-declaration order that
//      mirrors this. A member reorder in EITHER file silently breaks the
//      other with no compile-time or test-time signal.
// If you reorder members here or in applicationmanager.h, re-read both
// sites and the Kartend-gutqx UBSan/vptr history before shipping.
InteractionManager::~InteractionManager() {
  // Mark teardown FIRST (Kartend-gutqx) so eventFilter() and slots like
  // onKeyboardStopRepeat short-circuit from this point on — sub-manager
  // destruction emits late stopRepeat / cleanup signals (e.g.
  // ~GamepadManager → KeyboardManager::stopRepeat fires after the arrow
  // handler unique_ptr has already been freed by member destruction), and
  // nested event processing during teardown must not re-enter
  // m_eventManager->filterEvent() on a partially-destroyed object.
  //
  // LOAD-BEARING INVARIANT: m_destroying MUST be set before any event-filter
  // detach or sub-manager teardown below. Do not move it later.
  m_destroying = true;

  // Detach the application-wide event filter installed in
  // installEventFilters() before any owned sub-manager (notably
  // m_eventManager, which the filter delegates to) starts being
  // destroyed. Without this, Qt can still deliver events to
  // eventFilter() while ~EventManager runs, producing a UBSan
  // vptr violation when m_eventManager->filterEvent() is invoked
  // on a partially-destroyed object.
  if (qApp) {
    qApp->removeEventFilter(this);
  }
  // Kartend-gutqx: installEventFilters() also installed `this` on four
  // widgets; the qApp removal above does not cover them, and events
  // delivered through those installations during member teardown were the
  // same UB class the qApp removal fixed — through a different door. The
  // members are QPointer-guarded, so removal is safe even if MainWindow
  // already tore the widgets down.
  if (m_itemsPage) {
    m_itemsPage->removeEventFilter(this);
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->removeEventFilter(this);
    if (QWidget *viewport = m_itemScrollArea->viewport()) {
      viewport->removeEventFilter(this);
    }
  }
  if (m_gridContainer) {
    m_gridContainer->removeEventFilter(this);
  }

  stopRepeat();
  clearSelection();
}

// Wires references, installs event filters, and initializes search UI
auto InteractionManager::resolveDoubleClickIndexCandidate() const -> int {
  int idx = currentSelectedIndex();
  if (idx < 0 && scrollMgr()) {
    const auto &active = scrollMgr()->getActiveWidgets();
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
      if (it.value() && it.value()->isSelected()) {
        return it.key();
      }
    }
  }
  return idx;
}

// Helper: derive file path for a given visual index via ScrollManager
auto InteractionManager::derivePathFromIndex(int idx) const -> QString {
  if (scrollMgr() && idx >= 0) {
    return scrollMgr()->filePathForVisualIndex(idx);
  }
  return {};
}

// Helper: resolve owning collection index for a file path
auto InteractionManager::resolveOwnerForPath(const QString &path) const -> int {
  if (path.isEmpty()) {
    return -1;
  }
  if (databaseMgr()) {
    return databaseMgr()->getCollectionIndexForFile(path);
  }
  if (m_currentCollectionIndex) {
    return *m_currentCollectionIndex;
  }
  return -1;
}

// Helper: fallback collection index based on current selection or view
auto InteractionManager::getFallbackCollectionIndex() const -> int {
  QString selectedPath = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (!selectedPath.isEmpty()) {
    if (databaseMgr()) {
      int owner = databaseMgr()->getCollectionIndexForFile(selectedPath);
      if (owner >= 0) {
        return owner;
      }
    }
  }
  if (m_currentCollectionIndex) {
    return *m_currentCollectionIndex;
  }
  return -1;
}

// Launches on double‑click without altering or interrupting any scroll state
void InteractionManager::handleWidgetDoubleClickedWithCollection(const QString &filePath,
                                                                 int collectionIndex) {
  // Launch debounce now lives in launchItemWithCollection (Kartend-l06g6),
  // which every launch surface funnels through — no per-surface check here.

  // Reset click state on double-click launch
  m_state.click().rowChangeFirstClickIndex = -1;
  m_state.click().rowChangeFirstClickMs = 0;
  m_state.click().deferCenterOnClick = false;
  m_state.click().deferredCenterIndex = -1;

  QString path = filePath;
  int collIdx = collectionIndex;

  if (path.isEmpty()) {
    const int idx = resolveDoubleClickIndexCandidate();
    const QString derived = derivePathFromIndex(idx);
    if (!derived.isEmpty()) {
      path = derived;
    }
  }

  if (collIdx < 0) {
    collIdx = resolveOwnerForPath(path);
  }

  if (!path.isEmpty() && collIdx >= 0) {
    // Expand-mode: first double-click expands the artwork preview overlay
    // instead of launching; a second double-click on the same selection
    // (no selection change in between) falls through to launch.
    const int activationIdx = currentSelectedIndex();
    if (maybeExpandInsteadOfLaunch(path, collIdx, activationIdx)) {
      return;
    }
    launchItemWithCollection(path, collIdx);
    return;
  }
  const int fallbackIdx = getFallbackCollectionIndex();
  QString selectedPath = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (fallbackIdx >= 0 && !selectedPath.isEmpty()) {
    const int activationIdx = currentSelectedIndex();
    if (maybeExpandInsteadOfLaunch(selectedPath, fallbackIdx, activationIdx)) {
      return;
    }
    launchItemWithCollection(selectedPath, fallbackIdx);
  }
}

// Global event filter handling input, mouse/scroll, selection, and viewport
// scrolling
auto InteractionManager::eventFilter(QObject *obj, QEvent *event) -> bool {
  // m_destroying: nested event processing during ~InteractionManager must
  // not delegate into m_eventManager — reading a unique_ptr member is UB
  // once its own destruction has begun (Kartend-gutqx).
  if (QApplication::closingDown() || m_destroying || !event) {
    return QObject::eventFilter(obj, event);
  }

  // Delegate event filtering to EventManager
  if (m_eventManager) {
    bool handled = m_eventManager->filterEvent(obj, event);
    if (handled) {
      return true;
    }
  }

  return QObject::eventFilter(obj, event);
}

// Updates the selected file path and safely refreshes sidebar metadata without
// using stale widget pointers
void InteractionManager::updateFilePathForSelection(int index, const QList<int> &subcollections) {
  if (m_selectionManager) {
    m_selectionManager->updateFilePathForSelection(index, subcollections);
  }
}

void InteractionManager::clearSelection() {
  if (m_selectionManager) {
    m_selectionManager->clearSelection(m_isShuttingDown);
  }
}

auto InteractionManager::currentSelectedIndex() const -> int {
  return m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
}

auto InteractionManager::getSelectedMediaItem() const -> ItemWidget * {
  return m_selectionManager ? m_selectionManager->selectedWidget() : nullptr;
}

void InteractionManager::setSelectedMediaItem(ItemWidget *widget) {
  if (m_selectionManager) {
    m_selectionManager->setSelectedWidget(widget);
  }
}

auto InteractionManager::selectedFilePath() const -> QString {
  return m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
}

auto InteractionManager::isRestoringSelection() const -> bool {
  return m_selectionManager ? m_selectionManager->isRestoringSelection() : false;
}

auto InteractionManager::targetRestoreIndex() const -> int {
  return m_selectionManager ? m_selectionManager->targetRestoreIndex() : -1;
}

auto InteractionManager::forceImmediateCenter() const -> bool {
  if (m_selectionManager) {
    return m_selectionManager->forceImmediateCenter();
  }
  if (m_viewportManager) {
    return m_viewportManager->forceImmediateCenter();
  }
  return false;
}

// Returns the active grid width; prefers ScrollManager's current context to
// handle nested/filtered views
auto InteractionManager::getCurrentGridWidth() const -> int {
  if (scrollMgr()) {
    int currentWidth = scrollMgr()->getCurrentGridWidth();
    if (currentWidth > 0) {
      return currentWidth;
    }
  }
  return CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
}

void InteractionManager::applyMinorHorizontalSuppress() {
  constexpr qint64 kMinorHorizSuppressMs = 220;
  constexpr int kMinorHorizSuppressClearMs = 240;
  m_state.suppressArrowCenterFor(kMinorHorizSuppressMs);
  // Clear arrow center suppression slightly after the suppression window
  // expires - ensures horizontal navigation completes before vertical centering
  // resumes
  QTimer::singleShot(kMinorHorizSuppressClearMs, this,
                     [this]() { m_state.clearArrowCenterSuppression(); });
}

void InteractionManager::setPendingSelectionIfNeeded(bool condition, int newSelection) {
  if (condition) {
    m_state.beginSelectionSuppression(newSelection);
  }
}

void InteractionManager::updateSelectionStateAfterMove(int newSelection) {
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(newSelection, subs);
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(newSelection);
  }
  selectItemByIndex(newSelection, true);
}

void InteractionManager::centerItemVertically(int index, bool immediate) {
  if (m_viewportManager) {
    m_viewportManager->centerItemVertically(index, immediate);
  }
}

void InteractionManager::recenterCurrentSelection() {
  // Clear user scroll state to ensure centering isn't blocked
  m_state.scroll().userScrollActive = false;
  m_state.scroll().userFreeScroll = false;

  int selectedIndex = currentSelectedIndex();
  if (selectedIndex >= 0) {
    centerItemVertically(selectedIndex, true);
  }
}

void InteractionManager::ensureHorizontallyVisible(int index) {
  if (m_viewportManager) {
    m_viewportManager->ensureHorizontallyVisible(index);
  }
}

void InteractionManager::ensureItemVisible(int index, bool allowHorizontalScroll) {
  if (m_viewportManager) {
    m_viewportManager->ensureItemVisible(index, allowHorizontalScroll);
  }
}

void InteractionManager::applyImmediateViewportPositioningForSelection(int targetIndex) {
  if (m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(targetIndex);
  }
}

void InteractionManager::selectItemByIndex(int index, bool allowHorizontalScroll) {
  Q_UNUSED(allowHorizontalScroll);
  if (!scrollMgr() || !m_itemScrollArea ||
      !CollectionUtils::isInteractiveViewIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }

  const QStringList &filePaths = scrollMgr()->getFilePaths();
  QList<int> subcollections = getSubcollections(*m_currentCollectionIndex);
  int totalItems = subcollections.size() + filePaths.size();
  if (index < 0 || index >= totalItems) {
    return;
  }

  bool selectionChangedLocal = (index != currentSelectedIndex());
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(index);
  }
  if (selectionChangedLocal) {
    m_state.scroll().userFreeScroll = false;
  }

  ItemWidget *widget = m_selectionManager ? m_selectionManager->widgetForIndex(index) : nullptr;
  bool suppressed =
      m_state.click().selectionSuppressed && m_state.click().pendingSelectionIndex == index;
  bool skipCenter = m_state.click().suppressInitialClickCenter;

  if (widget) {
    if (m_selectionManager) {
      m_selectionManager->setSelectedWidget(widget);
    }
    updateFilePathForSelection(index, subcollections);
    if (!suppressed) {
      handleSuccessfulSelection(index);
    }
  } else {
    bool isCoverFlow = false;
    if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
      isCoverFlow = ((*m_collections)[*m_currentCollectionIndex].viewType == ViewType::CoverFlow);
    }
    if (isCoverFlow) {
      // Cover Flow renders CoverFlowCards instead of ItemWidgets, so the
      // virtual-grid retry loop would never resolve a widget here. Commit
      // the selection state directly so file path / metadata context stay
      // current for the info page and other consumers.
      updateFilePathForSelection(index, subcollections);
      if (!suppressed) {
        handleSuccessfulSelection(index);
      }
    } else {
      trySelectWidget(index, subcollections, 0);
    }
  }

  const int selected = currentSelectedIndex();
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(selected);
    if (m_state.scroll().clickHoldAdvancing) {
      scrollMgr()->refreshSelectionOverlayState();
    }
  }
  emit selectionChanged(selected);

  if (suppressed) {
    persistSuppressedSelectionAndMaybeCenter(index, subcollections, skipCenter);
  }

  if (skipCenter) {
    m_state.click().suppressInitialClickCenter = false;
  }
}

void InteractionManager::persistSuppressedSelectionAndMaybeCenter(int index,
                                                                  const QList<int> &subcollections,
                                                                  bool skipCenter) {
  bool deferCenter =
      m_state.click().deferCenterOnClick && m_state.click().deferredCenterIndex == index;
  if (!deferCenter && !skipCenter) {
    centerItemVertically(index, false);
  }
  int curColl = ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if (m_collections && curColl >= 0 && curColl < m_collections->size() && m_selectionManager) {
    QString title = m_selectionManager->titleForIndex(index, subcollections);
    m_selectionManager->persistSelection(curColl, index, title);
  }
  // Defer artwork update to allow selection animation to start smoothly
  // before triggering potentially expensive artwork loading operations
  QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && artworkMgr()) {
      artworkMgr()->updateViewportArtwork();
    }
  });
}

// Returns the direct child subcollection indices for a parent collection
auto InteractionManager::getSubcollections(int parentIndex) const -> QList<int> {
  // Delegate to SelectionManager which owns the canonical implementation
  if (m_selectionManager) {
    return m_selectionManager->getSubcollections(parentIndex);
  }
  // Fallback to O(n) scan
  if (!m_collections) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}

void InteractionManager::clearSelectionAndFocus() {
  clearSelection();
  if (m_itemsPage) {
    m_itemsPage->setFocus();
  }
}

void InteractionManager::trySelectWidget(int index, const QList<int> &subcollections, int attempt) {
  if ((!scrollMgr()) || currentSelectedIndex() != index || QApplication::closingDown()) {
    return;
  }
  constexpr int kMaxSelectAttempts = 10;
  if (attempt > kMaxSelectAttempts) {
    if (m_selectionManager) {
      m_selectionManager->setRestoringSelection(false);
      m_selectionManager->setTargetRestoreIndex(-1);
    }
    return;
  }

  ItemWidget *widget = m_selectionManager ? m_selectionManager->widgetForIndex(index) : nullptr;

  if (widget) {
    if (m_selectionManager) {
      m_selectionManager->setSelectedWidget(widget);
      m_selectionManager->applyWidgetSelection(widget);
    }
    updateFilePathForSelection(index, subcollections);
    handleSuccessfulSelection(index);
  } else {
    scrollMgr()->updateVirtualView();
    constexpr int kSelectRetryBaseMs = 30;
    constexpr int kSelectRetryStepMs = 30;
    int delay = kSelectRetryBaseMs + (attempt * kSelectRetryStepMs);
    // Retry widget selection with increasing delays - widget may not be
    // materialized yet during virtual scroll population
    QTimer::singleShot(delay, this, [this, index, subcollections, attempt]() {
      if (!QApplication::closingDown()) {
        trySelectWidget(index, subcollections, attempt + 1);
      }
    });
  }
}

// Cycles search mode regardless of search text; only updates results when there
// is search text
