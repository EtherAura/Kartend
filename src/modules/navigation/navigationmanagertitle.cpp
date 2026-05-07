// Title/breadcrumb + item-count completion handler extracted from
// navigationmanager.cpp:
//   - updateItemsPageTitle (~135 LOC)
//   - onItemCountLoaded (~215 LOC)
// Both remain NavigationManager members and access existing class state.
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "emptystatewidget.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "searchloadingoverlay.h"
#include "selectionrestoremanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "timerutils.h"
#include "uiconstants.h"
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
  if ((!parent()) || (!m_itemsPage)) {
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

  // Build breadcrumb with clickable parent links
  QString titleHtml;

  // Get tinted color for clickable links
  QPalette pal = titleLabel->palette();
  QColor highlightColor = pal.color(QPalette::Highlight);
  int h, s, l;
  highlightColor.getHsl(&h, &s, &l);
  QColor linkColor = QColor::fromHsl(h, s / 2, 170); // Tinted, slightly saturated
  QString linkColorHex = linkColor.name();

  // Check if we're in a subfolder - if so, make the collection name clickable
  bool inSubfolder = !config.currentSubfolder.isEmpty();

  if (config.isSubcollection) {
    const QList<int> ancestors = CollectionUtils::ancestorIndexChain(config, *m_collections);
    if (!ancestors.isEmpty()) {
      // Build clickable breadcrumb from root-most ancestor down to direct
      // parent. Each ancestor segment navigates back to that collection via
      // the `collection:<index>` link scheme handled in
      // onBreadcrumbLinkClicked (navigationmanagersubcollection.cpp:143).
      const QString linkTemplate = QStringLiteral("<a href=\"collection:%1\" style=\"color:%2; "
                                                  "text-decoration:none;\">%3</a>");
      QStringList segments;
      segments.reserve(ancestors.size() + 1);
      for (int idx : ancestors) {
        const QString name = (*m_collections)[idx].name.toHtmlEscaped();
        segments << linkTemplate.arg(QString::number(idx), linkColorHex, name);
      }
      if (inSubfolder) {
        // Current collection is clickable (returns to its root via `root:`)
        // only when we've navigated into a virtual subfolder below it.
        segments << QString("<a href=\"root:\" style=\"color:%1; "
                            "text-decoration:none;\">%2</a>")
                        .arg(linkColorHex, config.name.toHtmlEscaped());
      } else {
        segments << config.name.toHtmlEscaped();
      }
      titleHtml = segments.join(QStringLiteral(" › "));
    } else {
      titleHtml = config.name.toHtmlEscaped();
    }
  } else {
    // Root collection - make clickable only if in a subfolder
    if (inSubfolder) {
      titleHtml = QString("<a href=\"root:\" style=\"color:%1; "
                          "text-decoration:none;\">%2</a>")
                      .arg(linkColorHex)
                      .arg(config.name.toHtmlEscaped());
    } else {
      titleHtml = config.name.toHtmlEscaped();
    }
  }

  titleLabel->setText(titleHtml);

  // Show subfolder path if we're navigated into a virtual folder
  if (subfolderLabel) {
    const QString &subfolder = config.currentSubfolder;
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

      // Build subfolder breadcrumb - show path segments as clickable links
      QStringList pathParts = subfolder.split('/', Qt::SkipEmptyParts);
      QString subfolderHtml;
      QString accumulatedPath;

      for (int i = 0; i < pathParts.size(); ++i) {
        if (!accumulatedPath.isEmpty()) {
          accumulatedPath += '/';
        }
        accumulatedPath += pathParts[i];

        if (i > 0) {
          subfolderHtml += " › ";
        }

        if (i < pathParts.size() - 1) {
          // Intermediate folder - clickable to navigate to that level
          subfolderHtml += QString("<a href=\"subfolder:%1\" style=\"color:%2; "
                                   "text-decoration:none;\">%3</a>")
                               .arg(accumulatedPath.toHtmlEscaped())
                               .arg(linkColorHex)
                               .arg(pathParts[i].toHtmlEscaped());
        } else {
          // Current folder - not clickable, just styled
          subfolderHtml += pathParts[i].toHtmlEscaped();
        }
      }

      subfolderLabel->setText(subfolderHtml);
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
      m_scrollManager) {
    const int currentViewItems = m_scrollManager->getTotalItems();
    qCWarning(lcScanFlow) << "bgRefresh path: currentViewItems=" << currentViewItems
                          << "count=" << count;
    // Background scan completed. If the view already has items and count is the
    // same or lower, just update without resetting scroll/selection state.
    // If count INCREASED (new items from scan), do a full rebuild to load them.
    if (currentViewItems > 0 && count <= currentViewItems) {
      m_backgroundCountRefreshInProgress = false;
      m_backgroundCountRefreshCollectionIndex = -1;

      m_scrollManager->updateMediaItemCount(count);
      if (m_artworkManager && m_artworkManager->getTimerCoordinator()) {
        m_artworkManager->getTimerCoordinator()->scheduleViewportUpdate();
      }
      if (m_refreshTitleCounts) {
        m_refreshTitleCounts();
      }
      if (m_databaseManager && m_collections) {
        m_databaseManager->updateCachedCounts((*m_collections));
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
      QList<int> filtered;
      filtered.reserve(subcollections.size());
      for (int subIdx : subcollections) {
        if (subIdx < 0 || subIdx >= (*m_collections).size()) {
          continue;
        }
        const QString &name = (*m_collections)[subIdx].name;
        if (name.contains(searchText, Qt::CaseInsensitive)) {
          filtered.append(subIdx);
        }
      }
      subcollections = filtered;
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
    if (m_scrollManager) {
      m_scrollManager->hideSearchLoadingOverlay();
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
  if (!searchActive && m_generalSettings && m_generalSettings->rememberSelection) {
    rememberedSelIdx = lookupRememberedSelectionIndex(totalItems);
  }

  int selIdx = calculateSelectionIndex(totalItems);

  if (forceTopForThisLoad && !searchActive && rememberedSelIdx < 0) {
    selIdx = 0;
  }

  if (m_scrollManager) {
    // Pre-set scroll position to avoid visual jump
    if (searchActive) {
      m_scrollManager->setInitialScrollIndex(0);
    } else if (selIdx >= 0) {
      m_scrollManager->setInitialScrollIndex(selIdx);
    }
    qCWarning(lcScanFlow) << "Calling setupVirtualScrolling: totalItems=" << totalItems;
    m_scrollManager->setupVirtualScrolling(totalItems, context);

    // Hide search loading overlay once filtered results are ready
    m_scrollManager->hideSearchLoadingOverlay();
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
  if (m_artworkManager) {
    m_artworkManager->updateViewportArtwork();
  }
  if (m_artworkManager && m_artworkManager->getTimerCoordinator()) {
    m_artworkManager->getTimerCoordinator()->scheduleViewportUpdate();
  }

  // Restore selection if needed
  bool pendingRestore = m_state ? m_state->selectionRestore().restorePending : false;
  // Also skip if there's a pending path-based restore (from sort change)
  bool pendingPathRestore = m_scrollManager && m_scrollManager->hasPendingSelectionRestoreByPath();
  if (selIdx >= 0 && m_interactionManager && !pendingRestore && !pendingPathRestore) {
    scheduleSelectionRestore(selIdx, UIConstants::Selection::RESTORE_STEPS,
                             UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                             UIConstants::Selection::RESTORE_MAX_DELAY_MS);
  }

  schedulePostLoadOperations();
}
