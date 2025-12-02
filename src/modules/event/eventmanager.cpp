// Filters and dispatches input events to specialized handlers for mouse, keyboard, and wheel.
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
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "mousemanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "sidebarmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcEventManager, "kartend.eventmanager")
#define debugLog(msg) qCDebug(lcEventManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

// EventManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(EventManagerSetup, ScrollManager*, ScrollManager, scrollManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, AnimationManager*, AnimationManager, animationManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, ViewportManager*, ViewportManager, viewportManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, SelectionManager*, SelectionManager, selectionManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, ArtworkManager*, ArtworkManager, artworkManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, DatabaseManager*, DatabaseManager, databaseManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, SidebarManager*, SidebarManager, sidebarManager)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QScrollArea*, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QWidget*, GridContainer, gridContainer)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QStackedWidget*, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QWidget*, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QLineEdit*, SearchBar, searchBar)
SETUP_GETTER_DEF_SAME(EventManagerSetup, QList<CollectionConfig>*, Collections, collections)
SETUP_GETTER_DEF_SAME(EventManagerSetup, int*, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_SAME(EventManagerSetup, GeneralSettings*, GeneralSettings, generalSettings)
SETUP_GETTER_DEF_CTX_ONLY(EventManagerSetup, InteractionStateHolder*, InteractionState, interactionState)

EventManager::EventManager(QObject *parent) : QObject(parent) {}

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
      qobject_cast<QScrollBar *>(obj ? obj->parent() : nullptr) !=
          nullptr) {
    if (m_viewportManager) {
      m_viewportManager->setContinuousScrollActive(true);
    }
    // Clear continuous scroll state after user finishes scrollbar interaction -
    // allows time for the drag/click to complete before re-enabling auto-centering
    QTimer::singleShot(UIConstants::Mouse::CONTINUOUS_SCROLL_IDLE_MS, this,
                       [this]() {
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
  if (mouseReleaseEvent &&
      mouseReleaseEvent->button() == Qt::LeftButton) {
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

bool EventManager::handleWheelEvent(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
  QWidget *activeModal = QApplication::activeModalWidget();
  if (activeModal) {
    return false;
  }

  if (!m_itemScrollArea || !m_stackedWidget ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections) ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    return false;
  }

  auto *wheelEvent = static_cast<QWheelEvent *>(event);
  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  if (!vScrollBar) {
    return false;
  }

  AnimationManager::stopArrowKeyAnimationIfRunning(vScrollBar);

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];

  const int wheelSteps = MouseManager::computeWheelSteps(wheelEvent);
  if (wheelSteps == 0) {
    return false;
  }

  int currentPos = vScrollBar->value();

  if (m_state) {
    m_state->scroll().userScrollActive = true;
    m_state->scroll().programmaticScroll = true;
    m_state->suppressArrowCenterFor(UIConstants::Mouse::WHEEL_SUPPRESS_ARROW_CENTER_MS);
    if (m_scrollManager) {
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
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
    event->accept();
    return true;
  }

  int singleRowPixels = GridLayoutCalculator::getRowHeight(collection);
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

  if (m_state) {
    m_state->scroll().userScrollActive = true;
    m_state->scroll().programmaticScroll = true;
    if (m_scrollManager) {
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
          if (m_state) {
            m_state->scroll().userScrollActive = false;
            m_state->scroll().programmaticScroll = false;
            m_state->clearArrowCenterSuppression();
            if (m_scrollManager) {
              m_scrollManager->refreshSelectionOverlayState();
            }
          }
          int selectedIndex = m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
          if (m_scrollManager && selectedIndex >= 0) {
            m_scrollManager->updateSelectionForIndex(selectedIndex);
          }
          emit wheelScrollEnded();
        });
  }

  // Defer virtual view update to next event loop iteration - allows
  // the scroll position to settle before recalculating visible items
  if (m_scrollManager) {
    QTimer::singleShot(0, this, [this]() {
      if (m_scrollManager) {
        m_scrollManager->updateVirtualView();
      }
    });
  }

  emit wheelScrollStarted();
  event->accept();
  return true;
}

bool EventManager::handleKeyPressEvent(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
  auto *keyEvent = static_cast<QKeyEvent *>(event);

  if (QApplication::activeModalWidget()) {
    return false;
  }

  // Delegate to KeyboardManager for key handling
  if (m_keyboardManager) {
    const bool searchBarFocused =
        (m_searchBar) && m_searchBar->hasFocus();
    const bool handled =
        m_keyboardManager->handleKeyPress(keyEvent, searchBarFocused);
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

bool EventManager::handleMouseDoubleClick(QObject *obj, QEvent *event) {
  auto *mouseEvent = static_cast<QMouseEvent *>(event);
  if (!mouseEvent || mouseEvent->button() != Qt::LeftButton) {
    return false;
  }

  auto *widget = qobject_cast<ItemWidget *>(obj);
  if (!widget) {
    return false;
  }

  // If the double-clicked widget represents a subcollection or virtual folder,
  // allow the widget to handle the event so its signal is emitted.
  if (m_scrollManager && m_currentCollectionIndex &&
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
      // Convert visual index to actual index (accounts for filtering)
      int actualIndex = m_scrollManager->getFilteredIndex(visualIndex);
      const QList<int> subs = getSubcollections(*m_currentCollectionIndex);
      int subCount = subs.size();
      int folderCount = m_scrollManager->getVirtualFolderCount();
      // Pass through if it's a subcollection or virtual folder
      if (actualIndex >= 0 && actualIndex < subCount + folderCount) {
        return false;
      }
    }
  }

  QString path = widget->getFilePath();
  if (path.isEmpty()) {
    event->accept();
    return true;
  }

  int collIdx = -1;
  if (m_databaseManager) {
    collIdx = m_databaseManager->getCollectionIndexForFile(path);
  } else if (m_currentCollectionIndex) {
    collIdx = *m_currentCollectionIndex;
  }
  emit widgetDoubleClicked(path, collIdx);
  event->accept();
  return true;
}

bool EventManager::handleMousePress(QObject *obj, QEvent *event) {
  if (isRestoringSelection()) {
    event->accept();
    return true;
  }
  auto *mouseEvent = static_cast<QMouseEvent *>(event);
  if ((!mouseEvent) || mouseEvent->button() != Qt::LeftButton) {
    return false;
  }

  if (!m_itemScrollArea || (!m_gridContainer) ||
      (!m_stackedWidget) || (!m_itemsPage)) {
    return false;
  }
  if (m_stackedWidget->currentWidget() != m_itemsPage) {
    return false;
  }

  if (m_mouseManager) {
    m_mouseManager->setLeftMouseDown(true);
    m_mouseManager->clearHorizontalCandidate();
  }

  bool target =
      (obj == m_itemScrollArea || obj == m_itemScrollArea->viewport() ||
       obj == m_gridContainer || obj == m_itemsPage ||
       qobject_cast<ItemWidget *>(obj));
  if (!target) {
    return false;
  }

  QPoint clickPos = mouseEvent->pos();
  if (obj != m_gridContainer) {
    if (auto *w = qobject_cast<QWidget *>(obj)) {
      clickPos = m_gridContainer->mapFromGlobal(w->mapToGlobal(clickPos));
    }
  }

  if (!m_scrollManager) {
    emit clearSelectionRequested();
    event->accept();
    return true;
  }

  ItemWidget *chosen = MouseManager::findBestWidgetForClick(
      clickPos, m_scrollManager, m_gridContainer);
  if (chosen) {
    emit widgetClicked(chosen, clickPos, mouseEvent);
    event->accept();
    return true;
  }
  emit clearSelectionRequested();
  event->accept();
  return true;
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

bool EventManager::applyWheelSelectionDelta(int wheelSteps) {
  if (wheelSteps == 0 || !m_scrollManager ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }

  const CollectionConfig &collection =
      (*m_collections)[*m_currentCollectionIndex];
  int gridWidth = collection.gridWidth;
  if (gridWidth <= 0) {
    return false;
  }

  int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    return false;
  }

  int currentSelection = m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
  if (currentSelection < 0) {
    currentSelection = 0;
  }

  int rowDelta = -wheelSteps;
  int newSelection = currentSelection + (rowDelta * gridWidth);

  bool wrap = m_generalSettings
                  ? m_generalSettings->wrapNavigation
                  : false;
  bool wrapTriggered = false;

  if (wrap) {
    if (newSelection < 0) {
      newSelection = (totalItems + (newSelection % totalItems)) % totalItems;
      wrapTriggered = true;
    } else if (newSelection >= totalItems) {
      newSelection = newSelection % totalItems;
      wrapTriggered = true;
    }
  } else {
    newSelection = qBound(0, newSelection, totalItems - 1);
  }

  if (newSelection == currentSelection) {
    return wrapTriggered;
  }

  if (m_viewportManager) {
    if (wrapTriggered) {
      m_viewportManager->setForceImmediateCenter(true);
      m_viewportManager->setWrapSequenceActive(true);
      m_viewportManager->setContinuousScrollActive(false);
    } else {
      m_viewportManager->setContinuousScrollActive(true);
    }
  }

  if (m_selectionManager) {
    m_selectionManager->setSelectedIndex(newSelection);
    QList<int> subs = getSubcollections(*m_currentCollectionIndex);
    m_selectionManager->updateFilePathForSelection(newSelection, subs);
  }
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(newSelection);
  }

  if (wrapTriggered && m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(newSelection);
    if (m_scrollManager) {
      m_scrollManager->updateVirtualView();
    }
  }

  return wrapTriggered;
}
