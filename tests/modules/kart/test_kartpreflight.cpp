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
};

void TestKartPreflight::classify_emptyPath() {
  QCOMPARE(KartPreflight::classifyLauncherPath(""), LauncherCheck::EmptyPath);
  QCOMPARE(KartPreflight::classifyLauncherPath("   "), LauncherCheck::EmptyPath);
}

void TestKartPreflight::classify_missingAbsolutePath() {
  QCOMPARE(KartPreflight::classifyLauncherPath("/zzz/__definitely_not_here__/vlc"),
           LauncherCheck::Missing);
}

void TestKartPreflight::classify_executableAbsolutePath() {
  // /bin/sh is reliably present on every POSIX. Skip otherwise.
  const QString shell = "/bin/sh";
  if (!QFileInfo(shell).exists()) QSKIP("/bin/sh not present in test env");
  QCOMPARE(KartPreflight::classifyLauncherPath(shell), LauncherCheck::Ok);
}

void TestKartPreflight::classify_relativePath_pathResolved() {
  // "sh" is almost certainly on PATH; if it isn't we can't really test
  // PATH-resolution and just skip.
  if (QStandardPaths::findExecutable("sh").isEmpty()) QSKIP("sh not found via PATH in test env");
  QCOMPARE(KartPreflight::classifyLauncherPath("sh"), LauncherCheck::Ok);
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
  const QString home = QDir::homePath();
  const QStringList allowed = {
      QStringLiteral("/usr/bin/vlc"), QStringLiteral("/usr/local/bin/retroarch"),
      QStringLiteral("/opt/app/run"), home + QStringLiteral("/games/launch.sh")};
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
  // could auto-run /etc/... by writing "/usr/bin/../../../etc/evil".
  const QString escape = QStringLiteral("/usr/bin/../../../etc/evil");
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
  cfg.placeholderArtwork = QStringLiteral("/opt/ok/placeholder.png"); // allowed -> not flagged
  const auto out = kart::collectSuspiciousKartPaths(cfg, {});
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().first, QStringLiteral("additionalLaunchers[0].launcherPath"));
  QCOMPARE(out.first().second, QStringLiteral("/etc/evil"));
}

QTEST_MAIN(TestKartPreflight)
#include "test_kartpreflight.moc"
