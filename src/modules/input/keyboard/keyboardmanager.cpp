// Handles keyboard input processing, arrow key navigation, and key repeat
// behavior.
#include "keyboardmanager.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QLineEdit>
#include <QScrollArea>
#include <QStackedWidget>
#include <QWidget>

#include "applicationcontext.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "keyboardhelpers.h"
#include "scrollmanager.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcKeyboardManager, "kartend.keyboardmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcKeyboardManager().isDebugEnabled()) {                                                    \
      qCDebug(lcKeyboardManager) << msg;                                                           \
    }                                                                                              \
  } while (0)

// KeyboardManagerSetup getter definitions
SETUP_GETTER_DEF_MGR_SAME(KeyboardManagerSetup, ScrollManager *, ScrollManager, scrollManager)
SETUP_GETTER_DEF_UI_SAME(KeyboardManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_UI_SAME(KeyboardManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_UI_SAME(KeyboardManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_UI_SAME(KeyboardManagerSetup, QStackedWidget *, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_UI_SAME(KeyboardManagerSetup, QLineEdit *, SearchBar, searchBar)
SETUP_GETTER_DEF_COL_SAME(KeyboardManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_COL_SAME(KeyboardManagerSetup, int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_MGR_CTX_ONLY(KeyboardManagerSetup, InteractionStateHolder *, InteractionState,
                              interactionState)

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
  connect(m_repeatStartTimer, &QTimer::timeout, this, &KeyboardManager::onRepeatStartTimeout);

  m_repeatTimer = new QTimer(this);
  m_repeatTimer->setSingleShot(false);
  connect(m_repeatTimer, &QTimer::timeout, this, &KeyboardManager::onRepeatStep);
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
  m_generalSettings = setup.generalSettings;
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

  const int searchKey =
      m_generalSettings ? m_generalSettings->keySearch : static_cast<int>(Qt::Key_Slash);
  const int backKey =
      m_generalSettings ? m_generalSettings->keyBack : static_cast<int>(Qt::Key_Escape);
  const int confirmKey =
      m_generalSettings ? m_generalSettings->keyConfirm : static_cast<int>(Qt::Key_Return);

  auto isConfirmKey = [&](int k) -> bool {
    return KeyboardHelpers::isConfirmKey(k, confirmKey);
  };

  // Handle search bar focused state
  if (searchBarFocused) {
    if (key == searchKey) {
      if (m_searchBar && m_searchBar->text().trimmed().isEmpty()) {
        emit requestSearchModeToggle();
        return true;
      }
    } else if (key == backKey) {
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
  if (key == searchKey) {
    emit requestSearchBarFocus();
    return true;
  }
  if (key == backKey) {
    emit requestEscapeAction();
    return true;
  }
  if (isConfirmKey(key)) {
    emit requestEnterAction();
    return true;
  }

  // Arrow key handling
  const int navLeftKey =
      m_generalSettings ? m_generalSettings->keyNavLeft : static_cast<int>(Qt::Key_Left);
  const int navRightKey =
      m_generalSettings ? m_generalSettings->keyNavRight : static_cast<int>(Qt::Key_Right);
  const int navUpKey =
      m_generalSettings ? m_generalSettings->keyNavUp : static_cast<int>(Qt::Key_Up);
  const int navDownKey =
      m_generalSettings ? m_generalSettings->keyNavDown : static_cast<int>(Qt::Key_Down);

  const bool isNavKey =
      (key == navLeftKey || key == navRightKey || key == navUpKey || key == navDownKey);
  if (isNavKey) {
    int gridWidth = 1;
    if (m_scrollManager) {
      gridWidth = m_scrollManager->getCurrentGridWidth();
      if (gridWidth <= 0) {
        gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
      }
    }

    ViewType viewType = ViewType::Grid;
    if (CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
      viewType = (*m_collections)[*m_currentCollectionIndex].viewType;
    }

    const auto arrow = KeyboardHelpers::computeArrowDirection(
        key, navLeftKey, navRightKey, navUpKey, navDownKey, viewType, gridWidth);

    if (arrow.valid && arrow.direction != 0) {
      m_pendingNavigationKey = static_cast<Qt::Key>(key);
      m_pendingNavigationKeyAtMs = QDateTime::currentMSecsSinceEpoch();
      m_hasPendingNavigationKey = true;
      emit requestSelectionMove(arrow.direction, arrow.vertical);
      return true;
    }
  }

  // PageUp/PageDown for alphabetic navigation
  const int alphaBackKey =
      m_generalSettings ? m_generalSettings->keyAlphabeticBack : static_cast<int>(Qt::Key_PageUp);
  const int alphaForwardKey = m_generalSettings ? m_generalSettings->keyAlphabeticForward
                                                : static_cast<int>(Qt::Key_PageDown);
  if (key == alphaBackKey) {
    emit requestAlphabeticNavigation(false); // backward
    return true;
  }
  if (key == alphaForwardKey) {
    emit requestAlphabeticNavigation(true); // forward
    return true;
  }

  // Home/End for jumping to first/last item
  const int jumpFirstKey =
      m_generalSettings ? m_generalSettings->keyJumpFirst : static_cast<int>(Qt::Key_Home);
  const int jumpLastKey =
      m_generalSettings ? m_generalSettings->keyJumpLast : static_cast<int>(Qt::Key_End);
  if (key == jumpFirstKey) {
    emit requestJumpToEdge(false); // jump to first item
    return true;
  }
  if (key == jumpLastKey) {
    emit requestJumpToEdge(true); // jump to last item
    return true;
  }

  // detail page. Checked last so it can't shadow a re-bound
  // arrow / search / confirm key, and only outside the search bar (the
  // search-focused branch above returns before reaching here, so this is
  // already gated).
  const int detailsKey =
      m_generalSettings ? m_generalSettings->keyItemDetails : static_cast<int>(Qt::Key_I);
  if (key == detailsKey) {
    emit requestItemDetails();
    return true;
  }

  if (m_generalSettings && m_generalSettings->useHomeView && m_generalSettings->keyHomeView != 0 &&
      key == m_generalSettings->keyHomeView) {
    emit requestHomeView();
    return true;
  }

  return false;
}

bool KeyboardManager::handleKeyRelease(QKeyEvent *event) {
  if (!event) {
    return false;
  }

  const int keyCode = event->key();

  const int navLeftKey =
      m_generalSettings ? m_generalSettings->keyNavLeft : static_cast<int>(Qt::Key_Left);
  const int navRightKey =
      m_generalSettings ? m_generalSettings->keyNavRight : static_cast<int>(Qt::Key_Right);
  const int navUpKey =
      m_generalSettings ? m_generalSettings->keyNavUp : static_cast<int>(Qt::Key_Up);
  const int navDownKey =
      m_generalSettings ? m_generalSettings->keyNavDown : static_cast<int>(Qt::Key_Down);

  const bool isNav = (keyCode == navLeftKey || keyCode == navRightKey || keyCode == navUpKey ||
                      keyCode == navDownKey);
  if (!isNav) {
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

void KeyboardManager::finalizeKeyRepeat(QKeyEvent *event, int direction, bool vertical) {
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
    // Use keyboardRepeatDelayMs for initial delay before repeating starts
    int repeatDelay = m_generalSettings ? m_generalSettings->keyboardRepeatDelayMs : 260;
    m_repeatStartTimer->start(repeatDelay);
  }

  if (m_itemsPage) {
    m_itemsPage->setFocus();
  }
}

void KeyboardManager::finalizeKeyRepeatForKey(Qt::Key key, int direction, bool vertical) {
  m_repeatKey = key;
  m_repeatDelta = direction;
  m_repeatVertical = vertical;

  if (m_repeatStartTimer && !m_repeating) {
    // Use keyboardRepeatDelayMs for initial delay before repeating starts
    int repeatDelay = m_generalSettings ? m_generalSettings->keyboardRepeatDelayMs : 260;
    m_repeatStartTimer->start(repeatDelay);
  }

  if (m_itemsPage) {
    m_itemsPage->setFocus();
  }
}

bool KeyboardManager::consumePendingNavigationKey(Qt::Key &outKey) {
  if (!m_hasPendingNavigationKey) {
    return false;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 ageMs = now - m_pendingNavigationKeyAtMs;
  if (ageMs > UIConstants::Timing::MEDIUM_DELAY_MS) {
    m_hasPendingNavigationKey = false;
    m_pendingNavigationKey = Qt::Key_unknown;
    m_pendingNavigationKeyAtMs = 0;
    return false;
  }

  outKey = m_pendingNavigationKey;
  m_hasPendingNavigationKey = false;
  m_pendingNavigationKey = Qt::Key_unknown;
  m_pendingNavigationKeyAtMs = 0;
  return true;
}
