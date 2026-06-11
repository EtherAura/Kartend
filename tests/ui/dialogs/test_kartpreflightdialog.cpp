// Kartend-0eeuk: KartPreflightDialog check aggregation. The report itself is
// computed by KartPreflight::buildReport (covered in tests/modules/kart/);
// these tests cover the dialog's rendering decisions: a clean report shows
// the all-clear banner and hides the issues tree, a dirty report groups the
// three concern categories into top-level tree rows with per-group counts
// and per-issue status labels, and the summary table renders the bundle
// facts (conditional Type row, present/not-bundled wording, HTML escaping).
// Driven headlessly — the dialog is never shown or exec()'d.

#include "kartpreflightdialog.h"

#include "kartpreflight.h"

#include <QApplication>
#include <QLabel>
#include <QTest>
#include <QTreeWidget>

namespace {

QLabel *labelContaining(const QWidget &dlg, const QString &needle) {
  const auto labels = dlg.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->text().contains(needle)) {
      return l;
    }
  }
  return nullptr;
}

QTreeWidgetItem *topLevelItemNamed(const QTreeWidget *tree, const QString &name) {
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == name) {
      return tree->topLevelItem(i);
    }
  }
  return nullptr;
}

KartPreflight::PreflightReport makeCleanReport() {
  KartPreflight::PreflightReport r;
  r.collectionName = QStringLiteral("Documentaries");
  r.collectionType = QStringLiteral("Video");
  r.itemCount = 12;
  r.launcherCount = 3;
  r.hasMetadata = true;
  r.hasArtworkOverrides = false;
  return r;
}

} // namespace

class TestKartPreflightDialog : public QObject {
  Q_OBJECT

private slots:
  void cleanReportShowsAllClearAndHidesTree();
  void issuesGroupIntoCountedTreeSections();
  void summaryRendersBundleFactsAndEscapesHtml();
};

void TestKartPreflightDialog::cleanReportShowsAllClearAndHidesTree() {
  KartPreflightDialog dlg(makeCleanReport());

  QVERIFY(labelContaining(dlg, QStringLiteral("No validation issues")));
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QVERIFY(!tree->isVisibleTo(&dlg)); // nothing to review → tree withdrawn
  QCOMPARE(tree->topLevelItemCount(), 0);
}

void TestKartPreflightDialog::issuesGroupIntoCountedTreeSections() {
  KartPreflight::PreflightReport r = makeCleanReport();
  r.launcherIssues.append({QStringLiteral("Primary launcher"), QStringLiteral("/opt/player"),
                           KartPreflight::LauncherCheck::Missing});
  r.launcherIssues.append({QStringLiteral("Additional launcher: alt"), QStringLiteral("/opt/alt"),
                           KartPreflight::LauncherCheck::NotExecutable});
  r.suspiciousPaths.append({QStringLiteral("mediaDirectory"), QStringLiteral("/etc/shadow")});
  r.nameConflicts = true;

  KartPreflightDialog dlg(r);
  QVERIFY(labelContaining(dlg, QStringLiteral("Validation concerns")));
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QVERIFY(tree->isVisibleTo(&dlg));
  QCOMPARE(tree->topLevelItemCount(), 3); // launchers + suspicious + conflict

  QTreeWidgetItem *launchers = topLevelItemNamed(tree, QStringLiteral("Launchers"));
  QVERIFY(launchers);
  QCOMPARE(launchers->text(1), QStringLiteral("2")); // per-group count
  QCOMPARE(launchers->childCount(), 2);
  QCOMPARE(launchers->child(0)->text(0), QStringLiteral("Primary launcher"));
  QCOMPARE(launchers->child(0)->text(1),
           KartPreflight::launcherCheckLabel(KartPreflight::LauncherCheck::Missing));
  QCOMPARE(launchers->child(0)->text(2), QStringLiteral("/opt/player"));
  QCOMPARE(launchers->child(1)->text(1),
           KartPreflight::launcherCheckLabel(KartPreflight::LauncherCheck::NotExecutable));

  QTreeWidgetItem *suspicious = topLevelItemNamed(tree, QStringLiteral("Suspicious paths"));
  QVERIFY(suspicious);
  QCOMPARE(suspicious->text(1), QStringLiteral("1"));
  QCOMPARE(suspicious->childCount(), 1);
  QCOMPARE(suspicious->child(0)->text(0), QStringLiteral("mediaDirectory"));
  QCOMPARE(suspicious->child(0)->text(2), QStringLiteral("/etc/shadow"));

  QVERIFY(topLevelItemNamed(tree, QStringLiteral("Name conflict")));
}

void TestKartPreflightDialog::summaryRendersBundleFactsAndEscapesHtml() {
  KartPreflight::PreflightReport r = makeCleanReport();
  r.collectionName = QStringLiteral("<Docs & Talks>");
  r.hasMetadata = false;
  r.hasArtworkOverrides = true;

  KartPreflightDialog dlg(r);
  // The summary table carries the escaped name — a raw '<' would be
  // swallowed as a bogus rich-text tag and the name would vanish.
  QLabel *summary = labelContaining(dlg, QStringLiteral("&lt;Docs &amp; Talks&gt;"));
  QVERIFY(summary);
  QVERIFY(summary->text().contains(QStringLiteral("12"))); // item count
  QVERIFY(summary->text().contains(QStringLiteral("3")));  // launcher count
  QVERIFY(summary->text().contains(QStringLiteral("Video")));
  QVERIFY(summary->text().contains(QStringLiteral("not bundled"))); // metadata absent
  QVERIFY(summary->text().contains(QStringLiteral("present")));     // artwork overrides

  // Blank type → the Type row is omitted entirely, not rendered empty.
  KartPreflight::PreflightReport untyped = makeCleanReport();
  untyped.collectionType = QStringLiteral("   ");
  KartPreflightDialog dlg2(untyped);
  QLabel *summary2 = labelContaining(dlg2, QStringLiteral("Documentaries"));
  QVERIFY(summary2);
  QVERIFY(!summary2->text().contains(QStringLiteral("Type")));
}

QTEST_MAIN(TestKartPreflightDialog)
#include "test_kartpreflightdialog.moc"
