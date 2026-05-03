// Subcollection / virtual-folder navigation methods for NavigationManager.
// Extracted from navigationmanager.cpp during LOC-reduction refactor.
// These remain NavigationManager members; pure translation-unit split.
#include "interactionmanager.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "uiconstants.h"

#include <QDateTime>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QTimer>

void NavigationManager::onSubcollectionEntered(int subcollectionIndex) {
  if (subcollectionIndex >= 0 && subcollectionIndex < (*m_collections).size()) {
    if (m_state) {
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      m_state->click().suppressDoubleClickUntilMs =
          now + UIConstants::Selection::DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS;
    }

    if ((*m_currentCollectionIndex) >= 0 && (m_interactionManager)) {
      int currentSelection = m_interactionManager->currentSelectedIndex();
      if (currentSelection >= 0) {
        m_settingsManager->setLastSelectedItem((*m_currentCollectionIndex), currentSelection);
      }
    }

    if ((*m_currentCollectionIndex) >= 0 && (*m_currentCollectionIndex) < (*m_collections).size()) {
      m_stackManager->push(*m_currentCollectionIndex);
    }

    m_settingsManager->setLastSelectedItem(subcollectionIndex, -1);

    bool success = showCollectionItems(subcollectionIndex);
    if (!success) {
      // Undo the push if navigation failed
      (void)m_stackManager->pop();
      return;
    }

    // Delay horizontal centering until subcollection layout stabilizes
    QTimer::singleShot(
        UIConstants::Navigation::SUBCOLLECTION_SCROLL_CENTER_DELAY_MS, this, [this]() {
          m_scrollManager->centerHorizontalScrollbar((*m_currentCollectionIndex), (*m_collections));
        });

    // Clear double-click suppression after navigation animation completes
    QTimer::singleShot(UIConstants::Selection::DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS, this,
                       [this]() {
                         if (m_state) {
                           m_state->click().suppressDoubleClickUntilMs = 0;
                         }
                       });
  }
}

void NavigationManager::onVirtualFolderEntered(const QString &folderPath) {
  if (!m_collections || (*m_currentCollectionIndex) < 0) {
    return;
  }

  // Persist the currently selected folder tile in the parent scope so that
  // leaving the subfolder restores selection back to the entered folder.
  if (m_interactionManager) {
    m_interactionManager->saveCurrentSelection();
  }

  if (m_interactionManager) {
    m_interactionManager->stopScrollAnimations();
  }

  // Update the current subfolder path in the collection config
  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];

  // If the folder didn't actually change, avoid an unnecessary reload.
  if (config.currentSubfolder == folderPath) {
    return;
  }

  // Entering a new subfolder should start at the top of that folder's list.
  m_forceTopOnNextItemsViewLoad = true;
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    m_itemScrollArea->verticalScrollBar()->setValue(0);
  }

  config.currentSubfolder = folderPath;

  // Reload the collection to show the new folder contents
  safeReloadCollection(*m_currentCollectionIndex);
}

void NavigationManager::goBackFromVirtualFolder() {
  if (!m_collections || (*m_currentCollectionIndex) < 0) {
    return;
  }

  // Persist the current selection within this virtual subfolder scope before
  // changing currentSubfolder so re-entry can restore it.
  if (m_interactionManager) {
    m_interactionManager->saveCurrentSelection();
  }

  if (m_interactionManager) {
    m_interactionManager->stopScrollAnimations();
  }

  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];

  if (config.currentSubfolder.isEmpty()) {
    // Already at root, nothing to go back to
    return;
  }

  // Navigating to a different folder scope should start at the top.
  m_forceTopOnNextItemsViewLoad = true;
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    m_itemScrollArea->verticalScrollBar()->setValue(0);
  }

  // Go up one level
  int lastSlash = config.currentSubfolder.lastIndexOf('/');
  if (lastSlash > 0) {
    config.currentSubfolder = config.currentSubfolder.left(lastSlash);
  } else {
    config.currentSubfolder.clear();
  }

  // Reload to show parent folder
  safeReloadCollection(*m_currentCollectionIndex);
}

void NavigationManager::onBreadcrumbLinkClicked(const QString &link) {
  if (link.startsWith("collection:")) {
    // Navigate to parent collection
    bool ok = false;
    int targetIndex = link.mid(11).toInt(&ok); // Skip "collection:" prefix
    if (ok && targetIndex >= 0 && targetIndex < (*m_collections).size()) {
      // Clear current subfolder before navigating to parent
      if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < (*m_collections).size()) {
        (*m_collections)[*m_currentCollectionIndex].currentSubfolder.clear();
      }
      goBackToCollections();
    }
  } else if (link.startsWith("subfolder:")) {
    // Navigate to a specific subfolder level
    QString targetPath = link.mid(10); // Skip "subfolder:" prefix
    if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < (*m_collections).size()) {
      CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
      if (config.currentSubfolder != targetPath) {
        if (m_interactionManager) {
          m_interactionManager->saveCurrentSelection();
        }
        if (m_interactionManager) {
          m_interactionManager->stopScrollAnimations();
        }
        m_forceTopOnNextItemsViewLoad = true;
        if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
          m_itemScrollArea->verticalScrollBar()->setValue(0);
        }
      }
      config.currentSubfolder = targetPath;
      safeReloadCollection(*m_currentCollectionIndex);
    }
  } else if (link == "root:") {
    // Navigate back to root of current collection (clear subfolder)
    if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < (*m_collections).size()) {
      CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
      if (!config.currentSubfolder.isEmpty()) {
        if (m_interactionManager) {
          m_interactionManager->saveCurrentSelection();
        }
        if (m_interactionManager) {
          m_interactionManager->stopScrollAnimations();
        }
        m_forceTopOnNextItemsViewLoad = true;
        if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
          m_itemScrollArea->verticalScrollBar()->setValue(0);
        }
      }
      config.currentSubfolder.clear();
      safeReloadCollection(*m_currentCollectionIndex);
    }
  }
}

// Loads items for the current collection with optional subcollection
// aggregation and reapplies any active filter
void NavigationManager::loadCurrentAndSubcollections() {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context;
  context.currentIndex = idx;
  context.config = (*m_collections)[idx];
  context.config.mediaDirectory =
      SettingsUtils::expandConfigVariables(context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory =
      SettingsUtils::expandConfigVariables(context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
    // Kartend-dd8: mirror toolbar filters so subcollection tile visibility
    // honors the active type filter / hide-subs toggle.
    context.collectionTypeFilter = m_generalSettings->collectionTypeFilter;
    context.hideSubcollectionTiles = m_generalSettings->hideSubcollectionTiles;
  }

  context.queryIncludeDescendants = true;
  requestItemCountForContext(context, QString());

  // Delay filter reapplication until item count query completes -
  // ensures filter operates on the updated item list
  QTimer::singleShot(UIConstants::Artwork::FILTER_REAPPLY_DELAY_MS, this, [this]() {
    if (m_searchBar && !m_searchBar->text().trimmed().isEmpty() && m_scrollManager) {
      const QString currentSearchText = m_searchBar->text().trimmed();
      m_scrollManager->applyFilter(currentSearchText);
    }
  });
}

// Loads the aggregated view across all collections and reapplies any active
// filter
void NavigationManager::loadAllCollectionsView() {
  // DB-backed all-collections view (count + on-demand ranges).
  // Primarily used for AllCollections search mode.
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = buildExpandedContextForIndex(idx);
  context.queryIncludeAllCollections = true;
  context.hasSubcollectionOverride = true;
  context.subcollectionOverride = {};
  context.suppressVirtualFolders = true;

  const QString currentFilter = (m_searchBar) ? m_searchBar->text().trimmed() : QString();
  requestItemCountForContext(context, currentFilter);
}
