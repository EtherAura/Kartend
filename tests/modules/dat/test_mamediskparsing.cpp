/**
 * @file test_mamediskparsing.cpp
 * @brief MAME listxml `<disk>` parsing and its DatCache round-trip (Kartend-xu36n).
 *
 * The parser cases call the production entry points on synthetic XML; the
 * cache cases ingest into a real on-disk SQLite cache and reopen it.
 */

#include "datcache.h"
#include "datlookup.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// (a) rom + two dumped disks, (b) disk-only machine, (c) nodump disk without a
// sha1 beside a rom, (d) rom-only machine.
constexpr const char *kDiskXml = R"xml(<?xml version="1.0"?>
<mame build="test">
  <machine name="gdgame" cloneof="gdparent" romof="gdparent">
    <description>GD Game</description>
    <rom name="bios.ic27" size="2097152" crc="89F2B1A1" sha1="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"/>
    <disk name="gdl-0001" sha1="BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB" status="BadDump" merge="gdl-0001p" index="0"/>
    <disk name="gdl-0001b" sha1="cccccccccccccccccccccccccccccccccccccccc" index="1"/>
  </machine>
  <machine name="diskonly">
    <description>Disk Only</description>
    <disk name="hdd" sha1="dddddddddddddddddddddddddddddddddddddddd" status="good"/>
  </machine>
  <machine name="nodumpset">
    <description>No Dump</description>
    <rom name="p.bin" size="16" crc="12345678"/>
    <disk name="missing" status="nodump"/>
  </machine>
  <machine name="romonly">
    <description>Rom Only</description>
    <rom name="r.bin" size="32" crc="87654321" sha1="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"/>
  </machine>
</mame>
)xml";

// Repeated machine names (case-insensitively) with distinct descriptions and
// disk lists, plus the gdgame/diskonly machines for a plain round-trip.
constexpr const char *kDuplicateXml = R"xml(<?xml version="1.0"?>
<mame build="test">
  <machine name="Dup">
    <description>First Dup</description>
    <rom name="a.bin" size="4" crc="11111111"/>
    <disk name="first-a" sha1="1111111111111111111111111111111111111111" status="good"/>
    <disk name="first-b" sha1="2222222222222222222222222222222222222222" merge="parent-b"/>
  </machine>
  <machine name="dup">
    <description>Second Dup</description>
    <disk name="second-a" sha1="3333333333333333333333333333333333333333" status="baddump"/>
  </machine>
  <machine name="empty1">
    <description>First Empty</description>
    <rom name="e.bin" size="8" crc="22222222"/>
  </machine>
  <machine name="EMPTY1">
    <description>Second Empty With Disks</description>
    <disk name="late" sha1="4444444444444444444444444444444444444444"/>
  </machine>
  <machine name="gdgame">
    <description>GD Game</description>
    <rom name="bios.ic27" size="2097152" crc="89f2b1a1"/>
    <disk name="gdl-0001" sha1="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" status="baddump" merge="gdl-0001p"/>
    <disk name="gdl-0001b" sha1="cccccccccccccccccccccccccccccccccccccccc"/>
  </machine>
  <machine name="diskonly">
    <description>Disk Only</description>
    <disk name="hdd" sha1="dddddddddddddddddddddddddddddddddddddddd" status="good"/>
  </machine>
</mame>
)xml";

// kDuplicateXml with gdgame's disks gone, for the re-ingest case.
constexpr const char *kDuplicateXmlEdited = R"xml(<?xml version="1.0"?>
<mame build="test">
  <machine name="gdgame">
    <description>GD Game</description>
    <rom name="bios.ic27" size="2097152" crc="89f2b1a1"/>
  </machine>
</mame>
)xml";

QString writeDat(const QString &path, const char *bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return QString();
  f.write(bytes);
  f.close();
  return path;
}

/// Row count of dat_machine_disks through a separate connection to the real
/// on-disk cache file; -1 on failure.
int countDiskRows(const QString &dbPath) {
  const QString conn = QStringLiteral("test_mamedisk_count");
  int n = -1;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dbPath);
    if (db.open()) {
      QSqlQuery q(db);
      if (q.exec(QStringLiteral("SELECT COUNT(*) FROM dat_machine_disks")) && q.next()) {
        n = q.value(0).toInt();
      }
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
  return n;
}

/// "name|sha1|status|merge" per disk, in order — one comparable value.
QStringList describeDisks(const QList<DatLookup::MameDisk> &disks) {
  QStringList out;
  for (const DatLookup::MameDisk &d : disks) {
    out << QStringList{d.name, d.sha1, d.status, d.merge}.join(QLatin1Char('|'));
  }
  return out;
}

} // namespace

class TestMameDiskParsing : public QObject {
  Q_OBJECT

private slots:
  void init();

  void parserKeepsDiskFieldsOnTheirMachine();
  void parserKeepsRomOutputUnchanged();
  void cacheRoundTripsDisksThroughReopen();
  void duplicateNamesKeepFirstMachineDisksOnly();
  void firstMachineWithoutDisksGetsNoneFromLaterName();
  void diskRowsFollowReplacementClearAndSchemaReset();

private:
  QString cachePath() const { return m_dir.filePath("datcache.sqlite"); }
  QString datPath() const { return m_dir.filePath("disks.xml"); }
  QTemporaryDir m_dir;
};

void TestMameDiskParsing::init() {
  QFile::remove(cachePath());
  QFile::remove(cachePath() + QStringLiteral("-wal"));
  QFile::remove(cachePath() + QStringLiteral("-shm"));
  QFile::remove(datPath());
}

void TestMameDiskParsing::parserKeepsDiskFieldsOnTheirMachine() {
  auto parsed = DatLookup::parseMameListXmlWithMachines(QByteArray(kDiskXml));
  QVERIFY(parsed.isOk());
  const QList<DatLookup::MameMachine> &machines = parsed.value().machines;
  QCOMPARE(machines.size(), 4);

  // (a) rom + dumped disks: every field, in document order, sha1 lowercased,
  // status lowercased, merge verbatim.
  QCOMPARE(machines[0].setId, QStringLiteral("gdgame"));
  QCOMPARE(machines[0].disks.size(), 2);
  QCOMPARE(machines[0].disks[0].name, QStringLiteral("gdl-0001"));
  QCOMPARE(machines[0].disks[0].sha1, QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  QCOMPARE(machines[0].disks[0].status, QStringLiteral("baddump"));
  QCOMPARE(machines[0].disks[0].merge, QStringLiteral("gdl-0001p"));
  QCOMPARE(machines[0].disks[1].name, QStringLiteral("gdl-0001b"));
  QCOMPARE(machines[0].disks[1].sha1, QStringLiteral("cccccccccccccccccccccccccccccccccccccccc"));
  QVERIFY(machines[0].disks[1].status.isEmpty());
  QVERIFY(machines[0].disks[1].merge.isEmpty());

  // (b) a disk-only machine stays in the machine list with its disk.
  QCOMPARE(machines[1].setId, QStringLiteral("diskonly"));
  QCOMPARE(machines[1].description, QStringLiteral("Disk Only"));
  QCOMPARE(describeDisks(machines[1].disks),
           QStringList{QStringLiteral("hdd|dddddddddddddddddddddddddddddddddddddddd|good|")});

  // (c) the sha1-less nodump disk is skipped, not an error; (d) no disks.
  QCOMPARE(machines[2].setId, QStringLiteral("nodumpset"));
  QVERIFY(machines[2].disks.isEmpty());
  QCOMPARE(machines[3].setId, QStringLiteral("romonly"));
  QVERIFY(machines[3].disks.isEmpty());
}

void TestMameDiskParsing::parserKeepsRomOutputUnchanged() {
  const QByteArray xml(kDiskXml);
  auto parsed = DatLookup::parseMameListXmlWithMachines(xml);
  QVERIFY(parsed.isOk());
  const QList<DatLookup::DatRecord> &records = parsed.value().records;

  // Only the three <rom> entries become DatRecords; no disk leaks into them
  // and the disk-only machine contributes none.
  QCOMPARE(records.size(), 3);
  QCOMPARE(records[0].gameName, QStringLiteral("GD Game"));
  QCOMPARE(records[0].setId, QStringLiteral("gdgame"));
  QCOMPARE(records[0].romName, QStringLiteral("bios.ic27"));
  QCOMPARE(records[0].size, qint64(2097152));
  QCOMPARE(records[0].crc, QStringLiteral("89f2b1a1"));
  QCOMPARE(records[0].sha1, QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  QCOMPARE(records[0].cloneOf, QStringLiteral("gdparent"));
  QCOMPARE(records[1].romName, QStringLiteral("p.bin"));
  QCOMPARE(records[1].setId, QStringLiteral("nodumpset"));
  QCOMPARE(records[2].gameName, QStringLiteral("Rom Only"));
  QCOMPARE(records[2].romName, QStringLiteral("r.bin"));
  QCOMPARE(records[2].size, qint64(32));
  QCOMPARE(records[2].crc, QStringLiteral("87654321"));
  QCOMPARE(records[2].sha1, QStringLiteral("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"));
  for (const DatLookup::DatRecord &r : records) {
    QVERIFY(!r.romName.startsWith(QStringLiteral("gdl-")));
    QVERIFY(r.romName != QStringLiteral("hdd") && r.romName != QStringLiteral("missing"));
  }

  // The records-only entry point returns the same per-rom list.
  auto plain = DatLookup::parseMameListXml(xml);
  QVERIFY(plain.isOk());
  QCOMPARE(plain.value().size(), records.size());
  for (qsizetype i = 0; i < records.size(); ++i) {
    QCOMPARE(plain.value()[i].gameName, records[i].gameName);
    QCOMPARE(plain.value()[i].romName, records[i].romName);
    QCOMPARE(plain.value()[i].setId, records[i].setId);
    QCOMPARE(plain.value()[i].size, records[i].size);
    QCOMPARE(plain.value()[i].crc, records[i].crc);
    QCOMPARE(plain.value()[i].md5, records[i].md5);
    QCOMPARE(plain.value()[i].sha1, records[i].sha1);
    QCOMPARE(plain.value()[i].cloneOf, records[i].cloneOf);
    QCOMPARE(plain.value()[i].mia, records[i].mia);
  }
}

void TestMameDiskParsing::cacheRoundTripsDisksThroughReopen() {
  writeDat(datPath(), kDuplicateXml);
  {
    DatCache::Store store(cachePath());
    QVERIFY(store.isOpen());
    QVERIFY(store.openOrIngest(datPath()).isOk());
  }
  QCOMPARE(countDiskRows(cachePath()), 7);

  DatCache::Store reopened(cachePath());
  QVERIFY(reopened.isOpen());
  QCOMPARE(countDiskRows(cachePath()), 7);
  const auto source = reopened.peek(datPath());
  QVERIFY(source.has_value());

  const auto gd = reopened.machinesForSet({*source}, QStringLiteral("gdgame"));
  QCOMPARE(gd.size(), 1);
  QCOMPARE(gd[0].machine.description, QStringLiteral("GD Game"));
  QCOMPARE(describeDisks(gd[0].machine.disks),
           (QStringList{QStringLiteral(
                            "gdl-0001|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb|baddump|gdl-0001p"),
                        QStringLiteral("gdl-0001b|cccccccccccccccccccccccccccccccccccccccc||")}));

  const auto diskOnly = reopened.machinesForSet({*source}, QStringLiteral("diskonly"));
  QCOMPARE(diskOnly.size(), 1);
  QCOMPARE(describeDisks(diskOnly[0].machine.disks),
           QStringList{QStringLiteral("hdd|dddddddddddddddddddddddddddddddddddddddd|good|")});
}

void TestMameDiskParsing::duplicateNamesKeepFirstMachineDisksOnly() {
  // machinesForSet returns the first machine row for a (case-insensitive) set
  // name; the disks it carries must be that exact machine's, never the union
  // of every same-named machine.
  writeDat(datPath(), kDuplicateXml);
  {
    DatCache::Store store(cachePath());
    QVERIFY(store.openOrIngest(datPath()).isOk());
  }
  DatCache::Store reopened(cachePath());
  const auto source = reopened.peek(datPath());
  QVERIFY(source.has_value());

  const QStringList firstDupDisks{
      QStringLiteral("first-a|1111111111111111111111111111111111111111|good|"),
      QStringLiteral("first-b|2222222222222222222222222222222222222222||parent-b")};
  for (const QString &name :
       {QStringLiteral("Dup"), QStringLiteral("dup"), QStringLiteral("DUP")}) {
    const auto dup = reopened.machinesForSet({*source}, name);
    QCOMPARE(dup.size(), 1);
    QCOMPARE(dup[0].machine.setId, QStringLiteral("Dup"));
    QCOMPARE(dup[0].machine.description, QStringLiteral("First Dup"));
    QCOMPARE(describeDisks(dup[0].machine.disks), firstDupDisks);
  }
}

void TestMameDiskParsing::firstMachineWithoutDisksGetsNoneFromLaterName() {
  // First machine has no disks, a later same-named one has some: none leak in.
  writeDat(datPath(), kDuplicateXml);
  {
    DatCache::Store store(cachePath());
    QVERIFY(store.openOrIngest(datPath()).isOk());
  }
  DatCache::Store reopened(cachePath());
  const auto source = reopened.peek(datPath());
  QVERIFY(source.has_value());
  for (const QString &name : {QStringLiteral("empty1"), QStringLiteral("Empty1")}) {
    const auto empty = reopened.machinesForSet({*source}, name);
    QCOMPARE(empty.size(), 1);
    QCOMPARE(empty[0].machine.description, QStringLiteral("First Empty"));
    QVERIFY(empty[0].machine.disks.isEmpty());
  }
}

void TestMameDiskParsing::diskRowsFollowReplacementClearAndSchemaReset() {
  writeDat(datPath(), kDuplicateXml);
  DatCache::Store store(cachePath());
  auto first = store.openOrIngest(datPath());
  QVERIFY(first.isOk());
  QCOMPARE(countDiskRows(cachePath()), 7);

  // An mtime-driven re-ingest drops the old source's disks with its machines.
  const QDateTime originalMtime = QFileInfo(datPath()).lastModified();
  auto stampAdvanced = [&]() -> bool {
    writeDat(datPath(), kDuplicateXmlEdited);
    return QFileInfo(datPath()).lastModified() != originalMtime;
  };
  QTRY_VERIFY_WITH_TIMEOUT(stampAdvanced(), 5000);
  auto second = store.openOrIngest(datPath());
  QVERIFY(second.isOk());
  QVERIFY(second.value().id != first.value().id);
  QCOMPARE(countDiskRows(cachePath()), 0);
  const auto gd = store.machinesForSet({second.value()}, QStringLiteral("gdgame"));
  QCOMPARE(gd.size(), 1);
  QVERIFY(gd[0].machine.disks.isEmpty());

  // clearAll() drops every disk row.
  const QDateTime editedMtime = QFileInfo(datPath()).lastModified();
  auto restored = [&]() -> bool {
    writeDat(datPath(), kDuplicateXml);
    return QFileInfo(datPath()).lastModified() != editedMtime;
  };
  QTRY_VERIFY_WITH_TIMEOUT(restored(), 5000);
  QVERIFY(store.openOrIngest(datPath()).isOk());
  QCOMPARE(countDiskRows(cachePath()), 7);
  store.clearAll();
  QCOMPARE(countDiskRows(cachePath()), 0);

  // A cache from the previous schema (v6, no disk table) is wiped on open and
  // the MAME DAT re-ingests with its disks.
  QVERIFY(store.openOrIngest(datPath()).isOk());
  const QString conn = QStringLiteral("test_mamedisk_v6");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(cachePath());
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("DROP TABLE dat_machine_disks")));
    QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 6")));
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
  DatCache::Store reopened(cachePath());
  QVERIFY(reopened.isOpen());
  QVERIFY(!reopened.peek(datPath()).has_value());
  auto source = reopened.openOrIngest(datPath());
  QVERIFY(source.isOk());
  QCOMPARE(countDiskRows(cachePath()), 7);
  const auto dup = reopened.machinesForSet({source.value()}, QStringLiteral("dup"));
  QCOMPARE(dup.size(), 1);
  QCOMPARE(dup[0].machine.disks.size(), 2);
}

QTEST_MAIN(TestMameDiskParsing)
#include "test_mamediskparsing.moc"
