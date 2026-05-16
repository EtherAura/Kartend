// Signal/slot wiring for MainWindow's owned managers.
//
// This file is the single source of truth for how managers communicate at
// runtime. Connections are organized as a flat table per sender — every
// QObject::connect() call uses a member-function pointer (no inline lambdas)
// so the manager graph can be read top-to-bottom and grep'd mechanically.
//
// Manager graph (sender → receiver edges):
//
//   DatabaseManager → NavigationManager
//     itemsLoaded               → onItemsLoaded
//     itemCountLoadedWithToken  → onItemCountLoaded
//     collectionScanCompleted   → onBackgroundCollectionScanCompleted
//     itemsRangeLoaded          → onItemsRangeLoaded
//     errorOccurred             → onMediaLibraryError
//   DatabaseManager → ScrollManager
//     visualIndexForPathLoaded  → onVisualIndexForPathLoaded
//   DatabaseManager → MainWindow (UI/title/overlay state)
//     itemCountLoaded           → releaseStartupOverlaySuppressionIfIdle
//     cachedCountsUpdated       → refreshTitleCountsIfActive
//     itemsLoaded               → refreshFilterToolbarOnItemsLoaded
//     scanProgress              → onScanProgress
//     scanStarting              → onScanStarting
//     collectionScanCompleted   → onCollectionScanCompletedStartup
//     collectionScanCompleted   → onCollectionScanCompletedOverlay
//     scanItemsProgress         → onScanItemsProgress
//   DatabaseManager → DetailsPaneManager (collection summary refresh)
//     collectionScanCompleted   → refreshCollectionSummaryOnScanCompleted
//     cachedCountsUpdated       → refreshCollectionSummary
//
//   SettingsManager → MainWindow / DetailsPaneManager
//     collectionsModified       → rebuildHierarchyCache
//     collectionsModified       → refreshCollectionSummary
//
//   ScrollManager → NavigationManager
//     subcollectionEntered      → onSubcollectionEntered
//     virtualFolderEntered      → onVirtualFolderEntered
//     requestItemsRange         → fetchItemsRange
//   ScrollManager → InteractionManager
//     artworkPreviewLaunchRequested → onArtworkPreviewLaunchRequested
//   ScrollManager → MainWindow
//     sortModeChangeRequested   → onSortModeChangeRequested
//     selectItemByIndex         → onSelectItemByIndex
//     coverFlowActiveChanged    → onCoverFlowActiveChanged
//     artworkPreviewVisibilityChanged → onArtworkPreviewVisibilityChanged
//     coverFlowItemActivated    → onCoverFlowItemActivated
//     listColumnWidthChanged    → onListColumnWidthChanged
//     listArtworkColumnWidthChanged → onListArtworkColumnWidthChanged
//     filterChanged             → onScrollFilterChanged
//
//   ArtworkManager::TimerCoordinator → MainWindow
//     viewportUpdateRequested   → onArtworkViewportUpdateRequested
//     layoutUpdateRequested     → onArtworkLayoutUpdateRequested
//
//   InteractionManager → MainWindow
//     selectionChanged          → onInteractionSelectionChanged
//
//   DetailsPaneManager → MainWindow
//     sidebarVisibilityChanged  → onSidebarVisibilityChanged
//     sidebarLayoutChanged      → onSidebarLayoutChanged
//
//   ToolbarController → InteractionManager
//     searchModeAction.triggered → toggleSearchMode
//
//   QScrollBar (item area V/H) → NavigationManager
//     valueChanged              → onViewportChanged
#include <limits>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>

#include <QMessageBox>

#include "artworkmanager.h"
#include "artworkutils.h"
#include "batchscraperunner.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "marqueewindow.h"
#include "metadatalookupprovider.h"
#include "metadataproviderregistry.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "scraperesultdialog.h"
#include "scrollmanager.h"
#include "settingsmanager.h"
#include "timerutils.h"
#include "toolbarcontroller.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"
#include "videoutils.h"

// =====================================================================
// Slot handlers — extracted from inline lambdas. Each handler captures
// the exact behavior of the lambda it replaced (state mutations, early
// returns, conditional checks). Order roughly matches the connect() order
// in the section below. Names are descriptive rather than mirroring the
// signal name verbatim where the slot is more than a passive forward.
// =====================================================================

void MainWindow::releaseStartupOverlaySuppressionIfIdle(int /*count*/) {
  // If the initial load did not start a scan, we can re-enable scan overlays
  // immediately. If a startup scan is in-flight, keep overlays suppressed
  // until it completes.
  if (m_suppressStartupScanOverlays && m_startupActiveScanCount == 0) {
    m_suppressStartupScanOverlays = false;
  }
}

void MainWindow::refreshTitleCountsIfActive() {
  // Cached counts are now recomputed asynchronously; refresh the title once
  // the update completes so the user sees up-to-date totals.
  if (!QApplication::closingDown()) {
    refreshTitleCounts();
  }
}

void MainWindow::refreshFilterToolbarOnItemsLoaded(const QStringList & /*paths*/,
                                                   const QHash<QString, QString> & /*names*/) {
  // Refresh the consolidated filter popup each time a collection's items
  // finish loading. NavigationManager updates currentCollectionIndex through
  // a raw pointer (no Qt signal), so this is the most reliable post-switch
  // hook available — by the time itemsLoaded fires, the index points at the
  // collection the user is now viewing, and the popup's title-pattern toggle
  // needs to mirror that collection's flag.
  refreshFilterToolbar();
}

void MainWindow::onScanProgress(int current, int total, const QString &name) {
  // Update loading overlay with scan progress during initial collection
  // loading.
  if (m_suppressStartupScanOverlays) {
    // Keep the UI interactive on startup; progress is still visible via the
    // title bar set by scanStarting.
    return;
  }
  if (m_loadingOverlay) {
    if (m_loadingOverlay->isActive()) {
      m_loadingOverlay->setMessage(QString("Scanning %1...").arg(name));
      m_loadingOverlay->setProgress(current, total);
    } else {
      m_loadingOverlay->showWithProgress(QString("Scanning %1...").arg(name), current, total);
    }
  }
}

void MainWindow::onScanStarting(const QString &name, int estimatedItems) {
  Q_UNUSED(estimatedItems)
  // Track active scans so overlay persists until all complete (e.g., when
  // showAllSubcollectionItems triggers multiple scans).
  ++m_activeScanCount;

  if (m_suppressStartupScanOverlays) {
    ++m_startupActiveScanCount;
  }
  if (!m_suppressStartupScanOverlays) {
    if (m_loadingOverlay && !m_loadingOverlay->isActive()) {
      m_loadingOverlay->show(QString("Scanning %1...").arg(name));
    }
  }
  // Show "Scanning..." in title bar instead of "0 items"
  setWindowTitle(QString("%1 (Scanning...)").arg(name));
}

void MainWindow::onCollectionScanCompletedStartup(const QString & /*uuid*/) {
  if (!m_suppressStartupScanOverlays) {
    return;
  }
  if (m_startupActiveScanCount > 0) {
    --m_startupActiveScanCount;
  }
  if (m_startupActiveScanCount == 0) {
    m_suppressStartupScanOverlays = false;
  }
}

void MainWindow::onCollectionScanCompletedOverlay(const QString & /*uuid*/) {
  // Hide scan overlay once ALL scans in a batch have completed. When
  // showAllSubcollectionItems triggers scans of many descendants, we only
  // hide the overlay when the last one finishes.
  if (m_activeScanCount > 0) {
    --m_activeScanCount;
  }
  // Only hide overlay and reload when all scans are complete.
  if (m_activeScanCount != 0) {
    return;
  }
  if (m_suppressStartupScanOverlays) {
    return;
  }
  if (m_loadingOverlay && m_loadingOverlay->isActive()) {
    m_loadingOverlay->hide();
  }
  // When showAllSubcollectionItems is enabled and all descendant scans have
  // finished, reload the collection to get accurate counts. NavigationManager
  // skips intermediate reloads while the overlay is active, so we trigger the
  // final reload here after hiding it.
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size() ||
      !m_collections[currentCollectionIndex].showAllSubcollectionItems) {
    return;
  }
  // Use a longer delay (150ms) to ensure the scan worker's commit is visible
  // to the query worker before we request counts.
  QTimer::singleShot(150, this, [this]() {
    if (getNavigationManager()) {
      getNavigationManager()->safeReloadCollection(currentCollectionIndex);
    }
  });
}

void MainWindow::onScanItemsProgress(int itemsProcessed, int totalItems) {
  // Update progress during item scan/save.
  if (m_suppressStartupScanOverlays) {
    return;
  }
  if (!m_loadingOverlay || !m_loadingOverlay->isActive()) {
    return;
  }
  if (totalItems > 0) {
    // Indexing phase - we know the total
    m_loadingOverlay->setMessage(
        QString("Indexing %1 of %2 items...").arg(itemsProcessed).arg(totalItems));
    m_loadingOverlay->setProgress(itemsProcessed, totalItems);
  } else {
    // Scanning phase - total unknown, show count found so far
    m_loadingOverlay->setMessage(QString("Scanning... found %1 items").arg(itemsProcessed));
    // Show indeterminate progress (spinner continues)
    m_loadingOverlay->setProgress(0, 0);
  }
}

void MainWindow::refreshCollectionSummaryOnScanCompleted(const QString & /*uuid*/) {
  if (auto *dpm = getDetailsPaneManager()) {
    dpm->refreshCollectionSummary();
  }
}

void MainWindow::onSortModeChangeRequested(SortMode sortMode) {
  // List view header column click triggers sort mode change.
  if (!getNavigationManager()) {
    return;
  }
  // Capture currently selected file path to restore after reload.
  if (getInteractionManager() && getScrollManager()) {
    QString selectedPath = getInteractionManager()->selectedFilePath();
    if (!selectedPath.isEmpty()) {
      getScrollManager()->setPendingSelectionRestoreByPath(selectedPath);
    }
  }
  m_generalSettings.sortMode = sortMode;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
  getNavigationManager()->safeReloadCollection(currentCollectionIndex);
}

void MainWindow::onSelectItemByIndex(int index) {
  // Restore selection by index after sort change finds the item.
  if (!getInteractionManager()) {
    return;
  }
  getInteractionManager()->selectItemByIndex(index, true);
  // Instantly scroll to make the item visible (no animation).
  getInteractionManager()->applyImmediateViewportPositioningForSelection(index);
}

void MainWindow::onCoverFlowActiveChanged(bool active) {
  // Yield sidebar viewport space when entering CoverFlow, restore the
  // persisted per-collection state when leaving.
  if (auto *dpm = getDetailsPaneManager()) {
    dpm->setExternallyHidden(active);
  }
}

void MainWindow::onArtworkPreviewVisibilityChanged(bool visible) {
  // Bug #7: lower the sidebar while the artwork preview overlay is showing
  // so the overlay (parented to the top-level window) stays on top. Restored
  // on hide.
  if (auto *dpm = getDetailsPaneManager()) {
    dpm->setOverlayActive(visible);
  }
}

void MainWindow::onCoverFlowItemActivated(int index) {
  // Cover-flow activates a card → land selection on it then route through
  // the existing launch path. Subcollection / virtual-folder activations are
  // handled by ScrollManager itself via subcollectionEntered /
  // virtualFolderEntered above so they don't reach this slot. The owning
  // collection comes from DatabaseManager — the launcher / core / params are
  // configured per-collection and using currentCollectionIndex would mis-launch
  // any item inherited from a subcollection in showAllSubcollectionItems mode
  // (surfaces as "no launcher configured").
  if (!getInteractionManager() || !getScrollManager()) {
    return;
  }
  getInteractionManager()->selectItemByIndex(index, true);
  const QString filePath = getScrollManager()->filePathForVisualIndex(index);
  if (filePath.isEmpty()) {
    return;
  }
  int ownerIdx = currentCollectionIndex;
  if (getDatabaseManager()) {
    const int detected = getDatabaseManager()->getCollectionIndexForFile(filePath);
    if (detected >= 0) {
      ownerIdx = detected;
    }
  }
  getInteractionManager()->launchItemWithCollection(filePath, ownerIdx);
}

void MainWindow::onListColumnWidthChanged(int width) {
  // Persist list column width when user resizes.
  m_generalSettings.listCollectionColumnWidth = width;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
}

void MainWindow::onListArtworkColumnWidthChanged(int width) {
  // Persist list artwork column width when user resizes.
  m_generalSettings.listArtworkColumnWidth = width;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
}

void MainWindow::onArtworkViewportUpdateRequested() {
  if (!QApplication::closingDown() && getArtworkManager()) {
    getArtworkManager()->updateViewportArtwork();
  }
}

void MainWindow::onArtworkLayoutUpdateRequested() {
  if (!QApplication::closingDown() && getScrollManager()) {
    getScrollManager()->handleLayoutChange();
  }
}

void MainWindow::onScrollFilterChanged(int visible, int total) {
  if (!QApplication::closingDown()) {
    updateWindowTitleWithFilter(visible, total);
  }
}

void MainWindow::onInteractionSelectionChanged(int /*index*/) {
  // Refresh the top-bar "pos / total" label on selection and collection-size
  // transitions. The filterChanged connection already calls
  // updateItemPositionLabel via updateWindowTitleWithFilter; this also updates
  // on direct selection moves.
  if (!QApplication::closingDown()) {
    updateItemPositionLabel();
    // Push the new selection's artwork to the marquee through the debouncer
    // so a wheel/arrow storm coalesces into a single trailing-edge refresh
    // — the per-tick cost (path expand + FS video probe + image-mode disk
    // load) is the same the sidebar used to pay before its own debounce.
    // Fallback to direct call if setup hasn't wired the debouncer yet
    // (rare: very early teardown / tests).
    if (m_marqueeDebouncer) {
      m_marqueeDebouncer->trigger();
    } else {
      updateMarqueeArtwork();
    }
  }
}

namespace {

/// Resolve a configured marquee screen name to a live QScreen pointer.
/// Empty / missing names return the primary screen so a never-configured
/// (or unplugged-since-config) target still works.
QScreen *resolveMarqueeScreen(const QString &screenName) {
  if (!screenName.isEmpty()) {
    for (QScreen *s : QGuiApplication::screens()) {
      if (s->name() == screenName) return s;
    }
    qWarning("Marquee: configured screen '%s' not found — falling back to primary",
             qPrintable(screenName));
  }
  return QGuiApplication::primaryScreen();
}

/// Resolve the metadata-lookup provider for a collection scrape. An
/// explicit `CollectionConfig::scraperProviderId` override wins; with no
/// override (or one that doesn't resolve to a lookup-capable provider)
/// the first provider whose category matches the collection `type` is
/// used. Ownership of the chosen provider is released out of @p registry
/// into the returned shared_ptr; returns null when nothing usable matches.
std::shared_ptr<MetadataLookupProvider>
pickLookupProvider(std::vector<std::unique_ptr<MetadataProvider>> &registry,
                   const CollectionConfig &cfg) {
  // Claim a provider out of the registry: verify it does metadata
  // lookups, release its unique_ptr, hand back a shared_ptr. Returns
  // null (registry untouched) when the provider can't do lookups.
  auto claim = [&registry](MetadataProvider *p) -> std::shared_ptr<MetadataLookupProvider> {
    if (!p || !p->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup)) {
      return nullptr;
    }
    auto *typed = dynamic_cast<MetadataLookupProvider *>(p);
    if (!typed) return nullptr;
    for (auto &up : registry) {
      if (up.get() == p) {
        up.release();
        break;
      }
    }
    return std::shared_ptr<MetadataLookupProvider>(typed);
  };

  // Explicit per-collection override takes precedence.
  const QString overrideId = cfg.scraperProviderId.trimmed();
  if (!overrideId.isEmpty()) {
    for (auto &up : registry) {
      if (up && up->id() == overrideId) {
        if (auto provider = claim(up.get())) return provider;
        break; // ids are unique — a non-lookup match falls through to type
      }
    }
  }

  // Fall back to the first category match for the collection type.
  const auto applicable = MetadataProviderRegistry::forCategory(registry, cfg.type);
  for (auto &up : registry) {
    if (up && applicable.contains(up.get())) {
      if (auto provider = claim(up.get())) return provider;
    }
  }
  return nullptr;
}

} // namespace

void MainWindow::applyMarqueeSettings() {
  if (QApplication::closingDown()) return;
  if (!m_generalSettings.marqueeEnabled) {
    // Disable path: tear the window down so the user reclaims the
    // secondary monitor. The pointer is set to nullptr (rather than
    // hide()) so the next enable starts from a clean state — handles
    // the case where the user changed the target screen as part of
    // the same Save action.
    if (m_marqueeDebouncer) {
      // Drop any in-flight selection-storm trigger so it can't fire after
      // teardown and run updateMarqueeArtwork's no-op early return for
      // nothing. Harmless either way; this just avoids the spurious wakeup.
      m_marqueeDebouncer->cancel();
    }
    if (m_marqueeWindow) {
      m_marqueeWindow->close();
      m_marqueeWindow->deleteLater();
      m_marqueeWindow = nullptr;
    }
    return;
  }
  QScreen *target = resolveMarqueeScreen(m_generalSettings.marqueeScreenName);
  if (!m_marqueeWindow) {
    m_marqueeWindow = new MarqueeWindow(target);
  } else {
    m_marqueeWindow->pinToScreen(target);
  }
  m_marqueeWindow->show();
  // Settings save / first enable — push immediately, not through the
  // debouncer, so the user sees the marquee populated as soon as they
  // hit Save (no 60ms blank flash).
  if (m_marqueeDebouncer) {
    m_marqueeDebouncer->cancel();
  }
  updateMarqueeArtwork();
}

void MainWindow::updateMarqueeArtwork() {
  if (!m_marqueeWindow || !m_generalSettings.marqueeEnabled) return;

  // Mode 2 — video / attract loop. Source priority: per-item preview
  // video (resolved like the details-pane preview) → collection's
  // backgroundVideo. Empty path stops playback and clears the window.
  if (m_generalSettings.marqueeMode == 2) {
    if (!CollectionUtils::isValidIndex(currentCollectionIndex, &m_collections)) return;
    QString videoPath;
    if (auto *im = getInteractionManager()) {
      const QString filePath = im->selectedFilePath();
      if (!filePath.isEmpty()) {
        const CollectionConfig &cfg = m_collections[currentCollectionIndex];
        QString videoDir = PathUtils::validateAndExpandPath(cfg.videoDirectory, cfg.name);
        if (!videoDir.isEmpty()) {
          videoPath = VideoUtils::findVideoForFile(filePath, videoDir);
        }
        // Fall back to the artwork-tree's `video/` subdir, mirroring
        // the details-pane preview lookup so a single-root collection
        // still finds its videos.
        if (videoPath.isEmpty()) {
          const QString artDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
          if (!artDir.isEmpty()) {
            videoPath = VideoUtils::findVideoForFile(filePath, QDir(artDir).filePath("video"));
          }
        }
      }
    }
    if (videoPath.isEmpty()) {
      videoPath = m_collections[currentCollectionIndex].backgroundVideo;
    }
    m_marqueeWindow->setVideo(videoPath);
    return;
  }

  QString artworkPath;
  if (m_generalSettings.marqueeMode == 0) {
    // Item Artwork mode — the cover of the currently-selected item.
    if (!CollectionUtils::isValidIndex(currentCollectionIndex, &m_collections)) return;
    auto *im = getInteractionManager();
    if (!im) return;
    const QString filePath = im->selectedFilePath();
    if (filePath.isEmpty()) return;
    const CollectionConfig &cfg = m_collections[currentCollectionIndex];
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
    artworkPath = ArtworkManager::findArtworkForFile(QFileInfo(filePath).fileName(), artworkDir);
  } else {
    // Collection Icon mode — the active collection's icon. Stable
    // even as the user scrolls so the marquee acts as a banner.
    if (!CollectionUtils::isValidIndex(currentCollectionIndex, &m_collections)) return;
    artworkPath = m_collections[currentCollectionIndex].collectionIcon;
  }

  if (artworkPath.isEmpty()) {
    m_marqueeWindow->setPixmap(
        QPixmap()); // Clear to background; user sees the topper still exists.
    return;
  }
  QPixmap pix(artworkPath);
  m_marqueeWindow->setPixmap(pix);
}

void MainWindow::openScraperDialog(int preCollectionIndex, const QString &preItemPath) {
  qInfo() << "[MainWindow] openScraperDialog called preCol=" << preCollectionIndex
          << "preItem=" << preItemPath << "dialogExists=" << (m_scraperDialog != nullptr)
          << "service=" << m_scraperService.get();
  if (!getDatabaseManager()) {
    QMessageBox::warning(this, tr("Scraper"), tr("Database is not ready."));
    return;
  }

  // Single reused dialog instance. The dialog's closeEvent override
  // hides instead of destroying when Unified mode is active, so we
  // keep the widget tree across opens. First call constructs;
  // subsequent calls just rebind context + raise.
  if (!m_scraperDialog) {
    m_scraperDialog = new ScrapeResultDialog(/*provider=*/nullptr, /*candidates=*/{}, this);
    m_scraperDialog->setWindowFlag(Qt::Window, true);
    // Unified flow is non-modal — the user must be able to keep
    // using the main window while a scrape runs in the background.
    m_scraperDialog->setModal(false);
  }
  auto *dialog = m_scraperDialog;

  ScrapeResultDialog::ScraperContext sctx;
  sctx.collections = &m_collections;
  sctx.databaseManager = getDatabaseManager();
  sctx.generalSettings = &m_generalSettings;
  // Provider builder: per-collection-index lookup that hands the
  // dialog a fresh shared_ptr<MetadataLookupProvider>. Each call
  // rebuilds the registry with the live accessor closures so SS
  // sees current credentials + the right systemeid override.
  sctx.providerBuilder = [this](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
    auto registry = MetadataProviderRegistry::builtIn(
        [this]() -> const GeneralSettings * { return &m_generalSettings; },
        [this, idx]() -> const CollectionConfig * {
          if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
          return &m_collections[idx];
        });
    return pickLookupProvider(registry, m_collections[idx]);
  };
  // applyResult: post-Apply persistence hook for the unified
  // interactive flow. Mirrors the existing single-item right-click
  // path's Scraper::applyScrapedItem call so metadata + media writes
  // hit disk identically.
  sctx.applyResult = [this](int collectionIndex, const QString &filePath,
                            const ScrapeResultDialog::Result &result) {
    if (!CollectionUtils::isValidIndex(collectionIndex, &m_collections)) return;
    if (!getDatabaseManager()) return;
    const CollectionConfig &cfg = m_collections[collectionIndex];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
    const QString baseName = QFileInfo(filePath).completeBaseName();
    QList<Scraper::PendingMediaWrite> writes;
    writes.reserve(result.downloads.size());
    for (const auto &d : result.downloads) {
      Scraper::PendingMediaWrite w;
      w.asset = d.asset;
      w.bytes = d.bytes;
      writes.append(w);
    }
    const Scraper::RescrapeMode rescrapeMode =
        static_cast<Scraper::RescrapeMode>(m_generalSettings.scraperOptions.rescrapeMode);
    (void)Scraper::applyScrapedItem(getDatabaseManager(), uuid, filePath, artworkDir, baseName,
                                    result.item, writes, rescrapeMode);
  };
  dialog->setScraperContext(sctx);
  // Hand the long-lived service to the dialog. The service was
  // constructed in MainWindow's ctor; we configure it here with the
  // same builder so a resume on next launch can still build
  // providers, then bind to the dialog. The dialog reads
  // m_service->isActive() in startUnifiedScrape and lands directly
  // in the Live view when there's already a run in progress.
  Scraper::ScraperService::Context srvCtx;
  srvCtx.databaseManager = getDatabaseManager();
  srvCtx.generalSettings = &m_generalSettings;
  srvCtx.collections = &m_collections;
  srvCtx.providerBuilder = sctx.providerBuilder;
  m_scraperService->setContext(srvCtx);
  dialog->setScraperService(m_scraperService.get());

  // Post-completion housekeeping: refresh the active grid + sidebar +
  // artwork cache once the scrape ends, then surface a summary box.
  // UniqueConnection prevents the lambda from being attached multiple
  // times if the user opens the dialog repeatedly — without it, one
  // scrape-finish would pop N message boxes.
  static bool s_unifiedFinishedConnected = false;
  if (!s_unifiedFinishedConnected) {
    s_unifiedFinishedConnected = true;
    QObject::connect(
        dialog, &ScrapeResultDialog::unifiedScrapeFinished, this,
        [this](int scraped, int skipped, int errors, const QStringList &firstFailures) {
          if (getDetailsPaneManager() && getScrollManager() && getInteractionManager()) {
            const int sel = getInteractionManager()->currentSelectedIndex();
            if (sel >= 0) {
              ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
              getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
            }
          }
          ArtworkUtils::clearDirectoryCache();
          if (getNavigationManager() &&
              CollectionUtils::isValidIndex(currentCollectionIndex, &m_collections)) {
            getNavigationManager()->safeReloadCollection(currentCollectionIndex);
          }
          QString text = tr("Scrape complete.\n\nScraped: %1\nSkipped: %2\nErrors: %3")
                             .arg(scraped)
                             .arg(skipped)
                             .arg(errors);
          if (!firstFailures.isEmpty()) {
            text += QStringLiteral("\n\n") +
                    tr("First failures:\n%1").arg(firstFailures.join(QChar('\n')));
          }
          QMessageBox::information(this, tr("Scraper"), text);
        });
  }

  dialog->startUnifiedScrape(preCollectionIndex, preItemPath);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void MainWindow::promptResumePendingScrapeIfAny() {
  if (!m_scraperService) return;
  auto pending = m_scraperService->loadPendingState(/*consumeOnLoad=*/false);
  if (!pending.isValid()) return;
  // Wire the service context first so a resume (silent or accepted)
  // can build providers + persist progress. Same context as
  // openScraperDialog — kept in sync because both paths run on
  // MainWindow's live closures.
  Scraper::ScraperService::Context srvCtx;
  srvCtx.databaseManager = getDatabaseManager();
  srvCtx.generalSettings = &m_generalSettings;
  srvCtx.collections = &m_collections;
  srvCtx.providerBuilder = [this](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
    auto registry = MetadataProviderRegistry::builtIn(
        [this]() -> const GeneralSettings * { return &m_generalSettings; },
        [this, idx]() -> const CollectionConfig * {
          if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
          return &m_collections[idx];
        });
    return pickLookupProvider(registry, m_collections[idx]);
  };
  m_scraperService->setContext(srvCtx);

  // Auto-resume is gated by GeneralSettings::ScraperOptions::scrapeAutoResume
  // (Kartend-1uvp). Off by default — first-time users see the modal Resume /
  // Discard prompt below and learn the recovery path. Power users running
  // unattended overnight batches flip it on so a crash + relaunch self-heals
  // without a dialog blocking the resume.
  const bool autoResume = m_generalSettings.scraperOptions.scrapeAutoResume;
  if (autoResume) {
    m_scraperService->loadPendingState(/*consumeOnLoad=*/true); // delete file
    m_scraperService->resumeFromState(pending);
    // Also pop the Scraper window so the user has the Live view +
    // Close / Cancel buttons available without hunting for the menu.
    openScraperDialog();
    return;
  }
  const int remaining = pending.totalRemaining();
  const QString started =
      QDateTime::fromMSecsSinceEpoch(pending.startedAtUnixMs).toString(Qt::TextDate);
  // Non-blocking prompt — using open() + buttonClicked instead of
  // exec() keeps the event loop spinning so the collection grid can
  // finish painting / loading while the user reads the prompt.
  // exec() would freeze startup behind the modal, which on large
  // scrape jobs is exactly when the user wants the grid visible
  // first.
  auto *box = new QMessageBox(this);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowTitle(tr("Resume scrape?"));
  box->setIcon(QMessageBox::Question);
  box->setText(tr("An interrupted scrape from %1 was found.").arg(started));
  box->setInformativeText(tr("Scraped so far: %1\nSkipped: %2\nErrors: %3\n"
                             "Remaining items: %4")
                              .arg(pending.summarySoFar.scraped)
                              .arg(pending.summarySoFar.skipped)
                              .arg(pending.summarySoFar.errors)
                              .arg(remaining));
  auto *resumeBtn = box->addButton(tr("Resume"), QMessageBox::AcceptRole);
  auto *discardBtn = box->addButton(tr("Discard"), QMessageBox::DestructiveRole);
  box->addButton(tr("Keep for later"), QMessageBox::RejectRole);
  box->setDefaultButton(resumeBtn);
  // Capture pending by value — the state struct is cheap enough and
  // we already paid for the parse. Avoids re-parsing the file in the
  // Resume branch.
  connect(box, &QMessageBox::buttonClicked, this,
          [this, box, resumeBtn, discardBtn, pending](QAbstractButton *clicked) {
            if (clicked == resumeBtn) {
              m_scraperService->discardPendingState();
              m_scraperService->resumeFromState(pending);
              openScraperDialog();
            } else if (clicked == discardBtn) {
              m_scraperService->discardPendingState();
            }
            // "Keep for later" leaves the file in place — next launch re-prompts.
            box->deleteLater();
          });
  box->open();
}

void MainWindow::onSidebarVisibilityChanged(bool visible) {
  if (visible && getDetailsPaneManager() && getScrollManager() && getInteractionManager()) {
    int sel = getInteractionManager()->currentSelectedIndex();
    if (sel >= 0) {
      ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
      getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
    }
  }
  // Push the "sidebar hidden AND would shrink" predicate into ScrollManager so
  // the upcoming metrics recompute picks the right gridWidth /
  // horizontalGridHeight pair. Overlay mode never shrinks (the sidebar
  // floats), so it stays on the primary values.
  updateScrollManagerSidebarShrinking();
  // Delay metrics recalculation to allow sidebar animation to complete before
  // recalculating grid layout dimensions.
  QTimer::singleShot(UIConstants::DetailsPane::METRICS_RECALC_DELAY_MS, this, [this]() {
    if (getScrollManager()) {
      getScrollManager()->recalculateContainerMetrics();
    }
  });
}

void MainWindow::onSidebarLayoutChanged() {
  if (getScrollManager()) {
    getScrollManager()->recalculateContainerMetrics();
  }
  if (getDetailsPaneManager() && getScrollManager() && getInteractionManager() &&
      getDetailsPaneManager()->isSidebarVisible()) {
    int sel = getInteractionManager()->currentSelectedIndex();
    if (sel >= 0) {
      ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
      getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
    }
  }
}

// =====================================================================
// Connection tables — every connect is a one-liner using member-function
// pointers. Read top-to-bottom for the manager graph.
// =====================================================================

void MainWindow::connectDatabaseManager() {
  auto *db = getDatabaseManager();
  auto *nav = getNavigationManager();
  auto *scroll = getScrollManager();
  auto *settings = getSettingsManager();
  auto *details = getDetailsPaneManager();

  // DatabaseManager → NavigationManager
  QObject::connect(db, &DatabaseManager::itemsLoaded, nav, &NavigationManager::onItemsLoaded);
  QObject::connect(db, &DatabaseManager::itemCountLoadedWithToken, nav,
                   &NavigationManager::onItemCountLoaded);
  QObject::connect(db, &DatabaseManager::collectionScanCompleted, nav,
                   &NavigationManager::onBackgroundCollectionScanCompleted);
  QObject::connect(db, &DatabaseManager::itemsRangeLoaded, nav,
                   &NavigationManager::onItemsRangeLoaded);
  QObject::connect(db, &DatabaseManager::errorOccurred, nav,
                   &NavigationManager::onMediaLibraryError);

  // DatabaseManager → ScrollManager
  QObject::connect(db, &DatabaseManager::visualIndexForPathLoaded, scroll,
                   &ScrollManager::onVisualIndexForPathLoaded);

  // DatabaseManager → MainWindow (overlay/title/filter UI state)
  QObject::connect(db, &DatabaseManager::itemCountLoaded, this,
                   &MainWindow::releaseStartupOverlaySuppressionIfIdle);
  QObject::connect(db, &DatabaseManager::cachedCountsUpdated, this,
                   &MainWindow::refreshTitleCountsIfActive);
  QObject::connect(db, &DatabaseManager::itemsLoaded, this,
                   &MainWindow::refreshFilterToolbarOnItemsLoaded);
  QObject::connect(db, &DatabaseManager::scanProgress, this, &MainWindow::onScanProgress);
  QObject::connect(db, &DatabaseManager::scanStarting, this, &MainWindow::onScanStarting);
  QObject::connect(db, &DatabaseManager::collectionScanCompleted, this,
                   &MainWindow::onCollectionScanCompletedStartup);
  QObject::connect(db, &DatabaseManager::collectionScanCompleted, this,
                   &MainWindow::onCollectionScanCompletedOverlay);
  QObject::connect(db, &DatabaseManager::scanItemsProgress, this, &MainWindow::onScanItemsProgress);

  // SettingsManager → MainWindow / DetailsPaneManager
  QObject::connect(settings, &SettingsManager::collectionsModified, this,
                   &MainWindow::rebuildHierarchyCache);

  // DatabaseManager / SettingsManager → DetailsPaneManager (sidebar summary)
  if (details) {
    QObject::connect(db, &DatabaseManager::collectionScanCompleted, this,
                     &MainWindow::refreshCollectionSummaryOnScanCompleted);
    QObject::connect(db, &DatabaseManager::cachedCountsUpdated, details,
                     &DetailsPaneManager::refreshCollectionSummary);
    QObject::connect(settings, &SettingsManager::collectionsModified, details,
                     &DetailsPaneManager::refreshCollectionSummary);
  }
}

void MainWindow::connectScrollManager() {
  auto *scroll = getScrollManager();
  auto *nav = getNavigationManager();
  auto *interaction = getInteractionManager();
  auto *artwork = getArtworkManager();

  // ScrollManager → NavigationManager
  QObject::connect(scroll, &ScrollManager::subcollectionEntered, nav,
                   &NavigationManager::onSubcollectionEntered);
  QObject::connect(scroll, &ScrollManager::virtualFolderEntered, nav,
                   &NavigationManager::onVirtualFolderEntered);
  QObject::connect(scroll, &ScrollManager::requestItemsRange, nav,
                   &NavigationManager::fetchItemsRange);

  // ScrollManager → InteractionManager (expand-mode artwork preview launch)
  if (interaction) {
    QObject::connect(scroll, &ScrollManager::artworkPreviewLaunchRequested, interaction,
                     &InteractionManager::onArtworkPreviewLaunchRequested);
  }

  // ScrollManager → MainWindow
  QObject::connect(scroll, &ScrollManager::sortModeChangeRequested, this,
                   &MainWindow::onSortModeChangeRequested);
  QObject::connect(scroll, &ScrollManager::selectItemByIndex, this,
                   &MainWindow::onSelectItemByIndex);
  QObject::connect(scroll, &ScrollManager::coverFlowActiveChanged, this,
                   &MainWindow::onCoverFlowActiveChanged);
  QObject::connect(scroll, &ScrollManager::artworkPreviewVisibilityChanged, this,
                   &MainWindow::onArtworkPreviewVisibilityChanged);
  QObject::connect(scroll, &ScrollManager::coverFlowItemActivated, this,
                   &MainWindow::onCoverFlowItemActivated);
  QObject::connect(scroll, &ScrollManager::listColumnWidthChanged, this,
                   &MainWindow::onListColumnWidthChanged);
  QObject::connect(scroll, &ScrollManager::listArtworkColumnWidthChanged, this,
                   &MainWindow::onListArtworkColumnWidthChanged);
  QObject::connect(scroll, &ScrollManager::filterChanged, this, &MainWindow::onScrollFilterChanged);

  // ArtworkManager::TimerCoordinator → MainWindow
  if (artwork) {
    QObject::connect(artwork->getTimerCoordinator(),
                     &TimerUtils::Coordinator::viewportUpdateRequested, this,
                     &MainWindow::onArtworkViewportUpdateRequested);
    QObject::connect(artwork->getTimerCoordinator(),
                     &TimerUtils::Coordinator::layoutUpdateRequested, this,
                     &MainWindow::onArtworkLayoutUpdateRequested);
  }

  // InteractionManager → MainWindow
  if (interaction) {
    QObject::connect(interaction, &InteractionManager::selectionChanged, this,
                     &MainWindow::onInteractionSelectionChanged);
  }
}

void MainWindow::connectSidebarManager() {
  auto *details = getDetailsPaneManager();
  // DetailsPaneManager → MainWindow
  QObject::connect(details, &DetailsPaneManager::sidebarVisibilityChanged, this,
                   &MainWindow::onSidebarVisibilityChanged);
  QObject::connect(details, &DetailsPaneManager::sidebarLayoutChanged, this,
                   &MainWindow::onSidebarLayoutChanged);
}

void MainWindow::updateScrollManagerSidebarShrinking() {
  if (!getScrollManager() || !getDetailsPaneManager()) {
    return;
  }
  // Use DetailsPaneManager's tracked index rather than MainWindow::currentCollectionIndex
  // because this can be called from inside applySidebarStateForCollection (via
  // its sidebarVisibilityChanged emission) before MainWindow has caught up to
  // the new index after a collection switch.
  const int idx = getDetailsPaneManager()->currentCollectionIndex();
  if (idx < 0 || idx >= m_collections.size()) {
    getScrollManager()->setSidebarShrinkingActive(false);
    return;
  }
  const CollectionConfig &collection = m_collections[idx];
  // Overlay mode never shrinks the grid (the sidebar floats over content), so
  // the alt gridWidth only applies in Expand mode while the sidebar is hidden.
  if (collection.sidebarMode != DetailsPaneMode::Expand) {
    getScrollManager()->setSidebarShrinkingActive(false);
    return;
  }
  const bool sidebarHidden = !getDetailsPaneManager()->isSidebarVisible();
  // The pane only shrinks the grid along its own dock axis. A vertical-axis
  // dock (Top/Bottom) takes height, so vertical-scrolling views (Grid/List/
  // CoverFlow) — whose layout dimension is items-per-row, i.e. horizontal —
  // are unaffected by it. Symmetrically, an L/R dock leaves Horizontal view's
  // items-per-column dimension untouched. When the pane doesn't reduce the
  // relevant axis at all, the alt-when-hidden value should stay in effect
  // even while the pane is visible — toggling it never reclaimed any space
  // along that axis, so the user's "more items" value isn't a hidden-only
  // arrangement, it's the only correct one for that combo.
  const bool paneIsHorizontalDock =
      CollectionUtils::isDetailsPaneHorizontal(collection.sidebarPosition);
  const bool relevantAxisIsHorizontal = (collection.viewType != ViewType::Horizontal);
  const bool paneAffectsRelevantAxis =
      relevantAxisIsHorizontal ? !paneIsHorizontalDock : paneIsHorizontalDock;
  getScrollManager()->setSidebarShrinkingActive(sidebarHidden || !paneAffectsRelevantAxis);
}

void MainWindow::connectSearchComponents() {
  // Search-mode toggle lives inside the QLineEdit as a QAction owned by the
  // ToolbarController. Wire it to InteractionManager's existing toggle slot
  // so the cycling behavior remains identical to the legacy QPushButton.
  if (m_toolbarController && m_toolbarController->searchModeAction() && getInteractionManager()) {
    QObject::connect(m_toolbarController->searchModeAction(), &QAction::triggered,
                     getInteractionManager(), &InteractionManager::toggleSearchMode);
  }
}

void MainWindow::refreshFilterToolbar() {
  if (m_toolbarController) {
    m_toolbarController->refreshFilterToolbar();
  }
}

void MainWindow::connectFilterToolbar() {
  if (m_toolbarController) {
    m_toolbarController->connectFilterToolbar();
  }
}

void MainWindow::connectScrollBars() const {
  if (!ui->itemScrollArea || !getNavigationManager()) {
    return;
  }
  if (const QScrollBar *vScrollBar = ui->itemScrollArea->verticalScrollBar()) {
    QObject::connect(vScrollBar, &QScrollBar::valueChanged, getNavigationManager(),
                     &NavigationManager::onViewportChanged);
  }
  if (const QScrollBar *hScrollBar = ui->itemScrollArea->horizontalScrollBar()) {
    QObject::connect(hScrollBar, &QScrollBar::valueChanged, getNavigationManager(),
                     &NavigationManager::onViewportChanged);
  }
}
