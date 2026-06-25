// Real-SQLite test for DatAuditDownloadService (Kartend-ahf3d stage 1). The
// download / provenance / update-check orchestration was extracted off
// DatAuditDialog so it is testable without instantiating the QDialog. The
// network-bound paths (performDownload / checkUpdates) are runtime-gated and
// verified manually; this covers the provenance seam — recordProvenance's
// Outcome→Provenance mapping and trackedProvenance — against a real in-memory
// SQLite DB through the injected accessor (the same harness the other
// src/utils/db stores use). No DB mocking.

#include <algorithm>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTest>

#include "datauditdownloadservice.h"
#include "datlibrarystate.h"
#include "dbmigrations.h"

class TestDatAuditDownloadService : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void recordProvenanceMapsOutcomeAndPersists();
  void recordProvenanceEmptyDatPathsIsNoOp();
  void recordProvenanceToleratesNullAccessor();
  void trackedProvenanceReadsThroughAccessor();

private:
  /// The injected seam the dialog wires to withProfileDb — here it points at the
  /// test's real in-memory DB so the service round-trips through actual SQLite.
  [[nodiscard]] DatAuditDownloadService::ProvenanceAccess access() {
    return {[this] {
              auto r = DatLibraryState::loadAllProvenance(m_db);
              return r.isOk() ? r.value() : QList<DatLibraryState::Provenance>{};
            },
            [this](const DatLibraryState::Provenance &pr) {
              DatLibraryState::recordProvenance(m_db, pr);
            }};
  }

  QSqlDatabase m_db;
  QString m_conn;
  static int s_counter;
};

int TestDatAuditDownloadService::s_counter = 0;

void TestDatAuditDownloadService::init() {
  m_conn = QStringLiteral("test_datauditdownloadservice_%1").arg(s_counter++);
  m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
  m_db.setDatabaseName(QStringLiteral(":memory:"));
  QVERIFY(m_db.open());
  QSqlQuery q(m_db);
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, "
                                "path TEXT, last_modified TEXT)")));
  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("test_datauditdownloadservice"));
}

void TestDatAuditDownloadService::cleanup() {
  m_db.close();
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_conn);
}

void TestDatAuditDownloadService::recordProvenanceMapsOutcomeAndPersists() {
  DatAuditDownloadService svc(access());
  DatAuditDownloadService::Outcome o;
  o.ok = true;
  o.datPaths = {QStringLiteral("/lib/sys/a.dat"), QStringLiteral("/lib/sys/b.dat")};
  o.source = QStringLiteral("nointro");
  o.systemId = 42;
  o.version = QStringLiteral("2026-06-01");

  svc.recordProvenance(o);

  const auto loaded = svc.trackedProvenance();
  QCOMPARE(loaded.size(), 2);
  // canonicalFilePath() of a non-existent path is empty, so the service falls
  // back to the raw path — deterministic for these synthetic paths.
  QStringList paths;
  for (const auto &p : loaded) {
    paths << p.canonicalPath;
    QCOMPARE(p.source, QStringLiteral("nointro"));
    QCOMPARE(p.systemId, 42);
    QCOMPARE(p.version, QStringLiteral("2026-06-01"));
  }
  std::sort(paths.begin(), paths.end());
  QCOMPARE(paths,
           (QStringList{QStringLiteral("/lib/sys/a.dat"), QStringLiteral("/lib/sys/b.dat")}));
}

void TestDatAuditDownloadService::recordProvenanceEmptyDatPathsIsNoOp() {
  DatAuditDownloadService svc(access());
  DatAuditDownloadService::Outcome o;
  o.ok = true; // no datPaths — nothing to record
  svc.recordProvenance(o);
  QCOMPARE(svc.trackedProvenance().size(), 0);
}

void TestDatAuditDownloadService::recordProvenanceToleratesNullAccessor() {
  // A service constructed without accessors must not crash or touch a DB.
  DatAuditDownloadService svc(DatAuditDownloadService::ProvenanceAccess{});
  DatAuditDownloadService::Outcome o;
  o.datPaths = {QStringLiteral("/lib/x.dat")};
  o.source = QStringLiteral("redump");
  svc.recordProvenance(o);                     // no-op (null record accessor)
  QCOMPARE(svc.trackedProvenance().size(), 0); // null loadAll => empty
}

void TestDatAuditDownloadService::trackedProvenanceReadsThroughAccessor() {
  // Seed a row directly through DatLibraryState, then read it back via the
  // service's accessor to prove the load path is wired correctly.
  DatLibraryState::Provenance pr;
  pr.canonicalPath = QStringLiteral("/lib/redump/psx.dat");
  pr.source = QStringLiteral("redump");
  pr.slug = QStringLiteral("sony-playstation");
  pr.version = QStringLiteral("v123");
  QVERIFY(DatLibraryState::recordProvenance(m_db, pr).isOk());

  DatAuditDownloadService svc(access());
  const auto loaded = svc.trackedProvenance();
  QCOMPARE(loaded.size(), 1);
  QCOMPARE(loaded.first().slug, QStringLiteral("sony-playstation"));
  QCOMPARE(loaded.first().source, QStringLiteral("redump"));
}

QTEST_MAIN(TestDatAuditDownloadService)
#include "test_datauditdownloadservice.moc"
