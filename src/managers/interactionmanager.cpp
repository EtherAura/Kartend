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

#include "animationmanager.h"
#include "artworkmanager.h"
#include "databasemanager.h"
#include "eventmanager.h"
#include "gridutils.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "mousemanager.h"
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
#include "viewportmanager.h"

InteractionManager::InteractionManager(QObject *parent) : QObject(parent) {
  m_searchManager = std::make_unique<SearchManager>(this);
  m_selectionManager = std::make_unique<SelectionManager>(this);
  m_keyboardManager = std::make_unique<KeyboardManager>(this);
  m_animationManager = std::make_unique<AnimationManager>(this);
  m_mouseManager = std::make_unique<MouseManager>(this);
  m_launchManager = std::make_unique<LaunchManager>(this);
  m_viewportManager = std::make_unique<ViewportManager>(this);
  m_eventManager = std::make_unique<EventManager>(this);

  m_viewportManager->setContinuousScrollActive(true);
}

// Destructor: stop timers/animations and clear selection
InteractionManager::~InteractionManager() {
  m_isShuttingDown = true;
  stopRepeat();
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
  m_stackedWidget = setup.stackedWidget;
  m_itemsPage = setup.itemsPage;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_searchBar = setup.searchBar;
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
    selectionSetup.animationManager = m_animationManager.get();
    selectionSetup.viewportManager = m_viewportManager.get();
    selectionSetup.artworkManager = setup.artworkManager;
    selectionSetup.mainWindow = setup.mainWindow;
    selectionSetup.sidebar = setup.sidebar;
    selectionSetup.itemsPage = setup.itemsPage;
    selectionSetup.gridContainer = setup.gridContainer;
    selectionSetup.itemScrollArea = setup.itemScrollArea;
    selectionSetup.collections = setup.collections;
    selectionSetup.currentCollectionIndex = setup.currentCollectionIndex;
    m_selectionManager->setupReferences(selectionSetup);

    // Connect SelectionManager signals
    connect(m_selectionManager.get(), &SelectionManager::selectionChanged,
            this, [this](int index) {
              // Sync local selection state with SelectionManager
              m_selectedItemIndex = index;
              emit selectionChanged(index);
            });
    connect(m_selectionManager.get(), &SelectionManager::requestFocusItemsPage,
            this, [this]() {
              if (m_itemsPage != nullptr) {
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
              if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
                m_animationManager->verticalAnimation()->stop();
              }
            });
    connect(m_keyboardManager.get(), &KeyboardManager::repeatStepRequested,
            this, &InteractionManager::onKeyboardRepeatStep);
    connect(m_keyboardManager.get(), &KeyboardManager::stopRepeatRequested,
            this, &InteractionManager::onKeyboardStopRepeat);
  }

  // Setup AnimationManager with its dependencies
  if (m_animationManager) {
    AnimationManagerSetup animSetup;
    animSetup.itemScrollArea = setup.itemScrollArea;
    animSetup.scrollManager = setup.scrollManager;
    animSetup.artworkManager = setup.artworkManager;
    m_animationManager->setupReferences(animSetup);

    // Connect AnimationManager signals
    connect(m_animationManager.get(),
            &AnimationManager::requestVirtualViewUpdate, this, [this]() {
              if (m_scrollManager != nullptr) {
                m_scrollManager->updateVirtualView();
              }
            });
    connect(m_animationManager.get(),
            &AnimationManager::requestSelectionUpdate, this,
            [this]() {
              if (m_scrollManager != nullptr) {
                int idxDyn = property(PropertyKeys::SelectionSuppressed).toBool()
                                 ? property(PropertyKeys::PendingSelectionIndex).toInt()
                                 : m_selectedItemIndex;
                if (idxDyn >= 0) {
                  m_scrollManager->updateSelectionForIndex(idxDyn);
                }
              }
            });
    connect(m_animationManager.get(),
            &AnimationManager::requestSelectionOverlayRefresh, this,
            [this]() {
              if (m_scrollManager != nullptr) {
                m_scrollManager->refreshSelectionOverlayState();
              }
            });
    connect(m_animationManager.get(),
            &AnimationManager::requestGlideAnimationStart, this,
            [this]() {
              if (m_gridContainer != nullptr) {
                m_gridContainer->setProperty(PropertyKeys::GlideAnimating, true);
                if (m_scrollManager != nullptr) {
                  m_scrollManager->refreshSelectionOverlayState();
                }
              }
            });
    // Note: verticalAnimationFinished is connected in ViewportManager
    connect(m_animationManager.get(),
            &AnimationManager::horizontalAnimationFinished, this, [this]() {
              if (m_gridContainer) {
                m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
                if (m_scrollManager != nullptr) {
                  m_scrollManager->refreshSelectionOverlayState();
                }
              }
            });
  }

  // Setup MouseManager with its dependencies
  if (m_mouseManager) {
    MouseManagerSetup mouseSetup;
    mouseSetup.scrollManager = setup.scrollManager;
    mouseSetup.selectionManager = m_selectionManager.get();
    mouseSetup.mainWindow = setup.mainWindow;
    mouseSetup.itemScrollArea = setup.itemScrollArea;
    mouseSetup.gridContainer = setup.gridContainer;
    mouseSetup.collections = setup.collections;
    mouseSetup.currentCollectionIndex = setup.currentCollectionIndex;
    m_mouseManager->setupReferences(mouseSetup);

    // Connect MouseManager signals
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
              // Restore suppressed selection if any
              if (property(PropertyKeys::SelectionSuppressed).toBool()) {
                int pending = property(PropertyKeys::PendingSelectionIndex).toInt();
                if (pending >= 0) {
                  selectItemByIndex(pending, true);
                }
                setProperty(PropertyKeys::SelectionSuppressed, false);
                setProperty(PropertyKeys::PendingSelectionIndex, -1);
              }

              // Reset scroll state flags if not in keyboard repeat
              bool repeating = m_viewportManager ? m_viewportManager->isRepeating() : false;
              if (!repeating) {
                if (m_viewportManager) {
                  m_viewportManager->setContinuousScrollActive(false);
                  m_viewportManager->setPhysicalKeyDown(false);
                }
                if (m_itemScrollArea) {
                  m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
                  m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection, true);
                }
              }
              if (m_viewportManager) {
                m_viewportManager->setWrapSequenceActive(false);
              }
            });
    connect(m_mouseManager.get(), &MouseManager::requestSelectionUpdate, this,
            [this](int index) {
              if (index >= 0) {
                QList<int> subs = getSubcollections(*m_currentCollectionIndex);
                m_selectedItemIndex = index;
                if (m_selectionManager) {
                  m_selectionManager->setSelectedIndex(index);
                }
                updateFilePathForSelection(index, subs);
                if (m_scrollManager != nullptr) {
                  m_scrollManager->updateSelectionForIndex(index);
                }
                selectItemByIndex(index, true);
              }
            });
    connect(m_mouseManager.get(), &MouseManager::requestOverlayVisibility, this,
            [this](bool visible) {
              if (m_scrollManager != nullptr) {
                m_scrollManager->setForceSelectionOverlayVisible(visible);
              }
            });
    connect(m_mouseManager.get(), &MouseManager::requestScrollAreaProperty, this,
            [this](const char *name, bool value) {
              if (m_itemScrollArea != nullptr) {
                m_itemScrollArea->setProperty(name, value);
              }
            });
    connect(m_mouseManager.get(), &MouseManager::requestSetProperty, this,
            [this](const char *name, const QVariant &value) {
              setProperty(name, value);
            });
  }

  // Setup LaunchManager with its dependencies
  if (m_launchManager) {
    LaunchManagerSetup launchSetup;
    launchSetup.collections = setup.collections;
    m_launchManager->setupReferences(launchSetup);
  }

  // Setup ViewportManager with its dependencies
  if (m_viewportManager) {
    ViewportManagerSetup viewportSetup;
    viewportSetup.itemScrollArea = setup.itemScrollArea;
    viewportSetup.scrollManager = setup.scrollManager;
    viewportSetup.selectionManager = m_selectionManager.get();
    viewportSetup.animationManager = m_animationManager.get();
    viewportSetup.artworkManager = setup.artworkManager;
    viewportSetup.mainWindow = setup.mainWindow;
    viewportSetup.collections = setup.collections;
    viewportSetup.currentCollectionIndex = setup.currentCollectionIndex;
    m_viewportManager->setupReferences(viewportSetup);

    // Connect ViewportManager signals
    connect(m_viewportManager.get(), &ViewportManager::requestSelectionUpdate,
            this, [this](int idxDyn) {
              if (m_scrollManager != nullptr) {
                int idx = (idxDyn >= 0) ? idxDyn : m_selectedItemIndex;
                if (idx >= 0) {
                  m_scrollManager->updateSelectionForIndex(idx);
                }
              }
            });
  }

  // Setup EventManager with its dependencies
  if (m_eventManager) {
    m_eventManager->setScrollManager(setup.scrollManager);
    m_eventManager->setKeyboardManager(m_keyboardManager.get());
    m_eventManager->setMouseManager(m_mouseManager.get());
    m_eventManager->setAnimationManager(m_animationManager.get());
    m_eventManager->setViewportManager(m_viewportManager.get());
    m_eventManager->setSelectionManager(m_selectionManager.get());
    m_eventManager->setArtworkManager(setup.artworkManager);
    m_eventManager->setDatabaseManager(setup.databaseManager);
    m_eventManager->setSidebarManager(setup.sidebarManager);
    m_eventManager->setMainWindow(setup.mainWindow);
    m_eventManager->setItemScrollArea(setup.itemScrollArea);
    m_eventManager->setGridContainer(setup.gridContainer);
    m_eventManager->setStackedWidget(setup.stackedWidget);
    m_eventManager->setItemsPage(setup.itemsPage);
    m_eventManager->setSearchBar(setup.searchBar);
    m_eventManager->setCollections(setup.collections);
    m_eventManager->setCurrentCollectionIndex(setup.currentCollectionIndex);

    // Connect EventManager signals
    connect(m_eventManager.get(), &EventManager::widgetDoubleClicked,
            this, &InteractionManager::handleWidgetDoubleClickedWithCollection);
    connect(m_eventManager.get(), &EventManager::widgetClicked,
            this, [this](MediaItemWidget *widget, const QPoint &clickPos, QMouseEvent *event) {
              if (m_selectionManager) {
                const int clickedIndex = m_selectionManager->handleWidgetSelection(widget, clickPos, event);
                if (clickedIndex >= 0 && m_mouseManager) {
                  const int gridWidth = getCurrentGridWidth();
                  const int totalItems = m_scrollManager ? m_scrollManager->getTotalItems() : 0;
                  const int previousSelection = m_selectedItemIndex;
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

  // Clear user scroll state to ensure centering isn't blocked
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, true);
    m_itemScrollArea->setProperty(PropertyKeys::AllowArtworkDuringSelection,
                                  true);
  }
  setProperty(PropertyKeys::UserFreeScroll, false);

  const int gridWidth = getCurrentGridWidth();
  const int currentSelection =
      (m_selectedItemIndex < 0) ? 0 : m_selectedItemIndex;
  const bool offscreenBefore = isItemOffscreen(currentSelection, gridWidth);

  const bool wrapEnabled = (m_mainWindow != nullptr)
                               ? m_mainWindow->m_generalSettings.wrapNavigation
                               : false;
  if (m_viewportManager) {
    m_viewportManager->setIsWrappingNavigation(false);
  }
  if (m_keyboardManager) {
    m_keyboardManager->setWrapSequenceActive(false);
  }

  bool didWrap = false;
  const int newSelection =
      KeyboardManager::calculateNewSelection(totalItems, currentSelection, direction,
                            wrapEnabled, vertical, gridWidth, didWrap);
  if (didWrap) {
    if (m_viewportManager) {
      m_viewportManager->setIsWrappingNavigation(true);
    }
    if (m_keyboardManager) {
      m_keyboardManager->setWrapSequenceActive(true);
    }
  }

  const bool isNewRow =
      SelectionManager::isNewRow(currentSelection, newSelection, gridWidth);

  const bool isWrapping = m_viewportManager ? m_viewportManager->isWrappingNavigation() : false;
  const bool forceImmediate = offscreenBefore || isWrapping;
  if (forceImmediate && m_viewportManager) {
    m_viewportManager->applyImmediateCenterSuppression();
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

  // Clear user scroll state to ensure centering isn't blocked
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
  }
  setProperty(PropertyKeys::UserFreeScroll, false);

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
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(true);
    }
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
  if (m_animationManager && m_animationManager->isHorizontalAnimRunning()) {
    m_animationManager->horizontalAnimation()->stop();
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
    bool animRunning = (m_animationManager && m_animationManager->isVerticalAnimRunning());
    if (m_viewportManager) {
      m_viewportManager->setContinuousScrollActive(animRunning);
    }
  }

  if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
      !suppressRecentering) {
    QTimer::singleShot(
        UIConstants::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
          bool stillActive = m_keyboardManager
                                 ? m_keyboardManager->isContinuousScrollActive()
                                 : (m_viewportManager ? m_viewportManager->continuousScrollActive() : false);
          if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
              !stillActive) {
            centerItemVertically(m_selectedItemIndex, false);
          }
        });
  }
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
  // Delegate debounce check to LaunchManager
  if (m_launchManager && !filePath.isEmpty() && !m_launchManager->canLaunch(filePath)) {
    return;
  }

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

  // Delegate event filtering to EventManager
  if (m_eventManager) {
    bool handled = m_eventManager->filterEvent(obj, event);
    if (handled) {
      return true;
    }
  }

  return QObject::eventFilter(obj, event);
}

// Updates selection state and notifies dependent managers
void InteractionManager::updateSelectionForKeyMove(int newSelection) {
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
  if (m_viewportManager) {
    return m_viewportManager->forceImmediateCenter();
  }
  return false;
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
  if (m_viewportManager) {
    m_viewportManager->centerItemVertically(index, immediate);
  }
}

void InteractionManager::recenterCurrentSelection() {
  // Clear user scroll state to ensure centering isn't blocked
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
  }
  setProperty(PropertyKeys::UserFreeScroll, false);

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
  if (m_selectionManager) {
    m_selectionManager->handleWidgetClicked(widget, filePath);
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
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(m_selectionManager->forceImmediateCenter());
    }
  } else {
    clearSelection();
    m_restoringSelection = true;
    m_targetRestoreIndex = targetIndex;
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(true);
    }

    if (m_itemScrollArea) {
      m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
      qint64 until = QDateTime::currentMSecsSinceEpoch() +
                     UIConstants::ARROW_KEY_ANIMATION_SETTLE_MS +
                     UIConstants::ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS;
      m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                    until);
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

  if (m_selectedItemIndex == targetIndex) {
    finalizeRestoreFlagsAndFocus();
    emit selectionChanged(targetIndex);
  }

  // Finalize restore state
  if (m_selectionManager) {
    m_selectionManager->finalizeRestore();
    m_restoringSelection = m_selectionManager->isRestoringSelection();
    m_targetRestoreIndex = m_selectionManager->targetRestoreIndex();
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(m_selectionManager->forceImmediateCenter());
    }
  } else {
    m_restoringSelection = false;
    m_targetRestoreIndex = -1;
    if (m_viewportManager) {
      m_viewportManager->setForceImmediateCenter(false);
    }
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
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
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
    if (guard->m_viewportManager) {
      guard->m_viewportManager->ensureVerticalScrollbarPolicy();
    }
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
    setProperty(PropertyKeys::KeyContinuous, false);
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
  setProperty(PropertyKeys::HorizHoldActive, false);
  setProperty(PropertyKeys::KeyContinuous, false);
  setProperty(PropertyKeys::ArmFirstClickDelay, false);
  setProperty(PropertyKeys::PendingInitialCenter, false);

  if (m_gridContainer != nullptr) {
    m_gridContainer->setProperty(PropertyKeys::ArrowKeyScrolling, false);
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

  if (m_animationManager && m_animationManager->isHorizontalAnimRunning()) {
    m_animationManager->horizontalAnimation()->stop();
  }

  if (property(PropertyKeys::SelectionSuppressed).toBool()) {
    int pending = property(PropertyKeys::PendingSelectionIndex).toInt();
    if (pending >= 0) {
      selectItemByIndex(pending, true);
    }
    setProperty(PropertyKeys::SelectionSuppressed, false);
    setProperty(PropertyKeys::PendingSelectionIndex, -1);
  }

  if (m_viewportManager && !m_viewportManager->physicalKeyDown()) {
    m_viewportManager->setContinuousScrollActive(
        m_animationManager && m_animationManager->isVerticalAnimRunning());
  }

  if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
      !suppressRecentering) {
    QTimer::singleShot(
        UIConstants::STOP_REPEAT_RECENTER_DELAY_MS, this, [this]() {
          if (!QApplication::closingDown() && m_selectedItemIndex >= 0 &&
              m_viewportManager && !m_viewportManager->continuousScrollActive()) {
            centerItemVertically(m_selectedItemIndex, false);
          }
        });
  }
}

auto InteractionManager::isWheelScrolling() const -> bool {
  return m_mouseManager ? m_mouseManager->isWheelScrolling() : false;
}

// Advances selection during mouse-hold scrolling (called via MouseManager signal)
void InteractionManager::onMouseHoldScrollStep(int direction, bool isHorizontal) {
  if (m_scrollManager == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr) {
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
    int currentIndex = m_selectedItemIndex >= 0 ? m_selectedItemIndex : 0;
    bool wrap = ((m_mainWindow != nullptr)
                     ? m_mainWindow->m_generalSettings.wrapNavigation
                     : false);
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

  // Vertical scrolling
  int currentIndex = m_selectedItemIndex >= 0 ? m_selectedItemIndex : 0;
  int nextIndex = currentIndex + (direction * gridWidth);

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

  bool immediate = (m_viewportManager && m_viewportManager->forceImmediateCenter()) || restoringMatch;
  centerItemVertically(index, immediate);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
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

void InteractionManager::cancelPendingSelectionRestore() {
  if (m_selectionManager) {
    m_selectionManager->cancelPendingSelectionRestore();
  }
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
}
