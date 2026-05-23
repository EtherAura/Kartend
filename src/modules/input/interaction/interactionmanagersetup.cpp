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
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "idatabasemanager.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "mousemanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "collectionutils.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "idetailspane.h"
#include "idetailspanemanager.h"
#include "inavigationmanager.h"
#include "isessionmanager.h"
#include "isettingsmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
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
  m_ctx = setup.ctx;

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

  // Kartend-n8kh: capture the dialog runners. Either may be null in
  // headless contexts; the call sites guard before invoking.
  m_runSmartPlaylistDialog = setup.runSmartPlaylistDialog;
  m_runEditMetadataDialog = setup.runEditMetadataDialog;

  // Setup SearchManager with its dependencies
  if (m_searchManager) {
    SearchManagerSetup searchSetup;
    searchSetup.ctx = setup.ctx;
    searchSetup.collectionPage = setup.collectionPage;
    m_searchManager->setupReferences(searchSetup);
    connectSearchManagerSignals();
  }

  // Setup SelectionManager with its dependencies. Sibling managers come from ctx.
  if (m_selectionManager) {
    SelectionManagerSetup selectionSetup;
    selectionSetup.ctx = setup.ctx;
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
    // Kartend-davi: gamepadSetup.keyboardManager was removed — read through
    // ctx->keyboardManager() at the call site.
    gamepadSetup.generalSettings = m_generalSettings;
    gamepadSetup.isShuttingDown = m_isShuttingDown;
    m_gamepadManager->setupReferences(gamepadSetup);
    connectGamepadManagerSignals();
  }

  // Setup navigation handlers with state callbacks and signal connections
  setupArrowNavigationHandler(setup);
  setupAlphabeticNavigationHandler(setup);

  // Setup AnimationManager with its dependencies. Sibling managers come from ctx.
  if (m_animationManager) {
    AnimationManagerSetup animSetup;
    animSetup.ctx = setup.ctx;
    animSetup.generalSettings = m_generalSettings;
    animSetup.itemScrollArea = m_itemScrollArea;
    m_animationManager->setupReferences(animSetup);
    connectAnimationManagerSignals();
  }

  // Setup MouseManager with its dependencies. Sibling managers come from ctx.
  if (m_mouseManager) {
    MouseManagerSetup mouseSetup;
    mouseSetup.ctx = setup.ctx;
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

  // Setup ViewportManager with its dependencies. Sibling managers come from ctx.
  if (m_viewportManager) {
    ViewportManagerSetup viewportSetup;
    viewportSetup.ctx = setup.ctx;
    viewportSetup.generalSettings = m_generalSettings;
    m_viewportManager->setupReferences(viewportSetup);
    connectViewportManagerSignals();
  }

  // Setup EventManager with its dependencies. Sibling managers come from ctx.
  if (m_eventManager) {
    EventManagerSetup eventSetup;
    eventSetup.ctx = setup.ctx;
    m_eventManager->setupReferences(eventSetup);
    connectEventManagerSignals();
  }

  // Setup AttractManager with its dependencies. Sibling managers come from ctx;
  // only non-manager fields stay on the setup struct.
  if (m_attractManager) {
    AttractManagerSetup attractSetup;
    attractSetup.ctx = setup.ctx;
    attractSetup.itemScrollArea = m_itemScrollArea;
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

void InteractionManager::setupAlphabeticNavigationHandler(const InteractionManagerSetup &setup) {
  if (!m_alphabeticHandler) {
    return;
  }

  m_alphabeticHandler->setContext(setup.ctx);

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
            if (scrollMgr()) {
              scrollMgr()->updateVirtualView();
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
