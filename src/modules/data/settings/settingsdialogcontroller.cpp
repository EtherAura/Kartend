// Settings dialog orchestration extracted from settingsmanager.cpp:
//   - openSettingsDialog
//   - handleReloadRequired
//   - handleLayoutChanges
// Plus the file-local anonymous-namespace helpers used by these methods
// (compareNonReloadFields, compareReloadFields, updateViewingFlags,
// updateWindowTitle, applyScrollbarSettings, refreshSidebar,
// handleScrollBranch, detectChanges).
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants.h"
#include <QDialog>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QWidget>

namespace {
// Compares fields that do not require a reload
auto compareNonReloadFields(const CollectionConfig &configA, const CollectionConfig &configB,
                            bool &hasChanges) -> void {
  if (configA.name != configB.name || configA.launcherPath != configB.launcherPath ||
      configA.corePath != configB.corePath ||
      configA.launchParameters != configB.launchParameters ||
      configA.extractArchives != configB.extractArchives ||
      configA.extractedExtension != configB.extractedExtension ||
      configA.expandMode != configB.expandMode ||
      configA.parentCollectionIndex != configB.parentCollectionIndex ||
      configA.isSubcollection != configB.isSubcollection ||
      // Without sidebarPosition here, an orientation-only edit in the settings
      // dialog returned hasChanges=false and silently dropped the new value —
      // the user had to toggle the pane (or change another field) before the
      // chosen edge took effect. applySidebarStateForCollection downstream
      // re-runs the layout swap so the pane reparents into the right Qt
      // layout immediately on save.
      configA.sidebarPosition != configB.sidebarPosition) {
    hasChanges = true;
  }
}

// Compares fields that trigger reload
void compareReloadFields(const CollectionConfig &configA, const CollectionConfig &configB,
                         bool &hasChanges, bool &needsReload) {
  if (configA.mediaDirectory != configB.mediaDirectory ||
      configA.artworkDirectory != configB.artworkDirectory ||
      configA.includeContentSubfolders != configB.includeContentSubfolders ||
      configA.includeArtworkSubfolders != configB.includeArtworkSubfolders ||
      configA.showAllSubcollectionItems != configB.showAllSubcollectionItems ||
      configA.showAllSubfolderItems != configB.showAllSubfolderItems ||
      configA.hideSubfolderTitles != configB.hideSubfolderTitles ||
      configA.showHiddenFolders != configB.showHiddenFolders ||
      configA.extensions != configB.extensions) {
    hasChanges = true;
    needsReload = true;
  }
}

// Updates flags for the viewing collection only
void updateViewingFlags(const CollectionConfig &configA, const CollectionConfig &configB,
                        bool &hasChanges, bool &gridWidthChanged, bool &alignmentChanged,
                        bool &spacingChanged, bool &scrollbarChanged, bool &sidebarModeChanged,
                        bool &titleChanged, bool &fontSizeChanged, bool &hideTitlesChanged,
                        bool &appearanceChanged) {
  if (configA.gridWidth != configB.gridWidth) {
    hasChanges = true;
    gridWidthChanged = true;
  }
  if (configA.horizontalAlignment != configB.horizontalAlignment) {
    hasChanges = true;
    alignmentChanged = true;
  }
  if (configA.horizontalSpacing != configB.horizontalSpacing ||
      configA.verticalSpacing != configB.verticalSpacing) {
    hasChanges = true;
    spacingChanged = true;
  }
  if (configA.itemWidth != configB.itemWidth || configA.itemHeight != configB.itemHeight) {
    hasChanges = true;
    spacingChanged = true;
  }
  if (configA.fontSize != configB.fontSize || configA.listFontSize != configB.listFontSize ||
      configA.cornerRadius != configB.cornerRadius) {
    hasChanges = true;
    fontSizeChanged = true;
  }
  if (configA.hideTitles != configB.hideTitles ||
      configA.hideSubcollectionTitles != configB.hideSubcollectionTitles) {
    hasChanges = true;
    hideTitlesChanged = true;
  }
  if (configA.hideHorizontalScrollbar != configB.hideHorizontalScrollbar ||
      configA.hideVerticalScrollbar != configB.hideVerticalScrollbar) {
    hasChanges = true;
    scrollbarChanged = true;
  }
  if (configA.sidebarMode != configB.sidebarMode) {
    hasChanges = true;
    sidebarModeChanged = true;
  }
  if (configA.viewType != configB.viewType) {
    hasChanges = true;
    // View type changes require full layout update like spacing changes
    spacingChanged = true;
  }
  if (configA.hideMissingArtwork != configB.hideMissingArtwork) {
    hasChanges = true;
    // toggling the hide-missing-artwork predicate changes the
    // visible item count and the layout/scrollbars; ride the spacingChanged
    // path so handleScrollBranch calls primeLayoutFor + recreateLayout, which
    // re-runs the FilterManager baseline inside setupVirtualScrolling.
    spacingChanged = true;
  }
  if (configA.name != configB.name) {
    titleChanged = true;
  }
  // Appearance changes (colors)
  if (configA.primaryColor != configB.primaryColor || configA.tileColor != configB.tileColor ||
      configA.selectionColor != configB.selectionColor ||
      configA.backgroundColor != configB.backgroundColor ||
      configA.backgroundImage != configB.backgroundImage ||
      configA.backgroundVideo != configB.backgroundVideo ||
      configA.backgroundType != configB.backgroundType ||
      configA.listRowColor != configB.listRowColor ||
      configA.listAltRowColor != configB.listAltRowColor ||
      configA.listRowHeight != configB.listRowHeight ||
      configA.headerLogoImage != configB.headerLogoImage ||
      configA.headerLogoPosition != configB.headerLogoPosition ||
      configA.vignetteEnabled != configB.vignetteEnabled ||
      configA.vignetteIntensity != configB.vignetteIntensity ||
      configA.wallpaperParallax != configB.wallpaperParallax ||
      configA.parallaxStrength != configB.parallaxStrength ||
      configA.toolbarBackdropBlur != configB.toolbarBackdropBlur ||
      configA.backdropBlurRadius != configB.backdropBlurRadius) {
    hasChanges = true;
    appearanceChanged = true;
  }
}

// Title update logic separated from handleLayoutChanges
void updateWindowTitle(QWidget *parent, int viewingIndex,
                       const QList<CollectionConfig> &collections) {
  if (auto *mw = qobject_cast<MainWindow *>(parent)) {
    mw->updateWindowTitleForCollection(viewingIndex);
  }
  auto *titleLabel = parent->findChild<QLabel *>("itemsTitleLabel");
  if (titleLabel) {
    // Show full ancestor chain so the post-save refresh matches
    // the breadcrumb rendered by NavigationManager::updateItemsPageTitle.
    // This path is a plain-text fallback (no clickable links); the real
    // breadcrumb is rebuilt by the navigation code shortly after.
    const CollectionConfig &viewing = collections[viewingIndex];
    QStringList segments;
    for (int idx : CollectionUtils::ancestorIndexChain(viewing, collections)) {
      segments << collections[idx].name;
    }
    segments << viewing.name;
    titleLabel->setText(segments.join(QStringLiteral(" › ")));
  }
}

// Applies scrollbar settings for the viewing collection
void applyScrollbarSettings(QWidget *parent, int viewingIndex,
                            const QList<CollectionConfig> &collections) {
  auto *scrollArea = parent->findChild<QScrollArea *>("itemScrollArea");
  if (scrollArea) {
    SettingsUtils::applyHorizontalScrollbarSetting(scrollArea, viewingIndex, collections);
    SettingsUtils::applyVerticalScrollbarSetting(scrollArea, viewingIndex, collections);
  }
}

// Updates sidebar layout when mode changes
void refreshSidebar(DetailsPaneManager *detailsPaneManager,
                    const QList<CollectionConfig> & /*collections*/, int currentCollectionIndex) {
  if (detailsPaneManager) {
    detailsPaneManager->updateSidebarLayout(currentCollectionIndex);
  }
}

// Handles scroll manager branching
void handleScrollBranch(ScrollManager *scrollManager, ArtworkManager *artworkManager,
                        const QList<CollectionConfig> &collections, int viewingIndex,
                        bool spacingChanged, bool sidebarModeChanged, bool gridWidthChanged,
                        bool alignmentChanged, bool fontSizeChanged, bool hideTitlesChanged) {
  auto scheduleGridWidthRefresh = [](ScrollManager *scrollManager, ArtworkManager *artworkManager,
                                     int viewingIndex,
                                     const QList<CollectionConfig> *collectionsPtr) {
    if (!scrollManager || !collectionsPtr) {
      return;
    }

    // Delay layout recalculation to allow grid width change to propagate.
    TimerUtils::singleShotGuarded(UIConstants::Timing::LONG_DELAY_MS, scrollManager, [=]() {
      scrollManager->preCalculateLayout();
      scrollManager->forceVirtualViewUpdate();
    });

    // Wait for the pre-calculated layout to settle before updating the
    // virtual view, refreshing artwork, and re-centering.
    TimerUtils::singleShotGuarded(
        UIConstants::Timing::LONG_DELAY_MS + UIConstants::Timing::MEDIUM_DELAY_MS, scrollManager,
        [=]() {
          scrollManager->updateVirtualView();
          if (artworkManager) {
            artworkManager->updateViewportArtwork();
          }
          scrollManager->centerHorizontalScrollbar(viewingIndex, *collectionsPtr);
        });
  };

  if (!scrollManager) {
    return;
  }
  if (spacingChanged || sidebarModeChanged || fontSizeChanged || hideTitlesChanged) {
    scrollManager->primeLayoutFor(collections[viewingIndex]);
    scrollManager->recreateLayout();
    return;
  }
  if (gridWidthChanged) {
    scrollManager->updateGridWidth(collections[viewingIndex].gridWidth);
    scheduleGridWidthRefresh(scrollManager, artworkManager, viewingIndex, &collections);
    return;
  }
  if (alignmentChanged) {
    scrollManager->recenterVirtualContainer();
  } else {
    scrollManager->handleLayoutChange();
  }
}
auto detectChanges(const QList<CollectionConfig> &newCollections,
                   const QList<CollectionConfig> &originalCollections, int viewingCollectionIndex,
                   bool &needsReload, bool &gridWidthChangedForView, bool &alignmentChangedForView,
                   bool &spacingChangedForView, bool &scrollbarChangedForView,
                   bool &sidebarModeChangedForView, bool &titleChangedForView,
                   bool &fontSizeChangedForView, bool &hideTitlesChangedForView,
                   bool &appearanceChangedForView) -> bool {
  bool hasChanges = false;
  if (newCollections.size() != originalCollections.size()) {
    hasChanges = true;
    needsReload = true;
  }

  for (int i = 0; i < newCollections.size(); ++i) {
    if (i >= originalCollections.size()) {
      hasChanges = true;
      needsReload = true;
      continue;
    }
    const CollectionConfig &newConfig = newCollections[i];
    const CollectionConfig &oldConfig = originalCollections[i];
    compareNonReloadFields(newConfig, oldConfig, hasChanges);
    compareReloadFields(newConfig, oldConfig, hasChanges, needsReload);
    if (i == viewingCollectionIndex) {
      updateViewingFlags(newConfig, oldConfig, hasChanges, gridWidthChangedForView,
                         alignmentChangedForView, spacingChangedForView, scrollbarChangedForView,
                         sidebarModeChangedForView, titleChangedForView, fontSizeChangedForView,
                         hideTitlesChangedForView, appearanceChangedForView);
    }
  }
  return hasChanges;
}

} // namespace

void SettingsManager::openSettingsDialog(const SettingsDialogContext &context) {
  if (!context.collections || !context.currentCollectionIndex) {
    return;
  }

  QList<CollectionConfig> &collections = *context.collections;
  int &currentCollectionIndex = *context.currentCollectionIndex;
  QWidget *parent = context.parent;
  DetailsPaneManager *detailsPaneManager = context.detailsPaneManager;
  ScrollManager *scrollManager = context.scrollManager;
  NavigationManager *navigationManager = context.navigationManager;
  IDatabaseManager *databaseManager = context.databaseManager;

  int viewingCollectionIndex = currentCollectionIndex;
  QList<CollectionConfig> originalCollections = collections;

  SettingsDialog dlg(parent, collections, viewingCollectionIndex);

  connect(&dlg, &SettingsDialog::collectionSaved, this,
          [this, &collections](const QList<CollectionConfig> &savedCollections) {
            collections = savedCollections;
            saveCollections(collections);
          });

  // Connect rescan signal to trigger database rescan after dialog closes
  connect(&dlg, &SettingsDialog::rescanRequired, this, [navigationManager](int collectionIndex) {
    if (navigationManager) {
      // Defer rescan to allow dialog to fully close first
      QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS,
                         [navigationManager, collectionIndex]() {
                           navigationManager->forceRescanCollection(collectionIndex);
                         });
    }
  });

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  QList<CollectionConfig> newCollections = dlg.getCollections();

  // Don't switch viewingCollectionIndex - user should stay on the collection
  // they were viewing when they opened the dialog, even if they added or
  // selected a different collection in the dialog.

  bool hasChanges = false;
  bool needsReload = false;
  bool gridWidthChangedForView = false;
  bool alignmentChangedForView = false;
  bool spacingChangedForView = false;
  bool scrollbarChangedForView = false;
  bool sidebarModeChangedForView = false;
  bool titleChangedForView = false;
  bool fontSizeChangedForView = false;
  bool hideTitlesChangedForView = false;
  bool appearanceChangedForView = false;

  hasChanges =
      detectChanges(newCollections, originalCollections, viewingCollectionIndex, needsReload,
                    gridWidthChangedForView, alignmentChangedForView, spacingChangedForView,
                    scrollbarChangedForView, sidebarModeChangedForView, titleChangedForView,
                    fontSizeChangedForView, hideTitlesChangedForView, appearanceChangedForView);

  if (!hasChanges) {
    return;
  }

  ArtworkManager *art = m_ctx ? m_ctx->artworkManager() : nullptr;
  CacheManager *cache = m_ctx ? m_ctx->cacheManager() : nullptr;
  if (art && art->getTimerCoordinator()) {
    art->getTimerCoordinator()->stopAllTimers();
  }

  // Reconcile renamed collections in the database BEFORE the list is
  // persisted. A collection's uuid is hash(name, mediaDirectory), so a
  // rename strands its items + play history under the old uuid — which
  // is why the Statistics "total items" (a flat COUNT(*)) drifts above
  // the live per-collection totals. Match each renamed collection to
  // its pre-dialog self by media directory (unchanged across a rename)
  // and migrate its rows to the new uuid so the history survives.
  if (databaseManager) {
    for (const CollectionConfig &newC : newCollections) {
      if (newC.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      for (const CollectionConfig &oldC : originalCollections) {
        if (oldC.mediaDirectory == newC.mediaDirectory && oldC.name != newC.name) {
          databaseManager->migrateCollectionUuid(
              CollectionUtils::computeCollectionUuid(oldC.name, oldC.mediaDirectory),
              CollectionUtils::computeCollectionUuid(newC.name, newC.mediaDirectory));
          break;
        }
      }
    }
  }

  collections = newCollections;
  // saveCollections emits collectionsModified itself, so
  // an explicit emit here would double-fire and run rebuildHierarchyCache
  // twice for no benefit. Removed.
  saveCollections(collections);

  // Drop items/collections rows that no live collection owns — orphans
  // from this session's renames/removals (and any left by older
  // builds). Startup runs the same purge; doing it here too keeps the
  // counts honest without waiting for a restart.
  if (databaseManager) {
    databaseManager->purgeOrphanCollectionData(newCollections);
  }

  // detect collections that were freshly added during this
  // dialog session (UUID present in newCollections but not in the snapshot
  // captured at dialog open time) and stage a confirmation message box to
  // fire once their first scan completes. We only stage collections that
  // actually have a mediaDirectory — an empty-path stub won't trigger a
  // scan so there's nothing meaningful to confirm.
  {
    QSet<QString> previousUuids;
    previousUuids.reserve(originalCollections.size());
    for (const CollectionConfig &c : originalCollections) {
      if (!c.mediaDirectory.trimmed().isEmpty()) {
        previousUuids.insert(CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory));
      }
    }
    bool stagedAny = false;
    for (const CollectionConfig &c : newCollections) {
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      const QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
      if (previousUuids.contains(uuid)) {
        continue;
      }
      m_pendingAddSummaries.insert(uuid, c.name);
      stagedAny = true;
    }
    if (stagedAny) {
      m_pendingAddSummaryParent = parent;
      // Connect once (UniqueConnection) to the database manager's forwarded
      // signal so the user sees a single message box per newly added
      // collection even across repeated openSettingsDialog invocations.
      if (databaseManager) {
        connect(databaseManager, &DatabaseManager::collectionScanSummary, this,
                &SettingsManager::onCollectionScanSummary, Qt::UniqueConnection);
      }
    }
  }
  auto normalizeCollectionIndex = [](const QList<CollectionConfig> &list, int desiredIndex) -> int {
    if (list.isEmpty()) {
      return -1;
    }
    if (desiredIndex < 0) {
      return 0;
    }
    if (desiredIndex >= list.size()) {
      return list.size() - 1;
    }
    return desiredIndex;
  };

  int resolvedCollectionIndex = normalizeCollectionIndex(collections, viewingCollectionIndex);
  currentCollectionIndex = resolvedCollectionIndex;
  viewingCollectionIndex = resolvedCollectionIndex;

  if (detailsPaneManager) {
    if (resolvedCollectionIndex >= 0) {
      detailsPaneManager->applySidebarStateForCollection(resolvedCollectionIndex);
    }
  }

  // Always apply appearance settings (colors) after any changes
  if (navigationManager && resolvedCollectionIndex >= 0) {
    navigationManager->applyBackgroundForCollection(resolvedCollectionIndex);
    navigationManager->applyPrimaryColorForCollection(resolvedCollectionIndex);
  }

  if (needsReload) {
    handleReloadRequired(collections, newCollections, originalCollections, viewingCollectionIndex,
                         detailsPaneManager, scrollManager, navigationManager, art, cache,
                         currentCollectionIndex);
  } else {
    handleLayoutChanges(parent, collections, viewingCollectionIndex, titleChangedForView,
                        scrollbarChangedForView, sidebarModeChangedForView, gridWidthChangedForView,
                        spacingChangedForView, alignmentChangedForView, fontSizeChangedForView,
                        hideTitlesChangedForView, detailsPaneManager, scrollManager, art,
                        currentCollectionIndex);

    // If only appearance changed, still refresh widgets to show new colors
    if (appearanceChangedForView && scrollManager) {
      scrollManager->recreateLayout();
    }
  }
}

auto SettingsManager::handleReloadRequired(
    const QList<CollectionConfig> &collections, const QList<CollectionConfig> &newCollections,
    const QList<CollectionConfig> &originalCollections, int viewingCollectionIndex,
    DetailsPaneManager *detailsPaneManager, ScrollManager *scrollManager,
    NavigationManager *navigationManager, ArtworkManager *artworkManager,
    CacheManager *cacheManager, int currentCollectionIndex) -> void {
  Q_UNUSED(detailsPaneManager)
  Q_UNUSED(currentCollectionIndex)
  if (artworkManager) {
    artworkManager->cancelAllArtworkLoading();
  }
  if (viewingCollectionIndex >= 0 && viewingCollectionIndex < collections.size()) {

    // Check if this is a newly-added collection (not in originalCollections)
    bool isNewCollection = (viewingCollectionIndex >= originalCollections.size());

    bool mediaDirectoryChanged = isNewCollection;
    bool extensionsChanged = isNewCollection;

    if (!isNewCollection) {
      const QString &mediaDirectory = newCollections[viewingCollectionIndex].mediaDirectory;
      const QString &originalMediaDirectory =
          originalCollections[viewingCollectionIndex].mediaDirectory;
      mediaDirectoryChanged = (mediaDirectory != originalMediaDirectory);
      extensionsChanged = (newCollections[viewingCollectionIndex].extensions !=
                           originalCollections[viewingCollectionIndex].extensions);
    }

    if (mediaDirectoryChanged || extensionsChanged) {
      if (cacheManager) {
        cacheManager->clearCollectionCache(viewingCollectionIndex);
      }
      if (artworkManager) {
        artworkManager->clearLoadedArtworkState();
        artworkManager->clearWidgetReferences();
      }
    }

    if (scrollManager) {
      scrollManager->cleanup();
    }

    // Delay collection reload to allow cleanup operations to complete -
    // prevents race conditions with ongoing artwork loads or animations
    QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS,
                       [navigationManager, viewingCollectionIndex]() {
                         if (navigationManager) {
                           navigationManager->safeReloadCollection(viewingCollectionIndex);
                         }
                       });
  }
}

auto SettingsManager::handleLayoutChanges(
    QWidget *parent, const QList<CollectionConfig> &collections, int viewingCollectionIndex,
    bool titleChangedForView, bool scrollbarChangedForView, bool sidebarModeChangedForView,
    bool gridWidthChangedForView, bool spacingChangedForView, bool alignmentChangedForView,
    bool fontSizeChangedForView, bool hideTitlesChangedForView,
    DetailsPaneManager *detailsPaneManager, ScrollManager *scrollManager,
    ArtworkManager *artworkManager, int currentCollectionIndex) -> void {
  if (viewingCollectionIndex < 0 || viewingCollectionIndex >= collections.size()) {
    return;
  }
  if (titleChangedForView) {
    updateWindowTitle(parent, viewingCollectionIndex, collections);
  }
  if (scrollbarChangedForView) {
    applyScrollbarSettings(parent, viewingCollectionIndex, collections);
  }
  if (sidebarModeChangedForView) {
    refreshSidebar(detailsPaneManager, collections, currentCollectionIndex);
  }
  handleScrollBranch(scrollManager, artworkManager, collections, viewingCollectionIndex,
                     spacingChangedForView, sidebarModeChangedForView, gridWidthChangedForView,
                     alignmentChangedForView, fontSizeChangedForView, hideTitlesChangedForView);
}

// show "Collection Added — X of Y items" confirmation once the
// first scan for a newly-added collection completes. Tracked UUIDs come from
// openSettingsDialog's diff of the collection list at dialog open vs on
// accept.
void SettingsManager::onCollectionScanSummary(const QString &collectionUuid, int itemsScanned,
                                              int itemsApplied, bool success) {
  if (!m_pendingAddSummaries.contains(collectionUuid)) {
    return;
  }
  const QString name = m_pendingAddSummaries.take(collectionUuid);

  // Use the last-known dialog parent if still alive; fall back to nullptr so
  // the message box is still shown as a top-level window.
  QWidget *parent = m_pendingAddSummaryParent.data();

  if (success) {
    QMessageBox::information(
        parent, tr("Collection Added"),
        tr("Collection \"%1\" added.\n\n%2 of %3 items added from the media directory.")
            .arg(name)
            .arg(itemsApplied)
            .arg(itemsScanned));
  } else {
    QMessageBox::warning(parent, tr("Collection Added"),
                         tr("Collection \"%1\" added, but the initial scan did not complete "
                            "cleanly.\n\n%2 of %3 items were added before the scan stopped. "
                            "Check the media directory path and file extensions, then try "
                            "again.")
                             .arg(name)
                             .arg(itemsApplied)
                             .arg(itemsScanned));
  }
}
