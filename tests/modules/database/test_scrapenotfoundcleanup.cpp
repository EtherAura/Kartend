/**
 * @file test_scrapenotfoundcleanup.cpp
 * @brief Collection-removal cleanup of scrape_not_found rows (Kartend-bb8df).
 *
 * scrape_not_found (DB v30) is keyed by (collection_uuid, path, provider)
 * with no foreign key, so nothing removes a dropped collection's rows unless
 * the collection-removal purge (DatabaseManager::purgeOrphanCollectionData)
 * does. These tests drive that production purge against the real on-disk
 * media.db in a sandboxed AppDataLocation and inspect the result through a
 * second SQLite connection.
 */

#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "collection/collectioncontext.h"
#include "collection/typehelpers.h"
#include "databasemanager.h"
#include "sessionmanager.h"

namespace {
ApplicationContext makeCtxWithSession(SessionManager *session) {
  ApplicationContext ctx;
  ctx.managers.sessionManager = session;
  return ctx;
}

/// Second connection to DatabaseManager's media.db, for seeding and reading
/// back independently of the manager's own connections.
QSqlDatabase openInspector() {
  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbPath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  const QString conn = QStringLiteral("test_scrapenotfound_inspect");
  if (QSqlDatabase::contains(conn)) {
    QSqlDatabase::removeDatabase(conn);
  }
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
  db.setDatabaseName(dbPath);
  db.open();
  if (db.isOpen()) {
    // The purge runs on the DB worker connection; tolerate overlapping commits.
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
  }
  return db;
}

bool runSql(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  if (!q.exec(sql)) {
    qWarning() << "runSql failed:" << q.lastError().text() << "sql:" << sql.left(120);
    return false;
  }
  return true;
}

int scalar(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  return (q.exec(sql) && q.next()) ? q.value(0).toInt() : -1;
}

/// provider|lookup_key|missed_at for every stamp of @p uuid, ordered, so a
/// survivor's rows can be compared value-for-value before and after a purge.
QStringList stampsOf(QSqlDatabase &db, const QString &uuid) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT path, provider, lookup_key, missed_at FROM scrape_not_found "
                           "WHERE collection_uuid = ? ORDER BY path, provider"));
  q.addBindValue(uuid);
  QStringList out;
  if (q.exec()) {
    while (q.next()) {
      out << QStringLiteral("%1|%2|%3|%4")
                 .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                      q.value(3).toString());
    }
  }
  return out;
}

int countFor(QSqlDatabase &db, const char *table, const QString &uuid) {
  return scalar(db, QStringLiteral("SELECT COUNT(*) FROM %1 WHERE collection_uuid='%2'")
                        .arg(QLatin1String(table), uuid));
}

CollectionConfig makeCollection(const QString &name, const QString &mediaDir) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = mediaDir;
  return c;
}

QString uuidOf(const CollectionConfig &c) {
  return CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
}
} // namespace

class TestScrapeNotFoundCleanup : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void testCollectionRemoval_dropsOnlyRemovedCollectionsStamps();

private:
  std::unique_ptr<SessionManager> m_session;
};

void TestScrapeNotFoundCleanup::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scrapenotfoundcleanup"));
}

void TestScrapeNotFoundCleanup::testCollectionRemoval_dropsOnlyRemovedCollectionsStamps() {
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  const CollectionConfig removed =
      makeCollection(QStringLiteral("Removed"), QStringLiteral("/media/removed"));
  const CollectionConfig survivor =
      makeCollection(QStringLiteral("Survivor"), QStringLiteral("/media/survivor"));
  const CollectionConfig plain =
      makeCollection(QStringLiteral("Plain"), QStringLiteral("/media/plain"));
  const QString removedUuid = uuidOf(removed);
  const QString survivorUuid = uuidOf(survivor);
  const QString plainUuid = uuidOf(plain);

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  // The sandbox db can survive a previous run: start from empty tables and
  // clear the purge's live-set gate stamp so the first purge is not skipped.
  for (const char *t :
       {"scrape_not_found", "item_metadata", "item_artwork", "items", "collections"}) {
    QVERIFY(runSql(insp, QStringLiteral("DELETE FROM %1").arg(QLatin1String(t))));
  }
  QVERIFY(runSql(insp, "DELETE FROM meta WHERE key='orphan_purge_uuid_set_hash'"));

  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                      "VALUES (1, 'Removed', 'x', '%1'), "
                                      "(2, 'Survivor', 'x', '%2'), (3, 'Plain', 'x', '%3')")
                           .arg(removedUuid, survivorUuid, plainUuid)));
  // Both the removed collection and the survivor hold an item at the SAME
  // path, so a path-only delete would wrongly hit the survivor's stamp.
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO items (collection_id, path, name, "
                                      "last_modified, collection_uuid) VALUES "
                                      "(1, '/roms/shared.zip', 'shared', 'x', '%1'), "
                                      "(1, '/roms/hack.zip', 'hack', 'x', '%1'), "
                                      "(1, '/roms/found.zip', 'found', 'x', '%1'), "
                                      "(2, '/roms/shared.zip', 'shared', 'x', '%2'), "
                                      "(2, '/roms/known.zip', 'known', 'x', '%2'), "
                                      "(3, '/roms/plain.zip', 'plain', 'x', '%3')")
                           .arg(removedUuid, survivorUuid, plainUuid)));
  // Genuinely scraped items carry metadata/artwork; not-found items do not.
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO item_metadata (collection_uuid, path, title) "
                                      "VALUES ('%1', '/roms/found.zip', 'Found'), "
                                      "('%2', '/roms/known.zip', 'Known'), "
                                      "('%3', '/roms/plain.zip', 'Plain')")
                           .arg(removedUuid, survivorUuid, plainUuid)));
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO item_artwork (collection_uuid, path, "
                                      "artwork_type, manual_path, updated_at) VALUES "
                                      "('%1', '/roms/found.zip', 'cover', '/a.png', 'x'), "
                                      "('%2', '/roms/known.zip', 'cover', '/b.png', 'x')")
                           .arg(removedUuid, survivorUuid)));
  // Several providers' stamps in the removed collection; two in the survivor,
  // one of them on the shared path. Plain has none.
  QVERIFY(
      runSql(insp, QStringLiteral("INSERT INTO scrape_not_found (collection_uuid, path, provider, "
                                  "lookup_key, missed_at) VALUES "
                                  "('%1', '/roms/shared.zip', 'screenscraper', 'ss-r1', "
                                  "'2026-11-01T10:00:00Z'), "
                                  "('%1', '/roms/shared.zip', 'thegamesdb', 'tgdb-r1', "
                                  "'2026-11-01T10:00:01Z'), "
                                  "('%1', '/roms/hack.zip', 'screenscraper', 'ss-r2', "
                                  "'2026-11-01T10:00:02Z'), "
                                  "('%2', '/roms/shared.zip', 'screenscraper', 'ss-s1', "
                                  "'2026-11-02T09:30:00Z'), "
                                  "('%2', '/roms/shared.zip', 'igdb', 'igdb-s1', "
                                  "'2026-11-02T09:30:05Z')")
                       .arg(removedUuid, survivorUuid)));
  const QStringList survivorBefore = stampsOf(insp, survivorUuid);
  QCOMPARE(survivorBefore.size(), 2);
  QCOMPARE(countFor(insp, "scrape_not_found", removedUuid), 3);

  // Remove "Removed": the live set is what the app still has configured.
  db.purgeOrphanCollectionData({survivor, plain});

  // The purge is a queued worker write; wait for it to land.
  QTRY_COMPARE_WITH_TIMEOUT(countFor(insp, "items", removedUuid), 0, 10000);
  QCOMPARE(
      scalar(insp,
             QStringLiteral("SELECT COUNT(*) FROM collections WHERE uuid='%1'").arg(removedUuid)),
      0);
  QCOMPARE(countFor(insp, "item_metadata", removedUuid), 0);
  QCOMPARE(countFor(insp, "item_artwork", removedUuid), 0);
  // Every provider's stamp of the removed collection is gone...
  QCOMPARE(countFor(insp, "scrape_not_found", removedUuid), 0);
  // ...and the survivor's (including the overlapping path) are untouched.
  QCOMPARE(stampsOf(insp, survivorUuid), survivorBefore);
  QCOMPARE(countFor(insp, "items", survivorUuid), 2);
  QCOMPARE(countFor(insp, "item_metadata", survivorUuid), 1);
  QCOMPARE(countFor(insp, "item_artwork", survivorUuid), 1);
  QCOMPARE(countFor(insp, "items", plainUuid), 1);
  QCOMPARE(countFor(insp, "item_metadata", plainUuid), 1);
  // A not-found item never gained a metadata row.
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM item_metadata WHERE path='/roms/shared.zip'"), 0);

  // Removing a collection that has no stamps purges it normally and leaves
  // the survivor's stamps alone.
  db.purgeOrphanCollectionData({survivor});
  QTRY_COMPARE_WITH_TIMEOUT(countFor(insp, "items", plainUuid), 0, 10000);
  QCOMPARE(countFor(insp, "item_metadata", plainUuid), 0);
  QCOMPARE(
      scalar(insp,
             QStringLiteral("SELECT COUNT(*) FROM collections WHERE uuid='%1'").arg(plainUuid)),
      0);
  QCOMPARE(stampsOf(insp, survivorUuid), survivorBefore);

  // Repeating the removal with the same live set is gated off (existing
  // contract): nothing else changes. Fence on a write queued behind it.
  db.purgeOrphanCollectionData({survivor});
  db.recordItemLaunch(survivorUuid, QStringLiteral("/roms/known.zip"));
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, QStringLiteral("SELECT play_count FROM items WHERE collection_uuid='%1' "
                                  "AND path='/roms/known.zip'")
                       .arg(survivorUuid)),
      1, 10000);
  QCOMPARE(stampsOf(insp, survivorUuid), survivorBefore);
  QCOMPARE(countFor(insp, "items", survivorUuid), 2);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM scrape_not_found"), 2);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM item_metadata WHERE path='/roms/shared.zip'"), 0);
}

QTEST_MAIN(TestScrapeNotFoundCleanup)
#include "test_scrapenotfoundcleanup.moc"
