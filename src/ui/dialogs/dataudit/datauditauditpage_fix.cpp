// Sibling TU: fix + export for DatAuditAuditPage. Lives here: the Fix dialog
// flows (onFix from live results, fixProfile from a persisted snapshot, plus
// the seedFixDialogDefaults / offerRescrapeAfterFix helpers) and the export
// actions (exportTo + onExportCsv / onExportFixdat / onExportMissList).
// Profile UI + audit-run orchestration stay in datauditauditpage.cpp;
// results presentation lives in datauditauditpage_results.cpp.
#include "datauditauditpage.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSaveFile>

#include "datauditbrowsermodels.h" // DatAudit::auditRowsFromResults
#include "datauditexport.h"
#include "datauditfixdialog.h"
#include "datauditmodel.h"
#include "datauditprofilepanel.h"
#include "errorutils.h"

void DatAuditAuditPage::seedFixDialogDefaults(DatAuditFixDialog &dlg) {
  // Seed the managed-output option from the profile's persisted fix mode + root
  // (Kartend-m6qsb.14) instead of always starting blank.
  dlg.setManagedOutputDefaults(m_currentProfile.fixMode == DatAuditProfile::FixMode::ManagedOutput,
                               m_currentProfile.managedOutputRoot);
  // Seed the quarantine folder: the profile's per-collection root takes
  // precedence, falling back to the global default. Either may be empty.
  QString quarantineSeed = m_currentProfile.quarantineRoot.trimmed();
  if (quarantineSeed.isEmpty() && m_getQuarantineDefaultDir) {
    quarantineSeed = m_getQuarantineDefaultDir().trimmed();
  }
  dlg.setQuarantineDefault(quarantineSeed);
}

void DatAuditAuditPage::offerRescrapeAfterFix(const DatAuditFixDialog &dlg) {
  // After renames, the files carry their canonical names — the form
  // ScreenScraper matches best — so offer to re-scrape the linked collection
  // (Kartend-m6qsb.27). Only when collection-linked and a scraper hook exists.
  if (dlg.renamedCount() > 0 && !m_currentProfile.collectionUuid.isEmpty() &&
      m_openScraperForCollection) {
    const auto answer = QMessageBox::question(
        this, tr("Re-scrape renamed items"),
        tr("%n file(s) were renamed to their canonical names. Re-scrape this collection now? "
           "Canonical names match metadata much better.",
           nullptr, dlg.renamedCount()));
    if (answer == QMessageBox::Yes) {
      m_openScraperForCollection(m_currentProfile.collectionUuid);
    }
  }
}

void DatAuditAuditPage::onFix() {
  if (!hasResults()) {
    return;
  }
  DatAuditFixDialog dlg(m_model->allRows(), this);
  seedFixDialogDefaults(dlg);
  dlg.exec();
  if (!dlg.didApply()) {
    return;
  }
  onRun(); // re-audit so the table reflects the renamed/moved files
  offerRescrapeAfterFix(dlg);
}

void DatAuditAuditPage::fixProfile(qint64 profileId) {
  if (m_running) {
    QMessageBox::information(this, tr("DAT Audit"),
                             tr("An audit is already running — wait for it to finish."));
    return;
  }
  // Load the persisted profile so the Fix dialog gets this profile's
  // managed-output / quarantine defaults, and the post-apply re-audit runs
  // against the profile's saved DATs/roots (not unsaved on-screen edits).
  if (!loadProfileFromDb(profileId)) {
    QMessageBox::information(this, tr("DAT Audit"), tr("Couldn't load that profile."));
    return;
  }
  // Keep the combo display in sync (no-op-safe).
  m_profilePanel->selectProfileById(profileId);

  // Fix from the persisted snapshot (Kartend-7iqhl.2): reconstruct AuditRows
  // from the stored result rows rather than running a fresh audit first.
  QList<DatAuditProfile::ResultRow> rows;
  bool readOk = false;
  if (auto r = m_profileController.loadResultRows(profileId); r.isOk()) {
    rows = r.value();
    readOk = true;
  } else {
    ErrorUtils::logError(r.error());
  }
  const QList<DatAudit::AuditRow> auditRows = DatAudit::auditRowsFromResults(rows);
  if (auditRows.isEmpty()) {
    // Distinguish a genuinely empty (never-audited) profile from a failed read,
    // so a DB error isn't reported as "nothing to fix".
    QMessageBox::information(this, tr("DAT Audit"),
                             readOk ? tr("This profile has no audited results to fix yet.")
                                    : tr("Couldn't read this profile's audit results."));
    return;
  }

  DatAuditFixDialog dlg(auditRows, this);
  seedFixDialogDefaults(dlg);
  dlg.exec();
  if (!dlg.didApply()) {
    return;
  }
  // Re-audit the profile so the browser reflects the renamed/moved files.
  if (!scanRoots().isEmpty() && !datPaths().isEmpty()) {
    m_browserAuditPending = true;
    onRun();
  } else {
    emit browserNodeRefreshRequested(profileId); // can't re-audit; re-read the snapshot
  }
  offerRescrapeAfterFix(dlg);
}

void DatAuditAuditPage::exportTo(const QString &caption, const QString &filter,
                                 const QByteArray &bytes) {
  const QString path = QFileDialog::getSaveFileName(this, caption, QString(), filter);
  if (path.isEmpty()) {
    return;
  }
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
    QMessageBox::warning(this, tr("DAT Audit"), tr("Could not write %1").arg(path));
  }
}

void DatAuditAuditPage::onExportCsv() {
  exportTo(tr("Export CSV"), tr("CSV (*.csv)"), DatAudit::toCsv(m_model->allRows()));
}

void DatAuditAuditPage::onExportFixdat() {
  exportTo(tr("Export fixdat"), tr("DAT files (*.dat *.xml)"),
           DatAudit::toFixdat(m_model->allRows()));
}

void DatAuditAuditPage::onExportMissList() {
  exportTo(tr("Export miss list"), tr("Text (*.txt)"),
           DatAudit::toMissList(m_model->allRows()).toUtf8());
}
