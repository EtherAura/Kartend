// Title/breadcrumb + item-count completion handler extracted from
// navigationmanager.cpp:
//   - updateItemsPageTitle (~135 LOC)
//   - onItemCountLoaded (~215 LOC)
// Both remain NavigationManager members and access existing class state.
#include "applicationcontext.h"
#include "collection/collectioncontext.h"
#include "collection/hierarchyhelpers.h"
#include "emptystatewidget.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "isessionmanager.h"
#include "isettingsmanager.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationhelpers.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "searchloadingoverlay.h"
#include "selectionrestorecoordinator.h"
#include "timerutils.h"
#include "uiconstants/selection.h"
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPalette>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcNavigationManager)

// Re-define diagnostic logging macro (file-local in main TU).
auto NavigationManager::updateItemsPageTitle(int collectionIndex) -> void {
  if ((!appNotShuttingDown()) || (!m_itemsPage)) {
    return;
  }
  auto *titleLabel = m_itemsPage->findChild<QLabel *>("itemsTitleLabel");
  auto *subfolderLabel = m_itemsPage->findChild<QLabel *>("subfolderPathLabel");

  if (!titleLabel) {
    return;
  }

  if (collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    titleLabel->clear();
    if (subfolderLabel) subfolderLabel->setVisible(false);
    return;
  }

  // Connect linkActivated signal if not already connected
  static bool linkConnected = false;
  if (!linkConnected) {
    QObject::connect(titleLabel, &QLabel::linkActivated, this,
                     &NavigationManager::onBreadcrumbLinkClicked);
    linkConnected = true;
  }

  const CollectionConfig &config = (*m_collections)[collectionIndex];

  // Get tinted color for clickable links
  QPalette pal = titleLabel->palette();
  QColor highlightColor = pal.color(QPalette::Highlight);
  int h, s, l;
  highlightColor.getHsl(&h, &s, &l);
  QColor linkColor = QColor::fromHsl(h, s / 2, 170); // Tinted, slightly saturated
  QString linkColorHex = linkColor.name();

  // Breadcrumb assembly (ancestor "collection:<idx>" links, the "root:"
  // self-link while in a virtual subfolder) is pure HTML composition —
  // extracted to NavigationHelpers so it's unit-testable. The links land in
  // onBreadcrumbLinkClicked (navigationmanagersubcollection.cpp), decoded by
  // NavigationHelpers::parseBreadcrumbLink.
  titleLabel->setText(
      NavigationHelpers::buildTitleBreadcrumbHtml(collectionIndex, *m_collections, linkColorHex));

  // Kartend-w2n0: refresh the items-toolbar warning badge atomically with
  // the title so an active-collection switch can't leave the badge state
  // pointing at the prior collection. Callback may be null in tests.
  if (m_refreshCollectionWarningBadge) {
    m_refreshCollectionWarningBadge();
  }

  // Show subfolder path if we're navigated into a virtual folder
  if (subfolderLabel) {
    const QString &subfolder = config.folderBrowsing.currentSubfolder;
    if (!subfolder.isEmpty()) {
      // Connect subfolder label linkActivated if not already connected
      static bool subfolderLinkConnected = false;
      if (!subfolderLinkConnected) {
        QObject::connect(subfolderLabel, &QLabel::linkActivated, this,
                         &NavigationManager::onBreadcrumbLinkClicked);
        subfolderLabel->setTextFormat(Qt::RichText);
        subfolderLabel->setOpenExternalLinks(false);
        subfolderLinkConnected = true;
      }

      // Subfolder breadcrumb — clickable "subfolder:<path>" links per
      // intermediate segment, plain text for the current one. Pure HTML
      // composition extracted to NavigationHelpers.
      subfolderLabel->setText(
          NavigationHelpers::buildSubfolderBreadcrumbHtml(subfolder, linkColorHex));
      subfolderLabel->setStyleSheet(QString("color: %1;").arg(linkColorHex));
      subfolderLabel->setVisible(true);
    } else {
      subfolderLabel->setVisible(false);
    }
  }
}

void NavigationManager::onItemCountLoaded(int count, int requestToken) {
  qCWarning(lcScanFlow) << "onItemCountLoaded ENTRY: count=" << count << "token=" << requestToken
                        << "expected=" << m_itemCountRequestToken;
  if (requestToken != m_itemCountRequestToken) {
    qCDebug(lcSearchDiag) << QString("onItemCountLoaded: ignoring stale result count=%1 "
                                     "token=%2 expected=%3")
                                 .arg(count)
                                 .arg(requestToken)
                                 .arg(m_itemCountRequestToken);
    qCWarning(lcScanFlow) << "STALE token - ignoring";
    return;
  }

  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  // Use the filter that produced this count. This prevents a mismatch where
  // count is computed with an explicit filter, but subsequent range loads use
  // a different/empty searchBar value (common during debounced search updates).
  const QString searchText = !m_itemsQueryFilter.isEmpty()
                                 ? m_itemsQueryFilter
                                 : ((m_searchBar) ? m_searchBar->text().trimmed() : QString());
  const bool searchActive = !searchText.isEmpty();

  qCDebug(lcSearchDiag) << QString("onItemCountLoaded: count=%1 searchActive=%2 searchText='%3' "
                                   "itemsViewGen(before)=%4 token=%5")
                               .arg(count)
                               .arg(searchActive)
                               .arg(searchText)
                               .arg(m_itemsViewGeneration)
                               .arg(requestToken);

  qCWarning(lcScanFlow) << "onItemCountLoaded: count=" << count
                        << "bgRefresh=" << m_backgroundCountRefreshInProgress
                        << "bgIdx=" << m_backgroundCountRefreshCollectionIndex << "curIdx=" << idx;

  if (m_backgroundCountRefreshInProgress && m_backgroundCountRefreshCollectionIndex == idx &&
      scrollMgr()) {
    const int currentViewItems = scrollMgr()->getTotalItems();
    qCWarning(lcScanFlow) << "bgRefresh path: currentViewItems=" << currentViewItems
                          << "count=" << count;
    // Background scan completed. If the view already has items and count is the
    // same or lower, just update without resetting scroll/selection state.
    // If count INCREASED (new items from scan), do a full rebuild to load them.
    if (NavigationHelpers::shouldSkipRebuildAfterBackgroundRefresh(currentViewItems, count)) {
      m_backgroundCountRefreshInProgress = false;
      m_backgroundCountRefreshCollectionIndex = -1;

      scrollMgr()->updateMediaItemCount(count);
      if (artworkMgr() && artworkMgr()->getTimerCoordinator()) {
        artworkMgr()->getTimerCoordinator()->scheduleViewportUpdate();
      }
      if (m_refreshTitleCounts) {
        m_refreshTitleCounts();
      }
      if (databaseMgr() && m_collections) {
        databaseMgr()->updateCachedCounts((*m_collections));
      }
      return;
    }
    // View is empty OR count increased - clear the refresh flags and fall
    // through to full rebuild so the newly-scanned items are displayed.
    qCWarning(lcScanFlow) << "Count changed or view empty, falling through to "
                             "full rebuild";
    m_backgroundCountRefreshInProgress = false;
    m_backgroundCountRefreshCollectionIndex = -1;
  }

  const bool forceTopForThisLoad = m_forceTopOnNextItemsViewLoad;
  m_forceTopOnNextItemsViewLoad = false;

  // Any full rebuild invalidates in-flight range requests.
  ++m_itemsViewGeneration;
  m_pendingRangeGenerations.clear();

  qCDebug(lcSearchDiag)
      << QString("onItemCountLoaded: itemsViewGen(after)=%1").arg(m_itemsViewGeneration);

  // Clean up existing "no items" widgets
  cleanupExistingNoItemsWidgets();

  CollectionContext context =
      m_hasItemsQueryContext ? m_itemsQueryContext : buildExpandedContextForIndex(idx);

  // Get subcollections for this view (shown as folder tiles)
  QList<int> subcollections;
  if (context.hasSubcollectionOverride) {
    subcollections = context.subcollectionOverride;
  } else {
    subcollections = getSubcollections(idx);
    if (searchActive) {
      // Tile visibility during search is a pure name-contains filter —
      // extracted to NavigationHelpers.
      subcollections =
          NavigationHelpers::filterSubcollectionsByName(subcollections, *m_collections, searchText);
    }
  }

  // Count virtual folders (subdirectories shown as navigable tiles)
  const bool suppressVirtualFolders = context.suppressVirtualFolders || searchActive;
  int virtualFolderCount =
      suppressVirtualFolders ? 0 : CollectionUtils::countVirtualFolders(context.config);

  int totalItems = subcollections.size() + virtualFolderCount + count;

  qCDebug(lcSearchDiag) << QString("onItemCountLoaded: subcollections=%1 virtualFolders=%2 "
                                   "dbCount=%3 totalItems=%4")
                               .arg(subcollections.size())
                               .arg(virtualFolderCount)
                               .arg(count)
                               .arg(totalItems);

  // Check if collection has any content
  if (totalItems == 0) {
    qCDebug(lcSearchDiag) << "onItemCountLoaded: totalItems==0, calling handleEmptyContent";
    // Hide search loading overlay even when no results
    if (scrollMgr()) {
      scrollMgr()->hideSearchLoadingOverlay();
    }
    handleEmptyContent();
    return;
  }

  // Update loading overlay to show item count found
  if (m_loadingOverlay && m_loadingOverlay->isActive()) {
    const QString &collectionName = (*m_collections)[idx].name;
    m_loadingOverlay->setMessage(
        QString("Loading %1 (%2 items)...").arg(collectionName).arg(totalItems));
  }

  if (searchActive) {
    context.hasSubcollectionOverride = true;
    context.subcollectionOverride = subcollections;
    context.suppressVirtualFolders = true;
  }

  // Calculate selection index for initial scroll position
  int rememberedSelIdx = -1;
  if (!searchActive && m_generalSettings && m_generalSettings->input.rememberSelection) {
    rememberedSelIdx = lookupRememberedSelectionIndex(totalItems);
  }

  int selIdx = calculateSelectionIndex(totalItems);

  if (forceTopForThisLoad && !searchActive && rememberedSelIdx < 0) {
    selIdx = 0;
  }

  if (scrollMgr()) {
    // Pre-set scroll position to avoid visual jump
    if (searchActive) {
      scrollMgr()->setInitialScrollIndex(0);
    } else if (selIdx >= 0) {
      scrollMgr()->setInitialScrollIndex(selIdx);
    }
    qCWarning(lcScanFlow) << "Calling setupVirtualScrolling: totalItems=" << totalItems;
    scrollMgr()->setupVirtualScrolling(totalItems, context);

    // Hide search loading overlay once filtered results are ready
    scrollMgr()->hideSearchLoadingOverlay();
  }

  // Resume rendering on the items page
  resumeItemsPageRendering();

  if (m_loadingLabel) {
    m_loadingLabel->hide();
  }
  if (m_stackedWidget && m_itemsPage) {
    m_stackedWidget->setCurrentWidget(m_itemsPage);
  }

  // Update artwork for visible items
  if (artworkMgr()) {
    artworkMgr()->updateViewportArtwork();
  }
  if (artworkMgr() && artworkMgr()->getTimerCoordinator()) {
    artworkMgr()->getTimerCoordinator()->scheduleViewportUpdate();
  }

  // Restore selection if needed
  bool pendingRestore = state() ? state()->selectionRestore().restorePending : false;
  // Also skip if there's a pending path-based restore (from sort change)
  bool pendingPathRestore = scrollMgr() && scrollMgr()->hasPendingSelectionRestoreByPath();
  if (selIdx >= 0 && interactionMgr() && !pendingRestore && !pendingPathRestore) {
    scheduleSelectionRestore(selIdx, UIConstants::Selection::RESTORE_MAX_DELAY_MS);
  }

  schedulePostLoadOperations();
}
