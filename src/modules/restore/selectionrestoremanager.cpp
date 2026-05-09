// Manages selection restoration logic for collection navigation.
#include "selectionrestoremanager.h"

#include <memory>
#include <QApplication>
#include <QLineEdit>
#include <QTimer>

#include "applicationcontext.h"
#include "collectionutils.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "setuputils.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSelectionRestoreManager, "kartend.selectionrestoremanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSelectionRestoreManager().isDebugEnabled()) {                                            \
      qCDebug(lcSelectionRestoreManager) << msg;                                                   \
    }                                                                                              \
  } while (0)

SETUP_GETTER_DEF_MGR_SAME(SelectionRestoreManagerSetup, InteractionManager *, InteractionManager,
                          interactionManager)
SETUP_GETTER_DEF_MGR_SAME(SelectionRestoreManagerSetup, ScrollManager *, ScrollManager,
                          scrollManager)
SETUP_GETTER_DEF_MGR_SAME(SelectionRestoreManagerSetup, SessionManager *, SessionManager,
                          sessionManager)
SETUP_GETTER_DEF_MGR_SAME(SelectionRestoreManagerSetup, SettingsManager *, SettingsManager,
                          settingsManager)
SETUP_GETTER_DEF_UI_SAME(SelectionRestoreManagerSetup, QLineEdit *, SearchBar, searchBar)
SETUP_GETTER_DEF_COL_SAME(SelectionRestoreManagerSetup, int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_COL_SAME(SelectionRestoreManagerSetup, QList<CollectionConfig> *, Collections,
                          collections)
SETUP_GETTER_DEF_COL_SAME(SelectionRestoreManagerSetup, GeneralSettings *, GeneralSettings,
                          generalSettings)
SETUP_GETTER_DEF_MGR_CTX_ONLY(SelectionRestoreManagerSetup, InteractionStateHolder *,
                              InteractionState, interactionState)

SelectionRestoreManager::SelectionRestoreManager(QObject *parent) : QObject(parent) {}

SelectionRestoreManager::~SelectionRestoreManager() = default;

void SelectionRestoreManager::setupReferences(const SelectionRestoreManagerSetup &setup) {
  m_interactionManager = setup.getInteractionManager();
  m_state = setup.getInteractionState();
  m_scrollManager = setup.getScrollManager();
  m_sessionManager = setup.getSessionManager();
  m_settingsManager = setup.getSettingsManager();
  m_searchBar = setup.getSearchBar();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_collections = setup.getCollections();
  m_generalSettings = setup.getGeneralSettings();
  m_isShuttingDown = setup.isShuttingDown;
}

auto SelectionRestoreManager::shouldRestoreSelection() const -> bool {
  if (!m_generalSettings) {
    return false;
  }
  const bool remember = m_generalSettings->rememberSelection;
  const bool searchActive = (m_searchBar && !m_searchBar->text().trimmed().isEmpty());
  return remember && !searchActive && m_scrollManager && m_interactionManager;
}

auto SelectionRestoreManager::getSelectionRestoreIndex(int collectionIndex) const -> int {
  if (!m_scrollManager || !m_collections) {
    return -1;
  }

  int total = m_scrollManager->getTotalItems();
  if (total <= 0) {
    return -1;
  }

  if (!CollectionUtils::isValidIndex(collectionIndex, m_collections)) {
    return -1;
  }

  const CollectionConfig &cfg = (*m_collections)[collectionIndex];
  const bool subfolderActive = !cfg.currentSubfolder.trimmed().isEmpty();

  QString hierarchicalName = CollectionUtils::hierarchicalNameFor(cfg, *m_collections);
  int selIdx = -1;
  if (m_sessionManager) {
    if (subfolderActive) {
      const QString sessionKey = CollectionUtils::selectionSessionKeyFor(cfg, *m_collections);
      selIdx = m_sessionManager->getLastSelectedIndex(sessionKey);
    } else {
      selIdx = m_sessionManager->getLastSelectedIndex(hierarchicalName);
      if (selIdx < 0) {
        selIdx = m_sessionManager->getLastSelectedIndex(cfg.name);
      }
    }
  }
  if (selIdx >= total) {
    selIdx = total - 1;
  }
  return (selIdx >= 0) ? selIdx : -1;
}

auto SelectionRestoreManager::validateSelectionRestoreContext() const -> bool {
  if (!parent() || QApplication::closingDown()) {
    return false;
  }
  if (m_isShuttingDown && m_isShuttingDown()) {
    return false;
  }
  if (!m_scrollManager || !m_interactionManager) {
    return false;
  }
  return true;
}

auto SelectionRestoreManager::initializeSelectionRestoreToken() const -> int {
  if (!m_state) {
    return 0;
  }
  m_state->selectionRestore().restorePending = true;
  return ++m_state->selectionRestore().restoreToken;
}

auto SelectionRestoreManager::createRestoreValidationLambda(int scheduledCollectionIndex,
                                                            int token) const
    -> std::function<bool()> {
  return [this, scheduledCollectionIndex, token]() -> bool {
    if (!validateSelectionRestoreContext()) {
      debugLog("[SelectionRestore] validator: validateSelectionRestoreContext "
               "failed");
      return false;
    }
    if (!m_currentCollectionIndex || *m_currentCollectionIndex != scheduledCollectionIndex) {
      debugLog("[SelectionRestore] validator: collection mismatch - current="
               << (m_currentCollectionIndex ? *m_currentCollectionIndex : -1)
               << "scheduled=" << scheduledCollectionIndex);
      if (m_state) {
        m_state->selectionRestore().restorePending = false;
      }
      return false;
    }
    if (!m_state || m_state->selectionRestore().restoreToken != token) {
      debugLog("[SelectionRestore] validator: token mismatch - state token="
               << (m_state ? m_state->selectionRestore().restoreToken : -999)
               << "expected=" << token);
      if (m_state) {
        m_state->selectionRestore().restorePending = false;
      }
      return false;
    }
    return true;
  };
}

auto SelectionRestoreManager::executeSelectionRestore(int desiredIndex,
                                                      int scheduledCollectionIndex, int token) const
    -> void {
  auto validator = createRestoreValidationLambda(scheduledCollectionIndex, token);

  debugLog("[SelectionRestore] executeSelectionRestore: desiredIndex="
           << desiredIndex << "scheduledCollectionIndex=" << scheduledCollectionIndex
           << "token=" << token);

  if (!validator()) {
    debugLog("[SelectionRestore] validator failed");
    return;
  }

  int total = m_scrollManager->getTotalItems();
  debugLog("[SelectionRestore] total items=" << total);
  if (desiredIndex >= 0 && desiredIndex < total) {
    QPointer<const SelectionRestoreManager> guard(this);
    // Delay restore to allow virtual scroll population to complete -
    // widgets may not be materialized immediately after collection load
    QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this,
                       [guard, desiredIndex, validator]() {
                         debugLog("[SelectionRestore] timer fired, calling beginSelectionRestore");
                         if (guard && validator()) {
                           guard->m_interactionManager->beginSelectionRestore(desiredIndex);
                         } else {
                           debugLog("[SelectionRestore] timer: guard or validator failed");
                         }
                       });
  }
  if (m_state) {
    m_state->selectionRestore().restorePending = false;
  }
}

void SelectionRestoreManager::scheduleSelectionRestore(int desiredIndex, int maxAttempts,
                                                       int attemptDelayMs, int finalEnsureDelayMs) {
  Q_UNUSED(maxAttempts)
  Q_UNUSED(attemptDelayMs)

  debugLog("[SelectionRestore] scheduleSelectionRestore: desiredIndex=" << desiredIndex);

  if (!validateSelectionRestoreContext()) {
    debugLog("[SelectionRestore] validateSelectionRestoreContext failed");
    return;
  }

  if (desiredIndex < 0) {
    if (m_interactionManager) {
      m_interactionManager->cancelPendingSelectionRestore();
    }
    return;
  }

  if (!m_currentCollectionIndex) {
    return;
  }

  const int scheduledCollectionIndex = *m_currentCollectionIndex;
  const int token = initializeSelectionRestoreToken();

  QPointer<SelectionRestoreManager> guard(this);
  auto doRestore = [guard, desiredIndex, scheduledCollectionIndex, token]() {
    if (guard) {
      guard->executeSelectionRestore(desiredIndex, scheduledCollectionIndex, token);
    }
  };

  if (m_scrollManager->getTotalItems() > 0) {
    // Items already loaded - short delay allows layout to settle
    QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, doRestore);
  } else {
    auto validator = createRestoreValidationLambda(scheduledCollectionIndex, token);
    // shared_ptr ownership instead of raw new/delete: if the signal never
    // fires (ScrollManager or this destroyed first, guard expires, rapid
    // re-navigation orphans the wait), Qt auto-disconnects and destroys the
    // captured lambda, dropping the last shared_ptr ref and freeing the
    // Connection. Lambda capture by value is required so the handle outlives
    // the surrounding scope.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_scrollManager, &ScrollManager::virtualScrollSetupComplete, this,
                    [conn, doRestore, validator]() {
                      QObject::disconnect(*conn);
                      if (validator()) {
                        doRestore();
                      }
                    });
  }

  if (finalEnsureDelayMs > 0) {
    auto validator = createRestoreValidationLambda(scheduledCollectionIndex, token);
    // Final fallback restore attempt after all animations complete -
    // catches edge cases where earlier attempts were blocked
    QTimer::singleShot(finalEnsureDelayMs, this, [doRestore, validator]() {
      if (validator()) {
        doRestore();
      }
    });
  }
}

auto SelectionRestoreManager::createSelectionRestoreLambda(int collectionIndex, int selIdx,
                                                           int token) -> std::function<void()> {
  QPointer<SelectionRestoreManager> guard(this);
  return [guard, collectionIndex, selIdx, token]() {
    if (!guard || !guard->m_state) {
      return;
    }
    if (!guard->m_currentCollectionIndex || *guard->m_currentCollectionIndex != collectionIndex) {
      return;
    }
    if (guard->m_state->selectionRestore().restoreToken != token) {
      return;
    }
    if (!guard->m_interactionManager || !guard->m_scrollManager) {
      return;
    }
    if (guard->m_interactionManager->currentSelectedIndex() != selIdx) {
      guard->m_interactionManager->beginSelectionRestore(selIdx);
    }
  };
}

void SelectionRestoreManager::scheduleSelectionRestoreVerification(int collectionIndex, int selIdx,
                                                                   int token) {
  auto restoreLambda = createSelectionRestoreLambda(collectionIndex, selIdx, token);

  // Schedule early verification attempts to ensure selection is restored
  // even if initial restore was blocked by layout settling
  QTimer::singleShot(UIConstants::Selection::RESTORE_EARLY_VERIFY_1_MS, this, restoreLambda);
  QTimer::singleShot(UIConstants::Selection::RESTORE_EARLY_VERIFY_2_MS, this, restoreLambda);
}

void SelectionRestoreManager::handleSubcollectionRestore(int collectionIndex) {
  if (!shouldRestoreSelection()) {
    return;
  }

  int selIdx = getSelectionRestoreIndex(collectionIndex);
  if (selIdx < 0) {
    return;
  }

  if (!m_state) {
    return;
  }

  int token = ++m_state->selectionRestore().restoreToken;

  // Delay restore to allow filter application and widget materialization -
  // subcollection filter needs time to update virtual view before selection
  QPointer<SelectionRestoreManager> guard(this);
  QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this,
                     [guard, selIdx, collectionIndex, token]() {
                       if (!guard || !guard->m_interactionManager || !guard->m_state) {
                         return;
                       }
                       if (!guard->m_currentCollectionIndex ||
                           *guard->m_currentCollectionIndex != collectionIndex) {
                         return;
                       }
                       if (guard->m_state->selectionRestore().restoreToken != token) {
                         return;
                       }
                       guard->m_interactionManager->beginSelectionRestore(selIdx);
                     });

  scheduleSelectionRestoreVerification(collectionIndex, selIdx, token);
}
