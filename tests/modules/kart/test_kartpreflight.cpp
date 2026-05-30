#include <QSet>
#include <QString>
#include <QTemporaryFile>
#include <QtTest/QtTest>

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

QTEST_MAIN(TestKartPreflight)
#include "test_kartpreflight.moc"
