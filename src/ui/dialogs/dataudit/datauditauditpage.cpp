#include "datauditauditpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "datauditbrowsermodels.h" // DatAudit::auditRowsFromResults
#include "datauditexport.h"
#include "datauditfixdialog.h"
#include "datauditmodel.h"
#include "datauditprofile.h"
#include "datauditprofilepanel.h"
#include "datcache.h"
#include "errorutils.h"
#include "pathutils.h"

using DatAudit::AuditOutput;
using DatAudit::AuditSummary;
using DatAudit::DatAuditModel;
using DatAudit::Status;

DatAuditAuditPage::DatAuditAuditPage(DatAuditProfileStore &profileStore, QWidget *parent)
    : QWidget(parent), m_profileController(profileStore) {
  m_model = new DatAuditModel(this);
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  // buildAuditPage (sibling TU datauditauditpage_build.cpp) constructs the
  // profile panel too; the panel loads the saved-profile combo itself.
  root->addWidget(buildAuditPage(profileStore));
  wireProfilePanel();
  wireAuditActions();
  setBusy(false);
}

void DatAuditAuditPage::setCollections(QList<CollectionConfig> *collections) {
  m_collections = collections;
  m_profilePanel->setCollections(collections);
}

void DatAuditAuditPage::setQuarantineDefaultProvider(std::function<QString()> provider) {
  m_getQuarantineDefaultDir = std::move(provider);
}

void DatAuditAuditPage::setScraperOpener(std::function<void(const QString &)> opener) {
  m_openScraperForCollection = std::move(opener);
}

namespace {

// Status sets for each filter combo entry. Index 0 ("All") => no filter.
struct FilterEntry {
  const char *label;
  std::optional<QSet<Status>> statuses;
};

QList<FilterEntry> filterEntries() {
  return {
      {QT_TR_NOOP("All"), std::nullopt},
      // Framing presets: file-centric ("the files I own") vs entry-centric
      // ("how complete is the catalogue"). Both are just status subsets.
      {QT_TR_NOOP("Files I own"),
       QSet<Status>{Status::Have, Status::WrongName, Status::WrongHash, Status::Duplicate,
                    Status::Unknown, Status::Corrupt}},
      {QT_TR_NOOP("Catalogue completeness"),
       QSet<Status>{Status::Have, Status::WrongName, Status::Missing}},
      // Individual statuses.
      {QT_TR_NOOP("Have"), QSet<Status>{Status::Have}},
      {QT_TR_NOOP("Missing"), QSet<Status>{Status::Missing}},
      {QT_TR_NOOP("Wrong name"), QSet<Status>{Status::WrongName}},
      {QT_TR_NOOP("Wrong content"), QSet<Status>{Status::WrongHash}},
      {QT_TR_NOOP("Duplicate"), QSet<Status>{Status::Duplicate}},
      {QT_TR_NOOP("Unknown"), QSet<Status>{Status::Unknown}},
      {QT_TR_NOOP("Corrupt"), QSet<Status>{Status::Corrupt}},
  };
}

// Resolve the collection a profile links to, by the same canonical-uuid
// computation the rest of the app keys on (name + expanded media dir).
// Returns nullptr when the uuid matches no live collection — deleted,
// renamed before the uuid migration ran, or no list was injected.
const CollectionConfig *resolveLinkedCollection(const QList<CollectionConfig> *collections,
                                                const QString &uuid) {
  // CollectionUtils::findByUuid does the loop-and-match and the empty-uuid
  // guard; the null-list guard stays here (Kartend audit D-07).
  return collections ? CollectionUtils::findByUuid(*collections, uuid) : nullptr;
}

} // namespace

void DatAuditAuditPage::populateFilterCombo() {
  // Called from buildAuditPage (sibling TU); the entries stay file-local here,
  // next to onFilterChanged's use of the same list.
  for (const auto &e : filterEntries()) {
    m_filterCombo->addItem(tr(e.label));
  }
}

void DatAuditAuditPage::wireProfilePanel() {
  // The panel owns the profile combo + CRUD flows; the page adopts each
  // announced outcome into its working profile. The providers hand the flows
  // their inputs: Edit/Duplicate seed from the full on-screen state
  // (uiProfile()), while Rename persists the working profile WITHOUT unsaved
  // list edits — the post-persist reload has always reverted those.
  m_profilePanel->setWorkingProfileProvider([this] { return uiProfile(); });
  m_profilePanel->setBaseProfileProvider([this] { return m_currentProfile; });
  connect(m_profilePanel, &DatAuditProfilePanel::profileChanged, this,
          &DatAuditAuditPage::adoptProfile);
  connect(m_profilePanel, &DatAuditProfilePanel::unsavedSelected, this, [this] {
    m_currentProfile = DatAuditProfile::Profile{}; // "(unsaved)" — keep the ad-hoc lists
    updateLinkedUiState();
  });
  connect(m_profilePanel, &DatAuditProfilePanel::profileDeleted, this,
          [this] { m_currentProfile = DatAuditProfile::Profile{}; });
}

void DatAuditAuditPage::wireAuditActions() {
  connect(m_addDatButton, &QPushButton::clicked, this, &DatAuditAuditPage::onAddDat);
  connect(m_removeDatButton, &QPushButton::clicked, this, &DatAuditAuditPage::onRemoveDat);
  connect(m_addRootButton, &QPushButton::clicked, this, &DatAuditAuditPage::onAddRoot);
  connect(m_removeRootButton, &QPushButton::clicked, this, &DatAuditAuditPage::onRemoveRoot);
  connect(m_detectLayoutButton, &QPushButton::clicked, this, [this] { runLayoutDetection(false); });
  connect(m_applyLayoutButton, &QPushButton::clicked, this,
          &DatAuditAuditPage::applyDetectedLayout);
  connect(m_dismissLayoutButton, &QPushButton::clicked, this, [this] {
    m_pendingDetection = DatAudit::LayoutDetection{};
    m_layoutBanner->setVisible(false);
    m_applyLayoutButton->setVisible(false);
    m_dismissLayoutButton->setVisible(false);
  });
  connect(m_runButton, &QPushButton::clicked, this, &DatAuditAuditPage::onRun);
  connect(m_cancelButton, &QPushButton::clicked, this, &DatAuditAuditPage::onCancel);
  connect(m_filterCombo, &QComboBox::currentIndexChanged, this,
          &DatAuditAuditPage::onFilterChanged);
  connect(m_fixButton, &QPushButton::clicked, this, &DatAuditAuditPage::onFix);
  connect(m_exportCsvButton, &QPushButton::clicked, this, &DatAuditAuditPage::onExportCsv);
  connect(m_exportFixdatButton, &QPushButton::clicked, this, &DatAuditAuditPage::onExportFixdat);
  connect(m_exportMissButton, &QPushButton::clicked, this, &DatAuditAuditPage::onExportMissList);
  connect(&m_runController, &DatAuditRunController::progress, this,
          &DatAuditAuditPage::onAuditProgress);
  connect(&m_runController, &DatAuditRunController::snapshotPersisted, this,
          &DatAuditAuditPage::onSnapshotPersisted);
  connect(&m_runController, &DatAuditRunController::finished, this,
          &DatAuditAuditPage::onAuditFinished);
}

void DatAuditAuditPage::adoptProfile(const DatAuditProfile::Profile &profile) {
  // The panel announced a loaded/created/edited profile: it becomes the
  // working profile, and the input lists re-sync from it.
  m_currentProfile = profile;
  applyCollectionDerivation();
  syncUiFromProfile();
  updateLinkedUiState();
}

bool DatAuditAuditPage::loadProfileFromDb(qint64 id) {
  bool loaded = false;
  auto res = m_profileController.load(id);
  if (res.isOk()) {
    // Bind the success-payload optional once so the has_value() guard and the
    // dereference act on the same object (a second res.value() call reads as an
    // unchecked access to clang-tidy's flow analysis).
    const auto &profileOpt = res.value();
    if (profileOpt.has_value()) {
      m_currentProfile = *profileOpt;
      applyCollectionDerivation();
      syncUiFromProfile();
      loaded = true;
    }
  } else if (res.isError()) {
    ErrorUtils::logError(res.error());
  }
  return loaded;
}

void DatAuditAuditPage::applyCollectionDerivation() {
  const CollectionConfig *linked =
      resolveLinkedCollection(m_collections, m_currentProfile.collectionUuid);
  if (linked == nullptr) {
    return; // unlinked, or unresolved link — cached lists stay as fallback
  }
  // The scan folder and DAT files are both user-managed even for a linked
  // profile: seed each from the collection (media dir → scan root, configured
  // DATs → DAT list) the first time, but never overwrite a value the user has
  // since edited here, so a deliberate override survives reload.
  if (m_currentProfile.scanRoots.isEmpty()) {
    const QString scanRoot =
        PathUtils::expandPathWithoutExistenceCheck(linked->mediaDirectory, linked->name);
    if (!scanRoot.isEmpty()) {
      m_currentProfile.scanRoots = QStringList{scanRoot};
    }
  }
  if (m_currentProfile.dats.isEmpty()) {
    for (const QString &d : linked->scraperOverrides.datFilePaths) {
      if (!d.isEmpty()) {
        DatAuditProfile::DatRef ref;
        ref.path = d;
        m_currentProfile.dats.append(ref);
      }
    }
  }
}

void DatAuditAuditPage::updateLinkedUiState() {
  const bool linked = !m_currentProfile.collectionUuid.isEmpty();
  // The scan folder and DAT files stay user-editable even when linked — the
  // collection only seeds them. Nothing in the lists is locked; the hint just
  // explains the seeding.
  m_linkedHint->setVisible(linked);
}

void DatAuditAuditPage::runLayoutDetection(bool autoTriggered) {
  const QStringList roots = scanRoots();
  m_pendingDetection = DatAudit::LayoutDetection{};
  if (!roots.isEmpty()) {
    // The first root carries the collection's media dir in every seeded /
    // linked flow; multi-root ad-hoc profiles get the probe for their first
    // root, which is still the right default for the banner suggestion.
    m_pendingDetection = DatAudit::detectLayout(roots.first());
  }

  const QString token = DatAudit::layoutToken(m_pendingDetection.layout);
  const bool conclusive = m_pendingDetection.layout != DatAudit::Layout::Unknown;
  // The silent open-time probe only speaks up when it (a) concluded something
  // and (b) that something differs from what the profile already confirmed.
  if (autoTriggered && (!conclusive || (m_currentProfile.layoutConfirmed &&
                                        m_currentProfile.detectedLayout == token))) {
    return;
  }

  QString text;
  switch (m_pendingDetection.layout) {
  case DatAudit::Layout::Flat:
    text = tr("Flat layout detected — audit can skip subfolders.");
    break;
  case DatAudit::Layout::Nested:
    text = tr("Nested layout detected — subfolders will be scanned.");
    break;
  case DatAudit::Layout::ArchivePerItem:
    text = tr("Archive-per-item layout detected — audit archive contents.");
    break;
  case DatAudit::Layout::SubfolderPerItem:
    text = tr("Subfolder-per-item layout detected — subfolders will be scanned.");
    break;
  case DatAudit::Layout::Mixed:
    text = tr("Mixed layout detected — full recursive scan recommended.");
    break;
  case DatAudit::Layout::Unknown:
    text = tr("Could not determine the folder structure.");
    break;
  }
  m_layoutBanner->setText(text);
  m_layoutBanner->setToolTip(m_pendingDetection.evidence);
  m_layoutBanner->setVisible(true);
  m_applyLayoutButton->setVisible(conclusive);
  m_dismissLayoutButton->setVisible(true);
}

void DatAuditAuditPage::applyDetectedLayout() {
  if (m_pendingDetection.layout == DatAudit::Layout::Unknown) {
    return;
  }
  m_currentProfile.detectedLayout = DatAudit::layoutToken(m_pendingDetection.layout);
  m_currentProfile.layoutConfirmed = true;
  if (m_currentProfile.id >= 0) {
    // Through uiProfile() so the on-screen DAT/root lists ride along — writing
    // m_currentProfile directly would silently revert unsaved list edits.
    DatAuditProfile::Profile p = uiProfile();
    m_profilePanel->persistProfile(p);
  }
  m_layoutBanner->setVisible(false);
  m_applyLayoutButton->setVisible(false);
  m_dismissLayoutButton->setVisible(false);
}

void DatAuditAuditPage::syncUiFromProfile() {
  const QSignalBlocker bd(m_datList);
  const QSignalBlocker br(m_rootList);
  m_datList->clear();
  // Item text stays the raw path — datPaths()/findItems() round-trip on it.
  // The catalogue identity (header name / version / entry count) goes in the
  // tooltip, served by a read-only cache peek so never-ingested DATs simply
  // show no tooltip rather than triggering a parse here on the UI thread.
  DatCache::Store cache(DatCache::defaultPath());
  for (const DatAuditProfile::DatRef &d : m_currentProfile.dats) {
    auto *item = new QListWidgetItem(d.path, m_datList);
    if (const auto src = cache.peek(d.path)) {
      const QString label =
          src->headerName.isEmpty() ? QFileInfo(d.path).fileName() : src->headerName;
      const QString version =
          src->headerVersion.isEmpty() ? QString() : tr(" (%1)").arg(src->headerVersion);
      item->setToolTip(tr("%1%2 — %n entries", nullptr, src->recordCount).arg(label, version));
    }
  }
  m_rootList->clear();
  m_rootList->addItems(m_currentProfile.scanRoots);
}

DatAuditProfile::Profile DatAuditAuditPage::uiProfile() const {
  // m_currentProfile holds the settings (region/1G1R/merge/fix/ignore/name);
  // the DAT + folder lists are the live edit surface for paths, so read them
  // back here so a run/save/edit always reflects what's on screen.
  DatAuditProfile::Profile p = m_currentProfile;
  p.dats.clear();
  for (const QString &d : datPaths()) {
    DatAuditProfile::DatRef ref;
    ref.path = d;
    // Carry the cached mtime/dialect/record-count over for paths the profile
    // already knew — rebuilding refs path-only wiped the staleness hints on
    // every save (Kartend-m6qsb.1). persistProfile re-stats before writing.
    for (const DatAuditProfile::DatRef &known : m_currentProfile.dats) {
      if (known.path == d) {
        ref = known;
        break;
      }
    }
    p.dats.append(ref);
  }
  p.scanRoots = scanRoots();
  return p;
}

void DatAuditAuditPage::openForCollection(const QString &collectionUuid,
                                          const QString &collectionName, const QString &mediaDir,
                                          const QStringList &datPaths) {
  // Refresh the combo so a newly-linked profile is selectable.
  m_profilePanel->reloadProfiles();

  qint64 linkedId = -1;
  if (!collectionUuid.isEmpty()) {
    // Deterministic: most-recently-updated linked profile wins (the old
    // listAll() scan picked whatever sorted first by name).
    auto linked = m_profileController.loadByCollectionUuid(collectionUuid);
    if (linked.isOk()) {
      // Same single-bind guard as loadProfileFromDb: check and dereference the
      // one optional, not two separate value() calls.
      const auto &linkedOpt = linked.value();
      if (linkedOpt.has_value()) {
        linkedId = linkedOpt->id;
      }
    }
  }

  if (linkedId >= 0) {
    // → the panel announces the load; adoptProfile derives and syncs the UI.
    m_profilePanel->selectProfileById(linkedId);
    // The settings panel hands over its WORKING copy — unsaved media-dir /
    // DAT-list edits included (Kartend-6wn0p) — so it overrides the
    // saved-collection derivation adoptProfile just applied. Same
    // derivation rule, fresher inputs; nothing is persisted by opening.
    if (!mediaDir.isEmpty()) {
      m_currentProfile.scanRoots = QStringList{mediaDir};
    }
    m_currentProfile.dats.clear();
    for (const QString &d : datPaths) {
      if (!d.isEmpty()) {
        DatAuditProfile::DatRef ref;
        ref.path = d;
        m_currentProfile.dats.append(ref);
      }
    }
    syncUiFromProfile();
    updateLinkedUiState();
    if (!m_currentProfile.layoutConfirmed) {
      runLayoutDetection(true); // suggestion only; nothing applies silently
    }
    return;
  }

  // No linked profile yet: seed an unsaved working profile aimed at the
  // collection so the audit is pre-populated. Select the "(unsaved)" row
  // silently first — the panel's selection flow would otherwise announce a
  // cleared profile and discard the seed.
  DatAuditProfile::Profile seed;
  seed.name = collectionName;
  seed.collectionUuid = collectionUuid;
  if (!mediaDir.isEmpty()) {
    seed.scanRoots = QStringList{mediaDir};
  }
  for (const QString &d : datPaths) {
    if (!d.isEmpty()) {
      DatAuditProfile::DatRef ref;
      ref.path = d;
      seed.dats.append(ref);
    }
  }
  m_profilePanel->selectUnsavedSilently();
  m_currentProfile = seed;
  syncUiFromProfile(); // reflect the seeded DAT + scan-root lists on screen
  updateLinkedUiState();
  runLayoutDetection(true); // fresh seed never has a confirmed layout
}

QStringList DatAuditAuditPage::datPaths() const {
  QStringList out;
  for (int i = 0; i < m_datList->count(); ++i) {
    out << m_datList->item(i)->text();
  }
  return out;
}

QStringList DatAuditAuditPage::scanRoots() const {
  QStringList out;
  for (int i = 0; i < m_rootList->count(); ++i) {
    out << m_rootList->item(i)->text();
  }
  return out;
}

bool DatAuditAuditPage::hasResults() const {
  return !m_model->allRows().isEmpty();
}

bool DatAuditAuditPage::hasApplicableFixes() const {
  // The fix engine acts on exactly these statuses: WrongName (rename), and
  // Unknown / WrongHash (quarantine). Duplicate/Corrupt/Missing produce no
  // action, so they must not light the Fix button.
  for (const DatAudit::AuditRow &r : m_model->allRows()) {
    if (r.status == DatAudit::Status::WrongName || r.status == DatAudit::Status::Unknown ||
        r.status == DatAudit::Status::WrongHash) {
      return true;
    }
  }
  return false;
}

void DatAuditAuditPage::onAddDat() {
  const QStringList files = QFileDialog::getOpenFileNames(
      this, tr("Add DAT files"), QString(), tr("DAT files (*.dat *.xml);;All files (*)"));
  for (const QString &f : files) {
    if (m_datList->findItems(f, Qt::MatchExactly).isEmpty()) {
      m_datList->addItem(f);
    }
  }
}

void DatAuditAuditPage::onRemoveDat() {
  qDeleteAll(m_datList->selectedItems());
}

void DatAuditAuditPage::onAddRoot() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Add scan folder"));
  if (!dir.isEmpty() && m_rootList->findItems(dir, Qt::MatchExactly).isEmpty()) {
    m_rootList->addItem(dir);
  }
}

void DatAuditAuditPage::onRemoveRoot() {
  qDeleteAll(m_rootList->selectedItems());
}

void DatAuditAuditPage::onRun() {
  if (m_running) {
    return;
  }
  const QStringList dats = datPaths();
  const QStringList roots = scanRoots();
  if (roots.isEmpty()) {
    QMessageBox::information(this, tr("DAT Audit"), tr("Add at least one folder to scan."));
    return;
  }
  if (dats.isEmpty()) {
    QMessageBox::information(this, tr("DAT Audit"),
                             tr("Add at least one DAT file to audit against."));
    return;
  }

  // 1G1R / ignore settings come from the selected profile (Kartend-bmj1ko); the
  // DAT + folder paths come from the live lists above.
  const DatAuditProfile::Profile p = uiProfile();
  const QStringList regionPrefs = p.regionPrefs;
  const QStringList ignore = p.ignoreRules;
  const bool onePerGame = p.onePerGame;
  // Only a user-CONFIRMED layout changes scan semantics (Kartend-m6qsb.6);
  // an unconfirmed detection is just the banner waiting for an answer.
  const DatAudit::Layout layout =
      p.layoutConfirmed ? DatAudit::layoutFromToken(p.detectedLayout) : DatAudit::Layout::Unknown;
  // Clone/parent expected-file model (Kartend-m6qsb.29). Empty/unknown -> Split.
  const DatAudit::MergeMode mergeMode = DatAudit::mergeModeFromToken(p.mergeMode);
  // "Verify (ignore cache)" force-rehash (Kartend-p30ic). Read on this (UI)
  // thread; passed by value into the worker. A null checkbox (test-built dialog)
  // reads as off — the default cached path.
  const bool ignoreHashCache = m_forceRehashCheck != nullptr && m_forceRehashCheck->isChecked();

  setBusy(true);

  // Hand the snapshotted inputs to the run controller (Kartend-ahf3d stage 3):
  // it owns the worker thread, the hash-cache DB connection, and the
  // QFutureWatcher, and reports back via onAuditProgress / onAuditFinished.
  DatAuditRunController::Request req;
  req.datPaths = dats;
  req.scanRoots = roots;
  req.regionPrefs = regionPrefs;
  req.ignoreGlobs = ignore;
  req.onePerGame = onePerGame;
  req.ignoreHashCache = ignoreHashCache;
  req.layout = layout;
  req.mergeMode = mergeMode;
  // Persist the snapshot + last-scan stamp for a SAVED profile so the
  // collection settings can show "last audited / have / missing" without
  // re-running anything (Kartend-m6qsb.8). The worker does the writes on its
  // own DB connection (Kartend-h7xnr.5) — a large snapshot replayed GUI-side
  // stalled the dialog at scan finish. Ad-hoc (unsaved) runs stay ephemeral
  // by design: id is -1, which the controller treats as "don't persist".
  req.persistProfileId = m_currentProfile.id;
  m_runController.start(req);
}

void DatAuditAuditPage::onCancel() {
  m_runController.cancel();
  m_cancelButton->setEnabled(false);
}

void DatAuditAuditPage::onAuditProgress(const DatAudit::AuditProgress &p) {
  if (m_running && p.filesTotal > 0) {
    m_progress->setRange(0, p.filesTotal);
    m_progress->setValue(p.filesDone);
  }
  // Drive the browser's inline bar for a browser-initiated re-audit
  // (Kartend-7iqhl.3); no-op (total <= 0 / not pending) otherwise.
  if (m_browserAuditPending) {
    emit browserAuditProgress(p.filesDone, p.filesTotal);
  }
}

void DatAuditAuditPage::onSnapshotPersisted(qint64 profileId, qint64 whenMs) {
  // The worker committed the run's snapshot + last-scan stamp; mirror the
  // stamp into the working copy so the UI reflects it without a reload. The
  // id guard covers a profile switch during the run (the panel stays enabled
  // while an audit is in flight).
  if (profileId == m_currentProfile.id) {
    m_currentProfile.lastScanAtMs = whenMs;
  }
}

void DatAuditAuditPage::onAuditFinished(const DatAudit::AuditOutput &out) {
  m_model->setRows(out.rows);
  updateSummary(out.summary);
  if (out.cancelled) {
    m_summaryLabel->setText(m_summaryLabel->text() + tr("  (cancelled)"));
  }
  // The result snapshot for a saved profile was already persisted by the run
  // controller's worker (Kartend-h7xnr.5) — snapshotPersisted arrived before
  // this slot, so onSnapshotPersisted has stamped m_currentProfile. Only view
  // updates remain here.
  // Surface DATs that failed to load so the user knows the audit ran against a
  // partial catalogue (Kartend-2zcrz) — otherwise real games show as Missing /
  // files as Unknown with no explanation. Full list in the tooltip.
  if (!out.failedDats.isEmpty()) {
    m_summaryLabel->setText(m_summaryLabel->text() +
                            tr("   [!] %n DAT file(s) failed to load — results may be incomplete",
                               nullptr, static_cast<int>(out.failedDats.size())));
    m_summaryLabel->setToolTip(
        tr("DAT files that failed to load:\n%1").arg(out.failedDats.join(QLatin1Char('\n'))));
  }
  m_running = false;
  setBusy(false);

  // A browser-initiated re-audit (Kartend-7iqhl.2) updated this profile's
  // snapshot while the user is on the Browser page; rebuild its tree and keep
  // the just-audited profile selected. Cleared even on cancel so the flag never
  // leaks into the next (Audit-page) run.
  if (m_browserAuditPending) {
    m_browserAuditPending = false;
    emit browserNodeRefreshRequested(m_currentProfile.id);
  }
}

void DatAuditAuditPage::onFilterChanged(int index) {
  const auto entries = filterEntries();
  if (index >= 0 && index < entries.size()) {
    m_model->setVisibleStatuses(entries.at(index).statuses);
  }
}

void DatAuditAuditPage::updateSummary(const AuditSummary &s) {
  m_summaryLabel->setText(tr("Have %1 · Wrong name %2 · Wrong content %3 · Unknown %4 · "
                             "Missing %5 · Corrupt %6   (catalogue %7, files %8)")
                              .arg(s.have)
                              .arg(s.wrongName)
                              .arg(s.wrongHash)
                              .arg(s.unknown)
                              .arg(s.missing)
                              .arg(s.corrupt)
                              .arg(s.totalCatalogue)
                              .arg(s.totalFiles));
  // Completeness bar: present (Have + Wrong-name) over the catalogue total.
  const int present = s.present();
  if (s.totalCatalogue > 0) {
    m_completionBar->setRange(0, s.totalCatalogue);
    m_completionBar->setValue(present);
    QString tip = tr("%1 of %2 catalogue entries present").arg(present).arg(s.totalCatalogue);
    // Per-DAT breakdown (Kartend-m6qsb.15) — only when more than one DAT feeds
    // the catalogue, since a single source just restates the overall total.
    if (s.perSource.size() > 1) {
      for (const DatAudit::SourceCompleteness &sc : s.perSource) {
        tip += tr("\n%1: %2 / %3 present").arg(sc.name).arg(sc.present).arg(sc.total);
      }
    }
    m_completionBar->setToolTip(tip);
    m_completionBar->setVisible(true);
  } else {
    m_completionBar->setVisible(false);
  }
}

const DatAudit::AuditRow *DatAuditAuditPage::rowForProxyIndex(const QModelIndex &proxyIndex) const {
  if (!proxyIndex.isValid()) {
    return nullptr;
  }
  return m_model->rowAt(m_proxy->mapToSource(proxyIndex).row());
}

void DatAuditAuditPage::onResultDoubleClicked(const QModelIndex &index) {
  const DatAudit::AuditRow *row = rowForProxyIndex(index);
  if (row == nullptr || row->filePath.isEmpty()) {
    return;
  }
  // Reveal the file's containing folder in the system file manager.
  const QFileInfo fi(row->filePath);
  QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

void DatAuditAuditPage::onResultsContextMenu(const QPoint &pos) {
  const QModelIndex index = m_table->indexAt(pos);
  const DatAudit::AuditRow *row = rowForProxyIndex(index);
  if (row == nullptr) {
    return;
  }
  QMenu menu(this);
  if (!row->filePath.isEmpty()) {
    const QString path = row->filePath;
    menu.addAction(tr("Reveal in file manager"), this, [path] {
      QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
  }
  if (!row->expectedName.isEmpty()) {
    const QString name = row->expectedName;
    menu.addAction(tr("Copy canonical name"), this,
                   [name] { QApplication::clipboard()->setText(name); });
  }
  if (!row->actualName.isEmpty()) {
    const QString name = row->actualName;
    menu.addAction(tr("Copy file name"), this,
                   [name] { QApplication::clipboard()->setText(name); });
  }
  if (!menu.isEmpty()) {
    menu.exec(m_table->viewport()->mapToGlobal(pos));
  }
}

void DatAuditAuditPage::setBusy(bool busy) {
  m_running = busy;
  m_runButton->setEnabled(!busy);
  m_cancelButton->setEnabled(busy);
  if (m_forceRehashCheck != nullptr) {
    m_forceRehashCheck->setEnabled(!busy);
  }
  m_progress->setVisible(busy);
  if (busy) {
    // Indeterminate until the runner's first progress tick supplies a total
    // (catalogue build + enumeration happen before any per-file granularity).
    m_progress->setRange(0, 0);
  }
  // The main bar above lives on the Audit page; mirror onto the browser page's
  // inline bar when the run was launched from there (Kartend-7iqhl.3).
  if (m_browserAuditPending) {
    emit browserAuditRunningChanged(busy);
  }
  const bool canExport = !busy && hasResults();
  // Fix illuminates only when something is actually fixable in place; exports
  // (CSV / fixdat / miss-list) stay available for any result set.
  m_fixButton->setEnabled(canExport && hasApplicableFixes());
  m_exportCsvButton->setEnabled(canExport);
  m_exportFixdatButton->setEnabled(canExport);
  m_exportMissButton->setEnabled(canExport);
}

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

void DatAuditAuditPage::reauditProfile(qint64 profileId) {
  if (m_running) {
    QMessageBox::information(this, tr("DAT Audit"),
                             tr("An audit is already running — wait for it to finish."));
    return;
  }
  // Load the PERSISTED profile into the (hidden) Audit page so onRun() audits it
  // with the profile's own saved DATs / roots / region / ignore / layout — never
  // whatever unsaved edits happen to be on the Audit page's list widgets. A bare
  // selectProfileById() would no-op when the combo already sits on this profile,
  // leaving onRun() to read those live lists and overwrite the snapshot with
  // them (Kartend-7iqhl.2 review).
  if (!loadProfileFromDb(profileId)) {
    QMessageBox::information(this, tr("DAT Audit"), tr("Couldn't load that profile."));
    return;
  }
  // Keep the combo display in sync (no-op-safe).
  m_profilePanel->selectProfileById(profileId);
  if (scanRoots().isEmpty() || datPaths().isEmpty()) {
    QMessageBox::information(this, tr("DAT Audit"),
                             tr("This profile has no folders or DAT files to audit."));
    return;
  }
  m_browserAuditPending = true; // onAuditFinished refreshes + re-selects the node
  onRun();
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
