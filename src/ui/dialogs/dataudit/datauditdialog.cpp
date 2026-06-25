#include "datauditdialog.h"

#include <QApplication>
#include <QAtomicInteger>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QSqlDatabase>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QtConcurrent/QtConcurrentRun>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "databaseschema.h"
#include "datauditbrowsermodels.h" // DatAudit::auditRowsFromResults
#include "datauditbrowserpage.h"
#include "datauditexport.h"
#include "datauditfixdialog.h"
#include "datauditmodel.h"
#include "datauditprofile.h"
#include "datauditprofiledialog.h"
#include "datauditresultdelegate.h"
#include "datcache.h"
#include "datlibrarystate.h"
#include "datlookup.h"
#include "errorutils.h"
#include "formbuilders.h"
#include "nointrodownloader.h"
#include "pathutils.h"
#include "redumpdownload.h"
#include "uiconstants/icons.h"

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

// Resolve the collection a profile links to, by the same canonical-uuid
// computation the rest of the app keys on (name + expanded media dir).
// Returns nullptr when the uuid matches no live collection — deleted,
// renamed before the uuid migration ran, or no list was injected.
const CollectionConfig *resolveLinkedCollection(const QList<CollectionConfig> *collections,
                                                const QString &uuid) {
  if (collections == nullptr || uuid.isEmpty()) {
    return nullptr;
  }
  for (const CollectionConfig &c : *collections) {
    const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
    if (CollectionUtils::computeCollectionUuid(c.name, expanded) == uuid) {
      return &c;
    }
  }
  return nullptr;
}

// Fill each DatRef's staleness-hint columns (mtime + dialect/record count)
// before the rows are written. The mtime comes from a stat; dialect/record
// count come from a cheap DatCache peek — never an ingest, so attaching a
// huge never-parsed DAT stays instant and its counts simply stay 0 until the
// first audit ingests it.
void refreshDatRefMetadata(QList<DatAuditProfile::DatRef> &dats) {
  if (dats.isEmpty()) {
    return;
  }
  DatCache::Store cache(DatCache::defaultPath());
  for (DatAuditProfile::DatRef &d : dats) {
    const QFileInfo fi(d.path);
    if (fi.isFile()) {
      d.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    }
    if (const auto src = cache.peek(d.path)) {
      d.dialect = static_cast<int>(src->dialect);
      d.recordCount = src->recordCount;
    }
  }
}

} // namespace

DatAuditDialog::DatAuditDialog(QWidget *parent) : QDialog(parent) {
  // Kartend-ahf3d stage 1: the download/provenance/update-check orchestration
  // lives in DatAuditDownloadService. It never opens a QSqlDatabase itself —
  // provenance reads/writes reach SQLite through these injected accessors, which
  // wrap the dialog's existing transient-connection helper (withProfileDb). A
  // later stage (DatAuditProfileStore) replaces the accessor with a real store.
  m_downloadService =
      std::make_unique<DatAuditDownloadService>(DatAuditDownloadService::ProvenanceAccess{
          [] {
            QList<DatLibraryState::Provenance> all;
            withProfileDb([&all](QSqlDatabase &db) {
              if (auto r = DatLibraryState::loadAllProvenance(db); r.isOk()) {
                all = r.value();
              }
            });
            return all;
          },
          [](const DatLibraryState::Provenance &pr) {
            withProfileDb([&pr](QSqlDatabase &db) {
              if (auto res = DatLibraryState::recordProvenance(db, pr); res.isError()) {
                ErrorUtils::logError(res.error());
              }
            });
          }});
  setWindowTitle(tr("DAT Manager"));
  setWindowFlag(Qt::Window, true);
  // Default opening size, and a generous minimum floor: without an explicit
  // minimum the window can be dragged down to the layout's tiny hint, cramping
  // the nav rail + the Browser's tree/DAT-info/game/ROM panes (the ROM table
  // alone has 9 columns) and the multi-column Audit results. Keep it usable.
  resize(960, 640);
  setMinimumSize(880, 600);

  m_model = new DatAuditModel(this);

  // Shell: nav rail + (context header + stacked pages), mirroring the Settings
  // dialog. Native Breeze palette; the only stylesheet is the transparent
  // splitter handle the settings dialog also uses.
  auto *root = new QHBoxLayout(this);
  auto *splitter = new QSplitter(Qt::Horizontal, this);
  m_splitter = splitter;
  splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
  splitter->setChildrenCollapsible(false);

  m_nav = new QListWidget(splitter);
  m_nav->setObjectName(QStringLiteral("datNav"));

  auto *rightWrap = new QWidget(splitter);
  auto *rightCol = new QVBoxLayout(rightWrap);
  rightCol->setContentsMargins(0, 0, 0, 0);
  auto *headerRow = new QHBoxLayout();
  m_contextIcon = new QLabel(rightWrap);
  m_contextTitle = new QLabel(rightWrap);
  QFont titleFont = m_contextTitle->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
  m_contextTitle->setFont(titleFont);
  headerRow->addWidget(m_contextIcon);
  headerRow->addWidget(m_contextTitle, 1);
  rightCol->addLayout(headerRow);
  m_pages = new QStackedWidget(rightWrap);
  rightCol->addWidget(m_pages, 1);

  m_pages->addWidget(buildAuditPage());    // index 0
  m_pages->addWidget(buildLibraryPage());  // index 1
  m_pages->addWidget(buildDownloadPage()); // index 2
  m_browserPage = new DatAuditBrowserPage(this);
  m_pages->addWidget(m_browserPage); // index 3
  // Browser write-actions (Kartend-7iqhl.2): the page emits, the dialog (which
  // owns the runner + Fix dialog) does the work and refreshes the page.
  connect(m_browserPage, &DatAuditBrowserPage::reauditProfileRequested, this,
          &DatAuditDialog::onBrowserReauditRequested);
  connect(m_browserPage, &DatAuditBrowserPage::fixProfileRequested, this,
          &DatAuditDialog::onBrowserFixRequested);

  splitter->addWidget(m_nav);
  splitter->addWidget(rightWrap);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({220, 740});
  root->addWidget(splitter);

  addNavEntry(tr("Audit"), {QStringLiteral("document-edit"), QStringLiteral("configure")}, 0);
  addNavEntry(tr("DAT Library"), {QStringLiteral("folder-sync"), QStringLiteral("folder")}, 1);
  addNavEntry(tr("Download"), {QStringLiteral("download"), QStringLiteral("emblem-downloads")}, 2);
  addNavEntry(tr("Browser"), {QStringLiteral("view-list-tree"), QStringLiteral("folder-table")}, 3);

  connect(m_nav, &QListWidget::currentRowChanged, this, &DatAuditDialog::onNavRowChanged);
  wireProfileActions();
  wireAuditActions();
  wireDownloadActions();

  applyUniformSizing();
  setBusy(false);
  setDownloadBusy(false);
  loadProfiles();
  m_nav->setCurrentRow(0);
  restoreGeometry_();
}

QWidget *DatAuditDialog::buildAuditPage() {
  auto *page = new QWidget(this);
  auto *root = new QVBoxLayout(page);
  root->setContentsMargins(0, 0, 0, 0);

  // Composed from the per-section builders below (Kartend-139sr); the
  // add-order here is the construction order the page has always had.
  root->addLayout(buildProfileRow(page));

  // DAT files + scan folders. Locked + derived for a linked profile
  // (Kartend-m6qsb.2); updateLinkedUiState() drives the hint + lock.
  m_linkedHint =
      new QLabel(tr("The scan folder and DAT files are seeded from the linked collection "
                    "(its content folder and configured DATs), then managed here — add or "
                    "remove them freely to override."),
                 page);
  m_linkedHint->setWordWrap(true);
  m_linkedHint->setVisible(false);
  root->addWidget(m_linkedHint);
  root->addLayout(buildInputsSection(page));

  root->addLayout(buildLayoutBanner(page));

  // Run / cancel / progress / filter.
  auto *controls = new QHBoxLayout();
  m_runButton = new QPushButton(tr("Run audit"), page);
  m_cancelButton = new QPushButton(tr("Cancel"), page);
  m_cancelButton->setEnabled(false);
  m_progress = new QProgressBar(page);
  m_progress->setVisible(false);
  // "Verify (ignore cache)" force-rehash (Kartend-p30ic): off by default keeps
  // the fast cached path; when ticked, this run bypasses every file-hash-cache
  // hit and recomputes, catching an in-place same-size/same-mtime replacement
  // (rsync --times / cp -p) the (size, mtime) cache key cannot detect.
  m_forceRehashCheck = new QCheckBox(tr("Verify (ignore cache)"), page);
  m_forceRehashCheck->setToolTip(
      tr("Re-hash every file this run instead of trusting the cache. Use after a file may "
         "have been replaced in place while keeping the same size and modified time "
         "(e.g. rsync --times, cp -p), which the cache would otherwise miss. Slower."));
  controls->addWidget(m_runButton);
  controls->addWidget(m_cancelButton);
  controls->addWidget(m_forceRehashCheck);
  controls->addWidget(m_progress, 1);
  controls->addWidget(new QLabel(tr("View:"), page));
  m_filterCombo = new QComboBox(page);
  for (const auto &e : filterEntries()) {
    m_filterCombo->addItem(tr(e.label));
  }
  controls->addWidget(m_filterCombo);
  m_searchEdit = new QLineEdit(page);
  m_searchEdit->setPlaceholderText(tr("Search results…"));
  m_searchEdit->setClearButtonEnabled(true);
  controls->addWidget(m_searchEdit);
  root->addLayout(controls);

  // Summary line + at-a-glance completeness bar (Kartend-m6qsb.20).
  auto *summaryRow = new QHBoxLayout();
  m_summaryLabel = new QLabel(tr("No audit run yet."), page);
  summaryRow->addWidget(m_summaryLabel, 1);
  m_completionBar = new QProgressBar(page);
  m_completionBar->setFormat(tr("%p% present"));
  m_completionBar->setMaximumWidth(220);
  m_completionBar->setVisible(false);
  summaryRow->addWidget(m_completionBar);
  root->addLayout(summaryRow);

  root->addWidget(buildResultsTable(page), 1);

  root->addLayout(buildExportRow(page));
  return page;
}

QLayout *DatAuditDialog::buildProfileRow(QWidget *page) {
  // Saved profiles.
  auto *profileRow = new QHBoxLayout();
  profileRow->addWidget(new QLabel(tr("Profile:"), page));
  m_profileCombo = new QComboBox(page);
  m_newProfileButton = new QPushButton(tr("New…"), page);
  m_editProfileButton = new QPushButton(tr("Edit…"), page);
  m_duplicateProfileButton = new QPushButton(tr("Duplicate"), page);
  m_renameProfileButton = new QPushButton(tr("Rename"), page);
  m_deleteProfileButton = new QPushButton(tr("Delete"), page);
  profileRow->addWidget(m_profileCombo, 1);
  profileRow->addWidget(m_newProfileButton);
  profileRow->addWidget(m_editProfileButton);
  profileRow->addWidget(m_duplicateProfileButton);
  profileRow->addWidget(m_renameProfileButton);
  profileRow->addWidget(m_deleteProfileButton);
  return profileRow;
}

QLayout *DatAuditDialog::buildInputsSection(QWidget *page) {
  Q_UNUSED(page);
  auto *inputs = new QHBoxLayout();
  inputs->addWidget(FormBuilders::makePathListGroup(tr("DAT files"), m_datList, m_addDatButton,
                                                    m_removeDatButton, tr("Add DAT…")));
  inputs->addWidget(FormBuilders::makePathListGroup(tr("Scan folders"), m_rootList, m_addRootButton,
                                                    m_removeRootButton, tr("Add folder…")));
  return inputs;
}

QLayout *DatAuditDialog::buildLayoutBanner(QWidget *page) {
  // Folder-structure detection banner (Kartend-m6qsb.6).
  auto *layoutRow = new QHBoxLayout();
  m_detectLayoutButton = new QPushButton(tr("Detect structure"), page);
  layoutRow->addWidget(m_detectLayoutButton);
  m_layoutBanner = new QLabel(page);
  m_layoutBanner->setVisible(false);
  layoutRow->addWidget(m_layoutBanner, 1);
  m_applyLayoutButton = new QPushButton(tr("Apply"), page);
  m_applyLayoutButton->setVisible(false);
  m_dismissLayoutButton = new QPushButton(tr("Dismiss"), page);
  m_dismissLayoutButton->setVisible(false);
  layoutRow->addWidget(m_applyLayoutButton);
  layoutRow->addWidget(m_dismissLayoutButton);
  return layoutRow;
}

QWidget *DatAuditDialog::buildResultsTable(QWidget *page) {
  // Status-tinted, sortable, searchable results (Kartend-m6qsb.20). The status
  // combo still filters the source model; the proxy adds sort + a text search
  // on top. The delegate paints the per-status tint + icon.
  m_table = new QTableView(page);
  m_proxy = new QSortFilterProxyModel(this);
  m_proxy->setSourceModel(m_model);
  m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  m_proxy->setFilterKeyColumn(-1); // match across all columns
  m_proxy->setSortRole(Qt::DisplayRole);
  m_table->setModel(m_proxy);
  m_table->setItemDelegate(new DatAudit::DatAuditResultDelegate(this));
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSortingEnabled(true);
  m_table->setContextMenuPolicy(Qt::CustomContextMenu);
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->setVisible(false);
  connect(m_searchEdit, &QLineEdit::textChanged, m_proxy,
          &QSortFilterProxyModel::setFilterFixedString);
  connect(m_table, &QTableView::customContextMenuRequested, this,
          &DatAuditDialog::onResultsContextMenu);
  connect(m_table, &QTableView::doubleClicked, this, &DatAuditDialog::onResultDoubleClicked);
  return m_table;
}

QLayout *DatAuditDialog::buildExportRow(QWidget *page) {
  auto *exports = new QHBoxLayout();
  m_fixButton = new QPushButton(tr("Fix…"), page);
  exports->addWidget(m_fixButton);
  exports->addStretch();
  m_exportCsvButton = new QPushButton(tr("Export CSV…"), page);
  m_exportFixdatButton = new QPushButton(tr("Export fixdat…"), page);
  m_exportMissButton = new QPushButton(tr("Export miss list…"), page);
  exports->addWidget(m_exportCsvButton);
  exports->addWidget(m_exportFixdatButton);
  exports->addWidget(m_exportMissButton);
  return exports;
}

QWidget *DatAuditDialog::buildLibraryPage() {
  auto *page = new QWidget(this);
  auto *root = new QVBoxLayout(page);
  root->setContentsMargins(0, 0, 0, 0);

  auto *box = new QGroupBox(tr("Watched DAT library"), page);
  auto *boxLayout = new QVBoxLayout(box);
  boxLayout->addWidget(
      new QLabel(tr("Kartend checks this folder for new catalogues and proposes matches to your "
                    "collections. Attaching is always confirmed by you."),
                 box));
  m_libraryPathLabel = new QLabel(tr("No library folder set."), box);
  m_libraryPathLabel->setForegroundRole(QPalette::PlaceholderText);
  m_libraryPathLabel->setWordWrap(true);
  boxLayout->addWidget(m_libraryPathLabel);
  m_libraryReviewButton = new QPushButton(tr("Scan & review proposals…"), box);
  m_libraryReviewButton->setEnabled(false); // enabled once a controller hooks it up
  // Check downloaded catalogues against their source for newer revisions
  // (Kartend-m6qsb.26/.31). Hidden until a library path is set.
  m_checkUpdatesButton = new QPushButton(tr("Check for updates…"), box);
  m_checkUpdatesButton->setVisible(false);
  auto *btnRow = new QHBoxLayout();
  btnRow->addWidget(m_libraryReviewButton);
  btnRow->addWidget(m_checkUpdatesButton);
  btnRow->addStretch();
  boxLayout->addLayout(btnRow);
  root->addWidget(box);
  connect(m_checkUpdatesButton, &QPushButton::clicked, this, &DatAuditDialog::onCheckUpdates);

  // Import any cataloguer's DATs into the library (Kartend-m6qsb.22) — a
  // downloaded zip from Redump/TOSEC/No-Good, a folder of DATs, or a single
  // file. Buttons are hidden until a controller wires the import handler.
  auto *importBox = new QGroupBox(tr("Import DATs"), page);
  auto *importLayout = new QVBoxLayout(importBox);
  importLayout->addWidget(new QLabel(
      tr("Add catalogues from any source — point Kartend at a downloaded DAT zip, a "
         "folder of DATs, or a single .dat file; they're copied into the library and matched."),
      importBox));
  auto *importRow = new QHBoxLayout();
  m_importZipButton = new QPushButton(tr("Import DAT zip…"), importBox);
  m_importFolderButton = new QPushButton(tr("Import DAT folder…"), importBox);
  m_importZipButton->setVisible(false);
  m_importFolderButton->setVisible(false);
  importRow->addWidget(m_importZipButton);
  importRow->addWidget(m_importFolderButton);
  importRow->addStretch();
  importLayout->addLayout(importRow);
  root->addWidget(importBox);
  connect(m_importZipButton, &QPushButton::clicked, this, &DatAuditDialog::onImportZip);
  connect(m_importFolderButton, &QPushButton::clicked, this, &DatAuditDialog::onImportFolder);

  root->addStretch(1);
  return page;
}

QWidget *DatAuditDialog::buildDownloadPage() {
  auto *page = new QWidget(this);
  auto *root = new QVBoxLayout(page);
  root->setContentsMargins(0, 0, 0, 0);

  // Source selector (Kartend-m6qsb.23): No-Intro vs Redump drive different
  // widget sets below — onDownloadSourceChanged toggles them.
  auto *sourceRow = new QHBoxLayout();
  sourceRow->addWidget(new QLabel(tr("Source:"), page));
  m_dlSourceCombo = new QComboBox(page);
  m_dlSourceCombo->addItem(tr("No-Intro (DAT-o-MATIC)"), QStringLiteral("nointro"));
  m_dlSourceCombo->addItem(tr("Redump"), QStringLiteral("redump"));
  sourceRow->addWidget(m_dlSourceCombo);
  sourceRow->addStretch();
  root->addLayout(sourceRow);

  // The two mutually-exclusive source sub-forms (Kartend-139sr): No-Intro's
  // box + its sets box, then Redump's box. onDownloadSourceChanged toggles
  // their visibility; the add-order here is unchanged from the inlined form.
  root->addWidget(buildNoIntroSource(page));
  root->addWidget(m_setsBox);
  root->addWidget(buildRedumpSource(page));

  auto *dlRow = new QHBoxLayout();
  m_dlDownloadButton = new QPushButton(tr("Download && import"), page);
  m_dlDownloadButton->setEnabled(false);
  m_dlCancelButton = new QPushButton(tr("Cancel"), page);
  m_dlCancelButton->setEnabled(false);
  m_dlProgress = new QProgressBar(page);
  m_dlProgress->setVisible(false);
  dlRow->addWidget(m_dlDownloadButton);
  dlRow->addWidget(m_dlCancelButton);
  dlRow->addWidget(m_dlProgress, 1);
  root->addLayout(dlRow);

  m_dlStatus = new QLabel(page);
  m_dlStatus->setWordWrap(true);
  root->addWidget(m_dlStatus);
  root->addStretch(1);
  return page;
}

QWidget *DatAuditDialog::buildNoIntroSource(QWidget *page) {
  // --- No-Intro source ---
  m_noIntroBox = new QGroupBox(tr("No-Intro (DAT-o-MATIC)"), page);
  auto *form = new QFormLayout(m_noIntroBox);
  form->setHorizontalSpacing(16);
  form->setVerticalSpacing(6);

  auto *sysRow = new QHBoxLayout();
  m_dlSystemEdit = new QLineEdit(m_noIntroBox);
  m_dlSystemEdit->setPlaceholderText(tr("DAT-o-MATIC system id or daily-download URL"));
  m_dlLoadButton = new QPushButton(tr("Load"), m_noIntroBox);
  sysRow->addWidget(m_dlSystemEdit, 1);
  sysRow->addWidget(m_dlLoadButton);
  form->addRow(tr("System:"), sysRow);

  m_dlDatTypeCombo = new QComboBox(m_noIntroBox);
  m_dlDatTypeCombo->setEnabled(false);
  form->addRow(tr("DAT type:"), m_dlDatTypeCombo);

  m_dlPackLabel = new QLabel(tr("Load a system to see the available pack."), m_noIntroBox);
  m_dlPackLabel->setForegroundRole(QPalette::PlaceholderText);
  form->addRow(tr("Pack:"), m_dlPackLabel);

  // The "Include sets" group belongs to the No-Intro source; it is created
  // here but added to the page root separately by buildDownloadPage so the
  // widget add-order stays exactly as before.
  m_setsBox = new QGroupBox(tr("Include sets"), page);
  auto *setsOuter = new QVBoxLayout(m_setsBox);
  m_dlSetsContainer = new QWidget(m_setsBox);
  m_dlSetsLayout = new QVBoxLayout(m_dlSetsContainer);
  m_dlSetsLayout->setContentsMargins(0, 0, 0, 0);
  setsOuter->addWidget(m_dlSetsContainer);
  return m_noIntroBox;
}

QWidget *DatAuditDialog::buildRedumpSource(QWidget *page) {
  // --- Redump source (one GET per system; pick by name) ---
  m_redumpBox = new QGroupBox(tr("Redump"), page);
  auto *redumpRow = new QHBoxLayout(m_redumpBox);
  redumpRow->addWidget(new QLabel(tr("System:"), m_redumpBox));
  m_redumpSystemCombo = new QComboBox(m_redumpBox);
  m_redumpSystemCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_redumpRefreshButton = new QPushButton(tr("Refresh list"), m_redumpBox);
  redumpRow->addWidget(m_redumpSystemCombo, 1);
  redumpRow->addWidget(m_redumpRefreshButton);
  m_redumpBox->setVisible(false);
  return m_redumpBox;
}

void DatAuditDialog::wireProfileActions() {
  connect(m_profileCombo, &QComboBox::currentIndexChanged, this,
          &DatAuditDialog::onProfileSelected);
  connect(m_newProfileButton, &QPushButton::clicked, this, &DatAuditDialog::onNewProfile);
  connect(m_editProfileButton, &QPushButton::clicked, this, &DatAuditDialog::onEditProfile);
  connect(m_duplicateProfileButton, &QPushButton::clicked, this,
          &DatAuditDialog::onDuplicateProfile);
  connect(m_renameProfileButton, &QPushButton::clicked, this, &DatAuditDialog::onRenameProfile);
  connect(m_deleteProfileButton, &QPushButton::clicked, this, &DatAuditDialog::onDeleteProfile);
}

void DatAuditDialog::wireAuditActions() {
  connect(m_addDatButton, &QPushButton::clicked, this, &DatAuditDialog::onAddDat);
  connect(m_removeDatButton, &QPushButton::clicked, this, &DatAuditDialog::onRemoveDat);
  connect(m_addRootButton, &QPushButton::clicked, this, &DatAuditDialog::onAddRoot);
  connect(m_removeRootButton, &QPushButton::clicked, this, &DatAuditDialog::onRemoveRoot);
  connect(m_detectLayoutButton, &QPushButton::clicked, this, [this] { runLayoutDetection(false); });
  connect(m_applyLayoutButton, &QPushButton::clicked, this, &DatAuditDialog::applyDetectedLayout);
  connect(m_dismissLayoutButton, &QPushButton::clicked, this, [this] {
    m_pendingDetection = DatAudit::LayoutDetection{};
    m_layoutBanner->setVisible(false);
    m_applyLayoutButton->setVisible(false);
    m_dismissLayoutButton->setVisible(false);
  });
  connect(m_runButton, &QPushButton::clicked, this, &DatAuditDialog::onRun);
  connect(m_cancelButton, &QPushButton::clicked, this, &DatAuditDialog::onCancel);
  connect(m_filterCombo, &QComboBox::currentIndexChanged, this, &DatAuditDialog::onFilterChanged);
  connect(m_fixButton, &QPushButton::clicked, this, &DatAuditDialog::onFix);
  connect(m_exportCsvButton, &QPushButton::clicked, this, &DatAuditDialog::onExportCsv);
  connect(m_exportFixdatButton, &QPushButton::clicked, this, &DatAuditDialog::onExportFixdat);
  connect(m_exportMissButton, &QPushButton::clicked, this, &DatAuditDialog::onExportMissList);
  connect(&m_watcher, &QFutureWatcher<AuditOutput>::finished, this,
          &DatAuditDialog::onAuditFinished);
}

void DatAuditDialog::wireDownloadActions() {
  // Download page wiring (Kartend-m6qsb.16).
  connect(m_dlSourceCombo, &QComboBox::currentIndexChanged, this,
          &DatAuditDialog::onDownloadSourceChanged);
  connect(m_dlLoadButton, &QPushButton::clicked, this, &DatAuditDialog::onLoadDailyForm);
  connect(m_redumpRefreshButton, &QPushButton::clicked, this, [this] {
    m_redumpSystemCombo->clear();
    onDownloadSourceChanged(); // re-triggers the fetch
  });
  connect(m_redumpSystemCombo, &QComboBox::currentIndexChanged, this,
          [this] { refreshDownloadButtonEnabled(); });
  connect(&m_redumpSystemsWatcher, &QFutureWatcher<RedumpSystemsOutcome>::finished, this,
          &DatAuditDialog::onRedumpSystemsFetched);
  connect(m_dlDownloadButton, &QPushButton::clicked, this, &DatAuditDialog::onStartDownload);
  connect(m_dlCancelButton, &QPushButton::clicked, this, &DatAuditDialog::onCancelDownload);
  connect(&m_dlFormWatcher, &QFutureWatcher<DailyFormOutcome>::finished, this,
          &DatAuditDialog::onLoadDailyFormFinished);
  connect(&m_dlWatcher, &QFutureWatcher<DatAuditDownloadService::Outcome>::finished, this,
          &DatAuditDialog::onDownloadFinished);
  connect(&m_updateWatcher, &QFutureWatcher<DatAuditDownloadService::UpdateResult>::finished, this,
          &DatAuditDialog::onCheckUpdatesFinished);
}

void DatAuditDialog::addNavEntry(const QString &label, const QStringList &iconNames,
                                 int pageIndex) {
  auto *item = new QListWidgetItem(label, m_nav);
  for (const QString &name : iconNames) {
    const QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) {
      item->setIcon(icon);
      break;
    }
  }
  item->setData(Qt::UserRole, pageIndex);
}

void DatAuditDialog::onNavRowChanged() {
  const QListWidgetItem *item = m_nav->currentItem();
  if (item == nullptr) {
    return;
  }
  const int page = item->data(Qt::UserRole).toInt();
  m_pages->setCurrentIndex(page);
  m_contextIcon->setPixmap(item->icon().pixmap(28, 28));
  m_contextTitle->setText(item->text());
  // The browser reads persisted results on demand — refresh it each time it is
  // shown so a just-completed audit is reflected without reopening the window.
  if (page == 3 && m_browserPage != nullptr) {
    // Resolve uuid → collection name so the browser can group profiles under
    // their linked collection when category grouping is on (Kartend-7iqhl.5).
    QHash<QString, QString> names;
    if (m_collections != nullptr) {
      for (const CollectionConfig &c : *m_collections) {
        const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
        names.insert(CollectionUtils::computeCollectionUuid(c.name, expanded), c.name);
      }
    }
    m_browserPage->setCollectionNames(names);
    m_browserPage->refresh();
  }
}

void DatAuditDialog::applyUniformSizing() {
  // Mirror the settings dialog's uniform field widths so the pages read as one
  // family. Buttons get a floor (long labels still grow); line edits/combos a
  // sensible cap so they don't stretch the full pane.
  constexpr int kFieldWidth = 360;
  constexpr int kButtonMinWidth = 96;
  for (auto *edit : m_pages->findChildren<QLineEdit *>()) {
    edit->setMaximumWidth(kFieldWidth);
  }
  for (auto *button : m_pages->findChildren<QPushButton *>()) {
    button->setMinimumWidth(kButtonMinWidth);
  }
  m_nav->setMaximumWidth(220);
}

void DatAuditDialog::hideEvent(QHideEvent *event) {
  persistGeometry();
  QDialog::hideEvent(event);
}

void DatAuditDialog::persistGeometry() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));
  settings.setValue(QStringLiteral("geometry"), saveGeometry());
  if (m_splitter != nullptr) {
    settings.setValue(QStringLiteral("splitter"), m_splitter->saveState());
  }
  settings.endGroup();
  // Kartend-o46gy: the browser page persists its own splitter/column/filter
  // layout under the same group.
  if (m_browserPage != nullptr) {
    m_browserPage->persistState();
  }
}

void DatAuditDialog::restoreGeometry_() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));
  const QByteArray geom = settings.value(QStringLiteral("geometry")).toByteArray();
  if (!geom.isEmpty()) {
    restoreGeometry(geom);
  }
  const QByteArray sp = settings.value(QStringLiteral("splitter")).toByteArray();
  if (!sp.isEmpty() && m_splitter != nullptr) {
    m_splitter->restoreState(sp);
  }
  settings.endGroup();
  // Kartend-o46gy: restore the browser page's saved layout alongside the
  // dialog's own. Safe before first show — QSplitter/QHeaderView::restoreState
  // stash the sizes and apply them when the page becomes visible.
  if (m_browserPage != nullptr) {
    m_browserPage->restoreState_();
  }
}

void DatAuditDialog::setLibraryPathAccessors(std::function<QString()> getter,
                                             std::function<void(const QString &)> setter) {
  m_getLibraryPath = std::move(getter);
  m_saveLibraryPath = std::move(setter);
  if (m_getLibraryPath) {
    const QString path = m_getLibraryPath();
    m_libraryPathLabel->setText(path.isEmpty() ? tr("No library folder set.") : path);
  }
  // "Check for updates" only makes sense once we know where the library is.
  m_checkUpdatesButton->setVisible(static_cast<bool>(m_getLibraryPath));
  // The Download page needs a library destination; hide it when there is no
  // way to know where to import packs.
  if (m_nav != nullptr && m_nav->count() >= 3) {
    m_nav->item(2)->setHidden(!static_cast<bool>(m_getLibraryPath));
  }
}

void DatAuditDialog::setQuarantineDefaultProvider(std::function<QString()> provider) {
  m_getQuarantineDefaultDir = std::move(provider);
}

void DatAuditDialog::setImportHandler(std::function<void(const QString &)> handler) {
  m_importPack = std::move(handler);
  const bool on = static_cast<bool>(m_importPack);
  m_importZipButton->setVisible(on);
  m_importFolderButton->setVisible(on);
}

void DatAuditDialog::onImportZip() {
  if (!m_importPack) {
    return;
  }
  const QString zip =
      QFileDialog::getOpenFileName(this, tr("Import DAT zip"), QString(),
                                   tr("DAT archives (*.zip *.7z *.rar *.tar *.gz);;All files (*)"));
  if (!zip.isEmpty()) {
    m_importPack(zip);
  }
}

void DatAuditDialog::onImportFolder() {
  if (!m_importPack) {
    return;
  }
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Import DAT folder"));
  if (!dir.isEmpty()) {
    m_importPack(dir);
  }
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
    m_currentProfile = DatAuditProfile::Profile{}; // "(unsaved)" — keep the ad-hoc lists
    updateLinkedUiState();
    return;
  }
  loadProfileFromDb(id);
  updateLinkedUiState();
}

bool DatAuditDialog::loadProfileFromDb(qint64 id) {
  bool loaded = false;
  withProfileDb([this, id, &loaded](QSqlDatabase &db) {
    auto res = DatAuditProfile::load(db, id);
    if (res.isOk() && res.value().has_value()) {
      m_currentProfile = *res.value();
      applyCollectionDerivation();
      syncUiFromProfile();
      loaded = true;
    } else if (res.isError()) {
      ErrorUtils::logError(res.error());
    }
  });
  return loaded;
}

void DatAuditDialog::applyCollectionDerivation() {
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

void DatAuditDialog::updateLinkedUiState() {
  const bool linked = !m_currentProfile.collectionUuid.isEmpty();
  // The scan folder and DAT files stay user-editable even when linked — the
  // collection only seeds them. Nothing in the lists is locked; the hint just
  // explains the seeding.
  m_linkedHint->setVisible(linked);
}

void DatAuditDialog::runLayoutDetection(bool autoTriggered) {
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

void DatAuditDialog::applyDetectedLayout() {
  if (m_pendingDetection.layout == DatAudit::Layout::Unknown) {
    return;
  }
  m_currentProfile.detectedLayout = DatAudit::layoutToken(m_pendingDetection.layout);
  m_currentProfile.layoutConfirmed = true;
  if (m_currentProfile.id >= 0) {
    // Through uiProfile() so the on-screen DAT/root lists ride along — writing
    // m_currentProfile directly would silently revert unsaved list edits.
    DatAuditProfile::Profile p = uiProfile();
    persistProfile(p);
  }
  m_layoutBanner->setVisible(false);
  m_applyLayoutButton->setVisible(false);
  m_dismissLayoutButton->setVisible(false);
}

void DatAuditDialog::syncUiFromProfile() {
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

DatAuditProfile::Profile DatAuditDialog::uiProfile() const {
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

bool DatAuditDialog::persistProfile(DatAuditProfile::Profile &p) {
  refreshDatRefMetadata(p.dats);
  bool ok = false;
  withProfileDb([this, &p, &ok](QSqlDatabase &db) {
    if (p.id < 0) {
      auto res = DatAuditProfile::insert(db, p);
      if (res.isOk()) {
        p.id = res.value();
        ok = true;
      } else {
        QMessageBox::warning(this, tr("DAT Audit"),
                             tr("Could not save profile: %1").arg(res.error().message));
      }
    } else {
      auto res = DatAuditProfile::update(db, p);
      if (res.isOk()) {
        ok = true;
      } else {
        QMessageBox::warning(this, tr("DAT Audit"),
                             tr("Could not update profile: %1").arg(res.error().message));
      }
    }
  });
  return ok;
}

void DatAuditDialog::selectProfileById(qint64 id) {
  for (int i = 0; i < m_profileCombo->count(); ++i) {
    if (m_profileCombo->itemData(i).toLongLong() == id) {
      m_profileCombo->setCurrentIndex(i);
      return;
    }
  }
}

void DatAuditDialog::onNewProfile() {
  DatAuditProfileDialog dlg(DatAuditProfile::Profile{}, m_collections, this);
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  DatAuditProfile::Profile p = dlg.profile();
  if (!persistProfile(p)) {
    return;
  }
  m_currentProfile = p;
  applyCollectionDerivation();
  loadProfiles();
  selectProfileById(p.id);
  syncUiFromProfile();
  updateLinkedUiState();
}

void DatAuditDialog::onEditProfile() {
  DatAuditProfileDialog dlg(uiProfile(), m_collections, this);
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  m_currentProfile = dlg.profile();
  applyCollectionDerivation(); // the editor may have linked/unlinked a collection
  syncUiFromProfile();
  updateLinkedUiState();
  if (m_currentProfile.id >= 0) {
    persistProfile(m_currentProfile);
    loadProfiles();
    selectProfileById(m_currentProfile.id);
  }
}

void DatAuditDialog::onDuplicateProfile() {
  DatAuditProfile::Profile seed = uiProfile();
  seed.id = -1;
  seed.name = seed.name.isEmpty() ? tr("New profile") : tr("%1 (copy)").arg(seed.name);
  DatAuditProfileDialog dlg(seed, m_collections, this);
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  DatAuditProfile::Profile p = dlg.profile();
  if (!persistProfile(p)) {
    return;
  }
  m_currentProfile = p;
  applyCollectionDerivation();
  loadProfiles();
  selectProfileById(p.id);
  syncUiFromProfile();
  updateLinkedUiState();
}

void DatAuditDialog::onRenameProfile() {
  if (m_currentProfile.id < 0) {
    QMessageBox::information(this, tr("DAT Audit"), tr("Select a saved profile to rename."));
    return;
  }
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Rename profile"), tr("New name:"),
                                             QLineEdit::Normal, m_currentProfile.name, &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  m_currentProfile.name = name.trimmed();
  persistProfile(m_currentProfile);
  loadProfiles();
  selectProfileById(m_currentProfile.id);
}

void DatAuditDialog::onDeleteProfile() {
  const qint64 id = m_profileCombo->itemData(m_profileCombo->currentIndex()).toLongLong();
  if (id < 0) {
    return;
  }
  if (QMessageBox::question(this, tr("Delete profile"),
                            tr("Delete profile \"%1\"?").arg(m_profileCombo->currentText())) !=
      QMessageBox::Yes) {
    return;
  }
  withProfileDb([id](QSqlDatabase &db) {
    auto res = DatAuditProfile::remove(db, id);
    Q_UNUSED(res);
  });
  m_currentProfile = DatAuditProfile::Profile{};
  loadProfiles();
}

DatAuditDialog::~DatAuditDialog() {
  // If any off-thread run is in flight when the window is torn down, ask it to
  // stop and wait so a worker never outlives the dialog it reports into.
  if (m_cancel) {
    m_cancel->store(true);
  }
  if (m_dlCancel) {
    m_dlCancel->store(true);
  }
  if (m_updateCancel) {
    m_updateCancel->store(true);
  }
  if (m_watcher.isRunning()) {
    m_watcher.waitForFinished();
  }
  if (m_dlWatcher.isRunning()) {
    m_dlWatcher.waitForFinished();
  }
  if (m_updateWatcher.isRunning()) {
    m_updateWatcher.waitForFinished();
  }
}

void DatAuditDialog::setCollections(QList<CollectionConfig> *collections) {
  m_collections = collections;
}

void DatAuditDialog::setLibraryOpener(std::function<void()> opener) {
  m_libraryOpener = std::move(opener);
  m_libraryReviewButton->setEnabled(static_cast<bool>(m_libraryOpener));
  // Reconnect-safe: the controller re-installs the opener on every open.
  disconnect(m_libraryReviewButton, nullptr, this, nullptr);
  if (m_libraryOpener) {
    connect(m_libraryReviewButton, &QPushButton::clicked, this, [this] { m_libraryOpener(); });
  }
}

void DatAuditDialog::openForCollection(const QString &collectionUuid, const QString &collectionName,
                                       const QString &mediaDir, const QStringList &datPaths) {
  loadProfiles(); // refresh the combo so a newly-linked profile is selectable

  qint64 linkedId = -1;
  if (!collectionUuid.isEmpty()) {
    withProfileDb([&linkedId, &collectionUuid](QSqlDatabase &db) {
      // Deterministic: most-recently-updated linked profile wins (the old
      // listAll() scan picked whatever sorted first by name).
      auto linked = DatAuditProfile::loadByCollectionUuid(db, collectionUuid);
      if (linked.isOk() && linked.value().has_value()) {
        linkedId = linked.value()->id;
      }
    });
  }

  if (linkedId >= 0) {
    selectProfileById(linkedId); // → onProfileSelected loads, derives, and syncs the UI
    // The settings panel hands over its WORKING copy — unsaved media-dir /
    // DAT-list edits included (Kartend-6wn0p) — so it overrides the
    // saved-collection derivation onProfileSelected just applied. Same
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
  // collection so the audit is pre-populated. Select the "(unsaved)" row with
  // the combo signal blocked first — onProfileSelected would otherwise clear
  // m_currentProfile back to an empty profile and discard the seed.
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
  {
    const QSignalBlocker block(m_profileCombo);
    selectProfileById(-1); // the "(unsaved)" row
  }
  m_currentProfile = seed;
  syncUiFromProfile(); // reflect the seeded DAT + scan-root lists on screen
  updateLinkedUiState();
  runLayoutDetection(true); // fresh seed never has a confirmed layout
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

bool DatAuditDialog::hasApplicableFixes() const {
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

  m_cancel = std::make_shared<std::atomic<bool>>(false);
  setBusy(true);

  // Real progress (Kartend-m6qsb.7): the runner ticks on the worker thread;
  // updates marshal to the UI thread via queued invokes, throttled to ~200
  // posts per run so a 50k-file audit doesn't flood the event queue. The
  // dialog outlives the run (the controller caches it for the app's life).
  auto lastPosted = std::make_shared<std::atomic<int>>(-1);
  // Param named `prog` (not `p`) so it doesn't shadow the `const Profile p`
  // local above — gcc -Wshadow (-Werror in the Release+gcc / maintenance build)
  // flags the shadow even though clang's -Wshadow does not.
  auto progressFn = [this, lastPosted](const DatAudit::AuditProgress &prog) {
    const int step = qMax(1, prog.filesTotal / 200);
    const int last = lastPosted->load(std::memory_order_relaxed);
    if (prog.filesDone != prog.filesTotal && prog.filesDone - last < step) {
      return;
    }
    lastPosted->store(prog.filesDone, std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        this,
        [this, prog] {
          if (m_running && prog.filesTotal > 0) {
            m_progress->setRange(0, prog.filesTotal);
            m_progress->setValue(prog.filesDone);
          }
          // Drive the browser's inline bar for a browser-initiated re-audit
          // (Kartend-7iqhl.3); no-op (total<=0 / not pending) otherwise.
          if (m_browserAuditPending && m_browserPage != nullptr) {
            m_browserPage->setAuditProgress(prog.filesDone, prog.filesTotal);
          }
        },
        Qt::QueuedConnection);
  };

  auto cancel = m_cancel;
  auto future = QtConcurrent::run([dats, roots, cancel, regionPrefs, ignore, onePerGame, layout,
                                   mergeMode, ignoreHashCache, progressFn]() -> AuditOutput {
    // Both DB connections below are created, used, and removed on THIS worker
    // thread, satisfying QSqlDatabase's thread affinity.
    DatCache::Store cache(DatCache::defaultPath());
    QStringList failed;
    DatAudit::Catalogue cat = DatAudit::buildCatalogue(cache, dats, &failed);
    DatAudit::AuditOptions opts;
    opts.scanRoots = roots;
    opts.datPaths = dats;
    opts.ignoreGlobs = ignore;
    opts.regionPrefs = regionPrefs;
    opts.onePerGame = onePerGame;
    opts.layout = layout;
    opts.mergeMode = mergeMode;
    opts.ignoreHashCache = ignoreHashCache;

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
      out = DatAudit::run(cat, opts, cacheDb, cancel, progressFn);
      // Carry the failed-DAT list back to the GUI thread so it isn't
      // silently dropped — the audit ran against a partial catalogue
      // otherwise (Kartend-2zcrz).
      out.failedDats = failed;
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
  // Persist the snapshot + last-scan stamp for a SAVED profile so the
  // collection settings can show "last audited / have / missing" without
  // re-running anything (Kartend-m6qsb.8). A cancelled audit is a partial
  // statement of the world — never persisted. Ad-hoc (unsaved) runs stay
  // ephemeral by design: no auto-created profile rows.
  if (!out.cancelled && m_currentProfile.id >= 0) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<DatAuditProfile::ResultRow> rows;
    rows.reserve(out.rows.size());
    for (const DatAudit::AuditRow &r : out.rows) {
      DatAuditProfile::ResultRow row;
      // File-backed rows key by path (unique per enumeration); entry-only
      // rows (Missing) key by the canonical name. Prefixes keep the two key
      // spaces from colliding in the shared primary key.
      row.entryKey = r.filePath.isEmpty() ? QStringLiteral("entry:") + r.expectedName
                                          : QStringLiteral("file:") + r.filePath;
      row.status = static_cast<int>(r.status);
      row.filePath = r.filePath;
      row.detail = r.expectedName;
      // Source DAT + game + MIA so the browser's tree/game-list rollups are
      // grouped queries (Kartend-34lab, schema v22).
      row.sourceName = r.sourceName;
      row.gameName = r.gameName;
      row.mia = r.mia;
      row.zipIndex = r.zipIndex; // archive member index for the ZipIndex column
      rows.append(row);
    }
    const qint64 profileId = m_currentProfile.id;
    withProfileDb([profileId, now, &rows](QSqlDatabase &db) {
      if (auto stamped = DatAuditProfile::touchLastScan(db, profileId, now); stamped.isError()) {
        ErrorUtils::logError(stamped.error());
      }
      if (auto replaced = DatAuditProfile::replaceResults(db, profileId, rows);
          replaced.isError()) {
        ErrorUtils::logError(replaced.error());
      }
    });
    m_currentProfile.lastScanAtMs = now;
  }
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
    if (m_browserPage != nullptr) {
      m_browserPage->refresh();
      m_browserPage->selectProfileNode(m_currentProfile.id);
    }
  }
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

// Map a view (proxy) index back to its AuditRow in the source model.
const DatAudit::AuditRow *DatAuditDialog::rowForProxyIndex(const QModelIndex &proxyIndex) const {
  if (!proxyIndex.isValid()) {
    return nullptr;
  }
  return m_model->rowAt(m_proxy->mapToSource(proxyIndex).row());
}

void DatAuditDialog::onResultDoubleClicked(const QModelIndex &index) {
  const DatAudit::AuditRow *row = rowForProxyIndex(index);
  if (row == nullptr || row->filePath.isEmpty()) {
    return;
  }
  // Reveal the file's containing folder in the system file manager.
  const QFileInfo fi(row->filePath);
  QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

void DatAuditDialog::onResultsContextMenu(const QPoint &pos) {
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

void DatAuditDialog::setBusy(bool busy) {
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
  if (m_browserAuditPending && m_browserPage != nullptr) {
    m_browserPage->setAuditRunning(busy);
  }
  const bool canExport = !busy && hasResults();
  // Fix illuminates only when something is actually fixable in place; exports
  // (CSV / fixdat / miss-list) stay available for any result set.
  m_fixButton->setEnabled(canExport && hasApplicableFixes());
  m_exportCsvButton->setEnabled(canExport);
  m_exportFixdatButton->setEnabled(canExport);
  m_exportMissButton->setEnabled(canExport);
}

void DatAuditDialog::seedFixDialogDefaults(DatAuditFixDialog &dlg) {
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

void DatAuditDialog::offerRescrapeAfterFix(const DatAuditFixDialog &dlg) {
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

void DatAuditDialog::onFix() {
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

void DatAuditDialog::onBrowserReauditRequested(qint64 profileId) {
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
  selectProfileById(profileId); // keep the combo display in sync (no-op-safe)
  if (scanRoots().isEmpty() || datPaths().isEmpty()) {
    QMessageBox::information(this, tr("DAT Audit"),
                             tr("This profile has no folders or DAT files to audit."));
    return;
  }
  m_browserAuditPending = true; // onAuditFinished refreshes + re-selects the node
  onRun();
}

void DatAuditDialog::onBrowserFixRequested(qint64 profileId) {
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
  selectProfileById(profileId); // keep the combo display in sync (no-op-safe)

  // Fix from the persisted snapshot (Kartend-7iqhl.2): reconstruct AuditRows
  // from the stored result rows rather than running a fresh audit first.
  QList<DatAuditProfile::ResultRow> rows;
  bool readOk = false;
  withProfileDb([profileId, &rows, &readOk](QSqlDatabase &db) {
    if (auto r = DatAuditProfile::loadProfileResultRows(db, profileId); r.isOk()) {
      rows = r.value();
      readOk = true;
    } else {
      ErrorUtils::logError(r.error());
    }
  });
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
  } else if (m_browserPage != nullptr) {
    m_browserPage->refresh(); // can't re-audit; at least re-read the snapshot
    m_browserPage->selectProfileNode(profileId);
  }
  offerRescrapeAfterFix(dlg);
}

void DatAuditDialog::setScraperOpener(std::function<void(const QString &)> opener) {
  m_openScraperForCollection = std::move(opener);
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

// ---- Download page (Kartend-m6qsb.16) -------------------------------------

namespace {
// Accept either a bare DAT-o-MATIC system id or a daily-download URL carrying
// an "s=<id>" query item. Returns 0 when neither yields a positive id.
int parseSystemId(const QString &input) {
  const QString t = input.trimmed();
  bool ok = false;
  const int direct = t.toInt(&ok);
  if (ok && direct > 0) {
    return direct;
  }
  const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("[?&]s=(\\d+)")).match(t);
  return m.hasMatch() ? m.captured(1).toInt() : 0;
}
} // namespace

void DatAuditDialog::rebuildSetCheckboxes() {
  QLayoutItem *item = nullptr;
  while ((item = m_dlSetsLayout->takeAt(0)) != nullptr) {
    if (item->widget() != nullptr) {
      item->widget()->deleteLater();
    }
    delete item;
  }
  m_dlSetChecks.clear();
  for (const NoIntroParse::DailySet &s : m_dailyForm.sets) {
    auto *cb = new QCheckBox(s.label.isEmpty() ? s.field : s.label, m_dlSetsContainer);
    cb->setChecked(s.checkedByDefault);
    m_dlSetsLayout->addWidget(cb);
    m_dlSetChecks.append(cb);
  }
  m_dlDatTypeCombo->clear();
  for (const QString &dt : m_dailyForm.datTypes) {
    m_dlDatTypeCombo->addItem(dt);
  }
  const int idx = m_dlDatTypeCombo->findText(m_dailyForm.defaultDatType);
  m_dlDatTypeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  m_dlDatTypeCombo->setEnabled(m_dlDatTypeCombo->count() > 0);
}

DatAuditDialog::DownloadSource DatAuditDialog::currentDownloadSource() const {
  return m_dlSourceCombo->currentData().toString() == QLatin1String("redump")
             ? DownloadSource::Redump
             : DownloadSource::NoIntro;
}

void DatAuditDialog::refreshDownloadButtonEnabled() {
  bool ok = false;
  if (!m_downloading) {
    if (currentDownloadSource() == DownloadSource::NoIntro) {
      ok = m_dailyForm.valid;
    } else {
      ok = m_redumpSystemCombo->count() > 0 &&
           !m_redumpSystemCombo->currentData().toString().isEmpty();
    }
  }
  m_dlDownloadButton->setEnabled(ok);
}

void DatAuditDialog::setDownloadBusy(bool busy) {
  m_downloading = busy;
  m_dlSourceCombo->setEnabled(!busy);
  m_dlLoadButton->setEnabled(!busy);
  m_dlSystemEdit->setEnabled(!busy);
  m_redumpSystemCombo->setEnabled(!busy);
  m_redumpRefreshButton->setEnabled(!busy);
  m_dlCancelButton->setEnabled(busy);
  m_dlProgress->setVisible(busy);
  if (busy) {
    m_dlProgress->setRange(0, 0); // indeterminate until the transfer reports a total
  }
  refreshDownloadButtonEnabled();
}

void DatAuditDialog::onDownloadSourceChanged() {
  const bool redump = currentDownloadSource() == DownloadSource::Redump;
  m_noIntroBox->setVisible(!redump);
  m_setsBox->setVisible(!redump);
  m_redumpBox->setVisible(redump);
  // Lazily fetch the redump systems list the first time the user switches.
  if (redump && m_redumpSystemCombo->count() == 0 && !m_redumpSystemsWatcher.isRunning()) {
    m_redumpRefreshButton->setEnabled(false);
    m_dlStatus->setText(tr("Loading systems from redump.org…"));
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_redumpSystemsWatcher.setFuture(QtConcurrent::run([cancel]() -> RedumpSystemsOutcome {
      auto r = RedumpDownload::fetchSystems(cancel);
      if (r.isError()) {
        return RedumpSystemsOutcome{false, r.error().message, {}};
      }
      return RedumpSystemsOutcome{true, QString(), r.value()};
    }));
  }
  refreshDownloadButtonEnabled();
}

void DatAuditDialog::onRedumpSystemsFetched() {
  const RedumpSystemsOutcome o = m_redumpSystemsWatcher.result();
  m_redumpRefreshButton->setEnabled(true);
  if (!o.ok) {
    m_dlStatus->setText(o.error);
    return;
  }
  m_redumpSystemCombo->clear();
  for (const RedumpParse::System &s : o.systems) {
    m_redumpSystemCombo->addItem(s.name, s.slug);
  }
  m_dlStatus->setText(tr("%n system(s) available — pick one and Download & import.", nullptr,
                         static_cast<int>(o.systems.size())));
  refreshDownloadButtonEnabled();
}

void DatAuditDialog::onLoadDailyForm() {
  const int sysId = parseSystemId(m_dlSystemEdit->text());
  if (sysId <= 0) {
    m_dlStatus->setText(tr("Enter a DAT-o-MATIC system id or daily-download URL."));
    return;
  }
  m_dlCancel = std::make_shared<std::atomic<bool>>(false);
  setDownloadBusy(true);
  m_dlStatus->setText(tr("Loading available pack…"));
  auto cancel = m_dlCancel;
  m_dlFormWatcher.setFuture(QtConcurrent::run([sysId, cancel]() -> DailyFormOutcome {
    auto r = NoIntroDownload::fetchDailyForm(sysId, cancel);
    if (r.isError()) {
      return DailyFormOutcome{false, r.error().message, {}};
    }
    return DailyFormOutcome{true, QString(), r.value()};
  }));
}

void DatAuditDialog::onLoadDailyFormFinished() {
  const DailyFormOutcome o = m_dlFormWatcher.result();
  setDownloadBusy(false);
  if (!o.ok) {
    m_dailyForm = NoIntroParse::DailyForm{};
    m_dlDownloadButton->setEnabled(false);
    m_dlStatus->setText(o.error);
    return;
  }
  m_dailyForm = o.form;
  rebuildSetCheckboxes();
  m_dlPackLabel->setText(m_dailyForm.packDate.isEmpty()
                             ? tr("Available")
                             : tr("Available pack: %1").arg(m_dailyForm.packDate));
  refreshDownloadButtonEnabled();
  m_dlStatus->setText(tr("Ready. Choose sets, then Download & import."));
}

void DatAuditDialog::onStartDownload() {
  if (!m_getLibraryPath) {
    m_dlStatus->setText(tr("Set a DAT library folder on the DAT Library page first."));
    return;
  }
  QString lib = m_getLibraryPath().trimmed();
  if (lib.isEmpty()) {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a DAT library folder"));
    if (dir.isEmpty()) {
      return;
    }
    lib = dir;
    if (m_saveLibraryPath) {
      m_saveLibraryPath(dir);
    }
    m_libraryPathLabel->setText(dir);
  }
  const bool redump = currentDownloadSource() == DownloadSource::Redump;

  // Gather the source-specific request before flipping busy.
  NoIntroDownload::Options niOpts;
  QString redumpSlug;
  if (redump) {
    redumpSlug = m_redumpSystemCombo->currentData().toString();
    if (redumpSlug.isEmpty()) {
      m_dlStatus->setText(tr("Pick a system first."));
      return;
    }
  } else {
    const int sysId = parseSystemId(m_dlSystemEdit->text());
    if (sysId <= 0 || !m_dailyForm.valid) {
      m_dlStatus->setText(tr("Load a system first."));
      return;
    }
    niOpts.systemId = sysId;
    niOpts.datType = m_dlDatTypeCombo->currentText().isEmpty() ? m_dailyForm.defaultDatType
                                                               : m_dlDatTypeCombo->currentText();
    for (int i = 0; i < m_dlSetChecks.size() && i < m_dailyForm.sets.size(); ++i) {
      if (m_dlSetChecks.at(i)->isChecked()) {
        niOpts.selectedSets.append(m_dailyForm.sets.at(i).field);
      }
    }
  }

  m_dlCancel = std::make_shared<std::atomic<bool>>(false);
  setDownloadBusy(true);
  m_dlStatus->setText(tr("Downloading…"));
  auto cancel = m_dlCancel;
  // Marshal worker-thread progress onto the UI thread, throttle-free (the
  // transfer's own downloadProgress cadence is already coarse enough).
  auto progressFn = [this](const NoIntroDownload::Progress &p) {
    QMetaObject::invokeMethod(
        this,
        [this, p] {
          if (!m_downloading) {
            return;
          }
          if (p.total > 0) {
            m_dlProgress->setRange(0, 100);
            m_dlProgress->setValue(static_cast<int>(p.received * 100 / p.total));
          } else {
            m_dlProgress->setRange(0, 0);
          }
          if (!p.phase.isEmpty()) {
            m_dlStatus->setText(p.phase);
          }
        },
        Qt::QueuedConnection);
  };

  // No-Intro's revision is the daily pack date, known here on the UI thread;
  // Redump's is read from the downloaded DAT header inside the service.
  const QString packDate = redump ? QString() : m_dailyForm.packDate;
  DatAuditDownloadService::Request req;
  req.redump = redump;
  req.niOpts = niOpts;
  req.redumpSlug = redumpSlug;
  req.packDate = packDate;
  req.libraryDir = lib;
  m_dlWatcher.setFuture(m_downloadService->startDownload(req, cancel, progressFn));
}

void DatAuditDialog::onDownloadFinished() {
  const auto o = m_dlWatcher.result();
  setDownloadBusy(false);
  if (!o.ok) {
    m_dlStatus->setText(o.error);
    return;
  }
  m_dlStatus->setText(tr("Imported %n DAT file(s) into the library.", nullptr, o.datCount));
  m_downloadService->recordProvenance(o);
  // Surface matches immediately via the library review.
  if (m_libraryOpener) {
    m_libraryOpener();
  }
}

void DatAuditDialog::onCancelDownload() {
  if (m_dlCancel) {
    m_dlCancel->store(true);
  }
  m_dlCancelButton->setEnabled(false);
}

void DatAuditDialog::onCheckUpdates() {
  if (m_updateWatcher.isRunning()) {
    return;
  }
  const QList<DatLibraryState::Provenance> all = m_downloadService->trackedProvenance();
  if (all.isEmpty()) {
    QMessageBox::information(
        this, tr("Check for updates"),
        tr("No downloaded catalogues to check yet. Only DATs fetched via the Download page "
           "(No-Intro / Redump) are tracked for updates."));
    return;
  }
  const auto answer = QMessageBox::question(
      this, tr("Check for updates"),
      tr("Check your %n downloaded catalogue(s) against their source and re-download any with a "
         "newer version? This may download data.",
         nullptr, static_cast<int>(all.size())));
  if (answer != QMessageBox::Yes) {
    return;
  }
  const QString lib = m_getLibraryPath ? m_getLibraryPath().trimmed() : QString();
  m_updateCancel = std::make_shared<std::atomic<bool>>(false);
  auto cancel = m_updateCancel;
  m_checkUpdatesButton->setEnabled(false);
  m_checkUpdatesButton->setText(tr("Checking…"));
  m_updateWatcher.setFuture(m_downloadService->checkUpdates(all, lib, cancel));
}

void DatAuditDialog::onCheckUpdatesFinished() {
  m_checkUpdatesButton->setEnabled(true);
  m_checkUpdatesButton->setText(tr("Check for updates…"));
  const auto r = m_updateWatcher.result();
  int updated = 0;
  for (const auto &d : r.downloads) {
    if (d.ok) {
      m_downloadService->recordProvenance(d); // refresh stored version to the new revision
      ++updated;
    }
  }
  if (r.downloads.isEmpty()) {
    QMessageBox::information(
        this, tr("Check for updates"),
        tr("All %n downloaded catalogue(s) are up to date.", nullptr, r.checked));
    return;
  }
  const int failed = static_cast<int>(r.downloads.size()) - updated;
  QString msg = tr("Updated %n catalogue(s) to the latest version.", nullptr, updated);
  if (failed > 0) {
    msg += QLatin1Char(' ') + tr("%n could not be re-downloaded.", nullptr, failed);
  }
  QMessageBox::information(this, tr("Check for updates"), msg);
  if (m_libraryOpener) {
    m_libraryOpener(); // surface the refreshed catalogues
  }
}
