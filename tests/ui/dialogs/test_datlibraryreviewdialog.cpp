/**
 * @file test_datlibraryreviewdialog.cpp
 * @brief Tests for the DAT-library review's optional-association combo
 *        (Kartend-m6qsb.18).
 *
 * Drives the dialog headlessly via injected Hooks: verifies each row offers
 * "(No collection)" and "Add to new collection…" ahead of the ranked
 * candidates, and that "Apply selected" routes to the right hook (or none) per
 * the chosen entry. No DB, no network.
 */

#include "datlibraryreviewdialog.h"

#include "datcollectionmatch.h"
#include "datlibraryscan.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QList>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

using DatCollectionMatch::Candidate;
using DatLibraryScan::Proposal;
using DatLibraryScan::ScanResult;

namespace {

ScanResult oneProposal(const QString &datPath, const QString &candidateUuid) {
  Proposal p;
  p.datPath = datPath;
  p.canonicalPath = datPath;
  p.mtimeMs = 1000;
  p.headerName = QStringLiteral("Reference Video Set");
  Candidate c;
  c.collectionUuid = candidateUuid;
  c.score = 0.9;
  c.reason = QStringLiteral("name match");
  p.candidates = {c};
  ScanResult r;
  r.proposals = {p};
  return r;
}

Proposal unmatchedProposal(const QString &datPath) {
  Proposal p;
  p.datPath = datPath;
  p.canonicalPath = datPath;
  p.mtimeMs = 1000;
  p.headerName = QStringLiteral("Orphan Catalogue");
  // No candidates → unmatched.
  return p;
}

QComboBox *comboForRow(const QWidget &dlg, int row) {
  auto *tree = dlg.findChild<QTreeWidget *>();
  if (tree == nullptr || row < 0 || row >= tree->topLevelItemCount()) {
    return nullptr;
  }
  QTreeWidgetItem *item = tree->topLevelItem(row);
  tree->setCurrentItem(item); // select so Apply acts on it
  return qobject_cast<QComboBox *>(tree->itemWidget(item, 2));
}

QComboBox *rowCombo(const QWidget &dlg) {
  return comboForRow(dlg, 0);
}

QCheckBox *showUnmatchedBox(const QWidget &dlg) {
  return dlg.findChild<QCheckBox *>();
}

QPushButton *buttonByText(const QWidget &dlg, const QString &text) {
  for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

int treeRows(const QWidget &dlg) {
  auto *tree = dlg.findChild<QTreeWidget *>();
  return tree != nullptr ? tree->topLevelItemCount() : -1;
}

} // namespace

class TestDatLibraryReviewDialog : public QObject {
  Q_OBJECT

private slots:
  void comboOffersNoCollectionAndNewCollection();
  void noCollectionAppliesWithoutAttaching();
  void candidateAppliesAttach();
  void newCollectionInvokesCreateHook();
  void newCollectionCancelKeepsRow();
  void unmatchedHiddenUntilToggled();
  void unmatchedRowOffersAllCollectionsForManualAttach();
  void attachAllBestMatchesAttachesEveryMatched();
};

void TestDatLibraryReviewDialog::comboOffersNoCollectionAndNewCollection() {
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), {},
                             oneProposal(QStringLiteral("/lib/a.dat"), QStringLiteral("uuid-1")),
                             {});
  QComboBox *combo = rowCombo(dlg);
  QVERIFY(combo);
  QVERIFY(combo->itemText(0).contains(QStringLiteral("No collection")));
  QVERIFY(combo->itemData(0).toString().isEmpty());
  QVERIFY(combo->itemText(1).contains(QStringLiteral("new collection")));
  QCOMPARE(combo->itemData(1).toString(), QStringLiteral("+new"));
  // The real candidate is present (after the separator) carrying its uuid.
  QCOMPARE(combo->findData(QStringLiteral("uuid-1")) >= 0, true);
  // Default selection is the candidate, not a synthetic entry.
  QCOMPARE(combo->currentData().toString(), QStringLiteral("uuid-1"));
}

void TestDatLibraryReviewDialog::noCollectionAppliesWithoutAttaching() {
  bool attached = false;
  bool created = false;
  DatLibraryReviewDialog::Hooks hooks;
  hooks.attach = [&](const QString &, const QString &) { attached = true; };
  hooks.addToNewCollection = [&](const QString &) {
    created = true;
    return QStringLiteral("x");
  };
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), {},
                             oneProposal(QStringLiteral("/lib/a.dat"), QStringLiteral("uuid-1")),
                             hooks);
  QComboBox *combo = rowCombo(dlg);
  QVERIFY(combo);
  combo->setCurrentIndex(0); // (No collection)
  buttonByText(dlg, QStringLiteral("Apply selected"))->click();
  QVERIFY(!attached);         // nothing attached…
  QVERIFY(!created);          // …and no collection created
  QCOMPARE(treeRows(dlg), 0); // row acknowledged + removed
}

void TestDatLibraryReviewDialog::candidateAppliesAttach() {
  QString gotUuid;
  QString gotPath;
  DatLibraryReviewDialog::Hooks hooks;
  hooks.attach = [&](const QString &uuid, const QString &path) {
    gotUuid = uuid;
    gotPath = path;
  };
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), {},
                             oneProposal(QStringLiteral("/lib/a.dat"), QStringLiteral("uuid-1")),
                             hooks);
  QComboBox *combo = rowCombo(dlg);
  QVERIFY(combo);
  combo->setCurrentIndex(combo->findData(QStringLiteral("uuid-1")));
  buttonByText(dlg, QStringLiteral("Apply selected"))->click();
  QCOMPARE(gotUuid, QStringLiteral("uuid-1"));
  QCOMPARE(gotPath, QStringLiteral("/lib/a.dat"));
  QCOMPARE(treeRows(dlg), 0);
}

void TestDatLibraryReviewDialog::newCollectionInvokesCreateHook() {
  QString createdForPath;
  DatLibraryReviewDialog::Hooks hooks;
  hooks.addToNewCollection = [&](const QString &path) {
    createdForPath = path;
    return QStringLiteral("new-uuid"); // non-empty => created
  };
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), {},
                             oneProposal(QStringLiteral("/lib/a.dat"), QStringLiteral("uuid-1")),
                             hooks);
  QComboBox *combo = rowCombo(dlg);
  QVERIFY(combo);
  combo->setCurrentIndex(1); // Add to new collection…
  buttonByText(dlg, QStringLiteral("Apply selected"))->click();
  QCOMPARE(createdForPath, QStringLiteral("/lib/a.dat"));
  QCOMPARE(treeRows(dlg), 0);
}

void TestDatLibraryReviewDialog::newCollectionCancelKeepsRow() {
  DatLibraryReviewDialog::Hooks hooks;
  hooks.addToNewCollection = [&](const QString &) {
    return QString(); // cancelled
  };
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), {},
                             oneProposal(QStringLiteral("/lib/a.dat"), QStringLiteral("uuid-1")),
                             hooks);
  QComboBox *combo = rowCombo(dlg);
  QVERIFY(combo);
  combo->setCurrentIndex(1); // Add to new collection… then cancel
  buttonByText(dlg, QStringLiteral("Apply selected"))->click();
  QCOMPARE(treeRows(dlg), 1); // cancellation leaves the row for another try
}

void TestDatLibraryReviewDialog::unmatchedHiddenUntilToggled() {
  ScanResult r = oneProposal(QStringLiteral("/lib/a.dat"), QStringLiteral("uuid-1"));
  r.unmatched = {unmatchedProposal(QStringLiteral("/lib/orphan.dat"))};
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), {}, r, {});
  QCOMPARE(treeRows(dlg), 1); // only the matched proposal
  QCheckBox *box = showUnmatchedBox(dlg);
  QVERIFY(box);
  box->setChecked(true);
  QCOMPARE(treeRows(dlg), 2); // unmatched now shown
  box->setChecked(false);
  QCOMPARE(treeRows(dlg), 1);
}

void TestDatLibraryReviewDialog::unmatchedRowOffersAllCollectionsForManualAttach() {
  ScanResult r;
  r.unmatched = {unmatchedProposal(QStringLiteral("/lib/orphan.dat"))};
  const QList<DatCollectionMatch::CollectionInfo> collections{
      {QStringLiteral("uuid-a"), QStringLiteral("Movies"), {}, {}},
      {QStringLiteral("uuid-b"), QStringLiteral("Music"), {}, {}}};
  QString attachedUuid;
  QString attachedPath;
  DatLibraryReviewDialog::Hooks hooks;
  hooks.attach = [&](const QString &u, const QString &p) {
    attachedUuid = u;
    attachedPath = p;
  };
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), collections, r, hooks);
  showUnmatchedBox(dlg)->setChecked(true);
  QComboBox *combo = comboForRow(dlg, 0);
  QVERIFY(combo);
  // Manual pick: every collection is offered (no ranked candidates), default none.
  QVERIFY(combo->currentData().toString().isEmpty());
  const int idx = combo->findData(QStringLiteral("uuid-b"));
  QVERIFY(idx > 0);
  combo->setCurrentIndex(idx);
  buttonByText(dlg, QStringLiteral("Apply selected"))->click();
  QCOMPARE(attachedUuid, QStringLiteral("uuid-b"));
  QCOMPARE(attachedPath, QStringLiteral("/lib/orphan.dat"));
}

void TestDatLibraryReviewDialog::attachAllBestMatchesAttachesEveryMatched() {
  ScanResult r;
  r.proposals = {
      oneProposal(QStringLiteral("/lib/a.dat"), QStringLiteral("uuid-1")).proposals.first(),
      oneProposal(QStringLiteral("/lib/b.dat"), QStringLiteral("uuid-2")).proposals.first()};
  r.unmatched = {unmatchedProposal(QStringLiteral("/lib/orphan.dat"))};
  QList<QPair<QString, QString>> attached;
  DatLibraryReviewDialog::Hooks hooks;
  hooks.attach = [&](const QString &u, const QString &p) { attached.append({u, p}); };
  DatLibraryReviewDialog dlg(QStringLiteral("/lib"), {}, r, hooks);
  buttonByText(dlg, QStringLiteral("Attach all best matches"))->click();
  QCOMPARE(attached.size(), 2); // both matched, to their best candidate
  QCOMPARE(attached.at(0).first, QStringLiteral("uuid-1"));
  QCOMPARE(attached.at(1).first, QStringLiteral("uuid-2"));
  // Matched rows cleared; unmatched untouched (not shown unless toggled).
  QCOMPARE(treeRows(dlg), 0);
}

QTEST_MAIN(TestDatLibraryReviewDialog)
#include "test_datlibraryreviewdialog.moc"
