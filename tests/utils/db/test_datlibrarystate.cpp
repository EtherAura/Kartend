// Tests for DatLibraryState (schema v19 dat_library_dismissal table) —
// the "don't ask again" persistence behind the DAT-library review flow
// (Kartend-m6qsb.5). Same in-memory-DB + real-migrations harness as the
// other src/utils/db stores. No DB mocking.

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

#include "datlibrarystate.h"
#include "dbmigrations.h"

class TestDatLibraryState : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void dismissalRoundTripsThroughKeys();
  void reDismissalMovesTheRevisionForward();
  void rejectsEmptyPathAndClosedDb();

private:
  QSqlDatabase m_db;
  QString m_conn;
  static int s_counter;
};

int TestDatLibraryState::s_counter = 0;

void TestDatLibraryState::init() {
  m_conn = QStringLiteral("test_datlibrarystate_%1").arg(s_counter++);
  m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
  m_db.setDatabaseName(QStringLiteral(":memory:"));
  QVERIFY(m_db.open());
  QSqlQuery q(m_db);
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, "
                                "path TEXT, last_modified TEXT)")));
  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("test_datlibrarystate"));
}

void TestDatLibraryState::cleanup() {
  m_db.close();
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_conn);
}

void TestDatLibraryState::dismissalRoundTripsThroughKeys() {
  QVERIFY(DatLibraryState::addDismissal(m_db, QStringLiteral("/lib/a.dat"), 1000).isOk());
  QVERIFY(DatLibraryState::addDismissal(m_db, QStringLiteral("/lib/b.dat"), 2000).isOk());

  auto keys = DatLibraryState::loadDismissalKeys(m_db);
  QVERIFY(keys.isOk());
  QCOMPARE(keys.value().size(), 2);
  QVERIFY(keys.value().contains(DatLibraryState::dismissalKey(QStringLiteral("/lib/a.dat"), 1000)));
  QVERIFY(keys.value().contains(DatLibraryState::dismissalKey(QStringLiteral("/lib/b.dat"), 2000)));
  // A different mtime is a different key — the scanner proposes again.
  QVERIFY(
      !keys.value().contains(DatLibraryState::dismissalKey(QStringLiteral("/lib/a.dat"), 1001)));
}

void TestDatLibraryState::reDismissalMovesTheRevisionForward() {
  QVERIFY(DatLibraryState::addDismissal(m_db, QStringLiteral("/lib/a.dat"), 1000).isOk());
  // Path is the primary key: dismissing the updated revision replaces the
  // old row ("ask once per catalogue revision").
  QVERIFY(DatLibraryState::addDismissal(m_db, QStringLiteral("/lib/a.dat"), 5000).isOk());

  auto keys = DatLibraryState::loadDismissalKeys(m_db);
  QVERIFY(keys.isOk());
  QCOMPARE(keys.value().size(), 1);
  QVERIFY(keys.value().contains(DatLibraryState::dismissalKey(QStringLiteral("/lib/a.dat"), 5000)));
}

void TestDatLibraryState::rejectsEmptyPathAndClosedDb() {
  QVERIFY(DatLibraryState::addDismissal(m_db, QString(), 1).isError());
  QSqlDatabase closed;
  QVERIFY(DatLibraryState::addDismissal(closed, QStringLiteral("/x"), 1).isError());
  QVERIFY(DatLibraryState::loadDismissalKeys(closed).isError());
}

QTEST_MAIN(TestDatLibraryState)
#include "test_datlibrarystate.moc"
