// Regression tests for the items_fts readiness pipeline (Kartend-4i5e4).
//
// The v3 migration created the items_fts external-content table AND its sync
// triggers in one block, while the index itself was only populated later by an
// incremental backfill on the scan worker. That left a window in which the
// items_fts_au / items_fts_ad triggers issued FTS5 'delete' commands for rows
// the backfill had not indexed yet. Deleting postings that were never written
// is documented FTS5 external-content corruption: on newer SQLite it is
// detected and the triggering items UPDATE/DELETE fails outright with
// SQLITE_CORRUPT ("database disk image is malformed"), i.e. a routine rescan
// upsert on an upgraded install broke scan writes until the backfill finished.
//
// What did NOT reproduce while building these tests (CLI probe + this suite):
// the "double-populated index / inflated MATCH counts" symptom from the
// original report. Re-INSERTing an already-indexed rowid with identical values
// is last-write-wins in FTS5 (the newest segment supersedes older postings for
// the same (term, rowid)), so the fresh-install flow — trigger-indexed scan
// rows re-inserted by the next launch's backfill — converged to a consistent
// index. The fresh-install test below pins that contract anyway.
//
// The fix replaces the incremental backfill with a one-shot
// INSERT INTO items_fts(items_fts) VALUES('rebuild') executed in a single
// transaction that also (re)creates the sync triggers and stamps the readiness
// generation — so trigger existence implies index consistency, and any write
// that lands before the rebuild simply goes un-triggered and is captured by
// the rebuild itself. These tests drive the real QueryManager slots against a
// real on-disk SQLite database (docs/dev/testing.md: no DB mocking).
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "../../support/inspectordb.h"
#include "../../support/scopeexit.h"
#include "../../support/testsandbox.h"
#include "querymanager.h"
#include "sessionmanager.h"

namespace {

// Drives QueryManager on a dedicated worker thread (its required threading
// model) against the sandboxed AppData media.db, mirroring the pattern in
// test_querymanager_cancel_scan.cpp. Each fixture takes a unique connection
// name so the suite's tests never collide on Qt's named-connection registry.
class FtsFixture {
public:
  explicit FtsFixture(const QString &connectionName) {
    // Each test wants a virgin database file; the sandbox dir persists for
    // the whole suite (and across reruns), so wipe the db + WAL sidecars.
    const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dbDir);
    m_dbPath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
    QFile::remove(m_dbPath);
    QFile::remove(m_dbPath + QStringLiteral("-wal"));
    QFile::remove(m_dbPath + QStringLiteral("-shm"));

    m_qm = new QueryManager(&m_sessionManager, connectionName);
    m_qm->moveToThread(&m_worker);
    m_worker.start();
  }

  ~FtsFixture() {
    if (m_qm) {
      m_qm->deleteLater();
    }
    if (m_worker.isRunning()) {
      m_worker.quit();
      if (!m_worker.wait(10'000)) {
        m_worker.terminate();
        m_worker.wait(10'000);
      }
    }
  }
  FtsFixture(const FtsFixture &) = delete;
  FtsFixture &operator=(const FtsFixture &) = delete;

  [[nodiscard]] const QString &dbPath() const { return m_dbPath; }

  // Production launch step 1: open + migrate the schema on the worker.
  void initDatabase() {
    QMetaObject::invokeMethod(m_qm, &QueryManager::initDatabase, Qt::BlockingQueuedConnection);
  }

  // Production launch step 2 (DatabaseManager's startup single-shot): the FTS
  // readiness/rebuild slot, on the scan worker.
  void ensureItemsFtsReady() {
    QMetaObject::invokeMethod(m_qm, &QueryManager::ensureItemsFtsReady,
                              Qt::BlockingQueuedConnection);
  }

private:
  SessionManager m_sessionManager;
  QString m_dbPath;
  QueryManager *m_qm = nullptr;
  QThread m_worker;
};

// The legacy (pre-migration) base tables, verbatim from
// QueryManager::initDatabase. Creating these + rows BEFORE the QueryManager
// ever touches the file simulates an existing install upgrading through the
// migration ladder (user_version 0 -> current).
bool createLegacyTables(QSqlDatabase db) {
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS collections ("
                             "id INTEGER PRIMARY KEY, "
                             "name TEXT NOT NULL, "
                             "last_scanned TEXT NOT NULL, "
                             "ext_signature TEXT DEFAULT '', "
                             "uuid TEXT DEFAULT ''"
                             ")"))) {
    return false;
  }
  return q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS items ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "collection_id INTEGER NOT NULL, "
                               "path TEXT NOT NULL, "
                               "name TEXT NOT NULL, "
                               "artwork_path TEXT, "
                               "last_modified TEXT NOT NULL, "
                               "file_size INTEGER DEFAULT 0, "
                               "play_count INTEGER DEFAULT 0, "
                               "last_played TEXT, "
                               "rating INTEGER DEFAULT 0, "
                               "collection_uuid TEXT DEFAULT '', "
                               "UNIQUE(collection_id, path), "
                               "FOREIGN KEY(collection_id) REFERENCES collections(id) "
                               "ON DELETE CASCADE"
                               ")"));
}

bool insertCollection(QSqlDatabase db, const QString &uuid) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT INTO collections (id, name, last_scanned, ext_signature, uuid) "
                           "VALUES (1, 'FtsCollection', '2026-01-01T00:00:00', '', ?)"));
  q.addBindValue(uuid);
  return q.exec();
}

bool insertItem(QSqlDatabase db, const QString &uuid, const QString &path, const QString &name) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT INTO items (collection_id, collection_uuid, path, name, "
                           "last_modified) VALUES (1, ?, ?, ?, '2026-01-01T00:00:00')"));
  q.addBindValue(uuid);
  q.addBindValue(path);
  q.addBindValue(name);
  return q.exec();
}

int matchCount(QSqlDatabase db, const QString &term) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT COUNT(*) FROM items_fts WHERE items_fts MATCH ?"));
  q.addBindValue(term);
  if (!q.exec() || !q.next()) return -1;
  return q.value(0).toInt();
}

// FTS5's own external-content consistency audit: errors with SQLITE_CORRUPT
// when the index postings disagree with the items content table.
bool ftsIntegrityCheckPasses(QSqlDatabase db, QString *errorOut = nullptr) {
  QSqlQuery q(db);
  const bool ok = q.exec(QStringLiteral("INSERT INTO items_fts(items_fts) "
                                        "VALUES('integrity-check')"));
  if (!ok && errorOut != nullptr) {
    *errorOut = q.lastError().text();
  }
  return ok;
}

int ftsTriggerCount(QSqlDatabase db) {
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='trigger' AND name IN "
                             "('items_fts_ai', 'items_fts_ad', 'items_fts_au')")) ||
      !q.next()) {
    return -1;
  }
  return q.value(0).toInt();
}

QString metaValue(QSqlDatabase db, const QString &key) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
  q.addBindValue(key);
  if (!q.exec() || !q.next()) return QString();
  return q.value(0).toString();
}

} // namespace

class TestQueryManagerFtsBackfill : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void upgradeInstall_upsertBeforeRebuild_doesNotCorruptFts();
  void freshInstall_scanAfterReady_staysConsistentAcrossLaunches();
  void legacyBackfillCompletedInstall_selfHealsViaRebuild();
};

void TestQueryManagerFtsBackfill::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-fts-backfill"));
}

// THE Kartend-4i5e4 repro. An existing install (items rows predate the v3
// migration) upgrades: the FTS table is created empty, and before the index
// has been populated a rescan upsert UPDATEs one of the un-indexed rows.
// With the old trigger-from-migration scheme the items_fts_au trigger issued
// an FTS 'delete' for postings that were never written — SQLite detects this
// as corruption and FAILS the items UPDATE itself ("database disk image is
// malformed"), breaking scan writes. With the fix, no sync trigger exists
// until the one-shot rebuild has run, so the pre-rebuild UPDATE is plain SQL
// and the rebuild then captures the final row state.
void TestQueryManagerFtsBackfill::upgradeInstall_upsertBeforeRebuild_doesNotCorruptFts() {
  FtsFixture fx(QStringLiteral("fts_qm_upgrade"));

  const QString uuid = QStringLiteral("uuid-upgrade");
  {
    KartendTest::InspectorDb seed(fx.dbPath(), QStringLiteral("fts_seed_upgrade"));
    QVERIFY2(seed.isOpen(), "failed to open seed connection");
    QVERIFY(createLegacyTables(seed.db()));
    QVERIFY(insertCollection(seed.db(), uuid));
    QVERIFY(insertItem(seed.db(), uuid, QStringLiteral("/m/a.bin"), QStringLiteral("alphaone")));
    QVERIFY(insertItem(seed.db(), uuid, QStringLiteral("/m/b.bin"), QStringLiteral("betatwo")));
    QVERIFY(insertItem(seed.db(), uuid, QStringLiteral("/m/c.bin"), QStringLiteral("gammathree")));
  }

  // Launch: open + migrate (v3 creates items_fts; the index is still empty).
  fx.initDatabase();

  KartendTest::InspectorDb inspector(fx.dbPath(), QStringLiteral("fts_inspect_upgrade"));
  QVERIFY2(inspector.isOpen(), "failed to open inspector connection");
  QSqlDatabase db = inspector.db();

  // A rescan upsert hits the DO UPDATE arm on a row the index has never seen.
  // This is the statement that failed with SQLITE_CORRUPT under the old
  // scheme — it must succeed.
  {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE items SET name = 'deltarenamed' WHERE id = 1"));
    QVERIFY2(q.exec(), qPrintable(QStringLiteral("rescan upsert UPDATE on a not-yet-indexed row "
                                                 "failed (Kartend-4i5e4 corruption): ") +
                                  q.lastError().text()));
  }
  // The prune arm of a rescan (DELETE) walks the same trigger path.
  {
    QSqlQuery q(db);
    QVERIFY2(q.exec(QStringLiteral("DELETE FROM items WHERE id = 3")),
             qPrintable(QStringLiteral("rescan prune DELETE on a not-yet-indexed row failed "
                                       "(Kartend-4i5e4 corruption): ") +
                        q.lastError().text()));
  }

  // Startup FTS maintenance runs (DatabaseManager's single-shot).
  fx.ensureItemsFtsReady();

  // The index must now reflect the post-upsert reality, exactly once each.
  QCOMPARE(matchCount(db, QStringLiteral("deltarenamed")), 1);
  QCOMPARE(matchCount(db, QStringLiteral("betatwo")), 1);
  QCOMPARE(matchCount(db, QStringLiteral("alphaone")), 0);
  QCOMPARE(matchCount(db, QStringLiteral("gammathree")), 0);

  QString integrityError;
  QVERIFY2(ftsIntegrityCheckPasses(db, &integrityError),
           qPrintable(QStringLiteral("items_fts integrity-check failed: ") + integrityError));

  // Trigger maintenance is live from here on: a post-ready UPDATE and DELETE
  // must keep the index in sync.
  QCOMPARE(ftsTriggerCount(db), 3);
  {
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("UPDATE items SET name = 'epsilonfinal' WHERE id = 2")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items WHERE id = 1")));
  }
  QCOMPARE(matchCount(db, QStringLiteral("epsilonfinal")), 1);
  QCOMPARE(matchCount(db, QStringLiteral("betatwo")), 0);
  QCOMPARE(matchCount(db, QStringLiteral("deltarenamed")), 0);
  QVERIFY2(ftsIntegrityCheckPasses(db, &integrityError),
           qPrintable(QStringLiteral("items_fts integrity-check failed after post-ready "
                                     "trigger maintenance: ") +
                      integrityError));
}

// Fresh-install flow across two launches: launch 1 readies the (empty) index
// before any scan, the scan then inserts rows, launch 2's readiness pass must
// be a no-op. Every row indexed exactly once, index consistent. (The original
// report suspected duplicate postings here; that did not reproduce — FTS5 is
// last-write-wins per rowid — but this pins the single-match contract.)
void TestQueryManagerFtsBackfill::freshInstall_scanAfterReady_staysConsistentAcrossLaunches() {
  FtsFixture fx(QStringLiteral("fts_qm_fresh"));

  // Launch 1: migrate, then the startup readiness pass (items table empty).
  fx.initDatabase();
  fx.ensureItemsFtsReady();

  KartendTest::InspectorDb inspector(fx.dbPath(), QStringLiteral("fts_inspect_fresh"));
  QVERIFY2(inspector.isOpen(), "failed to open inspector connection");
  QSqlDatabase db = inspector.db();

  // The first scan persists rows ("media" is a shared second token so a
  // cross-row MATCH can count total postings).
  const QString uuid = QStringLiteral("uuid-fresh");
  QVERIFY(insertCollection(db, uuid));
  QVERIFY(insertItem(db, uuid, QStringLiteral("/m/a.bin"), QStringLiteral("alphaone media")));
  QVERIFY(insertItem(db, uuid, QStringLiteral("/m/b.bin"), QStringLiteral("betatwo media")));
  QVERIFY(insertItem(db, uuid, QStringLiteral("/m/c.bin"), QStringLiteral("gammathree media")));

  // Launch 2: the readiness pass runs again over the scan-populated table.
  fx.ensureItemsFtsReady();

  QCOMPARE(matchCount(db, QStringLiteral("alphaone")), 1);
  QCOMPARE(matchCount(db, QStringLiteral("betatwo")), 1);
  QCOMPARE(matchCount(db, QStringLiteral("gammathree")), 1);
  QCOMPARE(matchCount(db, QStringLiteral("media")), 3);

  QString integrityError;
  QVERIFY2(ftsIntegrityCheckPasses(db, &integrityError),
           qPrintable(QStringLiteral("items_fts integrity-check failed: ") + integrityError));
  QCOMPARE(ftsTriggerCount(db), 3);
}

// An install that completed the OLD incremental backfill carries
// items_fts_ready = '1' — and possibly an index damaged by the
// delete-unindexed-row interplay on whatever SQLite version wrote it. The
// readiness generation was bumped so '1' is treated as not-ready: the next
// launch must run one self-healing rebuild and stamp the new generation.
void TestQueryManagerFtsBackfill::legacyBackfillCompletedInstall_selfHealsViaRebuild() {
  FtsFixture fx(QStringLiteral("fts_qm_legacy"));

  fx.initDatabase();

  KartendTest::InspectorDb inspector(fx.dbPath(), QStringLiteral("fts_inspect_legacy"));
  QVERIFY2(inspector.isOpen(), "failed to open inspector connection");
  QSqlDatabase db = inspector.db();

  const QString uuid = QStringLiteral("uuid-legacy");
  QVERIFY(insertCollection(db, uuid));
  QVERIFY(insertItem(db, uuid, QStringLiteral("/m/a.bin"), QStringLiteral("alphaone")));

  // Simulate the legacy completed-backfill marker (plus its watermark key).
  {
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) "
                                  "VALUES('items_fts_ready', '1')")));
    QVERIFY(q.exec(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) "
                                  "VALUES('items_fts_indexed_up_to_id', '1')")));
  }

  fx.ensureItemsFtsReady();

  // The rebuild ran: generation stamped, index consistent, triggers in place.
  QCOMPARE(metaValue(db, QStringLiteral("items_fts_ready")), QStringLiteral("2"));
  QCOMPARE(matchCount(db, QStringLiteral("alphaone")), 1);
  QString integrityError;
  QVERIFY2(ftsIntegrityCheckPasses(db, &integrityError),
           qPrintable(QStringLiteral("items_fts integrity-check failed: ") + integrityError));
  QCOMPARE(ftsTriggerCount(db), 3);
}

QTEST_MAIN(TestQueryManagerFtsBackfill)

#include "test_querymanager_fts_backfill.moc"
