// Cancellation / drain-with-budget coverage for CacheDiskStorage. The disk
// side of the cache runs writes on a dedicated single-thread QThreadPool and
// shuts down via a cooperative cancellation token plus a bounded drain; the
// in-memory side is exercised by test_cachemanager, but this token/drain path
// (the one the LSan QThreadPool suppression papers over) had no direct test.
// These slots lock in the observable, deterministic semantics:
//   - cancel() flips isCancelled();
//   - a clean drain of an idle pool reports success;
//   - a scheduled metadata save is flushed before drainWithBudget() returns;
//   - cancelling before a save runs makes the queued task bail without writing;
//   - drainWithBudget() is single-shot and post-drain scheduling is a safe no-op.
// The budget-EXPIRED leak path (a runaway task outliving the drain budget) is
// intentionally not asserted here — forcing it needs a guaranteed-slow task and
// the assertion would be timing-dependent; that path is covered by the LSan
// suppression + ~QThreadPool's blocking reap. (Kartend-3qyih)
#include "cachediskstorage.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {
// A round-second epoch (ms) so it survives the metadata file's ISODate
// round-trip (which truncates sub-second precision) byte-for-byte.
constexpr qint64 kRoundSecondMs = 1700000000000LL;
const QString kArtworkKey = QStringLiteral("/collections/Retro/artwork/game.png");
} // namespace

class TestCacheDiskStorage : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();

  void cancelFlipsIsCancelled();
  void drainOfIdlePoolReportsCleanDrain();
  void scheduledMetadataSaveIsFlushedOnDrain();
  void cancelBeforeRunSkipsTheWrite();
  void drainIsSingleShotAndPostDrainScheduleIsNoOp();

private:
  // Per-process HOME so this test's Qt test-mode QStandardPaths tree — and
  // thus the artwork_cache.json path it asserts on — is unique to this
  // process. See initTestCase for why this is load-bearing (Kartend-j1ahs).
  QTemporaryDir m_homeDir;
};

void TestCacheDiskStorage::initTestCase() {
  // Isolate this process's QStandardPaths tree so a sibling test running in
  // parallel under `ctest -j` can't create or clobber the metadata file this
  // test asserts the presence/absence of. cacheDirectory() resolves to
  // GenericCacheLocation + "/kartend", which in Qt test mode is
  // ~/.qttest/cache/kartend with NO per-binary suffix — so every test process
  // otherwise shares one artwork_cache.json. An integration test that spins up
  // CacheManager and flushes the cache on shutdown was racing this test's
  // file-existence assertions, flaking it ~1-in-N under load (Kartend-j1ahs).
  // Test mode ignores XDG_CACHE_HOME but honours HOME (re-read per call), so
  // point HOME at a private per-process temp dir before enabling test mode.
  QVERIFY(m_homeDir.isValid());
  qputenv("HOME", m_homeDir.path().toUtf8());

  // Redirect the cache dir under QStandardPaths into a throwaway test
  // location so these slots can't read or clobber the developer's real
  // artwork-cache metadata.
  QStandardPaths::setTestModeEnabled(true);
}

void TestCacheDiskStorage::init() {
  // Each slot reasons about the metadata file's presence/absence, so start
  // every one from a known-clean state.
  QFile::remove(CacheDiskStorage::metadataPath());
}

void TestCacheDiskStorage::cancelFlipsIsCancelled() {
  CacheDiskStorage storage;
  QVERIFY(!storage.isCancelled());
  storage.cancel();
  QVERIFY(storage.isCancelled());
  // Drain so the pool is deleted rather than leaked (the destructor only
  // clears the queue; drainWithBudget owns the delete).
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheDiskStorage::drainOfIdlePoolReportsCleanDrain() {
  CacheDiskStorage storage;
  // Nothing scheduled — the pool must drain cleanly (and well) within budget.
  QVERIFY(storage.drainWithBudget(5000));
}

void TestCacheDiskStorage::scheduledMetadataSaveIsFlushedOnDrain() {
  // Positive control: a non-cancelled save reaches disk, and drainWithBudget()
  // blocks until the in-flight write completes (so the file exists on return).
  // This is what makes the cancelled-save assertion below meaningful — it
  // proves an absent file there is due to cancellation, not a broken pipeline.
  const QString meta = CacheDiskStorage::metadataPath();
  QVERIFY(!QFileInfo::exists(meta));

  CacheDiskStorage storage;
  QHash<QString, qint64> timestamps;
  timestamps.insert(kArtworkKey, kRoundSecondMs);
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, timestamps, {});

  QVERIFY2(storage.drainWithBudget(5000),
           "drain should report a clean shutdown once the write finishes");
  QVERIFY2(QFileInfo::exists(meta),
           "scheduled metadata save should be flushed before drain returns");

  // Read it back through the same (uncancelled) storage to prove the entry
  // round-tripped, exercising the read path too.
  QHash<QString, qint64> loaded;
  storage.readTimestampsInto(loaded);
  QVERIFY(loaded.contains(kArtworkKey));
  QCOMPARE(loaded.value(kArtworkKey), kRoundSecondMs);
}

void TestCacheDiskStorage::cancelBeforeRunSkipsTheWrite() {
  // Cancelling before the queued task runs must make it bail at the token
  // check, leaving nothing on disk.
  const QString meta = CacheDiskStorage::metadataPath();
  QVERIFY(!QFileInfo::exists(meta));

  CacheDiskStorage storage;
  storage.cancel();
  QVERIFY(storage.isCancelled());

  QHash<QString, qint64> timestamps;
  timestamps.insert(kArtworkKey, kRoundSecondMs);
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, timestamps, {});

  QVERIFY2(storage.drainWithBudget(5000), "a bailed task should drain immediately");
  QVERIFY2(!QFileInfo::exists(meta),
           "a cancelled storage must not flush queued metadata to disk");
}

void TestCacheDiskStorage::drainIsSingleShotAndPostDrainScheduleIsNoOp() {
  CacheDiskStorage storage;
  // First drain deletes + nulls the pool; the second sees a null pool and
  // still reports a clean drain. Calling it twice must be safe.
  QVERIFY(storage.drainWithBudget(1000));
  QVERIFY(storage.drainWithBudget(1000));

  // With the pool gone, scheduling is a silent no-op — it must not crash and
  // must not write anything (there is no worker left to run the task).
  const QString meta = CacheDiskStorage::metadataPath();
  QVERIFY(!QFileInfo::exists(meta));
  QHash<QString, qint64> timestamps;
  timestamps.insert(kArtworkKey, kRoundSecondMs);
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, timestamps, {});
  QVERIFY2(!QFileInfo::exists(meta), "scheduling after drain must not write");
}

QTEST_GUILESS_MAIN(TestCacheDiskStorage)
#include "test_cachediskstorage.moc"
