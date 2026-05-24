#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "collectionfilesystemwatcher.h"

namespace {

void touch(const QString &path) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();
}

QString makeDir(const QString &parent, const QString &name) {
  const QString p = parent + QDir::separator() + name;
  QDir().mkpath(p);
  return p;
}

class TestCollectionFilesystemWatcher : public QObject {
  Q_OBJECT
private slots:
  void enumerate_emptyInput_returnsEmpty();
  void enumerate_nonExistent_returnsEmpty();
  void enumerate_singleDir_returnsItself();
  void enumerate_nestedTree_visitsAllSubdirs();
  void enumerate_sortedAscending();
  void configure_disabledCollection_isNotWatched();
  void configure_enabledCollection_watchesEveryDir();
  void configure_reconfigure_removesStaleWatches();
};

void TestCollectionFilesystemWatcher::enumerate_emptyInput_returnsEmpty() {
  QCOMPARE(CollectionFilesystemWatcher::enumerateWatchableSubdirs(""), QStringList{});
  QCOMPARE(CollectionFilesystemWatcher::enumerateWatchableSubdirs("   "), QStringList{});
}

void TestCollectionFilesystemWatcher::enumerate_nonExistent_returnsEmpty() {
  const QString bogus = QDir::tempPath() + "/__kartend_nonexistent__/zzz";
  QCOMPARE(CollectionFilesystemWatcher::enumerateWatchableSubdirs(bogus), QStringList{});
}

void TestCollectionFilesystemWatcher::enumerate_singleDir_returnsItself() {
  QTemporaryDir td;
  QVERIFY(td.isValid());
  const QStringList dirs = CollectionFilesystemWatcher::enumerateWatchableSubdirs(td.path());
  QCOMPARE(dirs.size(), 1);
  QCOMPARE(QFileInfo(dirs.first()).canonicalFilePath(), QFileInfo(td.path()).canonicalFilePath());
}

void TestCollectionFilesystemWatcher::enumerate_nestedTree_visitsAllSubdirs() {
  QTemporaryDir td;
  QVERIFY(td.isValid());
  makeDir(td.path(), "a");
  makeDir(td.path(), "a/b");
  makeDir(td.path(), "a/b/c");
  makeDir(td.path(), "d");
  touch(td.path() + "/file.txt"); // files should be ignored

  const QStringList dirs = CollectionFilesystemWatcher::enumerateWatchableSubdirs(td.path());
  // root + a + a/b + a/b/c + d = 5 directories
  QCOMPARE(dirs.size(), 5);
}

void TestCollectionFilesystemWatcher::enumerate_sortedAscending() {
  QTemporaryDir td;
  QVERIFY(td.isValid());
  makeDir(td.path(), "zz");
  makeDir(td.path(), "aa");
  makeDir(td.path(), "mm");
  const QStringList dirs = CollectionFilesystemWatcher::enumerateWatchableSubdirs(td.path());
  QStringList sorted = dirs;
  std::sort(sorted.begin(), sorted.end());
  QCOMPARE(dirs, sorted);
}

void TestCollectionFilesystemWatcher::configure_disabledCollection_isNotWatched() {
  QTemporaryDir td;
  QVERIFY(td.isValid());
  makeDir(td.path(), "sub");

  CollectionConfig cfg;
  cfg.name = "C";
  cfg.mediaDirectory = td.path();
  cfg.watchFilesystem = false;

  CollectionFilesystemWatcher w;
  w.configure({cfg});
  QVERIFY(w.watchedPaths().isEmpty());
}

void TestCollectionFilesystemWatcher::configure_enabledCollection_watchesEveryDir() {
  QTemporaryDir td;
  QVERIFY(td.isValid());
  makeDir(td.path(), "a");
  makeDir(td.path(), "a/b");

  CollectionConfig cfg;
  cfg.name = "C";
  cfg.mediaDirectory = td.path();
  cfg.watchFilesystem = true;

  CollectionFilesystemWatcher w;
  w.configure({cfg});

  // root + a + a/b = 3
  QCOMPARE(w.watchedPaths().size(), 3);
}

void TestCollectionFilesystemWatcher::configure_reconfigure_removesStaleWatches() {
  QTemporaryDir td;
  QVERIFY(td.isValid());
  makeDir(td.path(), "sub");

  CollectionConfig cfg;
  cfg.name = "C";
  cfg.mediaDirectory = td.path();
  cfg.watchFilesystem = true;

  CollectionFilesystemWatcher w;
  w.configure({cfg});
  QVERIFY(!w.watchedPaths().isEmpty());

  // Flip the toggle off and reconfigure — every watch should disappear.
  cfg.watchFilesystem = false;
  w.configure({cfg});
  QVERIFY(w.watchedPaths().isEmpty());
}

} // namespace

QTEST_MAIN(TestCollectionFilesystemWatcher)
#include "test_collectionfilesystemwatcher.moc"
