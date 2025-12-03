// Handles keyboard input processing, arrow key navigation, and key repeat behavior.
#include "keyboardmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QLineEdit>
#include <QScrollArea>
#include <QStackedWidget>
#include <QWidget>
#include <algorithm>

#include "applicationcontext.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "scrollmanager.h"
#include "uiconstants.h"

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcKeyboardManager, "kartend.keyboardmanager")
#define debugLog(msg) qCDebug(lcKeyboardManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

// KeyboardManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, ScrollManager*, ScrollManager, scrollManager)
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, QWidget*, GridContainer, gridContainer)
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, QWidget*, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, QScrollArea*, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, QStackedWidget*, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, QLineEdit*, SearchBar, searchBar)
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, QList<CollectionConfig>*, Collections, collections)
SETUP_GETTER_DEF_SAME(KeyboardManagerSetup, int*, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_CTX_ONLY(KeyboardManagerSetup, InteractionStateHolder*, InteractionState, interactionState)

KeyboardManager::KeyboardManager(QObject *parent) : QObject(parent) {
  initTimers();
  m_continuousScrollActive = true;
}

KeyboardManager::~KeyboardManager() {
  m_isShuttingDown = true;
  cleanupTimers();
}

void KeyboardManager::initTimers() {
  m_repeatStartTimer = new QTimer(this);
  m_repeatStartTimer->setSingleShot(true);
  connect(m_repeatStartTimer, &QTimer::timeout, this,
          &KeyboardManager::onRepeatStartTimeout);

  m_repeatTimer = new QTimer(this);
  m_repeatTimer->setSingleShot(false);
  connect(m_repeatTimer, &QTimer::timeout, this,
          &KeyboardManager::onRepeatStep);
}

void KeyboardManager::cleanupTimers() {
  if (m_repeatTimer) {
    m_repeatTimer->stop();
    m_repeatTimer->deleteLater();
    m_repeatTimer = nullptr;
  }
  if (m_repeatStartTimer) {
    m_repeatStartTimer->stop();
    m_repeatStartTimer->deleteLater();
    m_repeatStartTimer = nullptr;
  }
}

void KeyboardManager::setupReferences(const KeyboardManagerSetup &setup) {
  m_state = setup.getInteractionState();
  m_scrollManager = setup.getScrollManager();
  m_gridContainer = setup.getGridContainer();
  m_itemsPage = setup.getItemsPage();
  m_itemScrollArea = setup.getItemScrollArea();
  m_stackedWidget = setup.getStackedWidget();
  m_searchBar = setup.getSearchBar();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
}

bool KeyboardManager::handleKeyPress(QKeyEvent *event, bool searchBarFocused) {
  if (!event) {
    return false;
  }

  // Consume auto-repeat events (we handle our own repeat logic)
  if (event->isAutoRepeat()) {
    return true;
  }

  const int key = event->key();

  // Handle search bar focused state
  if (searchBarFocused) {
    if (key == Qt::Key_Slash) {
      if (m_searchBar && m_searchBar->text().trimmed().isEmpty()) {
        emit requestSearchModeToggle();
        return true;
      }
    } else if (key == Qt::Key_Escape) {
      if (m_searchBar && !m_searchBar->text().trimmed().isEmpty()) {
        emit requestClearSearchBar();
        return true;
      } else {
        emit requestFocusGrid();
        return true;
      }
    }
    return false;
  }

  // Global key handling
  if (key == Qt::Key_Slash) {
    emit requestSearchBarFocus();
    return true;
  }
  if (key == Qt::Key_Escape) {
    emit requestEscapeAction();
    return true;
  }
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    emit requestEnterAction();
    return true;
  }

  // Arrow key handling
  const bool isArrowKey = (key == Qt::Key_Left || key == Qt::Key_Right ||
                           key == Qt::Key_Up || key == Qt::Key_Down);
  if (isArrowKey) {
    int gridWidth = 1;
    if (m_scrollManager) {
      gridWidth = m_scrollManager->getCurrentGridWidth();
      if (gridWidth <= 0) {
        gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
      }
    }

    int direction = 0;
    bool vertical = false;
    if (deriveDirectionForKey(key, gridWidth, direction, vertical)) {
      emit requestSelectionMove(direction, vertical);
      return true;
    }
  }

  // PageUp/PageDown for alphabetic navigation
  if (key == Qt::Key_PageUp) {
    emit requestAlphabeticNavigation(false); // backward
    return true;
  }
  if (key == Qt::Key_PageDown) {
    emit requestAlphabeticNavigation(true); // forward
    return true;
  }

  return false;
}

bool KeyboardManager::handleKeyRelease(QKeyEvent *event) {
  if (!event) {
    return false;
  }

  const int keyCode = event->key();
  const bool isArrow = (keyCode == Qt::Key_Left || keyCode == Qt::Key_Right ||
                        keyCode == Qt::Key_Up || keyCode == Qt::Key_Down);
  if (!isArrow) {
    return false;
  }

  const bool physicalRelease = !event->isAutoRepeat();
  if (!physicalRelease) {
    return false;
  }

  if (m_repeatStartTimer && m_repeatStartTimer->isActive() &&
      keyCode == static_cast<int>(m_repeatKey)) {
    m_repeatStartTimer->stop();
  }

  if (m_repeating && keyCode == static_cast<int>(m_repeatKey)) {
    m_physicalKeyDown = false;
    stopRepeat();
    return true;
  }

  m_physicalKeyDown = false;
  if (m_state) {
    m_state->scroll().horizHoldActive = false;
  }

  return false;
}

void KeyboardManager::beginHoldRepeat() {
  if (m_isShuttingDown) {
    return;
  }

  constexpr int kVerticalRepeatIntervalMs = 250;
  constexpr int kHorizontalRepeatIntervalMs = 130;
  constexpr qint64 kSuppressArrowCenterHoldMs = 60000; // 60s safeguard window

  if (!m_repeatTimer) {
    m_repeatTimer = new QTimer(this);
    m_repeatTimer->setSingleShot(false);
    connect(m_repeatTimer, &QTimer::timeout, this,
            &KeyboardManager::onRepeatStep);
  }
  if (m_repeatStartTimer) {
    m_repeatStartTimer->stop();
  }

  m_repeating = true;
  m_repeatInterval =
      m_repeatVertical ? kVerticalRepeatIntervalMs : kHorizontalRepeatIntervalMs;

  if (m_state) {
    m_state->scroll().horizHoldActive = !m_repeatVertical;
    m_state->scroll().keyContinuous = true;
  }
  m_continuousScrollActive = true;

  if (m_state) {
    m_state->arrow().arrowKeyScrolling = true;
  }

  if (m_state) {
    m_state->artwork().suppressArtwork = true;
    m_state->artwork().allowDuringSelection = true;
    if (!m_repeatVertical) {
      m_state->arrow().suppressArrowCenter = true;
      m_state->arrow().suppressArrowCenterUntilMs =
          QDateTime::currentMSecsSinceEpoch() + kSuppressArrowCenterHoldMs;
    }
  }

  m_repeatTimer->start(m_repeatInterval);
}

void KeyboardManager::stopRepeat(bool suppressRecentering) {
  if (m_isShuttingDown || QApplication::closingDown()) {
    clearRepeatState();
    if (m_state) {
      m_state->scroll().keyContinuous = false;
    }
    return;
  }

  if (m_repeatTimer) {
    m_repeatTimer->stop();
  }
  if (m_repeatStartTimer) {
    m_repeatStartTimer->stop();
  }

  clearRepeatState();

  if (m_state) {
    m_state->scroll().horizHoldActive = false;
    m_state->scroll().keyContinuous = false;
    m_state->click().armFirstClickDelay = false;
    m_state->click().pendingInitialCenter = false;
  }

  if (m_state) {
    m_state->arrow().arrowKeyScrolling = false;
    m_state->setGlideAnimating(false);
    if (m_scrollManager) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }

  if (m_scrollManager) {
    m_scrollManager->setForceSelectionOverlayVisible(false);
  }

  if (m_state) {
    m_state->artwork().suppressArtwork = false;
    m_state->artwork().allowDuringSelection = true;
    m_state->clearArrowCenterSuppression();
  }

  emit stopRepeatRequested(suppressRecentering);
}

void KeyboardManager::clearRepeatState() {
  m_repeating = false;
  m_repeatKey = Qt::Key_unknown;
  m_repeatDelta = 0;
  m_repeatVertical = false;
  m_wrapSequenceActive = false;
}

void KeyboardManager::onRepeatStep() {
  if (!m_repeating || !m_physicalKeyDown || m_repeatDelta == 0) {
    stopRepeat();
    return;
  }
  if (!m_scrollManager || !m_collections ||
      !m_currentCollectionIndex) {
    stopRepeat();
    return;
  }
  if (!m_stackedWidget || !m_itemsPage ||
      m_stackedWidget->currentWidget() != m_itemsPage) {
    stopRepeat();
    return;
  }
  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    stopRepeat();
    return;
  }

  emit repeatStepRequested();
}

void KeyboardManager::onRepeatStartTimeout() {
  beginHoldRepeat();
}

void KeyboardManager::prepareKeyNavigationState() {
  if (m_state) {
    m_state->scroll().userFreeScroll = false;
    m_state->setHorizAnimActive(false);
    m_state->nextHorizAnimGen();
    m_state->click().clickForceAnim = false;
    m_state->click().suppressInitialClickCenter = false;
  }
  m_physicalKeyDown = true;
}

void KeyboardManager::finalizeKeyRepeat(QKeyEvent *event, int direction,
                                        bool vertical) {
  // If no event provided, derive key from direction and vertical
  if (event) {
    m_repeatKey = static_cast<Qt::Key>(event->key());
  } else {
    // Derive key from direction and vertical flags
    if (vertical) {
      m_repeatKey = (direction > 0) ? Qt::Key_Down : Qt::Key_Up;
    } else {
      m_repeatKey = (direction > 0) ? Qt::Key_Right : Qt::Key_Left;
    }
  }
  m_repeatDelta = direction;
  m_repeatVertical = vertical;

  if (m_repeatStartTimer && !m_repeating) {
    m_repeatStartTimer->start(UIConstants::Keyboard::REPEAT_START_DELAY_MS);
  }

  if (m_itemsPage) {
    m_itemsPage->setFocus();
  }
}

int KeyboardManager::calculateNewSelection(int totalItems, int currentSelection,
                                           int direction, bool wrapEnabled,
                                           bool vertical, int gridWidth,
                                           bool &didWrap) {
  didWrap = false;
  if (vertical) {
    return calculateVerticalSelection(totalItems, currentSelection, direction,
                                      wrapEnabled, gridWidth, didWrap);
  }
  return calculateHorizontalSelection(totalItems, currentSelection, direction,
                                      wrapEnabled, didWrap);
}

int KeyboardManager::calculateHorizontalSelection(int totalItems,
                                                  int currentSelection,
                                                  int direction,
                                                  bool wrapEnabled,
                                                  bool &didWrap) {
  didWrap = false;
  int newSelection = currentSelection + direction;
  if (wrapEnabled) {
    if (direction == -1 && currentSelection == 0) {
      newSelection = totalItems - 1;
      didWrap = true;
    } else if (direction == 1 && currentSelection == totalItems - 1) {
      newSelection = 0;
      didWrap = true;
    }
  }
  if (!didWrap) {
    newSelection = std::max(newSelection, 0);
    if (newSelection >= totalItems) {
      newSelection = totalItems - 1;
    }
  }
  return newSelection;
}

int KeyboardManager::calculateVerticalSelection(int totalItems,
                                                int currentSelection,
                                                int direction, bool wrapEnabled,
                                                int gridWidth, bool &didWrap) {
  didWrap = false;
  int newSelection = currentSelection + direction;
  if (wrapEnabled && gridWidth > 0) {
    if (direction == -gridWidth && currentSelection < gridWidth) {
      const int lastRowFirst = ((totalItems - 1) / gridWidth) * gridWidth;
      const int targetColumn = currentSelection % gridWidth;
      const int candidate = lastRowFirst + targetColumn;
      newSelection = qMin(candidate, totalItems - 1);
      didWrap = true;
    } else if (direction == gridWidth &&
               currentSelection + gridWidth >= totalItems) {
      newSelection = currentSelection % gridWidth;
      if (newSelection >= totalItems) {
        newSelection = totalItems - 1;
      }
      didWrap = true;
    }
  }
  if (!didWrap) {
    newSelection = std::max(newSelection, 0);
    if (newSelection >= totalItems) {
      newSelection = totalItems - 1;
    }
  }
  return newSelection;
}

bool KeyboardManager::hasRowChanged(int gridWidth, int currentSelection,
                                    int newSelection) {
  if (gridWidth <= 0) {
    return false;
  }
  return (currentSelection / gridWidth) != (newSelection / gridWidth);
}

bool KeyboardManager::deriveDirectionForKey(int key, int gridWidth,
                                            int &direction, bool &vertical) {
  direction = 0;
  vertical = false;

  switch (key) {
  case Qt::Key_Left:
    direction = -1;
    vertical = false;
    return true;
  case Qt::Key_Right:
    direction = 1;
    vertical = false;
    return true;
  case Qt::Key_Up:
    direction = -gridWidth;
    vertical = true;
    return true;
  case Qt::Key_Down:
    direction = gridWidth;
    vertical = true;
    return true;
  default:
    return false;
  }
}
