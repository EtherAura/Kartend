#include <QDir>
#include <QSet>
#include <QString>
#include <QTemporaryFile>
#include <QtTest/QtTest>

#include "collection/collectionconfig.h"
#include "kartmanager.h"
#include "kartpreflight.h"

using KartPreflight::LauncherCheck;

class TestKartPreflight : public QObject {
  Q_OBJECT
private slots:
  void classify_emptyPath();
  void classify_missingAbsolutePath();
  void classify_executableAbsolutePath();
  void classify_relativePath_pathResolved();
  void report_minimalManifest_isClean();
  void report_missingPrimaryLauncher_isFlagged();
  void report_nameConflict_isDetected();
  void report_itemMetadataPresence_isDetected();
  void report_artworkOverrides_areDetected();

  // collectSuspiciousKartPaths — the import path-allowlist security gate.
  void suspicious_pathsInsideAllowedRoots_notFlagged();
  void suspicious_pathsOutsideAllowedRoots_flagged();
  void suspicious_dotDotTraversalEscapingRoot_flagged();
  void suspicious_trustedLauncherPathExempted();
  void suspicious_trustExemptsOnlyLauncherFields();
  void suspicious_additionalLaunchersAndEmptyPathsHandled();

  // collectInTreeLauncherPaths (Kartend-u8wf0) — the headless-import gate that
  // catches a .kart bundling its own launcher executable.
  void inTree_launcherInsideExtractedRoot_flagged();
  void inTree_launcherOutsideExtractedRoot_notFlagged();
  void inTree_additionalLauncherInside_flagged();
  void inTree_dotDotEscapingRoot_notFlagged();
  void inTree_emptyRootOrPath_notFlagged();
  void inTree_bundledCoreAndParameterToken_flagged();

  // collectLauncherTrustFindings (Kartend-kxqqf) — the import gate proper. An
  // imported launcher block picks the program AND its argv, so every field is
  // disclosed and the allowlist only decides how loud each row is.
  void trust_noLauncherBlock_producesNothing();
  void trust_allowlistedLauncherIsStillDisclosed();
  void trust_launchParametersAreInspected();
  void trust_corePathIsInspected();
  void trust_interpreterProgramIsElevatedInsideAllowlist();
  void trust_inlineCodeParametersAreElevated();
  void trust_benignParametersAreNotElevated();
  void trust_bundledPayloadIsElevated();
  void trust_additionalLauncherFieldsAreInspected();
  void trust_trustedPathDemotesAllowlistOnly();
  void trust_confirmationRowsCarryValueAndReason();

  // Kartend-7f43t — Windows-native program roots. Classification is by path
  // shape, so it holds on either host: a Windows-authored bundle reads the
  // same way whether it is imported on Windows or inspected on Linux.
  void trust_windowsProgramFilesLauncherIsQuiet();
  void trust_windowsProgramRootLookalikesStayLoud();

  // The preflight report must never come back clean for a bundle whose
  // launcher configuration it merely listed.
  void report_launchParametersAlone_defeatTheAllClear();
  void report_allowlistedInterpreter_isElevated();
};

void TestKartPreflight::classify_emptyPath() {
  QCOMPARE(KartPreflight::classifyLauncherPath(""), LauncherCheck::EmptyPath);
  QCOMPARE(KartPreflight::classifyLauncherPath("   "), LauncherCheck::EmptyPath);
}

void TestKartPreflight::classify_missingAbsolutePath() {
  // A drive-rooted path is absolute on Windows; a "/"-leading one is NOT (Qt
  // treats it as relative without a drive spec), so it would slip into the
  // PATH-resolution branch instead of exercising the absolute→Missing path we
  // mean to cover here. Pick a platform-absolute, guaranteed-missing target.
#ifdef Q_OS_WIN
  const QString missing = QStringLiteral("C:/zzz/__definitely_not_here__/vlc.exe");
#else
  const QString missing = QStringLiteral("/zzz/__definitely_not_here__/vlc");
#endif
  QVERIFY(QFileInfo(missing).isAbsolute()); // premise: really absolute on this OS
  QCOMPARE(KartPreflight::classifyLauncherPath(missing), LauncherCheck::Missing);
}

void TestKartPreflight::classify_executableAbsolutePath() {
  // A reliably-present, executable, absolute binary per platform. cmd.exe is
  // always under System32 on Windows; /bin/sh on every POSIX. Skip if absent
  // rather than assert against a missing host tool.
#ifdef Q_OS_WIN
  const QString shell = QStringLiteral("C:/Windows/System32/cmd.exe");
#else
  const QString shell = QStringLiteral("/bin/sh");
#endif
  if (!QFileInfo(shell).exists()) QSKIP("platform reference executable not present in test env");
  QCOMPARE(KartPreflight::classifyLauncherPath(shell), LauncherCheck::Ok);
}

void TestKartPreflight::classify_relativePath_pathResolved() {
  // A bare command name that PATH-resolution (QStandardPaths::findExecutable)
  // will find: "cmd" on Windows (via PATHEXT), "sh" on POSIX. If it isn't on
  // PATH we can't exercise PATH-resolution and just skip.
#ifdef Q_OS_WIN
  const QString name = QStringLiteral("cmd");
#else
  const QString name = QStringLiteral("sh");
#endif
  if (QStandardPaths::findExecutable(name).isEmpty())
    QSKIP("reference command not found via PATH in test env");
  QCOMPARE(KartPreflight::classifyLauncherPath(name), LauncherCheck::Ok);
}

namespace {

KartManifest::Manifest makeManifest(const QString &name, const QString &launcherPath) {
  KartManifest::Manifest m;
  m.name = name;
  m.collectionConfig.name = name;
  m.collectionConfig.type = "video";
  m.collectionConfig.launcher.launcherPath = launcherPath;
  return m;
}

} // namespace

void TestKartPreflight::report_minimalManifest_isClean() {
  // Empty launcherPath is treated as "not yet configured" — not an issue.
  // No items either, so the suspicious-path / metadata branches stay quiet.
  KartManifest::Manifest m = makeManifest("Demos", "");
  const auto r = KartPreflight::buildReport(m, {}, {});
  QCOMPARE(r.collectionName, QStringLiteral("Demos"));
  QCOMPARE(r.itemCount, 0);
  QVERIFY(r.launcherIssues.isEmpty());
  QVERIFY(r.suspiciousPaths.isEmpty());
  QVERIFY(!r.nameConflicts);
  QVERIFY(!r.hasIssues());
}

void TestKartPreflight::report_missingPrimaryLauncher_isFlagged() {
  KartManifest::Manifest m = makeManifest("Demos", "/totally/not/a/real/binary/__nope__");
  const auto r = KartPreflight::buildReport(m, {}, {});
  QCOMPARE(r.launcherIssues.size(), 1);
  QCOMPARE(r.launcherIssues.first().reason, LauncherCheck::Missing);
  QVERIFY(r.hasIssues());
}

void TestKartPreflight::report_nameConflict_isDetected() {
  KartManifest::Manifest m = makeManifest("Demos", "");
  QSet<QString> existing;
  existing.insert("demos");
  const auto r = KartPreflight::buildReport(m, {}, existing);
  QVERIFY(r.nameConflicts);
}

void TestKartPreflight::report_itemMetadataPresence_isDetected() {
  KartManifest::Manifest m = makeManifest("Demos", "");
  KartManifest::Item it;
  it.mediaPath = "x.mkv";
  it.metadata.developer = "Studio";
  m.items.append(it);
  const auto r = KartPreflight::buildReport(m, {}, {});
  QVERIFY(r.hasMetadata);
  QCOMPARE(r.itemCount, 1);
}

void TestKartPreflight::report_artworkOverrides_areDetected() {
  KartManifest::Manifest m = makeManifest("Demos", "");
  KartManifest::Item it;
  it.mediaPath = "x.mkv";
  it.artworkPath = "art/x.png";
  m.items.append(it);
  const auto r = KartPreflight::buildReport(m, {}, {});
  QVERIFY(r.hasArtworkOverrides);
}

namespace {
CollectionConfig configWithLauncher(const QString &launcherPath) {
  CollectionConfig cfg;
  cfg.launcher.launcherPath = launcherPath;
  return cfg;
}
} // namespace

void TestKartPreflight::suspicious_pathsInsideAllowedRoots_notFlagged() {
  // The allowlist is {home, /usr/bin, /usr/local/bin, /opt}. The home root is
  // the only one that resolves the same on both platforms: on Windows a
  // "/usr/bin/..." string has no drive spec, so QFileInfo::absoluteFilePath
  // prefixes the current drive ("C:/.../usr/bin/...") and it no longer matches
  // the literal "/usr/bin" root — it would be wrongly flagged. So derive the
  // cross-platform "inside" fixtures from home (always absolute, always its own
  // allowed root) and only check the POSIX-absolute roots on POSIX.
  const QString home = QDir::homePath();
  QStringList allowed = {
      QDir::cleanPath(home + QStringLiteral("/games/launch.sh")),
      QDir::cleanPath(home + QStringLiteral("/.local/bin/run")),
  };
#ifndef Q_OS_WIN
  allowed << QStringLiteral("/usr/bin/vlc") << QStringLiteral("/usr/local/bin/retroarch")
          << QStringLiteral("/opt/app/run");
#endif
  for (const QString &p : allowed) {
    QVERIFY2(
        kart::collectSuspiciousKartPaths(configWithLauncher(p), {}).isEmpty(),
        qPrintable(QStringLiteral("path inside an allowed root must not be flagged: %1").arg(p)));
  }
}

void TestKartPreflight::suspicious_pathsOutsideAllowedRoots_flagged() {
  // /bin is intentionally NOT on the allowlist (home, /usr/bin, /usr/local/bin,
  // /opt) — a .kart pointing a launcher at /bin/sh or /etc must prompt.
  const QStringList outside = {QStringLiteral("/etc/cron.d/evil"), QStringLiteral("/tmp/evil.sh"),
                               QStringLiteral("/bin/sh"), QStringLiteral("/var/lib/x")};
  for (const QString &p : outside) {
    const auto out = kart::collectSuspiciousKartPaths(configWithLauncher(p), {});
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().first, QStringLiteral("launcher.launcherPath"));
    QCOMPARE(out.first().second, p);
  }
}

void TestKartPreflight::suspicious_dotDotTraversalEscapingRoot_flagged() {
  // Security regression guard: a manifest path that prefixes an allowed root
  // then climbs out with .. must still be flagged — otherwise a malicious .kart
  // could auto-run a sibling of the allowed tree (e.g. /etc/...) by writing
  // "<allowed-root>/../../../etc/evil". We escape the home root specifically
  // because it's the one allowed root that exists+absolutizes identically on
  // both Linux and Windows; the production isPathAllowed absolutizes (collapsing
  // "..") before the prefix compare, so the escaped path must NOT match home.
  const QString home = QDir::homePath();
  const QString escape = home + QStringLiteral("/../../../../etc/evil");
  // Premise: after .. collapse this really is outside home (otherwise the test
  // would be vacuous).
  QVERIFY(!QFileInfo(escape).absoluteFilePath().startsWith(home + QLatin1Char('/')));
  QVERIFY2(!kart::collectSuspiciousKartPaths(configWithLauncher(escape), {}).isEmpty(),
           "a .. traversal escaping an allowed root must be flagged as suspicious");
}

void TestKartPreflight::suspicious_trustedLauncherPathExempted() {
  const QString custom = QStringLiteral("/srv/launchers/custom"); // outside the roots
  QCOMPARE(kart::collectSuspiciousKartPaths(configWithLauncher(custom), {}).size(), 1);
  // Already present on a trusted collection -> exempt (no re-prompt on re-import).
  QVERIFY(kart::collectSuspiciousKartPaths(configWithLauncher(custom), {custom}).isEmpty());
}

void TestKartPreflight::suspicious_trustExemptsOnlyLauncherFields() {
  // The trust set exempts launcher fields only; an icon/placeholder outside the
  // roots stays flagged even when the same string is "trusted".
  const QString custom = QStringLiteral("/srv/assets/icon.png");
  CollectionConfig cfg;
  cfg.collectionIcon = custom;
  const auto out = kart::collectSuspiciousKartPaths(cfg, {custom});
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().first, QStringLiteral("collectionIcon"));
}

void TestKartPreflight::suspicious_additionalLaunchersAndEmptyPathsHandled() {
  CollectionConfig cfg; // empty primary launcher -> skipped
  LauncherConfig alt;
  alt.launcherPath = QStringLiteral("/etc/evil");
  cfg.launcher.additionalLaunchers.append(alt);
  // An allowed placeholder must NOT add to the flagged count. Use a home-rooted
  // path: "/opt/..." only counts as inside an allowed root on POSIX (on Windows
  // it absolutizes to C:/opt/... and would be flagged too, inflating out.size).
  cfg.placeholderArtwork =
      QDir::cleanPath(QDir::homePath() + QStringLiteral("/art/placeholder.png"));
  const auto out = kart::collectSuspiciousKartPaths(cfg, {});
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().first, QStringLiteral("additionalLaunchers[0].launcherPath"));
  QCOMPARE(out.first().second, QStringLiteral("/etc/evil"));
}

void TestKartPreflight::inTree_launcherInsideExtractedRoot_flagged() {
  // A manifest launcherPath that resolves inside the extracted kart tree is the
  // dangerous self-bundled-executable case. collectSuspiciousKartPaths can't
  // catch it (the root usually sits under an allowlisted prefix), so the
  // headless-import gate relies on this. The bundled file need not exist yet —
  // detection happens before launch — so derive the path from the canonical
  // root to keep the prefix compare exact across symlinked temp roots.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString canonRoot = QFileInfo(root.path()).canonicalFilePath();
  const QString inside = canonRoot + QStringLiteral("/payload/runner.sh");
  const auto out = kart::collectInTreeLauncherPaths(configWithLauncher(inside), root.path());
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().first, QStringLiteral("launcher.launcherPath"));
  QCOMPARE(out.first().second, inside);
}

void TestKartPreflight::inTree_launcherOutsideExtractedRoot_notFlagged() {
  // A launcher elsewhere on the host (the normal case — an installed emulator)
  // is none of this gate's business; only in-tree paths are blocked.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString outside =
      QDir::cleanPath(QDir::homePath() + QStringLiteral("/.local/bin/installed-runner"));
  QVERIFY(kart::collectInTreeLauncherPaths(configWithLauncher(outside), root.path()).isEmpty());
}

void TestKartPreflight::inTree_additionalLauncherInside_flagged() {
  // The check covers every launcher field, not just the primary, so a kart
  // can't smuggle its bundled executable in via an additional launcher.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString canonRoot = QFileInfo(root.path()).canonicalFilePath();
  CollectionConfig cfg; // empty primary -> skipped
  LauncherConfig alt;
  alt.launcherPath = canonRoot + QStringLiteral("/bin/alt-runner");
  cfg.launcher.additionalLaunchers.append(alt);
  const auto out = kart::collectInTreeLauncherPaths(cfg, root.path());
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().first, QStringLiteral("additionalLaunchers[0].launcherPath"));
}

void TestKartPreflight::inTree_dotDotEscapingRoot_notFlagged() {
  // A path that prefixes the root then climbs out with .. resolves OUTSIDE the
  // tree, so it's not an in-tree launcher — the cleanPath/canonicalize step
  // must collapse the traversal before the prefix compare (mirror of the
  // suspicious-path traversal guard).
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString canonRoot = QFileInfo(root.path()).canonicalFilePath();
  const QString escape = canonRoot + QStringLiteral("/../escapee/runner");
  // Premise: after .. collapse this really is outside the root.
  QVERIFY(!QDir::cleanPath(escape).startsWith(canonRoot + QLatin1Char('/')));
  QVERIFY(kart::collectInTreeLauncherPaths(configWithLauncher(escape), root.path()).isEmpty());
}

void TestKartPreflight::inTree_emptyRootOrPath_notFlagged() {
  // Defensive guards: an empty extraction root (shouldn't happen post-extract)
  // or an empty launcher path must never report a false positive.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  QVERIFY(
      kart::collectInTreeLauncherPaths(configWithLauncher(QStringLiteral("/anything")), QString())
          .isEmpty());
  QVERIFY(kart::collectInTreeLauncherPaths(configWithLauncher(QString()), root.path()).isEmpty());
}

void TestKartPreflight::inTree_bundledCoreAndParameterToken_flagged() {
  // The launcher binary is not the only way a bundle gets its own code run:
  // a core is loaded into the launcher's process (read permission is enough,
  // so the non-executable extraction does not stand in its way), and a path
  // argument reaches a bundled payload without touching corePath at all.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString canonRoot = QFileInfo(root.path()).canonicalFilePath();

  CollectionConfig core; // launcher elsewhere, core shipped by the bundle
  core.launcher.launcherPath = QStringLiteral("/usr/bin/some-frontend");
  core.launcher.corePath = canonRoot + QStringLiteral("/payload/core.so");
  const auto coreOut = kart::collectInTreeLauncherPaths(core, root.path());
  QCOMPARE(coreOut.size(), 1);
  QCOMPARE(coreOut.first().first, QStringLiteral("launcher.corePath"));

  CollectionConfig viaArgs;
  viaArgs.launcher.launcherPath = QStringLiteral("/usr/bin/some-frontend");
  viaArgs.launcher.launchParameters =
      QStringLiteral("-L %1/payload/core.so %media%").arg(canonRoot);
  const auto argOut = kart::collectInTreeLauncherPaths(viaArgs, root.path());
  QCOMPARE(argOut.size(), 1);
  QCOMPARE(argOut.first().first, QStringLiteral("launcher.launchParameters"));

  // A parameter template naming nothing in the tree stays clean.
  CollectionConfig benign;
  benign.launcher.launchParameters = QStringLiteral("--fullscreen %media%");
  QVERIFY(kart::collectInTreeLauncherPaths(benign, root.path()).isEmpty());
}

namespace {

using kart::KartLauncherFinding;
using kart::LauncherTrustReason;

/// The single finding for @p field, or a default-constructed one when the
/// field was not reported (so a miss fails on the reason compare instead of
/// crashing the run).
KartLauncherFinding findingFor(const QList<KartLauncherFinding> &findings, const QString &field) {
  for (const KartLauncherFinding &f : findings) {
    if (f.field == field) return f;
  }
  return KartLauncherFinding{};
}

/// Pins the working directory to the filesystem root for the duration of a
/// test, and puts it back afterwards even if an assertion returns early.
///
/// The Windows-root cases below are RELATIVE paths on POSIX ("C:/…" has no
/// leading slash), so QFileInfo absolutises them against the cwd — and the cwd
/// during a test run is the build tree, which normally sits under $HOME, the
/// one allowlisted root on every platform. Every such path would then read
/// quiet for a reason that has nothing to do with the rule under test, and the
/// negative cases would pass vacuously. The root is under no allowed prefix on
/// either host, so it makes the shape rule the only thing that can quiet a row.
class ScopedRootWorkingDir {
public:
  ScopedRootWorkingDir() : m_previous(QDir::currentPath()) { QDir::setCurrent(QDir::rootPath()); }
  ~ScopedRootWorkingDir() { QDir::setCurrent(m_previous); }
  ScopedRootWorkingDir(const ScopedRootWorkingDir &) = delete;
  ScopedRootWorkingDir &operator=(const ScopedRootWorkingDir &) = delete;

private:
  QString m_previous;
};

} // namespace

void TestKartPreflight::trust_noLauncherBlock_producesNothing() {
  // A bundle that asks for nothing executable is the one case that imports
  // with no prompt — otherwise the warning becomes noise and gets clicked
  // through. Whitespace-only fields count as absent.
  CollectionConfig cfg;
  QVERIFY(kart::collectLauncherTrustFindings(cfg, {}, QString()).isEmpty());
  cfg.launcher.launcherPath = QStringLiteral("   ");
  cfg.launcher.corePath = QStringLiteral("  ");
  cfg.launcher.launchParameters = QStringLiteral("\t");
  QVERIFY(kart::collectLauncherTrustFindings(cfg, {}, QString()).isEmpty());
}

void TestKartPreflight::trust_allowlistedLauncherIsStillDisclosed() {
  // The regression this whole gate exists for: living in an allowlisted
  // directory used to mean saying nothing at all. It now means the row is
  // quiet, not absent — the user still sees what the bundle will run.
  // Home-rooted like suspicious_pathsInsideAllowedRoots_notFlagged: home is
  // the one allowlisted root on every platform. "/usr/bin/mpv" reads as
  // OutsideAllowlist on Windows (drive-less paths absolutize to
  // "C:/usr/bin/…" and never prefix-match the POSIX roots), which failed
  // this suite's first Windows run.
  const QString allowlisted = QDir::cleanPath(QDir::homePath() + QStringLiteral("/apps/mpv"));
  const auto out =
      kart::collectLauncherTrustFindings(configWithLauncher(allowlisted), {}, QString());
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().field, QStringLiteral("launcher.launcherPath"));
  QCOMPARE(out.first().value, allowlisted);
  QCOMPARE(out.first().reason, LauncherTrustReason::BundleSupplied);
  QVERIFY(!kart::isElevatedLauncherTrustReason(out.first().reason));
}

void TestKartPreflight::trust_launchParametersAreInspected() {
  // launchParameters is the argv template — the field that decides what an
  // allowlisted program actually does. It had no coverage at all before.
  CollectionConfig cfg;
  cfg.launcher.launchParameters = QStringLiteral("--fullscreen %media%");
  const auto out = kart::collectLauncherTrustFindings(cfg, {}, QString());
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().field, QStringLiteral("launcher.launchParameters"));
  QCOMPARE(out.first().value, QStringLiteral("--fullscreen %media%"));
}

void TestKartPreflight::trust_corePathIsInspected() {
  // A core is code the launcher loads into itself. No allowlist applies —
  // cores legitimately live in library directories the launcher allowlist
  // never covered — but the value is always shown.
  CollectionConfig cfg;
  cfg.launcher.corePath = QStringLiteral("/usr/lib/libretro/example_libretro.so");
  const auto out = kart::collectLauncherTrustFindings(cfg, {}, QString());
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().field, QStringLiteral("launcher.corePath"));
  QCOMPARE(out.first().reason, LauncherTrustReason::BundleSupplied);
}

void TestKartPreflight::trust_interpreterProgramIsElevatedInsideAllowlist() {
  // /bin/sh was caught only because /bin is off the allowlist; /usr/bin/sh is
  // the same binary on a merged-usr system and used to sail through. A
  // program that executes its arguments is elevated wherever it lives.
  const QStringList interpreters = {
      QStringLiteral("/usr/bin/sh"),
      QStringLiteral("/usr/bin/bash"),
      QStringLiteral("/usr/bin/python3"),
      QStringLiteral("/usr/bin/python3.13"),
      QStringLiteral("/usr/bin/env"),
      QStringLiteral("/usr/local/bin/perl"),
      QStringLiteral("/opt/tools/node"),
      QStringLiteral("/usr/bin/pkexec"),
      QStringLiteral("/usr/bin/powershell.exe"),
      QDir::cleanPath(QDir::homePath() + QStringLiteral("/.local/bin/ruby"))};
  for (const QString &p : interpreters) {
    const auto out = kart::collectLauncherTrustFindings(configWithLauncher(p), {}, QString());
    QCOMPARE(out.size(), 1);
    QVERIFY2(out.first().reason == LauncherTrustReason::InterpreterProgram,
             qPrintable(QStringLiteral("interpreter must be elevated regardless of "
                                       "directory: %1")
                            .arg(p)));
  }
  // A launcher that merely lives beside them is not an interpreter.
  // Home-rooted so the fixture is allowlisted on Windows too (see
  // trust_allowlistedLauncherIsStillDisclosed).
  const auto benign = kart::collectLauncherTrustFindings(
      configWithLauncher(QDir::cleanPath(QDir::homePath() + QStringLiteral("/apps/mpv"))), {},
      QString());
  QCOMPARE(benign.first().reason, LauncherTrustReason::BundleSupplied);
  // ...nor is a wrapper script whose name merely ends in a shell's name.
  const auto wrapper = kart::collectLauncherTrustFindings(
      configWithLauncher(QDir::cleanPath(QDir::homePath() + QStringLiteral("/bin/launch.sh"))), {},
      QString());
  QCOMPARE(wrapper.first().reason, LauncherTrustReason::BundleSupplied);
}

void TestKartPreflight::trust_inlineCodeParametersAreElevated() {
  // The flags whose documented job is to run an inline string. This is a
  // volume control, not the guarantee — anything it misses is still
  // disclosed by the row itself.
  const QStringList hostile = {
      QStringLiteral("-c \"import os; os.system('x')\""), QStringLiteral("--eval 'payload()'"),
      QStringLiteral("--command=sh org.example.App"), QStringLiteral("/c payload.bat"),
      QStringLiteral("-EncodedCommand ABCD")};
  for (const QString &params : hostile) {
    CollectionConfig cfg;
    cfg.launcher.launcherPath = QStringLiteral("/usr/bin/mpv");
    cfg.launcher.launchParameters = params;
    const auto out = kart::collectLauncherTrustFindings(cfg, {}, QString());
    const auto finding = findingFor(out, QStringLiteral("launcher.launchParameters"));
    QVERIFY2(finding.reason == LauncherTrustReason::InlineCodeParameter,
             qPrintable(QStringLiteral("inline-code parameters must be elevated: %1").arg(params)));
  }
}

void TestKartPreflight::trust_benignParametersAreNotElevated() {
  // The ordinary case must stay quiet, or the loud case stops meaning
  // anything.
  const QStringList benign = {QStringLiteral("--fullscreen %media%"),
                              QStringLiteral("-L /usr/lib/libretro/example_libretro.so \"%1\""),
                              QStringLiteral("--title %name% --sub-file %dir%/subs.srt")};
  for (const QString &params : benign) {
    CollectionConfig cfg;
    cfg.launcher.launchParameters = params;
    const auto out = kart::collectLauncherTrustFindings(cfg, {}, QString());
    QCOMPARE(out.size(), 1);
    QVERIFY2(
        !kart::isElevatedLauncherTrustReason(out.first().reason),
        qPrintable(QStringLiteral("ordinary parameters must not be elevated: %1").arg(params)));
  }
}

void TestKartPreflight::trust_bundledPayloadIsElevated() {
  // Post-extraction the strongest signal is available: the bundle shipped the
  // thing it wants run. Reached through launcherPath, through corePath, or
  // through an argument.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString canonRoot = QFileInfo(root.path()).canonicalFilePath();

  const auto viaLauncher = kart::collectLauncherTrustFindings(
      configWithLauncher(canonRoot + QStringLiteral("/bin/runner")), {}, root.path());
  QCOMPARE(findingFor(viaLauncher, QStringLiteral("launcher.launcherPath")).reason,
           LauncherTrustReason::BundledExecutable);

  CollectionConfig cfg;
  cfg.launcher.launcherPath = QStringLiteral("/usr/bin/some-frontend");
  cfg.launcher.corePath = canonRoot + QStringLiteral("/core.so");
  cfg.launcher.launchParameters = QStringLiteral("--plugin %1/plug.so").arg(canonRoot);
  const auto out = kart::collectLauncherTrustFindings(cfg, {}, root.path());
  QCOMPARE(findingFor(out, QStringLiteral("launcher.corePath")).reason,
           LauncherTrustReason::BundledExecutable);
  QCOMPARE(findingFor(out, QStringLiteral("launcher.launchParameters")).reason,
           LauncherTrustReason::BundledExecutable);

  // Pre-extraction (empty root) the same config reports without that
  // escalation — and, crucially, still reports.
  const auto preExtract = kart::collectLauncherTrustFindings(cfg, {}, QString());
  QCOMPARE(preExtract.size(), 3);
  QCOMPARE(findingFor(preExtract, QStringLiteral("launcher.corePath")).reason,
           LauncherTrustReason::BundleSupplied);
}

void TestKartPreflight::trust_additionalLauncherFieldsAreInspected() {
  // Every launcher slot is covered, so a bundle can't hide the dangerous one
  // behind a harmless primary.
  CollectionConfig cfg;
  cfg.launcher.launcherPath = QStringLiteral("/usr/bin/mpv");
  LauncherConfig alt;
  alt.launcherPath = QStringLiteral("/usr/bin/python3");
  alt.corePath = QStringLiteral("/srv/cores/x.so");
  alt.launchParameters = QStringLiteral("-c payload");
  cfg.launcher.additionalLaunchers.append(alt);
  const auto out = kart::collectLauncherTrustFindings(cfg, {}, QString());
  QCOMPARE(out.size(), 4);
  QCOMPARE(findingFor(out, QStringLiteral("additionalLaunchers[0].launcherPath")).reason,
           LauncherTrustReason::InterpreterProgram);
  QCOMPARE(findingFor(out, QStringLiteral("additionalLaunchers[0].corePath")).value,
           QStringLiteral("/srv/cores/x.so"));
  QCOMPARE(findingFor(out, QStringLiteral("additionalLaunchers[0].launchParameters")).reason,
           LauncherTrustReason::InlineCodeParameter);
}

void TestKartPreflight::trust_trustedPathDemotesAllowlistOnly() {
  // A launcher the user already runs elsewhere loses the allowlist warning —
  // re-prompting about it every re-import is the noise this exemption exists
  // to avoid. It does NOT lose the interpreter warning: the arguments beside
  // an interpreter arrive fresh with every bundle.
  const QString field = QStringLiteral("launcher.launcherPath");
  const QString custom = QStringLiteral("/srv/launchers/custom");
  QCOMPARE(findingFor(kart::collectLauncherTrustFindings(configWithLauncher(custom), {}, QString()),
                      field)
               .reason,
           LauncherTrustReason::OutsideAllowlist);
  const auto trusted =
      kart::collectLauncherTrustFindings(configWithLauncher(custom), {custom}, QString());
  QCOMPARE(trusted.size(), 1); // still disclosed
  QCOMPARE(findingFor(trusted, field).reason, LauncherTrustReason::BundleSupplied);

  const QString interpreter = QStringLiteral("/srv/launchers/bash");
  const auto trustedInterpreter =
      kart::collectLauncherTrustFindings(configWithLauncher(interpreter), {interpreter}, QString());
  QCOMPARE(findingFor(trustedInterpreter, field).reason, LauncherTrustReason::InterpreterProgram);
}

void TestKartPreflight::trust_confirmationRowsCarryValueAndReason() {
  // The prompt rows must show the value verbatim — "what will run" is the
  // only question that matters — and say why each row is listed.
  CollectionConfig cfg;
  cfg.launcher.launcherPath = QStringLiteral("/usr/bin/python3");
  cfg.launcher.launchParameters = QStringLiteral("-c payload");
  cfg.collectionIcon = QStringLiteral("/etc/icon.png");
  const auto rows =
      kart::importConfirmationRows(kart::collectLauncherTrustFindings(cfg, {}, QString()),
                                   kart::collectSuspiciousAssetPaths(cfg));
  QCOMPARE(rows.size(), 3);
  QVERIFY(rows.at(0).first.contains(QStringLiteral("launcher.launcherPath")));
  QVERIFY(rows.at(0).first.contains(
      kart::launcherTrustReasonLabel(LauncherTrustReason::InterpreterProgram)));
  QCOMPARE(rows.at(0).second, QStringLiteral("/usr/bin/python3"));
  QCOMPARE(rows.at(2).second, QStringLiteral("/etc/icon.png"));
}

void TestKartPreflight::trust_windowsProgramFilesLauncherIsQuiet() {
  // Kartend-7f43t: before this, EVERY Windows launcher outside %USERPROFILE%
  // read OutsideAllowlist, because the roots were POSIX-only. "C:/Program
  // Files/mpv/mpv.exe" is the exact Windows analogue of "/usr/bin/mpv" — an
  // admin-writable-only install root — so it earns the same quiet row.
  //
  // Quiet, not absent: the finding is still emitted, it just stops claiming a
  // danger signal matched.
  const ScopedRootWorkingDir atRoot;
  const QString field = QStringLiteral("launcher.launcherPath");
  const QStringList quiet = {
      QStringLiteral("C:/Program Files/mpv/mpv.exe"),
      QStringLiteral("C:/Program Files (x86)/mpv/mpv.exe"),
      QStringLiteral("c:/program files/mpv/mpv.exe"),        // Windows paths are case-insensitive
      QStringLiteral("D:\\Program Files\\mpv\\mpv.exe"),     // other drive, native separators
      QStringLiteral("C:/Program Files/mpv/../mpv/mpv.exe"), // .. that stays inside
  };
  for (const QString &p : quiet) {
    const auto out = kart::collectLauncherTrustFindings(configWithLauncher(p), {}, QString());
    QCOMPARE(out.size(), 1);        // disclosed
    QCOMPARE(out.first().value, p); // verbatim, as the UI shows it
    QVERIFY2(
        !kart::isElevatedLauncherTrustReason(findingFor(out, field).reason),
        qPrintable(QStringLiteral("a Windows program-root launcher must read quiet: %1").arg(p)));
  }
}

void TestKartPreflight::trust_windowsProgramRootLookalikesStayLoud() {
  // The shape test matches a raw manifest string, so it does not get the ".."
  // collapse that absolutisation hands the POSIX prefix compare for free —
  // this is the Windows half of suspicious_dotDotTraversalEscapingRoot_flagged.
  // A path that prefixes Program Files and then climbs out of it must not buy
  // silence for System32, and neither must a near-miss spelling.
  const ScopedRootWorkingDir atRoot;
  const QString field = QStringLiteral("launcher.launcherPath");
  const QStringList loud = {
      QStringLiteral("C:/Program Files/../Windows/System32/evil.exe"),      // escapes the root
      QStringLiteral("C:\\Program Files\\..\\Windows\\System32\\evil.exe"), // same, native seps
      QStringLiteral("C:/ProgramFiles/evil.exe"),         // no space — a different directory
      QStringLiteral("C:/Program Files Custom/evil.exe"), // prefix, not a path segment
      QStringLiteral("C:/Program Files"),                 // the root itself is not a program
  };
  for (const QString &p : loud) {
    const auto out = kart::collectLauncherTrustFindings(configWithLauncher(p), {}, QString());
    QCOMPARE(out.size(), 1);
    QCOMPARE(findingFor(out, field).reason, LauncherTrustReason::OutsideAllowlist);
  }
}

void TestKartPreflight::report_launchParametersAlone_defeatTheAllClear() {
  // The exact false-green: a manifest whose only launcher field is the argv
  // template. No launcher path to fail resolution, no allowlist violation,
  // no name conflict — and the dialog used to answer "no validation issues".
  KartManifest::Manifest m = makeManifest(QStringLiteral("Demos"), QString());
  m.collectionConfig.launcher.launchParameters = QStringLiteral("-c payload");
  const auto r = KartPreflight::buildReport(m, {}, {});
  QVERIFY(r.launcherIssues.isEmpty());
  QVERIFY(r.suspiciousPaths.isEmpty());
  QVERIFY(!r.nameConflicts);
  QCOMPARE(r.launcherTrust.size(), 1);
  QVERIFY2(r.hasIssues(), "a bundle carrying launch parameters must never report as clean");
  QVERIFY(r.hasElevatedLauncherTrust());
}

void TestKartPreflight::report_allowlistedInterpreter_isElevated() {
  // The reported chain: an allowlisted interpreter plus a bundle-chosen argv.
  // The report must carry it as elevated, not merely present.
  KartManifest::Manifest m = makeManifest(QStringLiteral("Demos"), QStringLiteral("/usr/bin/sh"));
  m.collectionConfig.launcher.launchParameters = QStringLiteral("-c payload");
  const auto r = KartPreflight::buildReport(m, {}, {});
  QVERIFY(r.suspiciousPaths.isEmpty()); // the allowlist still likes the path
  QVERIFY(r.hasElevatedLauncherTrust());
  QVERIFY(r.hasIssues());
}

QTEST_MAIN(TestKartPreflight)
#include "test_kartpreflight.moc"
