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
#include "detailspanemanager.h"
#include "eventhelpers.h"
#include "gridlayoutcalculator.h"
#include "gridutils.h"
#include "hoverscrollhandler.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "mousemanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"
#include "wheeleventhandler.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcEventManager, "kartend.eventmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcEventManager().isDebugEnabled()) {                                                       \
      qCDebug(lcEventManager) << msg;                                                              \
    }                                                                                              \
  } while (0)

// EventManagerSetup getter definitions (non-manager fields only).
SETUP_GETTER_DEF_UI_SAME(EventManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_UI_SAME(EventManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_UI_SAME(EventManagerSetup, QStackedWidget *, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_UI_SAME(EventManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_UI_SAME(EventManagerSetup, QWidget *, ItemsTopBar, itemsTopBar)
SETUP_GETTER_DEF_UI_SAME(EventManagerSetup, QLineEdit *, SearchBar, searchBar)
SETUP_GETTER_DEF_COL_SAME(EventManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_COL_SAME(EventManagerSetup, int *, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_COL_SAME(EventManagerSetup, GeneralSettings *, GeneralSettings, generalSettings)

EventManager::EventManager(QObject *parent)
    : QObject(parent), m_hoverScroll(std::make_unique<HoverScrollHandler>(this)),
      m_wheelHandler(std::make_unique<WheelEventHandler>(this)) {
  // Forward wheel-handler start/end signals so external listeners
  // (toolbar, attract mode) keep their existing subscription to the
  // EventManager surface.
  connect(m_wheelHandler.get(), &WheelEventHandler::scrollStarted, this,
          &EventManager::wheelScrollStarted);
  connect(m_wheelHandler.get(), &WheelEventHandler::scrollEnded, this,
          &EventManager::wheelScrollEnded);
}

EventManager::~EventManager() = default;

void EventManager::setupReferences(const EventManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_generalSettings = setup.getGeneralSettings();
  m_itemScrollArea = setup.getItemScrollArea();
  m_gridContainer = setup.getGridContainer();
  m_stackedWidget = setup.getStackedWidget();
  m_itemsPage = setup.getItemsPage();
  m_itemsTopBar = setup.getItemsTopBar();
  m_searchBar = setup.getSearchBar();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();

  HoverScrollHandler::Setup hoverSetup;
  hoverSetup.ctx = setup.ctx;
  hoverSetup.itemScrollArea = m_itemScrollArea;
  hoverSetup.gridContainer = m_gridContainer;
  hoverSetup.stackedWidget = m_stackedWidget;
  hoverSetup.itemsPage = m_itemsPage;
  hoverSetup.collections = m_collections;
  hoverSetup.currentCollectionIndex = m_currentCollectionIndex;
  hoverSetup.generalSettings = m_generalSettings;
  m_hoverScroll->setupReferences(hoverSetup);

  WheelEventHandler::Setup wheelSetup;
  wheelSetup.ctx = setup.ctx;
  wheelSetup.itemScrollArea = m_itemScrollArea;
  wheelSetup.stackedWidget = m_stackedWidget;
  wheelSetup.itemsPage = m_itemsPage;
  wheelSetup.collections = m_collections;
  wheelSetup.currentCollectionIndex = m_currentCollectionIndex;
  wheelSetup.generalSettings = m_generalSettings;
  m_wheelHandler->setupReferences(wheelSetup);
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
  if (selectionMgr()) {
    return selectionMgr()->isRestoringSelection();
  }
  return false;
}

bool EventManager::handleActivityEvent(QEvent *event) {
  if (!EventHelpers::isActivityEvent(event->type())) {
    return false;
  }

  if (artworkMgr()) {
    artworkMgr()->updateUserActivity();
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 last = state() ? state()->lastUiActivityMs() : 0;
  if (state() && EventHelpers::shouldArmFirstClickDelay(
                     last, now, UIConstants::Timing::USER_IDLE_THRESHOLD_MS)) {
    state()->click().armFirstClickDelay = true;
  }
  if (state()) {
    state()->setLastUiActivityMs(now);
  }
  emit activityDetected();
  return true;
}

bool EventManager::handleMouseButtonPress(QObject *obj, QEvent *event) {
  if ((obj && qobject_cast<QScrollBar *>(obj)) ||
      qobject_cast<QScrollBar *>(obj ? obj->parent() : nullptr)) {
    if (viewportMgr()) {
      viewportMgr()->setContinuousScrollActive(true);
    }
    // Clear continuous scroll state after user finishes scrollbar interaction -
    // allows time for the drag/click to complete before re-enabling
    // auto-centering
    QTimer::singleShot(UIConstants::Mouse::CONTINUOUS_SCROLL_IDLE_MS, this, [this]() {
      if (viewportMgr()) {
        viewportMgr()->setContinuousScrollActive(false);
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
    if (mouseMgr()) {
      mouseMgr()->setLeftMouseDown(false);
      mouseMgr()->stopClickHoldTimer();
      if (mouseMgr()->isMouseHoldScrolling()) {
        mouseMgr()->stopMouseHoldScrolling();
      }
    }
    if (state()) {
      state()->click().clickHoldRowChange = false;
      state()->click().deferCenterOnClick = false;
      state()->click().deferredCenterIndex = -1;
      state()->scroll().clickScroll = false;
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
  if (keyboardMgr()) {
    const bool searchBarFocused = (m_searchBar) && m_searchBar->hasFocus();
    const bool handled = keyboardMgr()->handleKeyPress(keyEvent, searchBarFocused);
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
  if (keyboardMgr()) {
    const bool handled = keyboardMgr()->handleKeyRelease(keyEvent);
    if (handled) {
      event->accept();
      return true;
    }
  }

  return false;
}

int EventManager::getCurrentGridWidth() const {
  // Prefer ScrollManager's value for filtered/nested views
  if (scrollMgr()) {
    int width = scrollMgr()->getCurrentGridWidth();
    if (width > 0) {
      return width;
    }
  }
  return CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
}

QList<int> EventManager::getSubcollections(int parentIndex) const {
  // Delegate to SelectionManager which owns the canonical implementation
  if (selectionMgr()) {
    return selectionMgr()->getSubcollections(parentIndex);
  }
  // Fallback to O(n) scan
  if (!m_collections) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}
