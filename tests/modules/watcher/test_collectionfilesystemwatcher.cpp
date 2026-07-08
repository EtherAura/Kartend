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
  void directoryChanged_burstCoalescesIntoSingleRescan();
  void directoryChanged_newNestedSubdirsBecomeWatched();
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

// Kartend-309nh.2: the reconcile tree walk moved off the per-event GUI-thread
// path — it now runs once per debounce window, on a QtConcurrent worker, and
// the rescan callback fires after the reconciled watch set has been applied.
// A burst of mutations inside the window must therefore produce exactly one
// rescan and leave every new subdirectory watched.
void TestCollectionFilesystemWatcher::directoryChanged_burstCoalescesIntoSingleRescan() {
  QTemporaryDir td;
  QVERIFY(td.isValid());

  CollectionConfig cfg;
  cfg.name = "C";
  cfg.mediaDirectory = td.path();
  cfg.watchFilesystem = true;

  CollectionFilesystemWatcher w;
  // Long enough for the OS to deliver every event of the burst inside one
  // debounce window even on slow CI runners; short enough to keep the test
  // quick.
  const int debounceMs = 500;
  w.setDebounceMs(debounceMs);
  int rescans = 0;
  int lastIndex = -1;
  w.setRescanCallback([&](int idx) {
    ++rescans;
    lastIndex = idx;
  });
  w.configure({cfg});
  QCOMPARE(w.watchedPaths().size(), 1);

  // Burst: several mutations in quick succession — one directoryChanged per
  // mutation on the root — must coalesce into a single walk + rescan.
  makeDir(td.path(), "a");
  makeDir(td.path(), "b");
  makeDir(td.path(), "c");
  touch(td.path() + "/file.txt");

  QTRY_COMPARE_WITH_TIMEOUT(rescans, 1, 15000);
  QCOMPARE(lastIndex, 0);
  // The reconcile walk completed before the rescan fired, so the new subdirs
  // are already watched: root + a + b + c.
  QCOMPARE(w.watchedPaths().size(), 4);

  // Settle well past the debounce window: no trailing timer or deferred walk
  // may produce a second rescan for the same burst.
  QTest::qWait(3 * debounceMs);
  QCOMPARE(rescans, 1);
}

// Subdirectories created below a directory that was itself just created (so
// its parent was not yet watched when the mutation happened) must still be
// picked up by the debounced re-walk, and the reconciled watches must be
// live — a later mutation inside the new subtree triggers another rescan.
void TestCollectionFilesystemWatcher::directoryChanged_newNestedSubdirsBecomeWatched() {
  QTemporaryDir td;
  QVERIFY(td.isValid());

  CollectionConfig cfg;
  cfg.name = "C";
  cfg.mediaDirectory = td.path();
  cfg.watchFilesystem = true;

  CollectionFilesystemWatcher w;
  w.setDebounceMs(200);
  int rescans = 0;
  w.setRescanCallback([&](int) { ++rescans; });
  w.configure({cfg});
  QCOMPARE(w.watchedPaths().size(), 1);

  // Only the root emits an event here — "a" is not watched yet when "a/b" is
  // created — but the full re-walk must find both.
  makeDir(td.path(), "a");
  makeDir(td.path(), "a/b");
  QTRY_COMPARE_WITH_TIMEOUT(w.watchedPaths().size(), 3, 15000);
  QTRY_VERIFY_WITH_TIMEOUT(rescans >= 1, 15000);

  // The reconciled watch on "a/b" must be live: mutating it triggers a fresh
  // debounce cycle and another rescan.
  const int rescansBefore = rescans;
  makeDir(td.path(), "a/b/c");
  QTRY_VERIFY_WITH_TIMEOUT(rescans > rescansBefore, 15000);
  QTRY_COMPARE_WITH_TIMEOUT(w.watchedPaths().size(), 4, 15000);
}

} // namespace

QTEST_MAIN(TestCollectionFilesystemWatcher)
#include "test_collectionfilesystemwatcher.moc"
