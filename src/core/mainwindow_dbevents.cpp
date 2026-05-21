// MainWindow's reactions to DatabaseManager scan/count signals.
//
// Extracted from mainwindow_wiring.cpp during the responsibility-based TU
// split. These slot handlers all share one concern: react to background
// scan / count progress and end-of-scan transitions by driving the
// loading overlay, the window title, the scan-suppression counters, and
// the post-scan collection-summary refresh.
//
// The signal/slot connect()s that wire DatabaseManager into these handlers
// continue to live in mainwindow_wiring.cpp's connectDatabaseManager().

#include <QApplication>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "navigationmanager.h"

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
