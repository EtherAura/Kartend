// Tests for DatAuditProfile CRUD (schema v17 dat_audit_profile* tables).
//
// Uses an in-memory SQLite database seeded with the minimal base schema, then
// run through the real DbMigrations so the v17 tables are created exactly as
// in production. No DB mocking.

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

#include "datauditprofile.h"
#include "dbmigrations.h"
#include "errorutils.h"

using DatAuditProfile::DatRef;
using DatAuditProfile::FixMode;
using DatAuditProfile::MergeMode;
using DatAuditProfile::Profile;

namespace {

auto openMemoryDb(const QString &connName) -> QSqlDatabase {
  auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
  db.setDatabaseName(QStringLiteral(":memory:"));
  if (!db.open()) {
    qWarning("Failed to open in-memory db: %s", qPrintable(db.lastError().text()));
  }
  return db;
}

auto createBaseSchema(QSqlDatabase &db) -> void {
  QSqlQuery q(db);
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, "
                                "path TEXT, last_modified TEXT)")));
}

} // namespace

class TestDatAuditProfile : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void insertAndLoadRoundTrip();
  void loadMissingReturnsEmptyOptional();
  void listAllOrdersByName();
  void updateRewritesFieldsAndDats();
  void removeDeletesProfileAndChildren();
  void touchLastScanStampsTime();
  void insertRejectsEmptyName();
  void enumStringMappingsRoundTrip();

private:
  QSqlDatabase m_db;
  QString m_conn;
  static int s_counter;

  static Profile sampleProfile();
};

int TestDatAuditProfile::s_counter = 0;

Profile TestDatAuditProfile::sampleProfile() {
  Profile p;
  p.name = QStringLiteral("My Audit");
  p.collectionUuid = QStringLiteral("uuid-123");
  p.scanRoots = {QStringLiteral("/roms/a"), QStringLiteral("/roms/b")};
  p.mergeMode = MergeMode::NonMerged;
  p.regionPrefs = {QStringLiteral("USA"), QStringLiteral("World")};
  p.onePerGame = true;
  p.ignoreRules = {QStringLiteral("*.txt")};
  p.fixMode = FixMode::ManagedOutput;
  p.managedOutputRoot = QStringLiteral("/sorted");
  DatRef d1;
  d1.path = QStringLiteral("/dats/nointro.dat");
  d1.mtimeMs = 111;
  d1.dialect = 1;
  d1.recordCount = 5000;
  DatRef d2;
  d2.path = QStringLiteral("/dats/mame.xml");
  d2.mtimeMs = 222;
  d2.dialect = 2;
  d2.recordCount = 250000;
  p.dats = {d1, d2};
  return p;
}

void TestDatAuditProfile::init() {
  m_conn = QStringLiteral("test_datauditprofile_%1").arg(s_counter++);
  m_db = openMemoryDb(m_conn);
  QVERIFY(m_db.isOpen());
  createBaseSchema(m_db);
  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("test_datauditprofile"));
}

void TestDatAuditProfile::cleanup() {
  m_db.close();
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_conn);
}

void TestDatAuditProfile::insertAndLoadRoundTrip() {
  const Profile in = sampleProfile();
  auto ins = DatAuditProfile::insert(m_db, in);
  QVERIFY2(ins.isOk(), qPrintable(ins.isError() ? ins.error().message : QString()));
  const qint64 id = ins.value();
  QVERIFY(id > 0);

  auto loaded = DatAuditProfile::load(m_db, id);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  const Profile &out = *loaded.value();

  QCOMPARE(out.id, id);
  QCOMPARE(out.name, in.name);
  QCOMPARE(out.collectionUuid, in.collectionUuid);
  QCOMPARE(out.scanRoots, in.scanRoots);
  QCOMPARE(out.mergeMode, in.mergeMode);
  QCOMPARE(out.regionPrefs, in.regionPrefs);
  QCOMPARE(out.onePerGame, in.onePerGame);
  QCOMPARE(out.ignoreRules, in.ignoreRules);
  QCOMPARE(out.fixMode, in.fixMode);
  QCOMPARE(out.managedOutputRoot, in.managedOutputRoot);
  QCOMPARE(out.dats.size(), 2);
  // Order preserved by the position column.
  QCOMPARE(out.dats.at(0).path, in.dats.at(0).path);
  QCOMPARE(out.dats.at(0).recordCount, in.dats.at(0).recordCount);
  QCOMPARE(out.dats.at(1).path, in.dats.at(1).path);
  QCOMPARE(out.dats.at(1).dialect, in.dats.at(1).dialect);
  QVERIFY(out.createdAtMs > 0);
  QVERIFY(out.updatedAtMs > 0);
}

void TestDatAuditProfile::loadMissingReturnsEmptyOptional() {
  auto loaded = DatAuditProfile::load(m_db, 999999);
  QVERIFY(loaded.isOk());
  QVERIFY(!loaded.value().has_value());
}

void TestDatAuditProfile::listAllOrdersByName() {
  Profile a = sampleProfile();
  a.name = QStringLiteral("Zebra");
  Profile b = sampleProfile();
  b.name = QStringLiteral("Alpha");
  QVERIFY(DatAuditProfile::insert(m_db, a).isOk());
  QVERIFY(DatAuditProfile::insert(m_db, b).isOk());

  auto all = DatAuditProfile::listAll(m_db);
  QVERIFY(all.isOk());
  QCOMPARE(all.value().size(), 2);
  QCOMPARE(all.value().at(0).name, QStringLiteral("Alpha"));
  QCOMPARE(all.value().at(1).name, QStringLiteral("Zebra"));
  QCOMPARE(all.value().at(0).dats.size(), 2);
}

void TestDatAuditProfile::updateRewritesFieldsAndDats() {
  auto ins = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  Profile edit = sampleProfile();
  edit.id = id;
  edit.name = QStringLiteral("Renamed");
  edit.fixMode = FixMode::InPlace;
  edit.onePerGame = false;
  DatRef only;
  only.path = QStringLiteral("/dats/single.dat");
  only.recordCount = 42;
  edit.dats = {only};

  auto upd = DatAuditProfile::update(m_db, edit);
  QVERIFY2(upd.isOk(), qPrintable(upd.isError() ? upd.error().message : QString()));

  auto loaded = DatAuditProfile::load(m_db, id);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  const Profile &out = *loaded.value();
  QCOMPARE(out.name, QStringLiteral("Renamed"));
  QCOMPARE(out.fixMode, FixMode::InPlace);
  QCOMPARE(out.onePerGame, false);
  QCOMPARE(out.dats.size(), 1);
  QCOMPARE(out.dats.at(0).path, QStringLiteral("/dats/single.dat"));
}

void TestDatAuditProfile::removeDeletesProfileAndChildren() {
  auto ins = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  QVERIFY(DatAuditProfile::remove(m_db, id).isOk());

  auto loaded = DatAuditProfile::load(m_db, id);
  QVERIFY(loaded.isOk());
  QVERIFY(!loaded.value().has_value());

  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("SELECT COUNT(*) FROM dat_audit_profile_dat WHERE profile_id = ?"));
  q.addBindValue(id);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestDatAuditProfile::touchLastScanStampsTime() {
  auto ins = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  QVERIFY(DatAuditProfile::touchLastScan(m_db, id, 1234567).isOk());
  auto loaded = DatAuditProfile::load(m_db, id);
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().has_value());
  QCOMPARE(loaded.value()->lastScanAtMs, qint64(1234567));
}

void TestDatAuditProfile::insertRejectsEmptyName() {
  Profile p = sampleProfile();
  p.name = QStringLiteral("   ");
  auto ins = DatAuditProfile::insert(m_db, p);
  QVERIFY(ins.isError());
  QVERIFY(ins.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
}

void TestDatAuditProfile::enumStringMappingsRoundTrip() {
  QCOMPARE(DatAuditProfile::fixModeFromString(DatAuditProfile::fixModeToString(FixMode::InPlace)),
           FixMode::InPlace);
  QCOMPARE(
      DatAuditProfile::fixModeFromString(DatAuditProfile::fixModeToString(FixMode::ManagedOutput)),
      FixMode::ManagedOutput);
  QCOMPARE(
      DatAuditProfile::mergeModeFromString(DatAuditProfile::mergeModeToString(MergeMode::Split)),
      MergeMode::Split);
  QCOMPARE(
      DatAuditProfile::mergeModeFromString(DatAuditProfile::mergeModeToString(MergeMode::Merged)),
      MergeMode::Merged);
  QCOMPARE(DatAuditProfile::mergeModeFromString(
               DatAuditProfile::mergeModeToString(MergeMode::NonMerged)),
           MergeMode::NonMerged);
  // Unknown strings fall back to the safe defaults.
  QCOMPARE(DatAuditProfile::fixModeFromString(QStringLiteral("garbage")), FixMode::InPlace);
  QCOMPARE(DatAuditProfile::mergeModeFromString(QStringLiteral("garbage")), MergeMode::Split);
}

QTEST_MAIN(TestDatAuditProfile)
#include "test_datauditprofile.moc"
