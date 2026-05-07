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
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "mousemanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "gridutils.h"
#include "itemwidget.h"
#include "detailspane.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "detailspanemanager.h"
#include "timerutils.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcInteractionManager, "kartend.interactionmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcInteractionManager().isDebugEnabled()) {                                                 \
      qCDebug(lcInteractionManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

InteractionManager::InteractionManager(QObject *parent) : QObject(parent) {
  m_searchManager = std::make_unique<SearchManager>(this);
  m_selectionManager = std::make_unique<SelectionManager>(this);
  m_keyboardManager = std::make_unique<KeyboardManager>(this);
  m_gamepadManager = std::make_unique<GamepadManager>(this);
  m_arrowHandler = std::make_unique<ArrowNavigationHandler>(this);
  m_alphabeticHandler = std::make_unique<AlphabeticNavigationHandler>(this);
  m_animationManager = std::make_unique<AnimationManager>(this);
  m_mouseManager = std::make_unique<MouseManager>(this);
  m_launchManager = std::make_unique<LaunchManager>(this);
  m_viewportManager = std::make_unique<ViewportManager>(this);
  m_eventManager = std::make_unique<EventManager>(this);
  m_attractManager = std::make_unique<AttractManager>(this);

  m_viewportManager->setContinuousScrollActive(true);
}

// Destructor: stop timers/animations and clear selection
InteractionManager::~InteractionManager() {
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
  stopRepeat();
  clearSelection();
}

// Wires references, installs event filters, and initializes search UI
auto InteractionManager::resolveDoubleClickIndexCandidate() const -> int {
  int idx = currentSelectedIndex();
  if (idx < 0 && m_scrollManager) {
    const auto &active = m_scrollManager->getActiveWidgets();
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
  if (m_scrollManager && idx >= 0) {
    return m_scrollManager->filePathForVisualIndex(idx);
  }
  return {};
}

// Helper: resolve owning collection index for a file path
auto InteractionManager::resolveOwnerForPath(const QString &path) const -> int {
  if (path.isEmpty()) {
    return -1;
  }
  if (m_databaseManager) {
    return m_databaseManager->getCollectionIndexForFile(path);
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
    if (m_databaseManager) {
      int owner = m_databaseManager->getCollectionIndexForFile(selectedPath);
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
  // Delegate debounce check to LaunchManager
  if (m_launchManager && !filePath.isEmpty() && !m_launchManager->canLaunch(filePath)) {
    return;
  }

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
  if (QApplication::closingDown() || !event) {
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
  if (m_scrollManager) {
    int currentWidth = m_scrollManager->getCurrentGridWidth();
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
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(newSelection);
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
  if (!m_scrollManager || !m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }

  const QStringList &filePaths = m_scrollManager->getFilePaths();
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
    trySelectWidget(index, subcollections, 0);
  }

  const int selected = currentSelectedIndex();
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(selected);
    if (m_state.scroll().clickHoldAdvancing) {
      m_scrollManager->refreshSelectionOverlayState();
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
    if (!QApplication::closingDown() && m_artworkManager) {
      m_artworkManager->updateViewportArtwork();
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
  if ((!m_scrollManager) || currentSelectedIndex() != index || QApplication::closingDown()) {
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
    m_scrollManager->updateVirtualView();
    QApplication::processEvents();
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
