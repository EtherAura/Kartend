// Tests for CollectionHealth::analyze. Filesystem existence is checked
// against files in a QTemporaryDir so the test stays self-contained.
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "collectionhealth.h"

using CollectionHealth::ItemPath;
using CollectionHealth::Report;

class TestCollectionHealth : public QObject {
  Q_OBJECT
private slots:
  void noItems_reportIsZeroed();
  void missingFile_countAndSamplesAreCapped();
  void emptyArtworkPath_countedAsMissing();
  void launcher_emptyPath_flaggedAsUnconfigured();
  void launcher_validatorFalse_flagsExecutableNotFound();
  void launcher_validatorTrue_noIssue();

private:
  /// Touches a file at `path` so QFileInfo::exists returns true.
  static void touch(const QString &path) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("ok");
    f.close();
  }
};

void TestCollectionHealth::noItems_reportIsZeroed() {
  CollectionConfig cfg;
  cfg.name = "Empty";
  // Mark the primary launcher as configured so we isolate this test to
  // the item-side counters. (LauncherProfile always carries a primary
  // slot; an unconfigured one is exercised separately below.)
  cfg.launcher.launcherPath = "/usr/bin/echo";
  const Report r = CollectionHealth::analyze(cfg, {}, [](const QString &) { return true; });
  QCOMPARE(r.totalItems, 0);
  QCOMPARE(r.missingFileCount, 0);
  QCOMPARE(r.missingArtworkCount, 0);
  QVERIFY(r.launcherIssues.isEmpty());
}

void TestCollectionHealth::missingFile_countAndSamplesAreCapped() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Real file the analyzer should mark as present.
  const QString present = dir.filePath("present.mp4");
  touch(present);

  QList<ItemPath> items;
  items.append(ItemPath{present, "/somewhere/art.jpg"});
  // Generate enough missing entries to exceed the sample cap so we can
  // verify the count is the real total while the samples list stops at
  // kMaxSamples.
  const int missing = CollectionHealth::kMaxSamples + 5;
  for (int i = 0; i < missing; ++i) {
    items.append(ItemPath{dir.filePath(QStringLiteral("missing%1.mp4").arg(i)), QString()});
  }

  CollectionConfig cfg;
  cfg.name = "Test";
  const Report r = CollectionHealth::analyze(cfg, items, [](const QString &) { return true; });
  QCOMPARE(r.totalItems, items.size());
  QCOMPARE(r.missingFileCount, missing);
  QCOMPARE(r.missingFileSamples.size(), CollectionHealth::kMaxSamples);
}

void TestCollectionHealth::emptyArtworkPath_countedAsMissing() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString present = dir.filePath("a.mp4");
  touch(present);

  // One item with artwork, one without — only the latter should count.
  QList<ItemPath> items;
  items.append(ItemPath{present, "/path/to/art.png"});
  items.append(ItemPath{present, QString()});
  items.append(ItemPath{present, QStringLiteral("   ")}); // whitespace = empty

  CollectionConfig cfg;
  cfg.name = "Test";
  const Report r = CollectionHealth::analyze(cfg, items, [](const QString &) { return true; });
  QCOMPARE(r.missingArtworkCount, 2);
}

void TestCollectionHealth::launcher_emptyPath_flaggedAsUnconfigured() {
  CollectionConfig cfg;
  cfg.name = "Test";
  cfg.launcher.launcherPath = ""; // primary unconfigured
  const Report r = CollectionHealth::analyze(cfg, {}, [](const QString &) { return true; });
  QCOMPARE(r.launcherIssues.size(), 1);
  QVERIFY(r.launcherIssues.first().issue.contains("No launcher path"));
}

void TestCollectionHealth::launcher_validatorFalse_flagsExecutableNotFound() {
  CollectionConfig cfg;
  cfg.name = "Test";
  cfg.launcher.launcherPath = "nonexistent-binary";
  // Validator returns false → analyzer surfaces the "not found" message.
  const Report r = CollectionHealth::analyze(cfg, {}, [](const QString &) { return false; });
  QCOMPARE(r.launcherIssues.size(), 1);
  QVERIFY(r.launcherIssues.first().issue.contains("Executable not found"));
}

void TestCollectionHealth::launcher_validatorTrue_noIssue() {
  CollectionConfig cfg;
  cfg.name = "Test";
  cfg.launcher.launcherPath = "/usr/bin/some-binary";
  const Report r = CollectionHealth::analyze(cfg, {}, [](const QString &) { return true; });
  QVERIFY(r.launcherIssues.isEmpty());
}

QTEST_MAIN(TestCollectionHealth)
#include "test_collectionhealth.moc"
