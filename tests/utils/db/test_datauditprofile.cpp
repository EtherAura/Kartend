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
  void loadByCollectionUuidPicksMostRecentlyUpdated();
  void loadByCollectionUuidTieBreaksOnId();
  void loadByCollectionUuidEmptyWhenNoneLinked();
  void loadByCollectionUuidRejectsEmptyUuid();
  void replaceResultsWritesAndRewritesSnapshot();
  void loadResultSummaryCountsByStatus();
  void loadResultSummaryDistinguishesNeverFromEmpty();
  void rollupsGroupBySourceGameAndMia();

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
  p.regionPrefs = {QStringLiteral("USA"), QStringLiteral("World")};
  p.onePerGame = true;
  p.ignoreRules = {QStringLiteral("*.txt")};
  p.fixMode = FixMode::ManagedOutput;
  p.managedOutputRoot = QStringLiteral("/sorted");
  p.quarantineRoot = QStringLiteral("/quarantine");
  p.category = QStringLiteral("Retro");
  p.detectedLayout = QStringLiteral("archive_per_item");
  p.layoutConfirmed = true;
  p.mergeMode = QStringLiteral("merged"); // Kartend-m6qsb.29
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
  QCOMPARE(out.regionPrefs, in.regionPrefs);
  QCOMPARE(out.onePerGame, in.onePerGame);
  QCOMPARE(out.ignoreRules, in.ignoreRules);
  QCOMPARE(out.fixMode, in.fixMode);
  QCOMPARE(out.managedOutputRoot, in.managedOutputRoot);
  QCOMPARE(out.quarantineRoot, in.quarantineRoot);
  QCOMPARE(out.category, in.category);
  QCOMPARE(out.detectedLayout, in.detectedLayout);
  QCOMPARE(out.layoutConfirmed, in.layoutConfirmed);
  QCOMPARE(out.mergeMode, in.mergeMode);
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
  // Unknown strings fall back to the safe default.
  QCOMPARE(DatAuditProfile::fixModeFromString(QStringLiteral("garbage")), FixMode::InPlace);
}

void TestDatAuditProfile::loadByCollectionUuidPicksMostRecentlyUpdated() {
  Profile a = sampleProfile();
  a.name = QStringLiteral("Older");
  Profile b = sampleProfile();
  b.name = QStringLiteral("Newer");
  auto insA = DatAuditProfile::insert(m_db, a);
  auto insB = DatAuditProfile::insert(m_db, b);
  QVERIFY(insA.isOk());
  QVERIFY(insB.isOk());

  // Pin updated_at directly — both inserts land within the same millisecond
  // on a fast machine, so stamping through the public API would be flaky.
  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("UPDATE dat_audit_profile SET updated_at_unix_ms = ? WHERE id = ?"));
  q.bindValue(0, qint64(1000));
  q.bindValue(1, insB.value());
  QVERIFY(q.exec());
  q.bindValue(0, qint64(2000));
  q.bindValue(1, insA.value());
  QVERIFY(q.exec());

  auto linked = DatAuditProfile::loadByCollectionUuid(m_db, QStringLiteral("uuid-123"));
  QVERIFY(linked.isOk());
  QVERIFY(linked.value().has_value());
  QCOMPARE(linked.value()->id, insA.value());
  QCOMPARE(linked.value()->name, QStringLiteral("Older"));
  // DAT child rows come along, same as load().
  QCOMPARE(linked.value()->dats.size(), 2);
}

void TestDatAuditProfile::loadByCollectionUuidTieBreaksOnId() {
  auto insA = DatAuditProfile::insert(m_db, sampleProfile());
  auto insB = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(insA.isOk());
  QVERIFY(insB.isOk());

  QSqlQuery q(m_db);
  QVERIFY(q.exec(QStringLiteral("UPDATE dat_audit_profile SET updated_at_unix_ms = 5000")));

  auto linked = DatAuditProfile::loadByCollectionUuid(m_db, QStringLiteral("uuid-123"));
  QVERIFY(linked.isOk());
  QVERIFY(linked.value().has_value());
  QCOMPARE(linked.value()->id, insB.value());
}

void TestDatAuditProfile::loadByCollectionUuidEmptyWhenNoneLinked() {
  QVERIFY(DatAuditProfile::insert(m_db, sampleProfile()).isOk());
  auto linked = DatAuditProfile::loadByCollectionUuid(m_db, QStringLiteral("no-such-uuid"));
  QVERIFY(linked.isOk());
  QVERIFY(!linked.value().has_value());
}

void TestDatAuditProfile::loadByCollectionUuidRejectsEmptyUuid() {
  // '' means "not linked" in the schema — matching it would return an
  // arbitrary unlinked profile, so the store rejects it outright.
  Profile unlinked = sampleProfile();
  unlinked.collectionUuid.clear();
  QVERIFY(DatAuditProfile::insert(m_db, unlinked).isOk());
  auto linked = DatAuditProfile::loadByCollectionUuid(m_db, QString());
  QVERIFY(linked.isError());
  QVERIFY(linked.hasErrorCode(ErrorUtils::ErrorCode::InvalidArgument));
}

void TestDatAuditProfile::replaceResultsWritesAndRewritesSnapshot() {
  auto ins = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  QList<DatAuditProfile::ResultRow> first;
  DatAuditProfile::ResultRow a;
  a.entryKey = QStringLiteral("file:/media/clip-one.mkv");
  a.status = 0; // Have
  a.filePath = QStringLiteral("/media/clip-one.mkv");
  a.detail = QStringLiteral("Clip One.mkv");
  a.zipIndex = 4; // archive member index (Kartend-7iqhl.4)
  DatAuditProfile::ResultRow b;
  b.entryKey = QStringLiteral("entry:Clip Two.mkv");
  b.status = 6; // Missing
  first << a << b;
  QVERIFY(DatAuditProfile::replaceResults(m_db, id, first).isOk());

  QSqlQuery q(m_db);
  q.prepare(QStringLiteral("SELECT COUNT(*) FROM dat_audit_result WHERE profile_id = ?"));
  q.addBindValue(id);
  QVERIFY(q.exec() && q.next());
  QCOMPARE(q.value(0).toInt(), 2);

  // zip_index round-trips: the member row keeps its index, the entry-only row
  // keeps the -1 default (Kartend-7iqhl.4).
  auto loaded = DatAuditProfile::loadProfileResultRows(m_db, id);
  QVERIFY(loaded.isOk());
  int zipForA = -99;
  int zipForB = -99;
  for (const auto &r : loaded.value()) {
    if (r.entryKey == a.entryKey) zipForA = r.zipIndex;
    if (r.entryKey == b.entryKey) zipForB = r.zipIndex;
  }
  QCOMPARE(zipForA, 4);
  QCOMPARE(zipForB, -1);

  // A re-run is a full re-statement: the old snapshot must vanish wholesale,
  // not merge with the new rows.
  QList<DatAuditProfile::ResultRow> second{a};
  QVERIFY(DatAuditProfile::replaceResults(m_db, id, second).isOk());
  QVERIFY(q.exec() && q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  // Duplicate entry keys within one snapshot collapse (INSERT OR REPLACE)
  // instead of failing the whole write on the primary key.
  QList<DatAuditProfile::ResultRow> dup{a, a};
  QVERIFY(DatAuditProfile::replaceResults(m_db, id, dup).isOk());
  QVERIFY(q.exec() && q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  // Invalid profile id is rejected.
  QVERIFY(DatAuditProfile::replaceResults(m_db, -1, first).isError());
}

void TestDatAuditProfile::rollupsGroupBySourceGameAndMia() {
  auto ins = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  // Two sources; source A has a present + a missing-MIA rom in one game, source
  // B a present rom in another game (Kartend-34lab).
  const auto mk = [](const QString &key, int status, const QString &src, const QString &game,
                     bool mia) {
    DatAuditProfile::ResultRow r;
    r.entryKey = key;
    r.status = status;
    r.sourceName = src;
    r.gameName = game;
    r.mia = mia;
    return r;
  };
  QList<DatAuditProfile::ResultRow> rows;
  rows << mk(QStringLiteral("file:/a/ok.bin"), 0, QStringLiteral("A.dat"), QStringLiteral("Game A"),
             false) // Have
       << mk(QStringLiteral("entry:lost.bin"), 6, QStringLiteral("A.dat"), QStringLiteral("Game A"),
             true) // Missing+MIA
       << mk(QStringLiteral("file:/b/ok.bin"), 0, QStringLiteral("B.dat"), QStringLiteral("Game B"),
             false); // Have
  QVERIFY(DatAuditProfile::replaceResults(m_db, id, rows).isOk());

  auto all = DatAuditProfile::loadAllRollups(m_db);
  QVERIFY(all.isOk());
  int miaTotal = 0;
  int aHave = 0;
  for (const auto &r : all.value()) {
    if (r.profileId != id) continue;
    if (r.mia) miaTotal += r.count;
    if (r.sourceName == QLatin1String("A.dat") && r.status == 0) aHave += r.count;
  }
  QCOMPARE(miaTotal, 1);
  QCOMPARE(aHave, 1);

  // Game list for source A: one Have game-row + one Missing(MIA) game-row.
  auto gamesA = DatAuditProfile::loadGameRollups(m_db, id, QStringLiteral("A.dat"));
  QVERIFY(gamesA.isOk());
  int aRows = 0;
  bool sawMissingMia = false;
  for (const auto &g : gamesA.value()) {
    if (g.gameName == QLatin1String("Game A")) {
      ++aRows;
      if (g.status == 6 && g.mia) sawMissingMia = true;
    }
  }
  QCOMPARE(aRows, 2);
  QVERIFY(sawMissingMia);

  auto gamesB = DatAuditProfile::loadGameRollups(m_db, id, QStringLiteral("B.dat"));
  QVERIFY(gamesB.isOk());
  QCOMPARE(gamesB.value().size(), 1);

  // Kartend-m6qsb.30: the folder-as-item view loads every row of a source (all
  // games), to regroup them by item-folder client-side.
  auto srcA = DatAuditProfile::loadSourceResultRows(m_db, id, QStringLiteral("A.dat"));
  QVERIFY(srcA.isOk());
  QCOMPARE(srcA.value().size(), 2);
  auto srcB = DatAuditProfile::loadSourceResultRows(m_db, id, QStringLiteral("B.dat"));
  QVERIFY(srcB.isOk());
  QCOMPARE(srcB.value().size(), 1);

  // Kartend-7iqhl.2: the browser's profile-scoped Fix loads every row across all
  // sources (2 from A.dat + 1 from B.dat) to reconstruct fixable AuditRows.
  auto allRows = DatAuditProfile::loadProfileResultRows(m_db, id);
  QVERIFY(allRows.isOk());
  QCOMPARE(allRows.value().size(), 3);
  bool sawA = false;
  bool sawB = false;
  for (const auto &r : allRows.value()) {
    sawA = sawA || r.sourceName == QLatin1String("A.dat");
    sawB = sawB || r.sourceName == QLatin1String("B.dat");
  }
  QVERIFY(sawA);
  QVERIFY(sawB);
}

void TestDatAuditProfile::loadResultSummaryCountsByStatus() {
  auto ins = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  QList<DatAuditProfile::ResultRow> rows;
  for (int i = 0; i < 3; ++i) {
    DatAuditProfile::ResultRow r;
    r.entryKey = QStringLiteral("file:/media/have-%1.mkv").arg(i);
    r.status = 0; // Have
    rows << r;
  }
  DatAuditProfile::ResultRow miss;
  miss.entryKey = QStringLiteral("entry:Clip Gone.mkv");
  miss.status = 6; // Missing
  rows << miss;
  QVERIFY(DatAuditProfile::replaceResults(m_db, id, rows).isOk());
  QVERIFY(DatAuditProfile::touchLastScan(m_db, id, 777).isOk());

  auto summary = DatAuditProfile::loadResultSummary(m_db, id);
  QVERIFY(summary.isOk());
  QVERIFY(summary.value().has_value());
  QCOMPARE(summary.value()->lastScanAtMs, qint64(777));
  QVERIFY(summary.value()->hasResults);
  QCOMPARE(summary.value()->count(0), 3);
  QCOMPARE(summary.value()->count(6), 1);
  QCOMPARE(summary.value()->count(4), 0); // status never written → 0, not error
}

void TestDatAuditProfile::loadResultSummaryDistinguishesNeverFromEmpty() {
  auto ins = DatAuditProfile::insert(m_db, sampleProfile());
  QVERIFY(ins.isOk());
  const qint64 id = ins.value();

  // Never audited: profile exists, no stamp, no rows.
  auto never = DatAuditProfile::loadResultSummary(m_db, id);
  QVERIFY(never.isOk());
  QVERIFY(never.value().has_value());
  QVERIFY(!never.value()->hasResults);
  QCOMPARE(never.value()->lastScanAtMs, qint64(0));

  // Audited-but-empty: a stamp with zero rows still reads as "has results"
  // (auditing an empty folder is a legitimate completed audit).
  QVERIFY(DatAuditProfile::touchLastScan(m_db, id, 123).isOk());
  auto empty = DatAuditProfile::loadResultSummary(m_db, id);
  QVERIFY(empty.isOk());
  QVERIFY(empty.value()->hasResults);

  // Absent profile: empty optional, not an error.
  auto absent = DatAuditProfile::loadResultSummary(m_db, 999999);
  QVERIFY(absent.isOk());
  QVERIFY(!absent.value().has_value());
}

QTEST_MAIN(TestDatAuditProfile)
#include "test_datauditprofile.moc"
