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
// Also covers the launch-time overload (presets + %collection% expansion)
// used by the pre-launch gate: a %collection% path must be judged after
// expansion (raw text would misread as unfindable), preset-backed
// entries must be judged against the preset's stored path (raw inline
// fields are empty, which used to skip them as "unconfigured"), and a
// dangling presetId with no usable inline fallback must be flagged naming
// the missing preset instead of skipping silently.
//
// Real PathUtils::checkLauncherPath is intentional (no mocking): the helper
// is a thin wrapper around it, and any test fixture would just be
// reimplementing the helper. We use known-bad paths (clearly nonexistent
// directories with embedded slashes so the PATH lookup isn't invoked) to
// keep the assertions deterministic across hosts.
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"

class TestLauncherPathIssues : public QObject {
  Q_OBJECT
private slots:
  void emptyProfile_returnsNoIssues();
  void emptyPathsSkipped_returnsNoIssues();
  void brokenPrimary_returnsOneIssueWithLauncherLabel();
  void brokenAdditional_unnamedUsesNumberedLabel();
  void brokenAdditional_namedAppendsNameInParens();
  void mixedSlots_returnsOneIssuePerBrokenSlot();
  // Launch-time overload (presets + %collection% expansion) — the pre-launch
  // gate variant that judges what launchItem would actually execute.
  void launchTime_collectionPlaceholderPathResolvesAfterExpansion_noIssue();
  void launchTime_presetBackedEntryWithBrokenPresetPath_flagged();
  void launchTime_presetBackedEntryWithHealthyPresetPath_noIssue();
  void launchTime_danglingPresetIdWithEmptyInline_flaggedNamingPreset();
  void launchTime_danglingPresetIdWithHealthyInlineFallback_noIssue();
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

namespace {
// Creates <root>/<collection>/runner.exe with +x and returns its absolute
// path. Used to prove a %collection%-placeholder path resolves once expanded.
// The .exe suffix is load-bearing on Windows: QFileInfo::isExecutable() there
// keys off the extension (PATHEXT), not the POSIX exe bit, so an extensionless
// file reads as non-executable and the "healthy path" cases would wrongly flag
// a NotExecutable issue. On Linux the exe bit still governs, so .exe is inert.
QString makeExecutableUnder(const QString &root, const QString &collection) {
  const QString dir = root + QLatin1Char('/') + collection;
  QDir().mkpath(dir);
  const QString exe = dir + QStringLiteral("/runner.exe");
  QFile f(exe);
  f.open(QIODevice::WriteOnly);
  f.write("#!/bin/sh\n");
  f.close();
  f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  return exe;
}
} // namespace

void TestLauncherPathIssues::launchTime_collectionPlaceholderPathResolvesAfterExpansion_noIssue() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  makeExecutableUnder(tmp.path(), QStringLiteral("Videos"));

  LauncherProfile profile;
  profile.launcherPath = tmp.path() + QStringLiteral("/%collection%/runner.exe");

  // The raw overload misreads the stored template — the literal %collection%
  // segment doesn't exist on disk — which is exactly the false positive that
  // hard-blocked launches that would have succeeded.
  QCOMPARE(LauncherUtils::launcherPathIssues(profile).size(), 1);

  // The launch-time overload expands %collection% first (same expansion
  // buildLaunchCommand applies) and finds the real executable.
  const QStringList issues =
      LauncherUtils::launcherPathIssues(profile, QList<LauncherPreset>{}, QStringLiteral("Videos"));
  QCOMPARE(issues.size(), 0);
}

void TestLauncherPathIssues::launchTime_presetBackedEntryWithBrokenPresetPath_flagged() {
  LauncherPreset preset;
  preset.id = "preset-player";
  preset.name = "Cloud Player";
  preset.launcherPath = "/nonexistent/preset/player";

  LauncherProfile profile;
  LauncherConfig al;
  al.presetId = preset.id; // Inline path left empty — the preset supplies it.
  profile.additionalLaunchers.append(al);

  // The raw overload sees only the empty inline path and skips the slot as
  // "unconfigured" — the gap that let a broken preset sail through the gate
  // and fail moments later at launch.
  QCOMPARE(LauncherUtils::launcherPathIssues(profile).size(), 0);

  const QStringList issues = LauncherUtils::launcherPathIssues(
      profile, QList<LauncherPreset>{preset}, QStringLiteral("Videos"));
  QCOMPARE(issues.size(), 1);
  // Labelled with the preset's name (what the chooser shows) and the
  // preset's stored path (what would actually be executed).
  QVERIFY(issues.first().startsWith("Additional launcher 1 (Cloud Player) ("));
  QVERIFY(issues.first().contains("/nonexistent/preset/player"));
}

void TestLauncherPathIssues::launchTime_presetBackedEntryWithHealthyPresetPath_noIssue() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString exe = makeExecutableUnder(tmp.path(), QStringLiteral("Music"));

  LauncherPreset preset;
  preset.id = "preset-ok";
  preset.name = "Local Player";
  preset.launcherPath = exe;

  LauncherProfile profile;
  LauncherConfig al;
  al.presetId = preset.id;
  profile.additionalLaunchers.append(al);

  const QStringList issues = LauncherUtils::launcherPathIssues(
      profile, QList<LauncherPreset>{preset}, QStringLiteral("Music"));
  QCOMPARE(issues.size(), 0);
}

void TestLauncherPathIssues::launchTime_danglingPresetIdWithEmptyInline_flaggedNamingPreset() {
  // The preset the slot references was deleted; the inline fields are empty
  // (the usual shape for a preset-backed entry). resolvePreset falls back to
  // the empty inline path, which used to skip silently as "unconfigured" —
  // the launch then proceeded and only failed at buildLaunchCommand with a
  // generic "No launcher configured". The gate must instead flag the slot,
  // naming the missing preset, so the pre-launch dialog catches it.
  LauncherPreset unrelated;
  unrelated.id = "preset-other";
  unrelated.name = "Other Player";
  unrelated.launcherPath = "/nonexistent/other/player";

  LauncherProfile profile;
  LauncherConfig al;
  al.name = "Cloud Player"; // stale inline copy of the deleted preset's name
  al.presetId = "preset-ghost";
  profile.additionalLaunchers.append(al);

  // The raw overload still sees only the empty inline path (unchanged).
  QCOMPARE(LauncherUtils::launcherPathIssues(profile).size(), 0);

  const QStringList issues = LauncherUtils::launcherPathIssues(
      profile, QList<LauncherPreset>{unrelated}, QStringLiteral("Videos"));
  QCOMPARE(issues.size(), 1);
  // Labelled with the slot (embedding the stale name so the user can find
  // the row) and naming the missing preset id.
  QVERIFY(issues.first().startsWith("Additional launcher 1 (Cloud Player)"));
  QVERIFY(issues.first().contains("preset-ghost"));
}

void TestLauncherPathIssues::launchTime_danglingPresetIdWithHealthyInlineFallback_noIssue() {
  // A dangling presetId whose slot still carries a usable inline path: the
  // launch falls back to those inline fields (resolvePreset's contract) and
  // succeeds, so the gate must NOT hard-block it as unresolvable.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString exe = makeExecutableUnder(tmp.path(), QStringLiteral("Music"));

  LauncherProfile profile;
  LauncherConfig al;
  al.presetId = "preset-ghost";
  al.launcherPath = exe;
  profile.additionalLaunchers.append(al);

  const QStringList issues =
      LauncherUtils::launcherPathIssues(profile, QList<LauncherPreset>{}, QStringLiteral("Music"));
  QCOMPARE(issues.size(), 0);
}

QTEST_MAIN(TestLauncherPathIssues)
#include "test_launcherpathissues.moc"
