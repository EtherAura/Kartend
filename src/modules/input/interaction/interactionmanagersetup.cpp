// Sibling TU: setup + event filter installation for InteractionManager.
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

#include "alphabeticnavigationhandler.h"
#include "animationmanager.h"
#include "applicationcontext.h"
#include "arrownavigationhandler.h"
#include "attractmanager.h"
#include "databasemanager.h"
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
#include "detailspane.h"
#include "detailspanemanager.h"
#include "gridutils.h"
#include "itemwidget.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcInteractionManager().isDebugEnabled()) {                                                 \
      qCDebug(lcInteractionManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

void InteractionManager::setupReferences(const InteractionManagerSetup &setup) {
  // Manager dependencies - use accessors that check context fallback
  m_scrollManager = setup.getScrollManager();
  m_detailsPaneManager = setup.getDetailsPaneManager();
  m_detailPageManager = setup.getDetailPageManager();
  m_settingsManager = setup.getSettingsManager();
  m_databaseManager = setup.getDatabaseManager();
  m_navigationManager = setup.getNavigationManager();
  // the only consumer is the context menu, so we just read the
  // pointer directly from the shared ApplicationContext rather than adding a
  // dedicated setup field.
  m_playlistManager = (setup.ctx ? setup.ctx->managers.playlistManager : nullptr);
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

  // Setup GamepadManager after KeyboardManager so it can reuse the existing
  // repeat/navigation pipeline.
  if (m_gamepadManager) {
    GamepadManagerSetup gamepadSetup;
    gamepadSetup.ctx = setup.ctx;
    gamepadSetup.keyboardManager = m_keyboardManager.get();
    gamepadSetup.generalSettings = m_generalSettings;
    gamepadSetup.isShuttingDown = m_isShuttingDown;
    m_gamepadManager->setupReferences(gamepadSetup);
    connectGamepadManagerSignals();
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
    // forward launch + session events into DatabaseManager via
    // callbacks so LaunchManager itself doesn't take a hard link-time
    // dependency on the database module (keeps the launch unit tests slim).
    if (setup.ctx) {
      const ApplicationContext *appCtx = setup.ctx;
      launchSetup.onLaunched = [appCtx](const QString &uuid, const QString &filePath) {
        if (!appCtx->managers.databaseManager) {
          return;
        }
        appCtx->managers.databaseManager->recordItemLaunch(uuid, filePath);
        // chronological history runs on the same hook so a single
        // launch updates aggregate stats and the history log atomically.
        // historyEnabled gates the insert; historyMaxEntries (>0) drives the
        // post-insert trim so the table never grows unbounded.
        const GeneralSettings *gs = appCtx->collection.generalSettings;
        if (gs && gs->historyEnabled) {
          // Pull the user-visible name from item_metadata.title when present
          // so the dialog matches what the sidebar shows; HistoryStore falls
          // back to the path basename when the title is empty.
          const auto metadata = appCtx->managers.databaseManager->loadItemMetadata(uuid, filePath);
          appCtx->managers.databaseManager->recordHistoryEntry(uuid, filePath, metadata.title,
                                                               gs->historyMaxEntries);
        }
      };
      launchSetup.onPlaySessionEnded = [appCtx](const QString &uuid, const QString &filePath,
                                                qint64 seconds) {
        if (appCtx->managers.databaseManager) {
          appCtx->managers.databaseManager->recordItemPlaySession(uuid, filePath, seconds);
        }
      };
      // per-item launcher override pulled from item_metadata.
      // Callback indirection keeps test_launchmanager free of the database
      // module at link time.
      launchSetup.resolveLauncherOverride = [appCtx](const QString &uuid,
                                                     const QString &filePath) -> int {
        if (!appCtx->managers.databaseManager) {
          return -1;
        }
        const auto metadata = appCtx->managers.databaseManager->loadItemMetadata(uuid, filePath);
        return metadata.launcherIndex;
      };
    }
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

  // Setup AttractManager with its dependencies
  if (m_attractManager) {
    AttractManagerSetup attractSetup;
    attractSetup.ctx = setup.ctx;
    attractSetup.itemScrollArea = m_itemScrollArea;
    attractSetup.scrollManager = m_scrollManager;
    attractSetup.selectionManager = m_selectionManager.get();
    attractSetup.generalSettings = m_generalSettings;
    attractSetup.isShuttingDown = m_isShuttingDown;
    m_attractManager->setupReferences(attractSetup);
    connectAttractManagerSignals();
  }

  updateSearchModeButton();
  updateSearchBarPlaceholder();

  if (m_searchBar) {
    connect(m_searchBar, &QLineEdit::textChanged, this,
            &InteractionManager::handleImmediateSearchTextChanged);
  }

  installEventFilters();
}

void InteractionManager::setupArrowNavigationHandler(const InteractionManagerSetup &setup) {
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
  m_arrowHandler->setGetCurrentSelection([this]() { return currentSelectedIndex(); });
  m_arrowHandler->setGetCurrentGridWidth([this]() { return getCurrentGridWidth(); });
  m_arrowHandler->setIsItemOffscreen(
      [this](int selection, int gridWidth) { return isItemOffscreen(selection, gridWidth); });

  // Connect handler signals
  connect(m_arrowHandler.get(), &ArrowNavigationHandler::requestFullSelectionUpdate, this,
          [this](int index) {
            const QList<int> subs = getSubcollections(*m_currentCollectionIndex);
            updateFilePathForSelection(index, subs);
            selectItemByIndex(index, true);
          });
  connect(m_arrowHandler.get(), &ArrowNavigationHandler::requestRecenter, this,
          &InteractionManager::recenterCurrentSelection);
  connect(m_arrowHandler.get(), &ArrowNavigationHandler::requestMinorHorizontalSuppress, this,
          &InteractionManager::applyMinorHorizontalSuppress);
  connect(m_arrowHandler.get(), &ArrowNavigationHandler::requestFocusItemsPage, this, [this]() {
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

  // Connect handler signals - scroll first, then select, since the target
  // widget may not exist until the viewport is scrolled to show it
  connect(m_alphabeticHandler.get(), &AlphabeticNavigationHandler::requestSelection, this,
          [this](int index) {
            // User initiated navigation - cancel any pending automatic restore
            // to prevent it from overriding this explicit user choice
            cancelPendingSelectionRestore();
            // Scroll to target position first so the widget gets materialized
            if (m_viewportManager) {
              m_viewportManager->centerItemVertically(index, true);
            }
            // Update virtual view to materialize widgets at new scroll position
            if (m_scrollManager) {
              m_scrollManager->updateVirtualView();
            }
            // Now select the item (widget should exist after scroll + update)
            selectItemByIndex(index, true);
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

// KeyboardManager callback: handles arrow key navigation
// Helper: pick a currently selected visual index if any
