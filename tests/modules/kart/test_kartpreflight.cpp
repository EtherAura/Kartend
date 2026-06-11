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

QTEST_MAIN(TestKartPreflight)
#include "test_kartpreflight.moc"
