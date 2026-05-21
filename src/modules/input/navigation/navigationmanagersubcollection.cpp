// Subcollection / virtual-folder navigation methods for NavigationManager.
// Extracted from navigationmanager.cpp during LOC-reduction refactor.
// These remain NavigationManager members; pure translation-unit split.
#include "emptystatewidget.h"
#include "idetailspanemanager.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "navigationhelpers.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "settingsutils.h"
#include "uiconstants.h"

#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QString>
#include <QTimer>

void NavigationManager::onSubcollectionEntered(int subcollectionIndex) {
  if (subcollectionIndex >= 0 && subcollectionIndex < (*m_collections).size()) {
    if (state()) {
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      state()->click().suppressDoubleClickUntilMs =
          now + UIConstants::Selection::DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS;
    }

    if ((*m_currentCollectionIndex) >= 0 && (interactionMgr())) {
      int currentSelection = interactionMgr()->currentSelectedIndex();
      if (currentSelection >= 0) {
        settingsMgr()->setLastSelectedItem((*m_currentCollectionIndex), currentSelection);
      }
    }

    if ((*m_currentCollectionIndex) >= 0 && (*m_currentCollectionIndex) < (*m_collections).size()) {
      m_stackManager->push(*m_currentCollectionIndex);
    }

    settingsMgr()->setLastSelectedItem(subcollectionIndex, -1);

    bool success = showCollectionItems(subcollectionIndex);
    if (!success) {
      // Undo the push if navigation failed
      (void)m_stackManager->pop();
      return;
    }

    // Delay horizontal centering until subcollection layout stabilizes
    QTimer::singleShot(
        UIConstants::Navigation::SUBCOLLECTION_SCROLL_CENTER_DELAY_MS, this, [this]() {
          scrollMgr()->centerHorizontalScrollbar((*m_currentCollectionIndex), (*m_collections));
        });

    // Clear double-click suppression after navigation animation completes
    QTimer::singleShot(UIConstants::Selection::DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS, this,
                       [this]() {
                         if (state()) {
                           state()->click().suppressDoubleClickUntilMs = 0;
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
  if (interactionMgr()) {
    interactionMgr()->saveCurrentSelection();
  }

  if (interactionMgr()) {
    interactionMgr()->stopScrollAnimations();
  }

  // Update the current subfolder path in the collection config
  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];

  // If the folder didn't actually change, avoid an unnecessary reload.
  if (config.folderBrowsing.currentSubfolder == folderPath) {
    return;
  }

  // Entering a new subfolder should start at the top of that folder's list.
  m_forceTopOnNextItemsViewLoad = true;
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    m_itemScrollArea->verticalScrollBar()->setValue(0);
  }

  config.folderBrowsing.currentSubfolder = folderPath;

  // Reload the collection to show the new folder contents
  safeReloadCollection(*m_currentCollectionIndex);
}

void NavigationManager::goBackFromVirtualFolder() {
  if (!m_collections || (*m_currentCollectionIndex) < 0) {
    return;
  }

  // Persist the current selection within this virtual subfolder scope before
  // changing currentSubfolder so re-entry can restore it.
  if (interactionMgr()) {
    interactionMgr()->saveCurrentSelection();
  }

  if (interactionMgr()) {
    interactionMgr()->stopScrollAnimations();
  }

  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];

  if (config.folderBrowsing.currentSubfolder.isEmpty()) {
    // Already at root, nothing to go back to
    return;
  }

  // Navigating to a different folder scope should start at the top.
  m_forceTopOnNextItemsViewLoad = true;
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    m_itemScrollArea->verticalScrollBar()->setValue(0);
  }

  // Go up one level
  config.folderBrowsing.currentSubfolder =
      NavigationHelpers::parentSubfolderPath(config.folderBrowsing.currentSubfolder);

  // Reload to show parent folder
  safeReloadCollection(*m_currentCollectionIndex);
}

void NavigationManager::onBreadcrumbLinkClicked(const QString &link) {
  const NavigationHelpers::BreadcrumbLink parsed = NavigationHelpers::parseBreadcrumbLink(link);
  using Kind = NavigationHelpers::BreadcrumbLink::Kind;

  if (parsed.kind == Kind::Collection) {
    if (parsed.collectionIndex < (*m_collections).size()) {
      // Clear current subfolder before navigating to parent
      if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < (*m_collections).size()) {
        (*m_collections)[*m_currentCollectionIndex].folderBrowsing.currentSubfolder.clear();
      }
      goBackToCollections();
    }
  } else if (parsed.kind == Kind::Subfolder) {
    if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < (*m_collections).size()) {
      CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
      if (config.folderBrowsing.currentSubfolder != parsed.subfolderPath) {
        if (interactionMgr()) {
          interactionMgr()->saveCurrentSelection();
        }
        if (interactionMgr()) {
          interactionMgr()->stopScrollAnimations();
        }
        m_forceTopOnNextItemsViewLoad = true;
        if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
          m_itemScrollArea->verticalScrollBar()->setValue(0);
        }
      }
      config.folderBrowsing.currentSubfolder = parsed.subfolderPath;
      safeReloadCollection(*m_currentCollectionIndex);
    }
  } else if (parsed.kind == Kind::Root) {
    if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < (*m_collections).size()) {
      CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
      if (!config.folderBrowsing.currentSubfolder.isEmpty()) {
        if (interactionMgr()) {
          interactionMgr()->saveCurrentSelection();
        }
        if (interactionMgr()) {
          interactionMgr()->stopScrollAnimations();
        }
        m_forceTopOnNextItemsViewLoad = true;
        if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
          m_itemScrollArea->verticalScrollBar()->setValue(0);
        }
      }
      config.folderBrowsing.currentSubfolder.clear();
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
    // mirror toolbar filters so subcollection tile visibility
    // honors the active type filter / hide-subs toggle.
    context.collectionTypeFilter = m_generalSettings->collectionTypeFilter;
    context.hideSubcollectionTiles = m_generalSettings->hideSubcollectionTiles;
  }

  context.queryIncludeDescendants = true;
  requestItemCountForContext(context, QString());

  // Delay filter reapplication until item count query completes -
  // ensures filter operates on the updated item list
  QTimer::singleShot(UIConstants::Artwork::FILTER_REAPPLY_DELAY_MS, this, [this]() {
    if (m_searchBar && !m_searchBar->text().trimmed().isEmpty() && scrollMgr()) {
      const QString currentSearchText = m_searchBar->text().trimmed();
      scrollMgr()->applyFilter(currentSearchText);
    }
  });
}

// Renders the synthetic "Home" view: one tile per root collection
// (parentCollectionIndex == -1), no host collection, no DB query, no items.
void NavigationManager::loadRootView() {
  if (!m_collections || !m_currentCollectionIndex || !scrollMgr()) {
    return;
  }

  if (interactionMgr()) {
    interactionMgr()->stopRepeat();
    if (*m_currentCollectionIndex >= 0) {
      interactionMgr()->cancelPendingSelectionRestore();
    }
  }
  persistCurrentSelection();
  performNavigationStackCleanup();
  m_stackManager->clear();
  scrollMgr()->cleanup();

  *m_currentCollectionIndex = -1;
  m_inRootView = true;
  m_isInitialStartupLoad = false;
  m_cachedExpandedContextIndex = -1;

  QList<int> rootIndices;
  for (int i = 0; i < (*m_collections).size(); ++i) {
    if ((*m_collections)[i].parentCollectionIndex == -1) {
      rootIndices.append(i);
    }
  }

  CollectionContext context;
  context.isRootView = true;
  context.currentIndex = -1;
  context.hasSubcollectionOverride = true;
  context.subcollectionOverride = rootIndices;
  context.suppressVirtualFolders = true;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
  }

  m_hasItemsQueryContext = true;
  m_itemsQueryContext = context;
  m_itemsQueryFilter.clear();

  if (m_searchBar) {
    m_searchBar->blockSignals(true);
    m_searchBar->clear();
    m_searchBar->blockSignals(false);
  }

  // Pin the search-mode toggle to AllCollections — the only meaningful
  // scope when no host collection is selected. SearchManager's cycle
  // handles the root-view case.
  if (interactionMgr()) {
    interactionMgr()->initializeSearchModeForCurrentCollection();
  }

  if (m_stackedWidget && m_itemsPage) {
    m_stackedWidget->setCurrentWidget(m_itemsPage);
  }

  if (auto *titleLabel =
          m_itemsPage ? m_itemsPage->findChild<QLabel *>("itemsTitleLabel") : nullptr) {
    QString label;
    if (m_generalSettings) {
      label = m_generalSettings->homeViewLabel.trimmed();
    }
    titleLabel->setText(label.isEmpty() ? tr("Home") : label);
  }
  if (auto *subfolderLabel =
          m_itemsPage ? m_itemsPage->findChild<QLabel *>("subfolderPathLabel") : nullptr) {
    subfolderLabel->setVisible(false);
  }

  // No items, only tiles. setupVirtualScrolling with totalCount=0 routes the
  // ScrollManager through the tile-only render path; the override list in the
  // context populates the visible tiles.
  scrollMgr()->setupVirtualScrolling(0, context);

  if (rootIndices.isEmpty() && m_loadingLabel) {
    m_loadingLabel->showMessage(tr("No collections yet"),
                                tr("Add a collection in Settings to populate the home view."),
                                QStringLiteral("📭"));
  }

  if (detailsPaneMgr()) {
    detailsPaneMgr()->applySidebarStateForCollection(-1);
  }

  if (interactionMgr()) {
    interactionMgr()->setNavigationInProgress(false);
  }
  if (m_refreshTitleCounts) m_refreshTitleCounts();
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
