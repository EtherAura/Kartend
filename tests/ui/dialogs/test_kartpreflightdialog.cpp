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
  void launcherConfigurationSuppressesTheAllClear();
  void elevatedLauncherFindingRaisesTheBanner();
  void extractionDestinationRowRendersCountAndEscapedName();
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

void TestKartPreflightDialog::launcherConfigurationSuppressesTheAllClear() {
  // Kartend-kxqqf: a bundle that brings a launcher configuration is
  // executable content. Even when nothing about it looks unusual, the dialog
  // must show it rather than answer "no validation issues" — the banner it
  // used to show for exactly this bundle.
  KartPreflight::PreflightReport r = makeCleanReport();
  r.launcherTrust.append({QStringLiteral("launcher.launcherPath"), QStringLiteral("/usr/bin/mpv"),
                          kart::LauncherTrustReason::BundleSupplied});
  r.launcherTrust.append({QStringLiteral("launcher.launchParameters"),
                          QStringLiteral("--fullscreen %media%"),
                          kart::LauncherTrustReason::BundleSupplied});

  KartPreflightDialog dlg(r);
  QVERIFY(!labelContaining(dlg, QStringLiteral("No validation issues")));
  QVERIFY(labelContaining(dlg, QStringLiteral("brings its own launcher configuration")));

  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QVERIFY(tree->isVisibleTo(&dlg));
  QTreeWidgetItem *group = topLevelItemNamed(tree, QStringLiteral("Launcher configuration"));
  QVERIFY(group);
  QCOMPARE(group->text(1), QStringLiteral("2"));
  QCOMPARE(group->childCount(), 2);
  QCOMPARE(group->child(0)->text(0), QStringLiteral("launcher.launcherPath"));
  QCOMPARE(group->child(0)->text(1),
           kart::launcherTrustReasonLabel(kart::LauncherTrustReason::BundleSupplied));
  // The value is shown verbatim — the point of the section is that the user
  // can read what will run.
  QCOMPARE(group->child(1)->text(2), QStringLiteral("--fullscreen %media%"));
}

void TestKartPreflightDialog::elevatedLauncherFindingRaisesTheBanner() {
  // A danger signal escalates the wording; the allowlist-clean case above
  // stays calmer, so the loud banner keeps its meaning.
  KartPreflight::PreflightReport r = makeCleanReport();
  r.launcherTrust.append({QStringLiteral("launcher.launcherPath"),
                          QStringLiteral("/usr/bin/python3"),
                          kart::LauncherTrustReason::InterpreterProgram});

  KartPreflightDialog dlg(r);
  QVERIFY(!labelContaining(dlg, QStringLiteral("No validation issues")));
  QVERIFY(labelContaining(dlg, QStringLiteral("asks to run something unusual")));
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QTreeWidgetItem *group = topLevelItemNamed(tree, QStringLiteral("Launcher configuration"));
  QVERIFY(group);
  QCOMPARE(group->child(0)->text(1),
           kart::launcherTrustReasonLabel(kart::LauncherTrustReason::InterpreterProgram));
}

void TestKartPreflightDialog::extractionDestinationRowRendersCountAndEscapedName() {
  // Kartend-qbfk1: the summary states what accepting means on disk — the
  // would-write file count and the fresh bundle-named folder, name escaped
  // like every other user-controlled string in the table.
  KartPreflight::PreflightReport r = makeCleanReport();
  r.extractionSubdirName = QStringLiteral("<Docs & Talks>");
  r.bundleEntryCount = 14;
  KartPreflightDialog dlg(r);
  QLabel *row = labelContaining(dlg, QStringLiteral("Extracts to"));
  QVERIFY(row);
  QVERIFY(row->text().contains(QStringLiteral("14")));
  QVERIFY(row->text().contains(QStringLiteral("&lt;Docs &amp; Talks&gt;")));
  QVERIFY(row->text().contains(QStringLiteral("new folder")));

  // Unknown count (the container walk failed; extraction will error on its
  // own) → the folder line renders without a file count.
  KartPreflight::PreflightReport unknown = makeCleanReport();
  unknown.extractionSubdirName = QStringLiteral("Docs");
  unknown.bundleEntryCount = -1;
  KartPreflightDialog dlg2(unknown);
  QLabel *row2 = labelContaining(dlg2, QStringLiteral("new folder"));
  QVERIFY(row2);
  QVERIFY(!row2->text().contains(QStringLiteral("file")));

  // No subdirectory name supplied (a report built outside the import flow)
  // → the row is omitted entirely, matching the blank-Type convention.
  KartPreflightDialog dlg3(makeCleanReport());
  QVERIFY(!labelContaining(dlg3, QStringLiteral("Extracts to")));
}

QTEST_MAIN(TestKartPreflightDialog)
#include "test_kartpreflightdialog.moc"
