// Implementation extracted from mainwindow_dbevents.cpp (Kartend-hzef step 2).
// All 8 slots and the 3 scan-counter fields moved verbatim; the
// MainWindow:: receiver was swapped for DbEventsController::, the local
// state references for m_ctx.<getter>() calls.
#include "dbeventscontroller.h"

#include <QApplication>
#include <QTimer>

#include "collectionutils.h"
#include "detailspanemanager.h"
#include "loadingoverlay.h"
#include "navigationmanager.h"

DbEventsController::DbEventsController(QObject *parent) : QObject(parent) {}
DbEventsController::~DbEventsController() = default;

void DbEventsController::setContext(const DbEventsControllerContext &context) {
  m_ctx = context;
}

void DbEventsController::releaseStartupOverlaySuppressionIfIdle(int /*count*/) {
  // If the initial load did not start a scan, we can re-enable scan overlays
  // immediately. If a startup scan is in-flight, keep overlays suppressed
  // until it completes.
  if (m_suppressStartupScanOverlays && m_startupActiveScanCount == 0) {
    m_suppressStartupScanOverlays = false;
  }
}

void DbEventsController::refreshTitleCountsIfActive() {
  // Cached counts are now recomputed asynchronously; refresh the title once
  // the update completes so the user sees up-to-date totals.
  if (!QApplication::closingDown() && m_ctx.refreshTitleCounts) {
    m_ctx.refreshTitleCounts();
  }
}

void DbEventsController::refreshFilterToolbarOnItemsLoaded(
    const QStringList & /*paths*/, const QHash<QString, QString> & /*names*/) {
  // Refresh the consolidated filter popup each time a collection's items
  // finish loading. NavigationManager updates currentCollectionIndex through
  // a raw pointer (no Qt signal), so this is the most reliable post-switch
  // hook available — by the time itemsLoaded fires, the index points at the
  // collection the user is now viewing, and the popup's title-pattern toggle
  // needs to mirror that collection's flag.
  if (m_ctx.refreshFilterToolbar) {
    m_ctx.refreshFilterToolbar();
  }
}

void DbEventsController::onScanProgress(int current, int total, const QString &name) {
  // Update loading overlay with scan progress during initial collection
  // loading.
  if (m_suppressStartupScanOverlays) {
    // Keep the UI interactive on startup; progress is still visible via the
    // title bar set by scanStarting.
    return;
  }
  LoadingOverlay *overlay = m_ctx.getLoadingOverlay ? m_ctx.getLoadingOverlay() : nullptr;
  if (overlay) {
    if (overlay->isActive()) {
      overlay->setMessage(QString("Scanning %1...").arg(name));
      overlay->setProgress(current, total);
    } else {
      overlay->showWithProgress(QString("Scanning %1...").arg(name), current, total);
    }
  }
}

void DbEventsController::onScanStarting(const QString &name, int estimatedItems) {
  Q_UNUSED(estimatedItems)
  // Track active scans so overlay persists until all complete (e.g., when
  // showAllSubcollectionItems triggers multiple scans).
  ++m_activeScanCount;

  if (m_suppressStartupScanOverlays) {
    ++m_startupActiveScanCount;
  }
  if (!m_suppressStartupScanOverlays) {
    LoadingOverlay *overlay = m_ctx.getLoadingOverlay ? m_ctx.getLoadingOverlay() : nullptr;
    if (overlay && !overlay->isActive()) {
      overlay->show(QString("Scanning %1...").arg(name));
    }
  }
  // Show "Scanning..." in title bar instead of "0 items"
  if (m_ctx.setWindowTitle) {
    m_ctx.setWindowTitle(QString("%1 (Scanning...)").arg(name));
  }
}

void DbEventsController::onCollectionScanCompletedStartup(const QString & /*uuid*/) {
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

void DbEventsController::onCollectionScanCompletedOverlay(const QString & /*uuid*/) {
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
  LoadingOverlay *overlay = m_ctx.getLoadingOverlay ? m_ctx.getLoadingOverlay() : nullptr;
  if (overlay && overlay->isActive()) {
    overlay->hide();
  }
  // When showAllSubcollectionItems is enabled and all descendant scans have
  // finished, reload the collection to get accurate counts. NavigationManager
  // skips intermediate reloads while the overlay is active, so we trigger the
  // final reload here after hiding it.
  const int curIndex = m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
  const auto *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  if (!collections || curIndex < 0 || curIndex >= collections->size() ||
      !(*collections)[curIndex].showAllSubcollectionItems) {
    return;
  }
  // Use a longer delay (150ms) to ensure the scan worker's commit is visible
  // to the query worker before we request counts.
  QTimer::singleShot(150, this, [this, curIndex]() {
    if (auto *nav = m_ctx.getNavigationManager ? m_ctx.getNavigationManager() : nullptr) {
      nav->safeReloadCollection(curIndex);
    }
  });
}

void DbEventsController::onScanItemsProgress(int itemsProcessed, int totalItems) {
  // Update progress during item scan/save.
  if (m_suppressStartupScanOverlays) {
    return;
  }
  LoadingOverlay *overlay = m_ctx.getLoadingOverlay ? m_ctx.getLoadingOverlay() : nullptr;
  if (!overlay || !overlay->isActive()) {
    return;
  }
  if (totalItems > 0) {
    // Indexing phase - we know the total
    overlay->setMessage(
        QString("Indexing %1 of %2 items...").arg(itemsProcessed).arg(totalItems));
    overlay->setProgress(itemsProcessed, totalItems);
  } else {
    // Scanning phase - total unknown, show count found so far
    overlay->setMessage(QString("Scanning... found %1 items").arg(itemsProcessed));
    // Show indeterminate progress (spinner continues)
    overlay->setProgress(0, 0);
  }
}

void DbEventsController::refreshCollectionSummaryOnScanCompleted(const QString & /*uuid*/) {
  if (auto *dpm = m_ctx.getDetailsPaneManager ? m_ctx.getDetailsPaneManager() : nullptr) {
    dpm->refreshCollectionSummary();
  }
}
