#include "datauditdialog.h"

#include <QAtomicInteger>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTableView>
#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>

#include "databaseschema.h"
#include "datauditexport.h"
#include "datauditfixdialog.h"
#include "datauditmodel.h"
#include "datauditprofile.h"
#include "datcache.h"

using DatAudit::AuditOutput;
using DatAudit::AuditSummary;
using DatAudit::DatAuditModel;
using DatAudit::Status;

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

QGroupBox *makePathGroup(const QString &title, QListWidget *&list, QPushButton *&add,
                         QPushButton *&remove, const QString &addText) {
  auto *box = new QGroupBox(title);
  auto *v = new QVBoxLayout(box);
  list = new QListWidget(box);
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  v->addWidget(list);
  auto *row = new QHBoxLayout();
  add = new QPushButton(addText, box);
  remove = new QPushButton(QObject::tr("Remove"), box);
  row->addWidget(add);
  row->addWidget(remove);
  row->addStretch();
  v->addLayout(row);
  return box;
}

// Run `fn` with a transient connection to the main app DB (where the v17
// dat_audit_profile* tables live). Profile CRUD is light + occasional, so a
// short-lived UI-thread connection is fine; the connection is opened, used, and
// removed each call to avoid lifetime juggling. No-op if the DB can't be opened.
template <typename Fn> void withProfileDb(Fn &&fn) {
  static int counter = 0; // UI thread only
  const QString conn = QStringLiteral("dataudit_profiles_%1").arg(counter++);
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (DatabaseSchema::openConnection(db, dir)) {
      DatabaseSchema::applyConnectionPragmas(db);
      fn(db);
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

} // namespace

DatAuditDialog::DatAuditDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("DAT Audit"));
  resize(820, 560);

  m_model = new DatAuditModel(this);

  auto *root = new QVBoxLayout(this);

  // Saved profiles.
  auto *profileRow = new QHBoxLayout();
  profileRow->addWidget(new QLabel(tr("Profile:"), this));
  m_profileCombo = new QComboBox(this);
  m_saveProfileButton = new QPushButton(tr("Save as…"), this);
  m_deleteProfileButton = new QPushButton(tr("Delete"), this);
  profileRow->addWidget(m_profileCombo, 1);
  profileRow->addWidget(m_saveProfileButton);
  profileRow->addWidget(m_deleteProfileButton);
  root->addLayout(profileRow);

  // Inputs: DAT files + scan folders.
  auto *inputs = new QHBoxLayout();
  QPushButton *addDat = nullptr;
  QPushButton *removeDat = nullptr;
  QPushButton *addRoot = nullptr;
  QPushButton *removeRoot = nullptr;
  inputs->addWidget(makePathGroup(tr("DAT files"), m_datList, addDat, removeDat, tr("Add DAT…")));
  inputs->addWidget(
      makePathGroup(tr("Scan folders"), m_rootList, addRoot, removeRoot, tr("Add folder…")));
  root->addLayout(inputs);

  // Controls: run / cancel / progress / filter.
  auto *controls = new QHBoxLayout();
  m_runButton = new QPushButton(tr("Run audit"), this);
  m_cancelButton = new QPushButton(tr("Cancel"), this);
  m_cancelButton->setEnabled(false);
  m_progress = new QProgressBar(this);
  m_progress->setVisible(false);
  controls->addWidget(m_runButton);
  controls->addWidget(m_cancelButton);
  controls->addWidget(m_progress, 1);
  controls->addWidget(new QLabel(tr("View:"), this));
  m_filterCombo = new QComboBox(this);
  for (const auto &e : filterEntries()) {
    m_filterCombo->addItem(tr(e.label));
  }
  controls->addWidget(m_filterCombo);
  root->addLayout(controls);

  // Summary line.
  m_summaryLabel = new QLabel(tr("No audit run yet."), this);
  root->addWidget(m_summaryLabel);

  // Results table.
  m_table = new QTableView(this);
  m_table->setModel(m_model);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSortingEnabled(false);
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->setVisible(false);
  root->addWidget(m_table, 1);

  // Fix + exports.
  auto *exports = new QHBoxLayout();
  m_fixButton = new QPushButton(tr("Fix…"), this);
  exports->addWidget(m_fixButton);
  exports->addStretch();
  m_exportCsvButton = new QPushButton(tr("Export CSV…"), this);
  m_exportFixdatButton = new QPushButton(tr("Export fixdat…"), this);
  m_exportMissButton = new QPushButton(tr("Export miss list…"), this);
  exports->addWidget(m_exportCsvButton);
  exports->addWidget(m_exportFixdatButton);
  exports->addWidget(m_exportMissButton);
  root->addLayout(exports);

  connect(addDat, &QPushButton::clicked, this, &DatAuditDialog::onAddDat);
  connect(removeDat, &QPushButton::clicked, this, &DatAuditDialog::onRemoveDat);
  connect(addRoot, &QPushButton::clicked, this, &DatAuditDialog::onAddRoot);
  connect(removeRoot, &QPushButton::clicked, this, &DatAuditDialog::onRemoveRoot);
  connect(m_runButton, &QPushButton::clicked, this, &DatAuditDialog::onRun);
  connect(m_cancelButton, &QPushButton::clicked, this, &DatAuditDialog::onCancel);
  connect(m_filterCombo, &QComboBox::currentIndexChanged, this, &DatAuditDialog::onFilterChanged);
  connect(m_fixButton, &QPushButton::clicked, this, &DatAuditDialog::onFix);
  connect(m_exportCsvButton, &QPushButton::clicked, this, &DatAuditDialog::onExportCsv);
  connect(m_exportFixdatButton, &QPushButton::clicked, this, &DatAuditDialog::onExportFixdat);
  connect(m_exportMissButton, &QPushButton::clicked, this, &DatAuditDialog::onExportMissList);
  connect(&m_watcher, &QFutureWatcher<AuditOutput>::finished, this,
          &DatAuditDialog::onAuditFinished);
  connect(m_profileCombo, &QComboBox::currentIndexChanged, this,
          &DatAuditDialog::onProfileSelected);
  connect(m_saveProfileButton, &QPushButton::clicked, this, &DatAuditDialog::onSaveProfile);
  connect(m_deleteProfileButton, &QPushButton::clicked, this, &DatAuditDialog::onDeleteProfile);

  setBusy(false);
  loadProfiles();
}

void DatAuditDialog::loadProfiles() {
  const QSignalBlocker block(m_profileCombo);
  m_profileCombo->clear();
  m_profileCombo->addItem(tr("(unsaved)"), QVariant(qlonglong(-1)));
  withProfileDb([this](QSqlDatabase &db) {
    auto all = DatAuditProfile::listAll(db);
    if (all.isOk()) {
      for (const DatAuditProfile::Profile &p : all.value()) {
        m_profileCombo->addItem(p.name, QVariant(qlonglong(p.id)));
      }
    }
  });
}

void DatAuditDialog::onProfileSelected(int index) {
  if (index < 0) {
    return;
  }
  const qint64 id = m_profileCombo->itemData(index).toLongLong();
  if (id < 0) {
    return; // the "(unsaved)" entry — keep the current ad-hoc selections
  }
  withProfileDb([this, id](QSqlDatabase &db) {
    auto loaded = DatAuditProfile::load(db, id);
    if (loaded.isOk() && loaded.value().has_value()) {
      const DatAuditProfile::Profile &p = *loaded.value();
      m_datList->clear();
      for (const DatAuditProfile::DatRef &d : p.dats) {
        m_datList->addItem(d.path);
      }
      m_rootList->clear();
      for (const QString &r : p.scanRoots) {
        m_rootList->addItem(r);
      }
    }
  });
}

void DatAuditDialog::onSaveProfile() {
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Save profile"), tr("Profile name:"),
                                             QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  DatAuditProfile::Profile p;
  p.name = name.trimmed();
  p.scanRoots = scanRoots();
  for (const QString &d : datPaths()) {
    DatAuditProfile::DatRef ref;
    ref.path = d;
    p.dats.append(ref);
  }
  withProfileDb([this, &p](QSqlDatabase &db) {
    auto res = DatAuditProfile::insert(db, p);
    if (res.isError()) {
      QMessageBox::warning(this, tr("DAT Audit"),
                           tr("Could not save profile: %1").arg(res.error().message));
    }
  });
  loadProfiles();
  const int idx = m_profileCombo->findText(p.name);
  if (idx >= 0) {
    m_profileCombo->setCurrentIndex(idx);
  }
}

void DatAuditDialog::onDeleteProfile() {
  const qint64 id = m_profileCombo->itemData(m_profileCombo->currentIndex()).toLongLong();
  if (id < 0) {
    return;
  }
  withProfileDb([id](QSqlDatabase &db) {
    auto res = DatAuditProfile::remove(db, id);
    Q_UNUSED(res);
  });
  loadProfiles();
}

DatAuditDialog::~DatAuditDialog() {
  // If a run is in flight when the window is torn down, ask it to stop and wait
  // so the worker doesn't outlive the dialog it reports into.
  if (m_cancel) {
    m_cancel->store(true);
  }
  if (m_watcher.isRunning()) {
    m_watcher.waitForFinished();
  }
}

QStringList DatAuditDialog::datPaths() const {
  QStringList out;
  for (int i = 0; i < m_datList->count(); ++i) {
    out << m_datList->item(i)->text();
  }
  return out;
}

QStringList DatAuditDialog::scanRoots() const {
  QStringList out;
  for (int i = 0; i < m_rootList->count(); ++i) {
    out << m_rootList->item(i)->text();
  }
  return out;
}

bool DatAuditDialog::hasResults() const {
  return !m_model->allRows().isEmpty();
}

void DatAuditDialog::onAddDat() {
  const QStringList files = QFileDialog::getOpenFileNames(
      this, tr("Add DAT files"), QString(), tr("DAT files (*.dat *.xml);;All files (*)"));
  for (const QString &f : files) {
    if (m_datList->findItems(f, Qt::MatchExactly).isEmpty()) {
      m_datList->addItem(f);
    }
  }
}

void DatAuditDialog::onRemoveDat() {
  qDeleteAll(m_datList->selectedItems());
}

void DatAuditDialog::onAddRoot() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Add scan folder"));
  if (!dir.isEmpty() && m_rootList->findItems(dir, Qt::MatchExactly).isEmpty()) {
    m_rootList->addItem(dir);
  }
}

void DatAuditDialog::onRemoveRoot() {
  qDeleteAll(m_rootList->selectedItems());
}

void DatAuditDialog::onRun() {
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

  m_cancel = std::make_shared<std::atomic<bool>>(false);
  setBusy(true);

  auto cancel = m_cancel;
  auto future = QtConcurrent::run([dats, roots, cancel]() -> AuditOutput {
    // Both DB connections below are created, used, and removed on THIS worker
    // thread, satisfying QSqlDatabase's thread affinity.
    DatCache::Store cache(DatCache::defaultPath());
    QStringList failed;
    DatAudit::Catalogue cat = DatAudit::buildCatalogue(cache, dats, &failed);
    DatAudit::AuditOptions opts;
    opts.scanRoots = roots;
    opts.datPaths = dats;

    // Open a main-DB connection for the file-hash cache so re-audits skip
    // re-hashing unchanged files (the v17 file_hash_cache table already exists;
    // the app applied migrations at startup). WAL lets this coexist with the
    // DatabaseManager's own connection.
    static QAtomicInteger<quint64> hashConnCounter{0};
    const QString conn =
        QStringLiteral("dataudit_hashcache_%1").arg(hashConnCounter.fetchAndAddRelaxed(1));
    AuditOutput out;
    {
      QSqlDatabase hashDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
      QSqlDatabase *cacheDb = nullptr;
      if (DatabaseSchema::openConnection(
              hashDb, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))) {
        DatabaseSchema::applyConnectionPragmas(hashDb);
        cacheDb = &hashDb;
      }
      out = DatAudit::run(cat, opts, cacheDb, cancel, nullptr);
      hashDb.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return out;
  });
  m_watcher.setFuture(future);
}

void DatAuditDialog::onCancel() {
  if (m_cancel) {
    m_cancel->store(true);
  }
  m_cancelButton->setEnabled(false);
}

void DatAuditDialog::onAuditFinished() {
  const AuditOutput out = m_watcher.result();
  m_model->setRows(out.rows);
  updateSummary(out.summary);
  if (out.cancelled) {
    m_summaryLabel->setText(m_summaryLabel->text() + tr("  (cancelled)"));
  }
  m_running = false;
  setBusy(false);
}

void DatAuditDialog::onFilterChanged(int index) {
  const auto entries = filterEntries();
  if (index >= 0 && index < entries.size()) {
    m_model->setVisibleStatuses(entries.at(index).statuses);
  }
}

void DatAuditDialog::updateSummary(const AuditSummary &s) {
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
}

void DatAuditDialog::setBusy(bool busy) {
  m_running = busy;
  m_runButton->setEnabled(!busy);
  m_cancelButton->setEnabled(busy);
  m_progress->setVisible(busy);
  if (busy) {
    m_progress->setRange(0, 0); // busy indicator (no per-file granularity in v1)
  }
  const bool canExport = !busy && hasResults();
  m_fixButton->setEnabled(canExport);
  m_exportCsvButton->setEnabled(canExport);
  m_exportFixdatButton->setEnabled(canExport);
  m_exportMissButton->setEnabled(canExport);
}

void DatAuditDialog::onFix() {
  if (!hasResults()) {
    return;
  }
  DatAuditFixDialog dlg(m_model->allRows(), this);
  dlg.exec();
  if (dlg.didApply()) {
    onRun(); // re-audit so the table reflects the renamed/moved files
  }
}

void DatAuditDialog::exportTo(const QString &caption, const QString &filter,
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

void DatAuditDialog::onExportCsv() {
  exportTo(tr("Export CSV"), tr("CSV (*.csv)"), DatAudit::toCsv(m_model->allRows()));
}

void DatAuditDialog::onExportFixdat() {
  exportTo(tr("Export fixdat"), tr("DAT files (*.dat *.xml)"),
           DatAudit::toFixdat(m_model->allRows()));
}

void DatAuditDialog::onExportMissList() {
  exportTo(tr("Export miss list"), tr("Text (*.txt)"),
           DatAudit::toMissList(m_model->allRows()).toUtf8());
}
