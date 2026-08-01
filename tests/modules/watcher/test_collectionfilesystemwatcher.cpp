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
  void configure_supersededConfigure_onlyNewestDirsWatched();
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

  // configure() registers the root synchronously; the subdirectory walk runs
  // on the worker pool and its result is applied when the event loop spins.
  QCOMPARE(w.watchedPaths().size(), 1);
  // root + a + a/b = 3
  QTRY_COMPARE_WITH_TIMEOUT(w.watchedPaths().size(), 3, 15000);
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
  // Let the async registration walk land first so the reconfigure below
  // tears down a fully-registered watch set, not just the root.
  QTRY_COMPARE_WITH_TIMEOUT(w.watchedPaths().size(), 2, 15000);

  // Flip the toggle off and reconfigure — every watch should disappear.
  cfg.watchFilesystem = false;
  w.configure({cfg});
  QVERIFY(w.watchedPaths().isEmpty());
}

// Back-to-back configures: A's registration walk may still be in flight when
// B's configure tears the layout down. The generation guard must drop A's
// result when it lands — only B's tree may end up watched.
void TestCollectionFilesystemWatcher::configure_supersededConfigure_onlyNewestDirsWatched() {
  QTemporaryDir tdA;
  QTemporaryDir tdB;
  QVERIFY(tdA.isValid());
  QVERIFY(tdB.isValid());
  makeDir(tdA.path(), "a1");
  makeDir(tdB.path(), "b1");
  makeDir(tdB.path(), "b1/b2");

  CollectionConfig cfgA;
  cfgA.name = "A";
  cfgA.mediaDirectory = tdA.path();
  cfgA.watchFilesystem = true;

  CollectionConfig cfgB;
  cfgB.name = "B";
  cfgB.mediaDirectory = tdB.path();
  cfgB.watchFilesystem = true;

  CollectionFilesystemWatcher w;
  w.configure({cfgA});
  w.configure({cfgB});

  // root + b1 + b1/b2 = 3
  QTRY_COMPARE_WITH_TIMEOUT(w.watchedPaths().size(), 3, 15000);
  // Settle a moment so a stale walk result from the superseded configure had
  // a chance to land (and be dropped) before the watch set is judged.
  QTest::qWait(100);
  const QStringList dirs = w.watchedPaths();
  QCOMPARE(dirs.size(), 3);
  const QString rootB = QFileInfo(tdB.path()).absoluteFilePath();
  for (const QString &d : dirs) {
    QVERIFY2(d.startsWith(rootB),
             qPrintable(QStringLiteral("dir from superseded configure still watched: %1").arg(d)));
  }
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
  // Short enough to keep the test quick. NOTE this is deliberately far below
  // the production default (2000 ms, collectionfilesystemwatcher.h) and
  // nothing in the app ever calls setDebounceMs — so this narrow window is a
  // test artifact, not shipped behaviour. That matters for what we assert
  // below (Kartend-4gahs).
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

  // Wait for the burst to produce a rescan, then settle well past the
  // debounce window so any straggler batch has landed before we judge the
  // count — no trailing timer or deferred walk may still be pending.
  QTRY_VERIFY_WITH_TIMEOUT(rescans >= 1, 15000);
  QTest::qWait(3 * debounceMs);

  QCOMPARE(lastIndex, 0);
  // The reconcile walk completed before the rescan fired, so the new subdirs
  // are already watched: root + a + b + c.
  QCOMPARE(w.watchedPaths().size(), 4);

  // The contract is COALESCING — four mutations must not produce four
  // rescans — not an exact count. Asserting == 1 encoded an inotify
  // assumption: Linux delivers the burst immediately, so one 500 ms window
  // catches all of it. macOS FSEvents batches on its own ~1 s schedule and
  // can split a burst across two windows, producing 2. That is the debounce
  // behaving correctly on a chunkier event source, not a regression — and it
  // does not reach users, because production's 2000 ms window is wider than
  // FSEvents' latency. Kartend-4gahs: this assertion failed on ~25% of macOS
  // CI legs while the code under test was untouched.
  QVERIFY2(
      rescans >= 1 && rescans < 4,
      qPrintable(QStringLiteral("expected 4 mutations to coalesce, got %1 rescans").arg(rescans)));
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
