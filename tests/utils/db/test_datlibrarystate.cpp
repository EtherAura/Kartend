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
  void provenanceRoundTripsAndReplaces();
  void loadAllProvenanceReturnsEveryRow();
  void isUpdateAvailableComparesVersions();
  void outdatedAmongSelectsNewerRevisions();

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

void TestDatLibraryState::provenanceRoundTripsAndReplaces() {
  DatLibraryState::Provenance p;
  p.canonicalPath = QStringLiteral("/lib/Sample (USA).dat");
  p.source = QStringLiteral("nointro");
  p.systemId = 64;
  p.version = QStringLiteral("2026-05-30");
  QVERIFY(DatLibraryState::recordProvenance(m_db, p).isOk());

  auto loaded = DatLibraryState::loadProvenance(m_db, p.canonicalPath);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->source, QStringLiteral("nointro"));
  QCOMPARE(loaded.value()->systemId, 64);
  QCOMPARE(loaded.value()->version, QStringLiteral("2026-05-30"));

  // OR REPLACE: re-recording the same path updates the stored version.
  p.version = QStringLiteral("2026-06-14");
  QVERIFY(DatLibraryState::recordProvenance(m_db, p).isOk());
  auto reloaded = DatLibraryState::loadProvenance(m_db, p.canonicalPath);
  QCOMPARE(reloaded.value()->version, QStringLiteral("2026-06-14"));

  // Unknown path -> nullopt, not an error.
  auto missing = DatLibraryState::loadProvenance(m_db, QStringLiteral("/lib/nope.dat"));
  QVERIFY(missing.isOk());
  QVERIFY(!missing.value().has_value());

  // Empty path / closed DB are errors.
  QVERIFY(DatLibraryState::recordProvenance(m_db, DatLibraryState::Provenance{}).isError());
}

void TestDatLibraryState::loadAllProvenanceReturnsEveryRow() {
  DatLibraryState::Provenance a;
  a.canonicalPath = QStringLiteral("/lib/a.dat");
  a.source = QStringLiteral("redump");
  a.slug = QStringLiteral("psx");
  a.version = QStringLiteral("v1");
  DatLibraryState::Provenance b;
  b.canonicalPath = QStringLiteral("/lib/b.dat");
  b.source = QStringLiteral("nointro");
  b.systemId = 1;
  b.version = QStringLiteral("2026-01-01");
  QVERIFY(DatLibraryState::recordProvenance(m_db, a).isOk());
  QVERIFY(DatLibraryState::recordProvenance(m_db, b).isOk());

  auto all = DatLibraryState::loadAllProvenance(m_db);
  QVERIFY(all.isOk());
  QCOMPARE(all.value().size(), 2);
}

void TestDatLibraryState::isUpdateAvailableComparesVersions() {
  QVERIFY(DatLibraryState::isUpdateAvailable(QStringLiteral("2026-05-30"),
                                             QStringLiteral("2026-06-14")));
  QVERIFY(!DatLibraryState::isUpdateAvailable(QStringLiteral("2026-05-30"),
                                              QStringLiteral("2026-05-30")));
  QVERIFY(!DatLibraryState::isUpdateAvailable(QStringLiteral(" v1 "), QStringLiteral("v1")));
  // Can't tell without both revisions.
  QVERIFY(!DatLibraryState::isUpdateAvailable(QString(), QStringLiteral("v2")));
  QVERIFY(!DatLibraryState::isUpdateAvailable(QStringLiteral("v1"), QString()));
}

void TestDatLibraryState::outdatedAmongSelectsNewerRevisions() {
  using DatLibraryState::Provenance;
  auto pv = [](const QString &path, const QString &source, const QString &ver) {
    Provenance p;
    p.canonicalPath = path;
    p.source = source;
    p.version = ver;
    return p;
  };
  const QList<Provenance> all{
      pv(QStringLiteral("/lib/a.dat"), QStringLiteral("nointro"), QStringLiteral("2026-05-30")),
      pv(QStringLiteral("/lib/b.dat"), QStringLiteral("redump"), QStringLiteral("v1")),
      pv(QStringLiteral("/lib/c.dat"), QStringLiteral(""), QStringLiteral("x")), // generic: skip
      pv(QStringLiteral("/lib/d.dat"), QStringLiteral("nointro"),
         QString())}; // no stored ver: skip

  // Stub fetcher: a.dat has a newer date, b.dat is unchanged, others wouldn't be queried.
  auto fetch = [](const Provenance &p) -> QString {
    if (p.canonicalPath == QStringLiteral("/lib/a.dat")) return QStringLiteral("2026-06-14");
    if (p.canonicalPath == QStringLiteral("/lib/b.dat")) return QStringLiteral("v1"); // same
    return QString();
  };

  const auto outdated = DatLibraryState::outdatedAmong(all, fetch);
  QCOMPARE(outdated.size(), 1);
  QCOMPARE(outdated.first().canonicalPath, QStringLiteral("/lib/a.dat"));
  QCOMPARE(outdated.first().version, QStringLiteral("2026-06-14")); // carries the new revision

  // A null fetcher yields nothing rather than crashing.
  QVERIFY(DatLibraryState::outdatedAmong(all, {}).isEmpty());
}

QTEST_MAIN(TestDatLibraryState)
#include "test_datlibrarystate.moc"
