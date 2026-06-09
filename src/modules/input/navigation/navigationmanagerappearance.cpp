// Sibling TU: appearance/styling application for NavigationManager.
#include "artworkutils.h"
#include "collection/hierarchyhelpers.h"
#include "collection/validationhelpers.h"
#include "collectionbackgroundcontroller.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "idetailspane.h"
#include "idetailspanemanager.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "isessionmanager.h"
#include "isettingsmanager.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "titlefilter.h"
#include "uiconstants/selection.h"
#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QtGlobal>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcNavigationManager().isDebugEnabled()) {                                                  \
      qCDebug(lcNavigationManager) << msg;                                                         \
    }                                                                                              \
  } while (0)

auto NavigationManager::applyCollectionSettingsOnly(int collectionIndex) -> void {
  if (collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  // compare and apply the *effective* grid width — the alt
  // (gridWidthSidebarHidden) is in play when the sidebar is hidden in Expand
  // mode, and the navigation-time live-apply has to respect that or it'll
  // briefly flash to the primary value before the sidebar restores the alt.
  const bool sidebarShrinkingActive = scrollMgr()->sidebarShrinkingActive();
  const int effectiveTargetWidth =
      CollectionUtils::effectiveGridWidth(collection, sidebarShrinkingActive);
  if (effectiveTargetWidth != scrollMgr()->getCurrentGridWidth()) {
    scrollMgr()->updateGridWidth(effectiveTargetWidth);
  }
  // live-apply the per-collection horizontal items-per-column
  // setting. updateHorizontalGridHeight no-ops when the active view isn't
  // Horizontal, so this is safe to call unconditionally.
  scrollMgr()->updateHorizontalGridHeight(
      CollectionUtils::effectiveHorizontalGridHeight(collection, sidebarShrinkingActive));

  SettingsUtils::applyHorizontalScrollbarSetting(m_itemScrollArea, collectionIndex,
                                                 (*m_collections));
  SettingsUtils::applyVerticalScrollbarSetting(m_itemScrollArea, collectionIndex, (*m_collections));

  applyBackgroundForCollection(collectionIndex);
  applyPrimaryColorForCollection(collectionIndex);

  if (detailsPaneMgr()) {
    detailsPaneMgr()->applySidebarStateForCollection(collectionIndex);
  }
}

void NavigationManager::applyBackgroundForCollection(int collectionIndex) {
  if (m_backgroundController) {
    m_backgroundController->applyBackgroundForCollection(collectionIndex);
  }
}

void NavigationManager::applyPrimaryColorForCollection(int collectionIndex) {
  if (m_backgroundController) {
    m_backgroundController->applyPrimaryColorForCollection(collectionIndex);
  }
}

void NavigationManager::onCollectionBackgroundChanged(int collectionIndex,
                                                      const CollectionBackground &background) {
  Q_UNUSED(background);
  if (!m_currentCollectionIndex || collectionIndex != *m_currentCollectionIndex) {
    return;
  }
  applyBackgroundForCollection(collectionIndex);
  applyPrimaryColorForCollection(collectionIndex);
}

void NavigationManager::onCollectionFilterPreferencesChanged(
    int collectionIndex, const CollectionFilterPreferences &filter) {
  Q_UNUSED(collectionIndex);
  Q_UNUSED(filter);
  // TitleFilter is a global registry keyed by collection index, so any
  // diff needs a full rebuild from the current collection list to keep
  // index → patterns mapping intact. SettingsManager::saveCollections
  // already calls rebuildFromCollections inline, so this connect mostly
  // covers alternate emission paths (none today, but the connect graph
  // stays complete for future ones).
  if (m_collections) {
    TitleFilter::rebuildFromCollections(*m_collections);
  }
}

void NavigationManager::onFolderBrowsingOptionsChanged(
    int collectionIndex, const FolderBrowsingOptions &folderBrowsing) {
  Q_UNUSED(folderBrowsing);
  // includeContentSubfolders feeds the ScanService scan signature; a
  // changed value requires a rescan to repopulate the DB with the new
  // subfolder set. QueryManager's fetch paths re-read this field per
  // query, so the items list already reflects the new value on the next
  // navigation — only a rescan rebuilds the per-collection item count.
  // Force-rescan integration is filed as a follow-up of Kartend-2hzy.
  qCDebug(lcNavigationManager).nospace()
      << "FolderBrowsingOptions changed for collection " << collectionIndex
      << " — DB rescan recommended to pick up subfolder changes (no auto-rescan yet).";
}

void NavigationManager::restoreSelectionForCurrentCollection() {
  if ((!appNotShuttingDown()) || QApplication::closingDown()) {
    return;
  }
  if ((!scrollMgr()) || (!interactionMgr())) {
    return;
  }
  int coll = (*m_currentCollectionIndex);
  if (coll < 0 || coll >= (*m_collections).size()) {
    return;
  }
  int total = scrollMgr()->getTotalItems();
  if (total <= 0) {
    return;
  }
  int desired = -1;
  if (settingsMgr()) {
    desired = settingsMgr()->getLastSelectedItem(coll);
  }
  if (desired < 0 || desired >= total) {
    desired = 0;
  }
  if (interactionMgr()->currentSelectedIndex() == desired) {
    return;
  }

  scheduleSelectionRestore(desired, UIConstants::Selection::RESTORE_MAX_DELAY_MS);
}
void NavigationManager::persistCurrentSelection() {
  static const bool diagEnabled = qEnvironmentVariableIntValue("KARTEND_SEARCH_DIAG");
  if (diagEnabled) {
    qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: ENTRY";
  }
  if ((!interactionMgr()) || (!settingsMgr()) || (!m_currentCollectionIndex) || (!m_collections)) {
    if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                               "missing deps"
                            << "interaction=" << static_cast<bool>(interactionMgr())
                            << "settings=" << static_cast<bool>(settingsMgr())
                            << "collIndex=" << static_cast<bool>(m_currentCollectionIndex)
                            << "collections=" << static_cast<bool>(m_collections);
    }
    return;
  }
  int coll = *m_currentCollectionIndex;
  if (!CollectionUtils::isValidIndex(coll, m_collections)) {
    if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                               "invalid collection index"
                            << coll;
    }
    return;
  }
  int sel = interactionMgr()->currentSelectedIndex();
  if (sel < 0) {
    if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                               "no selection, caching viewport anyway";
    }
    // Still try to cache viewport even without selection for fast startup
  } else {
    settingsMgr()->setLastSelectedItem(coll, sel);
  }

  // Also cache the current viewport for instant startup
  if (scrollMgr() && sessionMgr() && m_generalSettings &&
      m_generalSettings->input.rememberSelection) {
    int startIndex = 0;
    int totalItems = 0;
    QStringList filePaths;
    QHash<QString, QString> fileNames;
    QHash<QString, QString> artworkPaths;

    if (scrollMgr()->getCurrentViewportForCache(startIndex, totalItems, filePaths, fileNames,
                                                artworkPaths)) {
      const CollectionConfig &cfg = (*m_collections)[coll];
      const QString collectionKey = CollectionUtils::hierarchicalNameFor(cfg, *m_collections);
      if (diagEnabled) {
        qCDebug(lcSearchDiag) << "[NavigationManager] "
                                 "persistCurrentSelection: caching viewport for"
                              << collectionKey << "startIndex=" << startIndex
                              << "totalItems=" << totalItems << "filePaths=" << filePaths.size();
      }
      sessionMgr()->setCachedViewport(collectionKey, startIndex, totalItems, filePaths, fileNames,
                                      artworkPaths);
    } else if (diagEnabled) {
      qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                               "getCurrentViewportForCache returned false";
    }
  } else if (diagEnabled) {
    qCDebug(lcSearchDiag) << "[NavigationManager] persistCurrentSelection: "
                             "cannot cache viewport"
                          << "scrollManager=" << static_cast<bool>(scrollMgr())
                          << "sessionManager=" << static_cast<bool>(sessionMgr())
                          << "generalSettings=" << static_cast<bool>(m_generalSettings)
                          << "rememberSelection="
                          << (m_generalSettings ? m_generalSettings->input.rememberSelection
                                                : false);
  }
}

void NavigationManager::applyUiPoliciesForCollection(int collectionIndex) {
  if (detailsPaneMgr()) {
    detailsPaneMgr()->applySidebarStateForCollection(collectionIndex);
  }
  if (settingsMgr() && m_itemScrollArea && m_collections) {
    SettingsUtils::applyHorizontalScrollbarSetting(m_itemScrollArea, collectionIndex,
                                                   *m_collections);
    SettingsUtils::applyVerticalScrollbarSetting(m_itemScrollArea, collectionIndex, *m_collections);
  }
}
