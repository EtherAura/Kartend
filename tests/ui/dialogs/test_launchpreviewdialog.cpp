// Kartend-0eeuk: LaunchPreviewDialog preview-to-text mapping. The dialog is
// a read-only renderer of a LaunchPreview; its regression surface is the
// display-command construction (shell-style quoting of whitespace-bearing
// args + embedded-quote escaping), the buildOk fallback text, the
// exists/MISSING file status, the unresolved-executable branch, the
// archive-extraction line (target extension vs "no target extension set",
// hidden entirely when extraction does not apply), and the warnings list vs
// the all-clear message. setPreview is exercised directly; the dialog is
// never shown or exec()'d.

#include "launchpreviewdialog.h"

#include "launchpreview.h"

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTest>

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

QString commandText(const QWidget &dlg) {
  const auto *view = dlg.findChild<QPlainTextEdit *>();
  return view ? view->toPlainText() : QString();
}

LaunchPreview okPreview() {
  LaunchPreview p;
  p.buildOk = true;
  p.program = QStringLiteral("/usr/bin/player");
  p.arguments = {QStringLiteral("--fullscreen"), QStringLiteral("/m/clip.mkv")};
  p.resolvedProgram = QStringLiteral("/usr/bin/player");
  p.fileExists = true;
  return p;
}

} // namespace

class TestLaunchPreviewDialog : public QObject {
  Q_OBJECT

private slots:
  void plainArgumentsJoinUnquoted();
  void whitespaceArgumentsAreQuotedAndInnerQuotesEscaped();
  void buildFailureShowsPlaceholderAndWarning();
  void fileStatusMapsToExistsOrMissing();
  void unresolvedExecutableShowsNotFoundBranch();
  void archiveLineShowsTargetExtensionOrFallback();
  void archiveLineHiddenWhenExtractionDoesNotApply();
  void emptyTitleAndLauncherFallBackToPlaceholders();
};

void TestLaunchPreviewDialog::plainArgumentsJoinUnquoted() {
  LaunchPreviewDialog dlg;
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 okPreview());
  QCOMPARE(commandText(dlg), QStringLiteral("/usr/bin/player --fullscreen /m/clip.mkv"));
}

void TestLaunchPreviewDialog::whitespaceArgumentsAreQuotedAndInnerQuotesEscaped() {
  LaunchPreviewDialog dlg;
  LaunchPreview p = okPreview();
  p.program = QStringLiteral("/opt/media player/run");
  p.arguments = {QStringLiteral("--title"), QStringLiteral("My \"Best\" Clips"),
                 QStringLiteral("plain")};
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 p);
  // Program and the multi-word arg gain quotes; inner quotes are escaped;
  // the single-word arg stays bare.
  QCOMPARE(commandText(dlg),
           QStringLiteral("\"/opt/media player/run\" --title \"My \\\"Best\\\" Clips\" plain"));
}

void TestLaunchPreviewDialog::buildFailureShowsPlaceholderAndWarning() {
  LaunchPreviewDialog dlg;
  LaunchPreview p;
  p.buildOk = false;
  p.buildError = QStringLiteral("No launcher path configured");
  p.warnings = {p.buildError};
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 p);

  QCOMPARE(commandText(dlg), QStringLiteral("(no command — see warnings below)"));
  QLabel *warnings = labelContaining(dlg, QStringLiteral("No launcher path configured"));
  QVERIFY(warnings);
  QVERIFY(warnings->text().contains(QStringLiteral("• "))); // bulleted list form
  QVERIFY(!labelContaining(dlg, QStringLiteral("No problems detected.")));
}

void TestLaunchPreviewDialog::fileStatusMapsToExistsOrMissing() {
  LaunchPreviewDialog dlg;
  LaunchPreview p = okPreview();
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 p);
  QVERIFY(labelContaining(dlg, QStringLiteral("/m/clip.mkv — exists")));

  p.fileExists = false;
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 p);
  QVERIFY(labelContaining(dlg, QStringLiteral("/m/clip.mkv — MISSING")));
}

void TestLaunchPreviewDialog::unresolvedExecutableShowsNotFoundBranch() {
  LaunchPreviewDialog dlg;
  LaunchPreview p = okPreview();
  p.resolvedProgram.clear();
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 p);
  QVERIFY(labelContaining(dlg, QStringLiteral("(not found on PATH or disk)")));

  p.resolvedProgram = QStringLiteral("/usr/bin/player");
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 p);
  QVERIFY(labelContaining(dlg, QStringLiteral("Resolved executable: /usr/bin/player")));
}

void TestLaunchPreviewDialog::archiveLineShowsTargetExtensionOrFallback() {
  LaunchPreviewDialog dlg;
  LaunchPreview p = okPreview();
  p.wouldExtractArchive = true;
  p.archiveTargetExtension = QStringLiteral("mkv");
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.zip"),
                 p);
  QLabel *archive = labelContaining(dlg, QStringLiteral("Archive handling"));
  QVERIFY(archive);
  QVERIFY(archive->text().contains(QStringLiteral("*.mkv")));

  p.archiveTargetExtension = QStringLiteral("  "); // configured-but-blank
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.zip"),
                 p);
  archive = labelContaining(dlg, QStringLiteral("Archive handling"));
  QVERIFY(archive);
  QVERIFY(archive->text().contains(QStringLiteral("(no target extension set)")));
}

void TestLaunchPreviewDialog::archiveLineHiddenWhenExtractionDoesNotApply() {
  LaunchPreviewDialog dlg;
  LaunchPreview p = okPreview();
  p.wouldExtractArchive = true;
  p.archiveTargetExtension = QStringLiteral("mkv");
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.zip"),
                 p);
  QVERIFY(labelContaining(dlg, QStringLiteral("Archive handling")));

  // Re-render without extraction: the stale line must clear, not linger.
  p.wouldExtractArchive = false;
  dlg.setPreview(QStringLiteral("Clip"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 p);
  QVERIFY(!labelContaining(dlg, QStringLiteral("Archive handling")));
}

void TestLaunchPreviewDialog::emptyTitleAndLauncherFallBackToPlaceholders() {
  LaunchPreviewDialog dlg;
  dlg.setPreview(QString(), QString(), QStringLiteral("/m/clip.mkv"), okPreview());
  QVERIFY(labelContaining(dlg, QStringLiteral("Launcher: (unnamed)")));
  QVERIFY(labelContaining(dlg, QStringLiteral("Launch preview")));
  QVERIFY(labelContaining(dlg, QStringLiteral("No problems detected.")));

  // Titled variant escapes the item name into the rich-text header.
  dlg.setPreview(QStringLiteral("A & B"), QStringLiteral("Player"), QStringLiteral("/m/clip.mkv"),
                 okPreview());
  QVERIFY(labelContaining(dlg, QStringLiteral("Launch preview for: <b>A &amp; B</b>")));
}

QTEST_MAIN(TestLaunchPreviewDialog)
#include "test_launchpreviewdialog.moc"
