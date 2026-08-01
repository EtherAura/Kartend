// Sibling TU: results presentation for DatAuditAuditPage. Lives here: the
// status-filter combo entries (populateFilterCombo / onFilterChanged), the
// summary line + completion bar (updateSummary), result-row queries
// (hasResults / hasApplicableFixes / rowForProxyIndex), and the result
// table's double-click / context-menu handlers. Profile UI + audit-run
// orchestration stay in datauditauditpage.cpp; page construction lives in
// datauditauditpage_build.cpp; fix + export in datauditauditpage_fix.cpp.
#include "datauditauditpage.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QSortFilterProxyModel>
#include <QTableView>

#include "datauditmodel.h"

using DatAudit::AuditSummary;
using DatAudit::Status;

namespace {

// Status sets for each filter combo entry. Index 0 ("All") => no filter.
struct FilterEntry {
  const char *label;
  std::optional<QSet<Status>> statuses;
};

// Kartend-dfix4: QT_TRANSLATE_NOOP, not QT_TR_NOOP. This is a free function in
// an anonymous namespace, so QT_TR_NOOP gave lupdate no class to attribute the
// strings to — it warned "tr() cannot be called without context" and then
// DROPPED them, which is why none of these labels were in
// translations/kartend_en.ts and the filter combo was untranslatable.
//
// The context has to be the one the strings are looked up under at runtime.
// Both consumers (populateFilterCombo, onFilterChanged) are DatAuditAuditPage
// members calling tr(e.label), and DatAuditAuditPage is a Q_OBJECT at global
// scope — the `namespace DatAudit` block in the header is only a forward
// declaration of DatAuditModel — so that context is "DatAuditAuditPage",
// matching the existing context of the same name in the .ts seed. It is
// repeated literally on every entry because lupdate parses QT_TRANSLATE_NOOP
// statically; a shared constant would put it right back to extracting nothing.
QList<FilterEntry> filterEntries() {
  return {
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "All"), std::nullopt},
      // Framing presets: file-centric ("the files I own") vs entry-centric
      // ("how complete is the catalogue"). Both are just status subsets.
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Files I own"),
       QSet<Status>{Status::Have, Status::WrongName, Status::WrongHash, Status::Duplicate,
                    Status::Unknown, Status::Corrupt}},
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Catalogue completeness"),
       QSet<Status>{Status::Have, Status::WrongName, Status::Missing}},
      // Individual statuses.
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Have"), QSet<Status>{Status::Have}},
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Missing"), QSet<Status>{Status::Missing}},
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Wrong name"), QSet<Status>{Status::WrongName}},
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Wrong content"), QSet<Status>{Status::WrongHash}},
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Duplicate"), QSet<Status>{Status::Duplicate}},
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Unknown"), QSet<Status>{Status::Unknown}},
      {QT_TRANSLATE_NOOP("DatAuditAuditPage", "Corrupt"), QSet<Status>{Status::Corrupt}},
  };
}

} // namespace

void DatAuditAuditPage::populateFilterCombo() {
  // Called from buildAuditPage (sibling TU); the entries stay file-local here,
  // next to onFilterChanged's use of the same list.
  for (const auto &e : filterEntries()) {
    m_filterCombo->addItem(tr(e.label));
  }
}

void DatAuditAuditPage::onFilterChanged(int index) {
  const auto entries = filterEntries();
  if (index >= 0 && index < entries.size()) {
    m_model->setVisibleStatuses(entries.at(index).statuses);
  }
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
