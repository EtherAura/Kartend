// Tests for the DAT-audit engine: Catalogue indexing, the pure classify()
// against the full status taxonomy, end-to-end run() over real temp files, and
// buildCatalogue() through a real DatCache. No DB mocking.

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "datauditcatalogue.h"
#include "datauditregion.h"
#include "datauditrunner.h"
#include "dataudittypes.h"
#include "datcache.h"
#include "dbmigrations.h"
#include "romhasher.h"

using DatAudit::AuditOptions;
using DatAudit::AuditOutput;
using DatAudit::AuditRow;
using DatAudit::Catalogue;
using DatAudit::ScannedFile;
using DatAudit::SourceCompleteness;
using DatAudit::Status;
using DatLookup::DatRecord;

// Kartend-t9u0o: the run()-driving cases used to QSKIP under TSan
// (Kartend-x9mkif.1) because DatAudit::run's QtConcurrent fan-out trips
// Qt-internal pool-lifecycle reports with all-stripped libQt6Core frames.
// Those are now muted module-wide by tests/suppressions/tsan.txt Group D
// (called_from_lib:libQt6Core.so.6), so the worker lambda runs INSTRUMENTED
// under the nightly TSan job — a racy change to it (shared mutable state, a
// non-atomic cancel check) fires again instead of being skipped wholesale.

namespace {

DatRecord makeRecord(const QString &game, const QString &rom, const QString &crc,
                     const QString &md5, const QString &sha1, qint64 size = -1) {
  DatRecord r;
  r.gameName = game;
  r.romName = rom;
  r.crc = crc;
  r.md5 = md5;
  r.sha1 = sha1;
  r.size = size;
  return r;
}

ScannedFile makeFile(const QString &path, const QString &crc, const QString &md5,
                     const QString &sha1, qint64 size = 1, bool readOk = true) {
  ScannedFile f;
  f.path = path;
  f.crc = crc;
  f.md5 = md5;
  f.sha1 = sha1;
  f.size = size;
  f.readOk = readOk;
  return f;
}

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes) {
  const QString path = dir.filePath(name);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return {};
  }
  f.write(bytes);
  f.close();
  return path;
}

constexpr const char *kLogiqxDat = R"xml(<?xml version="1.0"?>
<datafile>
  <header><name>Test</name></header>
  <game name="Alpha">
    <rom name="Alpha.bin" size="5" crc="deadbeef"
         md5="11111111111111111111111111111111"
         sha1="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"/>
  </game>
  <game name="Beta">
    <rom name="Beta.bin" size="5" crc="cafebabe"
         md5="22222222222222222222222222222222"
         sha1="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"/>
  </game>
</datafile>
)xml";

} // namespace

class TestDatAuditRunner : public QObject {
  Q_OBJECT

private slots:
  // Catalogue
  void catalogueMatchesByHashPriority();
  void catalogueMatchesByName();
  void catalogueMissReturnsMinusOne();

  // classify()
  void classifyHave();
  void classifyWrongName();
  void classifyWrongHash();
  void classifyDuplicate();
  void classifyUnknown();
  void classifyCorrupt();
  void classifyMissing();
  void classifySummaryCounts();
  void classifyCarriesZipIndex();

  // 1G1R / region collapse (Kartend-bmj1ko)
  void region_baseNameRegionAndRank();
  void classify1G1R_collapsesAbsentGameToPreferredRegion();
  void classify1G1R_gamePresentInAnyRegionIsNotMissing();

  // disc-aware 1G1R (Kartend-x9mkif.2)
  void region_discQualifierAndGroupKey();
  void classify1G1R_keepsMultiDiscSeparate();

  // run()
  void runEndToEndOverTempFiles();
  void runHonoursIgnoreGlobs();
  void runFlatLayoutSkipsSubfolders();
  void runArchivePerItemAuditsMembers();
  void runAuditsArchiveMembersInAnyLayout();
  void runCancelledBeforeScanReturnsCancelled();
  void runPopulatesHashCache();

  // buildCatalogue()
  void buildCatalogueIngestsDat();

  // catalogue provenance / per-DAT completeness (Kartend-m6qsb.15)
  void catalogueTracksSourceProvenance();
  void classifyReportsPerSourceCompleteness();
  void buildCatalogueAttributesSourceByFilename();

  // persistence contract (Kartend-m6qsb.8)
  void statusIntValuesArePinnedForPersistence();

private:
  static Catalogue twoEntryCatalogue();
};

Catalogue TestDatAuditRunner::twoEntryCatalogue() {
  Catalogue c;
  c.addRecord(makeRecord(QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"),
                         QStringLiteral("deadbeef"),
                         QStringLiteral("11111111111111111111111111111111"),
                         QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 5));
  c.addRecord(makeRecord(QStringLiteral("Beta"), QStringLiteral("Beta.bin"),
                         QStringLiteral("cafebabe"),
                         QStringLiteral("22222222222222222222222222222222"),
                         QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"), 5));
  return c;
}

void TestDatAuditRunner::catalogueMatchesByHashPriority() {
  const Catalogue c = twoEntryCatalogue();
  // sha1 of Beta but crc of Alpha => sha1 wins.
  const int idx = c.matchByHash(QStringLiteral("deadbeef"), QString(),
                                QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  QVERIFY(idx >= 0);
  QCOMPARE(c.record(idx).gameName, QStringLiteral("Beta"));
  // crc-only still resolves.
  QCOMPARE(c.record(c.matchByHash(QStringLiteral("deadbeef"), QString(), QString())).gameName,
           QStringLiteral("Alpha"));
  // Case-insensitive.
  QVERIFY(c.matchByHash(QString(), QString(),
                        QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")) >= 0);
}

void TestDatAuditRunner::catalogueMatchesByName() {
  const Catalogue c = twoEntryCatalogue();
  QCOMPARE(c.matchByName(QStringLiteral("Beta.bin")), 1);
  QCOMPARE(c.matchByName(QStringLiteral("Nope.bin")), -1);
}

void TestDatAuditRunner::catalogueMissReturnsMinusOne() {
  const Catalogue c = twoEntryCatalogue();
  QCOMPARE(c.matchByHash(QStringLiteral("ffffffff"), QString(), QString()), -1);
  QCOMPARE(c.matchByHash(QString(), QString(), QString()), -1);
}

void TestDatAuditRunner::classifyHave() {
  const Catalogue c = twoEntryCatalogue();
  QList<ScannedFile> files{makeFile(QStringLiteral("/x/Alpha.bin"), QStringLiteral("deadbeef"),
                                    QString(), QString(), 5)};
  const AuditOutput out = DatAudit::classify(c, files);
  // 1 file row (Have) + 1 Missing (Beta).
  QCOMPARE(out.summary.have, 1);
  QCOMPARE(out.summary.missing, 1);
  bool sawHave = false;
  for (const AuditRow &r : out.rows) {
    if (r.status == Status::Have) {
      sawHave = true;
      QCOMPARE(r.gameName, QStringLiteral("Alpha"));
      QCOMPARE(r.actualName, QStringLiteral("Alpha.bin"));
    }
  }
  QVERIFY(sawHave);
}

void TestDatAuditRunner::classifyCarriesZipIndex() {
  // The archive member index threads from the ScannedFile onto the AuditRow
  // (Kartend-7iqhl.4), regardless of how the row classifies. An unmatched
  // member (Unknown) is the simplest carrier — no catalogue entry needed.
  Catalogue empty;
  ScannedFile member = makeFile(QStringLiteral("/x/set.zip/disc2.bin"), QStringLiteral("deadbeef"),
                                QString(), QString(), 5);
  member.zipIndex = 3;
  const AuditOutput out = DatAudit::classify(empty, {member});
  QCOMPARE(out.rows.size(), 1);
  QCOMPARE(out.rows.at(0).status, Status::Unknown);
  QCOMPARE(out.rows.at(0).zipIndex, 3);
}

void TestDatAuditRunner::classifyWrongName() {
  const Catalogue c = twoEntryCatalogue();
  // Content of Alpha but on disk as "misnamed.bin".
  QList<ScannedFile> files{makeFile(QStringLiteral("/x/misnamed.bin"), QStringLiteral("deadbeef"),
                                    QString(), QString(), 5)};
  const AuditOutput out = DatAudit::classify(c, files);
  QCOMPARE(out.summary.wrongName, 1);
  QCOMPARE(out.summary.have, 0);
  for (const AuditRow &r : out.rows) {
    if (r.status == Status::WrongName) {
      QCOMPARE(r.expectedName, QStringLiteral("Alpha.bin"));
      QCOMPARE(r.actualName, QStringLiteral("misnamed.bin"));
    }
  }
}

void TestDatAuditRunner::classifyWrongHash() {
  const Catalogue c = twoEntryCatalogue();
  // Named Alpha.bin but the content hash matches nothing in the catalogue.
  QList<ScannedFile> files{makeFile(QStringLiteral("/x/Alpha.bin"), QStringLiteral("99999999"),
                                    QString(), QString(), 5)};
  const AuditOutput out = DatAudit::classify(c, files);
  QCOMPARE(out.summary.wrongHash, 1);
  // Alpha is still Missing (no file holds its real content).
  QCOMPARE(out.summary.missing, 2);
}

void TestDatAuditRunner::classifyDuplicate() {
  const Catalogue c = twoEntryCatalogue();
  QList<ScannedFile> files{
      makeFile(QStringLiteral("/x/Alpha.bin"), QStringLiteral("deadbeef"), QString(), QString(), 5),
      makeFile(QStringLiteral("/y/Alpha.bin"), QStringLiteral("deadbeef"), QString(), QString(),
               5)};
  const AuditOutput out = DatAudit::classify(c, files);
  QCOMPARE(out.summary.have, 1);
  QCOMPARE(out.summary.duplicate, 1);
}

void TestDatAuditRunner::classifyUnknown() {
  const Catalogue c = twoEntryCatalogue();
  QList<ScannedFile> files{makeFile(QStringLiteral("/x/mystery.bin"), QStringLiteral("00000000"),
                                    QString(), QString(), 7)};
  const AuditOutput out = DatAudit::classify(c, files);
  QCOMPARE(out.summary.unknown, 1);
}

void TestDatAuditRunner::classifyCorrupt() {
  const Catalogue c = twoEntryCatalogue();
  QList<ScannedFile> files{
      makeFile(QStringLiteral("/x/broken.bin"), QString(), QString(), QString(), -1, false)};
  const AuditOutput out = DatAudit::classify(c, files);
  QCOMPARE(out.summary.corrupt, 1);
}

void TestDatAuditRunner::classifyMissing() {
  const Catalogue c = twoEntryCatalogue();
  const AuditOutput out = DatAudit::classify(c, {});
  QCOMPARE(out.summary.missing, 2);
  QCOMPARE(out.summary.totalCatalogue, 2);
  QCOMPARE(out.summary.totalFiles, 0);
}

void TestDatAuditRunner::classifySummaryCounts() {
  const Catalogue c = twoEntryCatalogue();
  QList<ScannedFile> files{
      makeFile(QStringLiteral("/x/Alpha.bin"), QStringLiteral("deadbeef"), QString(), QString(), 5),
      makeFile(QStringLiteral("/x/junk.bin"), QStringLiteral("00000000"), QString(), QString(), 9)};
  const AuditOutput out = DatAudit::classify(c, files);
  QCOMPARE(out.summary.have, 1);
  QCOMPARE(out.summary.unknown, 1);
  QCOMPARE(out.summary.missing, 1); // Beta
  QCOMPARE(out.summary.totalFiles, 2);
  QCOMPARE(out.summary.present(), 1);
}

void TestDatAuditRunner::runEndToEndOverTempFiles() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Bootstrap a catalogue from the real hashes of files we write, so run()'s
  // own hashing matches.
  const QString aPath = writeFile(dir, QStringLiteral("Alpha.bin"), QByteArrayLiteral("AAAAA"));
  const QString bPath = writeFile(dir, QStringLiteral("misnamed.bin"), QByteArrayLiteral("BBBBB"));
  writeFile(dir, QStringLiteral("mystery.bin"), QByteArrayLiteral("ZZZZZZZ"));
  QVERIFY(!aPath.isEmpty() && !bPath.isEmpty());

  auto ha = RomHasher::hashFile(aPath);
  auto hb = RomHasher::hashFile(bPath);
  QVERIFY(ha.isOk() && hb.isOk());

  Catalogue c;
  c.addRecord(makeRecord(QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"), ha.value().crc,
                         ha.value().md5, ha.value().sha1, ha.value().size));
  c.addRecord(makeRecord(QStringLiteral("Beta"), QStringLiteral("Beta.bin"), hb.value().crc,
                         hb.value().md5, hb.value().sha1, hb.value().size));
  c.addRecord(makeRecord(QStringLiteral("Gamma"), QStringLiteral("Gamma.bin"),
                         QStringLiteral("0badf00d"), QString(), QString(), 3));

  AuditOptions opts;
  opts.scanRoots = {dir.path()};
  const AuditOutput out = DatAudit::run(c, opts, nullptr, nullptr, nullptr);

  QVERIFY(!out.cancelled);
  QCOMPARE(out.summary.have, 1);      // Alpha.bin correctly named
  QCOMPARE(out.summary.wrongName, 1); // misnamed.bin holds Beta's content
  QCOMPARE(out.summary.unknown, 1);   // mystery.bin
  QCOMPARE(out.summary.missing, 1);   // Gamma
  QCOMPARE(out.summary.totalFiles, 3);
}

void TestDatAuditRunner::runHonoursIgnoreGlobs() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  writeFile(dir, QStringLiteral("keep.bin"), QByteArrayLiteral("data"));
  writeFile(dir, QStringLiteral("notes.txt"), QByteArrayLiteral("ignore me"));

  Catalogue c; // empty catalogue: every scanned file is Unknown
  AuditOptions opts;
  opts.scanRoots = {dir.path()};
  opts.ignoreGlobs = {QStringLiteral("*.txt")};
  const AuditOutput out = DatAudit::run(c, opts, nullptr, nullptr, nullptr);

  QCOMPARE(out.summary.totalFiles, 1); // notes.txt skipped
  QCOMPARE(out.summary.unknown, 1);
}

void TestDatAuditRunner::runFlatLayoutSkipsSubfolders() {
  // Kartend-m6qsb.6: a user-confirmed Flat layout means subfolders are noise
  // — the walk must not descend. Unknown (the default) keeps the historical
  // fully-recursive behavior.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  writeFile(dir, QStringLiteral("top.bin"), QByteArrayLiteral("top"));
  QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("sub")));
  {
    QFile nested(dir.filePath(QStringLiteral("sub/nested.bin")));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write("nested");
  }

  Catalogue c; // empty: every scanned file is Unknown, so totalFiles tells all
  AuditOptions opts;
  opts.scanRoots = {dir.path()};
  const AuditOutput recursive = DatAudit::run(c, opts, nullptr, nullptr, nullptr);
  QCOMPARE(recursive.summary.totalFiles, 2);

  opts.layout = DatAudit::Layout::Flat;
  const AuditOutput flat = DatAudit::run(c, opts, nullptr, nullptr, nullptr);
  QCOMPARE(flat.summary.totalFiles, 1);
}

void TestDatAuditRunner::runArchivePerItemAuditsMembers() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("libtsan fork CHECK bug — QProcess can't be used here under TSan");
#endif
  // Needs zip to build the fixture and an extractor for RomHasher to read it.
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    QSKIP("zip not available — cannot build the archive fixture");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    QSKIP("no archive extractor on PATH — RomHasher would error out");
  }

  // One archive holding the catalogue entry's content (Kartend-m6qsb.7):
  // under the outer-bytes audit this reported Unknown container + Missing
  // entry; with layout=ArchivePerItem the MEMBER is the audit unit.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray content = QByteArrayLiteral("MEMBER-CONTENT-0123456789");
  const QString srcDir = dir.path() + QStringLiteral("/src");
  QVERIFY(QDir().mkpath(srcDir));
  {
    QFile f(srcDir + QStringLiteral("/Clip One.mkv"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
  }
  auto memberHash = RomHasher::hashFile(srcDir + QStringLiteral("/Clip One.mkv"));
  QVERIFY(memberHash.isOk());

  QProcess zipProc;
  zipProc.setWorkingDirectory(srcDir);
  zipProc.start(QStringLiteral("zip"), {QStringLiteral("-q"), dir.path() + "/Clip One.zip",
                                        QStringLiteral("Clip One.mkv")});
  QVERIFY(zipProc.waitForFinished(5000));
  QCOMPARE(zipProc.exitCode(), 0);
  QVERIFY(QDir(srcDir).removeRecursively()); // only the archive remains

  Catalogue c;
  c.addRecord(makeRecord(QStringLiteral("Clip One"), QStringLiteral("Clip One.mkv"),
                         memberHash.value().crc, memberHash.value().md5, memberHash.value().sha1,
                         memberHash.value().size));

  // Member-cache plumbing needs a migrated DB connection (same in-memory
  // bootstrap as runPopulatesHashCache).
  const QString conn = QStringLiteral("test_runner_member_cache");
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
  db.setDatabaseName(QStringLiteral(":memory:"));
  QVERIFY(db.open());
  {
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, "
                                  "path TEXT, last_modified TEXT)")));
  }
  DbMigrations::applySchemaMigrations(db, QStringLiteral("test_runner_member_cache"));

  AuditOptions opts;
  opts.scanRoots = {dir.path()};
  opts.layout = DatAudit::Layout::ArchivePerItem;
  const AuditOutput out = DatAudit::run(c, opts, &db, nullptr, nullptr);
  QVERIFY(!out.cancelled);
  QCOMPARE(out.summary.have, 1);    // the member satisfies the entry
  QCOMPARE(out.summary.missing, 0); // outer-bytes audit reported 1 here
  QCOMPARE(out.summary.unknown, 0); // and 1 here (the container)

  // The member cache was populated, and a second run is served from it with
  // identical classification (no re-extraction needed for correctness).
  {
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM archive_member_hash_cache")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
  }
  const AuditOutput again = DatAudit::run(c, opts, &db, nullptr, nullptr);
  QCOMPARE(again.summary.have, 1);

  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(conn);
}

void TestDatAuditRunner::runAuditsArchiveMembersInAnyLayout() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("libtsan fork CHECK bug — QProcess can't be used here under TSan");
#endif
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    QSKIP("zip not available — cannot build the archive fixture");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    QSKIP("no archive extractor on PATH — RomHasher would error out");
  }

  // Regression for "DAT auditor cannot see compressed ROMs" (Kartend-34lab
  // follow-up): a zipped ROM must match the DAT even with the DEFAULT Unknown
  // layout — member hashing must NOT require a confirmed archive-per-item
  // layout. Before the fix this reported the .zip as Unknown + the entry as
  // Missing.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray content = QByteArrayLiteral("ROM-INSIDE-A-ZIP-9876543210");
  const QString srcDir = dir.path() + QStringLiteral("/src");
  QVERIFY(QDir().mkpath(srcDir));
  {
    QFile f(srcDir + QStringLiteral("/Game (USA).gb"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
  }
  auto memberHash = RomHasher::hashFile(srcDir + QStringLiteral("/Game (USA).gb"));
  QVERIFY(memberHash.isOk());

  QProcess zipProc;
  zipProc.setWorkingDirectory(srcDir);
  zipProc.start(QStringLiteral("zip"), {QStringLiteral("-q"), dir.path() + "/Game (USA).zip",
                                        QStringLiteral("Game (USA).gb")});
  QVERIFY(zipProc.waitForFinished(5000));
  QCOMPARE(zipProc.exitCode(), 0);
  QVERIFY(QDir(srcDir).removeRecursively()); // only the .zip remains

  Catalogue c;
  c.addRecord(makeRecord(QStringLiteral("Game (USA)"), QStringLiteral("Game (USA).gb"),
                         memberHash.value().crc, memberHash.value().md5, memberHash.value().sha1,
                         memberHash.value().size));

  AuditOptions opts;
  opts.scanRoots = {dir.path()};
  opts.layout = DatAudit::Layout::Unknown; // the user never confirmed a layout
  const AuditOutput out = DatAudit::run(c, opts, nullptr, nullptr, nullptr);
  QVERIFY(!out.cancelled);
  QCOMPARE(out.summary.have, 1);    // the zip member satisfies the entry
  QCOMPARE(out.summary.missing, 0); // not Missing
  QCOMPARE(out.summary.unknown, 0); // the container is not an Unknown file
}

void TestDatAuditRunner::runCancelledBeforeScanReturnsCancelled() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  writeFile(dir, QStringLiteral("a.bin"), QByteArrayLiteral("x"));

  auto cancel = std::make_shared<std::atomic<bool>>(true); // already cancelled
  AuditOptions opts;
  opts.scanRoots = {dir.path()};
  const AuditOutput out = DatAudit::run(twoEntryCatalogue(), opts, nullptr, cancel, nullptr);
  QVERIFY(out.cancelled);
}

void TestDatAuditRunner::runPopulatesHashCache() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  writeFile(dir, QStringLiteral("a.bin"), QByteArrayLiteral("cache me"));

  const QString conn = QStringLiteral("test_runner_cache");
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
  db.setDatabaseName(QStringLiteral(":memory:"));
  QVERIFY(db.open());
  {
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, "
                                  "path TEXT, last_modified TEXT)")));
  }
  DbMigrations::applySchemaMigrations(db, QStringLiteral("test_runner_cache"));

  AuditOptions opts;
  opts.scanRoots = {dir.path()};
  const AuditOutput out = DatAudit::run(Catalogue{}, opts, &db, nullptr, nullptr);
  QCOMPARE(out.summary.totalFiles, 1);

  QSqlQuery count(db);
  QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM file_hash_cache")));
  QVERIFY(count.next());
  QCOMPARE(count.value(0).toInt(), 1); // the scanned file was cached

  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(conn);
}

void TestDatAuditRunner::buildCatalogueIngestsDat() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString datPath = dir.filePath(QStringLiteral("test.dat"));
  {
    QFile f(datPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kLogiqxDat);
    f.close();
  }
  DatCache::Store cache(dir.filePath(QStringLiteral("cache.sqlite")));
  QVERIFY(cache.isOpen());

  QStringList failed;
  const Catalogue c = DatAudit::buildCatalogue(cache, {datPath}, &failed);
  QVERIFY(failed.isEmpty());
  QCOMPARE(c.size(), 2);
  QVERIFY(c.matchByName(QStringLiteral("Alpha.bin")) >= 0);
  QVERIFY(c.matchByHash(QString(), QString(),
                        QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")) >= 0);

  // A missing/garbage DAT is reported, not fatal.
  QStringList failed2;
  const Catalogue empty =
      DatAudit::buildCatalogue(cache, {dir.filePath(QStringLiteral("nope.dat"))}, &failed2);
  QCOMPARE(empty.size(), 0);
  QCOMPARE(failed2.size(), 1);
}

void TestDatAuditRunner::catalogueTracksSourceProvenance() {
  Catalogue c;
  const int datA = c.addSource(QStringLiteral("A.dat"));
  c.addRecord(makeRecord(QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"),
                         QStringLiteral("deadbeef"), QString(), QString(), 5),
              datA);
  const int datB = c.addSource(QStringLiteral("B.dat"));
  c.addRecord(makeRecord(QStringLiteral("Beta"), QStringLiteral("Beta.bin"),
                         QStringLiteral("cafebabe"), QString(), QString(), 5),
              datB);
  // A hand-added record with no source stays attributable to nothing.
  c.addRecord(makeRecord(QStringLiteral("Orphan"), QStringLiteral("Orphan.bin"),
                         QStringLiteral("0badf00d"), QString(), QString(), 5));

  QCOMPARE(c.sourceCount(), 2);
  QCOMPARE(c.sourceName(datA), QStringLiteral("A.dat"));
  QCOMPARE(c.sourceName(datB), QStringLiteral("B.dat"));
  QCOMPARE(c.recordSource(0), datA);
  QCOMPARE(c.recordSource(1), datB);
  QCOMPARE(c.recordSource(2), -1);
  QVERIFY(c.sourceName(-1).isEmpty()); // orphan record's source
  QVERIFY(c.sourceName(99).isEmpty()); // out of range
}

void TestDatAuditRunner::classifyReportsPerSourceCompleteness() {
  // Two DATs: A has 2 entries (one present), B has 1 (present). Per-source
  // breakdown should attribute have/missing back to each DAT.
  Catalogue c;
  const int datA = c.addSource(QStringLiteral("A.dat"));
  c.addRecord(makeRecord(QStringLiteral("Alpha"), QStringLiteral("Alpha.bin"),
                         QStringLiteral("deadbeef"), QString(), QString(), 5),
              datA);
  c.addRecord(makeRecord(QStringLiteral("Beta"), QStringLiteral("Beta.bin"),
                         QStringLiteral("cafebabe"), QString(), QString(), 5),
              datA);
  const int datB = c.addSource(QStringLiteral("B.dat"));
  c.addRecord(makeRecord(QStringLiteral("Gamma"), QStringLiteral("Gamma.bin"),
                         QStringLiteral("0badf00d"), QString(), QString(), 5),
              datB);

  const QList<ScannedFile> files{
      makeFile(QStringLiteral("/x/Alpha.bin"), QStringLiteral("deadbeef"), QString(), QString(), 5),
      makeFile(QStringLiteral("/x/Gamma.bin"), QStringLiteral("0badf00d"), QString(), QString(),
               5)};
  const AuditOutput out = DatAudit::classify(c, files);

  QCOMPARE(out.summary.perSource.size(), 2);
  const SourceCompleteness &a = out.summary.perSource.at(0);
  QCOMPARE(a.name, QStringLiteral("A.dat"));
  QCOMPARE(a.total, 2);
  QCOMPARE(a.present, 1); // Alpha present, Beta missing
  QCOMPARE(a.missing, 1);
  const SourceCompleteness &b = out.summary.perSource.at(1);
  QCOMPARE(b.name, QStringLiteral("B.dat"));
  QCOMPARE(b.total, 1);
  QCOMPARE(b.present, 1);
  QCOMPARE(b.missing, 0);

  // Rows carry the provenance of their catalogue entry.
  for (const AuditRow &r : out.rows) {
    if (r.gameName == QStringLiteral("Alpha") || r.gameName == QStringLiteral("Beta")) {
      QCOMPARE(r.sourceName, QStringLiteral("A.dat"));
    } else if (r.gameName == QStringLiteral("Gamma")) {
      QCOMPARE(r.sourceName, QStringLiteral("B.dat"));
    }
  }

  // A hand-built catalogue with no sources leaves the breakdown empty.
  const AuditOutput plain = DatAudit::classify(twoEntryCatalogue(), {});
  QVERIFY(plain.summary.perSource.isEmpty());
}

void TestDatAuditRunner::buildCatalogueAttributesSourceByFilename() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString datPath = dir.filePath(QStringLiteral("My Catalogue.dat"));
  {
    QFile f(datPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kLogiqxDat);
    f.close();
  }
  DatCache::Store cache(dir.filePath(QStringLiteral("cache.sqlite")));
  QVERIFY(cache.isOpen());

  const Catalogue c = DatAudit::buildCatalogue(cache, {datPath});
  QCOMPARE(c.sourceCount(), 1);
  QCOMPARE(c.sourceName(0), QStringLiteral("My Catalogue.dat")); // filename, not full path
  QCOMPARE(c.size(), 2);
  QCOMPARE(c.recordSource(0), 0);
  QCOMPARE(c.recordSource(1), 0);
}

void TestDatAuditRunner::region_baseNameRegionAndRank() {
  using namespace DatAudit::Region;
  QCOMPARE(baseGameName(QStringLiteral("Sonic the Hedgehog (USA) (Rev 1)")),
           QStringLiteral("Sonic the Hedgehog"));
  QCOMPARE(baseGameName(QStringLiteral("Plain Title")), QStringLiteral("Plain Title"));
  QCOMPARE(baseGameName(QStringLiteral("Tetris [b1]")), QStringLiteral("Tetris"));

  QCOMPARE(detectRegion(QStringLiteral("Zelda (Europe)")), QStringLiteral("Europe"));
  QCOMPARE(detectRegion(QStringLiteral("Zelda (J)")), QStringLiteral("Japan"));
  QCOMPARE(detectRegion(QStringLiteral("Zelda (Japan, USA)")), QStringLiteral("Japan"));
  QCOMPARE(detectRegion(QStringLiteral("Zelda (En,Fr,De)")), QString()); // languages, not regions
  QCOMPARE(detectRegion(QStringLiteral("Zelda")), QString());

  const QStringList prefs{QStringLiteral("USA"), QStringLiteral("Europe")};
  QCOMPARE(regionRank(QStringLiteral("USA"), prefs), 0);
  QCOMPARE(regionRank(QStringLiteral("europe"), prefs), 1); // case-insensitive
  QCOMPARE(regionRank(QStringLiteral("Japan"), prefs), 2);  // tagged but unlisted
  QCOMPARE(regionRank(QString(), prefs), 3);                // untagged ranks last
}

void TestDatAuditRunner::classify1G1R_collapsesAbsentGameToPreferredRegion() {
  // One game in three regions + a second game; nothing on disk.
  Catalogue c;
  c.addRecord(makeRecord(QStringLiteral("Zelda (Japan)"), QStringLiteral("Zelda (Japan).bin"),
                         QStringLiteral("11111111"), QString(), QString(), 4));
  c.addRecord(makeRecord(QStringLiteral("Zelda (USA)"), QStringLiteral("Zelda (USA).bin"),
                         QStringLiteral("22222222"), QString(), QString(), 4));
  c.addRecord(makeRecord(QStringLiteral("Zelda (Europe)"), QStringLiteral("Zelda (Europe).bin"),
                         QStringLiteral("33333333"), QString(), QString(), 4));
  c.addRecord(makeRecord(QStringLiteral("Mario (USA)"), QStringLiteral("Mario (USA).bin"),
                         QStringLiteral("44444444"), QString(), QString(), 4));

  const QStringList prefs{QStringLiteral("USA"), QStringLiteral("Europe")};
  const AuditOutput out = DatAudit::classify(c, {}, prefs, /*onePerGame=*/true);

  // One Missing per game (Zelda + Mario), not one per region variant.
  QCOMPARE(out.summary.missing, 2);
  int zeldaMissing = 0;
  QString zeldaName;
  for (const AuditRow &r : out.rows) {
    if (r.status == Status::Missing && r.gameName.startsWith(QStringLiteral("Zelda"))) {
      ++zeldaMissing;
      zeldaName = r.gameName;
    }
  }
  QCOMPARE(zeldaMissing, 1);
  QCOMPARE(zeldaName, QStringLiteral("Zelda (USA)")); // most-preferred region in the DAT
}

void TestDatAuditRunner::classify1G1R_gamePresentInAnyRegionIsNotMissing() {
  Catalogue c;
  c.addRecord(makeRecord(QStringLiteral("Zelda (USA)"), QStringLiteral("Zelda (USA).bin"),
                         QStringLiteral("22222222"), QString(), QString(), 4));
  c.addRecord(makeRecord(QStringLiteral("Zelda (Japan)"), QStringLiteral("Zelda (Japan).bin"),
                         QStringLiteral("11111111"), QString(), QString(), 4));

  // The Japan variant is on disk (crc match) while the preferred USA is absent.
  const QList<ScannedFile> files{makeFile(QStringLiteral("/roms/Zelda (Japan).bin"),
                                          QStringLiteral("11111111"), QString(), QString(), 4)};
  const QStringList prefs{QStringLiteral("USA")};
  const AuditOutput out = DatAudit::classify(c, files, prefs, /*onePerGame=*/true);

  // Covered by the present Japan variant — not Missing despite USA being absent.
  QCOMPARE(out.summary.missing, 0);
  QCOMPARE(out.summary.have, 1);
}

void TestDatAuditRunner::region_discQualifierAndGroupKey() {
  using namespace DatAudit::Region;
  QCOMPARE(discQualifier(QStringLiteral("FF7 (USA) (Disc 1)")), QStringLiteral("disc 1"));
  QCOMPARE(discQualifier(QStringLiteral("FF7 (Europe) (Disk 2)")), QStringLiteral("disc 2"));
  QCOMPARE(discQualifier(QStringLiteral("Sonic (USA)")), QString());

  // Region variants of one disc share a key; different discs do not.
  QCOMPARE(groupKey(QStringLiteral("FF7 (USA) (Disc 1)")),
           groupKey(QStringLiteral("FF7 (Europe) (Disc 1)")));
  QVERIFY(groupKey(QStringLiteral("FF7 (USA) (Disc 1)")) !=
          groupKey(QStringLiteral("FF7 (USA) (Disc 2)")));
  // A non-disc title still groups by base name across regions.
  QCOMPARE(groupKey(QStringLiteral("Sonic (USA)")), groupKey(QStringLiteral("Sonic (Europe)")));
}

void TestDatAuditRunner::classify1G1R_keepsMultiDiscSeparate() {
  // A two-disc game, each disc in USA + Europe.
  Catalogue c;
  c.addRecord(makeRecord(QStringLiteral("FF7 (USA) (Disc 1)"),
                         QStringLiteral("FF7 (USA) (Disc 1).bin"), QStringLiteral("d1usa000"),
                         QString(), QString(), 4));
  c.addRecord(makeRecord(QStringLiteral("FF7 (Europe) (Disc 1)"),
                         QStringLiteral("FF7 (Europe) (Disc 1).bin"), QStringLiteral("d1eur000"),
                         QString(), QString(), 4));
  c.addRecord(makeRecord(QStringLiteral("FF7 (USA) (Disc 2)"),
                         QStringLiteral("FF7 (USA) (Disc 2).bin"), QStringLiteral("d2usa000"),
                         QString(), QString(), 4));
  c.addRecord(makeRecord(QStringLiteral("FF7 (Europe) (Disc 2)"),
                         QStringLiteral("FF7 (Europe) (Disc 2).bin"), QStringLiteral("d2eur000"),
                         QString(), QString(), 4));

  // Only Disc 1 (USA) is on disk; Disc 2 is entirely absent.
  const QList<ScannedFile> files{makeFile(QStringLiteral("/roms/FF7 (USA) (Disc 1).bin"),
                                          QStringLiteral("d1usa000"), QString(), QString(), 4)};
  const QStringList prefs{QStringLiteral("USA")};
  const AuditOutput out = DatAudit::classify(c, files, prefs, /*onePerGame=*/true);

  // Disc 1 is covered; Disc 2 is still Missing (one row, preferred USA). The
  // discs must not collapse into a single "game".
  QCOMPARE(out.summary.have, 1);
  QCOMPARE(out.summary.missing, 1);
  for (const AuditRow &r : out.rows) {
    if (r.status == Status::Missing) {
      QCOMPARE(r.gameName, QStringLiteral("FF7 (USA) (Disc 2)"));
    }
  }
}

void TestDatAuditRunner::statusIntValuesArePinnedForPersistence() {
  // dat_audit_result.status persists these as plain ints (the profile store
  // sits below this module in the layering DAG and can't see the enum), so
  // reordering Status silently re-labels every stored snapshot. If this test
  // fails you are changing the on-disk meaning — add a DB migration or map
  // explicitly instead.
  QCOMPARE(static_cast<int>(Status::Have), 0);
  QCOMPARE(static_cast<int>(Status::WrongName), 1);
  QCOMPARE(static_cast<int>(Status::WrongHash), 2);
  QCOMPARE(static_cast<int>(Status::Duplicate), 3);
  QCOMPARE(static_cast<int>(Status::Unknown), 4);
  QCOMPARE(static_cast<int>(Status::Corrupt), 5);
  QCOMPARE(static_cast<int>(Status::Missing), 6);
}

QTEST_MAIN(TestDatAuditRunner)
#include "test_datauditrunner.moc"
