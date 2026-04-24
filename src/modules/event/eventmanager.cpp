// Filters and dispatches input events to specialized handlers for mouse,
// keyboard, and wheel.
#include "eventmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include "animationmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "mousemanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "sidebarmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcEventManager, "kartend.eventmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcEventManager().isDebugEnabled()) {                                                       \
      qCDebug(lcEventManager) << msg;                                                              \
    }                                                                                              \
  } while (0)

// EventManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(EventManagerSetup, ScrollManager *, ScrollManager, scrollManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, AnimationManager *, AnimationManager, animationManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, ViewportManager *, ViewportManager, viewportManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, SelectionManager *, SelectionManager, selectionManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, ArtworkManager *, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, DatabaseManager *, DatabaseManager, databaseManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, SidebarManager *, SidebarManager, sidebarManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QStackedWidget *, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QWidget *, ItemsTopBar, itemsTopBar)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QLineEdit *, SearchBar, searchBar)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_SAME(EventManagerSetup, int *, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_SAME(EventManagerSetup, GeneralSettings *, GeneralSettings, generalSettings)
SETUP_GETTER_DEF_CTX_ONLY(EventManagerSetup, InteractionStateHolder *, InteractionState,
                          interactionState)

EventManager::EventManager(QObject *parent) : QObject(parent) {
  m_hoverSelectTimer.setSingleShot(true);
  connect(&m_hoverSelectTimer, &QTimer::timeout, this,
          &EventManager::commitPendingHoverSelection);
}

EventManager::~EventManager() = default;

void EventManager::setupReferences(const EventManagerSetup &setup) {
  m_scrollManager = setup.getScrollManager();
  m_keyboardManager = setup.getKeyboardManager();
  m_mouseManager = setup.getMouseManager();
  m_animationManager = setup.getAnimationManager();
  m_viewportManager = setup.getViewportManager();
  m_selectionManager = setup.getSelectionManager();
  m_artworkManager = setup.getArtworkManager();
  m_databaseManager = setup.getDatabaseManager();
  m_sidebarManager = setup.getSidebarManager();
  m_state = setup.getInteractionState();
  m_generalSettings = setup.getGeneralSettings();
  m_itemScrollArea = setup.getItemScrollArea();
  m_gridContainer = setup.getGridContainer();
  m_stackedWidget = setup.getStackedWidget();
  m_itemsPage = setup.getItemsPage();
  m_itemsTopBar = setup.getItemsTopBar();
  m_searchBar = setup.getSearchBar();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
}

void EventManager::installEventFilters() {
  if (qApp) {
    qApp->installEventFilter(parent());
  }
  if (m_itemsPage) {
    m_itemsPage->installEventFilter(parent());
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->installEventFilter(parent());
    QWidget *viewport = m_itemScrollArea->viewport();
    if (viewport) {
      viewport->installEventFilter(parent());
    }
  }
  if (m_gridContainer) {
    m_gridContainer->installEventFilter(parent());
  }
}

bool EventManager::filterEvent(QObject *obj, QEvent *event) {
  if (QApplication::closingDown() || !event) {
    return false;
  }

  (void)handleActivityEvent(event);

  switch (event->type()) {
  case QEvent::Enter:
  case QEvent::MouseMove:
    return handleHoverSelection(obj, event);
  case QEvent::MouseButtonPress:
    return handleMouseButtonPress(obj, event);
  case QEvent::MouseButtonRelease:
    return handleMouseButtonRelease(obj, event);
  case QEvent::Wheel:
    return handleWheelEvent(obj, event);
  case QEvent::KeyPress:
    return handleKeyPressEvent(obj, event);
  case QEvent::KeyRelease:
    return handleKeyReleaseEvent(obj, event);
  case QEvent::MouseButtonDblClick:
    return handleMouseDoubleClick(obj, event);
  default:
    break;
  }
  return false;
}

bool EventManager::isRestoringSelection() const {
  // Query SelectionManager as the single source of truth
  if (m_selectionManager) {
    return m_selectionManager->isRestoringSelection();
  }
  return false;
}

bool EventManager::handleActivityEvent(QEvent *event) {
  bool activityEvent = false;
  switch (event->type()) {
  case QEvent::MouseMove:
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonRelease:
  case QEvent::KeyPress:
  case QEvent::KeyRelease:
  case QEvent::Wheel:
    activityEvent = true;
    if (m_artworkManager) {
      m_artworkManager->updateUserActivity();
    }
    break;
  default:
    break;
  }

  if (activityEvent) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 last = m_state ? m_state->lastUiActivityMs() : 0;
    if (last > 0 && (now - last) >= UIConstants::Timing::USER_IDLE_THRESHOLD_MS) {
      if (m_state) {
        m_state->click().armFirstClickDelay = true;
      }
    }
    if (m_state) {
      m_state->setLastUiActivityMs(now);
    }
    emit activityDetected();
  }

  return activityEvent;
}

bool EventManager::handleMouseButtonPress(QObject *obj, QEvent *event) {
  if ((obj && qobject_cast<QScrollBar *>(obj)) ||
      qobject_cast<QScrollBar *>(obj ? obj->parent() : nullptr)) {
    if (m_viewportManager) {
      m_viewportManager->setContinuousScrollActive(true);
    }
    // Clear continuous scroll state after user finishes scrollbar interaction -
    // allows time for the drag/click to complete before re-enabling
    // auto-centering
    QTimer::singleShot(UIConstants::Mouse::CONTINUOUS_SCROLL_IDLE_MS, this, [this]() {
      if (m_viewportManager) {
        m_viewportManager->setContinuousScrollActive(false);
      }
    });
    emit requestStopRepeat(true);
    emit scrollbarClicked();
    return false;
  }

  return handleMousePress(obj, event);
}

bool EventManager::handleMouseButtonRelease(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
  auto *mouseReleaseEvent = static_cast<QMouseEvent *>(event);
  if (mouseReleaseEvent && mouseReleaseEvent->button() == Qt::LeftButton) {
    if (m_mouseManager) {
      m_mouseManager->setLeftMouseDown(false);
      m_mouseManager->stopClickHoldTimer();
      if (m_mouseManager->isMouseHoldScrolling()) {
        m_mouseManager->stopMouseHoldScrolling();
      }
    }
    if (m_state) {
      m_state->click().clickHoldRowChange = false;
      m_state->click().deferCenterOnClick = false;
      m_state->click().deferredCenterIndex = -1;
      m_state->scroll().clickScroll = false;
    }
  }
  return false;
}

bool EventManager::handleKeyPressEvent(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
  auto *keyEvent = static_cast<QKeyEvent *>(event);

  if (QApplication::activeModalWidget()) {
    return false;
  }

  // Delegate to KeyboardManager for key handling
  if (m_keyboardManager) {
    const bool searchBarFocused = (m_searchBar) && m_searchBar->hasFocus();
    const bool handled = m_keyboardManager->handleKeyPress(keyEvent, searchBarFocused);
    if (handled) {
      event->accept();
      return true;
    }
  }

  // If search bar is focused and KeyboardManager didn't handle, let it through
  if ((m_searchBar) && m_searchBar->hasFocus()) {
    return false;
  }

  return false;
}

bool EventManager::handleKeyReleaseEvent(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
  auto *keyEvent = static_cast<QKeyEvent *>(event);
  if (!keyEvent) {
    return false;
  }

  // Delegate to KeyboardManager for key release handling
  if (m_keyboardManager) {
    const bool handled = m_keyboardManager->handleKeyRelease(keyEvent);
    if (handled) {
      event->accept();
      return true;
    }
  }

  return false;
}

int EventManager::getCurrentGridWidth() const {
  // Prefer ScrollManager's value for filtered/nested views
  if (m_scrollManager) {
    int width = m_scrollManager->getCurrentGridWidth();
    if (width > 0) {
      return width;
    }
  }
  return CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
}

QList<int> EventManager::getSubcollections(int parentIndex) const {
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
