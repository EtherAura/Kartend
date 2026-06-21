// Settings dialog orchestration, extracted from settingsmanager.cpp and then
// relocated out of modules/data/ into this ui-layer controller class
// (Kartend-q8p29, following the DetailsPaneManager precedent):
//   - openSettingsDialog
//   - handleReloadRequired
//   - handleLayoutChanges
//   - onCollectionScanSummary (async "Collection Added" summary boxes)
// Plus the file-local anonymous-namespace helpers used by these methods
// (compareNonReloadFields, compareReloadFields, updateViewingFlags,
// updateWindowTitle, applyScrollbarSettings, refreshSidebar,
// handleScrollBranch, detectChanges).
#include "settingsdialogcontroller.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "collection/typehelpers.h"
#include "errorpresentation.h"
#include "iartworkmanager.h"
#include "icachemanager.h"
#include "idatabasemanager.h"
#include "idetailspanemanager.h"
#include "imainwindow.h"
#include "inavigationmanager.h"
#include "iscrollmanager.h"
#include "isettingsdialog.h"
#include "isettingsmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/timing.h"
#include <memory>
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
  if (configA.name != configB.name ||
      configA.launcher.launcherPath != configB.launcher.launcherPath ||
      configA.launcher.corePath != configB.launcher.corePath ||
      configA.launcher.launchParameters != configB.launcher.launchParameters ||
      configA.archive.extractArchives != configB.archive.extractArchives ||
      configA.archive.extractedExtension != configB.archive.extractedExtension ||
      configA.expandMode != configB.expandMode ||
      configA.parentCollectionIndex != configB.parentCollectionIndex ||
      configA.isSubcollection != configB.isSubcollection ||
      // Without sidebarPosition here, an orientation-only edit in the settings
      // dialog returned hasChanges=false and silently dropped the new value —
      // the user had to toggle the pane (or change another field) before the
      // chosen edge took effect. applySidebarStateForCollection downstream
      // re-runs the layout swap so the pane reparents into the right Qt
      // layout immediately on save.
      configA.sidebar.sidebarPosition != configB.sidebar.sidebarPosition) {
    hasChanges = true;
  }
}

// Compares fields that trigger reload
void compareReloadFields(const CollectionConfig &configA, const CollectionConfig &configB,
                         bool &hasChanges, bool &needsReload) {
  if (configA.mediaDirectory != configB.mediaDirectory ||
      configA.artworkDirectory != configB.artworkDirectory ||
      configA.folderBrowsing.includeContentSubfolders !=
          configB.folderBrowsing.includeContentSubfolders ||
      configA.folderBrowsing.includeArtworkSubfolders !=
          configB.folderBrowsing.includeArtworkSubfolders ||
      configA.showAllSubcollectionItems != configB.showAllSubcollectionItems ||
      configA.folderBrowsing.showAllSubfolderItems !=
          configB.folderBrowsing.showAllSubfolderItems ||
      configA.folderBrowsing.hideSubfolderTitles != configB.folderBrowsing.hideSubfolderTitles ||
      configA.folderBrowsing.showHiddenFolders != configB.folderBrowsing.showHiddenFolders ||
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
  if (configA.gridLayout.gridWidth != configB.gridLayout.gridWidth) {
    hasChanges = true;
    gridWidthChanged = true;
  }
  if (configA.horizontalAlignment != configB.horizontalAlignment) {
    hasChanges = true;
    alignmentChanged = true;
  }
  if (configA.gridLayout.horizontalSpacing != configB.gridLayout.horizontalSpacing ||
      configA.gridLayout.verticalSpacing != configB.gridLayout.verticalSpacing) {
    hasChanges = true;
    spacingChanged = true;
  }
  if (configA.gridLayout.itemWidth != configB.gridLayout.itemWidth ||
      configA.gridLayout.itemHeight != configB.gridLayout.itemHeight) {
    hasChanges = true;
    spacingChanged = true;
  }
  if (configA.gridLayout.fontSize != configB.gridLayout.fontSize ||
      configA.listView.listFontSize != configB.listView.listFontSize ||
      configA.gridLayout.cornerRadius != configB.gridLayout.cornerRadius) {
    hasChanges = true;
    fontSizeChanged = true;
  }
  if (configA.hideTitles != configB.hideTitles ||
      configA.hideSubcollectionTitles != configB.hideSubcollectionTitles) {
    hasChanges = true;
    hideTitlesChanged = true;
  }
  if (configA.gridLayout.hideHorizontalScrollbar != configB.gridLayout.hideHorizontalScrollbar ||
      configA.gridLayout.hideVerticalScrollbar != configB.gridLayout.hideVerticalScrollbar) {
    hasChanges = true;
    scrollbarChanged = true;
  }
  if (configA.sidebar.sidebarMode != configB.sidebar.sidebarMode) {
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
  // Appearance changes (colors).
  //
  // Kartend-fybhy: the whole-background scalar enumeration that used to live
  // here collapses to the leaf-level CollectionBackground::operator!= — that
  // operator compares exactly the 15 background fields the old block listed
  // (backgroundType/Color/Image/Video, primary/tile/selectionColor,
  // headerLogoImage/Position, vignetteEnabled/Intensity, wallpaperParallax/
  // parallaxStrength, toolbarBackdropBlur/backdropBlurRadius), so this is a
  // field-for-field equivalent that can no longer silently miss a newly added
  // background field. The three listView appearance fields are kept inline (NOT
  // collapsed to ListViewOptions::operator!=) on purpose: that leaf also covers
  // listFontSize, which is already routed through the fontSizeChanged path
  // above — folding the whole leaf in here would over-trigger appearanceChanged
  // on a font-size-only edit.
  if (configA.background != configB.background ||
      configA.listView.listRowColor != configB.listView.listRowColor ||
      configA.listView.listAltRowColor != configB.listView.listAltRowColor ||
      configA.listView.listRowHeight != configB.listView.listRowHeight) {
    hasChanges = true;
    appearanceChanged = true;
  }
}

// Title update logic separated from handleLayoutChanges
void updateWindowTitle(QWidget *parent, int viewingIndex,
                       const QList<CollectionConfig> &collections) {
  // dynamic_cast (not qobject_cast): IMainWindow is a plain abstract base
  // with no Qt meta-object. The cross-cast still resolves whenever `parent`
  // is the real MainWindow.
  if (auto *mw = dynamic_cast<IMainWindow *>(parent)) {
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
void refreshSidebar(IDetailsPaneManager *detailsPaneManager,
                    const QList<CollectionConfig> & /*collections*/, int currentCollectionIndex) {
  if (detailsPaneManager) {
    detailsPaneManager->updateSidebarLayout(currentCollectionIndex);
  }
}

// Handles scroll manager branching
void handleScrollBranch(IScrollManager *scrollManager, IArtworkManager *artworkManager,
                        const QList<CollectionConfig> &collections, int viewingIndex,
                        bool spacingChanged, bool sidebarModeChanged, bool gridWidthChanged,
                        bool alignmentChanged, bool fontSizeChanged, bool hideTitlesChanged) {
  auto scheduleGridWidthRefresh = [](IScrollManager *scrollManager, IArtworkManager *artworkManager,
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
    scrollManager->updateGridWidth(collections[viewingIndex].gridLayout.gridWidth);
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

void SettingsDialogController::openSettingsDialog(const SettingsDialogContext &context) {
  if (!context.collections || !context.currentCollectionIndex) {
    return;
  }

  QList<CollectionConfig> &collections = *context.collections;
  int &currentCollectionIndex = *context.currentCollectionIndex;
  QWidget *parent = context.parent;
  IDetailsPaneManager *detailsPaneManager = context.detailsPaneManager;
  IScrollManager *scrollManager = context.scrollManager;
  INavigationManager *navigationManager = context.navigationManager;
  IDatabaseManager *databaseManager = context.databaseManager;

  int viewingCollectionIndex = currentCollectionIndex;
  QList<CollectionConfig> originalCollections = collections;

  // MainWindow supplies a factory that constructs the concrete SettingsDialog
  // and wires its collectionSaved / rescanRequired Qt signals to the two
  // callbacks below (the signal connections need the concrete symbols). We
  // then drive the dialog through the neutral ISettingsDialog interface.
  if (!context.createSettingsDialog) {
    return;
  }

  auto onCollectionSaved = [this, &collections,
                            parent](const QList<CollectionConfig> &savedCollections) {
    collections = savedCollections;
    // Persist + surface disk-write failures back through the open settings
    // dialog. Without this the toolbar/tree-driven save buttons would lie
    // about a successful save when QSettings::sync() returns a status error.
    if (ISettingsManager *settings = m_ctx ? m_ctx->settingsManager() : nullptr) {
      if (auto result = settings->saveCollections(collections); result.isError()) {
        ErrorPresentation::showError(parent, result.error());
      }
    }
  };

  auto onRescanRequired = [navigationManager](int collectionIndex) {
    if (navigationManager) {
      // Defer rescan past the dialog's tear-down: forceRescanCollection
      // grabs the database connection, and racing it against QDialog::done()
      // queuing destroy events trips a reentrancy assertion under TSan.
      QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS,
                         [navigationManager, collectionIndex]() {
                           navigationManager->forceRescanCollection(collectionIndex);
                         });
    }
  };

  std::unique_ptr<ISettingsDialog> dlg = context.createSettingsDialog(
      parent, collections, viewingCollectionIndex, onCollectionSaved, onRescanRequired);
  if (!dlg) {
    return;
  }

  // Apply the caller's optional page hint before exec() so the user lands
  // on the relevant tab without an extra click. Default is a no-op.
  dlg->setInitialPage(context.initialPage);

  if (dlg->exec() != QDialog::Accepted) {
    return;
  }

  QList<CollectionConfig> newCollections = dlg->getCollections();

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

  IArtworkManager *art = m_ctx ? m_ctx->artworkManager() : nullptr;
  ICacheManager *cache = m_ctx ? m_ctx->cacheManager() : nullptr;
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
  if (ISettingsManager *settings = m_ctx ? m_ctx->settingsManager() : nullptr) {
    if (auto result = settings->saveCollections(collections); result.isError()) {
      ErrorPresentation::showError(parent, result.error());
    }
  }

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
      // Kartend-sqoq0: capture the owner-supplied runners for the async
      // summary boxes — the context is gone by the time the scan finishes.
      m_dialogRunners = context.dialogs;
      // Connect once (UniqueConnection) to the database manager's forwarded
      // signal so the user sees a single message box per newly added
      // collection even across repeated openSettingsDialog invocations.
      if (databaseManager) {
        connect(databaseManager, &IDatabaseManager::collectionScanSummary, this,
                &SettingsDialogController::onCollectionScanSummary, Qt::UniqueConnection);
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

auto SettingsDialogController::handleReloadRequired(
    const QList<CollectionConfig> &collections, const QList<CollectionConfig> &newCollections,
    const QList<CollectionConfig> &originalCollections, int viewingCollectionIndex,
    IDetailsPaneManager *detailsPaneManager, IScrollManager *scrollManager,
    INavigationManager *navigationManager, IArtworkManager *artworkManager,
    ICacheManager *cacheManager, int currentCollectionIndex) -> void {
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
        // Cache eviction is scoped to the collection's artworkDirectory so
        // unrelated collections' cached pixmaps survive a media-directory
        // change here. Skip when the directory is empty — there's nothing
        // to scope by and the previous blanket-clear is no longer the
        // implementation.
        const QString &artworkDir = newCollections[viewingCollectionIndex].artworkDirectory;
        if (!artworkDir.isEmpty()) {
          cacheManager->clearCollectionCache(artworkDir);
        }
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

auto SettingsDialogController::handleLayoutChanges(
    QWidget *parent, const QList<CollectionConfig> &collections, int viewingCollectionIndex,
    bool titleChangedForView, bool scrollbarChangedForView, bool sidebarModeChangedForView,
    bool gridWidthChangedForView, bool spacingChangedForView, bool alignmentChangedForView,
    bool fontSizeChangedForView, bool hideTitlesChangedForView,
    IDetailsPaneManager *detailsPaneManager, IScrollManager *scrollManager,
    IArtworkManager *artworkManager, int currentCollectionIndex) -> void {
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
void SettingsDialogController::onCollectionScanSummary(const QString &collectionUuid,
                                                       int itemsScanned, int itemsApplied,
                                                       bool success) {
  if (!m_pendingAddSummaries.contains(collectionUuid)) {
    return;
  }
  const QString name = m_pendingAddSummaries.take(collectionUuid);

  const QString title = tr("Collection Added");
  if (success) {
    const QString text =
        tr("Collection \"%1\" added.\n\n%2 of %3 items added from the media directory.")
            .arg(name)
            .arg(itemsApplied)
            .arg(itemsScanned);
    // Kartend-sqoq0: prefer the owner-supplied runner; fall back to the
    // stock QMessageBox (parented on the last-known dialog parent if still
    // alive, else shown as a top-level window) when none is wired.
    if (m_dialogRunners.info) {
      m_dialogRunners.info(title, text);
    } else {
      QMessageBox::information(m_pendingAddSummaryParent.data(), title, text);
    }
  } else {
    const QString text = tr("Collection \"%1\" added, but the initial scan did not complete "
                            "cleanly.\n\n%2 of %3 items were added before the scan stopped. "
                            "Check the media directory path and file extensions, then try "
                            "again.")
                             .arg(name)
                             .arg(itemsApplied)
                             .arg(itemsScanned);
    if (m_dialogRunners.warn) {
      m_dialogRunners.warn(title, text);
    } else {
      QMessageBox::warning(m_pendingAddSummaryParent.data(), title, text);
    }
  }
}
