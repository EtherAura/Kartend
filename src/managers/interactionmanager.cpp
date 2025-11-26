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
              if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
                m_animationManager->verticalAnimation()->stop();
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
              m_allowArtworkDuringSelection = true;
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
                m_allowArtworkDuringSelection = true;
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
    if (m_viewportManager) {
      m_viewportManager->setContinuousScrollActive(true);
    }
    QTimer::singleShot(UIConstants::CONTINUOUS_SCROLL_IDLE_MS, this,
                       [this]() {
                         if (m_viewportManager) {
                           m_viewportManager->setContinuousScrollActive(false);
                         }
                       });
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
    if (m_mouseManager) {
      m_mouseManager->setLeftMouseDown(false);
      m_mouseManager->stopClickHoldTimer();
      if (m_mouseManager->isMouseHoldScrolling()) {
        m_mouseManager->stopMouseHoldScrolling();
      }
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

  AnimationManager::stopArrowKeyAnimationIfRunning(vScrollBar);

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];

  const int wheelSteps = MouseManager::computeWheelSteps(wheelEvent);
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
    if (m_mouseManager) {
      m_mouseManager->setWheelScrolling(false);
    }
    if (m_viewportManager) {
      m_viewportManager->setContinuousScrollActive(false);
    }
    if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
      m_animationManager->verticalAnimation()->stop();
    }
    if (m_scrollManager != nullptr) {
      m_scrollManager->updateVirtualView();
    }
    event->accept();
    return true;
  }

  int singleRowPixels = collection.itemHeight + collection.verticalSpacing;
  int basePos = currentPos;
  if (m_animationManager && m_animationManager->isVerticalAnimRunning()) {
    basePos = m_animationManager->getVerticalAnimEndValue();
  }
  int targetPos = basePos - (wheelSteps * singleRowPixels);
  targetPos = qBound(0, targetPos, vScrollBar->maximum());

  if (m_mouseManager) {
    m_mouseManager->setWheelScrolling(true);
  }
  if (m_viewportManager) {
    m_viewportManager->setContinuousScrollActive(true);
  }

  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, true);
    m_itemScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
    if (m_scrollManager != nullptr) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }

  if (m_animationManager) {
    m_animationManager->startWheelScrollAnimation(
        vScrollBar, currentPos, targetPos, [this]() {
          if (m_mouseManager) {
            m_mouseManager->setWheelScrolling(false);
          }
          if (m_viewportManager) {
            m_viewportManager->setContinuousScrollActive(false);
          }
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
  }

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
      if (m_viewportManager) {
        m_viewportManager->setWrapSequenceActive(true);
        m_viewportManager->setForceImmediateCenter(true);
        m_viewportManager->setContinuousScrollActive(false);
      }
    }
    const bool isNewRow =
        SelectionManager::isNewRow(currentSelection, newSelection, gridWidth);
    updateSelectionForKeyMove(newSelection);
    performVisibilityForKeyMove(isNewRow, newSelection);

    currentSelection = newSelection;
    remainingSteps += (remainingSteps > 0) ? -1 : 1;
  }

  if (wrapTriggered) {
    if (m_viewportManager) {
      m_viewportManager->setWrapSequenceActive(true);
      m_viewportManager->setForceImmediateCenter(true);
    }
  }

  return wrapTriggered;
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

  if (m_mouseManager) {
    m_mouseManager->setLeftMouseDown(true);
    m_mouseManager->clearHorizontalCandidate();
  }

  const int previousSelection = m_selectedItemIndex;

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

  MediaItemWidget *chosen = MouseManager::findBestWidgetForClick(
      clickPos, m_scrollManager, m_gridContainer);
  if (chosen != nullptr) {
    const int clickedIndex =
        handleWidgetSelection(chosen, clickPos, mouseEvent);

    // Ensure we have a valid index before setting up hold candidate
    if (clickedIndex >= 0 && m_mouseManager) {
      const int gridWidth = getCurrentGridWidth();
      const int totalItems = m_scrollManager->getTotalItems();
      m_mouseManager->updateClickHoldHorizontalCandidate(previousSelection,
                                                         clickedIndex, gridWidth);
      m_mouseManager->startClickHoldTimer(clickPos, m_selectedItemIndex,
                                          gridWidth, totalItems);
    }

    event->accept();
    return true;
  }
  clearSelectionAndFocus();
  event->accept();
  return true;
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

int InteractionManager::computeVerticalCenterDuration(int distance,
                                                       bool repeatActive) const {
  int itemHeight = 0;
  int vSpacing = 0;
  if ((m_collections != nullptr) && (m_currentCollectionIndex != nullptr) &&
      *m_currentCollectionIndex >= 0 &&
      *m_currentCollectionIndex < m_collections->size()) {
    itemHeight = (*m_collections)[*m_currentCollectionIndex].itemHeight;
    vSpacing = (*m_collections)[*m_currentCollectionIndex].verticalSpacing;
  }
  return AnimationManager::computeVerticalCenterDuration(distance, itemHeight,
                                                         vSpacing, repeatActive);
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
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  stopRepeat();

  const int pendingIndex =
      property(PropertyKeys::RowChangeFirstClickIndex).toInt();
  const qint64 pendingMs =
      property(PropertyKeys::RowChangeFirstClickMs).toLongLong();
  const bool pendingValid =
      (pendingIndex >= 0 && (nowMs - pendingMs) <= dcInterval);

  const int fromIndex = m_selectedItemIndex;
  const bool canAnimateHoriz =
      SelectionManager::shouldAnimateHorizontalHop(fromIndex, visualIndex, gridWidth);

  if (canAnimateHoriz) {
    runHorizontalHopAnimation(fromIndex, visualIndex, nowMs);
    return;
  }

  const bool treatAsNewRow = m_selectionManager
      ? m_selectionManager->shouldTreatAsNewRow(visualIndex, gridWidth)
      : false;
  if (treatAsNewRow) {
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

  bool immediate = (m_viewportManager && m_viewportManager->forceImmediateCenter()) || restoringMatch;
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

void InteractionManager::cancelPendingSelectionRestore() {
  if (m_selectionManager) {
    m_selectionManager->cancelPendingSelectionRestore();
  }
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
}
