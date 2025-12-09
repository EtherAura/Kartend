// Orchestrates user interactions, delegating to specialized managers for input handling.
#include "interactionmanager.h"

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
#include <algorithm>

// Include full headers for forward-declared owned managers
#include "animationmanager.h"
#include "arrownavigationhandler.h"
#include "alphabeticnavigationhandler.h"
#include "eventmanager.h"
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
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcInteractionManager, "kartend.interactionmanager")
#define debugLog(msg) qCDebug(lcInteractionManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

InteractionManager::InteractionManager(QObject *parent) : QObject(parent) {
  m_searchManager = std::make_unique<SearchManager>(this);
  m_selectionManager = std::make_unique<SelectionManager>(this);
  m_keyboardManager = std::make_unique<KeyboardManager>(this);
  m_arrowHandler = std::make_unique<ArrowNavigationHandler>(this);
  m_alphabeticHandler = std::make_unique<AlphabeticNavigationHandler>(this);
  m_animationManager = std::make_unique<AnimationManager>(this);
  m_mouseManager = std::make_unique<MouseManager>(this);
  m_launchManager = std::make_unique<LaunchManager>(this);
  m_viewportManager = std::make_unique<ViewportManager>(this);
  m_eventManager = std::make_unique<EventManager>(this);

  m_viewportManager->setContinuousScrollActive(true);
}

// Destructor: stop timers/animations and clear selection
InteractionManager::~InteractionManager() {
  stopRepeat();
  clearSelection();
}

// Wires references, installs event filters, and initializes search UI
void InteractionManager::setupReferences(const InteractionManagerSetup &setup) {
  // Manager dependencies - use accessors that check context fallback
  m_scrollManager = setup.getScrollManager();
  m_sidebarManager = setup.getSidebarManager();
  m_settingsManager = setup.getSettingsManager();
  m_databaseManager = setup.getDatabaseManager();
  m_navigationManager = setup.getNavigationManager();
  m_sessionManager = setup.getSessionManager();
  m_artworkManager = setup.getArtworkManager();
  
  // UI elements - use accessors that check context fallback
  m_itemScrollArea = setup.getItemScrollArea();
  m_gridContainer = setup.getGridContainer();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_stackedWidget = setup.getStackedWidget();
  m_itemsPage = setup.getItemsPage();
  m_searchBar = setup.getSearchBar();
  m_generalSettings = setup.getGeneralSettings();
  m_isShuttingDown = setup.getIsShuttingDown();

  // Setup SearchManager with its dependencies
  if (m_searchManager) {
    SearchManagerSetup searchSetup;
    searchSetup.ctx = setup.ctx;
    searchSetup.collectionPage = setup.collectionPage;
    m_searchManager->setupReferences(searchSetup);
    connectSearchManagerSignals();
  }

  // Setup SelectionManager with its dependencies
  // Pass ctx and any overrides - getters handle fallback to ctx
  if (m_selectionManager) {
    SelectionManagerSetup selectionSetup;
    selectionSetup.ctx = setup.ctx;
    // Override managers that are owned by InteractionManager
    selectionSetup.animationManager = m_animationManager.get();
    selectionSetup.viewportManager = m_viewportManager.get();
    m_selectionManager->setupReferences(selectionSetup);
    connectSelectionManagerSignals();
  }

  // Setup KeyboardManager with its dependencies
  if (m_keyboardManager) {
    KeyboardManagerSetup keyboardSetup;
    keyboardSetup.ctx = setup.ctx;
    keyboardSetup.generalSettings = m_generalSettings;
    m_keyboardManager->setupReferences(keyboardSetup);
    connectKeyboardManagerSignals();
  }

  // Setup navigation handlers with state callbacks and signal connections
  setupArrowNavigationHandler(setup);
  setupAlphabeticNavigationHandler();

  // Setup AnimationManager with its dependencies
  if (m_animationManager) {
    AnimationManagerSetup animSetup;
    animSetup.ctx = setup.ctx;
    animSetup.generalSettings = m_generalSettings;
    m_animationManager->setupReferences(animSetup);
    connectAnimationManagerSignals();
  }

  // Setup MouseManager with its dependencies
  if (m_mouseManager) {
    MouseManagerSetup mouseSetup;
    mouseSetup.ctx = setup.ctx;
    mouseSetup.selectionManager = m_selectionManager.get();
    mouseSetup.generalSettings = m_generalSettings;
    m_mouseManager->setupReferences(mouseSetup);
    connectMouseManagerSignals();
  }

  // Setup LaunchManager with its dependencies
  if (m_launchManager) {
    LaunchManagerSetup launchSetup;
    launchSetup.ctx = setup.ctx;
    m_launchManager->setupReferences(launchSetup);
  }

  // Setup ViewportManager with its dependencies
  if (m_viewportManager) {
    ViewportManagerSetup viewportSetup;
    viewportSetup.ctx = setup.ctx;
    viewportSetup.generalSettings = m_generalSettings;
    viewportSetup.selectionManager = m_selectionManager.get();
    viewportSetup.animationManager = m_animationManager.get();
    m_viewportManager->setupReferences(viewportSetup);
    connectViewportManagerSignals();
  }

  // Setup EventManager with its dependencies using struct pattern
  if (m_eventManager) {
    EventManagerSetup eventSetup;
    eventSetup.ctx = setup.ctx;
    eventSetup.keyboardManager = m_keyboardManager.get();
    eventSetup.mouseManager = m_mouseManager.get();
    eventSetup.animationManager = m_animationManager.get();
    eventSetup.viewportManager = m_viewportManager.get();
    eventSetup.selectionManager = m_selectionManager.get();
    m_eventManager->setupReferences(eventSetup);
    connectEventManagerSignals();
  }

  updateSearchModeButton();
  updateSearchBarPlaceholder();

  if (m_searchBar) {
    connect(m_searchBar, &QLineEdit::textChanged, this,
            &InteractionManager::handleImmediateSearchTextChanged);
  }

  installEventFilters();
}

void InteractionManager::setupArrowNavigationHandler(
    const InteractionManagerSetup &setup) {
  if (!m_arrowHandler) {
    return;
  }

  ArrowNavigationHandlerSetup arrowSetup;
  arrowSetup.ctx = setup.ctx;
  // Override with owned managers
  arrowSetup.keyboardManager = m_keyboardManager.get();
  arrowSetup.animationManager = m_animationManager.get();
  arrowSetup.viewportManager = m_viewportManager.get();
  arrowSetup.selectionManager = m_selectionManager.get();
  m_arrowHandler->setupReferences(arrowSetup);

  // Set callbacks for accessing InteractionManager state
  m_arrowHandler->setGetCurrentSelection(
      [this]() { return currentSelectedIndex(); });
  m_arrowHandler->setGetCurrentGridWidth(
      [this]() { return getCurrentGridWidth(); });
  m_arrowHandler->setIsItemOffscreen(
      [this](int selection, int gridWidth) {
        return isItemOffscreen(selection, gridWidth);
      });

  // Connect handler signals
  connect(m_arrowHandler.get(),
          &ArrowNavigationHandler::requestFullSelectionUpdate, this,
          [this](int index) {
            const QList<int> subs = getSubcollections(*m_currentCollectionIndex);
            updateFilePathForSelection(index, subs);
            selectItemByIndex(index, true);
          });
  connect(m_arrowHandler.get(), &ArrowNavigationHandler::requestRecenter,
          this, &InteractionManager::recenterCurrentSelection);
  connect(m_arrowHandler.get(),
          &ArrowNavigationHandler::requestMinorHorizontalSuppress, this,
          &InteractionManager::applyMinorHorizontalSuppress);
  connect(m_arrowHandler.get(),
          &ArrowNavigationHandler::requestFocusItemsPage, this, [this]() {
            if (m_itemsPage) {
              m_itemsPage->setFocus();
            }
          });
}

void InteractionManager::setupAlphabeticNavigationHandler() {
  if (!m_alphabeticHandler) {
    return;
  }

  m_alphabeticHandler->setScrollManager(m_scrollManager);
  m_alphabeticHandler->setSelectionManager(m_selectionManager.get());

  // Connect handler signals - use immediate centering for large jumps
  connect(m_alphabeticHandler.get(),
          &AlphabeticNavigationHandler::requestSelection, this,
          [this](int index) {
            selectItemByIndex(index, true);
            if (m_viewportManager) {
              m_viewportManager->centerItemVertically(index, true);
            }
          });
}

void InteractionManager::installEventFilters() {
  if (qApp) {
    qApp->installEventFilter(this);
  }
  if (m_itemsPage) {
    m_itemsPage->installEventFilter(this);
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->installEventFilter(this);
    QWidget *viewport = m_itemScrollArea->viewport();
    if (viewport) {
      viewport->installEventFilter(this);
    }
  }
  if (m_gridContainer) {
    m_gridContainer->installEventFilter(this);
  }
}

void InteractionManager::connectSearchManagerSignals() {
  connect(m_searchManager.get(), &SearchManager::requestClearSelection,
          this, &InteractionManager::clearSelection);
  connect(m_searchManager.get(), &SearchManager::requestSelectionRestore,
          this, [this](int index) {
            if (m_navigationManager) {
              m_navigationManager->scheduleSelectionRestore(
                  index, UIConstants::Selection::RESTORE_STEPS,
                  UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                  UIConstants::Selection::RESTORE_MAX_DELAY_MS);
            }
          });
  connect(m_searchManager.get(), &SearchManager::requestScrollbarRecovery,
          this, &InteractionManager::scheduleScrollbarRecovery);
}

void InteractionManager::connectSelectionManagerSignals() {
  connect(m_selectionManager.get(), &SelectionManager::selectionChanged,
          this, [this](int index) { emit selectionChanged(index); });
  connect(m_selectionManager.get(), &SelectionManager::requestFocusItemsPage,
          this, [this]() {
            if (m_itemsPage) {
              m_itemsPage->setFocus();
            }
          });
  connect(m_selectionManager.get(), &SelectionManager::requestStopScrollAnimations,
          this, [this]() {
            if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
              m_animationManager->verticalAnimation()->stop();
            }
          });
  connect(m_selectionManager.get(), &SelectionManager::requestCenterVertically,
          this, &InteractionManager::centerItemVertically);
  connect(m_selectionManager.get(), &SelectionManager::requestEnsureHorizontallyVisible,
          this, &InteractionManager::ensureHorizontallyVisible);
  connect(m_selectionManager.get(), &SelectionManager::requestStopRepeat,
          this, [this]() { stopRepeat(); });
}

void InteractionManager::connectKeyboardManagerSignals() {
  connect(m_keyboardManager.get(), &KeyboardManager::requestSelectionMove,
          this, &InteractionManager::handleArrowKeyNavigation);
  connect(m_keyboardManager.get(), &KeyboardManager::requestAlphabeticNavigation,
          this, &InteractionManager::handleAlphabeticNavigation);
  connect(m_keyboardManager.get(), &KeyboardManager::requestJumpToEdge,
          this, &InteractionManager::handleJumpToEdge);
  connect(m_keyboardManager.get(), &KeyboardManager::requestEnterAction,
          this, [this]() {
            if (m_scrollManager) {
              const int totalItems = m_scrollManager->getTotalItems();
              processEnterOrReturnKey(totalItems);
            }
          });
  connect(m_keyboardManager.get(), &KeyboardManager::requestSearchModeToggle,
          this, &InteractionManager::toggleSearchMode);
  connect(m_keyboardManager.get(), &KeyboardManager::requestSearchBarFocus,
          this, [this]() { (void)handleSlashKey(); });
  connect(m_keyboardManager.get(), &KeyboardManager::requestEscapeAction,
          this, [this]() { (void)handleEscapeKey(); });
  connect(m_keyboardManager.get(), &KeyboardManager::requestClearSearchBar,
          this, [this]() {
            if (m_searchBar) {
              m_state.search().clearedByEscape = true;
              m_searchBar->clear();
            }
          });
  connect(m_keyboardManager.get(), &KeyboardManager::requestFocusGrid,
          this, [this]() {
            if (m_gridContainer) {
              m_gridContainer->setFocus(Qt::OtherFocusReason);
            }
          });
  connect(m_keyboardManager.get(), &KeyboardManager::requestScrollAnimationStop,
          this, [this]() {
            if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
              m_animationManager->verticalAnimation()->stop();
            }
          });
  connect(m_keyboardManager.get(), &KeyboardManager::repeatStepRequested,
          this, &InteractionManager::onKeyboardRepeatStep);
  connect(m_keyboardManager.get(), &KeyboardManager::stopRepeatRequested,
          this, &InteractionManager::onKeyboardStopRepeat);
}

void InteractionManager::connectAnimationManagerSignals() {
  connect(m_animationManager.get(),
          &AnimationManager::requestVirtualViewUpdate, this, [this]() {
            if (m_scrollManager) {
              m_scrollManager->updateVirtualView();
            }
          });
  connect(m_animationManager.get(),
          &AnimationManager::requestSelectionUpdate, this, [this]() {
            if (m_scrollManager) {
              int idxDyn = m_state.isSelectionSuppressed()
                               ? m_state.pendingSelectionIndex()
                               : currentSelectedIndex();
              if (idxDyn >= 0) {
                m_scrollManager->updateSelectionForIndex(idxDyn);
              }
            }
          });
  connect(m_animationManager.get(),
          &AnimationManager::requestSelectionOverlayRefresh, this, [this]() {
            if (m_scrollManager) {
              m_scrollManager->refreshSelectionOverlayState();
            }
          });
  connect(m_animationManager.get(),
          &AnimationManager::requestGlideAnimationStart, this, [this]() {
            if (m_gridContainer) {
              m_state.setGlideAnimating(true);
              if (m_scrollManager) {
                m_scrollManager->refreshSelectionOverlayState();
              }
            }
          });
  connect(m_animationManager.get(),
          &AnimationManager::horizontalAnimationFinished, this, [this]() {
            if (m_gridContainer) {
              m_state.setGlideAnimating(false);
              if (m_scrollManager) {
                m_scrollManager->refreshSelectionOverlayState();
              }
            }
          });
}

void InteractionManager::connectMouseManagerSignals() {
  connect(m_mouseManager.get(), &MouseManager::scrollStepRequested, this,
          &InteractionManager::onMouseHoldScrollStep);
  connect(m_mouseManager.get(), &MouseManager::holdScrollingStarted, this,
          [this](bool isHorizontal) {
            Q_UNUSED(isHorizontal);
            if (m_viewportManager) {
              m_viewportManager->setContinuousScrollActive(true);
              m_viewportManager->setRepeating(true);
              m_viewportManager->setPhysicalKeyDown(true);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::holdScrollingStopped, this,
          [this]() {
            if (m_state.isSelectionSuppressed()) {
              int pending = m_state.endSelectionSuppression();
              if (pending >= 0) {
                selectItemByIndex(pending, true);
              }
            }
            bool repeating = m_viewportManager ? m_viewportManager->isRepeating() : false;
            if (!repeating) {
              if (m_viewportManager) {
                m_viewportManager->setContinuousScrollActive(false);
                m_viewportManager->setPhysicalKeyDown(false);
              }
              m_state.artwork().suppressArtwork = false;
              m_state.artwork().allowDuringSelection = true;
            }
            if (m_viewportManager) {
              m_viewportManager->setWrapSequenceActive(false);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::requestSelectionUpdate, this,
          [this](int index) {
            if (index >= 0) {
              QList<int> subs = getSubcollections(*m_currentCollectionIndex);
              if (m_selectionManager) {
                m_selectionManager->setSelectedIndex(index);
              }
              updateFilePathForSelection(index, subs);
              if (m_scrollManager) {
                m_scrollManager->updateSelectionForIndex(index);
              }
              selectItemByIndex(index, true);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::requestOverlayVisibility, this,
          [this](bool visible) {
            if (m_scrollManager) {
              m_scrollManager->setForceSelectionOverlayVisible(visible);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::requestScrollAreaProperty, this,
          [this](const char *name, bool value) {
            if (m_itemScrollArea) {
              m_itemScrollArea->setProperty(name, value);
            }
          });
  connect(m_mouseManager.get(), &MouseManager::requestSetProperty, this,
          [this](const char *name, const QVariant &value) {
            setProperty(name, value);
          });
}

void InteractionManager::connectViewportManagerSignals() {
  connect(m_viewportManager.get(), &ViewportManager::requestSelectionUpdate,
          this, [this](int idxDyn) {
            if (m_scrollManager) {
              int idx = (idxDyn >= 0) ? idxDyn : currentSelectedIndex();
              if (idx >= 0) {
                m_scrollManager->updateSelectionForIndex(idx);
              }
            }
          });
}

void InteractionManager::connectEventManagerSignals() {
  connect(m_eventManager.get(), &EventManager::widgetDoubleClicked,
          this, &InteractionManager::handleWidgetDoubleClickedWithCollection);
  connect(m_eventManager.get(), &EventManager::widgetClicked,
          this, [this](ItemWidget *widget, const QPoint &clickPos, QMouseEvent *event) {
            if (m_selectionManager) {
              const int clickedIndex = m_selectionManager->handleWidgetSelection(widget, clickPos, event);
              if (clickedIndex >= 0 && m_mouseManager) {
                const int gridWidth = getCurrentGridWidth();
                const int totalItems = m_scrollManager ? m_scrollManager->getTotalItems() : 0;
                const int previousSelection = currentSelectedIndex();
                m_mouseManager->updateClickHoldHorizontalCandidate(previousSelection, clickedIndex, gridWidth);
                m_mouseManager->startClickHoldTimer(clickPos, clickedIndex, gridWidth, totalItems);
              }
            }
          });
  connect(m_eventManager.get(), &EventManager::clearSelectionRequested,
          this, &InteractionManager::clearSelectionAndFocus);
  connect(m_eventManager.get(), &EventManager::requestStopRepeat,
          this, &InteractionManager::stopRepeat);
}

// KeyboardManager callback: handles arrow key navigation
void InteractionManager::handleArrowKeyNavigation(int direction, bool vertical) {
  bool restoringSelection = m_selectionManager && m_selectionManager->isRestoringSelection();
  if (restoringSelection || m_navigationInProgress) {
    return;
  }
  if (m_arrowHandler) {
    m_arrowHandler->handleArrowKeyNavigation(direction, vertical);
  }
}

// KeyboardManager callback: handles alphabetic navigation via PageUp/PageDown
void InteractionManager::handleAlphabeticNavigation(bool forward) {
  bool restoringSelection = m_selectionManager && m_selectionManager->isRestoringSelection();
  if (restoringSelection || m_navigationInProgress) {
    return;
  }
  if (m_alphabeticHandler) {
    (void)m_alphabeticHandler->navigateToNextLetter(forward);
  }
}

// KeyboardManager callback: handles Home/End key to jump to first/last item
void InteractionManager::handleJumpToEdge(bool toEnd) {
  bool restoringSelection = m_selectionManager && m_selectionManager->isRestoringSelection();
  if (restoringSelection || m_navigationInProgress) {
    return;
  }
  if (!m_scrollManager) {
    return;
  }
  
  const int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    return;
  }
  
  // Stop any ongoing scroll animation
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
  }
  
  // Calculate target index: 0 for Home, last item for End
  const int targetIndex = toEnd ? (totalItems - 1) : 0;
  const int currentIndex = currentSelectedIndex();
  
  if (targetIndex == currentIndex) {
    return; // Already at edge
  }
  
  // Select and center on the target item with immediate viewport jump
  selectItemByIndex(targetIndex, true);
  if (m_viewportManager) {
    m_viewportManager->centerItemVertically(targetIndex, true);
  }
}

// KeyboardManager callback: handles repeat step during key hold
void InteractionManager::onKeyboardRepeatStep() {
  if (m_arrowHandler) {
    m_arrowHandler->handleRepeatStep();
  }
}

// KeyboardManager callback: handles cleanup when key hold stops
void InteractionManager::onKeyboardStopRepeat(bool suppressRecentering) {
  if (m_arrowHandler) {
    m_arrowHandler->handleStopRepeat(suppressRecentering);
  }
}

// Handles global key presses; ESC clears search, navigates to parent only if
// subcollection, otherwise does nothing
auto InteractionManager::handleGlobalKeyPress(QKeyEvent *event) -> bool {
  if (!event) {
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
  if (!m_searchBar || !m_searchBar->isVisible()) {
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
      (m_searchBar ? m_searchBar->text().trimmed() : QString());
  const bool searchBarFocused = (m_searchBar && m_searchBar->hasFocus());
  
  if (!current.isEmpty()) {
    if (m_searchBar) {
      m_state.search().clearedByEscape = true;
      m_searchBar->clear();
      // Keep focus on search bar when clearing text
    }
    return true;
  }
  
  // If search bar is focused but empty, return focus to grid
  if (searchBarFocused) {
    if (m_gridContainer) {
      m_gridContainer->setFocus(Qt::OtherFocusReason);
    }
    return true;
  }

  const int collIndex =
      (m_currentCollectionIndex ? *m_currentCollectionIndex : -1);
  if (m_collections && collIndex >= 0 &&
      collIndex < m_collections->size()) {
    const CollectionConfig &cfg = (*m_collections)[collIndex];
    
    // First check if we're in a virtual subfolder - go back one level
    if (!cfg.currentSubfolder.isEmpty()) {
      if (m_navigationManager) {
        m_navigationManager->goBackFromVirtualFolder();
      }
      return true;
    }
    
    // Then check if we're in a subcollection - go to parent
    if (cfg.isSubcollection && cfg.parentCollectionIndex >= 0 &&
        cfg.parentCollectionIndex < m_collections->size()) {
      if (m_navigationManager) {
        constexpr int kRestoreAttempts = UIConstants::Selection::RESTORE_STEPS;
        constexpr int kRestoreIntervalMs =
            UIConstants::Selection::RESTORE_STEP_DELAY_MS;
        constexpr int kRestoreTimeoutMs =
            UIConstants::Selection::RESTORE_MAX_DELAY_MS;
        const int parent = cfg.parentCollectionIndex;
        m_navigationManager->showCollectionItems(parent);
        int sel = -1;
        if (m_settingsManager) {
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

void InteractionManager::handleImmediateSearchTextChanged(const QString &text) {
  updateSearchBarPlaceholder();
  if (m_searchManager) {
    m_searchManager->onSearchTextChanged(text, currentSelectedIndex());
  }
}

// Helper: pick a currently selected visual index if any
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
      int owner =
          m_databaseManager->getCollectionIndexForFile(selectedPath);
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
void InteractionManager::handleWidgetDoubleClickedWithCollection(
    const QString &filePath, int collectionIndex) {
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
    launchItemWithCollection(path, collIdx);
    return;
  }
  const int fallbackIdx = getFallbackCollectionIndex();
  QString selectedPath = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (fallbackIdx >= 0 && !selectedPath.isEmpty()) {
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
void InteractionManager::updateFilePathForSelection(
    int index, const QList<int> &subcollections) {
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

auto InteractionManager::processEnterOrReturnKey(int totalItems) -> bool {
  const int currentSelection = std::max(0, currentSelectedIndex());
  if (currentSelection < 0 || currentSelection >= totalItems) {
    return true;
  }
  const QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  if (currentSelection < subs.size()) {
    return handleEnterOnSubcollection(currentSelection, subs);
  }
  
  // Check if this is a virtual folder
  if (m_scrollManager) {
    QString folderPath = m_scrollManager->virtualFolderPathForVisualIndex(currentSelection);
    if (!folderPath.isEmpty()) {
      return handleEnterOnVirtualFolder(folderPath);
    }
  }
  
  return handleEnterOnItem(currentSelection, totalItems);
}

auto InteractionManager::handleEnterOnSubcollection(int currentSelection,
                                                    const QList<int> &subs)
    -> bool {
  saveCurrentSelection();
  const int subIdx = subs[currentSelection];
  if (m_navigationManager) {
    if (*m_currentCollectionIndex >= 0 &&
        *m_currentCollectionIndex < m_collections->size()) {
      m_navigationManager->stackManager()->push(*m_currentCollectionIndex);
    }
    clearSelectionAndFocus();
    if (m_sidebarManager) {
      m_sidebarManager->updateSidebarMetadata(nullptr);
    }
    const bool success = m_navigationManager->showCollectionItems(subIdx);
    if (!success) {
      // Undo the push if navigation failed
      (void)m_navigationManager->stackManager()->pop();
      selectItemByIndex(currentSelection, true);
      if (m_itemsPage) {
        m_itemsPage->setFocus();
      }
    } else {
      // Delay horizontal centering until after subcollection navigation animations
      // complete and layout is stable
      constexpr int kHorizontalCenterDelayMs = 600;
      QTimer::singleShot(kHorizontalCenterDelayMs, this, [this]() {
        if (!QApplication::closingDown() && m_scrollManager) {
          m_scrollManager->centerHorizontalScrollbar(*m_currentCollectionIndex,
                                                     *m_collections);
        }
      });
    }
  }
  return true;
}

auto InteractionManager::handleEnterOnVirtualFolder(const QString &folderPath) -> bool {
  if (m_navigationManager) {
    m_navigationManager->onVirtualFolderEntered(folderPath);
  }
  return true;
}

auto InteractionManager::handleEnterOnItem(int currentSelection,
                                           int /*totalItems*/) -> bool {
  QString path = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (m_scrollManager)) {
    path = m_scrollManager->filePathForVisualIndex(currentSelection);
  }
  if (!path.isEmpty()) {
    saveCurrentSelection();
    const int cIdx = ((m_databaseManager)
                          ? m_databaseManager->getCollectionIndexForFile(path)
                          : -1);
    const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
    launchItemWithCollection(path, ownerIdx);
  }
  return true;
}

auto InteractionManager::isItemOffscreen(int selection, int gridWidth) const
    -> bool {
  if (!m_itemScrollArea || selection < 0 || gridWidth <= 0 ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }
  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vbar = m_itemScrollArea->verticalScrollBar();
  if (!vbar) {
    return false;
  }
  const int viewportH = m_itemScrollArea->viewport()->height();
  if (viewportH <= 0) {
    return false;
  }
  const int itemY = GridUtils::computeItemY(
      selection, gridWidth, collection.itemHeight, collection.verticalSpacing,
      UIConstants::Grid::MARGINS);
  const int visibleTop = vbar->value();
  const int visibleBottom = visibleTop + viewportH;
  return (itemY + collection.itemHeight) <= visibleTop ||
         itemY >= visibleBottom;
}

void InteractionManager::applyMinorHorizontalSuppress() {
  constexpr qint64 kMinorHorizSuppressMs = 220;
  constexpr int kMinorHorizSuppressClearMs = 240;
  m_state.suppressArrowCenterFor(kMinorHorizSuppressMs);
  // Clear arrow center suppression slightly after the suppression window expires -
  // ensures horizontal navigation completes before vertical centering resumes
  QTimer::singleShot(kMinorHorizSuppressClearMs, this, [this]() {
    m_state.clearArrowCenterSuppression();
  });
}

void InteractionManager::setPendingSelectionIfNeeded(bool condition,
                                                     int newSelection) {
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

void InteractionManager::ensureItemVisible(int index,
                                           bool allowHorizontalScroll) {
  if (m_viewportManager) {
    m_viewportManager->ensureItemVisible(index, allowHorizontalScroll);
  }
}

void InteractionManager::selectItemByIndex(int index,
                                           bool allowHorizontalScroll) {
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

  ItemWidget *widget = m_selectionManager
      ? m_selectionManager->widgetForIndex(index)
      : nullptr;
  bool suppressed =
      m_state.click().selectionSuppressed &&
      m_state.click().pendingSelectionIndex == index;
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

void InteractionManager::persistSuppressedSelectionAndMaybeCenter(
    int index, const QList<int> &subcollections, bool skipCenter) {
  bool deferCenter =
      m_state.click().deferCenterOnClick &&
      m_state.click().deferredCenterIndex == index;
  if (!deferCenter && !skipCenter) {
    centerItemVertically(index, false);
  }
  int curColl =
      ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if (m_collections && curColl >= 0 &&
      curColl < m_collections->size() && m_selectionManager) {
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
auto InteractionManager::getSubcollections(int parentIndex) const
    -> QList<int> {
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

void InteractionManager::trySelectWidget(int index,
                                         const QList<int> &subcollections,
                                         int attempt) {
  if ((!m_scrollManager) || currentSelectedIndex() != index ||
      QApplication::closingDown()) {
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

  ItemWidget *widget = m_selectionManager
      ? m_selectionManager->widgetForIndex(index)
      : nullptr;

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
void InteractionManager::toggleSearchMode() {
  if (!m_searchManager) {
    return;
  }

  // Delegate to SearchManager
  m_searchManager->toggleSearchMode();

  // Do not reload or change the grid when search text is empty.
  // Only reapply results if the user has entered text.
  if ((m_searchBar) && !m_searchBar->text().trimmed().isEmpty()) {
    m_searchManager->onSearchTextChanged(m_searchBar->text(), currentSelectedIndex());
  }
}

void InteractionManager::saveCurrentSelection() {
  const int selected = currentSelectedIndex();
  if (selected >= 0) {
    handleSuccessfulSelection(selected);
  }
}

// Updates the search mode button icon/tooltip without coercing the current mode
void InteractionManager::updateSearchModeButton() {
  if (m_searchManager) {
    m_searchManager->updateSearchModeButton();
  }
}

// Updates the search bar placeholder/text style without coercing the current
// mode
void InteractionManager::updateSearchBarPlaceholder() {
  if (m_searchManager) {
    m_searchManager->updateSearchBarPlaceholder();
  }
}

// Restores selection instantly, ensures viewport positioning, and updates
// sidebar metadata
void InteractionManager::beginSelectionRestore(int targetIndex) {
  debugLog("[SelectionRestore] beginSelectionRestore: targetIndex=" << targetIndex);
  if (targetIndex < 0) {
    return;
  }

  // Check if user has made an explicit selection since navigation started -
  // if so, don't override their choice with automatic restore
  if (m_state.selectionRestore().userSelectionMade) {
    debugLog("[SelectionRestore] Skipping restore - user made explicit selection");
    return;
  }

  // Use SelectionManager for preparation
  if (m_selectionManager) {
    m_selectionManager->prepareForRestore(targetIndex);
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(m_selectionManager->forceImmediateCenter());
    }
  }

  // Stop any running scroll animations
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    m_animationManager->verticalAnimation()->stop();
  }

  applySelectionStateForIndex(targetIndex);
  if (m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(targetIndex);
  }
  selectItemByIndex(targetIndex, false);

  if (currentSelectedIndex() == targetIndex) {
    finalizeRestoreFlagsAndFocus();
    emit selectionChanged(targetIndex);
  }

  // Finalize restore state
  if (m_selectionManager) {
    m_selectionManager->finalizeRestore();
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(m_selectionManager->forceImmediateCenter());
    }
  }

  if ((m_sidebarManager) && m_sidebarManager->isSidebarVisible()) {
    ItemWidget *widget = nullptr;
    if (m_selectionManager) {
      widget = m_selectionManager->widgetForIndex(targetIndex);
    } else if (m_scrollManager) {
      const auto &active = m_scrollManager->getActiveWidgets();
      widget = active.value(targetIndex, nullptr);
    }
    if (widget) {
      m_sidebarManager->updateSidebarMetadata(widget);
    }
    constexpr int kMetadataSidebarUpdateDelayMs = 120;
    scheduleSidebarMetadataUpdateIfVisible(targetIndex, 0,
                                           kMetadataSidebarUpdateDelayMs);
  }
}

void InteractionManager::applySelectionStateForIndex(int idx) {
  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(idx);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(idx, subs);
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
    m_scrollManager->updateSelectionForIndex(idx);
  }
}

void InteractionManager::finalizeRestoreFlagsAndFocus() {
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  // Only set focus to items page if search bar doesn't currently have focus
  if ((m_itemsPage) && !m_itemsPage->hasFocus()) {
    if (!m_searchBar || !m_searchBar->hasFocus()) {
      m_itemsPage->setFocus();
    }
  }
  // Clear arrow center suppression after restore completes - ensures the
  // selection is fully visible before allowing subsequent centering operations
  QTimer::singleShot(UIConstants::Keyboard::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS, this,
                     [this]() {
                       m_state.clearArrowCenterSuppression();
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
      ItemWidget *itemWidget =
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

// Handles search text debounce; uses a state flag to avoid refocusing after
// ESC-clears
namespace {
struct ResetClearedFlag {
  InteractionStateHolder *state = nullptr;
  ~ResetClearedFlag() {
    if (state) {
      state->search().clearedByEscape = false;
    }
  }
};
} // namespace

// Schedules repeated attempts plus a layout-complete hook to restore vertical
// scrollbar visibility after clearing search
void InteractionManager::scheduleScrollbarRecovery() {
  if (!m_itemScrollArea || !m_scrollManager ||
      !m_collections || !m_currentCollectionIndex) {
    return;
  }
  int idx = *m_currentCollectionIndex;
  if (!CollectionUtils::isValidIndex(idx, m_collections)) {
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
    if (!guard->m_scrollManager ||
        !guard->m_itemScrollArea) {
      return;
    }
    guard->m_scrollManager->recalculateContainerMetrics();
    if (guard->m_viewportManager) {
      guard->m_viewportManager->ensureVerticalScrollbarPolicy();
    }
    QScrollBar *verticalScrollBar =
        guard->m_itemScrollArea->verticalScrollBar();
    if (verticalScrollBar && verticalScrollBar->maximum() > 0) {
      guard->m_itemScrollArea->setVerticalScrollBarPolicy(
          Qt::ScrollBarAsNeeded);
    }
  };

  attempt();
  // Retry scrollbar recovery at increasing intervals - handles race conditions
  // where the scrollbar maximum isn't set immediately after collection load
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_1_MS, this,
                     attempt);
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_2_MS, this,
                     attempt);
  QTimer::singleShot(UIConstants::Navigation::SCROLLBAR_RECOVERY_ATTEMPT_3_MS, this,
                     attempt);

  if (!m_scrollbarRecoveryConn) {
    m_scrollbarRecoveryConn = QObject::connect(
        m_scrollManager, &ScrollManager::virtualScrollSetupComplete, this,
        [guard]() {
          if (!guard) {
            return;
          }
          if (guard->m_viewportManager) {
            guard->m_viewportManager->ensureVerticalScrollbarPolicy();
          }
          if (guard->m_scrollbarRecoveryConn) {
            QObject::disconnect(guard->m_scrollbarRecoveryConn);
            guard->m_scrollbarRecoveryConn = QMetaObject::Connection();
          }
        });
  }
}

// Initialize search mode for the current collection; reset away from
// AllCollections and prefer collection defaults
void InteractionManager::initializeSearchModeForCurrentCollection() {
  if (m_searchManager) {
    m_searchManager->initializeSearchModeForCurrentCollection();
  }
}

// Launches an item using the collection's configured launcher; expands
// variables without path validation so launch works even if artworkDirectory is
// Delegates to LaunchManager for launching media items
void InteractionManager::launchItemWithCollection(const QString &filePath,
                                                  int collectionIndex) {
  if (m_launchManager) {
    m_launchManager->recordLaunch(filePath);
    m_launchManager->launchItem(filePath, collectionIndex);
  }
}

// Stops key/mouse repeat navigation and restores artwork / centering properties
void InteractionManager::stopRepeat(bool suppressRecentering) {
  if (m_isShuttingDown || QApplication::closingDown()) {
    if (m_viewportManager) {
      m_viewportManager->setRepeating(false);
      m_viewportManager->setWrapSequenceActive(false);
    }
    m_state.scroll().keyContinuous = false;
    return;
  }

  // Delegate to KeyboardManager for timer/state cleanup
  if (m_keyboardManager) {
    m_keyboardManager->stopRepeat(suppressRecentering);
  }

  // Stop mouse hold scrolling if active
  if (m_mouseManager && m_mouseManager->isMouseHoldScrolling()) {
    m_mouseManager->stopMouseHoldScrolling();
  }

  if (m_viewportManager) {
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  m_state.scroll().horizHoldActive = false;
  m_state.scroll().keyContinuous = false;
  m_state.click().armFirstClickDelay = false;
  m_state.click().pendingInitialCenter = false;

  if (m_gridContainer) {
    m_state.arrow().arrowKeyScrolling = false;
    m_state.setGlideAnimating(false);
    if (m_scrollManager) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  if (m_scrollManager) {
    m_scrollManager->setForceSelectionOverlayVisible(false);
  }

  if (m_itemScrollArea) {
    m_state.artwork().suppressArtwork = false;
    m_state.artwork().allowDuringSelection = true;
    m_state.clearArrowCenterSuppression();
  }

  if (m_animationManager && m_animationManager->isHorizontalAnimRunning()) {
    m_animationManager->horizontalAnimation()->stop();
  }

  if (m_state.click().selectionSuppressed) {
    int pending = m_state.click().pendingSelectionIndex;
    if (pending >= 0) {
      selectItemByIndex(pending, true);
    }
    m_state.click().selectionSuppressed = false;
    m_state.click().pendingSelectionIndex = -1;
  }

  if (m_viewportManager && !m_viewportManager->physicalKeyDown()) {
    m_viewportManager->setContinuousScrollActive(
        m_animationManager && m_animationManager->isVerticalAnimRunning());
  }

  const int selected = currentSelectedIndex();
  if (!QApplication::closingDown() && selected >= 0 &&
      !suppressRecentering) {
    // Delay re-centering to allow scroll animations to settle after key repeat stops
    QTimer::singleShot(
        UIConstants::Mouse::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
          const int sel = currentSelectedIndex();
          if (!QApplication::closingDown() && sel >= 0 &&
              m_viewportManager && !m_viewportManager->continuousScrollActive()) {
            centerItemVertically(sel, false);
          }
        });
  }
}

auto InteractionManager::isWheelScrolling() const -> bool {
  return m_mouseManager ? m_mouseManager->isWheelScrolling() : false;
}

// Advances selection during mouse-hold scrolling (called via MouseManager signal)
void InteractionManager::onMouseHoldScrollStep(int direction, bool isHorizontal) {
  if (!m_scrollManager || !m_collections ||
      !m_currentCollectionIndex) {
    if (m_mouseManager) {
      m_mouseManager->stopMouseHoldScrolling();
    }
    return;
  }

  int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    if (m_mouseManager) {
      m_mouseManager->stopMouseHoldScrolling();
    }
    return;
  }

  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    if (m_mouseManager) {
      m_mouseManager->stopMouseHoldScrolling();
    }
    return;
  }

  if (isHorizontal) {
    int currentIndex = std::max(0, currentSelectedIndex());
    bool wrap = m_generalSettings
                    ? m_generalSettings->wrapNavigation
                    : false;
    bool didWrap = false;
    int nextIndex = KeyboardManager::calculateHorizontalSelection(
        totalItems, currentIndex, direction, wrap, didWrap);

    if (nextIndex == currentIndex) {
      return;
    }

    if (m_viewportManager) {
      if (didWrap) {
        m_viewportManager->setForceImmediateCenter(true);
        m_viewportManager->setWrapSequenceActive(true);
        m_viewportManager->setContinuousScrollActive(false);
      } else {
        m_viewportManager->setContinuousScrollActive(true);
      }
    }

    bool rowChanged = KeyboardManager::hasRowChanged(gridWidth, currentIndex, nextIndex);
    if (rowChanged) {
      m_state.click().selectionSuppressed = true;
      m_state.click().pendingSelectionIndex = nextIndex;
    }

    if (m_selectionManager) {
      m_selectionManager->setSelectedIndex(nextIndex);
    }
    QList<int> subs = getSubcollections(*m_currentCollectionIndex);
    updateFilePathForSelection(nextIndex, subs);
    
    // Set properties before selectItemByIndex so the selection update knows we're in hold mode
    m_state.scroll().clickScroll = true;
    m_state.scroll().clickHoldAdvancing = true;
    
    // selectItemByIndex will call updateSelectionForIndex internally
    selectItemByIndex(nextIndex, true);

    if (rowChanged) {
      centerItemVertically(nextIndex, false);
    } else {
      ensureHorizontallyVisible(nextIndex);
    }
    return;
  }

  // Vertical scrolling
  int currentIndex = std::max(0, currentSelectedIndex());
  int nextIndex = currentIndex + (direction * gridWidth);

  bool wrap = m_generalSettings
                  ? m_generalSettings->wrapNavigation
                  : false;
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

  if (m_viewportManager) {
    if (didWrap) {
      m_viewportManager->setForceImmediateCenter(true);
      m_viewportManager->setWrapSequenceActive(true);
      m_viewportManager->setContinuousScrollActive(false);
    } else {
      m_viewportManager->setContinuousScrollActive(true);
    }
  }

  if (*m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size() && gridWidth > 0) {
    int currentRow = (gridWidth > 0 ? currentIndex / gridWidth : -1);
    int targetRow = nextIndex / gridWidth;
    if (currentRow != targetRow) {
      m_state.click().selectionSuppressed = true;
      m_state.click().pendingSelectionIndex = nextIndex;
    }
  }

  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(nextIndex);
  }
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(nextIndex, subs);
  
  // Set properties before selectItemByIndex so the selection update knows we're in hold mode
  m_state.scroll().clickScroll = true;
  m_state.scroll().clickHoldAdvancing = true;
  
  // selectItemByIndex will call updateSelectionForIndex internally
  selectItemByIndex(nextIndex, true);

  centerItemVertically(nextIndex, false);
}

// Finalizes selection bookkeeping and persists selection; standardizes property
// key for user-free-scroll
void InteractionManager::handleSuccessfulSelection(int index) {
  m_state.scroll().userFreeScroll = false;
  
  bool restoringMatch = false;
  if (m_selectionManager) {
    restoringMatch = m_selectionManager->checkAndFinalizeRestore(index);
  }
  
  if ((m_isShuttingDown) && *m_isShuttingDown) {
    return;
  }

  int currentColl =
      ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if ((m_collections) && currentColl >= 0 && index >= 0) {
    persistSelectionForIndex(currentColl, index);
  }
  if (QApplication::closingDown()) {
    return;
  }

  bool immediate = (m_viewportManager && m_viewportManager->forceImmediateCenter()) || restoringMatch;
  centerItemVertically(index, immediate);
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
  }
}

auto InteractionManager::titleForIndexInColl(int coll, int idx) const
    -> QString {
  QList<int> subs = getSubcollections(coll);
  if (idx < subs.size()) {
    int subIdx = subs[idx];
    if (m_collections && subIdx >= 0 && subIdx < m_collections->size()) {
      return (*m_collections)[subIdx].name;
    }
    return {};
  }
  QString path = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (m_scrollManager)) {
    path = m_scrollManager->filePathForVisualIndex(idx);
  }
  if (!path.isEmpty()) {
    return QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  return {};
}

void InteractionManager::persistSelectionForIndex(int coll, int idx) {
  if (!m_settingsManager ||
      !CollectionUtils::isValidIndex(coll, m_collections)) {
    return;
  }
  m_settingsManager->setLastSelectedItem(coll, idx);
  // Use hierarchical name to match how calculateSelectionIndex looks it up
  QString collectionName = CollectionUtils::hierarchicalNameFor(
      (*m_collections)[coll], *m_collections);
  QString title;
  QString path = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (m_scrollManager)) {
    path = m_scrollManager->filePathForVisualIndex(idx);
  }
  if (!path.isEmpty()) {
    title = QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  if (m_sessionManager) {
    m_sessionManager->setLastSelected(collectionName, idx, title);
  }
  // Defer artwork update to allow UI state to settle after selection save
  QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && m_artworkManager) {
      m_artworkManager->updateViewportArtwork();
    }
  });
}

void InteractionManager::cancelPendingSelectionRestore() {
  if (m_selectionManager) {
    m_selectionManager->cancelPendingSelectionRestore();
  }
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
}

void InteractionManager::resetSelectionRestoreState() {
  if (m_selectionManager) {
    m_selectionManager->resetSelectionRestoreState();
  }
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
}
