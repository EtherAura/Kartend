/**
 * @file test_scrapenotfound.cpp
 * @brief ScrapeNotFoundStore row helper over scrape_not_found (Kartend-bb8df).
 *
 * Every case runs the production helper against a real, migrated, on-disk
 * media.db (KartendTest::MigratedDb, production bootstrap) and reads the
 * result back with plain SQL, so the stored text — not only the helper's own
 * view of it — is checked.
 */

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QTest>
#include <QTimeZone>

#include "../../support/migrateddb.h"
#include "scrapenotfound.h"

using ScrapeNotFoundStore::Stamp;

namespace {

const QString kUuid = QStringLiteral("uuid-a");
const QString kOtherUuid = QStringLiteral("uuid-b");
const QString kPath = QStringLiteral("/roms/snes/Unknown Hack (J).sfc");
const QString kOtherPath = QStringLiteral("/roms/snes/Another.sfc");
const QString kProvider = QStringLiteral("screenscraper");
const QString kOtherProvider = QStringLiteral("thegamesdb");
// Opaque on purpose: separators, whitespace, a newline and non-ASCII must all
// come back byte-for-byte.
const QString kKey = QStringLiteral("systemeid=4;crc=1A2B3C4D;md5=ab cd\nromnom=Ünknown.sfc");

/// A UTC instant with milliseconds, built without the Qt::TimeSpec
/// constructor: QTimeZone(0) is an offset zone, toUTC() turns it into Qt::UTC.
QDateTime utc(int h, int m, int s, int ms) {
  return QDateTime(QDate(2026, 11, 1), QTime(h, m, s, ms), QTimeZone(0)).toUTC();
}

Stamp makeStamp(const QString &uuid, const QString &path, const QString &provider,
                const QString &key, const QDateTime &at) {
  Stamp stamp;
  stamp.collectionUuid = uuid;
  stamp.path = path;
  stamp.provider = provider;
  stamp.lookupKey = key;
  stamp.missedAt = at;
  return stamp;
}

int scalar(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  return (q.exec(sql) && q.next()) ? q.value(0).toInt() : -1;
}

/// Every column of every row of @p table, ordered by id, one string per row.
QStringList dumpTable(QSqlDatabase &db, const QString &table) {
  QSqlQuery q(db);
  QStringList rows;
  if (!q.exec(QStringLiteral("SELECT * FROM %1 ORDER BY id").arg(table))) {
    rows << QStringLiteral("QUERY FAILED: ") + q.lastError().text();
    return rows;
  }
  while (q.next()) {
    QStringList cols;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) {
      cols << (q.isNull(i) ? QStringLiteral("<null>") : q.value(i).toString());
    }
    rows << cols.join(QLatin1Char('|'));
  }
  return rows;
}

/// The raw stored missed_at for one identity, or "<none>".
QString rawMissedAt(QSqlDatabase &db, const QString &uuid, const QString &path,
                    const QString &provider) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT missed_at FROM scrape_not_found "
                           "WHERE collection_uuid = ? AND path = ? AND provider = ?"));
  q.addBindValue(uuid);
  q.addBindValue(path);
  q.addBindValue(provider);
  if (!q.exec() || !q.next()) {
    return QStringLiteral("<none>");
  }
  return q.value(0).toString();
}

bool execSql(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  if (!q.exec(sql)) {
    qWarning() << "execSql failed:" << q.lastError().text() << sql;
    return false;
  }
  return true;
}

} // namespace

class TestScrapeNotFoundStore : public QObject {
  Q_OBJECT

private slots:
  void absentIsEmptyAndDistinctFromErrors();
  void recordPersistsExactFractionalUtc();
  void nonUtcOffsetStoresSameInstant();
  void reRecordUpdatesInPlace();
  void identitiesAreIndependent();
  void reopenPreservesFractionalValues();
  void removeIsScopedAndIdempotent();
  void leavesItemTablesAlone();
  void invalidRecordsWriteNothing();
};

void TestScrapeNotFoundStore::absentIsEmptyAndDistinctFromErrors() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  auto absent = ScrapeNotFoundStore::load(db, kUuid, kPath, kProvider);
  QVERIFY(absent.isOk());
  QVERIFY(!absent.value().has_value());

  // Closed connection to the same file: every call is an error, not "absent".
  {
    const QString conn = fixture.connectionName() + QStringLiteral("_closed");
    {
      QSqlDatabase closed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
      closed.setDatabaseName(fixture.databasePath());
      QVERIFY(!closed.isOpen());
      QVERIFY(ScrapeNotFoundStore::load(closed, kUuid, kPath, kProvider).isError());
      QVERIFY(ScrapeNotFoundStore::record(
                  closed, makeStamp(kUuid, kPath, kProvider, kKey, utc(10, 0, 0, 123)))
                  .isError());
      QVERIFY(ScrapeNotFoundStore::remove(closed, kUuid, kPath, kProvider).isError());
    }
    QSqlDatabase::removeDatabase(conn);
  }
  QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM scrape_not_found")), 0);

  // SQL failure on an open connection (table gone): error, not "absent".
  QVERIFY(execSql(db, QStringLiteral("DROP TABLE scrape_not_found")));
  QVERIFY(ScrapeNotFoundStore::load(db, kUuid, kPath, kProvider).isError());
  QVERIFY(
      ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, utc(10, 0, 0, 123)))
          .isError());
  QVERIFY(ScrapeNotFoundStore::remove(db, kUuid, kPath, kProvider).isError());
}

void TestScrapeNotFoundStore::recordPersistsExactFractionalUtc() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  const QDateTime at = utc(10, 0, 0, 123);
  QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, at)).isOk());

  QCOMPARE(rawMissedAt(db, kUuid, kPath, kProvider), QStringLiteral("2026-11-01T10:00:00.123Z"));
  QSqlQuery raw(db);
  QVERIFY(raw.exec(QStringLiteral("SELECT collection_uuid, path, provider, lookup_key "
                                  "FROM scrape_not_found")));
  QVERIFY(raw.next());
  QCOMPARE(raw.value(0).toString(), kUuid);
  QCOMPARE(raw.value(1).toString(), kPath);
  QCOMPARE(raw.value(2).toString(), kProvider);
  QCOMPARE(raw.value(3).toString(), kKey);
  QVERIFY(!raw.next());

  auto loaded = ScrapeNotFoundStore::load(db, kUuid, kPath, kProvider);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  const Stamp stamp = *loaded.value();
  QCOMPARE(stamp.collectionUuid, kUuid);
  QCOMPARE(stamp.path, kPath);
  QCOMPARE(stamp.provider, kProvider);
  QCOMPARE(stamp.lookupKey, kKey);
  QCOMPARE(stamp.missedAt.timeSpec(), Qt::UTC);
  QCOMPARE(stamp.missedAt.toMSecsSinceEpoch(), at.toMSecsSinceEpoch());
  QCOMPARE(stamp.missedAt.time().msec(), 123);
  QCOMPARE(stamp.missedAt, at);
}

void TestScrapeNotFoundStore::nonUtcOffsetStoresSameInstant() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  // 12:00:00.456 at UTC+02:00 is 10:00:00.456Z.
  const QDateTime offset(QDate(2026, 11, 1), QTime(12, 0, 0, 456), QTimeZone(2 * 3600));
  const QDateTime expected = utc(10, 0, 0, 456);
  QCOMPARE(offset.toMSecsSinceEpoch(), expected.toMSecsSinceEpoch());
  QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, offset)).isOk());

  QCOMPARE(rawMissedAt(db, kUuid, kPath, kProvider), QStringLiteral("2026-11-01T10:00:00.456Z"));
  auto loaded = ScrapeNotFoundStore::load(db, kUuid, kPath, kProvider);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->missedAt.timeSpec(), Qt::UTC);
  QCOMPARE(loaded.value()->missedAt.toMSecsSinceEpoch(), expected.toMSecsSinceEpoch());
  QCOMPARE(loaded.value()->missedAt, expected);
}

void TestScrapeNotFoundStore::reRecordUpdatesInPlace() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  QVERIFY(
      ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, utc(10, 0, 0, 123)))
          .isOk());
  const int id = scalar(db, QStringLiteral("SELECT id FROM scrape_not_found"));
  QVERIFY(id > 0);

  const QString newKey = QStringLiteral("systemeid=4;crc=FFFFFFFF;romnom=Renamed.sfc");
  const QDateTime later = utc(11, 30, 15, 789);
  QVERIFY(
      ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, newKey, later)).isOk());

  QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM scrape_not_found")), 1);
  QCOMPARE(scalar(db, QStringLiteral("SELECT id FROM scrape_not_found")), id);
  QCOMPARE(rawMissedAt(db, kUuid, kPath, kProvider), QStringLiteral("2026-11-01T11:30:15.789Z"));
  auto loaded = ScrapeNotFoundStore::load(db, kUuid, kPath, kProvider);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->lookupKey, newKey);
  QCOMPARE(loaded.value()->missedAt, later);
}

void TestScrapeNotFoundStore::identitiesAreIndependent() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  QVERIFY(
      ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, utc(10, 0, 0, 1)))
          .isOk());
  QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kOtherProvider,
                                                    QStringLiteral("tgdb-key"), utc(10, 0, 0, 2)))
              .isOk());
  QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kOtherUuid, kPath, kProvider,
                                                    QStringLiteral("other-coll"), utc(10, 0, 0, 3)))
              .isOk());
  QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kOtherPath, kProvider,
                                                    QStringLiteral("other-path"), utc(10, 0, 0, 4)))
              .isOk());
  QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM scrape_not_found")), 4);
  const QStringList before = dumpTable(db, QStringLiteral("scrape_not_found"));

  // Re-recording the base identity touches only its own row.
  QVERIFY(ScrapeNotFoundStore::record(
              db, makeStamp(kUuid, kPath, kProvider, QStringLiteral("changed"), utc(12, 0, 0, 999)))
              .isOk());
  const QStringList after = dumpTable(db, QStringLiteral("scrape_not_found"));
  QCOMPARE(after.size(), 4);
  QVERIFY(after.at(0) != before.at(0));
  QCOMPARE(after.mid(1), before.mid(1));

  auto other = ScrapeNotFoundStore::load(db, kUuid, kPath, kOtherProvider);
  QVERIFY(other.isOk());
  QVERIFY(other.value().has_value());
  QCOMPARE(other.value()->lookupKey, QStringLiteral("tgdb-key"));
  QCOMPARE(other.value()->missedAt, utc(10, 0, 0, 2));
  auto otherColl = ScrapeNotFoundStore::load(db, kOtherUuid, kPath, kProvider);
  QVERIFY(otherColl.isOk());
  QVERIFY(otherColl.value().has_value());
  QCOMPARE(otherColl.value()->missedAt, utc(10, 0, 0, 3));
  auto otherPath = ScrapeNotFoundStore::load(db, kUuid, kOtherPath, kProvider);
  QVERIFY(otherPath.isOk());
  QVERIFY(otherPath.value().has_value());
  QCOMPARE(otherPath.value()->missedAt, utc(10, 0, 0, 4));
}

void TestScrapeNotFoundStore::reopenPreservesFractionalValues() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  const QDateTime at = utc(23, 59, 59, 7);
  const QDateTime offsetAt(QDate(2026, 11, 1), QTime(1, 2, 3, 654), QTimeZone(-5 * 3600));
  {
    QSqlDatabase db = fixture.database();
    QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, at)).isOk());
    QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kOtherPath, kProvider, kKey, offsetAt))
                .isOk());
    db.close();
    QVERIFY(!db.isOpen());
  }

  // A brand-new connection to the same file.
  const QString conn = fixture.connectionName() + QStringLiteral("_reopen");
  {
    QSqlDatabase reopened = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    reopened.setDatabaseName(fixture.databasePath());
    QVERIFY(reopened.open());
    QCOMPARE(rawMissedAt(reopened, kUuid, kPath, kProvider),
             QStringLiteral("2026-11-01T23:59:59.007Z"));
    QCOMPARE(rawMissedAt(reopened, kUuid, kOtherPath, kProvider),
             QStringLiteral("2026-11-01T06:02:03.654Z"));
    auto loaded = ScrapeNotFoundStore::load(reopened, kUuid, kPath, kProvider);
    QVERIFY(loaded.isOk());
    QVERIFY(loaded.value().has_value());
    QCOMPARE(loaded.value()->lookupKey, kKey);
    QCOMPARE(loaded.value()->missedAt, at);
    auto loadedOffset = ScrapeNotFoundStore::load(reopened, kUuid, kOtherPath, kProvider);
    QVERIFY(loadedOffset.isOk());
    QVERIFY(loadedOffset.value().has_value());
    QCOMPARE(loadedOffset.value()->missedAt.toMSecsSinceEpoch(), offsetAt.toMSecsSinceEpoch());
    reopened.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

void TestScrapeNotFoundStore::removeIsScopedAndIdempotent() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  QVERIFY(
      ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, utc(10, 0, 0, 111)))
          .isOk());
  QVERIFY(ScrapeNotFoundStore::record(
              db, makeStamp(kUuid, kPath, kOtherProvider, kKey, utc(10, 0, 0, 222)))
              .isOk());
  QVERIFY(ScrapeNotFoundStore::record(
              db, makeStamp(kOtherUuid, kPath, kProvider, kKey, utc(10, 0, 0, 333)))
              .isOk());
  const QStringList before = dumpTable(db, QStringLiteral("scrape_not_found"));
  QCOMPARE(before.size(), 3);

  QVERIFY(ScrapeNotFoundStore::remove(db, kUuid, kPath, kProvider).isOk());
  const QStringList after = dumpTable(db, QStringLiteral("scrape_not_found"));
  QCOMPARE(after, before.mid(1));
  auto gone = ScrapeNotFoundStore::load(db, kUuid, kPath, kProvider);
  QVERIFY(gone.isOk());
  QVERIFY(!gone.value().has_value());

  // Repeat and never-recorded removals succeed and change nothing.
  QVERIFY(ScrapeNotFoundStore::remove(db, kUuid, kPath, kProvider).isOk());
  QVERIFY(ScrapeNotFoundStore::remove(db, kUuid, kOtherPath, kProvider).isOk());
  QCOMPARE(dumpTable(db, QStringLiteral("scrape_not_found")), after);
}

void TestScrapeNotFoundStore::leavesItemTablesAlone() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  // One scraped item (metadata + artwork rows) and one the provider did not
  // recognise: an items row and nothing else.
  QVERIFY(execSql(db, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                     "VALUES (1, 'SNES', '2026-11-01T00:00:00Z', 'uuid-a')")));
  QVERIFY(execSql(db, QStringLiteral("INSERT INTO items (collection_id, path, name, "
                                     "last_modified, collection_uuid) VALUES "
                                     "(1, '/roms/snes/Another.sfc', 'Another', "
                                     "'2026-11-01T00:00:00Z', 'uuid-a'), "
                                     "(1, '/roms/snes/Unknown Hack (J).sfc', 'Unknown Hack', "
                                     "'2026-11-01T00:00:00Z', 'uuid-a')")));
  QVERIFY(execSql(db, QStringLiteral("INSERT INTO item_metadata (collection_uuid, path, title) "
                                     "VALUES ('uuid-a', '/roms/snes/Another.sfc', 'Another')")));
  QVERIFY(execSql(db, QStringLiteral("INSERT INTO item_artwork (collection_uuid, path, "
                                     "artwork_type, manual_path, updated_at) VALUES "
                                     "('uuid-a', '/roms/snes/Another.sfc', 'front', "
                                     "'/art/another.png', '2026-11-01T00:00:00Z')")));
  const QStringList items = dumpTable(db, QStringLiteral("items"));
  const QStringList metadata = dumpTable(db, QStringLiteral("item_metadata"));
  const QStringList artwork = dumpTable(db, QStringLiteral("item_artwork"));
  QCOMPARE(items.size(), 2);
  QCOMPARE(metadata.size(), 1);
  QCOMPARE(artwork.size(), 1);

  QVERIFY(
      ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, utc(10, 0, 0, 123)))
          .isOk());
  QVERIFY(ScrapeNotFoundStore::load(db, kUuid, kPath, kProvider).isOk());
  QVERIFY(ScrapeNotFoundStore::record(
              db, makeStamp(kUuid, kPath, kProvider, QStringLiteral("k2"), utc(10, 0, 1, 456)))
              .isOk());
  QCOMPARE(dumpTable(db, QStringLiteral("items")), items);
  QCOMPARE(dumpTable(db, QStringLiteral("item_metadata")), metadata);
  QCOMPARE(dumpTable(db, QStringLiteral("item_artwork")), artwork);

  QVERIFY(ScrapeNotFoundStore::remove(db, kUuid, kPath, kProvider).isOk());
  QCOMPARE(dumpTable(db, QStringLiteral("items")), items);
  QCOMPARE(dumpTable(db, QStringLiteral("item_metadata")), metadata);
  QCOMPARE(dumpTable(db, QStringLiteral("item_artwork")), artwork);
  QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM item_metadata "
                                     "WHERE path = '/roms/snes/Unknown Hack (J).sfc'")),
           0);
}

void TestScrapeNotFoundStore::invalidRecordsWriteNothing() {
  KartendTest::MigratedDb fixture;
  QVERIFY(fixture.isOpen());
  QSqlDatabase db = fixture.database();

  const QDateTime at = utc(10, 0, 0, 123);
  QVERIFY(
      ScrapeNotFoundStore::record(db, makeStamp(kUuid, QString(), kProvider, kKey, at)).isError());
  QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, QString(), kKey, at)).isError());
  QVERIFY(ScrapeNotFoundStore::record(db, makeStamp(kUuid, kPath, kProvider, kKey, QDateTime()))
              .isError());
  QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM scrape_not_found")), 0);
}

QTEST_MAIN(TestScrapeNotFoundStore)
#include "test_scrapenotfound.moc"
