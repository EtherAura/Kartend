// Cancellation / drain-with-budget coverage for CacheDiskStorage, plus the
// SQLite timestamp store (Kartend-0ldg2): one-way legacy-JSON migration
// (both ISO-string and epoch-int forms), dirty-row-only upserts, the
// bounded prune pass with its rotating cursor, and a full round trip.
//
// The disk side of the cache runs writes on a dedicated single-thread
// QThreadPool and shuts down via a cooperative cancellation token plus a
// bounded drain; the in-memory side is exercised by test_cachemanager. These
// slots lock in the observable, deterministic semantics:
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

#include <thread>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

namespace {
// A round-second epoch (ms) so the legacy ISO-string form (which truncates
// sub-second precision) round-trips byte-for-byte through the migration.
constexpr qint64 kRoundSecondMs = 1700000000000LL;
const QString kArtworkKey = QStringLiteral("/collections/Video/artwork/clip.png");

// Remove every on-disk shape the store can take so each slot starts from a
// known-clean state: the SQLite file (+ WAL/SHM sidecars), the legacy JSON,
// and a .migrated leftover.
void removeStoreFiles() {
  const QString db = CacheDiskStorage::databasePath();
  // A failure-injection slot may have replaced the store file with a
  // directory of the same name — clean either shape.
  if (QFileInfo(db).isDir()) {
    QDir(db).removeRecursively();
  }
  QFile::remove(db);
  QFile::remove(db + QStringLiteral("-wal"));
  QFile::remove(db + QStringLiteral("-shm"));
  const QString json = CacheDiskStorage::metadataPath();
  QFile::remove(json);
  QFile::remove(json + QStringLiteral(".migrated"));
}

// Write a legacy artwork_cache.json in the pre-Kartend-0ldg2 shape.
void writeLegacyJson(const QJsonObject &timestamps) {
  QJsonObject root;
  root[QStringLiteral("timestamps")] = timestamps;
  QFile file(CacheDiskStorage::metadataPath());
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  file.close();
}

// Read the whole store through an independent connection — the "second
// connection" verification the dirty-row contract calls for.
QHash<QString, qint64> readStoreDirectly() {
  QHash<QString, qint64> rows;
  const QString connectionName = QStringLiteral("test-cachediskstorage-direct");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(CacheDiskStorage::databasePath());
    if (db.open()) {
      QSqlQuery query(db);
      if (query.exec(QStringLiteral("SELECT path, ts_ms FROM timestamps"))) {
        while (query.next()) {
          rows.insert(query.value(0).toString(), query.value(1).toLongLong());
        }
      }
      db.close();
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  return rows;
}
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

  // SQLite timestamp store (Kartend-0ldg2)
  void legacyJsonMigratesOnFirstOpen();
  void migrationDoesNotClobberNewerStoreRows();
  void writeTimestampsUpsertsOnlyDirtyRows();
  // Invalidation propagation (Kartend-9lm54)
  void writeTimestampsDeletesRemovedRowsInSameBatch();
  void removalsOnlyBatchDoesNotCreateStore();
  void scheduledAsyncSaveAppliesRemovals();
  // Failed / dropped removal batches stay staged for retry
  void cancelledBatchKeepsRemovalsStaged();
  void failedRemovalWriteIsRestagedForRetry();
  void pruneDeletesOnlyDeadRows();
  void pruneIsBoundedAndCursorRotates();
  void fullRoundTripManyRows();
  // cacheDirectory() hot-path memoization
  void cacheDirectoryMemoizesEnsuredLayout();

private:
  // Per-process HOME so this test's Qt test-mode QStandardPaths tree — and
  // thus the timestamp-store path it asserts on — is unique to this
  // process. See initTestCase for why this is load-bearing (Kartend-j1ahs).
  QTemporaryDir m_homeDir;
};

void TestCacheDiskStorage::initTestCase() {
  // Isolate this process's QStandardPaths tree so a sibling test running in
  // parallel under `ctest -j` can't create or clobber the store this test
  // asserts the presence/absence of. cacheDirectory() resolves to
  // GenericCacheLocation + "/kartend", which in Qt test mode is
  // ~/.qttest/cache/kartend with NO per-binary suffix — so every test process
  // otherwise shares one timestamp store. An integration test that spins up
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
  // Each slot reasons about the store's presence/absence/contents, so start
  // every one from a known-clean state.
  removeStoreFiles();
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
  // blocks until the in-flight write completes (so the store exists on
  // return). This is what makes the cancelled-save assertion below meaningful
  // — it proves an absent store there is due to cancellation, not a broken
  // pipeline.
  const QString dbPath = CacheDiskStorage::databasePath();
  QVERIFY(!QFileInfo::exists(dbPath));

  CacheDiskStorage storage;
  QHash<QString, qint64> timestamps;
  timestamps.insert(kArtworkKey, kRoundSecondMs);
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, timestamps, {}, {});

  QVERIFY2(storage.drainWithBudget(5000),
           "drain should report a clean shutdown once the write finishes");
  QVERIFY2(QFileInfo::exists(dbPath),
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
  // check, leaving nothing on disk (not even an empty store).
  const QString dbPath = CacheDiskStorage::databasePath();
  QVERIFY(!QFileInfo::exists(dbPath));

  CacheDiskStorage storage;
  storage.cancel();
  QVERIFY(storage.isCancelled());

  QHash<QString, qint64> timestamps;
  timestamps.insert(kArtworkKey, kRoundSecondMs);
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, timestamps, {}, {});

  QVERIFY2(storage.drainWithBudget(5000), "a bailed task should drain immediately");
  QVERIFY2(!QFileInfo::exists(dbPath),
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
  const QString dbPath = CacheDiskStorage::databasePath();
  QVERIFY(!QFileInfo::exists(dbPath));
  QHash<QString, qint64> timestamps;
  timestamps.insert(kArtworkKey, kRoundSecondMs);
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, timestamps, {}, {});
  QVERIFY2(!QFileInfo::exists(dbPath), "scheduling after drain must not write");
}

// ─── SQLite timestamp store (Kartend-0ldg2) ──────────────────────────────────

void TestCacheDiskStorage::legacyJsonMigratesOnFirstOpen() {
  // A legacy JSON containing BOTH historical value forms — the original
  // ISO-8601 string and the phase-1 epoch-ms integer — must be imported on
  // the store's first open and renamed to .migrated (one-way).
  const QString isoKey = QStringLiteral("/collections/Video/artwork/iso.png");
  const QString epochKey = QStringLiteral("/collections/Audio/artwork/epoch.png");
  // Generate the ISO string exactly the way the legacy writer did, so the
  // local-time round trip is faithful regardless of the machine's zone.
  const QString isoValue = QDateTime::fromMSecsSinceEpoch(kRoundSecondMs).toString(Qt::ISODate);

  QJsonObject timestamps;
  timestamps[isoKey] = isoValue;
  timestamps[epochKey] = static_cast<double>(kRoundSecondMs + 1000);
  writeLegacyJson(timestamps);

  CacheDiskStorage storage;
  QHash<QString, qint64> loaded;
  storage.readTimestampsInto(loaded);

  QCOMPARE(loaded.size(), 2);
  QCOMPARE(loaded.value(isoKey), kRoundSecondMs);
  QCOMPARE(loaded.value(epochKey), kRoundSecondMs + 1000);

  QVERIFY2(QFileInfo::exists(CacheDiskStorage::databasePath()),
           "migration must materialize the SQLite store");
  QVERIFY2(!QFileInfo::exists(CacheDiskStorage::metadataPath()),
           "the legacy JSON must be renamed away (one-way migration)");
  QVERIFY2(QFileInfo::exists(CacheDiskStorage::metadataPath() + QStringLiteral(".migrated")),
           "the legacy JSON must be preserved under the .migrated name");
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheDiskStorage::migrationDoesNotClobberNewerStoreRows() {
  // INSERT OR IGNORE semantics: if the store already has a row for a key
  // (e.g. an earlier import committed but the rename failed), the legacy
  // JSON's stale value must NOT overwrite it.
  QHash<QString, qint64> fresh;
  fresh.insert(kArtworkKey, kRoundSecondMs + 5000);
  CacheDiskStorage::writeTimestamps(fresh); // creates the store, no JSON yet

  QJsonObject timestamps;
  timestamps[kArtworkKey] = QDateTime::fromMSecsSinceEpoch(kRoundSecondMs).toString(Qt::ISODate);
  writeLegacyJson(timestamps);

  CacheDiskStorage storage;
  QHash<QString, qint64> loaded;
  storage.readTimestampsInto(loaded); // triggers the migration
  QCOMPARE(loaded.value(kArtworkKey), kRoundSecondMs + 5000);
  QVERIFY(!QFileInfo::exists(CacheDiskStorage::metadataPath()));
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheDiskStorage::writeTimestampsUpsertsOnlyDirtyRows() {
  // Seed three rows, then write a single-row dirty batch. The other rows
  // must come back bit-identical through an independent connection, and the
  // row count must not change — the keyed store replaces the old
  // read-everything/merge/rewrite-everything JSON cycle.
  const QString keyA = QStringLiteral("/a.png");
  const QString keyB = QStringLiteral("/b.png");
  const QString keyC = QStringLiteral("/c.png");
  QHash<QString, qint64> seed;
  seed.insert(keyA, 1000);
  seed.insert(keyB, 2000);
  seed.insert(keyC, 3000);
  CacheDiskStorage::writeTimestamps(seed);

  QHash<QString, qint64> dirty;
  dirty.insert(keyB, 9999);
  CacheDiskStorage::writeTimestamps(dirty);

  const QHash<QString, qint64> rows = readStoreDirectly();
  QCOMPARE(rows.size(), 3);
  QCOMPARE(rows.value(keyA), qint64(1000));
  QCOMPARE(rows.value(keyB), qint64(9999));
  QCOMPARE(rows.value(keyC), qint64(3000));
}

void TestCacheDiskStorage::writeTimestampsDeletesRemovedRowsInSameBatch() {
  // Invalidation propagation (Kartend-9lm54): a write batch carrying both
  // upserts and removals must DELETE exactly the removed keys and upsert the
  // dirty ones, leaving everything else untouched. A key present in BOTH
  // batches (invalidated, then re-cached within the same debounce window)
  // must end with the fresh upserted row — removals run first.
  const QString keyA = QStringLiteral("/a.png");
  const QString keyB = QStringLiteral("/b.png");
  const QString keyC = QStringLiteral("/c.png");
  QHash<QString, qint64> seed;
  seed.insert(keyA, 1000);
  seed.insert(keyB, 2000);
  seed.insert(keyC, 3000);
  CacheDiskStorage::writeTimestamps(seed);

  QHash<QString, qint64> dirty;
  dirty.insert(keyB, 9999);
  CacheDiskStorage::writeTimestamps(dirty, {keyA});

  QHash<QString, qint64> rows = readStoreDirectly();
  QCOMPARE(rows.size(), 2);
  QVERIFY2(!rows.contains(keyA), "the invalidated row must be deleted from the store");
  QCOMPARE(rows.value(keyB), qint64(9999));
  QCOMPARE(rows.value(keyC), qint64(3000));

  // Same key in both batches: delete-then-upsert keeps the fresh row.
  QHash<QString, qint64> recached;
  recached.insert(keyC, 7777);
  CacheDiskStorage::writeTimestamps(recached, {keyC});
  rows = readStoreDirectly();
  QCOMPARE(rows.value(keyC), qint64(7777));

  // A removals-only batch against an existing store also lands.
  CacheDiskStorage::writeTimestamps({}, {keyB});
  rows = readStoreDirectly();
  QCOMPARE(rows.size(), 1);
  QVERIFY(!rows.contains(keyB));
}

void TestCacheDiskStorage::removalsOnlyBatchDoesNotCreateStore() {
  // A removals-only batch when no store exists has nothing to delete — it
  // must not materialize an empty store (mirrors pruneDeadTimestamps).
  const QString dbPath = CacheDiskStorage::databasePath();
  QVERIFY(!QFileInfo::exists(dbPath));
  CacheDiskStorage::writeTimestamps({}, {kArtworkKey});
  QVERIFY2(!QFileInfo::exists(dbPath), "a removals-only batch must not create an empty store");
}

void TestCacheDiskStorage::scheduledAsyncSaveAppliesRemovals() {
  // The async-save lambda must carry the removals through to
  // writeTimestamps: schedule a metadata save whose batch is removals-only
  // and verify the row is gone once the drain returns.
  QHash<QString, qint64> seed;
  seed.insert(kArtworkKey, kRoundSecondMs);
  CacheDiskStorage::writeTimestamps(seed);

  CacheDiskStorage storage;
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, {}, {kArtworkKey}, {});
  QVERIFY2(storage.drainWithBudget(5000),
           "drain should report a clean shutdown once the write finishes");

  const QHash<QString, qint64> rows = readStoreDirectly();
  QVERIFY2(!rows.contains(kArtworkKey),
           "a scheduled async save must delete the invalidated row from the store");
  QVERIFY2(storage.pendingRemovals().isEmpty(),
           "a committed removals batch must not stay staged for retry");
}

void TestCacheDiskStorage::cancelledBatchKeepsRemovalsStaged() {
  // scheduleAsyncSave stages removals BEFORE dispatching, so a batch whose
  // task bails at the cancellation check (or is dropped by clearQueue)
  // keeps its deletions queued for the shutdown flush instead of losing
  // them — previously they were gone the moment the dirty sets were
  // snapshotted.
  QHash<QString, qint64> seed;
  seed.insert(kArtworkKey, kRoundSecondMs);
  QVERIFY(CacheDiskStorage::writeTimestamps(seed));

  CacheDiskStorage storage;
  storage.cancel();
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, {}, {kArtworkKey}, {});
  QVERIFY(storage.drainWithBudget(5000));

  // The bailed task must not have touched the store...
  QVERIFY(readStoreDirectly().contains(kArtworkKey));
  // ...but the batch stays staged, and applying it (as the shutdown flush
  // does via takePendingRemovals) deletes the row.
  const QStringList staged = storage.takePendingRemovals();
  QCOMPARE(staged, QStringList{kArtworkKey});
  QVERIFY2(storage.takePendingRemovals().isEmpty(), "take must drain the staged set");
  QVERIFY(CacheDiskStorage::writeTimestamps({}, staged));
  QVERIFY(!readStoreDirectly().contains(kArtworkKey));
}

void TestCacheDiskStorage::failedRemovalWriteIsRestagedForRetry() {
  // Force the store write itself to fail by replacing the SQLite file with
  // a DIRECTORY of the same name (deterministic open failure — works even
  // when the test runs as root, unlike permission tricks). The task must
  // re-stage its removals batch so a later write can still apply it.
  const QString dbPath = CacheDiskStorage::databasePath();
  QVERIFY(QDir().mkpath(dbPath));

  CacheDiskStorage storage;
  storage.scheduleAsyncSave(/*shouldWriteMetadata=*/true, {}, {kArtworkKey}, {});
  QVERIFY(storage.drainWithBudget(5000));

  const QStringList retained = storage.takePendingRemovals();
  QCOMPARE(retained, QStringList{kArtworkKey});

  // Unblock the store, materialize the row, and apply the retained batch —
  // exactly what the next flush (or the shutdown write) does.
  QVERIFY(QDir().rmdir(dbPath));
  QHash<QString, qint64> seed;
  seed.insert(kArtworkKey, kRoundSecondMs);
  QVERIFY(CacheDiskStorage::writeTimestamps(seed));
  QVERIFY(CacheDiskStorage::writeTimestamps({}, retained));
  QVERIFY(!readStoreDirectly().contains(kArtworkKey));
}

void TestCacheDiskStorage::pruneDeletesOnlyDeadRows() {
  // One row whose source file exists, two whose paths are dead. A prune pass
  // with a generous budget must delete exactly the dead rows and report
  // them, leaving the live row untouched.
  QTemporaryDir liveDir;
  QVERIFY(liveDir.isValid());
  const QString livePath = liveDir.path() + QStringLiteral("/live.png");
  {
    QFile liveFile(livePath);
    QVERIFY(liveFile.open(QIODevice::WriteOnly));
    liveFile.write("x");
  }
  const QString deadA = QStringLiteral("/nonexistent/a.png");
  const QString deadB = QStringLiteral("/nonexistent/b.png");

  QHash<QString, qint64> seed;
  seed.insert(livePath, 1);
  seed.insert(deadA, 2);
  seed.insert(deadB, 3);
  CacheDiskStorage::writeTimestamps(seed);

  // Cached PNGs for one dead and the live row: the prune must delete the
  // dead row's PNG together with the row (the row holds the only way to
  // re-derive the hashed filename — deleting the row alone strands the PNG
  // as a permanent orphan) and leave the live row's PNG untouched.
  const QString deadCachePng = CacheDiskStorage::artworkCachePath(deadA);
  const QString liveCachePng = CacheDiskStorage::artworkCachePath(livePath);
  QVERIFY(QDir().mkpath(QFileInfo(deadCachePng).absolutePath()));
  for (const QString &png : {deadCachePng, liveCachePng}) {
    QFile f(png);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("png-bytes");
  }

  QString cursor;
  const QStringList removed = CacheDiskStorage::pruneDeadTimestamps(100, &cursor);
  QCOMPARE(removed.size(), 2);
  QVERIFY(removed.contains(deadA));
  QVERIFY(removed.contains(deadB));
  QVERIFY2(cursor.isEmpty(), "a short batch must wrap the cursor");

  const QHash<QString, qint64> rows = readStoreDirectly();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.value(livePath), qint64(1));
  QVERIFY2(!QFile::exists(deadCachePng), "dead row's cached PNG must be deleted with the row");
  QVERIFY2(QFile::exists(liveCachePng), "live row's cached PNG must survive the prune");
}

void TestCacheDiskStorage::pruneIsBoundedAndCursorRotates() {
  // Three dead rows, budget of one per pass: each pass examines exactly one
  // row (ordered by path), advances the cursor, and the pass after the
  // keyspace is exhausted wraps the cursor back to the start.
  const QString deadA = QStringLiteral("/nx/a.png");
  const QString deadB = QStringLiteral("/nx/b.png");
  const QString deadC = QStringLiteral("/nx/c.png");
  QHash<QString, qint64> seed;
  seed.insert(deadA, 1);
  seed.insert(deadB, 2);
  seed.insert(deadC, 3);
  CacheDiskStorage::writeTimestamps(seed);

  QString cursor;
  QStringList removed = CacheDiskStorage::pruneDeadTimestamps(1, &cursor);
  QCOMPARE(removed, QStringList{deadA});
  QCOMPARE(cursor, deadA);

  removed = CacheDiskStorage::pruneDeadTimestamps(1, &cursor);
  QCOMPARE(removed, QStringList{deadB});
  QCOMPARE(cursor, deadB);

  removed = CacheDiskStorage::pruneDeadTimestamps(1, &cursor);
  QCOMPARE(removed, QStringList{deadC});
  QCOMPARE(cursor, deadC);

  // Keyspace exhausted: empty batch, no removals, cursor wraps for the next
  // revolution.
  removed = CacheDiskStorage::pruneDeadTimestamps(1, &cursor);
  QVERIFY(removed.isEmpty());
  QVERIFY(cursor.isEmpty());

  QVERIFY(readStoreDirectly().isEmpty());
}

void TestCacheDiskStorage::fullRoundTripManyRows() {
  // Write a few hundred rows across two batches (exercising upsert-into-
  // existing), read them all back through readTimestampsInto.
  QHash<QString, qint64> first;
  QHash<QString, qint64> second;
  for (int i = 0; i < 250; ++i) {
    first.insert(QStringLiteral("/roundtrip/first_%1.png").arg(i), kRoundSecondMs + i);
    second.insert(QStringLiteral("/roundtrip/second_%1.png").arg(i), kRoundSecondMs - i);
  }
  CacheDiskStorage::writeTimestamps(first);
  CacheDiskStorage::writeTimestamps(second);

  CacheDiskStorage storage;
  QHash<QString, qint64> loaded;
  storage.readTimestampsInto(loaded);
  QCOMPARE(loaded.size(), 500);
  QCOMPARE(loaded.value(QStringLiteral("/roundtrip/first_42.png")), kRoundSecondMs + 42);
  QCOMPARE(loaded.value(QStringLiteral("/roundtrip/second_199.png")), kRoundSecondMs - 199);
  QVERIFY(storage.drainWithBudget(1000));
}

void TestCacheDiskStorage::cacheDirectoryMemoizesEnsuredLayout() {
  // This thread already ensured the layout (init() resolves databasePath(),
  // which routes through cacheDirectory()), so subsequent same-thread calls
  // must take the memoized fast path — no re-stat, no re-mkpath. Observable
  // negative: a subdirectory deleted after the ensure is NOT healed by a
  // same-thread call...
  const QString root = CacheDiskStorage::cacheDirectory();
  const QString artworkDir = root + QStringLiteral("/artwork");
  QVERIFY(QFileInfo::exists(artworkDir));
  QVERIFY(QDir(artworkDir).removeRecursively());
  QCOMPARE(CacheDiskStorage::cacheDirectory(), root);
  QVERIFY2(!QFileInfo::exists(artworkDir),
           "same-thread calls must take the memoized fast path (no re-ensure)");

  // ...while a thread that never ensured the layout re-verifies once and
  // heals it (worker-pool threads are reaped and recreated over a session,
  // so each fresh thread pays exactly one verification round).
  std::thread freshThread([]() { (void)CacheDiskStorage::cacheDirectory(); });
  freshThread.join();
  QVERIFY2(QFileInfo::exists(artworkDir),
           "a fresh thread's first call must re-ensure the cache layout");
}

QTEST_GUILESS_MAIN(TestCacheDiskStorage)
#include "test_cachediskstorage.moc"
