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
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "mainwindow.h"
#include "mousemanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

EventManager::EventManager(QObject *parent) : QObject(parent) {}

EventManager::~EventManager() = default;

void EventManager::installEventFilters() {
  if (qApp != nullptr) {
    qApp->installEventFilter(parent());
  }
  if (m_itemsPage != nullptr) {
    m_itemsPage->installEventFilter(parent());
  }
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->installEventFilter(parent());
    QWidget *viewport = m_itemScrollArea->viewport();
    if (viewport != nullptr) {
      viewport->installEventFilter(parent());
    }
  }
  if (m_gridContainer != nullptr) {
    m_gridContainer->installEventFilter(parent());
  }
}

bool EventManager::filterEvent(QObject *obj, QEvent *event) {
  if (QApplication::closingDown() || event == nullptr) {
    return false;
  }

  handleActivityEvent(event);

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
  if (m_selectionManager) {
    return m_selectionManager->isRestoringSelection();
  }
  return m_restoringSelection;
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
    if (m_artworkManager != nullptr) {
      m_artworkManager->updateUserActivity();
    }
    break;
  default:
    break;
  }

  if (activityEvent) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 last = parent()->property(PropertyKeys::LastUiActivityMs).toLongLong();
    if (last > 0 && (now - last) >= UIConstants::USER_IDLE_THRESHOLD_MS) {
      parent()->setProperty(PropertyKeys::ArmFirstClickDelay, true);
    }
    parent()->setProperty(PropertyKeys::LastUiActivityMs, now);
    emit activityDetected();
  }

  return activityEvent;
}

bool EventManager::handleMouseButtonPress(QObject *obj, QEvent *event) {
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
    emit requestStopRepeat(true);
    emit scrollbarClicked();
    return false;
  }

  return handleMousePress(obj, event);
}

bool EventManager::handleMouseButtonRelease(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
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
    if (parent()) {
      parent()->setProperty(PropertyKeys::ClickHoldRowChange, false);
      parent()->setProperty(PropertyKeys::DeferCenterOnClick, false);
      parent()->setProperty(PropertyKeys::DeferredCenterIndex, -1);
      parent()->setProperty(PropertyKeys::ClickScroll, false);
    }
  }
  return false;
}

bool EventManager::handleWheelEvent(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
  QWidget *activeModal = QApplication::activeModalWidget();
  if (activeModal != nullptr) {
    return false;
  }

  if (m_itemScrollArea == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr || *m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size() ||
      m_stackedWidget == nullptr ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    return false;
  }

  auto *wheelEvent = static_cast<QWheelEvent *>(event);
  QScrollBar *vScrollBar = m_itemScrollArea->verticalScrollBar();
  if (vScrollBar == nullptr) {
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
          int selectedIndex = m_selectionManager ? m_selectionManager->currentSelectedIndex() : -1;
          if (m_scrollManager != nullptr && selectedIndex >= 0) {
            m_scrollManager->updateSelectionForIndex(selectedIndex);
          }
          emit wheelScrollEnded();
        });
  }

  if (m_scrollManager != nullptr) {
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

  if (QApplication::activeModalWidget() != nullptr) {
    return false;
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
    return false;
  }

  return false;
}

bool EventManager::handleKeyReleaseEvent(QObject *obj, QEvent *event) {
  Q_UNUSED(obj);
  auto *keyEvent = static_cast<QKeyEvent *>(event);
  if (keyEvent == nullptr) {
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
  if (mouseEvent == nullptr || mouseEvent->button() != Qt::LeftButton) {
    return false;
  }

  auto *widget = qobject_cast<MediaItemWidget *>(obj);
  if (widget == nullptr) {
    return false;
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
  if (m_databaseManager != nullptr) {
    collIdx = m_databaseManager->getCollectionIndexForFile(path);
  } else if (m_currentCollectionIndex != nullptr) {
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
  if ((mouseEvent == nullptr) || mouseEvent->button() != Qt::LeftButton) {
    return false;
  }

  if (!m_itemScrollArea || (m_gridContainer == nullptr) ||
      (m_stackedWidget == nullptr) || (m_itemsPage == nullptr)) {
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
       qobject_cast<MediaItemWidget *>(obj) != nullptr);
  if (!target) {
    return false;
  }

  QPoint clickPos = mouseEvent->pos();
  if (obj != m_gridContainer) {
    if (auto *w = qobject_cast<QWidget *>(obj)) {
      clickPos = m_gridContainer->mapFromGlobal(w->mapToGlobal(clickPos));
    }
  }

  if (m_scrollManager == nullptr) {
    emit clearSelectionRequested();
    event->accept();
    return true;
  }

  MediaItemWidget *chosen = MouseManager::findBestWidgetForClick(
      clickPos, m_scrollManager, m_gridContainer);
  if (chosen != nullptr) {
    emit widgetClicked(chosen, clickPos, mouseEvent);
    event->accept();
    return true;
  }
  emit clearSelectionRequested();
  event->accept();
  return true;
}

int EventManager::getCurrentGridWidth() const {
  if (m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    return 0;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return 0;
  }
  return (*m_collections)[*m_currentCollectionIndex].gridWidth;
}

QList<int> EventManager::getSubcollections(int parentIndex) const {
  if (m_collections == nullptr) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}

bool EventManager::applyWheelSelectionDelta(int wheelSteps) {
  if (wheelSteps == 0 || m_scrollManager == nullptr ||
      m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    return false;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
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

  bool wrap = (m_mainWindow != nullptr)
                  ? m_mainWindow->m_generalSettings.wrapNavigation
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
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(newSelection);
  }

  if (wrapTriggered && m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(newSelection);
    if (m_scrollManager != nullptr) {
      m_scrollManager->updateVirtualView();
    }
  }

  return wrapTriggered;
}
