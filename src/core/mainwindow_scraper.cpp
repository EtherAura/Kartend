// MainWindow's marquee-shim entry points + IMainWindow forwarders to the
// extracted ScraperController (Kartend-hzef step 3).
//
// The scraper-flow methods (openScraperDialog + promptResumePendingScrapeIfAny)
// and the pickLookupProvider helper moved to scrapercontroller.{h,cpp}. The
// IMainWindow virtual stays here so callers reaching MainWindow through the
// interface (interaction context menu, menu lambda) don't need to know about
// the controller; it just forwards.
//
// The marquee forwarders are too small to need their own TU — they stay
// because MarqueeController is owned by MainWindow and these are the only
// other entry points in this file's surface area.

#include <QSqlDatabase>
#include <QStandardPaths>

#include "applicationmanager.h"
#include "collection/typehelpers.h"
#include "databaseschema.h"
#include "datauditcontroller.h"
#include "datauditprofile.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "pathutils.h"
#include "scrapercontroller.h"

void MainWindow::applyMarqueeSettings() {
  if (m_marqueeController) {
    m_marqueeController->applyMarqueeSettings();
  }
}

void MainWindow::updateMarqueeArtwork() {
  if (m_marqueeController) {
    m_marqueeController->updateMarqueeArtwork();
  }
}

void MainWindow::openScraperDialog(int preCollectionIndex, const QString &preItemPath) {
  if (m_scraperController) {
    m_scraperController->openScraperDialog(preCollectionIndex, preItemPath);
  }
}

void MainWindow::openDatAuditDialog() {
  if (m_datAuditController) {
    m_datAuditController->openDialog(); // generic launch (File → DAT Audit…)
  }
}

void MainWindow::openDatAuditForCollection(const CollectionConfig &collection) {
  if (!m_datAuditController) {
    return;
  }
  // Pass the canonical UUID (existence-checked expansion, matching the profile
  // editor's "Linked collection" picker so the link resolves) plus an expanded
  // media dir to seed as the scan root. `collection` is the settings panel's
  // working copy, so unsaved media-dir / DAT-list edits are honoured
  // (Kartend-6wn0p).
  const QString uuid = CollectionUtils::computeCollectionUuid(
      collection.name,
      PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name));
  const QString scanRoot =
      PathUtils::expandPathWithoutExistenceCheck(collection.mediaDirectory, collection.name);
  m_datAuditController->openForCollection(uuid, collection.name, scanRoot,
                                          collection.scraperOverrides.datFilePaths);
}

qint64 MainWindow::lastDatAuditMsForCollection(const QString &collectionUuid) {
  if (collectionUuid.isEmpty()) {
    return 0;
  }
  // Short-lived UI-thread connection to the app DB (same pattern as the audit
  // dialog's withProfileDb); the table is tiny and this is read on settings
  // open / collection switch, not in a hot path.
  static int counter = 0; // UI thread only
  const QString conn = QStringLiteral("mw_lastaudit_%1").arg(counter++);
  qint64 latestMs = 0;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (DatabaseSchema::openConnection(db, dir)) {
      DatabaseSchema::applyConnectionPragmas(db);
      auto all = DatAuditProfile::listAll(db);
      if (all.isOk()) {
        for (const DatAuditProfile::Profile &p : all.value()) {
          if (p.collectionUuid == collectionUuid && p.lastScanAtMs > latestMs) {
            latestMs = p.lastScanAtMs; // newest across all profiles linked to this collection
          }
        }
      }
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
  return latestMs;
}

void MainWindow::promptResumePendingScrapeIfAny() {
  if (m_scraperController) {
    m_scraperController->promptResumePendingScrapeIfAny();
  }
}
