// Contract tests for PreparedStatementCache (Kartend-de4ft): a cache hit
// hands back the SAME still-prepared statement after a finish(), positional
// rebinding produces fresh correct results, and a caller that binds values
// but bails out before exec() cannot poison the next use of the statement.
// Real SQLite per the no-DB-mocking rule.

#include "preparedstatementcache.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTest>

class TestPreparedStatementCache : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void hitReturnsSameStatementAndRebindsCleanly();
  void earlyReturnWithoutExecDoesNotPoisonNextUse();
  void finishAllReleasesOpenCursors();

private:
  QSqlDatabase m_db;
};

namespace {
// QString (not constexpr char*) so the get() calls below don't re-convert
// per use — and QStringLiteral only accepts actual literals anyway.
const QString SELECT_BY_ID = QStringLiteral("SELECT name FROM t WHERE id = ?");
const QString SELECT_PAIR = QStringLiteral("SELECT name FROM t WHERE id = ? OR id = ? ORDER BY id");
} // namespace

void TestPreparedStatementCache::initTestCase() {
  m_db =
      QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("psc_test_connection"));
  m_db.setDatabaseName(QStringLiteral(":memory:"));
  QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

  QSqlQuery ddl(m_db);
  QVERIFY(ddl.exec(QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)")));
  QVERIFY(ddl.exec(QStringLiteral("INSERT INTO t VALUES (1,'alpha'),(2,'beta'),(3,'gamma')")));
}

void TestPreparedStatementCache::cleanupTestCase() {
  m_db.close();
  m_db = QSqlDatabase(); // drop the handle before removeDatabase
  QSqlDatabase::removeDatabase(QStringLiteral("psc_test_connection"));
}

void TestPreparedStatementCache::hitReturnsSameStatementAndRebindsCleanly() {
  PreparedStatementCache cache;
  cache.setDatabase(m_db);

  QSqlQuery &first = cache.get(SELECT_BY_ID);
  first.bindValue(0, 1);
  QVERIFY2(first.exec(), qPrintable(first.lastError().text()));
  QVERIFY(first.next());
  QCOMPARE(first.value(0).toString(), QStringLiteral("alpha"));

  // Hit: same underlying statement object, previous cursor released, and a
  // positional rebind yields the new row — not a stale one.
  QSqlQuery &second = cache.get(SELECT_BY_ID);
  QCOMPARE(&second, &first);
  second.bindValue(0, 3);
  QVERIFY2(second.exec(), qPrintable(second.lastError().text()));
  QVERIFY(second.next());
  QCOMPARE(second.value(0).toString(), QStringLiteral("gamma"));
}

void TestPreparedStatementCache::earlyReturnWithoutExecDoesNotPoisonNextUse() {
  PreparedStatementCache cache;
  cache.setDatabase(m_db);

  // Simulate a caller that binds and bails before exec() — the historical
  // trigger for "Parameter count mismatch" under addBindValue's append
  // semantics. With the positional-bind contract the next use overwrites
  // both slots and must succeed.
  QSqlQuery &abandoned = cache.get(SELECT_PAIR);
  abandoned.bindValue(0, 1);
  abandoned.bindValue(1, 2);
  // ... no exec() ...

  QSqlQuery &reused = cache.get(SELECT_PAIR);
  reused.bindValue(0, 2);
  reused.bindValue(1, 3);
  QVERIFY2(reused.exec(), qPrintable(reused.lastError().text()));
  QVERIFY(reused.next());
  QCOMPARE(reused.value(0).toString(), QStringLiteral("beta"));
  QVERIFY(reused.next());
  QCOMPARE(reused.value(0).toString(), QStringLiteral("gamma"));
  QVERIFY(!reused.next());
}

void TestPreparedStatementCache::finishAllReleasesOpenCursors() {
  PreparedStatementCache cache;
  cache.setDatabase(m_db);

  // Leave a cursor mid-resultset: exec + one next() keeps the statement
  // active, which holds the connection's implicit read transaction open
  // (the WAL-snapshot pin refreshWalView exists to break).
  QSqlQuery &open = cache.get(SELECT_PAIR);
  open.bindValue(0, 1);
  open.bindValue(1, 2);
  QVERIFY2(open.exec(), qPrintable(open.lastError().text()));
  QVERIFY(open.next());
  QVERIFY(open.isActive());

  cache.finishAll();
  QVERIFY(!open.isActive());

  // The compiled statement survives finishAll: rebind + exec still works.
  QSqlQuery &again = cache.get(SELECT_PAIR);
  QCOMPARE(&again, &open);
  again.bindValue(0, 3);
  again.bindValue(1, 3);
  QVERIFY2(again.exec(), qPrintable(again.lastError().text()));
  QVERIFY(again.next());
  QCOMPARE(again.value(0).toString(), QStringLiteral("gamma"));
}

QTEST_MAIN(TestPreparedStatementCache)
#include "test_preparedstatementcache.moc"
