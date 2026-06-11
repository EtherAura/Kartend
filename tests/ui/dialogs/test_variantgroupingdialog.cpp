// Kartend-0eeuk: VariantGroupingDialog tree mapping + leaf-only callback
// gating. The grouping math itself lives in VariantGrouping::groupByBaseName
// (covered under tests/utils/); these tests cover the dialog's half: each
// group renders as a root row with its variants as children carrying the
// absolute path in Qt::UserRole, leaf labels prefer the lowercased ".ext"
// (falling back to the file name when there is no extension), the summary
// line aggregates group/variant counts, and the Launch / Reveal buttons fire
// their callbacks only for a selected leaf — never for a group header, an
// empty selection, or when no handler is installed. Driven headlessly — the
// dialog is never shown or exec()'d.

#include "variantgroupingdialog.h"

#include "variantgrouping.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

namespace {

QList<VariantGrouping::VariantGroup> sampleGroups() {
  VariantGrouping::VariantGroup concert;
  concert.baseName = QStringLiteral("Concert");
  concert.paths = {QStringLiteral("/m/Concert.MKV"), QStringLiteral("/m/Concert.flac")};
  VariantGrouping::VariantGroup notes;
  notes.baseName = QStringLiteral("LinerNotes");
  notes.paths = {QStringLiteral("/m/LinerNotes"), QStringLiteral("/m/LinerNotes.txt"),
                 QStringLiteral("/m/LinerNotes.pdf")};
  return {concert, notes};
}

QPushButton *buttonWithText(const QWidget &dlg, const QString &text) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

QLabel *labelContaining(const QWidget &dlg, const QString &needle) {
  const auto labels = dlg.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->text().contains(needle)) {
      return l;
    }
  }
  return nullptr;
}

} // namespace

class TestVariantGroupingDialog : public QObject {
  Q_OBJECT

private slots:
  void buildsOneRootPerGroupWithPathPayloads();
  void leafLabelsUseLowercasedExtensionOrFileName();
  void summaryAggregatesGroupAndVariantCounts();
  void launchFiresOnlyForSelectedLeaf();
  void revealFiresOnlyForSelectedLeaf();
  void missingHandlersAreSafeNoOps();
};

void TestVariantGroupingDialog::buildsOneRootPerGroupWithPathPayloads() {
  VariantGroupingDialog dlg(sampleGroups());
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 2);

  QTreeWidgetItem *concert = tree->topLevelItem(0);
  QCOMPARE(concert->childCount(), 2);
  QVERIFY(concert->text(0).contains(QStringLiteral("Concert")));
  QVERIFY(concert->text(0).contains(QStringLiteral("2 variants")));

  // Children carry the absolute path both as the payload and in column 1,
  // in input order.
  QCOMPARE(concert->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("/m/Concert.MKV"));
  QCOMPARE(concert->child(1)->data(0, Qt::UserRole).toString(), QStringLiteral("/m/Concert.flac"));
  QCOMPARE(concert->child(0)->text(1), QStringLiteral("/m/Concert.MKV"));

  QTreeWidgetItem *notes = tree->topLevelItem(1);
  QCOMPARE(notes->childCount(), 3);
}

void TestVariantGroupingDialog::leafLabelsUseLowercasedExtensionOrFileName() {
  VariantGroupingDialog dlg(sampleGroups());
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);

  // Uppercase extension renders lowercased.
  QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral(".mkv"));
  QCOMPARE(tree->topLevelItem(0)->child(1)->text(0), QStringLiteral(".flac"));
  // Extension-less file falls back to the plain file name.
  QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QStringLiteral("LinerNotes"));
}

void TestVariantGroupingDialog::summaryAggregatesGroupAndVariantCounts() {
  VariantGroupingDialog dlg(sampleGroups());
  QVERIFY(labelContaining(dlg, QStringLiteral("2 group(s), 5 variant file(s)")));

  VariantGroupingDialog empty({});
  QVERIFY(labelContaining(empty, QStringLiteral("0 group(s), 0 variant file(s)")));
}

void TestVariantGroupingDialog::launchFiresOnlyForSelectedLeaf() {
  VariantGroupingDialog dlg(sampleGroups());
  QStringList launched;
  dlg.setLaunchHandler([&launched](const QString &path) { launched << path; });

  auto *tree = dlg.findChild<QTreeWidget *>();
  QPushButton *launch = buttonWithText(dlg, QStringLiteral("Launch selected"));
  QVERIFY(tree && launch);

  // No selection — nothing fires.
  tree->setCurrentItem(nullptr);
  launch->click();
  QVERIFY(launched.isEmpty());

  // Group header selected — still nothing (headers carry no path payload).
  tree->setCurrentItem(tree->topLevelItem(0));
  launch->click();
  QVERIFY(launched.isEmpty());

  // Leaf selected — the handler receives that leaf's absolute path.
  tree->setCurrentItem(tree->topLevelItem(0)->child(1));
  launch->click();
  QCOMPARE(launched, QStringList{QStringLiteral("/m/Concert.flac")});
}

void TestVariantGroupingDialog::revealFiresOnlyForSelectedLeaf() {
  VariantGroupingDialog dlg(sampleGroups());
  QStringList revealed;
  dlg.setSelectHandler([&revealed](const QString &path) { revealed << path; });

  auto *tree = dlg.findChild<QTreeWidget *>();
  QPushButton *reveal = buttonWithText(dlg, QStringLiteral("Reveal in grid"));
  QVERIFY(tree && reveal);

  tree->setCurrentItem(tree->topLevelItem(1));
  reveal->click();
  QVERIFY(revealed.isEmpty()); // header — no payload

  tree->setCurrentItem(tree->topLevelItem(1)->child(2));
  reveal->click();
  QCOMPARE(revealed, QStringList{QStringLiteral("/m/LinerNotes.pdf")});
}

void TestVariantGroupingDialog::missingHandlersAreSafeNoOps() {
  VariantGroupingDialog dlg(sampleGroups());
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  tree->setCurrentItem(tree->topLevelItem(0)->child(0));
  // Neither handler installed — clicking must not crash.
  buttonWithText(dlg, QStringLiteral("Launch selected"))->click();
  buttonWithText(dlg, QStringLiteral("Reveal in grid"))->click();
  QVERIFY(true);
}

QTEST_MAIN(TestVariantGroupingDialog)
#include "test_variantgroupingdialog.moc"
