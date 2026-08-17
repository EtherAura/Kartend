// LauncherManifestWatcher (Kartend-5vuqy): the debounce that keeps a Steam
// download's manifest churn from triggering a re-sync per write, and the
// reconcile that keeps the watch alive across a directory being deleted and
// recreated.
//
// Changes are injected through notifyChangeForTesting rather than by touching
// files: QFileSystemWatcher delivery is asynchronous and platform-dependent,
// and a test that waits on it is a test that goes flaky on a loaded machine.
// What is actually under test is the coalescing and the reconcile, both of
// which are ours.
#include <QDir>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "launchermanifestwatcher.h"

class TestLauncherManifestWatcher : public QObject {
  Q_OBJECT

private slots:
  void burstOfChangesCoalescesIntoOneSync();
  void noCallbackWithoutAChange();
  void onlyExistingDirectoriesAreRegistered();
  void directoryRecreatedAfterDeletionIsWatchedAgain();
  void reconfiguringDropsAPendingBurst();
  void defaultDebounceIsGenerous();

private:
  static QString makeDir(const QTemporaryDir &root, const QString &name) {
    const QString path = root.path() + QLatin1Char('/') + name;
    QDir().mkpath(path);
    return path;
  }
};

void TestLauncherManifestWatcher::burstOfChangesCoalescesIntoOneSync() {
  // The point of the class: Steam rewrites appmanifest_*.acf on start, on
  // progress and on completion. Each of those fires the watcher, and a
  // re-sync per write would hammer every launcher collection.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString steamapps = makeDir(root, "steamapps");

  LauncherManifestWatcher watcher;
  int syncs = 0;
  watcher.setSyncCallback([&syncs]() { ++syncs; });
  watcher.setDebounceInterval(60);
  watcher.setWatchedDirectories({steamapps});

  for (int i = 0; i < 12; ++i) {
    watcher.notifyChangeForTesting(steamapps);
  }
  QCOMPARE(syncs, 0); // nothing fires while the burst is still arriving
  QTRY_COMPARE_WITH_TIMEOUT(syncs, 1, 2000);

  // And the timer does not keep firing once the burst has settled.
  QTest::qWait(150);
  QCOMPARE(syncs, 1);
}

void TestLauncherManifestWatcher::noCallbackWithoutAChange() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  LauncherManifestWatcher watcher;
  int syncs = 0;
  watcher.setSyncCallback([&syncs]() { ++syncs; });
  watcher.setDebounceInterval(30);
  watcher.setWatchedDirectories({makeDir(root, "steamapps")});

  QTest::qWait(150);
  QCOMPARE(syncs, 0);
}

void TestLauncherManifestWatcher::onlyExistingDirectoriesAreRegistered() {
  // Callers pass every source's paths unconditionally, so a launcher that is
  // not installed must not produce a warning or a phantom watch.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString real = makeDir(root, "present");
  const QString missing = root.path() + QStringLiteral("/absent");

  LauncherManifestWatcher watcher;
  watcher.setWatchedDirectories({real, missing});

  QCOMPARE(watcher.activeDirectories(), QStringList{real});
  QCOMPARE(watcher.requestedDirectories().size(), 2); // both remembered
}

void TestLauncherManifestWatcher::directoryRecreatedAfterDeletionIsWatchedAgain() {
  // QFileSystemWatcher drops a path when it disappears and never takes it
  // back. An uninstall that empties a Steam library folder would therefore
  // kill the watch silently, and everything would still look wired up.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString steamapps = makeDir(root, "steamapps");

  LauncherManifestWatcher watcher;
  int syncs = 0;
  watcher.setSyncCallback([&syncs]() { ++syncs; });
  watcher.setDebounceInterval(40);
  watcher.setWatchedDirectories({steamapps});
  QCOMPARE(watcher.activeDirectories(), QStringList{steamapps});

  QVERIFY(QDir(steamapps).removeRecursively());
  QTRY_VERIFY_WITH_TIMEOUT(watcher.activeDirectories().isEmpty(), 2000);

  QDir().mkpath(steamapps);
  watcher.notifyChangeForTesting(steamapps);
  QTRY_COMPARE_WITH_TIMEOUT(syncs, 1, 2000);
  // Reconciled on the debounce tick, so the next install is still noticed.
  QCOMPARE(watcher.activeDirectories(), QStringList{steamapps});
}

void TestLauncherManifestWatcher::reconfiguringDropsAPendingBurst() {
  // A burst belongs to the watch set that produced it. Firing it after the
  // set changed would sync on behalf of directories nobody watches now.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString first = makeDir(root, "first");
  const QString second = makeDir(root, "second");

  LauncherManifestWatcher watcher;
  int syncs = 0;
  watcher.setSyncCallback([&syncs]() { ++syncs; });
  watcher.setDebounceInterval(80);
  watcher.setWatchedDirectories({first});
  watcher.notifyChangeForTesting(first);

  watcher.setWatchedDirectories({second}); // cancels the pending burst
  QTest::qWait(250);
  QCOMPARE(syncs, 0);
  QCOMPARE(watcher.activeDirectories(), QStringList{second});
}

void TestLauncherManifestWatcher::defaultDebounceIsGenerous() {
  // Documents the intent: seconds, not milliseconds. Being late is free
  // because the sync is idempotent and nothing waits on it; being eager
  // means several full syncs per install.
  LauncherManifestWatcher watcher;
  QVERIFY2(watcher.debounceInterval() >= 5000,
           qPrintable(QStringLiteral("debounce is %1 ms, too eager for Steam's manifest churn")
                          .arg(watcher.debounceInterval())));
}

QTEST_MAIN(TestLauncherManifestWatcher)
#include "test_launchermanifestwatcher.moc"
