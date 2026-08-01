#include "datauditcontroller.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <Qt>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QWidget>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "databaseschema.h"
#include "datauditdialog.h"
#include "datlibraryreviewdialog.h"
#include "datlibrarystate.h"
#include "datpackimport.h"
#include "pathutils.h"

namespace {

// Run `fn` with a transient connection to the main app DB (dismissal state
// lives beside the dat_audit_profile tables). Same throwaway-connection shape
// as the audit dialog's withProfileDb — light, occasional access.
template <typename Fn> void withAppDb(Fn &&fn) {
  static int counter = 0; // UI thread only
  const QString conn = QStringLiteral("dataudit_library_%1").arg(counter++);
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

DatAuditController::DatAuditController(QObject *parent) : QObject(parent) {
  connect(&m_libraryWatcher, &QFutureWatcher<DatLibraryScan::ScanResult>::finished, this, [this] {
    m_startupProposals = m_libraryWatcher.result();
    // Non-nagging (Kartend-m6qsb.10): only announce proposals not already
    // surfaced this session — a live rescan after an unrelated folder change
    // must not re-pop the same hint. Keys on canonicalPath+mtime so a genuinely
    // updated catalogue (new mtime) does re-announce.
    int fresh = 0;
    for (const DatLibraryScan::Proposal &p : m_startupProposals.proposals) {
      const QString key = p.canonicalPath + QLatin1Char('@') + QString::number(p.mtimeMs);
      if (!m_notifiedKeys.contains(key)) {
        ++fresh;
        m_notifiedKeys.insert(key);
      }
    }
    if (fresh > 0 && m_ctx.showStatusMessage) {
      m_ctx.showStatusMessage(
          tr("%n new DAT catalogue(s) match your collections — File → DAT Audit → Library…",
             nullptr, fresh));
    }
  });

  // Live filesystem watching of the DAT-library folder (Kartend-m6qsb.10):
  // mirror the collection watcher's debounced pattern — a directory change
  // restarts a short timer, and only the quiet point after a burst (e.g. a
  // multi-file copy) triggers one off-thread rescan.
  m_libraryFsWatcher = new QFileSystemWatcher(this);
  m_libraryDebounce = new QTimer(this);
  m_libraryDebounce->setSingleShot(true);
  m_libraryDebounce->setInterval(1500);
  connect(m_libraryFsWatcher, &QFileSystemWatcher::directoryChanged, this,
          [this] { m_libraryDebounce->start(); });
  connect(m_libraryDebounce, &QTimer::timeout, this, &DatAuditController::startupLibraryScan);
}

DatAuditController::~DatAuditController() {
  // Don't let a startup scan outlive the controller mid-teardown.
  m_libraryWatcher.cancel();
  m_libraryWatcher.waitForFinished();
}

void DatAuditController::setContext(const DatAuditControllerContext &context) {
  m_ctx = context;
}

DatAuditDialog *DatAuditController::ensureDialog() {
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  if (!m_dialog) {
    m_dialog = new DatAuditDialog(parent);
    m_dialog->setWindowFlag(Qt::Window, true);
    m_dialog->setModal(false);
  }
  // Refresh the borrowed collection list on every open so the profile editor's
  // "Linked collection" picker reflects collections added/removed since.
  m_dialog->setCollections(m_ctx.getCollections ? m_ctx.getCollections() : nullptr);
  m_dialog->setLibraryOpener([this] { openLibraryReview(); });
  // Library-folder accessors power both the Library page display and the
  // Download page's import destination (Kartend-m6qsb.16). Only wired when the
  // host provided them.
  if (m_ctx.getDatLibraryPath) {
    // Route saves through persistLibraryPath so the live watch follows the
    // folder when the user changes it (Kartend-m6qsb.10).
    m_dialog->setLibraryPathAccessors(m_ctx.getDatLibraryPath,
                                      [this](const QString &root) { persistLibraryPath(root); });
    m_dialog->setImportHandler([this](const QString &path) { importDatPack(path); });
  }
  // Pass through the scraper opener (Kartend-m6qsb.27) — null when the host
  // didn't provide one, which the dialog treats as "no re-scrape offer".
  m_dialog->setScraperOpener(m_ctx.openScraperForCollection);
  // Global default quarantine folder for the Fix dialog (null = no default).
  m_dialog->setQuarantineDefaultProvider(m_ctx.getQuarantineDefaultDir);
  adoptModalParentIfNeeded(m_dialog);
  return m_dialog;
}

// Kartend-j6a00: SettingsDialog is application-modal (setModal(true) +
// exec()), and an application-modal window blocks input to every window
// except its own children. The audit dialog is parented to the main window,
// so its prominent "Open DAT Audit" entry point in the settings Behavior
// panel produced a window that appeared but was frozen until Settings
// closed. While a modal is active, transient-parent the dialog to it — Qt
// then exempts it from the block and the live-unsaved-edits handover flow
// works as designed.
//
// QWidget parents OWN their top-level children, and this dialog is a
// long-lived cache that must outlive the settings session, so the parent is
// handed back to the main window the moment the modal finishes or starts
// destruction (~QObject emits destroyed() BEFORE deleting children, so the
// rescue reparent runs in time on that path too).
void DatAuditController::adoptModalParentIfNeeded(DatAuditDialog *dialog) {
  QWidget *modal = QApplication::activeModalWidget();
  modal = modal ? modal->window() : nullptr;
  if (modal == nullptr || modal == dialog) {
    return;
  }
  if (m_modalParent == modal) {
    return; // second open from the same modal session — already adopted
  }
  m_modalParent = modal;
  const QRect geometry = dialog->geometry();
  const bool visible = dialog->isVisible();
  dialog->setParent(modal, dialog->windowFlags()); // setParent hides; re-show below
  if (visible) {
    dialog->setGeometry(geometry);
    dialog->show();
  }
  if (auto *modalDialog = qobject_cast<QDialog *>(modal)) {
    connect(modalDialog, &QDialog::finished, this, &DatAuditController::restoreBaseParent);
  }
  connect(modal, &QObject::destroyed, this, &DatAuditController::restoreBaseParent);
}

void DatAuditController::restoreBaseParent() {
  m_modalParent = nullptr;
  if (!m_dialog) {
    return;
  }
  QWidget *base = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  if (m_dialog->parentWidget() == base) {
    return; // finished + destroyed both route here; second call is a no-op
  }
  const QRect geometry = m_dialog->geometry();
  const bool visible = m_dialog->isVisible();
  m_dialog->setParent(base, m_dialog->windowFlags());
  if (visible) {
    m_dialog->setGeometry(geometry);
    m_dialog->show();
  }
}

void DatAuditController::importDatPack(const QString &sourcePath) {
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  QString lib = m_ctx.getDatLibraryPath ? m_ctx.getDatLibraryPath().trimmed() : QString();
  if (lib.isEmpty()) {
    // No library yet — ask once and remember it, so imports have a home.
    lib = QFileDialog::getExistingDirectory(parent, tr("Choose a DAT library folder"));
    if (lib.isEmpty()) {
      return;
    }
    persistLibraryPath(lib); // saves + starts watching the new library
  }
  auto res = DatPackImport::importInto(sourcePath, lib);
  if (res.isError()) {
    QMessageBox::warning(parent, tr("Import DATs"), res.error().message);
    return;
  }
  if (m_ctx.showStatusMessage) {
    m_ctx.showStatusMessage(tr("Imported %n DAT file(s) into the library.", nullptr,
                               static_cast<int>(res.value().size())));
  }
  // Surface matches for the freshly-imported catalogues.
  openLibraryReview();
}

void DatAuditController::openDialog() {
  DatAuditDialog *dialog = ensureDialog();
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void DatAuditController::openForCollection(const QString &collectionUuid,
                                           const QString &collectionName, const QString &mediaDir,
                                           const QStringList &datPaths) {
  DatAuditDialog *dialog = ensureDialog();
  dialog->openForCollection(collectionUuid, collectionName, mediaDir, datPaths);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

QList<DatCollectionMatch::CollectionInfo> DatAuditController::collectionInfos() const {
  QList<DatCollectionMatch::CollectionInfo> out;
  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  if (collections == nullptr) {
    return out;
  }
  out.reserve(collections->size());
  for (const CollectionConfig &c : *collections) {
    DatCollectionMatch::CollectionInfo info;
    const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
    info.uuid = CollectionUtils::computeCollectionUuid(c.name, expanded);
    info.name = c.name;
    info.extensions = c.extensions;
    info.attachedDatPaths = c.scraperOverrides.datFilePaths;
    out.append(info);
  }
  return out;
}

DatLibraryScan::ScanResult DatAuditController::scanLibrary(const QString &root) const {
  QSet<QString> dismissed;
  withAppDb([&dismissed](QSqlDatabase &db) {
    if (auto keys = DatLibraryState::loadDismissalKeys(db); keys.isOk()) {
      dismissed = keys.value();
    }
  });
  return DatLibraryScan::scan(root, collectionInfos(), dismissed);
}

void DatAuditController::persistLibraryPath(const QString &root) {
  if (m_ctx.saveDatLibraryPath) {
    m_ctx.saveDatLibraryPath(root);
  }
  updateLibraryWatch(); // follow the library wherever the user points it
}

void DatAuditController::updateLibraryWatch() {
  const QString root = m_ctx.getDatLibraryPath ? m_ctx.getDatLibraryPath().trimmed() : QString();
  if (root == m_watchedRoot) {
    return; // already watching the right place
  }
  if (!m_libraryFsWatcher->directories().isEmpty()) {
    m_libraryFsWatcher->removePaths(m_libraryFsWatcher->directories());
  }
  m_watchedRoot = root;
  m_notifiedKeys.clear(); // a different library announces its matches fresh
  if (root.isEmpty() || !QFileInfo(root).isDir()) {
    return;
  }
  // Watch the root and one level of subfolders. QFileSystemWatcher isn't
  // recursive; DAT libraries are conventionally flat or shallow, and the
  // startup / manual rescan still covers anything deeper.
  QStringList toWatch{root};
  QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
  while (it.hasNext()) {
    toWatch.append(it.next());
  }
  m_libraryFsWatcher->addPaths(toWatch);
}

void DatAuditController::startupLibraryScan() {
  updateLibraryWatch(); // begin (or keep) watching whenever a scan is kicked off
  const QString root = m_ctx.getDatLibraryPath ? m_ctx.getDatLibraryPath() : QString();
  if (root.trimmed().isEmpty() || m_libraryWatcher.isRunning()) {
    return;
  }
  // Snapshot the inputs on the UI thread — the worker must not touch the
  // live collection list or open UI-thread DB connections.
  const QList<DatCollectionMatch::CollectionInfo> collections = collectionInfos();
  QSet<QString> dismissed;
  withAppDb([&dismissed](QSqlDatabase &db) {
    if (auto keys = DatLibraryState::loadDismissalKeys(db); keys.isOk()) {
      dismissed = keys.value();
    }
  });
  m_libraryWatcher.setFuture(QtConcurrent::run([root, collections, dismissed] {
    return DatLibraryScan::scan(root, collections, dismissed);
  }));
}

void DatAuditController::openLibraryReview() {
  const QString root = m_ctx.getDatLibraryPath ? m_ctx.getDatLibraryPath() : QString();
  // Use the startup scan's proposals when fresh ones exist; otherwise scan
  // now (cheap header probes) so the dialog always opens with live data.
  DatLibraryScan::ScanResult initial = m_startupProposals;
  if (initial.proposals.isEmpty() && !root.trimmed().isEmpty()) {
    initial = scanLibrary(root);
  }
  m_startupProposals = DatLibraryScan::ScanResult{}; // consumed

  DatLibraryReviewDialog::Hooks hooks;
  hooks.attach = m_ctx.attachDatToCollection;
  hooks.dismiss = [](const QString &canonicalPath, qint64 mtimeMs) {
    withAppDb([&](QSqlDatabase &db) {
      if (auto res = DatLibraryState::addDismissal(db, canonicalPath, mtimeMs); res.isError()) {
        ErrorUtils::logError(res.error());
      }
    });
  };
  hooks.rescan = [this](const QString &r) { return scanLibrary(r); };
  hooks.saveLibraryPath = m_ctx.saveDatLibraryPath;
  hooks.addToNewCollection = m_ctx.getNewCollectionForDat;

  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  DatLibraryReviewDialog dlg(root, collectionInfos(), initial, hooks,
                             m_dialog ? static_cast<QWidget *>(m_dialog.data()) : parent);
  dlg.exec();
}
