#include "interactionmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEasingCurve>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPoint>
#include <QProcess>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>

#include "artworkmanager.h"
#include "databasemanager.h"
#include "gridutils.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "uiconstants.h"

InteractionManager::InteractionManager(QObject *parent) : QObject(parent) {
  m_searchManager = std::make_unique<SearchManager>(this);
  m_selectionManager = std::make_unique<SelectionManager>(this);
  m_keyboardManager = std::make_unique<KeyboardManager>(this);

  m_hScrollAnim = nullptr;

  m_continuousScrollActive = true;
}

// Destructor: stop timers/animations and clear selection
InteractionManager::~InteractionManager() {
  m_isShuttingDown = true;
  stopRepeat();

  TimerUtils::stopAndDisconnectTimers({m_repeatStartTimer, m_repeatTimer});
  TimerUtils::deleteLaterTimer(m_repeatStartTimer);
  TimerUtils::deleteLaterTimer(m_repeatTimer);

  if (m_hScrollAnim != nullptr) {
    m_hScrollAnim->stop();
    m_hScrollAnim->deleteLater();
    m_hScrollAnim = nullptr;
  }

  clearSelection();
}

// Wires references, installs event filters, and initializes search UI
void InteractionManager::setupReferences(const InteractionManagerSetup &setup) {
  m_scrollManager = setup.scrollManager;
  m_sidebarManager = setup.sidebarManager;
  m_settingsManager = setup.settingsManager;
  m_databaseManager = setup.databaseManager;
  m_navigationManager = setup.navigationManager;
  m_sessionManager = setup.sessionManager;
  m_artworkManager = setup.artworkManager;
  m_itemScrollArea = setup.itemScrollArea;
  m_gridContainer = setup.gridContainer;
  m_metadataSidebar = setup.sidebar;
  m_stackedWidget = setup.stackedWidget;
  m_itemsPage = setup.itemsPage;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_searchBar = setup.searchBar;
  m_searchModeButton = setup.searchModeButton;
  m_mainWindow = setup.mainWindow;

  // Setup SearchManager with its dependencies
  if (m_searchManager) {
    SearchManagerSetup searchSetup;
    searchSetup.databaseManager = setup.databaseManager;
    searchSetup.navigationManager = setup.navigationManager;
    searchSetup.scrollManager = setup.scrollManager;
    searchSetup.settingsManager = setup.settingsManager;
    searchSetup.mainWindow = setup.mainWindow;
    searchSetup.searchBar = setup.searchBar;
    searchSetup.searchModeButton = setup.searchModeButton;
    searchSetup.itemScrollArea = setup.itemScrollArea;
    searchSetup.stackedWidget = setup.stackedWidget;
    searchSetup.collectionPage = setup.collectionPage;
    searchSetup.itemsPage = setup.itemsPage;
    searchSetup.collections = setup.collections;
    searchSetup.currentCollectionIndex = setup.currentCollectionIndex;
    m_searchManager->setupReferences(searchSetup);

    // Connect SearchManager signals
    connect(m_searchManager.get(), &SearchManager::requestClearSelection,
            this, &InteractionManager::clearSelection);
    connect(m_searchManager.get(), &SearchManager::requestSelectionRestore,
            this, [this](int index) {
              if (m_navigationManager) {
                m_navigationManager->scheduleSelectionRestore(
                    index, UIConstants::SELECTION_RESTORE_STEPS,
                    UIConstants::SELECTION_RESTORE_STEP_DELAY_MS,
                    UIConstants::SELECTION_RESTORE_MAX_DELAY_MS);
              }
            });
    connect(m_searchManager.get(), &SearchManager::requestScrollbarRecovery,
            this, &InteractionManager::scheduleScrollbarRecovery);
  }

  // Setup SelectionManager with its dependencies
  if (m_selectionManager) {
    SelectionManagerSetup selectionSetup;
    selectionSetup.scrollManager = setup.scrollManager;
    selectionSetup.sidebarManager = setup.sidebarManager;
    selectionSetup.sessionManager = setup.sessionManager;
    selectionSetup.settingsManager = setup.settingsManager;
    selectionSetup.navigationManager = setup.navigationManager;
    selectionSetup.mainWindow = setup.mainWindow;
    selectionSetup.metadataSidebar = setup.sidebar;
    selectionSetup.itemsPage = setup.itemsPage;
    selectionSetup.itemScrollArea = setup.itemScrollArea;
    selectionSetup.collections = setup.collections;
    selectionSetup.currentCollectionIndex = setup.currentCollectionIndex;
    m_selectionManager->setupReferences(selectionSetup);

    // Connect SelectionManager signals
    connect(m_selectionManager.get(), &SelectionManager::selectionChanged,
            this, &InteractionManager::selectionChanged);
    connect(m_selectionManager.get(), &SelectionManager::requestFocusItemsPage,
            this, [this]() {
              if (m_itemsPage != nullptr) {
                m_itemsPage->setFocus();
              }
            });
    connect(m_selectionManager.get(), &SelectionManager::requestStopScrollAnimations,
            this, [this]() {
              if ((m_vScrollAnim != nullptr) &&
                  m_vScrollAnim->state() == QAbstractAnimation::Running) {
                m_vScrollAnim->stop();
              }
            });
  }

  // Setup KeyboardManager with its dependencies
  if (m_keyboardManager) {
    KeyboardManagerSetup keyboardSetup;
    keyboardSetup.scrollManager = setup.scrollManager;
    keyboardSetup.mainWindow = setup.mainWindow;
    keyboardSetup.gridContainer = setup.gridContainer;
    keyboardSetup.itemsPage = setup.itemsPage;
    keyboardSetup.itemScrollArea = setup.itemScrollArea;
    keyboardSetup.stackedWidget = setup.stackedWidget;
    keyboardSetup.searchBar = setup.searchBar;
    keyboardSetup.collections = setup.collections;
    keyboardSetup.currentCollectionIndex = setup.currentCollectionIndex;
    m_keyboardManager->setupReferences(keyboardSetup);

    // Connect KeyboardManager signals
    connect(m_keyboardManager.get(), &KeyboardManager::requestSelectionMove,
            this, &InteractionManager::handleArrowKeyNavigation);
    connect(m_keyboardManager.get(), &KeyboardManager::requestEnterAction,
            this, [this]() {
              if (m_scrollManager != nullptr) {
                const int totalItems = m_scrollManager->getTotalItems();
                processEnterOrReturnKey(totalItems);
              }
            });
    connect(m_keyboardManager.get(), &KeyboardManager::requestSearchModeToggle,
            this, &InteractionManager::toggleSearchMode);
    connect(m_keyboardManager.get(), &KeyboardManager::requestSearchBarFocus,
            this, [this]() {
              handleSlashKey();
            });
    connect(m_keyboardManager.get(), &KeyboardManager::requestEscapeAction,
            this, [this]() {
              handleEscapeKey();
            });
    connect(m_keyboardManager.get(), &KeyboardManager::requestClearSearchBar,
            this, [this]() {
              if (m_searchBar != nullptr) {
                m_searchBar->setProperty(PropertyKeys::ClearedByEscape, true);
                m_searchBar->clear();
              }
            });
    connect(m_keyboardManager.get(), &KeyboardManager::requestFocusGrid,
            this, [this]() {
              if (m_gridContainer != nullptr) {
                m_gridContainer->setFocus(Qt::OtherFocusReason);
              }
            });
    connect(m_keyboardManager.get(), &KeyboardManager::requestScrollAnimationStop,
            this, [this]() {
              if ((m_vScrollAnim != nullptr) &&
                  m_vScrollAnim->state() == QAbstractAnimation::Running) {
                m_vScrollAnim->stop();
              }
            });
    connect(m_keyboardManager.get(), &KeyboardManager::repeatStepRequested,
            this, &InteractionManager::onKeyboardRepeatStep);
    connect(m_keyboardManager.get(), &KeyboardManager::stopRepeatRequested,
            this, &InteractionManager::onKeyboardStopRepeat);
  }

  updateSearchModeButton();
  updateSearchBarPlaceholder();

  if (m_searchBar != nullptr) {
    connect(m_searchBar, &QLineEdit::textChanged, this,
            &InteractionManager::handleImmediateSearchTextChanged);
  }

  if (qApp != nullptr) {
    qApp->installEventFilter(this);
  }
  if (m_itemsPage != nullptr) {
    m_itemsPage->installEventFilter(this);
  }
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->installEventFilter(this);
    QWidget *viewport = m_itemScrollArea->viewport();
    if (viewport != nullptr) {
      viewport->installEventFilter(this);
    }
  }
  if (m_gridContainer != nullptr) {
    m_gridContainer->installEventFilter(this);
  }
}

// KeyboardManager callback: handles arrow key navigation
void InteractionManager::handleArrowKeyNavigation(int direction, bool vertical) {
  if (m_restoringSelection || m_navigationInProgress) {
    return;
  }
  if (m_scrollManager == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }

  if (m_keyboardManager) {
    m_keyboardManager->prepareKeyNavigationState();
  }
  const int totalItems = m_scrollManager->getTotalItems();
  if (totalItems == 0) {
    return;
  }

  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, true);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
  }

  const int gridWidth = getCurrentGridWidth();
  const int currentSelection =
      (m_selectedItemIndex < 0) ? 0 : m_selectedItemIndex;
  const bool offscreenBefore = isItemOffscreen(currentSelection, gridWidth);

  const bool wrapEnabled = (m_mainWindow != nullptr)
                               ? m_mainWindow->m_generalSettings.wrapNavigation
                               : false;
  m_isWrappingNavigation = false;
  if (m_keyboardManager) {
    m_keyboardManager->setWrapSequenceActive(false);
  }

  bool didWrap = false;
  const int newSelection =
      KeyboardManager::calculateNewSelection(totalItems, currentSelection, direction,
                            wrapEnabled, vertical, gridWidth, didWrap);
  if (didWrap) {
    m_isWrappingNavigation = true;
    if (m_keyboardManager) {
      m_keyboardManager->setWrapSequenceActive(true);
    }
  }

  const bool isNewRow =
      computeIsNewRow(currentSelection, newSelection, gridWidth);

  const bool forceImmediate = offscreenBefore || m_isWrappingNavigation;
  if (forceImmediate) {
    applyImmediateCenterSuppression();
  }

  if (!vertical && !isNewRow && m_itemScrollArea != nullptr) {
    applyMinorHorizontalSuppress();
  }

  updateSelectionForKeyMove(newSelection);
  performVisibilityForKeyMove(isNewRow, newSelection);

  if (m_keyboardManager) {
    m_keyboardManager->finalizeKeyRepeat(nullptr, direction, vertical);
  }
  if (m_itemsPage != nullptr) {
    m_itemsPage->setFocus();
  }
}

// KeyboardManager callback: handles repeat step during key hold
void InteractionManager::onKeyboardRepeatStep() {
  if (m_keyboardManager == nullptr || !m_keyboardManager->isRepeating() ||
      !m_keyboardManager->isPhysicalKeyDown() ||
      m_keyboardManager->repeatDelta() == 0) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }
  if (m_scrollManager == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }
  if (m_stackedWidget == nullptr || m_itemsPage == nullptr ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }

  const int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    if (m_keyboardManager) {
      m_keyboardManager->stopRepeat();
    }
    return;
  }

  const int direction = m_keyboardManager->repeatDelta();
  const bool repeatVertical = m_keyboardManager->repeatVertical();
  const bool horizontal = !repeatVertical;

  const int currentSelection =
      (m_selectedItemIndex < 0) ? 0 : m_selectedItemIndex;
  const bool wrapEnabled = (m_mainWindow != nullptr)
                               ? m_mainWindow->m_generalSettings.wrapNavigation
                               : false;
  const int gridWidth = getCurrentGridWidth();
  bool didWrap = false;
  const int newSelection = KeyboardManager::calculateNewSelection(
      totalItems, currentSelection, direction, wrapEnabled, repeatVertical,
      gridWidth, didWrap);
  if (newSelection == currentSelection) {
    return;
  }

  if (didWrap || m_keyboardManager->isWrapSequenceActive()) {
    m_forceImmediateCenter = true;
    m_keyboardManager->setWrapSequenceActive(true);
    m_keyboardManager->setContinuousScrollActive(false);
  } else {
    m_keyboardManager->setContinuousScrollActive(true);
  }

  const bool rowChanged =
      (*m_currentCollectionIndex >= 0 &&
       *m_currentCollectionIndex < m_collections->size() && gridWidth > 0)
          ? KeyboardManager::hasRowChanged(gridWidth, currentSelection, newSelection)
          : false;

  if (horizontal) {
    setPendingSelectionIfNeeded(true, newSelection);
  } else if (rowChanged) {
    setPendingSelectionIfNeeded(true, newSelection);
  }

  m_selectedItemIndex = newSelection;
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(newSelection);
  }

  updateSelectionStateAfterMove(newSelection);

  if (horizontal && !rowChanged) {
    ensureHorizontallyVisible(newSelection);
    return;
  }

  centerItemVertically(newSelection, false);
}

// KeyboardManager callback: handles cleanup when key hold stops
void InteractionManager::onKeyboardStopRepeat(bool suppressRecentering) {
  // KeyboardManager handles timers and properties; we handle selection cleanup
  if (m_hScrollAnim != nullptr &&
      m_hScrollAnim->state() == QAbstractAnimation::Running) {
    m_hScrollAnim->stop();
  }

  if (property(PropertyKeys::SelectionSuppressed).toBool()) {
    int pending = property(PropertyKeys::PendingSelectionIndex).toInt();
    if (pending >= 0) {
      selectItemByIndex(pending, true);
    }
    setProperty(PropertyKeys::SelectionSuppressed, false);
    setProperty(PropertyKeys::PendingSelectionIndex, -1);
  }

  if (m_keyboardManager && !m_keyboardManager->isPhysicalKeyDown()) {
    m_continuousScrollActive =
        (m_vScrollAnim != nullptr &&
         m_vScrollAnim->state() == QAbstractAnimation::Running);
  }

  if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
      !suppressRecentering) {
    QTimer::singleShot(
        UIConstants::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
          bool stillActive = m_keyboardManager
                                 ? m_keyboardManager->isContinuousScrollActive()
                                 : m_continuousScrollActive;
          if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
              !stillActive) {
            centerItemVertically(m_selectedItemIndex, false);
          }
        });
  }
}

// Computes search context flags including availability of "All collections"
// under the specified constraints
auto InteractionManager::computeSearchContext() const -> SearchContext {
  SearchContext ctx{};

  const int collIndex =
      (m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1;
  if (m_collections == nullptr || collIndex < 0 ||
      collIndex >= m_collections->size()) {
    return ctx;
  }

  const CollectionConfig &cfg = (*m_collections)[collIndex];
  const QList<int> subs = getSubcollections(collIndex);
  ctx.hasSubs = !subs.isEmpty();

  ctx.realDirectItems = hasDirectItemsForIndex(collIndex);

  ctx.allowAll = allowAllFor(cfg, collIndex, ctx.hasSubs);

  ctx.isContainer = ctx.hasSubs && !ctx.realDirectItems;
  return ctx;
}

// Returns whether index has any direct items according to cache or filesystem
auto InteractionManager::hasDirectItemsForIndex(int idx) const -> bool {
  if (m_collections == nullptr || idx < 0 || idx >= m_collections->size()) {
    return false;
  }

  const CollectionConfig &collCfg = (*m_collections)[idx];

  qint64 direct = -1;
  qint64 recursive = -1;
  bool haveCounts = false;
  if (m_sessionManager) {
    haveCounts = m_sessionManager->getCollectionCounts(collCfg, *m_collections,
                                                       direct, recursive);
  }
  if (haveCounts) {
    return direct > 0;
  }

  QString mediaDir = (m_settingsManager != nullptr)
                         ? SettingsUtils::expandConfigVariables(
                               collCfg.mediaDirectory, collCfg.name)
                         : collCfg.mediaDirectory;
  if (mediaDir.trimmed().isEmpty()) {
    return false;
  }
  QDir dir(mediaDir);
  if (!dir.exists()) {
    return false;
  }
  const QStringList filters =
      collCfg.extensions.isEmpty() ? QStringList() : collCfg.extensions;
  const QStringList files = filters.isEmpty()
                                ? dir.entryList(QDir::Files)
                                : dir.entryList(filters, QDir::Files);
  return !files.isEmpty();
}

// Returns whether the "All collections" option should be allowed
auto InteractionManager::allowAllFor(const CollectionConfig &cfg, int collIndex,
                                     bool hasSubs) const -> bool {
  const bool isRoot = (cfg.parentCollectionIndex == -1);
  const bool isLeaf = !hasSubs;

  if (isRoot) {
    const int total = (m_collections != nullptr) ? m_collections->size() : 0;
    for (int i = 0; i < total; ++i) {
      if (i == collIndex) {
        continue;
      }
      const CollectionConfig &rootCandidate = (*m_collections)[i];
      if (rootCandidate.parentCollectionIndex == -1 &&
          hasDirectItemsForIndex(i)) {
        return true;
      }
    }
    return false;
  }

  if (hasSubs || isLeaf) {
    return true;
  }
  return false;
}

// Handles global key presses; ESC clears search, navigates to parent only if
// subcollection, otherwise does nothing
auto InteractionManager::handleGlobalKeyPress(QKeyEvent *event) -> bool {
  if (event == nullptr) {
    return false;
  }
  if (event->key() == Qt::Key_Slash) {
    return handleSlashKey();
  }
  if (event->key() == Qt::Key_Escape) {
    return handleEscapeKey();
  }
  return false;
}

// Focuses the search bar and selects all text when '/' is pressed.
// If the search bar already has focus and is empty, toggles search mode instead.
auto InteractionManager::handleSlashKey() -> bool {
  if (m_searchBar == nullptr || !m_searchBar->isVisible()) {
    return false;
  }
  
  if (m_searchBar->hasFocus() && m_searchBar->text().trimmed().isEmpty()) {
    toggleSearchMode();
    return true;
  }
  
  m_searchBar->setFocus(Qt::ShortcutFocusReason);
  m_searchBar->selectAll();
  return true;
}

// Clears search if non-empty (keeping focus), otherwise navigates to parent
// collection when possible. If search bar is focused and empty, returns focus
// to the grid.
auto InteractionManager::handleEscapeKey() -> bool {
  const QString current =
      (m_searchBar != nullptr ? m_searchBar->text().trimmed() : QString());
  const bool searchBarFocused = (m_searchBar != nullptr && m_searchBar->hasFocus());
  
  if (!current.isEmpty()) {
    if (m_searchBar != nullptr) {
      m_searchBar->setProperty(PropertyKeys::ClearedByEscape, true);
      m_searchBar->clear();
      // Keep focus on search bar when clearing text
    }
    return true;
  }
  
  // If search bar is focused but empty, return focus to grid
  if (searchBarFocused) {
    if (m_gridContainer != nullptr) {
      m_gridContainer->setFocus(Qt::OtherFocusReason);
    }
    return true;
  }

  const int collIndex =
      (m_currentCollectionIndex != nullptr ? *m_currentCollectionIndex : -1);
  if (m_collections != nullptr && collIndex >= 0 &&
      collIndex < m_collections->size()) {
    const CollectionConfig &cfg = (*m_collections)[collIndex];
    if (cfg.isSubcollection && cfg.parentCollectionIndex >= 0 &&
        cfg.parentCollectionIndex < m_collections->size()) {
      if (m_navigationManager != nullptr) {
        constexpr int kRestoreAttempts = UIConstants::SELECTION_RESTORE_STEPS;
        constexpr int kRestoreIntervalMs =
            UIConstants::SELECTION_RESTORE_STEP_DELAY_MS;
        constexpr int kRestoreTimeoutMs =
            UIConstants::SELECTION_RESTORE_MAX_DELAY_MS;
        const int parent = cfg.parentCollectionIndex;
        m_navigationManager->showCollectionItems(parent);
        int sel = -1;
        if (m_settingsManager != nullptr) {
          sel = m_settingsManager->getLastSelectedItem(parent);
        }
        if (sel >= 0) {
          m_navigationManager->scheduleSelectionRestore(
              sel, kRestoreAttempts, kRestoreIntervalMs, kRestoreTimeoutMs);
        }
      }
    } else {
      return true;
    }
    return true;
  }
  return true;
}

// Builds the allowed search mode cycle respecting the constraints and desired
// defaults
auto InteractionManager::buildSearchModeCycle(const SearchContext &ctx) const
    -> QVector<SearchMode> {
  QVector<SearchMode> cycle;
  cycle.reserve(3);

  const int collIndex =
      ((m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1);
  const bool valid = ((m_collections != nullptr) && collIndex >= 0 &&
                      collIndex < m_collections->size());
  bool isRoot = false;
  if (valid) {
    isRoot = ((*m_collections)[collIndex].parentCollectionIndex == -1);
  }

  if (isRoot) {
    cycle << (ctx.hasSubs ? SearchMode::CurrentAndSubcollections
                          : SearchMode::CurrentCollection);
    if (ctx.allowAll) {
      cycle << SearchMode::AllCollections;
    }
    return cycle;
  }

  if (ctx.hasSubs) {
    cycle << SearchMode::CurrentAndSubcollections;
    if (ctx.realDirectItems) {
      cycle << SearchMode::CurrentCollection;
    }
    if (ctx.allowAll) {
      cycle << SearchMode::AllCollections;
    }
    return cycle;
  }

  cycle << SearchMode::CurrentCollection;
  if (ctx.allowAll) {
    cycle << SearchMode::AllCollections;
  }
  return cycle;
}

void InteractionManager::handleImmediateSearchTextChanged(const QString &text) {
  updateSearchBarPlaceholder();
  if (m_searchManager) {
    m_searchManager->onSearchTextChanged(text, currentSelectedIndex());
  }
}

// Helper: pick a currently selected visual index if any
auto InteractionManager::resolveDoubleClickIndexCandidate() const -> int {
  int idx = (m_selectedItemIndex >= 0) ? m_selectedItemIndex : -1;
  if (idx < 0 && m_scrollManager != nullptr) {
    const auto &active = m_scrollManager->getActiveWidgets();
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
      if (it.value() != nullptr && it.value()->isSelected()) {
        return it.key();
      }
    }
  }
  return idx;
}

// Helper: derive file path for a given visual index via ScrollManager
auto InteractionManager::derivePathFromIndex(int idx) const -> QString {
  if (m_scrollManager != nullptr && idx >= 0) {
    return m_scrollManager->filePathForVisualIndex(idx);
  }
  return {};
}

// Helper: resolve owning collection index for a file path
auto InteractionManager::resolveOwnerForPath(const QString &path) const -> int {
  if (path.isEmpty()) {
    return -1;
  }
  if (m_databaseManager != nullptr) {
    return m_databaseManager->getCollectionIndexForFile(path);
  }
  if (m_currentCollectionIndex != nullptr) {
    return *m_currentCollectionIndex;
  }
  return -1;
}

// Helper: fallback collection index based on current selection or view
auto InteractionManager::getFallbackCollectionIndex() const -> int {
  if (!m_selectedFilePath.isEmpty()) {
    if (m_databaseManager != nullptr) {
      int owner =
          m_databaseManager->getCollectionIndexForFile(m_selectedFilePath);
      if (owner >= 0) {
        return owner;
      }
    }
  }
  if (m_currentCollectionIndex != nullptr) {
    return *m_currentCollectionIndex;
  }
  return -1;
}

// Launches on double‑click without altering or interrupting any scroll state
void InteractionManager::handleWidgetDoubleClickedWithCollection(
    const QString &filePath, int collectionIndex) {
  static QHash<QString, qint64> lastLaunchTimes;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  constexpr qint64 kDoubleLaunchGuardMs = 500;
  if (lastLaunchTimes.contains(filePath)) {
    if (now - lastLaunchTimes[filePath] < kDoubleLaunchGuardMs) {
      return;
    }
  }
  lastLaunchTimes[filePath] = now;

  setProperty(PropertyKeys::RowChangeFirstClickIndex, -1);
  setProperty(PropertyKeys::RowChangeFirstClickMs, 0);
  setProperty(PropertyKeys::DeferCenterOnClick, false);
  setProperty(PropertyKeys::DeferredCenterIndex, -1);

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
    launchItemWithCollection(path, collIdx);
    return;
  }
  const int fallbackIdx = getFallbackCollectionIndex();
  if (fallbackIdx >= 0 && !m_selectedFilePath.isEmpty()) {
    launchItemWithCollection(m_selectedFilePath, fallbackIdx);
  }
}

// Global event filter handling input, mouse/scroll, selection, and viewport
// scrolling
// Global event filter handling input, mouse/scroll, selection, and viewport
// scrolling
auto InteractionManager::eventFilter(QObject *obj, QEvent *event) -> bool {
  if (QApplication::closingDown() || event == nullptr) {
    return QObject::eventFilter(obj, event);
  }

  if (handleActivityEvent(event)) {
    // Activity tracking was handled
  }

  switch (event->type()) {
  case QEvent::MouseButtonPress:
    return handleMouseButtonPress(obj, event);
  case QEvent::MouseButtonRelease:
    return handleMouseButtonRelease(obj, event);
  case QEvent::Wheel:
    return handleWheelEvent(obj, event);
  case QEvent::KeyPress:
    return handleKeyPressEvent(obj, event);
  case QEvent::KeyRelease: {
    return handleKeyReleaseEvent(obj, event);
  }
  case QEvent::MouseButtonDblClick:
    return handleMouseDoubleClick(obj, event);
  default:
    break;
  }
  return QObject::eventFilter(obj, event);
}

auto InteractionManager::handleKeyReleaseEvent(QObject *obj, QEvent *event)
    -> bool {
  auto *keyEvent = static_cast<QKeyEvent *>(event);
  if (keyEvent == nullptr) {
    return QObject::eventFilter(obj, event);
  }

  // Delegate to KeyboardManager for key release handling
  if (m_keyboardManager) {
    const bool handled = m_keyboardManager->handleKeyRelease(keyEvent);
    if (handled) {
      event->accept();
      return true;
    }
  }

  return QObject::eventFilter(obj, event);
}
auto InteractionManager::handleActivityEvent(QEvent *event) -> bool {
  bool activityEvent = false;
  switch (event->type()) {
  case QEvent::MouseMove:
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonRelease:
  case QEvent::KeyPress:
  case QEvent::KeyRelease:
  case QEvent::Wheel:
    activityEvent = true;
    if (m_artworkManager != nullptr) {
      m_artworkManager->updateUserActivity();
    }
    break;
  default:
    break;
  }

  if (activityEvent) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 last = property(PropertyKeys::LastUiActivityMs).toLongLong();
    if (last > 0 && (now - last) >= UIConstants::USER_IDLE_THRESHOLD_MS) {
      setProperty(PropertyKeys::ArmFirstClickDelay, true);
    }
    setProperty(PropertyKeys::LastUiActivityMs, now);
  }

  return activityEvent;
}

auto InteractionManager::handleMouseButtonPress(QObject *obj, QEvent *event)
    -> bool {
  if ((obj != nullptr && qobject_cast<QScrollBar *>(obj) != nullptr) ||
      qobject_cast<QScrollBar *>(obj != nullptr ? obj->parent() : nullptr) !=
          nullptr) {
    m_continuousScrollActive = true;
    QTimer::singleShot(UIConstants::CONTINUOUS_SCROLL_IDLE_MS, this,
                       [this]() { m_continuousScrollActive = false; });
    stopRepeat(true);
    return QObject::eventFilter(obj, event);
  }

  return handleMousePress(obj, event);
}

auto InteractionManager::handleMouseButtonRelease(QObject *obj, QEvent *event)
    -> bool {
  auto *mouseReleaseEvent = static_cast<QMouseEvent *>(event);
  if (mouseReleaseEvent != nullptr &&
      mouseReleaseEvent->button() == Qt::LeftButton) {
    m_leftMouseDown = false;
    if (m_clickHoldTimer != nullptr && m_clickHoldTimer->isActive()) {
      m_clickHoldTimer->stop();
    }
    if (m_mouseHoldScrolling) {
      stopMouseHoldScrolling();
    }
    setProperty(PropertyKeys::ClickHoldRowChange, false);
    setProperty(PropertyKeys::DeferCenterOnClick, false);
    setProperty(PropertyKeys::DeferredCenterIndex, -1);
    setProperty(PropertyKeys::ClickScroll, false);
  }
  return QObject::eventFilter(obj, event);
}

auto InteractionManager::handleWheelEvent(QObject *obj, QEvent *event) -> bool {
  QWidget *activeModal = QApplication::activeModalWidget();
  if (activeModal != nullptr) {
    return QObject::eventFilter(obj, event);
  }

  if (m_itemScrollArea == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr || *m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size() ||
      m_stackedWidget == nullptr ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    return QObject::eventFilter(obj, event);
  }

  auto *wheelEvent = static_cast<QWheelEvent *>(event);
  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  if (vScrollBar == nullptr) {
    return QObject::eventFilter(obj, event);
  }

  stopArrowKeyAnimationIfRunning(vScrollBar);

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];

  const int wheelSteps = computeWheelSteps(wheelEvent);
  if (wheelSteps == 0) {
    return QObject::eventFilter(obj, event);
  }

  int currentPos = vScrollBar->value();

  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, true);
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
    qint64 until = QDateTime::currentMSecsSinceEpoch() +
                   UIConstants::WHEEL_SUPPRESS_ARROW_CENTER_MS;
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                  until);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }

  const bool wrapTriggered = applyWheelSelectionDelta(wheelSteps);
  if (wrapTriggered) {
    m_wheelScrolling = false;
    m_continuousScrollActive = false;
    if (m_vScrollAnim != nullptr &&
        m_vScrollAnim->state() == QAbstractAnimation::Running) {
      m_vScrollAnim->stop();
    }
    if (m_scrollManager != nullptr) {
      m_scrollManager->updateVirtualView();
    }
    event->accept();
    return true;
  }

  int singleRowPixels = collection.itemHeight + collection.verticalSpacing;
  int basePos = currentPos;
  if (m_vScrollAnim != nullptr &&
      m_vScrollAnim->state() == QAbstractAnimation::Running) {
    basePos = m_vScrollAnim->endValue().toInt();
  }
  int targetPos = basePos - (wheelSteps * singleRowPixels);
  targetPos = qBound(0, targetPos, vScrollBar->maximum());

  m_wheelScrolling = true;
  m_continuousScrollActive = true;

  if (m_vScrollAnim == nullptr) {
    m_vScrollAnim = new QPropertyAnimation(vScrollBar, "value", this);
    m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  }

  if (m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
  }

  m_vScrollAnim->setStartValue(currentPos);
  m_vScrollAnim->setEndValue(targetPos);
  m_vScrollAnim->setDuration(UIConstants::SMOOTH_SCROLL_WHEEL_DURATION);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(m_vScrollAnim, &QPropertyAnimation::valueChanged, this, [this]() {
    if (m_scrollManager != nullptr) {
      m_scrollManager->updateVirtualView();
    }
  });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this, [this]() {
    m_wheelScrolling = false;
    m_continuousScrollActive = false;
    if (m_itemScrollArea) {
      m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
      m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, false);
      m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, false);
      m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs, 0);
      if (m_scrollManager != nullptr) {
        m_scrollManager->refreshSelectionOverlayState();
      }
    }
    if (m_scrollManager != nullptr && m_selectedItemIndex >= 0) {
      m_scrollManager->updateSelectionForIndex(m_selectedItemIndex);
    }
  });

  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, true);
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }

  m_vScrollAnim->start();

  if (m_scrollManager != nullptr) {
    QTimer::singleShot(0, this, [this]() {
      if (m_scrollManager) {
        m_scrollManager->updateVirtualView();
      }
    });
  }

  event->accept();
  return true;
}

// Computes wheel scroll steps from angleDelta or pixelDelta
auto InteractionManager::computeWheelSteps(const QWheelEvent *wheelEvent)
    -> int {
  if (wheelEvent == nullptr) {
    return 0;
  }
  const int wheelAngle = wheelEvent->angleDelta().y();
  if (wheelAngle != 0) {
    constexpr int kWheelAngleStep = 120;
    int steps = wheelAngle / kWheelAngleStep;
    if (steps == 0) {
      steps = (wheelAngle > 0 ? 1 : -1);
    }
    return steps;
  }
  const QPoint pixelDelta = wheelEvent->pixelDelta();
  const int pixelDeltaY = pixelDelta.y();
  if (pixelDeltaY == 0) {
    return 0;
  }
  constexpr int kPixelDeltaStep = 120;
  int steps = pixelDeltaY / kPixelDeltaStep;
  if (steps == 0) {
    steps = (pixelDeltaY > 0 ? 1 : -1);
  }
  return steps;
}

auto InteractionManager::applyWheelSelectionDelta(int wheelSteps) -> bool {
  if (wheelSteps == 0 || m_scrollManager == nullptr ||
      m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    return false;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return false;
  }
  const int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    return false;
  }

  const int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return false;
  }

  const bool wrapEnabled =
      (m_mainWindow != nullptr)
          ? m_mainWindow->m_generalSettings.wrapNavigation
          : false;

  int currentSelection = (m_selectedItemIndex < 0) ? 0 : m_selectedItemIndex;
  int remainingSteps = wheelSteps;
  bool wrapTriggered = false;

  while (remainingSteps != 0) {
    const int direction = (remainingSteps > 0) ? -gridWidth : gridWidth;
    bool didWrap = false;
    const int newSelection = KeyboardManager::calculateNewSelection(
        totalItems, currentSelection, direction, wrapEnabled, true, gridWidth,
        didWrap);

    if (newSelection == currentSelection) {
      break;
    }

    wrapTriggered = wrapTriggered || didWrap;
    if (didWrap) {
      m_wrapSequenceActive = true;
      m_forceImmediateCenter = true;
      m_continuousScrollActive = false;
    }
    const bool isNewRow =
        computeIsNewRow(currentSelection, newSelection, gridWidth);
    updateSelectionForKeyMove(newSelection);
    performVisibilityForKeyMove(isNewRow, newSelection);

    currentSelection = newSelection;
    remainingSteps += (remainingSteps > 0) ? -1 : 1;
  }

  if (wrapTriggered) {
    m_wrapSequenceActive = true;
    m_forceImmediateCenter = true;
  }

  return wrapTriggered;
}

// Stops any arrow key scroll animation associated with the given scrollbar
void InteractionManager::stopArrowKeyAnimationIfRunning(QScrollBar *scrollBar) {
  if (scrollBar == nullptr) {
    return;
  }
  if (auto *anim =
          scrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim")) {
    if (anim->state() == QAbstractAnimation::Running) {
      anim->stop();
    }
  }
}

auto InteractionManager::handleKeyPressEvent(QObject *obj, QEvent *event)
    -> bool {
  auto *keyEvent = static_cast<QKeyEvent *>(event);

  if (QApplication::activeModalWidget() != nullptr) {
    return QObject::eventFilter(obj, event);
  }

  // Delegate to KeyboardManager for key handling
  if (m_keyboardManager) {
    const bool searchBarFocused =
        (m_searchBar != nullptr) && m_searchBar->hasFocus();
    const bool handled =
        m_keyboardManager->handleKeyPress(keyEvent, searchBarFocused);
    if (handled) {
      event->accept();
      return true;
    }
  }

  // If search bar is focused and KeyboardManager didn't handle, let it through
  if ((m_searchBar != nullptr) && m_searchBar->hasFocus()) {
    return QObject::eventFilter(obj, event);
  }

  return QObject::eventFilter(obj, event);
}

auto InteractionManager::handleMouseDoubleClick(QObject *obj, QEvent *event)
    -> bool {
  auto *mouseEvent = static_cast<QMouseEvent *>(event);
  if (mouseEvent == nullptr || mouseEvent->button() != Qt::LeftButton) {
    return QObject::eventFilter(obj, event);
  }

  auto *widget = qobject_cast<MediaItemWidget *>(obj);
  if (widget == nullptr) {
    return QObject::eventFilter(obj, event);
  }

  // If the double-clicked widget represents a subcollection, allow the widget
  // to handle the event so its subcollectionDoubleClicked signal is emitted.
  if (m_scrollManager != nullptr && m_currentCollectionIndex != nullptr &&
      *m_currentCollectionIndex >= 0) {
    int visualIndex = -1;
    const auto &active = m_scrollManager->getActiveWidgets();
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
      if (it.value() == widget) {
        visualIndex = it.key();
        break;
      }
    }
    if (visualIndex >= 0) {
      const QList<int> subs = getSubcollections(*m_currentCollectionIndex);
      if (visualIndex < subs.size()) {
        return QObject::eventFilter(obj, event);
      }
    }
  }

  QString path = widget->getFilePath();
  if (path.isEmpty()) {
    event->accept();
    return true;
  }

  int collIdx = -1;
  if (m_databaseManager != nullptr) {
    collIdx = m_databaseManager->getCollectionIndexForFile(path);
  } else if (m_currentCollectionIndex != nullptr) {
    collIdx = *m_currentCollectionIndex;
  } else {
    collIdx = -1;
  }
  handleWidgetDoubleClickedWithCollection(path, collIdx);
  event->accept();
  return true;
}

auto InteractionManager::handleMousePress(QObject *obj, QEvent *event) -> bool {
  if (m_restoringSelection) {
    event->accept();
    return true;
  }
  auto *mouseEvent = static_cast<QMouseEvent *>(event);
  if ((mouseEvent == nullptr) || mouseEvent->button() != Qt::LeftButton) {
    return QObject::eventFilter(obj, event);
  }

  if (!m_itemScrollArea || (m_gridContainer == nullptr) ||
      (m_stackedWidget == nullptr) || (m_itemsPage == nullptr)) {
    return QObject::eventFilter(obj, event);
  }
  if (m_stackedWidget->currentWidget() != m_itemsPage) {
    return QObject::eventFilter(obj, event);
  }

  m_leftMouseDown = true;

  const int previousSelection = m_selectedItemIndex;
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;

  bool target =
      (obj == m_itemScrollArea || obj == m_itemScrollArea->viewport() ||
       obj == m_gridContainer || obj == m_itemsPage ||
       qobject_cast<MediaItemWidget *>(obj) != nullptr);
  if (!target) {
    return QObject::eventFilter(obj, event);
  }

  QPoint clickPos = mouseEvent->pos();
  if (obj != m_gridContainer) {
    if (auto *w = qobject_cast<QWidget *>(obj)) {
      clickPos = m_gridContainer->mapFromGlobal(w->mapToGlobal(clickPos));
    }
  }

  if (m_scrollManager == nullptr) {
    clearSelectionAndFocus();
    event->accept();
    return true;
  }

  MediaItemWidget *chosen = findBestWidgetForClick(clickPos);
  if (chosen != nullptr) {
    const int clickedIndex =
        handleWidgetSelection(chosen, clickPos, mouseEvent);

    // Ensure we have a valid index before setting up hold candidate
    if (clickedIndex >= 0) {
      updateClickHoldHorizontalCandidate(previousSelection, clickedIndex);

      if (m_clickHoldTimer == nullptr) {
        m_clickHoldTimer = new QTimer(this);
        m_clickHoldTimer->setSingleShot(true);
        connect(m_clickHoldTimer, &QTimer::timeout, this, [this, clickPos]() {
          if (m_leftMouseDown) {
            startMouseHoldScrolling(clickPos);
          }
        });
      }
      m_clickHoldTimer->start(UIConstants::CLICK_HOLD_START_MS);
    }

    event->accept();
    return true;
  }
  clearSelectionAndFocus();
  event->accept();
  return true;
}

// Instantly positions scrollbars around the target index and marks programmatic
// scroll using PropertyKeys
// Instantly positions scrollbars around the target index and marks programmatic
// scroll using PropertyKeys
void InteractionManager::applyImmediateViewportPositioningForSelection(
    int targetIndex) {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }
  if (targetIndex < 0) {
    return;
  }

  QScrollBar *verticalScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *horizontalScrollBar = m_itemScrollArea->horizontalScrollBar();

  if ((verticalScrollBar != nullptr) && *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    const CollectionConfig &collection =
        (*m_collections)[*m_currentCollectionIndex];
    if (collection.gridWidth > 0) {
      int row = targetIndex / collection.gridWidth;
      int col = targetIndex % collection.gridWidth;

      int viewportH = m_itemScrollArea->viewport()->height();
      int viewportW = m_itemScrollArea->viewport()->width();

      if (viewportH > 0) {
        int itemY =
            UIConstants::GRID_MARGINS +
            (row * (collection.itemHeight + collection.verticalSpacing));
        int targetY = itemY + (collection.itemHeight / 2) - (viewportH / 2);
        targetY = qBound(0, targetY, qMax(0, targetY));
        stopArrowKeyAnimationIfRunning(verticalScrollBar);
        m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
        if (m_scrollManager != nullptr) {
          m_scrollManager->refreshSelectionOverlayState();
        }
        verticalScrollBar->setValue(targetY);
        QTimer::singleShot(0, this, [this]() {
          if (m_itemScrollArea) {
            m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll,
                                          false);
            if (m_scrollManager != nullptr) {
              m_scrollManager->refreshSelectionOverlayState();
            }
          }
        });
      }

      if (viewportW > 0 && (horizontalScrollBar != nullptr)) {
        int itemX =
            UIConstants::GRID_MARGINS +
            (col * (collection.itemWidth + collection.horizontalSpacing));
        int targetX = itemX + (collection.itemWidth / 2) - (viewportW / 2);
        targetX = qBound(0, targetX, qMax(0, targetX));
        horizontalScrollBar->setValue(targetX);
      }
    }
  }
}

// Computes whether the target selection lies on a different row
auto InteractionManager::computeIsNewRow(int currentSelection, int newSelection,
                                         int gridWidth) const -> bool {
  if (m_currentCollectionIndex == nullptr || m_collections == nullptr ||
      *m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size() || gridWidth <= 0) {
    return false;
  }
  const int currentRow =
      (currentSelection >= 0) ? currentSelection / gridWidth : -1;
  const int targetRow = newSelection / gridWidth;
  return currentRow != targetRow;
}

// Applies immediate centering suppression when offscreen or wrapping
void InteractionManager::applyImmediateCenterSuppression() {
  m_forceImmediateCenter = true;
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
    const qint64 until = QDateTime::currentMSecsSinceEpoch() +
                         UIConstants::ARROW_KEY_ANIMATION_SETTLE_MS;
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                  until);
  }
}

// Updates selection state and notifies dependent managers
void InteractionManager::updateSelectionForKeyMove(int newSelection) {
  m_allowArtworkDuringSelection = true;
  setProperty(PropertyKeys::SelectionSuppressed, true);
  setProperty(PropertyKeys::PendingSelectionIndex, newSelection);
  m_selectedItemIndex = newSelection;
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(newSelection);
  }
  const QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(newSelection, subs);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(newSelection);
  }
  selectItemByIndex(newSelection, true);
}

// Performs visibility adjustments post-selection update
void InteractionManager::performVisibilityForKeyMove(bool isNewRow,
                                                     int newSelection) {
  if (isNewRow) {
    centerItemVertically(newSelection, false);
  } else {
    ensureHorizontallyVisible(newSelection);
  }
}

// Restores a viewed collection after search is cleared (single reload path) and
// schedules scrollbar recovery
void InteractionManager::restoreViewedCollectionAfterSearchClear() {
  if (m_navigationManager == nullptr) {
    return;
  }

  if (m_searchManager && m_searchManager->debounceTimer() != nullptr) {
    m_searchManager->debounceTimer()->stop();
  }
  if (m_searchManager) {
    auto &conn = m_searchManager->itemsLoadedConnection();
    if (conn != QMetaObject::Connection()) {
      QObject::disconnect(conn);
      conn = QMetaObject::Connection();
    }
  }

  const int fallback =
      ((m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1);
  const int targetIndex =
      (m_searchManager ? m_searchManager->preSearchCollectionIndex() : -1);
  const int finalTarget = (targetIndex >= 0 ? targetIndex : fallback);
  if (finalTarget < 0) {
    if (m_searchManager) {
      m_searchManager->setSearchActive(false);
    }
    return;
  }

  m_navigationManager->filterItems(QString());
  m_navigationManager->safeReloadCollection(finalTarget);
  initializeSearchModeForCurrentCollection();

  int sel = m_searchManager ? m_searchManager->preSearchSelectedIndex() : -1;
  if (sel < 0 && (m_settingsManager != nullptr)) {
    sel = m_settingsManager->getLastSelectedItem(finalTarget);
  }
  if (sel >= 0) {
    m_navigationManager->scheduleSelectionRestore(
        sel, UIConstants::SELECTION_RESTORE_STEPS,
        UIConstants::SELECTION_RESTORE_STEP_DELAY_MS,
        UIConstants::SELECTION_RESTORE_MAX_DELAY_MS);
  }

  scheduleScrollbarRecovery();
  if (m_searchManager) {
    m_searchManager->setSearchActive(false);
  }
}

// Updates the selected file path and safely refreshes sidebar metadata without
// using stale widget pointers
void InteractionManager::updateFilePathForSelection(
    int index, const QList<int> &subcollections) {
  if (m_selectionManager) {
    m_selectionManager->updateFilePathForSelection(index, subcollections);
    // Keep local state in sync
    m_selectedFilePath = m_selectionManager->selectedFilePath();
  }
}

void InteractionManager::clearSelection() {
  if (m_selectionManager) {
    m_selectionManager->clearSelection(m_isShuttingDown);
    // Keep local state in sync for backward compatibility during refactor
    m_selectedMediaItem = m_selectionManager->selectedWidget();
    m_selectedFilePath = m_selectionManager->selectedFilePath();
    m_selectedItemIndex = m_selectionManager->currentSelectedIndex();
  }
}

auto InteractionManager::currentSelectedIndex() const -> int {
  if (m_selectionManager) {
    return m_selectionManager->currentSelectedIndex();
  }
  return m_selectedItemIndex;
}

auto InteractionManager::getSelectedMediaItem() const -> MediaItemWidget * {
  if (m_selectionManager) {
    return m_selectionManager->selectedWidget();
  }
  return m_selectedMediaItem;
}

void InteractionManager::setSelectedMediaItem(MediaItemWidget *widget) {
  m_selectedMediaItem = widget;
  if (m_selectionManager) {
    m_selectionManager->setSelectedWidget(widget);
  }
}

auto InteractionManager::selectedFilePath() const -> QString {
  if (m_selectionManager) {
    return m_selectionManager->selectedFilePath();
  }
  return m_selectedFilePath;
}

auto InteractionManager::isRestoringSelection() const -> bool {
  if (m_selectionManager) {
    return m_selectionManager->isRestoringSelection();
  }
  return m_restoringSelection;
}

auto InteractionManager::targetRestoreIndex() const -> int {
  if (m_selectionManager) {
    return m_selectionManager->targetRestoreIndex();
  }
  return m_targetRestoreIndex;
}

auto InteractionManager::forceImmediateCenter() const -> bool {
  if (m_selectionManager) {
    return m_selectionManager->forceImmediateCenter();
  }
  return m_forceImmediateCenter;
}

// Returns the active grid width; prefers ScrollManager's current context to
// handle nested/filtered views
auto InteractionManager::getCurrentGridWidth() const -> int {
  if (m_scrollManager != nullptr) {
    int currentWidth = m_scrollManager->getCurrentGridWidth();
    if (currentWidth > 0) {
      return currentWidth;
    }
  }
  if ((m_collections == nullptr) || (m_currentCollectionIndex == nullptr)) {
    return UIConstants::DEFAULT_GRID_WIDTH;
  }
  if (*m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    return (*m_collections)[*m_currentCollectionIndex].gridWidth;
  }
  return UIConstants::DEFAULT_GRID_WIDTH;
}

auto InteractionManager::processEnterOrReturnKey(int totalItems) -> bool {
  const int currentSelection =
      (m_selectedItemIndex < 0) ? 0 : m_selectedItemIndex;
  if (currentSelection < 0 || currentSelection >= totalItems) {
    return true;
  }
  const QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  if (currentSelection < subs.size()) {
    return handleEnterOnSubcollection(currentSelection, subs);
  }
  return handleEnterOnItem(currentSelection, totalItems);
}

auto InteractionManager::handleEnterOnSubcollection(int currentSelection,
                                                    const QList<int> &subs)
    -> bool {
  saveCurrentSelection();
  const int subIdx = subs[currentSelection];
  if (m_navigationManager != nullptr) {
    if (*m_currentCollectionIndex >= 0 &&
        *m_currentCollectionIndex < m_collections->size()) {
      m_navigationManager->m_navigationStack.append(*m_currentCollectionIndex);
      m_navigationManager->m_navigationDepth++;
    }
    clearSelectionAndFocus();
    if (m_mainWindow->getMetadataSidebar() != nullptr) {
      m_mainWindow->getMetadataSidebar()->clearMetadata();
    }
    const bool success = m_navigationManager->showCollectionItems(subIdx);
    if (!success) {
      if (!m_navigationManager->m_navigationStack.isEmpty()) {
        m_navigationManager->m_navigationStack.removeLast();
      }
      m_navigationManager->m_navigationDepth =
          qMax(0, m_navigationManager->m_navigationDepth - 1);
      selectItemByIndex(currentSelection, true);
      if (m_itemsPage != nullptr) {
        m_itemsPage->setFocus();
      }
    } else {
      constexpr int kHorizontalCenterDelayMs = 600;
      QTimer::singleShot(kHorizontalCenterDelayMs, m_mainWindow, [this]() {
        if (!QApplication::closingDown() && m_scrollManager) {
          m_scrollManager->centerHorizontalScrollbar(*m_currentCollectionIndex,
                                                     *m_collections);
        }
      });
    }
  }
  return true;
}

auto InteractionManager::handleEnterOnItem(int currentSelection,
                                           int /*totalItems*/) -> bool {
  QString path = m_selectedFilePath;
  if (path.isEmpty() && (m_scrollManager != nullptr)) {
    path = m_scrollManager->filePathForVisualIndex(currentSelection);
  }
  if (!path.isEmpty()) {
    saveCurrentSelection();
    const int cIdx = ((m_databaseManager != nullptr)
                          ? m_databaseManager->getCollectionIndexForFile(path)
                          : -1);
    const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
    launchItemWithCollection(path, ownerIdx);
  }
  return true;
}

auto InteractionManager::isItemOffscreen(int selection, int gridWidth) const
    -> bool {
  if (m_itemScrollArea == nullptr || m_currentCollectionIndex == nullptr ||
      m_collections == nullptr || selection < 0) {
    return false;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size() || gridWidth <= 0) {
    return false;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vbar = m_itemScrollArea->verticalScrollBar();
  if (vbar == nullptr) {
    return false;
  }
  const int viewportH = m_itemScrollArea->viewport()->height();
  if (viewportH <= 0) {
    return false;
  }
  const int itemY = GridUtils::computeItemY(
      selection, gridWidth, collection.itemHeight, collection.verticalSpacing,
      UIConstants::GRID_MARGINS);
  const int visibleTop = vbar->value();
  const int visibleBottom = visibleTop + viewportH;
  return (itemY + collection.itemHeight) <= visibleTop ||
         itemY >= visibleBottom;
}

void InteractionManager::applyMinorHorizontalSuppress() {
  constexpr qint64 kMinorHorizSuppressMs = 220;
  constexpr int kMinorHorizSuppressClearMs = 240;
  m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
  const qint64 until =
      QDateTime::currentMSecsSinceEpoch() + kMinorHorizSuppressMs;
  m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                until);
  QPointer<QScrollArea> scrollAreaPtr = m_itemScrollArea;
  QTimer::singleShot(kMinorHorizSuppressClearMs, this, [scrollAreaPtr]() {
    if (scrollAreaPtr) {
      scrollAreaPtr->setProperty(PropertyKeys::SuppressArrowCenter, false);
    }
  });
}

void InteractionManager::setPendingSelectionIfNeeded(bool condition,
                                                     int newSelection) {
  if (condition) {
    setProperty(PropertyKeys::SelectionSuppressed, true);
    setProperty(PropertyKeys::PendingSelectionIndex, newSelection);
  }
}

void InteractionManager::updateSelectionStateAfterMove(int newSelection) {
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(newSelection, subs);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(newSelection);
  }
  selectItemByIndex(newSelection, true);
}

void InteractionManager::centerItemVertically(int index, bool immediate) {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }
  if (index < 0) {
    return;
  }
  if (shouldDeferCenterNow(immediate, index)) {
    return;
  }

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *verticalScrollBar = m_itemScrollArea->verticalScrollBar();
  if (verticalScrollBar == nullptr) {
    return;
  }
  int viewportHeight = m_itemScrollArea->viewport()->height();
  if (viewportHeight <= 0) {
    return;
  }

  bool clickScroll = property(PropertyKeys::ClickScroll).toBool();
  bool clickHoldAdv = property(PropertyKeys::ClickHoldAdvancing).toBool();
  bool forceClickAnim = property(PropertyKeys::ClickForceAnim).toBool();

  int targetY = computeTargetYForIndex(
      index, gridWidth, collection.itemHeight, collection.verticalSpacing,
      viewportHeight, verticalScrollBar->maximum());

  bool forceImmediate = computeForceImmediate(immediate);
  if (shouldEarlyReturnUserScroll(forceImmediate)) {
    return;
  }

  int targetYUnboundedLocal =
      GridUtils::computeItemY(index, gridWidth, collection.itemHeight,
                              collection.verticalSpacing,
                              UIConstants::GRID_MARGINS) +
      (collection.itemHeight / 2) - (viewportHeight / 2);
  if (handlePendingInitialCenterIfNeeded(verticalScrollBar, index,
                                         targetYUnboundedLocal, immediate)) {
    return;
  }

  int curY = verticalScrollBar->value();
  int distance = qAbs(targetY - curY);

  int currentRow = (gridWidth > 0 ? index / gridWidth : -1);
  int smallThreshold = computeSmallThreshold(currentRow);

  bool useSmooth = forceClickAnim ||
                   (m_continuousScrollActive && !m_instantPositioning &&
                    !m_wrapSequenceActive) ||
                   property(PropertyKeys::ClickContinuous).toBool() ||
                   property(PropertyKeys::KeyContinuous).toBool();

  if (!forceImmediate && distance <= smallThreshold) {
    if (handleSmallMovementEarlyReturn(distance, clickScroll, index,
                                       currentRow)) {
      return;
    }
  }

  if (maybeHandleImmediateCenter(distance <= 1, useSmooth, forceImmediate,
                                 forceClickAnim, verticalScrollBar, targetY,
                                 index, currentRow)) {
    return;
  }

  ensureVAnimCreated(verticalScrollBar);

  if (handleExistingVerticalAnimIfRunning(verticalScrollBar, targetY,
                                          clickScroll, clickHoldAdv, curY,
                                          distance)) {
    return;
  }

  int duration = computeVerticalCenterDuration(distance, m_repeating);

  if (forceClickAnim && distance <= 1) {
    adjustForForceClickZeroDistance(verticalScrollBar, targetY, curY, distance,
                                    duration, forceClickAnim);
  }

  configureAndStartVerticalAnimation(verticalScrollBar, curY, targetY, duration,
                                     clickScroll, clickHoldAdv);
}

auto InteractionManager::computeTargetYForIndex(int index, int gridWidth,
                                                int itemHeight,
                                                int verticalSpacing,
                                                int viewportHeight,
                                                int scrollbarMax) -> int {
  int itemY = GridUtils::computeItemY(
      index, gridWidth, itemHeight, verticalSpacing, UIConstants::GRID_MARGINS);
  int targetYUnbounded = itemY + (itemHeight / 2) - (viewportHeight / 2);
  return qBound(0, targetYUnbounded, scrollbarMax);
}

auto InteractionManager::computeForceImmediate(bool immediate) const -> bool {
  return immediate || m_forceImmediateCenter || m_isWrappingNavigation ||
         m_restoringSelection || m_instantPositioning || m_wrapSequenceActive;
}

auto InteractionManager::computeSmallThreshold(int currentRow) const -> int {
  constexpr int kSmallThresholdSameRow = 8;
  constexpr int kSmallThresholdOtherRow = 2;
  return (m_lastSelectedRow >= 0 && m_lastSelectedRow == currentRow)
             ? kSmallThresholdSameRow
             : kSmallThresholdOtherRow;
}

auto InteractionManager::handleSmallMovementEarlyReturn(
    int /*distance*/, bool clickScroll, int index, int currentRow) -> bool {
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                     ? property(PropertyKeys::PendingSelectionIndex).toInt()
                     : index;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
  if (clickScroll) {
    setProperty(PropertyKeys::ClickScroll, false);
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, false);
  }
  m_instantPositioning = false;
  m_lastSelectedRow = currentRow;
  return true;
}

auto InteractionManager::shouldDeferCenterNow(bool immediate, int index) const
    -> bool {
  if (immediate) {
    return false;
  }
  if (!property(PropertyKeys::DeferCenterOnClick).toBool()) {
    return false;
  }
  int defIdx = property(PropertyKeys::DeferredCenterIndex).toInt();
  return (defIdx < 0 || defIdx == index);
}

auto InteractionManager::shouldEarlyReturnUserScroll(bool forceImmediate) const
    -> bool {
  return m_itemScrollArea->property(PropertyKeys::UserScrollActive).toBool() &&
         !forceImmediate;
}

auto InteractionManager::handlePendingInitialCenterIfNeeded(
    QScrollBar *verticalScrollBar, int index, int targetYUnbounded,
    bool immediate) -> bool {
  Q_UNUSED(targetYUnbounded);
  if (verticalScrollBar->maximum() == 0 && !immediate) {
    if (!property(PropertyKeys::PendingInitialCenter).toBool()) {
      setProperty(PropertyKeys::PendingInitialCenter, true);
      QTimer::singleShot(
          UIConstants::INITIAL_CENTER_SCROLL_DELAY_MS, this, [this, index]() {
            setProperty(PropertyKeys::PendingInitialCenter, false);
            if (!QApplication::closingDown()) {
              centerItemVertically(index, false);
            }
          });
    }
    return true;
  }
  return false;
}

void InteractionManager::adjustForForceClickZeroDistance(
    QScrollBar *verticalScrollBar, int targetY, int &curY, int &distance,
    int &duration, bool /*forceClickAnim*/) {
  if (targetY == curY) {
    int adjust = (targetY > 0 ? -1 : 1);
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
    int startVal = qBound(0, targetY + adjust, verticalScrollBar->maximum());
    verticalScrollBar->setValue(startVal);
    QTimer::singleShot(0, this, [this]() {
      if (m_itemScrollArea) {
        m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, false);
        if (m_scrollManager != nullptr) {
          m_scrollManager->refreshSelectionOverlayState();
        }
      }
    });
    curY = startVal;
    distance = qAbs(targetY - curY);
    duration = computeVerticalCenterDuration(distance, m_repeating);
  }
}

auto InteractionManager::handleImmediateCenterForEnsureVisible(int index)
    -> bool {
  if (!m_forceImmediateCenter) {
    return false;
  }
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return false;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return false;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if ((vScrollBar == nullptr) || (hScrollBar == nullptr)) {
    return false;
  }
  QRect viewport = m_itemScrollArea->viewport()->rect();
  int viewportWidth = viewport.width();
  int viewportHeight = viewport.height();
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0 || viewportHeight <= 0) {
    return false;
  }
  int hSpacing = (m_scrollManager != nullptr)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::GRID_MARGINS;
  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth,
                                      hSpacing, margins);
  int itemY = GridUtils::computeItemY(index, gridWidth, collection.itemHeight,
                                      collection.verticalSpacing, margins);

  int targetY = GridUtils::computeCenterTarget(
      itemY, collection.itemHeight, viewportHeight, vScrollBar->maximum());
  int targetX = GridUtils::computeCenterTarget(
      itemX, collection.itemWidth, viewportWidth, hScrollBar->maximum());
  vScrollBar->setValue(targetY);
  hScrollBar->setValue(targetX);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
  }
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
  m_forceImmediateCenter = false;
  m_deferredCenterPending = false;
  return true;
}

auto InteractionManager::maybeHandleImmediateCenter(
    bool distanceSmall, bool useSmooth, bool forceImmediate,
    bool forceClickAnim, QScrollBar *verticalScrollBar, int targetY, int index,
    int currentRow) -> bool {
  if (((distanceSmall && !useSmooth) || forceImmediate) && !forceClickAnim) {
    if (handleImmediateCenterPath(verticalScrollBar, targetY, index,
                                  currentRow)) {
      return true;
    }
  }
  return false;
}

auto InteractionManager::handleExistingVerticalAnimIfRunning(
    QScrollBar *verticalScrollBar, int targetY, bool clickScroll,
    bool clickHoldAdv, int &curY, int &distance) -> bool {
  if (m_vScrollAnim->state() == QAbstractAnimation::Running) {
    if (clickScroll && !clickHoldAdv) {
      m_vScrollAnim->setEndValue(targetY);
      return true;
    }
    m_vScrollAnim->stop();
    curY = verticalScrollBar->value();
    distance = qAbs(targetY - curY);
  }
  return false;
}

auto InteractionManager::handleImmediateCenterPath(
    QScrollBar *verticalScrollBar, int targetY, int index, int currentRow)
    -> bool {
  stopActiveVerticalAnims(verticalScrollBar);
  setProgrammaticScrollGuarded(true);
  setScrollValueAndUpdateSelection(verticalScrollBar, targetY, index);
  setProgrammaticScrollGuarded(false);
  finalizeImmediateCenteringState(index, currentRow);
  clearArtworkSuppressionViewportUpdateIfNeeded();
  clearArrowCenterSuppressionWhenDue();
  return true;
}

void InteractionManager::stopActiveVerticalAnims(
    QScrollBar *verticalScrollBar) {
  if ((m_vScrollAnim != nullptr) &&
      m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
  }
  if (auto *arrowKeyAnim = verticalScrollBar->findChild<QPropertyAnimation *>(
          "arrowKeyScrollAnim")) {
    if (arrowKeyAnim->state() == QAbstractAnimation::Running) {
      arrowKeyAnim->stop();
    }
  }
}

void InteractionManager::setProgrammaticScrollGuarded(bool enable) {
  if (!m_itemScrollArea) {
    return;
  }
  if (enable) {
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  } else {
    QPointer<QScrollArea> scrollAreaPtr = m_itemScrollArea;
    QTimer::singleShot(0, this, [this, scrollAreaPtr]() {
      if (scrollAreaPtr) {
        scrollAreaPtr->setProperty(PropertyKeys::ProgrammaticScroll, false);
        if (m_scrollManager != nullptr) {
          m_scrollManager->refreshSelectionOverlayState();
        }
      }
    });
  }
}

void InteractionManager::setScrollValueAndUpdateSelection(
    QScrollBar *verticalScrollBar, int targetY, int index) {
  verticalScrollBar->setValue(targetY);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                     ? property(PropertyKeys::PendingSelectionIndex).toInt()
                     : index;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
}

void InteractionManager::clearArtworkSuppressionViewportUpdateIfNeeded() {
  if (m_itemScrollArea &&
      m_itemScrollArea->property(PropertyKeys::SuppressArtwork).toBool() &&
      !m_repeating) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
      if (!QApplication::closingDown() && m_artworkManager) {
        m_artworkManager->updateViewportArtwork();
      }
    });
  }
}

void InteractionManager::clearArrowCenterSuppressionWhenDue() {
  if (!m_itemScrollArea) {
    return;
  }
  qint64 until =
      m_itemScrollArea->property(PropertyKeys::SuppressArrowCenterUntilMs)
          .toLongLong();
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (until > now) {
    qint64 delay = until - now;
    QPointer<QScrollArea> scrollAreaPtr = m_itemScrollArea;
    constexpr qint64 kMaxArrowCenterSuppressClearMs = 1000;
    QTimer::singleShot(
        static_cast<int>(qMin<qint64>(delay, kMaxArrowCenterSuppressClearMs)),
        this, [scrollAreaPtr]() {
          if (scrollAreaPtr) {
            scrollAreaPtr->setProperty(PropertyKeys::SuppressArrowCenter,
                                       false);
          }
        });
  } else {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, false);
  }
}

void InteractionManager::finalizeImmediateCenteringState(int index,
                                                         int currentRow) {
  if (m_restoringSelection && index == m_targetRestoreIndex) {
    m_restoringSelection = false;
    m_targetRestoreIndex = -1;
  }
  m_isWrappingNavigation = false;
  m_forceImmediateCenter = false;
  if (m_wrapSequenceActive) {
    m_wrapSequenceActive = false;
    m_continuousScrollActive = true;
  }
  if (property(PropertyKeys::ClickScroll).toBool()) {
    setProperty(PropertyKeys::ClickScroll, false);
  }
  if (!m_repeating && !m_physicalKeyDown &&
      !property(PropertyKeys::ClickContinuous).toBool() &&
      !property(PropertyKeys::KeyContinuous).toBool()) {
    m_continuousScrollActive = false;
  }
  m_instantPositioning = false;
  m_lastSelectedRow = currentRow;
}

void InteractionManager::ensureVAnimCreated(QScrollBar *vScrollBar) {
  if (m_vScrollAnim == nullptr) {
    m_vScrollAnim = new QPropertyAnimation(vScrollBar, "value", this);
  }
}

void InteractionManager::configureAndStartVerticalAnimation(
    QScrollBar *vScrollBar, int curY, int targetY, int duration,
    bool clickScroll, bool clickHoldAdv) {
  m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  m_vScrollAnim->setStartValue(curY);
  m_vScrollAnim->setEndValue(targetY);
  m_vScrollAnim->setDuration(duration);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(m_vScrollAnim, &QPropertyAnimation::valueChanged, this,
          [this, clickScroll, clickHoldAdv]() {
            updateVirtualViewAndSelectionDuringVAnim(clickScroll, clickHoldAdv);
          });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this,
          [this]() { onVScrollAnimationFinished(); });

  m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
  if (m_scrollManager != nullptr) {
    m_scrollManager->refreshSelectionOverlayState();
  }
  m_vScrollAnim->start();
}

void InteractionManager::updateVirtualViewAndSelectionDuringVAnim(
    bool clickScroll, bool clickHoldAdv) {
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                     ? property(PropertyKeys::PendingSelectionIndex).toInt()
                     : m_selectedItemIndex;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
  if (clickScroll && !clickHoldAdv && !m_mouseHoldScrolling &&
      qAbs(m_vScrollAnim->currentValue().toInt() -
           m_vScrollAnim->endValue().toInt()) < 3) {
    setProperty(PropertyKeys::ClickScroll, false);
  }
}

void InteractionManager::onVScrollAnimationFinished() {
  setProperty(PropertyKeys::ClickForceAnim, false);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                     ? property(PropertyKeys::PendingSelectionIndex).toInt()
                     : m_selectedItemIndex;
    if (idxDyn >= 0) {
      m_scrollManager->updateSelectionForIndex(idxDyn);
    }
  }
  if (!m_repeating && !m_physicalKeyDown &&
      !property(PropertyKeys::ClickContinuous).toBool() &&
      !property(PropertyKeys::KeyContinuous).toBool()) {
    m_continuousScrollActive = false;
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, false);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  if (m_itemScrollArea && !m_repeating && !m_physicalKeyDown) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
      if (!QApplication::closingDown() && m_artworkManager) {
        m_artworkManager->updateViewportArtwork();
      }
    });
  }
  setProperty(PropertyKeys::ClickScroll, false);
  m_instantPositioning = false;
  int gridWidthLocal = getCurrentGridWidth();
  int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                   ? property(PropertyKeys::PendingSelectionIndex).toInt()
                   : m_selectedItemIndex;
  if (gridWidthLocal > 0 && idxDyn >= 0) {
    m_lastSelectedRow = idxDyn / gridWidthLocal;
  }
}

// Selects an item by index, updates visuals, persists selection even when
// suppressed, and optionally centers
void InteractionManager::selectItemByIndex(int index,
                                           bool allowHorizontalScroll) {
  Q_UNUSED(allowHorizontalScroll);
  if (m_scrollManager == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr || m_itemScrollArea == nullptr) {
    return;
  }
  if ((m_collections == nullptr) || *m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }

  const QStringList &filePaths = m_scrollManager->getFilePaths();
  QList<int> subcollections = getSubcollections(*m_currentCollectionIndex);
  int totalItems = subcollections.size() + filePaths.size();
  if (index < 0 || index >= totalItems) {
    return;
  }

  bool selectionChangedLocal = (index != m_selectedItemIndex);
  m_selectedItemIndex = index;
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(index);
  }
  if (selectionChangedLocal) {
    setProperty(PropertyKeys::UserFreeScroll, false);
  }

  MediaItemWidget *widget = nullptr;
  if (m_selectionManager) {
    widget = m_selectionManager->widgetForIndex(index);
  } else {
    const auto &activeWidgets = m_scrollManager->getActiveWidgets();
    widget = activeWidgets.value(index, nullptr);
  }
  bool suppressed =
      property(PropertyKeys::SelectionSuppressed).toBool() &&
      property(PropertyKeys::PendingSelectionIndex).toInt() == index;
  bool skipCenter = property(PropertyKeys::SuppressInitialClickCenter).toBool();

  if (widget != nullptr) {
    m_selectedMediaItem = widget;
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

  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(m_selectedItemIndex);
    if (property(PropertyKeys::ClickHoldAdvancing).toBool()) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  emit selectionChanged(m_selectedItemIndex);

  if (suppressed) {
    persistSuppressedSelectionAndMaybeCenter(index, subcollections, skipCenter);
  }

  if (skipCenter) {
    setProperty(PropertyKeys::SuppressInitialClickCenter, false);
  }
}

void InteractionManager::persistSuppressedSelectionAndMaybeCenter(
    int index, const QList<int> &subcollections, bool skipCenter) {
  m_allowArtworkDuringSelection = true;
  bool deferCenter =
      property(PropertyKeys::DeferCenterOnClick).toBool() &&
      property(PropertyKeys::DeferredCenterIndex).toInt() == index;
  if (!deferCenter && !skipCenter) {
    centerItemVertically(index, false);
  }
  int curColl =
      ((m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1);
  if ((m_mainWindow != nullptr) && curColl >= 0 &&
      curColl < m_mainWindow->m_collections.size()) {
    QString title;
    if (m_selectionManager) {
      title = m_selectionManager->titleForIndex(index, subcollections);
      m_selectionManager->persistSelection(curColl, index, title);
    } else {
      // Fallback if SelectionManager not available
      m_mainWindow->getSettingsManager()->setLastSelectedItem(curColl, index);
      QString collectionName = m_mainWindow->m_collections[curColl].name;
      if (index < subcollections.size()) {
        int subIdx = subcollections[index];
        if (subIdx >= 0 && subIdx < m_mainWindow->m_collections.size()) {
          title = m_mainWindow->m_collections[subIdx].name;
        }
      } else {
        QString path = m_selectedFilePath;
        if (path.isEmpty() && (m_scrollManager != nullptr)) {
          path = m_scrollManager->filePathForVisualIndex(index);
        }
        if (!path.isEmpty()) {
          title =
              QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
        }
      }
      if (m_sessionManager) {
        m_sessionManager->setLastSelected(collectionName, index, title);
      }
    }
  }
  QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
    if (!QApplication::closingDown() && m_artworkManager) {
      m_artworkManager->updateViewportArtwork();
    }
  });
}

void InteractionManager::handleWidgetClicked(MediaItemWidget *widget,
                                             const QString &filePath) {
  cancelPendingSelectionRestore();

  if ((widget == nullptr) || (m_scrollManager == nullptr) ||
      (m_collections == nullptr) || (m_currentCollectionIndex == nullptr)) {
    return;
  }
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }

  int visualIndex = -1;
  const auto &active = m_scrollManager->getActiveWidgets();
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() == widget) {
      visualIndex = it.key();
      break;
    }
  }
  if (visualIndex < 0) {
    return;
  }

  processSingleClickSelection(visualIndex, filePath, true);
}

// Ensures the selected item is horizontally visible, adapting behavior for hold
// scrolling
void InteractionManager::ensureHorizontallyVisible(int index) {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }
  if (index < 0) {
    return;
  }

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if (hScrollBar == nullptr) {
    return;
  }

  int hSpacing = (m_scrollManager != nullptr)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::GRID_MARGINS;
  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth,
                                      hSpacing, margins);

  QRect viewport = m_itemScrollArea->viewport()->rect();
  int curX = hScrollBar->value();
  int viewportWidth = viewport.width();
  int targetX = curX;

  if (itemX < curX + margins) {
    targetX = qMax(0, itemX - margins);
  } else if (itemX + collection.itemWidth > curX + viewportWidth - margins) {
    targetX =
        qMax(0, qMin(itemX + collection.itemWidth - viewportWidth + margins,
                     hScrollBar->maximum()));
  }

  if (targetX == curX) {
    if (m_scrollManager != nullptr) {
      m_scrollManager->updateVirtualView();
    }
    return;
  }

  bool hold = property(PropertyKeys::HorizHoldActive).toBool();

  initHorizontalAnimIfNeeded(hScrollBar);

  if (hold) {
    if (m_hScrollAnim->state() == QAbstractAnimation::Running) {
      m_hScrollAnim->stop();
    }
    int startX = hScrollBar->value();
    animateHorizontalHold(hScrollBar, startX, targetX);
  } else {
    animateHorizontalSmooth(hScrollBar, curX, targetX);
  }

  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
  }
}

void InteractionManager::initHorizontalAnimIfNeeded(QScrollBar *hScrollBar) {
  if (m_hScrollAnim == nullptr) {
    m_hScrollAnim = new QPropertyAnimation(hScrollBar, "value", this);
    m_hScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::disconnect(m_hScrollAnim, nullptr, this, nullptr);
    connect(m_hScrollAnim, &QPropertyAnimation::valueChanged, this, [this]() {
      if (m_scrollManager != nullptr) {
        m_scrollManager->updateVirtualView();
      }
    });
    connect(m_hScrollAnim, &QPropertyAnimation::finished, this, [this]() {
      if (m_gridContainer) {
        m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
        if (m_scrollManager != nullptr) {
          m_scrollManager->refreshSelectionOverlayState();
        }
      }
      if (m_scrollManager != nullptr) {
        m_scrollManager->updateVirtualView();
      }
    });
  }
}

void InteractionManager::animateHorizontalHold(QScrollBar *hScrollBar,
                                               int startX, int targetX) {
  int distance = qAbs(targetX - startX);
  constexpr double kPixelsPerSecond = 700.0;
  constexpr double kMillisecondsPerSecond = 1000.0;
  constexpr int kMinHoldDurationMs = 30;
  int duration = static_cast<int>(
      std::round((distance / kPixelsPerSecond) * kMillisecondsPerSecond));
  duration = std::max(duration, kMinHoldDurationMs);

  m_hScrollAnim->setEasingCurve(QEasingCurve::Linear);
  m_hScrollAnim->setStartValue(startX);
  m_hScrollAnim->setEndValue(targetX);
  m_hScrollAnim->setDuration(duration);

  if (m_gridContainer != nullptr) {
    m_gridContainer->setProperty(PropertyKeys::GlideAnimating, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  m_hScrollAnim->start();
  QTimer::singleShot(0, this, [this]() {
    if (m_itemScrollArea) {
      m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, false);
      if (m_scrollManager != nullptr) {
        m_scrollManager->refreshSelectionOverlayState();
      }
    }
  });
}

void InteractionManager::animateHorizontalSmooth(QScrollBar * /*hScrollBar*/,
                                                 int startX, int targetX) {
  if (m_hScrollAnim->state() == QAbstractAnimation::Running) {
    m_hScrollAnim->stop();
  }
  m_hScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  m_hScrollAnim->setStartValue(startX);
  m_hScrollAnim->setEndValue(targetX);
  m_hScrollAnim->setDuration(UIConstants::HSCROLL_ANIM_DURATION_MS);
  if (m_gridContainer != nullptr) {
    m_gridContainer->setProperty(PropertyKeys::GlideAnimating, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  m_hScrollAnim->start();
}

// Ensures the currently selected item stays visible without forcing vertical
// centering if unnecessary
void InteractionManager::ensureItemVisible(int index,
                                           bool allowHorizontalScroll) {
  if (shouldExitEnsureItemVisible(index)) {
    return;
  }

  if (property(PropertyKeys::DeferCenterOnClick).toBool() &&
      !m_physicalKeyDown) {
    return;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return;
  }

  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  QScrollBar *hScrollBar = m_itemScrollArea->horizontalScrollBar();
  if ((vScrollBar == nullptr) || (hScrollBar == nullptr)) {
    return;
  }

  int hSpacing = (m_scrollManager != nullptr)
                     ? m_scrollManager->getEffectiveHorizontalSpacing()
                     : collection.horizontalSpacing;
  int margins = UIConstants::GRID_MARGINS;

  int itemX = GridUtils::computeItemX(index, gridWidth, collection.itemWidth,
                                      hSpacing, margins);
  int itemY = GridUtils::computeItemY(index, gridWidth, collection.itemHeight,
                                      collection.verticalSpacing, margins);

  QRect viewport = m_itemScrollArea->viewport()->rect();
  int curX = hScrollBar->value();
  int curY = vScrollBar->value();
  int viewportWidth = viewport.width();
  int viewportHeight = viewport.height();
  if (viewportHeight <= 0) {
    return;
  }

  bool isRepeating = m_repeating && m_physicalKeyDown;
  // Horizontal repeating flag was only used to select the same action; no-op
  // retained

  if (handleImmediateCenterForEnsureVisible(index)) {
    return;
  }

  int targetX = allowHorizontalScroll
                    ? computeHorizontalTargetX(itemX, collection.itemWidth,
                                               curX, viewportWidth, margins,
                                               hScrollBar->maximum())
                    : curX;
  bool needH = (targetX != curX);

  bool needV = false;
  int desiredY = computeDesiredYForVisibility(
      itemY, collection.itemHeight, curY, viewportHeight, margins, needV);

  if (!needV && !needH) {
    updateViewAndRowAfterVisibility(index, gridWidth);
    return;
  }

  if (needH) {
    hScrollBar->setValue(targetX);
  }

  if (!needV) {
    updateViewAndRowAfterVisibility(index, gridWidth);
    return;
  }

  int startVal = curY;
  int endVal = desiredY;
  if (startVal == endVal) {
    updateViewAndRowAfterVisibility(index, gridWidth);
    return;
  }

  // Duration is computed inside startEnsureVisibleVAnim; no local computation
  // needed here.

  if (m_vScrollAnim == nullptr) {
    m_vScrollAnim = new QPropertyAnimation(vScrollBar, "value", this);
    m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  }
  if (m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
    startVal = vScrollBar->value();
  }

  startEnsureVisibleVAnim(vScrollBar, startVal, endVal, isRepeating);
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
}

auto InteractionManager::computeHorizontalTargetX(int itemX,
                                                  int collectionItemWidth,
                                                  int curX, int viewportWidth,
                                                  int margins, int scrollMax)
    -> int {
  int targetX = curX;
  if (itemX < curX + margins) {
    targetX = qMax(0, itemX - margins);
  } else if (itemX + collectionItemWidth > curX + viewportWidth - margins) {
    targetX =
        qMax(0, qMin(itemX + collectionItemWidth - viewportWidth + margins,
                     scrollMax));
  }
  return targetX;
}

auto InteractionManager::computeDesiredYForVisibility(int itemY, int itemHeight,
                                                      int curY, int viewportH,
                                                      int margins, bool &needV)
    -> int {
  int visibleTop = curY;
  int visibleBottom = curY + viewportH;
  int desiredY = curY;
  needV = false;
  if (itemY < visibleTop + margins) {
    desiredY = qMax(0, itemY - margins);
    needV = true;
  } else if (itemY + itemHeight > visibleBottom - margins) {
    desiredY = qMax(0, itemY + itemHeight + margins - viewportH);
    needV = true;
  }
  return desiredY;
}

void InteractionManager::updateViewAndRowAfterVisibility(int index,
                                                         int gridWidth) {
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
  }
  m_lastSelectedRow = GridUtils::computeItemRow(index, gridWidth);
}

auto InteractionManager::shouldExitEnsureItemVisible(int index) const -> bool {
  if (QApplication::closingDown() ||
      ((m_mainWindow != nullptr) && m_mainWindow->isShuttingDown())) {
    return true;
  }
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return true;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return true;
  }
  if (index < 0) {
    return true;
  }
  return false;
}

void InteractionManager::startEnsureVisibleVAnim(QScrollBar *vScrollBar,
                                                 int startVal, int endVal,
                                                 bool isRepeating) {
  constexpr int kRepeatRecenterDurationMs = 140;
  int duration =
      isRepeating
          ? kRepeatRecenterDurationMs
          : computeVerticalCenterDuration(qAbs(endVal - startVal), false);

  if (m_vScrollAnim == nullptr) {
    m_vScrollAnim = new QPropertyAnimation(vScrollBar, "value", this);
    m_vScrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  }
  if (m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
  }

  m_vScrollAnim->setStartValue(startVal);
  m_vScrollAnim->setEndValue(endVal);
  m_vScrollAnim->setDuration(duration);

  QObject::disconnect(m_vScrollAnim, nullptr, this, nullptr);
  connect(m_vScrollAnim, &QPropertyAnimation::valueChanged, this, [this]() {
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
  });
  connect(m_vScrollAnim, &QPropertyAnimation::finished, this, [this]() {
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
  });

  m_vScrollAnim->start();
}

// Ensures vertical scrollbar policy matches collection settings after
// content/metrics are established
void InteractionManager::ensureVerticalScrollbarPolicy() {
  if (!m_itemScrollArea || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  int idx = *m_currentCollectionIndex;
  if (idx < 0 || idx >= m_collections->size()) {
    return;
  }
  if (!(*m_collections)[idx].hideVerticalScrollbar) {
    m_itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

// Returns the direct child subcollection indices for a parent collection
auto InteractionManager::getSubcollections(int parentIndex) const
    -> QList<int> {
  if (m_selectionManager) {
    return m_selectionManager->getSubcollections(parentIndex);
  }
  if (m_collections == nullptr) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}

void InteractionManager::clearSelectionAndFocus() {
  clearSelection();
  if (m_itemsPage != nullptr) {
    m_itemsPage->setFocus();
  }
}

void InteractionManager::trySelectWidget(int index,
                                         const QList<int> &subcollections,
                                         int attempt) {
  if ((m_scrollManager == nullptr) || m_selectedItemIndex != index ||
      QApplication::closingDown()) {
    return;
  }
  constexpr int kMaxSelectAttempts = 10;
  if (attempt > kMaxSelectAttempts) {
    if (m_selectionManager) {
      m_selectionManager->setRestoringSelection(false);
      m_selectionManager->setTargetRestoreIndex(-1);
    }
    m_restoringSelection = false;
    m_targetRestoreIndex = -1;
    return;
  }

  MediaItemWidget *widget = nullptr;
  if (m_selectionManager) {
    widget = m_selectionManager->widgetForIndex(index);
  } else {
    const auto &activeWidgets = m_scrollManager->getActiveWidgets();
    widget = activeWidgets.value(index, nullptr);
  }

  if (widget != nullptr) {
    m_selectedMediaItem = widget;
    if (m_selectionManager) {
      m_selectionManager->setSelectedWidget(widget);
      m_selectionManager->applyWidgetSelection(widget);
    } else {
      widget->setSelected(true);
    }
    updateFilePathForSelection(index, subcollections);
    handleSuccessfulSelection(index);
  } else {
    m_scrollManager->updateVirtualView();
    QApplication::processEvents();
    constexpr int kSelectRetryBaseMs = 30;
    constexpr int kSelectRetryStepMs = 30;
    int delay = kSelectRetryBaseMs + (attempt * kSelectRetryStepMs);
    QTimer::singleShot(delay, this, [this, index, subcollections, attempt]() {
      if (!QApplication::closingDown()) {
        trySelectWidget(index, subcollections, attempt + 1);
      }
    });
  }
}

// Handles a single-click selection at visualIndex; scrolls immediately while
// preserving double-click launch behavior
void InteractionManager::processSingleClickSelection(
    int visualIndex, const QString &filePath, bool applyScrollAreaSuppression) {
  Q_UNUSED(applyScrollAreaSuppression);
  if ((m_scrollManager == nullptr) || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }

  qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  int dcInterval = QApplication::doubleClickInterval();

  setProperty(PropertyKeys::HorizAnimActive, false);
  setProperty(PropertyKeys::HorizAnimGen,
              property(PropertyKeys::HorizAnimGen).toInt() + 1);

  if (filePath.isEmpty()) {
    m_selectedFilePath.clear();
  } else {
    m_selectedFilePath = filePath;
  }
  m_physicalKeyDown = false;
  m_repeating = false;
  m_wrapSequenceActive = false;
  stopRepeat();

  const int pendingIndex =
      property(PropertyKeys::RowChangeFirstClickIndex).toInt();
  const qint64 pendingMs =
      property(PropertyKeys::RowChangeFirstClickMs).toLongLong();
  const bool pendingValid =
      (pendingIndex >= 0 && (nowMs - pendingMs) <= dcInterval);

  const int fromIndex = m_selectedItemIndex;
  const bool canAnimateHoriz =
      shouldAnimateHorizontalHop(fromIndex, visualIndex, gridWidth);

  if (canAnimateHoriz) {
    runHorizontalHopAnimation(fromIndex, visualIndex, nowMs);
    return;
  }

  if (shouldTreatAsNewRowForClick(visualIndex, gridWidth)) {
    handleNewRowClickSelection(visualIndex, nowMs);
  } else {
    const bool skipCenter = (pendingValid && pendingIndex == visualIndex);
    handleSameRowClickSelection(visualIndex, skipCenter, nowMs);
  }

  setProperty(PropertyKeys::ClickSeriesLastMs, nowMs);
  if (m_itemsPage != nullptr) {
    m_itemsPage->setFocus();
  }
}

auto InteractionManager::shouldTreatAsNewRowForClick(int targetIndex,
                                                     int gridWidth) const
    -> bool {
  if (m_currentCollectionIndex == nullptr || m_collections == nullptr ||
      gridWidth <= 0) {
    return false;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return false;
  }
  int targetRow = targetIndex / gridWidth;
  return (m_lastSelectedRow < 0) || (targetRow != m_lastSelectedRow);
}

auto InteractionManager::shouldAnimateHorizontalHop(int fromIndex, int toIndex,
                                                    int gridWidth) -> bool {
  if (fromIndex < 0 || gridWidth <= 0) {
    return false;
  }
  return (fromIndex / gridWidth) == (toIndex / gridWidth) &&
         qAbs(toIndex - fromIndex) > 1;
}

void InteractionManager::runHorizontalHopAnimation(int start, int target,
                                                   qint64 nowMs) {
  const int gen = property(PropertyKeys::HorizAnimGen).toInt() + 1;
  setProperty(PropertyKeys::HorizAnimGen, gen);
  setProperty(PropertyKeys::HorizAnimActive, true);
  const int step = (target > start) ? 1 : -1;
  const int steps = qAbs(target - start);
  constexpr int kPerHopMs = 12;
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(start);
  }
  for (int i = 1; i <= steps; ++i) {
    QTimer::singleShot(
        i * kPerHopMs, this, [this, gen, i, step, start, target]() {
          if (property(PropertyKeys::HorizAnimGen).toInt() != gen) {
            return;
          }
          if (!m_scrollManager) {
            return;
          }
          int nextIdx = start + (i * step);
          if (nextIdx != target) {
            m_selectedItemIndex = nextIdx;
            if (m_selectionManager) {
              m_selectionManager->setSelectedIndex(nextIdx);
            }
            m_scrollManager->updateSelectionForIndex(nextIdx);
          } else {
            setProperty(PropertyKeys::HorizAnimActive, false);
            m_selectedItemIndex = target;
            if (m_selectionManager) {
              m_selectionManager->setSelectedIndex(target);
            }
            selectItemByIndex(target, true);
            centerItemVertically(target, false);
          }
        });
  }
  setProperty(PropertyKeys::ClickSeriesLastMs, nowMs);
  setProperty(PropertyKeys::RowChangeFirstClickIndex, -1);
  setProperty(PropertyKeys::RowChangeFirstClickMs, 0);
  if (m_itemsPage != nullptr) {
    m_itemsPage->setFocus();
  }
}

void InteractionManager::handleNewRowClickSelection(int visualIndex,
                                                    qint64 nowMs) {
  m_allowArtworkDuringSelection = false;
  setProperty(PropertyKeys::SelectionSuppressed, true);
  setProperty(PropertyKeys::PendingSelectionIndex, visualIndex);
  setProperty(PropertyKeys::DeferCenterOnClick, false);
  setProperty(PropertyKeys::DeferredCenterIndex, -1);
  m_selectedItemIndex = visualIndex;
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(visualIndex);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(visualIndex, subs);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(visualIndex);
  }
  selectItemByIndex(visualIndex, true);
  centerItemVertically(visualIndex, false);
  setProperty(PropertyKeys::RowChangeFirstClickIndex, visualIndex);
  setProperty(PropertyKeys::RowChangeFirstClickMs, nowMs);
}

void InteractionManager::handleSameRowClickSelection(int visualIndex,
                                                     bool skipCenter,
                                                     qint64 /*nowMs*/) {
  setProperty(PropertyKeys::DeferCenterOnClick, false);
  setProperty(PropertyKeys::DeferredCenterIndex, -1);
  m_allowArtworkDuringSelection = true;
  selectItemByIndex(visualIndex, true);
  if (!skipCenter) {
    centerItemVertically(visualIndex, false);
  }
  setProperty(PropertyKeys::RowChangeFirstClickIndex, -1);
  setProperty(PropertyKeys::RowChangeFirstClickMs, 0);
}

// Cycles search mode regardless of search text; only updates results when there
// is search text
void InteractionManager::toggleSearchMode() {
  if (!m_searchManager) {
    return;
  }

  // Sync current state to SearchManager before toggle
  m_searchManager->setCurrentMode(m_currentSearchMode);

  // Delegate to SearchManager
  m_searchManager->toggleSearchMode();

  // Sync result back from SearchManager
  SearchMode newMode = m_searchManager->currentMode();
  if (newMode != m_currentSearchMode) {
    m_currentSearchMode = newMode;
    emit searchModeChanged(m_currentSearchMode);
  }

  // Do not reload or change the grid when search text is empty.
  // Only reapply results if the user has entered text.
  if ((m_searchBar != nullptr) && !m_searchBar->text().trimmed().isEmpty()) {
    m_searchManager->onSearchTextChanged(m_searchBar->text(), currentSelectedIndex());
  }
}

// Computes vertical centering duration using UIConstants without down‑scaling
// so timing matches configured 1500ms values
auto InteractionManager::computeVerticalCenterDuration(int distance,
                                                       bool repeatActive) const
    -> int {
  int itemHeight = 0;
  int vSpacing = 0;
  if ((m_collections != nullptr) && (m_currentCollectionIndex != nullptr) &&
      *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    itemHeight = (*m_collections)[*m_currentCollectionIndex].itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].verticalSpacing;
  }
  int stepSpan = qMax(1, itemHeight + vSpacing);
  double rows = static_cast<double>(distance) / static_cast<double>(stepSpan);
  rows = std::max(rows, 1.0);

  int perRow = repeatActive ? UIConstants::CENTER_SCROLL_PER_ROW_REPEAT
                            : UIConstants::CENTER_SCROLL_PER_ROW;
  double raw = rows * static_cast<double>(perRow);

  int minDur = repeatActive ? UIConstants::CENTER_SCROLL_MIN_DURATION_REPEAT
                            : UIConstants::CENTER_SCROLL_MIN_DURATION;
  int maxDur = repeatActive ? UIConstants::CENTER_SCROLL_MAX_DURATION_REPEAT
                            : UIConstants::CENTER_SCROLL_MAX_DURATION;

  int duration = static_cast<int>(std::round(raw));
  duration = qBound(minDur, duration, maxDur);
  return duration;
}

void InteractionManager::saveCurrentSelection() {
  if (m_selectedItemIndex >= 0) {
    handleSuccessfulSelection(m_selectedItemIndex);
  }
}

// Updates the search mode button icon/tooltip without coercing the current mode
void InteractionManager::updateSearchModeButton() {
  if (m_searchManager) {
    m_searchManager->setCurrentMode(m_currentSearchMode);
    m_searchManager->updateSearchModeButton();
  }
}

// Updates the search bar placeholder/text style without coercing the current
// mode
void InteractionManager::updateSearchBarPlaceholder() {
  if (m_searchManager) {
    m_searchManager->setCurrentMode(m_currentSearchMode);
    m_searchManager->updateSearchBarPlaceholder();
  }
}

// Restores selection instantly, ensures viewport positioning, and updates
// sidebar metadata
void InteractionManager::beginSelectionRestore(int targetIndex) {
  if (targetIndex < 0) {
    return;
  }

  // Use SelectionManager for preparation if available
  if (m_selectionManager) {
    m_selectionManager->prepareForRestore(targetIndex);
    // Sync local state
    m_restoringSelection = m_selectionManager->isRestoringSelection();
    m_targetRestoreIndex = m_selectionManager->targetRestoreIndex();
    m_forceImmediateCenter = m_selectionManager->forceImmediateCenter();
  } else {
    clearSelection();
    m_restoringSelection = true;
    m_targetRestoreIndex = targetIndex;
    m_forceImmediateCenter = true;

    if (m_itemScrollArea) {
      m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
      qint64 until = QDateTime::currentMSecsSinceEpoch() +
                     UIConstants::ARROW_KEY_ANIMATION_SETTLE_MS +
                     UIConstants::ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS;
      m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                    until);
    }
  }

  // Stop any running scroll animations (not delegated - uses local m_vScrollAnim)
  if ((m_vScrollAnim != nullptr) &&
      m_vScrollAnim->state() == QAbstractAnimation::Running) {
    m_vScrollAnim->stop();
  }

  applySelectionStateForIndex(targetIndex);
  applyImmediateViewportPositioningForSelection(targetIndex);
  selectItemByIndex(targetIndex, false);

  if (m_selectedItemIndex == targetIndex) {
    finalizeRestoreFlagsAndFocus();
    emit selectionChanged(targetIndex);
  }

  // Finalize restore state
  if (m_selectionManager) {
    m_selectionManager->finalizeRestore();
    m_restoringSelection = m_selectionManager->isRestoringSelection();
    m_targetRestoreIndex = m_selectionManager->targetRestoreIndex();
    m_forceImmediateCenter = m_selectionManager->forceImmediateCenter();
  } else {
    m_restoringSelection = false;
    m_targetRestoreIndex = -1;
    m_forceImmediateCenter = false;
    setProperty(PropertyKeys::SelectionSuppressed, false);
    setProperty(PropertyKeys::PendingSelectionIndex, -1);
  }

  if ((m_sidebarManager != nullptr) && m_sidebarManager->isSidebarVisible()) {
    MediaItemWidget *widget = nullptr;
    if (m_selectionManager) {
      widget = m_selectionManager->widgetForIndex(targetIndex);
    } else if (m_scrollManager != nullptr) {
      const auto &active = m_scrollManager->getActiveWidgets();
      widget = active.value(targetIndex, nullptr);
    }
    if (widget != nullptr) {
      m_sidebarManager->updateSidebarMetadata(widget);
    }
    constexpr int kMetadataSidebarUpdateDelayMs = 120;
    scheduleSidebarMetadataUpdateIfVisible(targetIndex, 0,
                                           kMetadataSidebarUpdateDelayMs);
  }
}

void InteractionManager::applySelectionStateForIndex(int idx) {
  m_selectedItemIndex = idx;
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(idx);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(idx, subs);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    m_scrollManager->updateSelectionForIndex(idx);
  }
}

void InteractionManager::finalizeRestoreFlagsAndFocus() {
  m_physicalKeyDown = false;
  m_repeating = false;
  m_repeatKey = Qt::Key_unknown;
  m_repeatDelta = 0;
  m_repeatVertical = false;
  m_wrapSequenceActive = false;
  // Only set focus to items page if search bar doesn't currently have focus
  if ((m_itemsPage != nullptr) && !m_itemsPage->hasFocus()) {
    if (m_searchBar == nullptr || !m_searchBar->hasFocus()) {
      m_itemsPage->setFocus();
    }
  }
  QTimer::singleShot(UIConstants::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS, this,
                     [this]() {
                       if (m_itemScrollArea) {
                         m_itemScrollArea->setProperty(
                             PropertyKeys::SuppressArrowCenter, false);
                       }
                     });
}

void InteractionManager::scheduleSidebarMetadataUpdateIfVisible(
    int targetIndex, int initialDelayMs, int secondaryDelayMs) {
  QPointer<InteractionManager> guard(this);
  auto schedule = [guard, targetIndex](int delay) {
    QTimer::singleShot(delay, guard, [guard, targetIndex]() {
      if (!guard) {
        return;
      }
      if (!guard->m_sidebarManager || !guard->m_scrollManager) {
        return;
      }
      if (!guard->m_sidebarManager->isSidebarVisible()) {
        return;
      }
      MediaItemWidget *itemWidget =
          guard->m_scrollManager->getActiveWidgets().value(targetIndex,
                                                           nullptr);
      if (itemWidget) {
        guard->m_sidebarManager->updateSidebarMetadata(itemWidget);
      }
    });
  };
  schedule(initialDelayMs);
  schedule(secondaryDelayMs);
}

// Handles search text debounce; uses a guard property to avoid refocusing after
// ESC-clears and replaces all raw property strings
namespace {
struct ResetClearedFlag {
  QPointer<QLineEdit> bar;
  ~ResetClearedFlag() {
    if (bar) {
      bar->setProperty(PropertyKeys::ClearedByEscape, false);
    }
  }
};
} // namespace

void InteractionManager::onSearchDebounceTimeout() {
  if (m_searchBar == nullptr || m_navigationManager == nullptr) {
    return;
  }

  bool onCollections = false;
  bool onItems = false;
  bool startedTyping = false;
  bool stoppedTyping = false;
  QString newSearchText;
  buildSearchDebounceState(onCollections, onItems, startedTyping, stoppedTyping,
                           newSearchText);

  auto scheduleRefocusIfNeeded = [this]() {
    scheduleSearchBarRefocusIfNeeded();
  };

  ResetClearedFlag reset{m_searchBar};

  if (onCollections) {
    handleCollectionsSearchDebounce(stoppedTyping, newSearchText);
    return;
  }

  if (!onItems) {
    return;
  }
  if (m_currentCollectionIndex == nullptr) {
    return;
  }

  if (stoppedTyping) {
    restoreViewedCollectionAfterSearchClear();
    scheduleRefocusIfNeeded();
    return;
  }

  handleItemsSearchDebounce(startedTyping, stoppedTyping, newSearchText);
}

void InteractionManager::buildSearchDebounceState(bool &onCollections,
                                                  bool &onItems,
                                                  bool &startedTyping,
                                                  bool &stoppedTyping,
                                                  QString &newSearchText) {
  onCollections = (m_stackedWidget != nullptr && m_collectionPage != nullptr &&
                   m_stackedWidget->currentWidget() == m_collectionPage);
  onItems = (m_stackedWidget != nullptr && m_itemsPage != nullptr &&
             m_stackedWidget->currentWidget() == m_itemsPage);

  newSearchText = (m_searchBar != nullptr) ? m_searchBar->text() : QString();
  const QString previousSearchText = m_currentSearchText;
  m_currentSearchText = newSearchText;

  const bool wasEmpty = previousSearchText.trimmed().isEmpty();
  const bool isNowEmpty = newSearchText.trimmed().isEmpty();
  startedTyping = wasEmpty && !isNowEmpty;
  stoppedTyping = !wasEmpty && isNowEmpty;

  updateSearchBarPlaceholder();
}

void InteractionManager::scheduleSearchBarRefocusIfNeeded() {
  if (m_searchBar == nullptr) {
    return;
  }
  const bool clearedByEscape =
      m_searchBar->property(PropertyKeys::ClearedByEscape).toBool();
  if (clearedByEscape) {
    return;
  }
  QTimer::singleShot(0, this, [this]() {
    if (m_searchBar != nullptr && m_searchBar->isVisible()) {
      m_searchBar->setFocus(Qt::OtherFocusReason);
    }
  });
  QTimer::singleShot(UIConstants::SEARCH_REFOCUS_DELAY_SHORT_MS, this,
                     [this]() {
                       if (m_searchBar != nullptr && m_searchBar->isVisible()) {
                         m_searchBar->setFocus(Qt::OtherFocusReason);
                       }
                     });
  QTimer::singleShot(UIConstants::SEARCH_REFOCUS_DELAY_LONG_MS, this, [this]() {
    if (m_searchBar != nullptr && m_searchBar->isVisible()) {
      m_searchBar->setFocus(Qt::OtherFocusReason);
    }
  });
}

void InteractionManager::handleCollectionsSearchDebounce(
    bool stoppedTyping, const QString &newSearchText) {
  if (stoppedTyping) {
    m_navigationManager->filterItems({});
    scheduleSearchBarRefocusIfNeeded();
    return;
  }
  m_navigationManager->filterItems(newSearchText);
}

void InteractionManager::handleItemsSearchDebounce(
    bool startedTyping, bool /*stoppedTyping*/, const QString &newSearchText) {
  if (startedTyping) {
    if (handleStartedTypingForCurrentMode()) {
      return;
    }
  }
  m_navigationManager->filterItems(newSearchText);
}

// Schedules repeated attempts plus a layout-complete hook to restore vertical
// scrollbar visibility after clearing search
void InteractionManager::scheduleScrollbarRecovery() {
  if (m_itemScrollArea == nullptr || m_scrollManager == nullptr ||
      m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    return;
  }
  int idx = *m_currentCollectionIndex;
  if (idx < 0 || idx >= m_collections->size()) {
    return;
  }
  if ((*m_collections)[idx].hideVerticalScrollbar) {
    return;
  }

  QPointer<InteractionManager> guard(this);

  auto attempt = [guard]() {
    if (!guard) {
      return;
    }
    if (guard->m_scrollManager == nullptr ||
        guard->m_itemScrollArea == nullptr) {
      return;
    }
    guard->m_scrollManager->recalculateContainerMetrics();
    guard->ensureVerticalScrollbarPolicy();
    QScrollBar *verticalScrollBar =
        guard->m_itemScrollArea->verticalScrollBar();
    if (verticalScrollBar != nullptr && verticalScrollBar->maximum() > 0) {
      guard->m_itemScrollArea->setVerticalScrollBarPolicy(
          Qt::ScrollBarAsNeeded);
    }
  };

  attempt();
  QTimer::singleShot(UIConstants::SCROLLBAR_RECOVERY_ATTEMPT_1_MS, this,
                     attempt);
  QTimer::singleShot(UIConstants::SCROLLBAR_RECOVERY_ATTEMPT_2_MS, this,
                     attempt);
  QTimer::singleShot(UIConstants::SCROLLBAR_RECOVERY_ATTEMPT_3_MS, this,
                     attempt);

  if (m_scrollbarRecoveryConn == nullptr) {
    m_scrollbarRecoveryConn = QObject::connect(
        m_scrollManager, &ScrollManager::virtualScrollSetupComplete, this,
        [guard]() {
          if (!guard) {
            return;
          }
          guard->ensureVerticalScrollbarPolicy();
          if (guard->m_scrollbarRecoveryConn) {
            QObject::disconnect(guard->m_scrollbarRecoveryConn);
            guard->m_scrollbarRecoveryConn = QMetaObject::Connection();
          }
        });
  }
}

auto InteractionManager::handleWidgetSelection(MediaItemWidget *widget,
                                               const QPoint &clickPos,
                                               QMouseEvent *originalEvent)
    -> int {
  if (widget == nullptr || m_gridContainer == nullptr ||
      originalEvent == nullptr) {
    return -1;
  }

  QPoint localPos = widget->mapFrom(m_gridContainer, clickPos);
  QPoint globalPos = widget->mapToGlobal(localPos);
  QMouseEvent synthetic(QEvent::MouseButtonPress, localPos, globalPos,
                        Qt::LeftButton, Qt::LeftButton,
                        originalEvent->modifiers());
  widget->mousePressEvent(&synthetic);

  if (widget == nullptr || m_scrollManager == nullptr ||
      m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    return -1;
  }
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return -1;
  }

  int visualIndex = -1;
  const auto &active = m_scrollManager->getActiveWidgets();
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() == widget) {
      visualIndex = it.key();
      break;
    }
  }
  if (visualIndex < 0) {
    return -1;
  }

  processSingleClickSelection(visualIndex, QString(), false);
  return visualIndex;
}

void InteractionManager::updateClickHoldHorizontalCandidate(
    int previousSelection, int targetSelection) {
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;
  if (previousSelection < 0 || targetSelection < 0 ||
      previousSelection == targetSelection) {
    return;
  }
  const int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }
  const int previousRow = previousSelection / gridWidth;
  const int currentRow = targetSelection / gridWidth;
  if (previousRow != currentRow) {
    return;
  }
  m_mouseHoldHorizontalDirection =
      (targetSelection > previousSelection) ? 1 : -1;
  m_mouseHoldHorizontalStartIndex = targetSelection;
  m_clickHoldHorizontalEligible = true;
}

// Initialize search mode for the current collection; reset away from
// AllCollections and prefer collection defaults
void InteractionManager::initializeSearchModeForCurrentCollection() {
  if (m_searchManager) {
    m_searchManager->setCurrentMode(m_currentSearchMode);
    m_searchManager->initializeSearchModeForCurrentCollection();

    // Sync result back
    SearchMode newMode = m_searchManager->currentMode();
    if (newMode != m_currentSearchMode) {
      m_currentSearchMode = newMode;
      emit searchModeChanged(m_currentSearchMode);
    }
  }
}

// Launches an item using the collection's configured launcher; expands
// variables without path validation so launch works even if artworkDirectory is
// empty
void InteractionManager::launchItemWithCollection(const QString &filePath,
                                                  int collectionIndex) {
  if ((m_collections == nullptr) || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    QMessageBox::warning(nullptr, "Invalid Collection",
                         "Invalid collection specified.");
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  auto expandOnly = [&](const QString &text) -> QString {
    QString out = text;
    out.replace("%collection%", collection.name, Qt::CaseInsensitive);
    return out.trimmed();
  };

  QString expandedLauncherPath = expandOnly(collection.launcherPath);
  QString expandedCorePath = expandOnly(collection.corePath);
  QString expandedLaunchParameters = expandOnly(collection.launchParameters);

  if (expandedLauncherPath.isEmpty()) {
    QMessageBox::warning(nullptr, "No Launcher",
                         "No launcher configured for " + collection.name);
    return;
  }

  QString program;
  QStringList arguments;

  if (expandedLauncherPath.contains("retroarch", Qt::CaseInsensitive)) {
    if (expandedCorePath.isEmpty()) {
      QMessageBox::warning(nullptr, "No Core",
                           "No RetroArch core configured for " +
                               collection.name);
      return;
    }

    program = expandedLauncherPath;
    arguments << "-L" << expandedCorePath << filePath;
  } else {
    program = expandedLauncherPath;
    arguments << filePath;

    if (!expandedCorePath.isEmpty()) {
      QString params = expandedCorePath.trimmed();
      if (!params.isEmpty()) {
        arguments.removeLast();
        QStringList paramList = parseParameters(params);
        arguments.append(paramList);
        arguments << filePath;
      }
    }
  }

  bool success = QProcess::startDetached(program, arguments);

  if (!success) {
    QString errorMsg =
        QString("Failed to launch: %1\n\nCommand attempted:\n%2 %3\n\nMake "
                "sure the launcher path is correct and the file is executable.")
            .arg(expandedLauncherPath)
            .arg(program)
            .arg(arguments.join(" "));

    QMessageBox::critical(nullptr, "Launch Error", errorMsg);
  }
}

// Stops key/mouse repeat navigation and restores artwork / centering properties
void InteractionManager::stopRepeat(bool suppressRecentering) {
  if (m_isShuttingDown || QApplication::closingDown()) {
    m_repeating = false;
    m_repeatKey = Qt::Key_unknown;
    m_repeatDelta = 0;
    m_repeatVertical = false;
    m_wrapSequenceActive = false;
    setProperty(PropertyKeys::KeyContinuous, false);
    return;
  }

  if (m_repeatTimer != nullptr) {
    m_repeatTimer->stop();
  }
  if (m_repeatStartTimer != nullptr) {
    m_repeatStartTimer->stop();
  }

  m_repeating = false;
  m_repeatKey = Qt::Key_unknown;
  m_repeatDelta = 0;
  m_repeatVertical = false;
  m_wrapSequenceActive = false;
  setProperty(PropertyKeys::HorizHoldActive, false);
  setProperty(PropertyKeys::KeyContinuous, false);
  setProperty(PropertyKeys::ArmFirstClickDelay, false);
  setProperty(PropertyKeys::PendingInitialCenter, false);

  if (m_gridContainer != nullptr) {
    m_gridContainer->setProperty(PropertyKeys::ArrowKeyScrolling, false);
  }
  if (m_gridContainer != nullptr) {
    m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  if (m_scrollManager != nullptr) {
    m_scrollManager->setForceSelectionOverlayVisible(false);
  }

  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, false);
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs, 0);
  }

  if ((m_hScrollAnim != nullptr) &&
      m_hScrollAnim->state() == QAbstractAnimation::Running) {
    m_hScrollAnim->stop();
  }

  if (property(PropertyKeys::SelectionSuppressed).toBool()) {
    int pending = property(PropertyKeys::PendingSelectionIndex).toInt();
    if (pending >= 0) {
      selectItemByIndex(pending, true);
    }
    setProperty(PropertyKeys::SelectionSuppressed, false);
    setProperty(PropertyKeys::PendingSelectionIndex, -1);
  }

  if (!m_physicalKeyDown) {
    m_continuousScrollActive =
        ((m_vScrollAnim != nullptr) &&
         m_vScrollAnim->state() == QAbstractAnimation::Running);
  }

  if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
      !suppressRecentering) {
    QTimer::singleShot(
        UIConstants::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
          if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
              !m_continuousScrollActive) {
            centerItemVertically(m_selectedItemIndex, false);
          }
        });
  }
}

auto InteractionManager::parseParameters(const QString &paramString)
    -> QStringList {
  QStringList result;
  if (paramString.trimmed().isEmpty()) {
    return result;
  }

  QString params = paramString.trimmed();
  bool inQuotes = false;
  QString currentParam;
  QChar quoteChar;

  for (int i = 0; i < params.length(); ++i) {
    QChar currentChar = params[i];

    if (!inQuotes && (currentChar == '"' || currentChar == '\'')) {
      inQuotes = true;
      quoteChar = currentChar;
    } else if (inQuotes && currentChar == quoteChar) {
      inQuotes = false;
    } else if (currentChar == ' ' && !inQuotes) {
      if (!currentParam.isEmpty()) {
        result.append(currentParam);
        currentParam.clear();
      }
    } else {
      currentParam.append(currentChar);
    }
  }

  if (!currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}

auto InteractionManager::isWheelScrolling() const -> bool {
  return m_wheelScrolling;
}

// Start mouse-hold based scrolling selection updates; allows artwork updates
// during hold
void InteractionManager::startMouseHoldScrolling(const QPoint &clickPos) {
  Q_UNUSED(clickPos);
  if (m_scrollManager == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }

  int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    return;
  }

  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }

  if (m_selectedItemIndex < 0) {
    return;
  }

  if (m_mouseHoldTimer == nullptr) {
    m_mouseHoldTimer = new QTimer(this);
    connect(m_mouseHoldTimer, &QTimer::timeout, this,
            &InteractionManager::onMouseHoldScrollStep);
  }

  if (tryStartHorizontalClickHold(totalItems)) {
    return;
  }

  m_mouseHoldHorizontal = false;
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;
  setProperty(PropertyKeys::HorizHoldActive, false);

  const CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vBar = m_itemScrollArea->verticalScrollBar();
  if (vBar == nullptr) {
    return;
  }

  int selectedRow = m_selectedItemIndex / gridWidth;
  int rowHeight = config.itemHeight + config.verticalSpacing;
  int selectedItemY = UIConstants::GRID_MARGINS + (selectedRow * rowHeight) +
                      (config.itemHeight / 2);

  int scrollTop = vBar->value();
  int viewportHeight = m_itemScrollArea->viewport()->height();
  int viewportTop = scrollTop;
  int viewportBottom = scrollTop + viewportHeight;
  int viewportCenterY = scrollTop + (viewportHeight / 2);

  if (selectedItemY < viewportTop + rowHeight) {
    m_mouseHoldDirection = -1;
  } else if (selectedItemY > viewportBottom - rowHeight) {
    m_mouseHoldDirection = 1;
  } else if (selectedItemY < viewportCenterY) {
    m_mouseHoldDirection = -1;
  } else if (selectedItemY > viewportCenterY) {
    m_mouseHoldDirection = 1;
  } else {
    return;
  }

  m_mouseHoldScrolling = true;

  m_mouseHoldTimer->start(UIConstants::ARROW_KEY_BASE_INTERVAL_MS);

  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, true);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
  }

  m_continuousScrollActive = true;
  m_repeating = true;
  m_physicalKeyDown = true;
  m_repeatVertical = true;
  m_allowArtworkDuringSelection = true;

  setProperty(PropertyKeys::ClickScroll, true);
  setProperty(PropertyKeys::ClickHoldAdvancing, true);
  if (m_scrollManager != nullptr) {
    m_scrollManager->setForceSelectionOverlayVisible(true);
  }
}

bool InteractionManager::tryStartHorizontalClickHold(int totalItems) {
  if (!m_clickHoldHorizontalEligible || m_mouseHoldHorizontalDirection == 0 ||
      m_mouseHoldTimer == nullptr) {
    return false;
  }
  if (m_collections == nullptr || m_currentCollectionIndex == nullptr ||
      *m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    m_clickHoldHorizontalEligible = false;
    return false;
  }
  if (m_selectedItemIndex < 0 || m_selectedItemIndex >= totalItems) {
    m_clickHoldHorizontalEligible = false;
    return false;
  }

  int startIndex = m_mouseHoldHorizontalStartIndex;
  if (startIndex < 0 || startIndex >= totalItems) {
    startIndex = m_selectedItemIndex;
  }
  if (startIndex != m_selectedItemIndex && startIndex >= 0 &&
      startIndex < totalItems) {
    QList<int> subs = getSubcollections(*m_currentCollectionIndex);
    m_selectedItemIndex = startIndex;
    if (m_selectionManager) {
      m_selectionManager->setSelectedIndex(startIndex);
    }
    updateFilePathForSelection(startIndex, subs);
    if (m_scrollManager != nullptr) {
      m_scrollManager->updateSelectionForIndex(startIndex);
    }
    selectItemByIndex(startIndex, true);
  }
  m_mouseHoldHorizontalStartIndex = -1;

  m_mouseHoldHorizontal = true;
  m_mouseHoldScrolling = true;
  m_clickHoldHorizontalEligible = false;

  m_mouseHoldTimer->start(UIConstants::CLICK_HOLD_HORIZONTAL_INTERVAL_MS);

  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, true);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
  }

  m_continuousScrollActive = true;
  m_repeating = true;
  m_physicalKeyDown = true;
  m_repeatVertical = false;
  setProperty(PropertyKeys::HorizHoldActive, true);
  m_allowArtworkDuringSelection = true;

  setProperty(PropertyKeys::ClickScroll, true);
  setProperty(PropertyKeys::ClickHoldAdvancing, true);
  if (m_scrollManager != nullptr) {
    m_scrollManager->setForceSelectionOverlayVisible(true);
  }
  return true;
}

// Stops mouse-hold scrolling and restores suppressed selection if needed
void InteractionManager::stopMouseHoldScrolling() {
  if (m_mouseHoldTimer != nullptr) {
    m_mouseHoldTimer->stop();
  }
  m_mouseHoldScrolling = false;
  m_mouseHoldDirection = 0;
  m_mouseHoldHorizontal = false;
  m_clickHoldHorizontalEligible = false;
  m_mouseHoldHorizontalDirection = 0;
  m_mouseHoldHorizontalStartIndex = -1;

  m_repeatVertical = false;
  m_wrapSequenceActive = false;

  if (m_itemScrollArea && !m_repeating) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
  }

  if (property(PropertyKeys::SelectionSuppressed).toBool()) {
    int pending = property(PropertyKeys::PendingSelectionIndex).toInt();
    if (pending >= 0) {
      selectItemByIndex(pending, true);
    }
    setProperty(PropertyKeys::SelectionSuppressed, false);
    setProperty(PropertyKeys::PendingSelectionIndex, -1);
  }

  if (!m_repeating) {
    m_continuousScrollActive = false;
    m_physicalKeyDown = false;
    m_allowArtworkDuringSelection = true;
  }

  setProperty(PropertyKeys::ClickScroll, false);
  setProperty(PropertyKeys::ClickHoldAdvancing, false);
  setProperty(PropertyKeys::HorizHoldActive, false);
  if (m_scrollManager != nullptr) {
    m_scrollManager->setForceSelectionOverlayVisible(false);
  }
}

// Advances selection one row at a time during mouse-hold scrolling
void InteractionManager::onMouseHoldScrollStep() {
  if (!m_mouseHoldScrolling || m_scrollManager == nullptr ||
      m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    stopMouseHoldScrolling();
    return;
  }

  int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    stopMouseHoldScrolling();
    return;
  }

  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    stopMouseHoldScrolling();
    return;
  }

  if (m_mouseHoldHorizontal) {
    int currentIndex = m_selectedItemIndex >= 0 ? m_selectedItemIndex : 0;
    bool wrap = ((m_mainWindow != nullptr)
                     ? m_mainWindow->m_generalSettings.wrapNavigation
                     : false);
    bool didWrap = false;
    int nextIndex = KeyboardManager::calculateHorizontalSelection(
        totalItems, currentIndex, m_mouseHoldHorizontalDirection, wrap,
        didWrap);

    if (nextIndex == currentIndex) {
      return;
    }

    if (didWrap) {
      m_forceImmediateCenter = true;
      m_wrapSequenceActive = true;
      m_continuousScrollActive = false;
    } else {
      m_continuousScrollActive = true;
    }

    bool rowChanged = KeyboardManager::hasRowChanged(gridWidth, currentIndex, nextIndex);
    if (rowChanged) {
      setProperty(PropertyKeys::SelectionSuppressed, true);
      setProperty(PropertyKeys::PendingSelectionIndex, nextIndex);
    }

    m_selectedItemIndex = nextIndex;
    if (m_selectionManager) {
      m_selectionManager->setSelectedIndex(nextIndex);
    }
    QList<int> subs = getSubcollections(*m_currentCollectionIndex);
    updateFilePathForSelection(nextIndex, subs);
    
    // Set properties before selectItemByIndex so the selection update knows we're in hold mode
    setProperty(PropertyKeys::ClickScroll, true);
    setProperty(PropertyKeys::ClickHoldAdvancing, true);
    
    // selectItemByIndex will call updateSelectionForIndex internally
    selectItemByIndex(nextIndex, true);

    if (rowChanged) {
      centerItemVertically(nextIndex, false);
    } else {
      ensureHorizontallyVisible(nextIndex);
    }
    return;
  }

  int currentIndex = m_selectedItemIndex >= 0 ? m_selectedItemIndex : 0;
  int nextIndex = currentIndex + (m_mouseHoldDirection * gridWidth);

  bool wrap = ((m_mainWindow != nullptr)
                   ? m_mainWindow->m_generalSettings.wrapNavigation
                   : false);
  bool didWrap = false;

  if (wrap) {
    if (nextIndex < 0) {
      nextIndex = (totalItems + (nextIndex % totalItems)) % totalItems;
      didWrap = true;
    } else if (nextIndex >= totalItems) {
      nextIndex = nextIndex % totalItems;
      didWrap = true;
    }
  } else {
    nextIndex = std::max(nextIndex, 0);
    if (nextIndex >= totalItems) {
      nextIndex = totalItems - 1;
    }
  }

  if (nextIndex == currentIndex) {
    return;
  }

  if (didWrap) {
    m_forceImmediateCenter = true;
    m_wrapSequenceActive = true;
    m_continuousScrollActive = false;
  } else {
    m_continuousScrollActive = true;
  }

  if (*m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size() && gridWidth > 0) {
    int currentRow = (gridWidth > 0 ? currentIndex / gridWidth : -1);
    int targetRow = nextIndex / gridWidth;
    if (currentRow != targetRow) {
      setProperty(PropertyKeys::SelectionSuppressed, true);
      setProperty(PropertyKeys::PendingSelectionIndex, nextIndex);
    }
  }

  m_selectedItemIndex = nextIndex;
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(nextIndex);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(nextIndex, subs);
  
  // Set properties before selectItemByIndex so the selection update knows we're in hold mode
  setProperty(PropertyKeys::ClickScroll, true);
  setProperty(PropertyKeys::ClickHoldAdvancing, true);
  
  // selectItemByIndex will call updateSelectionForIndex internally
  selectItemByIndex(nextIndex, true);

  centerItemVertically(nextIndex, false);
}

// Forces a full reload of the viewed collection and clears any global view
// remnants
void InteractionManager::forceReloadViewedCollection() {
  if (m_navigationManager == nullptr) {
    return;
  }

  if (m_searchManager && m_searchManager->debounceTimer() != nullptr) {
    m_searchManager->debounceTimer()->stop();
  }
  if (m_searchManager) {
    auto &conn = m_searchManager->itemsLoadedConnection();
    if (conn != QMetaObject::Connection()) {
      QObject::disconnect(conn);
      conn = QMetaObject::Connection();
    }
  }

  int collIndex = -1;
  if (m_searchManager && m_searchManager->preSearchCollectionIndex() >= 0) {
    collIndex = m_searchManager->preSearchCollectionIndex();
  } else if (m_currentCollectionIndex != nullptr) {
    collIndex = *m_currentCollectionIndex;
  } else {
    collIndex = -1;
  }
  if (collIndex < 0) {
    return;
  }

  m_navigationManager->filterItems(QString());
  m_navigationManager->safeReloadCollection(collIndex);
  m_navigationManager->showCollectionItems(collIndex);

  int sel = m_searchManager ? m_searchManager->preSearchSelectedIndex() : -1;
  if (sel < 0 && (m_settingsManager != nullptr)) {
    sel = m_settingsManager->getLastSelectedItem(collIndex);
  }
  if (sel >= 0) {
    m_navigationManager->scheduleSelectionRestore(
        sel, UIConstants::SELECTION_RESTORE_STEPS,
        UIConstants::SELECTION_RESTORE_STEP_DELAY_MS,
        UIConstants::SELECTION_RESTORE_MAX_DELAY_MS);
  }

  if (m_searchManager) {
    m_searchManager->setSearchActive(false);
  }
}

// Finalizes selection bookkeeping and persists selection; standardizes property
// key for user-free-scroll
void InteractionManager::handleSuccessfulSelection(int index) {
  setProperty(PropertyKeys::UserFreeScroll, false);
  
  bool restoringMatch = false;
  if (m_selectionManager) {
    restoringMatch = m_selectionManager->checkAndFinalizeRestore(index);
    // Sync local flags
    m_restoringSelection = m_selectionManager->isRestoringSelection();
    m_targetRestoreIndex = m_selectionManager->targetRestoreIndex();
  } else {
    restoringMatch = (m_restoringSelection && index == m_targetRestoreIndex);
    if (restoringMatch) {
      m_restoringSelection = false;
      m_targetRestoreIndex = -1;
    }
  }
  
  if ((m_mainWindow != nullptr) && m_mainWindow->isShuttingDown()) {
    return;
  }

  int currentColl =
      ((m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1);
  if ((m_mainWindow != nullptr) && currentColl >= 0 && index >= 0) {
    persistSelectionForIndex(currentColl, index);
  }
  if (QApplication::closingDown()) {
    return;
  }

  bool immediate = m_forceImmediateCenter || restoringMatch;
  centerItemVertically(index, immediate);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
  }
}

auto InteractionManager::handleStartedTypingForCurrentMode() -> bool {
  switch (m_currentSearchMode) {
  case SearchMode::CurrentAndSubcollections:
    if (m_navigationManager != nullptr) {
      m_navigationManager->loadCurrentAndSubcollections();
      return true;
    }
    return false;
  case SearchMode::AllCollections:
    if (m_navigationManager != nullptr) {
      m_navigationManager->loadAllCollectionsView();
      return true;
    }
    return false;
  case SearchMode::CurrentCollection:
  default: {
    if ((m_collections != nullptr) && m_currentCollectionIndex != nullptr &&
        *m_currentCollectionIndex >= 0 &&
        *m_currentCollectionIndex < m_collections->size() &&
        (m_databaseManager != nullptr) && (m_settingsManager != nullptr)) {
      CollectionConfig cfg = (*m_collections)[*m_currentCollectionIndex];
      if (cfg.showAllSubcollectionItems) {
        CollectionContext ctx;
        ctx.currentIndex = *m_currentCollectionIndex;
        cfg.mediaDirectory = SettingsUtils::expandConfigVariables(
            cfg.mediaDirectory, cfg.name);
        cfg.artworkDirectory = SettingsUtils::expandConfigVariables(
            cfg.artworkDirectory, cfg.name);
        ctx.config = cfg;
        ctx.artworkDirectory = cfg.artworkDirectory;
        m_databaseManager->loadItemsWithSubcollections(ctx, *m_collections);
        return true;
      }
    }
    return false;
  }
  }
}

auto InteractionManager::titleForIndexInColl(int coll, int idx) const
    -> QString {
  QList<int> subs = getSubcollections(coll);
  if (idx < subs.size()) {
    int subIdx = subs[idx];
    if (subIdx >= 0 && subIdx < m_mainWindow->m_collections.size()) {
      return m_mainWindow->m_collections[subIdx].name;
    }
    return {};
  }
  QString path = m_selectedFilePath;
  if (path.isEmpty() && (m_scrollManager != nullptr)) {
    path = m_scrollManager->filePathForVisualIndex(idx);
  }
  if (!path.isEmpty()) {
    return QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  return {};
}

void InteractionManager::persistSelectionForIndex(int coll, int idx) {
  if (m_mainWindow->getSettingsManager() != nullptr) {
    m_mainWindow->getSettingsManager()->setLastSelectedItem(coll, idx);
    QString collectionName = m_mainWindow->m_collections[coll].name;
    QString title;
    // if (idx < subcollections.size()) {
    //   int subIdx = subcollections[idx];
    //   if (subIdx >= 0 && subIdx < m_mainWindow->m_collections.size()) {
    //     title = m_mainWindow->m_collections[subIdx].name;
    //   }
    // } else {
      QString path = m_selectedFilePath;
      if (path.isEmpty() && (m_scrollManager != nullptr)) {
        path = m_scrollManager->filePathForVisualIndex(idx);
      }
      if (!path.isEmpty()) {
        title =
            QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
      }
    // }
    if (m_sessionManager) {
      m_sessionManager->setLastSelected(collectionName, idx, title);
    }
    if (m_settingsManager != nullptr) {
      m_settingsManager->setLastSelectedItem(coll, idx);
    }
  }
  QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
    if (!QApplication::closingDown() && m_artworkManager) {
      m_artworkManager->updateViewportArtwork();
    }
  });
}

auto InteractionManager::findBestWidgetForClick(const QPoint &clickPos)
    -> MediaItemWidget * {
  if ((m_scrollManager == nullptr) || (m_gridContainer == nullptr)) {
    return nullptr;
  }

  QVector<MediaItemWidget *> candidates;
  const auto &active = m_scrollManager->getActiveWidgets();
  candidates.reserve(active.size());
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if ((it.value() != nullptr) && it.value()->isVisible()) {
      candidates.append(it.value());
    }
  }
  if (candidates.isEmpty()) {
    return nullptr;
  }

  QPoint virtualContainerOffset(0, 0);
  QWidget *virtualContainer = (candidates.first() != nullptr)
                                  ? candidates.first()->parentWidget()
                                  : nullptr;
  if ((virtualContainer != nullptr) &&
      virtualContainer->parentWidget() == m_gridContainer) {
    virtualContainerOffset = virtualContainer->pos();
  }
  QPoint posInVC = clickPos - virtualContainerOffset;

  QVector<MediaItemWidget *> under;
  under.reserve(candidates.size());
  for (MediaItemWidget *widget : candidates) {
    if (widget == nullptr) {
      continue;
    }
    if (widget->geometry().contains(posInVC)) {
      under.append(widget);
    }
  }

  if (!under.isEmpty()) {
    return findClosestWidget(under, posInVC);
  }
  return findClosestWidget(candidates, posInVC);
}

auto InteractionManager::findClosestWidget(
    const QVector<MediaItemWidget *> &candidates, const QPoint &clickPos)
    -> MediaItemWidget * {
  MediaItemWidget *best = nullptr;
  qint64 bestDist2 = -1;
  for (MediaItemWidget *widget : candidates) {
    if (widget == nullptr) {
      continue;
    }
    const QRect geometry = widget->geometry();
    const QPoint centerPoint = geometry.center();
    const qint64 deltaX = static_cast<qint64>(centerPoint.x()) -
                          static_cast<qint64>(clickPos.x());
    const qint64 deltaY = static_cast<qint64>(centerPoint.y()) -
                          static_cast<qint64>(clickPos.y());
    const qint64 dist2 = (deltaX * deltaX) + (deltaY * deltaY);
    if (bestDist2 < 0 || dist2 < bestDist2) {
      bestDist2 = dist2;
      best = widget;
    }
  }
  return best;
}

void InteractionManager::cancelPendingSelectionRestore() {
  if (m_selectionManager) {
    m_selectionManager->cancelPendingSelectionRestore();
  }
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
}
