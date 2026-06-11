// KartendDb::DbTransaction contract tests against a real on-disk SQLite db
// (no-DB-mocking rule, Kartend-c0dwd). The guard's whole purpose is the
// nested-BEGIN footgun (Kartend-gv7f): SQLite has no nested transactions, so
// only the OUTERMOST guard may issue transaction()/commit()/rollback() while
// nested guards defer. These tests pin commit persistence, rollback-on-
// early-return, the nesting semantics via a shared depth counter, the
// depth-less always-outermost ctor, and the failed-BEGIN reporting path.

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "dbtxn.h"
#include "errorutils.h"

class TestDbTxn : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();

  void testCommitPersistsRows();
  void testDestructionWithoutCommitRollsBack();
  void testNestedGuardDefersToOuter_innerCommitIsNotReal();
  void testNestedGuardDestructionDoesNotRollBackOuter();
  void testDepthlessCtorIsAlwaysOutermost();
  void testFailedBeginReportsAndStaysInactive();
  void testCommitOnFailedBeginReturnsFalse();

private:
  int rowCount();

  std::unique_ptr<QTemporaryDir> m_dir;
  QString m_connName;
  QSqlDatabase m_db;
};

void TestDbTxn::init() {
  m_dir = std::make_unique<QTemporaryDir>();
  QVERIFY2(m_dir->isValid(), "Failed to create temporary directory");
  m_connName = QStringLiteral("test_dbtxn_%1").arg(QString::number(quintptr(this), 16));
  m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
  m_db.setDatabaseName(m_dir->filePath(QStringLiteral("txn.db")));
  QVERIFY(m_db.open());
  QSqlQuery q(m_db);
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE t (v TEXT)")));
}

void TestDbTxn::cleanup() {
  m_db.close();
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_connName);
  m_dir.reset();
}

int TestDbTxn::rowCount() {
  QSqlQuery q(m_db);
  return (q.exec(QStringLiteral("SELECT COUNT(*) FROM t")) && q.next()) ? q.value(0).toInt() : -1;
}

void TestDbTxn::testCommitPersistsRows() {
  int depth = 0;
  {
    KartendDb::DbTransaction txn(m_db, depth, "TestDbTxn");
    QVERIFY(txn.active());
    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES ('a')")));
    QVERIFY(txn.commit());
  }
  QCOMPARE(depth, 0); // dtor restored the shared counter
  QCOMPARE(rowCount(), 1);
}

void TestDbTxn::testDestructionWithoutCommitRollsBack() {
  int depth = 0;
  {
    KartendDb::DbTransaction txn(m_db, depth, "TestDbTxn");
    QVERIFY(txn.active());
    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES ('doomed')")));
    QCOMPARE(rowCount(), 1); // visible inside the transaction
    // No commit: simulates every early-return error path.
  }
  QCOMPARE(depth, 0);
  QCOMPARE(rowCount(), 0); // rolled back
}

void TestDbTxn::testNestedGuardDefersToOuter_innerCommitIsNotReal() {
  int depth = 0;
  {
    KartendDb::DbTransaction outer(m_db, depth, "TestDbTxn.outer");
    QVERIFY(outer.active());
    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES ('outer')")));
    {
      KartendDb::DbTransaction inner(m_db, depth, "TestDbTxn.inner");
      QVERIFY(inner.active()); // nested guards always report active
      QCOMPARE(depth, 2);
      QSqlQuery qi(m_db);
      QVERIFY(qi.exec(QStringLiteral("INSERT INTO t VALUES ('inner')")));
      QVERIFY(inner.commit()); // reports success but must NOT really commit
    }
    QCOMPARE(depth, 1);
    // Outer destructs WITHOUT commit — if the inner commit had been real,
    // the rows would survive; deferral means everything rolls back.
  }
  QCOMPARE(depth, 0);
  QCOMPARE(rowCount(), 0);
}

void TestDbTxn::testNestedGuardDestructionDoesNotRollBackOuter() {
  int depth = 0;
  {
    KartendDb::DbTransaction outer(m_db, depth, "TestDbTxn.outer");
    QVERIFY(outer.active());
    {
      KartendDb::DbTransaction inner(m_db, depth, "TestDbTxn.inner");
      QSqlQuery qi(m_db);
      QVERIFY(qi.exec(QStringLiteral("INSERT INTO t VALUES ('kept')")));
      // Inner destroyed without commit — an early return in a helper. The
      // OUTER transaction must survive it.
    }
    QVERIFY(outer.commit());
  }
  QCOMPARE(rowCount(), 1);
}

void TestDbTxn::testDepthlessCtorIsAlwaysOutermost() {
  // Two sequential depth-less guards on the same connection each own a real
  // transaction (internal zero counter): first commits, second rolls back.
  {
    KartendDb::DbTransaction txn(m_db, "TestDbTxn.depthless1");
    QVERIFY(txn.active());
    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES ('one')")));
    QVERIFY(txn.commit());
  }
  {
    KartendDb::DbTransaction txn(m_db, "TestDbTxn.depthless2");
    QVERIFY(txn.active());
    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO t VALUES ('two')")));
    // no commit
  }
  QCOMPARE(rowCount(), 1);
}

void TestDbTxn::testFailedBeginReportsAndStaysInactive() {
  // BEGIN on a closed database fails: active() is false, activeOrReport
  // returns false and feeds the sink exactly once.
  m_db.close();
  int depth = 0;
  int sunk = 0;
  ErrorUtils::ErrorContext seen;
  {
    KartendDb::DbTransaction txn(m_db, depth, "TestDbTxn.closed",
                                 [&](const ErrorUtils::ErrorContext &e) {
                                   ++sunk;
                                   seen = e;
                                 });
    QVERIFY(!txn.active());
    QVERIFY(!txn.activeOrReport(QStringLiteral("begin failed (expected)")));
    QCOMPARE(sunk, 1);
    QCOMPARE(seen.code, ErrorUtils::ErrorCode::DatabaseTransactionFailed);
  }
  QCOMPARE(depth, 0);
}

void TestDbTxn::testCommitOnFailedBeginReturnsFalse() {
  m_db.close();
  int depth = 0;
  KartendDb::DbTransaction txn(m_db, depth, "TestDbTxn.closed");
  QVERIFY(!txn.active());
  QVERIFY(!txn.commit());
  QVERIFY(!txn.commitOrReport(QStringLiteral("commit after failed begin (expected)")));
}

QTEST_GUILESS_MAIN(TestDbTxn)

#include "test_dbtxn.moc"
