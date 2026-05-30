// Tests for LauncherUtils::launcherPathIssues — the toolbar-warning-badge
// + sidebar-issue-list helper introduced for Kartend-w2n0. Verifies that
// the helper:
//   - Returns an empty list when every launcher slot is OK or empty.
//   - Skips empty paths silently (empty = "not configured", not "broken").
//   - Flags a broken primary launcher with the localized "Launcher"
//     fieldLabel + the original raw path.
//   - Numbers additional launchers from 1 (the chooser's user-visible index
//     for "Additional launcher N") even when their `name` field is set.
//   - Embeds the launcher's user-supplied name in parens for additional
//     launchers when it's non-empty so the user can identify which row to
//     fix.
//
// Real PathUtils::checkLauncherPath is intentional (no mocking): the helper
// is a thin wrapper around it, and any test fixture would just be
// reimplementing the helper. We use known-bad paths (clearly nonexistent
// directories with embedded slashes so the PATH lookup isn't invoked) to
// keep the assertions deterministic across hosts.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "collection/launcherconfig.h"

class TestLauncherPathIssues : public QObject {
  Q_OBJECT
private slots:
  void emptyProfile_returnsNoIssues();
  void emptyPathsSkipped_returnsNoIssues();
  void brokenPrimary_returnsOneIssueWithLauncherLabel();
  void brokenAdditional_unnamedUsesNumberedLabel();
  void brokenAdditional_namedAppendsNameInParens();
  void mixedSlots_returnsOneIssuePerBrokenSlot();
};

void TestLauncherPathIssues::emptyProfile_returnsNoIssues() {
  LauncherProfile profile;
  QCOMPARE(LauncherUtils::launcherPathIssues(profile).size(), 0);
}

void TestLauncherPathIssues::emptyPathsSkipped_returnsNoIssues() {
  LauncherProfile profile;
  // Empty primary path. Additional launchers are also empty. We rely on the
  // helper distinguishing "unconfigured" from "broken" — an empty path is
  // the user's signal that the slot isn't set yet, not a failure.
  profile.launcherPath = QString();
  LauncherConfig al;
  al.name = "Extra";
  al.launcherPath = QString();
  profile.additionalLaunchers.append(al);
  QCOMPARE(LauncherUtils::launcherPathIssues(profile).size(), 0);
}

void TestLauncherPathIssues::brokenPrimary_returnsOneIssueWithLauncherLabel() {
  LauncherProfile profile;
  // Embedded slash forces the helper down the on-disk check branch rather
  // than the PATH lookup, so the assertion is host-independent.
  profile.launcherPath = "/nonexistent/path/to/launcher";
  const QStringList issues = LauncherUtils::launcherPathIssues(profile);
  QCOMPARE(issues.size(), 1);
  QVERIFY(issues.first().startsWith("Launcher ("));
  QVERIFY(issues.first().contains("/nonexistent/path/to/launcher"));
}

void TestLauncherPathIssues::brokenAdditional_unnamedUsesNumberedLabel() {
  LauncherProfile profile;
  LauncherConfig al;
  al.launcherPath = "/nonexistent/path/extra";
  profile.additionalLaunchers.append(al);
  const QStringList issues = LauncherUtils::launcherPathIssues(profile);
  QCOMPARE(issues.size(), 1);
  // Numbered from 1 because that's what the chooser shows the user; an
  // unnamed entry would otherwise be unidentifiable.
  QVERIFY(issues.first().startsWith("Additional launcher 1 ("));
}

void TestLauncherPathIssues::brokenAdditional_namedAppendsNameInParens() {
  LauncherProfile profile;
  LauncherConfig al;
  al.name = "RetroArch";
  al.launcherPath = "/nonexistent/path/retroarch";
  profile.additionalLaunchers.append(al);
  const QStringList issues = LauncherUtils::launcherPathIssues(profile);
  QCOMPARE(issues.size(), 1);
  QVERIFY(issues.first().startsWith("Additional launcher 1 (RetroArch) ("));
}

void TestLauncherPathIssues::mixedSlots_returnsOneIssuePerBrokenSlot() {
  LauncherProfile profile;
  // Primary broken.
  profile.launcherPath = "/nonexistent/primary";
  // Empty extra (skipped).
  LauncherConfig empty;
  profile.additionalLaunchers.append(empty);
  // Broken named extra.
  LauncherConfig named;
  named.name = "MPV";
  named.launcherPath = "/nonexistent/mpv";
  profile.additionalLaunchers.append(named);
  const QStringList issues = LauncherUtils::launcherPathIssues(profile);
  QCOMPARE(issues.size(), 2);
  QVERIFY(issues.at(0).startsWith("Launcher ("));
  // Additional launcher at index 2 in the unified view (primary=0,
  // empty=1 skipped from issues, MPV=2 reported).
  QVERIFY(issues.at(1).startsWith("Additional launcher 2 (MPV) ("));
}

QTEST_MAIN(TestLauncherPathIssues)
#include "test_launcherpathissues.moc"
