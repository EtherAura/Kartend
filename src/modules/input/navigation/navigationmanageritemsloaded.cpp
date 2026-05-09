// Sibling TU: collection-selected / items-loaded flow for NavigationManager.
#include "artworkmanager.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "detailspane.h"
#include "detailspanemanager.h"
#include "emptystatewidget.h"
#include "errordialog.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationhelpers.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants.h"
#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QtGlobal>
#include <QTimer>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcNavigationManager().isDebugEnabled()) {                                                  \
      qCDebug(lcNavigationManager) << msg;                                                         \
    }                                                                                              \
  } while (0)

void NavigationManager::onCollectionSelected(int collectionIndex) {
  // Start dentry prewarm early - while DB query runs, we warm the filesystem
  // cache so artwork lookups are fast when widgets appear
  if (m_artworkManager) {
    m_artworkManager->startEarlyDentryPrewarm(collectionIndex);
  }

  m_stackManager->clear();
  showCollectionItems(collectionIndex);
}

// Validates basic context for items loaded operations
auto NavigationManager::validateItemsLoadedContext() const -> bool {
  return parent() && !QApplication::closingDown() && !m_isShuttingDown();
}

// Cleans up existing no-items widgets from previous loads
auto NavigationManager::cleanupExistingNoItemsWidgets() -> void {
  if (m_loadingLabel) {
    m_loadingLabel->setVisible(false);
  }

  QList<QWidget *> existingLabels = m_gridContainer->findChildren<QWidget *>("noItemsWidget");
  for (QWidget *widget : existingLabels) {
    widget->deleteLater();
  }
}

// Determines if content is available considering subcollections and descendant
// media
auto NavigationManager::determineContentAvailability(const QStringList &filePaths,
                                                     const QList<int> &subcollections) const
    -> bool {
  bool hasContent = !filePaths.isEmpty() || !subcollections.isEmpty();

  if ((*m_currentCollectionIndex) >= 0 && (*m_currentCollectionIndex) < (*m_collections).size()) {
    const CollectionConfig &collection = (*m_collections)[(*m_currentCollectionIndex)];
    if (collection.showAllSubcollectionItems && filePaths.isEmpty() && !subcollections.isEmpty()) {
      QList<int> allDescendants = getAllDescendantCollections((*m_currentCollectionIndex));
      for (int descendantIndex : allDescendants) {
        if (descendantIndex >= 0 && descendantIndex < (*m_collections).size()) {
          if (!(*m_collections)[descendantIndex].mediaDirectory.trimmed().isEmpty()) {
            hasContent = true;
            break;
          }
        }
      }
    }
  }
  return hasContent;
}

// Handles the case when no content is available
auto NavigationManager::handleEmptyContent() -> void {
  // Hide loading overlay since we're done loading (even if empty)
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }

  const bool shuttingDown = QApplication::closingDown() || (m_isShuttingDown && m_isShuttingDown());

  if (!shuttingDown) {
    if (m_loadingLabel) {
      const QString searchText = m_searchBar ? m_searchBar->text().trimmed() : QString();
      const bool searchActive = !searchText.isEmpty();

      bool collectionConfigured = true;
      bool isSubcollectionOnly = false;
      QString collectionName;
      if (m_collections && m_currentCollectionIndex && *m_currentCollectionIndex >= 0 &&
          *m_currentCollectionIndex < (*m_collections).size()) {
        const CollectionConfig &cfg = (*m_collections)[*m_currentCollectionIndex];
        collectionName = cfg.name;
        const QString mediaDir = cfg.mediaDirectory.trimmed();
        // A leaf collection with no media directory configured is the
        // "needs setup" case; subcollection-only entries are valid empty.
        isSubcollectionOnly = cfg.isSubcollection && mediaDir.isEmpty();
        collectionConfigured = !mediaDir.isEmpty() || cfg.isSubcollection;
      }

      if (searchActive) {
        m_loadingLabel->showSearchEmpty(searchText);
      } else if (!collectionConfigured) {
        m_loadingLabel->showNoMediaDirectory();
      } else if (isSubcollectionOnly) {
        m_loadingLabel->showMessage(tr("This collection has no items"),
                                    tr("Choose a child collection from the sidebar."),
                                    QStringLiteral("📂"));
      } else {
        const QString hint = collectionName.isEmpty()
                                 ? tr("Add files to the collection's media directory.")
                                 : tr("Add files to %1's media directory.").arg(collectionName);
        m_loadingLabel->showMessage(tr("No items found"), hint, QStringLiteral("📭"));
      }
    }
    resumeItemsPageRendering();
    if (m_refreshTitleCounts) m_refreshTitleCounts();
  }
  if ((parent()) && (m_interactionManager)) {
    m_interactionManager->setNavigationInProgress(false);
  }
}

// Sets up collection context for items loaded
auto NavigationManager::setupCollectionContext(const QStringList &filePaths,
                                               const QHash<QString, QString> &fileNames) const
    -> CollectionContext {
  CollectionContext context;
  context.currentIndex = (*m_currentCollectionIndex);
  context.config = (*m_collections)[(*m_currentCollectionIndex)];
  context.artworkDirectory = context.config.artworkDirectory;
  context.filePaths = filePaths;
  context.fileNames = fileNames;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
    // mirror toolbar filters so subcollection tile visibility
    // honors the active type filter / hide-subs toggle.
    context.collectionTypeFilter = m_generalSettings->collectionTypeFilter;
    context.hideSubcollectionTiles = m_generalSettings->hideSubcollectionTiles;
  }
  if (m_allCollectionsActive) {
    context.config.showAllSubcollectionItems = true;
  }
  return context;
}

auto NavigationManager::lookupRememberedSelectionIndex(int totalItems) const -> int {
  if (!m_sessionManager || !m_collections || !m_currentCollectionIndex) {
    return -1;
  }
  return NavigationHelpers::lookupRememberedSelectionIndex(
      *m_currentCollectionIndex, *m_collections, totalItems,
      [this](const QString &key) { return m_sessionManager->getLastSelectedIndex(key); });
}

// Calculates the appropriate selection index for restoration
auto NavigationManager::calculateSelectionIndex(int totalItems) const -> int {
  if (!m_collections || !m_currentCollectionIndex || !m_generalSettings) {
    return -1;
  }
  const bool searchActive = (m_searchBar && !m_searchBar->text().trimmed().isEmpty());
  return NavigationHelpers::calculateSelectionIndex(
      *m_currentCollectionIndex, *m_collections, totalItems, searchActive,
      m_generalSettings->rememberSelection, [this](const QString &key) {
        return m_sessionManager ? m_sessionManager->getLastSelectedIndex(key) : -1;
      });
}

// Computes the depth of a collection in the hierarchy
auto NavigationManager::computeCollectionDepth(int collectionIndex) const -> int {
  if (!m_collections) {
    return 0;
  }
  return NavigationHelpers::computeCollectionDepth(collectionIndex, *m_collections);
}

// Schedules post-load operations including artwork loading and navigation
// cleanup
auto NavigationManager::schedulePostLoadOperations() -> void {
  // Track if we're completing a rescan (affects title refresh timing)
  bool wasRescan = m_isRescanInProgress;
  m_isRescanInProgress = false;

  // Hide loading overlay after items have loaded
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }

  // Delay silent loading start until items are visible and user can interact -
  // Background precaching disabled - only load visible viewport items
  // to minimize CPU usage when idle

  if (m_databaseManager) {
    m_databaseManager->updateCachedCounts((*m_collections));
  }

  // After a rescan, delay title refresh to allow main thread database to see
  // the new data committed by the worker thread (SQLite WAL read visibility)
  if (wasRescan) {
    QTimer::singleShot(100, this, [this]() {
      if (m_refreshTitleCounts) m_refreshTitleCounts();
    });
  } else {
    if (m_refreshTitleCounts) m_refreshTitleCounts();
  }

  m_allCollectionsActive = false;

  // Clear navigation progress flag after viewport settles -
  // allows user input to be processed again after load completes
  QTimer::singleShot(UIConstants::Timing::VIEWPORT_DELAY_MS, this, [this]() {
    if (parent() && m_interactionManager) {
      m_interactionManager->setNavigationInProgress(false);
    }
  });
}

// Handles items loaded; restores selection and resumes rendering after layout
// priming
