// Kartend-0eeuk: CollectionHealthDialog report classification + rendering
// decisions. The analyzer (CollectionHealth::analyze) is pure logic covered
// elsewhere; the dialog's own regression surface is the issue-count
// aggregation (missing files + missing artwork + launcher issues) driving
// the OK-vs-problem summary branch, the per-category sample blocks with the
// "and N more" overflow math, the launcher-issue record formatting, and the
// HTML escaping of the collection name. setReport is exercised directly on
// a hand-built Report; the dialog is never shown or exec()'d.

#include "collectionhealthdialog.h"

#include "collectionhealth.h"

#include <QApplication>
#include <QLabel>
#include <QTest>
#include <QTextEdit>

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

QString detailText(const QWidget &dlg) {
  const auto *view = dlg.findChild<QTextEdit *>();
  return view ? view->toPlainText() : QString();
}

} // namespace

class TestCollectionHealthDialog : public QObject {
  Q_OBJECT

private slots:
  void cleanReportRendersNoIssuesSummary();
  void issueCountAggregatesAllThreeCategories();
  void sampleOverflowRendersAndNMore();
  void launcherIssuesRenderIndexLabelAndMessage();
  void headerEscapesCollectionName();
  void reRenderReplacesPreviousReport();
};

void TestCollectionHealthDialog::cleanReportRendersNoIssuesSummary() {
  CollectionHealthDialog dlg;
  CollectionHealth::Report report;
  report.totalItems = 12;
  dlg.setReport(QStringLiteral("Field recordings"), report);

  QVERIFY(labelContaining(dlg, QStringLiteral("No issues detected across 12 item(s).")));
  const QString detail = detailText(dlg);
  QVERIFY(detail.contains(QStringLiteral("Missing files: 0")));
  QVERIFY(detail.contains(QStringLiteral("Missing artwork: 0")));
  QVERIFY(detail.contains(QStringLiteral("Launcher validity: OK")));
}

void TestCollectionHealthDialog::issueCountAggregatesAllThreeCategories() {
  CollectionHealthDialog dlg;
  CollectionHealth::Report report;
  report.totalItems = 9;
  report.missingFileCount = 2;
  report.missingFileSamples = {QStringLiteral("/m/a.mkv"), QStringLiteral("/m/b.mkv")};
  report.missingArtworkCount = 3;
  report.missingArtworkSamples = {QStringLiteral("/m/c.mkv")};
  CollectionHealth::LauncherIssue issue;
  issue.launcherIndex = 0;
  issue.launcherLabel = QStringLiteral("player");
  issue.issue = QStringLiteral("No launcher path configured");
  report.launcherIssues = {issue};
  dlg.setReport(QStringLiteral("Videos"), report);

  // 2 + 3 + 1 — every category contributes to the headline number.
  QVERIFY(labelContaining(dlg, QStringLiteral("6 issue(s) across 9 item(s).")));
  const QString detail = detailText(dlg);
  QVERIFY(detail.contains(QStringLiteral("Missing files: 2")));
  QVERIFY(detail.contains(QStringLiteral("/m/a.mkv")));
  QVERIFY(detail.contains(QStringLiteral("Missing artwork: 3")));
  QVERIFY(detail.contains(QStringLiteral("/m/c.mkv")));
}

void TestCollectionHealthDialog::sampleOverflowRendersAndNMore() {
  CollectionHealthDialog dlg;
  CollectionHealth::Report report;
  report.totalItems = 30;
  report.missingFileCount = 25;
  for (int i = 0; i < CollectionHealth::kMaxSamples; ++i) {
    report.missingFileSamples << QStringLiteral("/m/missing-%1.mkv").arg(i);
  }
  dlg.setReport(QStringLiteral("Videos"), report);

  const QString detail = detailText(dlg);
  // 25 total, 20 samples → 5 hidden.
  QVERIFY(detail.contains(QStringLiteral("…and 5 more")));
}

void TestCollectionHealthDialog::launcherIssuesRenderIndexLabelAndMessage() {
  CollectionHealthDialog dlg;
  CollectionHealth::Report report;
  report.totalItems = 4;
  CollectionHealth::LauncherIssue first;
  first.launcherIndex = 0;
  first.launcherLabel = QStringLiteral("primary-player");
  first.issue = QStringLiteral("Executable not found in PATH: primary-player");
  CollectionHealth::LauncherIssue second;
  second.launcherIndex = 2;
  second.launcherLabel = QStringLiteral("alt-viewer");
  second.issue = QStringLiteral("No launcher path configured");
  report.launcherIssues = {first, second};
  dlg.setReport(QStringLiteral("Videos"), report);

  const QString detail = detailText(dlg);
  QVERIFY(detail.contains(QStringLiteral("Launcher issues: 2")));
  QVERIFY(detail.contains(
      QStringLiteral("[0] primary-player: Executable not found in PATH: primary-player")));
  QVERIFY(detail.contains(QStringLiteral("[2] alt-viewer: No launcher path configured")));
  QVERIFY(!detail.contains(QStringLiteral("Launcher validity: OK")));
}

void TestCollectionHealthDialog::headerEscapesCollectionName() {
  CollectionHealthDialog dlg;
  CollectionHealth::Report report;
  dlg.setReport(QStringLiteral("Mix <tapes> & more"), report);
  // The header label is rich text; an unescaped name would inject markup.
  QVERIFY(labelContaining(dlg, QStringLiteral("Mix &lt;tapes&gt; &amp; more")));
}

void TestCollectionHealthDialog::reRenderReplacesPreviousReport() {
  CollectionHealthDialog dlg;
  CollectionHealth::Report broken;
  broken.totalItems = 5;
  broken.missingFileCount = 5;
  broken.missingFileSamples = {QStringLiteral("/m/gone.mkv")};
  dlg.setReport(QStringLiteral("Videos"), broken);
  QVERIFY(labelContaining(dlg, QStringLiteral("5 issue(s) across 5 item(s).")));

  CollectionHealth::Report clean;
  clean.totalItems = 5;
  dlg.setReport(QStringLiteral("Videos"), clean);
  QVERIFY(labelContaining(dlg, QStringLiteral("No issues detected across 5 item(s).")));
  QVERIFY(!detailText(dlg).contains(QStringLiteral("/m/gone.mkv")));
}

QTEST_MAIN(TestCollectionHealthDialog)
#include "test_collectionhealthdialog.moc"
