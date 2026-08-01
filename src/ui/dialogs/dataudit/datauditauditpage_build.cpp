// Sibling TU: page construction for DatAuditAuditPage. The per-section
// build* helpers live here so datauditauditpage.cpp keeps the profile UI +
// audit-run orchestration (wiring — wireProfilePanel / wireAuditActions —
// included); results presentation lives in datauditauditpage_results.cpp and
// the fix/export flows in datauditauditpage_fix.cpp.
#include "datauditauditpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include "datauditmodel.h"
#include "datauditprofilepanel.h"
#include "datauditresultdelegate.h"
#include "formbuilders.h"

QWidget *DatAuditAuditPage::buildAuditPage(DatAuditProfileStore &profileStore) {
  auto *page = new QWidget(this);
  auto *root = new QVBoxLayout(page);
  root->setContentsMargins(0, 0, 0, 0);

  // Composed from the per-section builders below (Kartend-139sr); the
  // add-order here is the construction order the page has always had.

  // Saved profiles: the combo + CRUD row is its own widget — it owns the
  // profile-CRUD controller and announces every selection / CRUD outcome;
  // wireProfilePanel() (main TU) adopts them into the working profile.
  m_profilePanel = new DatAuditProfilePanel(profileStore, page);
  root->addWidget(m_profilePanel);

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
  populateFilterCombo(); // entries live next to onFilterChanged in the main TU
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

QLayout *DatAuditAuditPage::buildInputsSection(QWidget *page) {
  Q_UNUSED(page);
  auto *inputs = new QHBoxLayout();
  inputs->addWidget(FormBuilders::makePathListGroup(tr("DAT files"), m_datList, m_addDatButton,
                                                    m_removeDatButton, tr("Add DAT…")));
  inputs->addWidget(FormBuilders::makePathListGroup(tr("Scan folders"), m_rootList, m_addRootButton,
                                                    m_removeRootButton, tr("Add folder…")));
  return inputs;
}

QLayout *DatAuditAuditPage::buildLayoutBanner(QWidget *page) {
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

QWidget *DatAuditAuditPage::buildResultsTable(QWidget *page) {
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
          &DatAuditAuditPage::onResultsContextMenu);
  connect(m_table, &QTableView::doubleClicked, this, &DatAuditAuditPage::onResultDoubleClicked);
  return m_table;
}

QLayout *DatAuditAuditPage::buildExportRow(QWidget *page) {
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
