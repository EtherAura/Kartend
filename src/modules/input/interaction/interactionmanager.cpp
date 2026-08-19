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

#include "artworkpreviewoverlay.h"
#include "collection/hierarchyhelpers.h"
#include "collection/validationhelpers.h"
#include "collectiontypes.h"
#include "databasemanager.h"
#include "focussectionoverlay.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "iartworkpreviewscroll.h"
#include "idetailspane.h"
#include "interactionhelpers.h"
#include "itemwidget.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "selectionindicator.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/timing.h"
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QToolButton>

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

  // Attract/auto-advance must not run behind a fullscreen artwork, and the
  // artwork boundary hand-off has to be connected however expand mode was
  // opened (user requests 2026-08-18). Observing the overlay's own
  // show/hide HERE — rather than connecting MainWindow to two visibility
  // signals — keeps this inside the object that owns both the attract
  // manager and the hook, so nothing can be delivered to a half-destroyed
  // window during teardown (a test caught exactly that).
  if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
    if (auto *widget = qobject_cast<QWidget *>(obj);
        widget && widget->objectName() == QLatin1String("artworkPreviewOverlay")) {
      const bool visible = event->type() == QEvent::Show;
      if (m_attractManager) {
        // Resuming reseeds the idle countdown, so dismissing the artwork
        // gives a fresh full timeout rather than an instant advance.
        m_attractManager->setSuspended(visible);
      }
      if (visible) {
        (void)visibleArtworkOverlay(); // connects the boundary hand-off
      }
    }
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

bool InteractionManager::shuttingDown() const {
  return QApplication::closingDown() || (m_isShuttingDown && *m_isShuttingDown);
}

void InteractionManager::clearSelection() {
  if (m_selectionManager) {
    // shuttingDown() — never the raw pointer, which is non-null for the
    // manager's whole life and would convert to a permanently-true bool
    // (the original stopRepeat bug, Kartend-kalh1).
    m_selectionManager->clearSelection(shuttingDown());
  }
}

auto InteractionManager::currentSelectedIndex() const -> int {
  return m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
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

InteractionManager::FocusSectionInfo InteractionManager::currentFocusSection() const {
  FocusSectionInfo info;
  if (!m_ctx) {
    return info;
  }
  QWidget *fw = QApplication::focusWidget();
  QWidget *paneW = m_ctx->ui.sidebar ? m_ctx->ui.sidebar->asWidget() : nullptr;
  const auto within = [fw](QWidget *w) { return w && fw && (w == fw || w->isAncestorOf(fw)); };
  if (within(m_ctx->ui.collectionTreeWidget)) {
    info.widget = m_ctx->ui.collectionTreeWidget;
    info.label = tr("Collections");
    info.kind = FocusSection::Tree;
  } else if (within(paneW)) {
    info.widget = paneW;
    info.label = tr("Details");
    info.kind = FocusSection::Pane;
  } else if (within(m_ctx->ui.itemsTopBar)) {
    info.widget = m_ctx->ui.itemsTopBar;
    info.label = tr("Toolbar");
    info.kind = FocusSection::Toolbar;
  } else {
    info.widget = m_ctx->ui.itemScrollArea;
    info.label = tr("Library");
    info.kind = FocusSection::Grid;
  }
  return info;
}

void InteractionManager::moveFocusSection(int dx, int dy) {
  if (!m_ctx) {
    return;
  }
  // Spatial moves between the grid, the top bar, and the two sidebars,
  // from wherever focus is now (user request 2026-08-17).
  QWidget *treeW = m_ctx->ui.collectionTreeWidget;
  QWidget *paneW = m_ctx->ui.sidebar ? m_ctx->ui.sidebar->asWidget() : nullptr;
  QWidget *toolbarW = m_ctx->ui.itemsTopBar;
  QWidget *gridW = m_ctx->ui.itemScrollArea;
  QWidget *const current = currentFocusSection().widget;

  const auto usable = [](QWidget *w) { return w && w->isVisible(); };
  QWidget *target = current;
  if (dy < 0 && current != toolbarW && usable(toolbarW)) {
    target = toolbarW;
  } else if (dy > 0 && current == toolbarW) {
    target = gridW;
  } else if (dx < 0) {
    target = current == paneW ? gridW : (current != treeW && usable(treeW) ? treeW : current);
  } else if (dx > 0) {
    target = current == treeW ? gridW : (current != paneW && usable(paneW) ? paneW : current);
  }
  if (!target || target == current) {
    return;
  }
  // Containers (pane, top bar) hand focus to their first visible focusable
  // child; setFocus works programmatically regardless of policy otherwise.
  QWidget *focusTarget = target;
  if (target->focusPolicy() == Qt::NoFocus) {
    const auto children = target->findChildren<QWidget *>();
    for (QWidget *child : children) {
      if (child->isVisible() && child->focusPolicy() != Qt::NoFocus) {
        focusTarget = child;
        break;
      }
    }
  }
  focusTarget->setFocus(Qt::OtherFocusReason);

  // Keep the modifier HUD's cut-out and the ring on the new section.
  if (m_focusOverlay && m_focusOverlay->isActive()) {
    const FocusSectionInfo info = currentFocusSection();
    m_focusOverlay->updateFocus(info.widget, tr("Focus: %1").arg(info.label));
    showSelectionIndicatorFor(info.widget);
  }
}

void InteractionManager::returnGamepadFocusToGrid() {
  if (!m_ctx) {
    return;
  }
  QWidget *fw = QApplication::focusWidget();
  if (!fw) {
    return;
  }
  const auto within = [fw](QWidget *w) { return w && (w == fw || w->isAncestorOf(fw)); };
  QWidget *paneW = m_ctx->ui.sidebar ? m_ctx->ui.sidebar->asWidget() : nullptr;
  if (within(m_ctx->ui.collectionTreeWidget) || within(paneW) || within(m_ctx->ui.itemsTopBar)) {
    if (QWidget *gridW = m_ctx->ui.itemScrollArea) {
      gridW->setFocus(Qt::OtherFocusReason);
    }
  }
  // Back on the grid: the pane is no longer what the buttons act on.
  m_paneSelectionActive = false;
  m_paneRegionIndex = -1;
  hideSelectionIndicator();
}

void InteractionManager::setFocusModifierActive(bool active) {
  if (!m_ctx) {
    return;
  }
  m_focusModifierHeld = active;
  if (!active) {
    if (m_focusOverlay) {
      m_focusOverlay->deactivate();
    }
    // Hand the ring back to the pane selection the chord borrowed it from.
    const QList<QWidget *> regions = paneRegions();
    if (m_paneRegionIndex >= 0 && m_paneRegionIndex < regions.size()) {
      showSelectionIndicatorFor(regions.at(m_paneRegionIndex));
    } else {
      hideSelectionIndicator();
    }
    return;
  }
  // The TOP-LEVEL window, not the items page: a full-height collection
  // tree docks into MainWindow's outer sidebar row, outside the items
  // page, and would otherwise be neither desaturated nor cut out.
  QWidget *content = m_ctx->ui.itemsPage ? m_ctx->ui.itemsPage->window() : nullptr;
  if (!content) {
    return;
  }
  if (!m_focusOverlay) {
    m_focusOverlay = new FocusSectionOverlay(content);
  }
  const FocusSectionInfo info = currentFocusSection();
  m_focusOverlay->activate(content, info.widget, tr("Focus: %1").arg(info.label));
  // The ring marks the focused section too, on top of the HUD.
  showSelectionIndicatorFor(info.widget);
}

QAbstractScrollArea *InteractionManager::detailsPaneScrollArea() const {
  if (!m_ctx || !m_ctx->ui.sidebar) {
    return nullptr;
  }
  QWidget *pane = m_ctx->ui.sidebar->asWidget();
  if (!pane || !pane->isVisible()) {
    return nullptr;
  }
  // The pane may BE a scroll area or merely contain one (the description
  // browser); either way, only a viewport with somewhere to go counts.
  QList<QAbstractScrollArea *> candidates;
  if (auto *self = qobject_cast<QAbstractScrollArea *>(pane)) {
    candidates.append(self);
  }
  candidates.append(pane->findChildren<QAbstractScrollArea *>());
  for (QAbstractScrollArea *area : candidates) {
    if (!area || !area->isVisible()) {
      continue;
    }
    const QScrollBar *bar = area->verticalScrollBar();
    if (bar && bar->maximum() > bar->minimum()) {
      return area;
    }
  }
  return nullptr;
}

bool InteractionManager::scrollDetailsPane(int steps) {
  QAbstractScrollArea *area = detailsPaneScrollArea();
  if (!area) {
    return false;
  }
  QScrollBar *bar = area->verticalScrollBar();
  bar->setValue(bar->value() + steps * qMax(1, bar->singleStep()) * 3);
  return true;
}

void InteractionManager::sendKeyToFocusedWidget(int key) {
  QWidget *fw = QApplication::focusWidget();
  if (!fw) {
    return;
  }
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QApplication::sendEvent(fw, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QApplication::sendEvent(fw, &release);
}

void InteractionManager::moveToolbarFocus(int delta) {
  if (!m_ctx || !m_ctx->ui.itemsTopBar || delta == 0) {
    return;
  }
  QWidget *bar = m_ctx->ui.itemsTopBar;
  QList<QAbstractButton *> buttons;
  const auto all = bar->findChildren<QAbstractButton *>();
  for (QAbstractButton *button : all) {
    if (button->isVisible() && button->isEnabled()) {
      buttons.append(button);
    }
  }
  if (buttons.isEmpty()) {
    return;
  }
  // Layout order, not child order: the top bar is assembled from several
  // nested layouts, so child order does not match what the user sees.
  std::sort(buttons.begin(), buttons.end(), [bar](QAbstractButton *a, QAbstractButton *b) {
    return a->mapTo(bar, QPoint(0, 0)).x() < b->mapTo(bar, QPoint(0, 0)).x();
  });
  const QWidget *fw = QApplication::focusWidget();
  int index = -1;
  for (int i = 0; i < buttons.size(); ++i) {
    if (buttons.at(i) == fw) {
      index = i;
      break;
    }
  }
  index = index < 0 ? (delta > 0 ? 0 : buttons.size() - 1)
                    : std::clamp(index + delta, 0, static_cast<int>(buttons.size()) - 1);
  buttons.at(index)->setFocus(Qt::OtherFocusReason);
}

bool InteractionManager::routeSectionInput(int dx, int dy) {
  // Expand mode first: the stick must not scroll the pane behind a
  // fullscreen artwork view.
  if (visibleArtworkOverlay()) {
    if (dx != 0) {
      return sendKeyToArtworkOverlay(dx < 0 ? Qt::Key_Left : Qt::Key_Right);
    }
    return true; // swallow vertical rather than driving what is hidden
  }
  // Held modifier: the stick is purely a section switcher — this is the
  // ONLY way the vertical axis reaches the toolbar (user decision
  // 2026-08-18).
  if (m_focusModifierHeld) {
    moveFocusSection(dx, dy);
    return false;
  }

  const FocusSectionInfo info = currentFocusSection();
  if (info.kind == FocusSection::Tree && dy != 0) {
    // A focused tree keeps its own list on the vertical axis.
    sendKeyToFocusedWidget(dy < 0 ? Qt::Key_Up : Qt::Key_Down);
    // …and highlighting IS choosing (user request 2026-08-18): the
    // collection switches on its own shortly after the highlight settles,
    // instead of making the user leave the chord and press confirm. The
    // debounce is what makes that affordable — skimming ten rows loads one
    // collection, not ten. Return goes through the tree's own activation
    // path, the same one a mouse click uses.
    if (!m_treeActivateTimer) {
      m_treeActivateTimer = new QTimer(this);
      m_treeActivateTimer->setSingleShot(true);
      m_treeActivateTimer->setInterval(260);
      connect(m_treeActivateTimer, &QTimer::timeout, this, [this]() {
        if (currentFocusSection().kind != FocusSection::Tree) {
          return;
        }
        sendKeyToFocusedWidget(Qt::Key_Return);
        // Re-assert focus AFTER the switch settles (field report
        // 2026-08-18: the stick stopped driving the tree after a
        // collection change, forcing a re-focus). Loading a collection
        // fans out across queued rebuilds, and any one of them can move
        // focus; rather than hunt each, claim it back once the queue
        // drains and again shortly after. Cheap, and a no-op when nothing
        // took it.
        for (int delayMs : {0, 250}) {
          QTimer::singleShot(delayMs, this, [this]() {
            if (!m_ctx || !m_ctx->ui.collectionTreeWidget) {
              return;
            }
            QWidget *tree = m_ctx->ui.collectionTreeWidget;
            if (!tree->isVisible()) {
              return;
            }
            QWidget *fw = QApplication::focusWidget();
            const bool treeStillHasIt = fw && (fw == tree || tree->isAncestorOf(fw));
            if (!treeStillHasIt) {
              tree->setFocus(Qt::OtherFocusReason);
            }
          });
        }
      });
    }
    m_treeActivateTimer->start();
    return true;
  }
  if (info.kind == FocusSection::Toolbar && dx != 0) {
    moveToolbarFocus(dx);
    return true;
  }
  if (dy != 0) {
    // Everywhere else the vertical axis belongs to the details pane,
    // whether or not it holds focus — and it NEVER switches sections
    // (user decision 2026-08-18: "up should only focus the toolbar when
    // the chord button is held"). With nothing to drive it simply does
    // nothing; falling back to section movement would smuggle the toolbar
    // back onto the unheld stick, which is the bug this rule fixes.
    return driveDetailsPane(dy, /*allowAdvance=*/true);
  }
  if (dx != 0) {
    moveFocusSection(dx, 0);
  }
  return false;
}

bool InteractionManager::activateFocusedSection() {
  // A ringed pane target wins: confirm opens THAT artwork expanded (user
  // request 2026-08-18) rather than launching whatever the grid had
  // selected. The ring is cleared the moment the d-pad returns to the
  // grid, so this cannot hijack a normal launch.
  if (m_paneSelectionActive) {
    const QList<QWidget *> regions = paneRegions();
    if (m_paneRegionIndex >= 0 && m_paneRegionIndex < regions.size()) {
      if (auto *tile = qobject_cast<QAbstractButton *>(regions.at(m_paneRegionIndex))) {
        // The click swaps the pane's main preview (its own handler); the
        // expand call is the second half the user asked for, so confirming
        // a tile both updates the sidebar art AND opens it fullscreen.
        tile->click();
        const QString path = tile->property("kartendGalleryPath").toString();
        if (!path.isEmpty() && m_ctx && m_ctx->ui.sidebar) {
          m_ctx->ui.sidebar->openArtworkExpanded(path,
                                                 tile->property("kartendGalleryIsVideo").toBool());
        }
      }
      return true;
    }
  }
  const FocusSectionInfo info = currentFocusSection();
  switch (info.kind) {
  case FocusSection::Grid:
    return false; // the grid keeps its launch behaviour
  case FocusSection::Tree:
    sendKeyToFocusedWidget(Qt::Key_Return);
    return true;
  case FocusSection::Toolbar:
    if (auto *button = qobject_cast<QAbstractButton *>(QApplication::focusWidget())) {
      button->click();
    }
    return true;
  case FocusSection::Pane:
    return true; // swallow: confirming in the pane must not launch the grid
  }
  return false;
}

QList<QWidget *> InteractionManager::paneRegions() const {
  QList<QWidget *> regions;
  if (!m_ctx || !m_ctx->ui.sidebar) {
    return regions;
  }
  QWidget *pane = m_ctx->ui.sidebar->asWidget();
  if (!pane || !pane->isVisible()) {
    return regions;
  }
  // Individual artwork tiles are targets in their own right (user request
  // 2026-08-18: "individual art items ... clickable/selectable"), so the
  // ring lands on ONE picture and confirm opens exactly that one. The
  // thumbs are the gallery's QToolButtons; the Edit control is a
  // QPushButton and is deliberately not swept up.
  const auto thumbs = pane->findChildren<QToolButton *>();
  QList<QWidget *> tiles;
  for (QToolButton *thumb : thumbs) {
    // ONLY real artwork tiles: they are the ones the gallery tagged with
    // their path. Without this filter any other tool button in the pane
    // (the title's edit pencil, for one) became a navigation target and
    // the ring landed on it (field report 2026-08-18).
    if (thumb->isVisible() && thumb->isEnabled() &&
        !thumb->property("kartendGalleryPath").toString().isEmpty()) {
      tiles.append(thumb);
    }
  }
  std::sort(tiles.begin(), tiles.end(), [pane](QWidget *a, QWidget *b) {
    return a->mapTo(pane, QPoint(0, 0)).x() < b->mapTo(pane, QPoint(0, 0)).x();
  });
  regions.append(tiles);

  // Then every scrollable region — description, metadata — so the stick
  // reaches all of them rather than only whichever was found first.
  const auto areas = pane->findChildren<QAbstractScrollArea *>();
  QList<QWidget *> scrollables;
  for (QAbstractScrollArea *area : areas) {
    if (!area->isVisible()) {
      continue;
    }
    if (!area->findChildren<QToolButton *>().isEmpty()) {
      continue; // the thumb strip: represented by its tiles above
    }
    const QScrollBar *vertical = area->verticalScrollBar();
    const QScrollBar *horizontal = area->horizontalScrollBar();
    const bool scrollable = (vertical && vertical->maximum() > vertical->minimum()) ||
                            (horizontal && horizontal->maximum() > horizontal->minimum());
    if (scrollable) {
      scrollables.append(area);
    }
  }
  std::sort(scrollables.begin(), scrollables.end(), [pane](QWidget *a, QWidget *b) {
    return a->mapTo(pane, QPoint(0, 0)).y() < b->mapTo(pane, QPoint(0, 0)).y();
  });
  regions.append(scrollables);
  return regions;
}

namespace {
/// Scrolls @p target into view inside whichever scroll area holds it, so a
/// ringed thumbnail off the right edge of the strip comes to the user.
void revealInsideScrollArea(QWidget *target) {
  for (QWidget *p = target->parentWidget(); p; p = p->parentWidget()) {
    if (auto *area = qobject_cast<QScrollArea *>(p)) {
      area->ensureWidgetVisible(target); // QScrollArea-only API
      return;
    }
  }
}
} // namespace

void InteractionManager::showSelectionIndicatorFor(QWidget *target) {
  if (!m_ctx || !target) {
    return;
  }
  QWidget *window = m_ctx->ui.itemsPage ? m_ctx->ui.itemsPage->window() : nullptr;
  if (!window) {
    return;
  }
  if (!m_selectionIndicator) {
    m_selectionIndicator = new SelectionIndicator(window);
  }
  m_selectionIndicator->showFor(target);
}

void InteractionManager::hideSelectionIndicator() {
  if (m_selectionIndicator) {
    m_selectionIndicator->hideIndicator();
  }
}

bool InteractionManager::driveDetailsPane(int steps, bool allowAdvance) {
  const QList<QWidget *> regions = paneRegions();
  if (regions.isEmpty() || steps == 0) {
    return false;
  }
  if (m_paneRegionIndex < 0 || m_paneRegionIndex >= regions.size()) {
    m_paneRegionIndex = steps > 0 ? 0 : regions.size() - 1;
  }
  m_paneSelectionActive = true;
  // One second without further input returns the stick to the grid (user
  // request 2026-08-18) so the pane never keeps it by accident.
  if (!m_paneIdleTimer) {
    m_paneIdleTimer = new QTimer(this);
    m_paneIdleTimer->setSingleShot(true);
    m_paneIdleTimer->setInterval(1000);
    connect(m_paneIdleTimer, &QTimer::timeout, this, [this]() {
      m_paneSelectionActive = false;
      m_paneRegionIndex = -1;
      hideSelectionIndicator();
      if (m_ctx && m_ctx->ui.itemScrollArea) {
        m_ctx->ui.itemScrollArea->setFocus(Qt::OtherFocusReason);
      }
    });
  }
  m_paneIdleTimer->start();
  QWidget *current = regions.at(m_paneRegionIndex);
  auto *area = qobject_cast<QAbstractScrollArea *>(current);
  if (!area) {
    // An artwork tile: there is nothing to scroll, so the stick simply
    // steps to the neighbouring target.
    const int next = std::clamp(m_paneRegionIndex + (steps > 0 ? 1 : -1), 0,
                                static_cast<int>(regions.size()) - 1);
    m_paneRegionIndex = next;
    QWidget *target = regions.at(next);
    revealInsideScrollArea(target);
    showSelectionIndicatorFor(target);
    // Hovering IS previewing (user request 2026-08-18): landing on a tile
    // swaps the pane's main artwork immediately, so the user sees what
    // they are pointed at without pressing anything. Confirm still opens
    // it fullscreen.
    if (auto *tile = qobject_cast<QAbstractButton *>(target)) {
      tile->click();
    }
    return true;
  }
  // The artwork strip scrolls sideways; drive whichever axis it actually
  // has, so one stick direction walks the whole pane.
  QScrollBar *bar = area->verticalScrollBar();
  if (!bar || bar->maximum() <= bar->minimum()) {
    bar = area->horizontalScrollBar();
  }
  if (bar && bar->maximum() > bar->minimum()) {
    const int before = bar->value();
    bar->setValue(before + steps * qMax(1, bar->singleStep()) * 3);
    if (bar->value() != before) {
      showSelectionIndicatorFor(area);
      return true; // still travelling inside this region
    }
  }
  if (!allowAdvance) {
    showSelectionIndicatorFor(area);
    return true; // at the end, but a held stick must not run away
  }
  const int next =
      std::clamp(m_paneRegionIndex + (steps > 0 ? 1 : -1), 0, static_cast<int>(regions.size()) - 1);
  m_paneRegionIndex = next;
  revealInsideScrollArea(regions.at(next));
  showSelectionIndicatorFor(regions.at(next));
  return true;
}

QWidget *InteractionManager::visibleArtworkOverlay() {
  QWidget *window = m_ctx && m_ctx->ui.itemsPage ? m_ctx->ui.itemsPage->window() : nullptr;
  if (!window) {
    return nullptr;
  }
  // Both overlays (grid preview and details-pane expand) are parented
  // under the window and share the object name, so one lookup covers both.
  const auto overlays = window->findChildren<QWidget *>(QStringLiteral("artworkPreviewOverlay"));
  for (QWidget *overlay : overlays) {
    if (!overlay->isVisible()) {
      continue;
    }
    // Hook the boundary hand-off HERE, not in the gamepad's key helper:
    // the keyboard and the wheel reach the overlay directly, so hooking
    // on send meant only the gamepad ever stepped items at the ends.
    // UniqueConnection + a member slot keeps this idempotent (a functor
    // would be a fatal error).
    if (auto *preview = qobject_cast<ArtworkPreviewOverlay *>(overlay)) {
      connect(preview, &ArtworkPreviewOverlay::galleryBoundaryReached, this,
              &InteractionManager::onExpandedGalleryBoundary, Qt::UniqueConnection);
    }
    return overlay;
  }
  return nullptr;
}

bool InteractionManager::sendKeyToArtworkOverlay(int key) {
  QWidget *overlay = visibleArtworkOverlay();
  if (!overlay) {
    return false;
  }
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QApplication::sendEvent(overlay, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QApplication::sendEvent(overlay, &release);
  return true;
}

bool InteractionManager::stepExpandedItem(int delta) {
  if (!visibleArtworkOverlay() || delta == 0) {
    return false;
  }
  handleArrowKeyNavigation(delta, /*vertical=*/false);
  // RELEASE the phantom key press. handleArrowKeyNavigation is the
  // KEYBOARD path: it calls prepareKeyNavigationState(), which sets
  // "physical key down" on the assumption that a real key-release will
  // follow. Driven programmatically from a wheel tick or a gamepad
  // flick, no release ever arrives — the repeat machinery stayed armed
  // and kept scrolling on its own, which is why the runaway only ever
  // appeared when travelling from one item to the next (field report
  // 2026-08-18). Same pair the gamepad uses when a direction is let go.
  if (m_keyboardManager) {
    m_keyboardManager->setPhysicalKeyDown(false);
    m_keyboardManager->stopRepeat(/*suppressRecentering=*/false);
  }
  // The selection lands asynchronously (scroll animation + viewport
  // rebuild) and the sidebar's gallery for the new item arrives later
  // still, so refresh in stages. Each pass re-shows only if the resolved
  // path actually changed, and re-syncs the strip either way.
  for (int delayMs : {0, 140, 380}) {
    QTimer::singleShot(delayMs, this, [this]() { refreshExpandedArtwork(); });
  }
  return true;
}

void InteractionManager::onExpandedGalleryBoundary(int direction) {
  m_expandedStepDirection = direction;
  m_expandedEntrySnapshot.clear();
  if (m_ctx && m_ctx->ui.sidebar) {
    const auto entries = m_ctx->ui.sidebar->currentGalleryEntries();
    for (const auto &entry : entries) {
      m_expandedEntrySnapshot.append(entry.path);
    }
  }
  (void)stepExpandedItem(direction);
}

void InteractionManager::refreshExpandedArtwork() {
  QWidget *overlayWidget = visibleArtworkOverlay();
  if (!overlayWidget || !m_collections) {
    return;
  }
  const QString filePath = derivePathFromIndex(currentSelectedIndex());
  if (filePath.isEmpty()) {
    return;
  }
  // Resolve directories the way the rest of expand mode does
  // (interactionmanager_enter.cpp): the OWNING collection wins over the
  // viewing parent, and expandConfigVariables is what the artwork lookup
  // expects. Getting this wrong is why the artwork stopped following the
  // item — the path resolved to nothing and the overlay kept its image.
  const int dbIndex = databaseMgr() ? databaseMgr()->getCollectionIndexForFile(filePath) : -1;
  const int ownerIdx = InteractionHelpers::resolveOwnerIndex(
      dbIndex, m_currentCollectionIndex ? *m_currentCollectionIndex : -1, m_collections->size());
  if (ownerIdx < 0) {
    return;
  }
  const CollectionConfig &owner = (*m_collections)[ownerIdx];
  const QString artworkDir =
      SettingsUtils::expandConfigVariables(owner.artworkDirectory, owner.name);
  const QString videoDir = SettingsUtils::expandConfigVariables(owner.videoDirectory, owner.name);

  // The GRID's overlay goes through the scroll layer so manual covers and
  // video-first previews behave exactly as they do everywhere else; the
  // details pane's own overlay is driven directly. Only re-show when the
  // item actually changed — the staged passes must not re-decode.
  if (filePath != m_expandedShownPath) {
    m_expandedShownPath = filePath;
    IArtworkPreviewScroll *gridPreview = m_ctx ? m_ctx->scrollPreview() : nullptr;
    auto *overlay = qobject_cast<ArtworkPreviewOverlay *>(overlayWidget);
    bool shown = false;
    if (gridPreview && gridPreview->isArtworkPreviewVisible()) {
      shown = gridPreview->showMediaPreview(filePath, artworkDir, videoDir);
    }
    if (!shown && overlay) {
      // Nothing resolved (an item with no artwork and no video), or the
      // pane's own overlay is the visible one. showArtworkForFile falls
      // back to the grid's hatched placeholder, which is what makes an
      // artless item read as ITSELF instead of leaving the previous
      // item's picture on screen (field report 2026-08-18: the grid path
      // returned false and nothing repainted).
      overlay->showArtworkForFile(filePath, artworkDir);
    }
  }
  syncExpandedGalleryStrip();
}

void InteractionManager::syncExpandedGalleryStrip() {
  QWidget *overlayWidget = visibleArtworkOverlay();
  if (!overlayWidget || !m_ctx || !m_ctx->ui.sidebar) {
    return;
  }
  // Mirror the sidebar's gallery, exactly as expand-mode activation does
  // (interactionmanager_enter.cpp). setGalleryEntries re-pins the current
  // index by matching the shown path, so the next direction press cycles
  // the NEW item's artwork from the right place.
  const auto sidebarEntries = m_ctx->ui.sidebar->currentGalleryEntries();
  QList<ArtworkPreviewOverlay::GalleryEntry> overlayEntries;
  overlayEntries.reserve(sidebarEntries.size());
  for (const auto &entry : sidebarEntries) {
    overlayEntries.append({entry.label, entry.path, entry.isVideo});
  }
  auto *overlay = qobject_cast<ArtworkPreviewOverlay *>(overlayWidget);
  // Arriving from a BACKWARD step: land on this item's LAST artwork so the
  // next left press cycles it, instead of hitting the boundary again and
  // skipping the item's other pictures entirely. Wait for the gallery to
  // actually turn over — these entries are still the previous item's until
  // the sidebar reloads, and acting on them lands on the WRONG item's art.
  QStringList currentPaths;
  currentPaths.reserve(overlayEntries.size());
  for (const auto &entry : overlayEntries) {
    currentPaths.append(entry.path);
  }
  const bool galleryTurnedOver = !currentPaths.isEmpty() && currentPaths != m_expandedEntrySnapshot;
  if (m_expandedStepDirection < 0 && galleryTurnedOver && overlay) {
    overlay->showArtworkAtPath(overlayEntries.constLast().path);
    // m_expandedShownPath deliberately KEEPS the item's path. It records
    // which ITEM the overlay is on, not which picture — clearing it made
    // the next staged pass believe the item had changed again and re-show
    // its default artwork, undoing this landing a few hundred ms later
    // (field report 2026-08-18: "it doesnt go to the last artwork item of
    // the previous item").
  }
  if (galleryTurnedOver) {
    m_expandedStepDirection = 0;
    m_expandedEntrySnapshot = currentPaths;
  }

  IArtworkPreviewScroll *gridPreview = m_ctx->scrollPreview();
  if (gridPreview && gridPreview->isArtworkPreviewVisible()) {
    if (auto *scroll = dynamic_cast<ScrollManager *>(scrollMgr())) {
      scroll->setArtworkPreviewGallery(overlayEntries);
    }
    return;
  }
  if (overlay) {
    overlay->setGalleryEntries(overlayEntries);
  }
}
