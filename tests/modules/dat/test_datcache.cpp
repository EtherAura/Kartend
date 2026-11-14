/**
 * @file test_datcache.cpp
 * @brief Unit tests for the on-disk sqlite DAT cache.
 *
 * Verifies the ingest → cache-hit → stale-mtime → re-ingest cycle,
 * SQL-backed hash lookups (sha1 / md5 / crc priority order),
 * dialect routing (Logiqx vs MAME listxml ingested into the same
 * cache file), error paths (empty / missing / unparseable DAT),
 * persistence across Store reopens, and clearAll().
 */

#include "datcache.h"
#include "datlookup.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

constexpr const char *kLogiqxDat = R"xml(<?xml version="1.0"?>
<datafile>
  <header><name>Test</name></header>
  <game name="Game Alpha (USA)">
    <rom name="Game Alpha (USA).bin" size="32768"
         crc="deadbeef" md5="11111111111111111111111111111111"
         sha1="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"/>
  </game>
  <game name="Game Beta (Japan)">
    <rom name="Game Beta (Japan).bin" size="65536"
         crc="cafebabe" md5="22222222222222222222222222222222"
         sha1="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"/>
  </game>
</datafile>
)xml";

constexpr const char *kMameDat = R"xml(<?xml version="1.0"?>
<mame build="0.250" mameconfig="10">
  <machine name="pacman" sourcefile="pacman.cpp">
    <description>Pac-Man (Midway)</description>
    <rom name="pacman.6e" size="4096"
         crc="c1e6ab10" sha1="e87e059c5be45753f7e9f33dff851f16d6751181"/>
  </machine>
</mame>
)xml";

// Bytes that differ from the original — same DAT shape, new hash,
// so we can verify mtime-driven re-ingest replaces the records.
constexpr const char *kLogiqxDatEdited = R"xml(<?xml version="1.0"?>
<datafile>
  <header><name>Test</name></header>
  <game name="Game Alpha (USA) Rev 1">
    <rom name="Game Alpha (USA) (Rev 1).bin" size="32768"
         crc="feedface" md5="33333333333333333333333333333333"
         sha1="cccccccccccccccccccccccccccccccccccccccc"/>
  </game>
</datafile>
)xml";

// Minimal complete clrmamepro text DAT (header block + one game).
constexpr const char *kClrMameProDat = R"cmp(clrmamepro (
	name "Test"
)

game (
	name "Game Alpha (USA)"
	rom ( name "Game Alpha (USA).bin" size 32768 crc deadbeef sha1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa )
)
)cmp";

// MAME listxml covering every machine-record field (Kartend-tnt0l): a fully
// described clone, a BIOS, a mechanical set, and two ROM-less machines — a
// device and a bare machine that leaves every optional field at its default.
constexpr const char *kMameMachinesDat = R"xml(<?xml version="1.0"?>
<mame build="0.260">
  <machine name="pacman" cloneof="puckman" romof="puckman">
    <description>Pac-Man (Midway)</description>
    <year>1980</year>
    <manufacturer>Namco (Midway license)</manufacturer>
    <rom name="pacman.6e" size="4096" crc="c1e6ab10"/>
    <display type="raster" rotate="90"/>
    <input players="2" coins="2"/>
    <driver status="good"/>
  </machine>
  <machine name="neogeo" isbios="yes">
    <description>Neo-Geo MV-6F</description>
    <year>1990</year>
    <rom name="sp-s2.sp1" size="131072" crc="9036d879"/>
  </machine>
  <machine name="z80" isdevice="yes" runnable="no">
    <description>Zilog Z80</description>
  </machine>
  <machine name="bare"/>
  <machine name="flipper" ismechanical="yes">
    <description>Flipper</description>
    <rom name="flip.u1" size="2048" crc="0badf00d"/>
  </machine>
</mame>
)xml";

// Same path, edited: pacman re-described, the z80 device dropped.
constexpr const char *kMameMachinesDatEdited = R"xml(<?xml version="1.0"?>
<mame build="0.261">
  <machine name="pacman">
    <description>Pac-Man (Midway, rev 2)</description>
    <rom name="pacman.6e" size="4096" crc="c1e6ab11"/>
  </machine>
</mame>
)xml";

// A second MAME DAT that shares the "pacman" set name with different data
// and carries a set the first DAT lacks.
constexpr const char *kMameMachinesDatOther = R"xml(<?xml version="1.0"?>
<mame build="0.200">
  <machine name="PacMan">
    <description>Pac-Man (other DAT)</description>
    <year>1981</year>
    <rom name="pm.bin" size="4096" crc="11112222"/>
  </machine>
  <machine name="onlyinb">
    <description>Only In B</description>
    <rom name="b.bin" size="16" crc="33334444"/>
  </machine>
</mame>
)xml";

// MAME listxml for the hide rule (Kartend-tnt0l): one set per flag on its own —
// ordinary, BIOS, device (still runnable), runnable="no" only, mechanical,
// non-working — plus a bare machine left at every default.
constexpr const char *kMameHideRulesDat = R"xml(<?xml version="1.0"?>
<mame build="0.260">
  <machine name="pacman">
    <description>Pac-Man</description>
    <rom name="pacman.6e" size="4096" crc="c1e6ab10"/>
    <driver status="good"/>
  </machine>
  <machine name="neogeo" isbios="yes">
    <description>Neo-Geo MV-6F</description>
    <rom name="sp-s2.sp1" size="131072" crc="9036d879"/>
  </machine>
  <machine name="ym2610" isdevice="yes">
    <description>YM2610</description>
  </machine>
  <machine name="notrun" runnable="no">
    <description>Not Runnable</description>
    <rom name="nr.bin" size="16" crc="55556666"/>
  </machine>
  <machine name="flipper" ismechanical="yes">
    <description>Flipper</description>
    <rom name="flip.u1" size="2048" crc="0badf00d"/>
  </machine>
  <machine name="broken">
    <description>Broken Game</description>
    <rom name="brk.bin" size="16" crc="77778888"/>
    <driver status="preliminary"/>
  </machine>
  <machine name="bare"/>
</mame>
)xml";

// A second MAME DAT that flags names the first lists as ordinary games.
constexpr const char *kMameHideRulesDatOther = R"xml(<?xml version="1.0"?>
<mame build="0.200">
  <machine name="PacMan" isbios="yes">
    <description>Pac-Man BIOS (other DAT)</description>
    <rom name="pmb.bin" size="16" crc="9999aaaa"/>
  </machine>
  <machine name="flipper" isdevice="yes" runnable="no">
    <description>Flipper device (other DAT)</description>
  </machine>
</mame>
)xml";

/// Every dat_machines row, one string per row in rowid order, read through a
/// separate connection to the real on-disk cache file — a snapshot to prove a
/// read-only call left the stored records untouched.
QStringList dumpMachineRows(const QString &dbPath) {
  const QString conn = QStringLiteral("test_datcache_machine_dump");
  QStringList rows;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dbPath);
    if (db.open()) {
      QSqlQuery q(db);
      if (q.exec(QStringLiteral("SELECT * FROM dat_machines ORDER BY rowid"))) {
        while (q.next()) {
          QStringList cols;
          for (int i = 0; i < q.record().count(); ++i) cols << q.value(i).toString();
          rows << cols.join(QLatin1Char('|'));
        }
      }
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
  return rows;
}

/// Row count of dat_machines (optionally for one source id) read through a
/// separate connection to the real on-disk cache file; -1 on failure.
int countMachineRows(const QString &dbPath, qint64 sourceId = -1) {
  const QString conn = QStringLiteral("test_datcache_machine_count");
  int n = -1;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dbPath);
    if (db.open()) {
      QSqlQuery q(db);
      if (sourceId < 0) {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM dat_machines"));
      } else {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM dat_machines WHERE source_id = ?"));
        q.addBindValue(sourceId);
      }
      if (q.exec() && q.next()) n = q.value(0).toInt();
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
  return n;
}

QString writeDat(const QString &path, const char *bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return QString();
  f.write(bytes);
  f.close();
  return path;
}

} // namespace

class TestDatCache : public QObject {
  Q_OBJECT

private slots:
  /// Wipe the cache DB + DAT fixture between tests. `m_dir` is a
  /// class member so its filesystem location stays stable, but the
  /// contents need a fresh start each test or stale cached source
  /// rows from a previous test will be served as cache hits.
  void init();

  void ingestsLogiqxDatThenServesCacheHit();
  void ingestsMameDatWithDescription();
  void lookupTriesSha1ThenMd5ThenCrc();
  void lookupMissReturnsNullopt();
  void editedDatIsReIngestedOnMtimeChange();
  void emptyPathErrors();
  void missingFileErrors();
  void unparseableDatPropagatesError();
  void truncatedClrMameProDatRefusedByIngest();
  void cacheSurvivesStoreReopen();
  void clearAllWipesEverything();
  void invalidSourceHandleSkipsLookup();
  void multipleSourcesCoexistInOneCache();
  void peekReturnsCachedRowWithoutIngesting();
  void peekMissesUningestedAndStaysCheap();
  void headerMetadataRoundTripsThroughCache();
  void schemaBumpWipesOldCache();
  void forEachRecordStreamsAllRecords();
  void forEachRecordInvalidSourceReturnsFalse();
  void cloneOfRoundTripsThroughCache();
  void miaRoundTripsThroughCache();

  // MAME machine records (Kartend-tnt0l); also run as datcache_machine_metadata.
  void machineRecordsRoundTripThroughReopen();
  void machineLookupIsScopedToSuppliedSources();
  void machineRowsFollowReplacementAndClearAll();
  void previousSchemaCacheReingestsMachines();
  void nonMameIngestStoresNoMachines();

  // Hide rule for BIOS / device / non-runnable sets (Kartend-tnt0l).
  void hideRuleFlagsBiosDeviceAndNonRunnable();
  void hideRuleKeepsUnflaggedAndUnknownSets();
  void hideRuleIsScopedToSuppliedSources();
  void hideRuleLeavesCacheUntouched();

private:
  QString cachePath() const { return m_dir.filePath("datcache.sqlite"); }
  QString datPath() const { return m_dir.filePath("test.dat"); }
  QTemporaryDir m_dir;
};

void TestDatCache::init() {
  QFile::remove(cachePath());
  QFile::remove(cachePath() + QStringLiteral("-wal"));
  QFile::remove(cachePath() + QStringLiteral("-shm"));
  QFile::remove(datPath());
}

void TestDatCache::ingestsLogiqxDatThenServesCacheHit() {
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());
  QVERIFY(store.isOpen());

  auto first = store.openOrIngest(datPath());
  QVERIFY(first.isOk());
  QCOMPARE(first.value().dialect, DatLookup::Dialect::Logiqx);
  QCOMPARE(first.value().recordCount, 2);
  const qint64 firstId = first.value().id;

  // Second call with no file change should return the same source
  // row (same id) — proves the cache hit path is wired.
  auto second = store.openOrIngest(datPath());
  QVERIFY(second.isOk());
  QCOMPARE(second.value().id, firstId);
  QCOMPARE(second.value().recordCount, 2);
}

void TestDatCache::ingestsMameDatWithDescription() {
  // MAME listxml: dialect should be reported as Mame, gameName comes
  // from the <description> text (not the cryptic set-id).
  writeDat(datPath(), kMameDat);
  DatCache::Store store(cachePath());
  QVERIFY(store.isOpen());

  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());
  QCOMPARE(source.value().dialect, DatLookup::Dialect::Mame);
  QCOMPARE(source.value().recordCount, 1);

  auto rec = store.lookup(source.value(), QString(),
                          QStringLiteral("e87e059c5be45753f7e9f33dff851f16d6751181"), QString());
  QVERIFY(rec.has_value());
  QCOMPARE(rec->gameName, QStringLiteral("Pac-Man (Midway)"));
  QCOMPARE(rec->romName, QStringLiteral("pacman.6e"));
}

void TestDatCache::lookupTriesSha1ThenMd5ThenCrc() {
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());

  // Point each hash at a different record. sha1 is the most reliable
  // and should win — verified by checking which record came back.
  auto rec =
      store.lookup(source.value(),
                   QStringLiteral("11111111111111111111111111111111"),         // md5 of Alpha
                   QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"), // sha1 of Beta
                   QStringLiteral("deadbeef"));                                // crc of Alpha
  QVERIFY(rec.has_value());
  QCOMPARE(rec->gameName, QStringLiteral("Game Beta (Japan)"));

  // md5 alone resolves correctly when sha1 is empty.
  auto byMd5 = store.lookup(source.value(), QStringLiteral("11111111111111111111111111111111"),
                            QString(), QString());
  QVERIFY(byMd5.has_value());
  QCOMPARE(byMd5->gameName, QStringLiteral("Game Alpha (USA)"));

  // crc alone (the legacy DAT case) still finds the record.
  auto byCrc = store.lookup(source.value(), QString(), QString(), QStringLiteral("cafebabe"));
  QVERIFY(byCrc.has_value());
  QCOMPARE(byCrc->gameName, QStringLiteral("Game Beta (Japan)"));
}

void TestDatCache::lookupMissReturnsNullopt() {
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());

  // Hash not present in the DAT.
  auto miss = store.lookup(source.value(), QStringLiteral("ffffffffffffffffffffffffffffffff"),
                           QStringLiteral("ffffffffffffffffffffffffffffffffffffffff"),
                           QStringLiteral("ffffffff"));
  QVERIFY(!miss.has_value());
  // All-empty hashes shouldn't accidentally match something.
  auto empty = store.lookup(source.value(), QString(), QString(), QString());
  QVERIFY(!empty.has_value());
}

void TestDatCache::editedDatIsReIngestedOnMtimeChange() {
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());
  auto first = store.openOrIngest(datPath());
  QVERIFY(first.isOk());
  const qint64 firstId = first.value().id;

  // Force a different mtime so the cache detects the edit. Re-writing
  // the file in a QTRY loop until QFileInfo reports a fresh mtime is
  // near-instant on msec-resolution filesystems (ext4 default, btrfs,
  // APFS) — the very first write ticks the stamp. On coarser-resolution
  // filesystems (FAT32 / old HFS+) we loop until the next-second tick
  // (typically <1s, bounded by the QTRY timeout). Replaces a flat 1.1s
  // sleep that paid worst-case latency on every CI run.
  const QDateTime originalMtime = QFileInfo(datPath()).lastModified();
  auto stampAdvanced = [&]() -> bool {
    writeDat(datPath(), kLogiqxDatEdited);
    return QFileInfo(datPath()).lastModified() != originalMtime;
  };
  QTRY_VERIFY_WITH_TIMEOUT(stampAdvanced(), 5000);

  auto reingest = store.openOrIngest(datPath());
  QVERIFY(reingest.isOk());
  // Eviction + re-insert: should not be the same source id (the
  // INSERT picks the next AUTOINCREMENT value).
  QVERIFY(reingest.value().id != firstId);
  QCOMPARE(reingest.value().recordCount, 1);

  // The new hash should be findable.
  auto rec = store.lookup(reingest.value(), QString(),
                          QStringLiteral("cccccccccccccccccccccccccccccccccccccccc"), QString());
  QVERIFY(rec.has_value());
  QCOMPARE(rec->gameName, QStringLiteral("Game Alpha (USA) Rev 1"));

  // The old hash from the pre-edit DAT must be gone — confirms the
  // stale-eviction actually deleted the cascading record rows.
  auto staleRec =
      store.lookup(reingest.value(), QString(),
                   QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), QString());
  QVERIFY(!staleRec.has_value());
}

void TestDatCache::emptyPathErrors() {
  DatCache::Store store(cachePath());
  auto result = store.openOrIngest(QString());
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestDatCache::missingFileErrors() {
  DatCache::Store store(cachePath());
  auto result = store.openOrIngest(m_dir.filePath("does-not-exist.dat"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestDatCache::unparseableDatPropagatesError() {
  // Garbage file → DatLookup::parseDat returns Unknown-dialect error
  // which the cache should surface rather than swallow.
  writeDat(datPath(), R"(not even close to xml)");
  DatCache::Store store(cachePath());
  auto result = store.openOrIngest(datPath());
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestDatCache::truncatedClrMameProDatRefusedByIngest() {
  // A partial clrmamepro download cut mid-game-block must be refused at
  // ingest: a committed partial parse would be served sticky on (path, mtime)
  // and skew Have/Miss audit verdicts until the file is touched. The XML
  // dialects already reject truncation via the closing-root-tag check
  // (Kartend-u4sdu); the text dialect's guard is paren balance.
  const QByteArray full(kClrMameProDat);
  const int cut = full.indexOf("crc");
  QVERIFY(cut > 0); // cut inside the rom block: game + rom blocks left open
  {
    QFile f(datPath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(full.left(cut));
  }
  DatCache::Store store(cachePath());
  QVERIFY(store.isOpen());
  auto result = store.openOrIngest(datPath());
  QVERIFY2(result.isError(), "a truncated clrmamepro DAT must not be ingested");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
  // Nothing was committed: the refused source must not be peekable.
  QVERIFY(!store.peek(datPath()).has_value());

  // The repaired (complete) file ingests normally through the same path.
  writeDat(datPath(), kClrMameProDat);
  auto fixed = store.openOrIngest(datPath());
  QVERIFY(fixed.isOk());
  QCOMPARE(fixed.value().dialect, DatLookup::Dialect::ClrMamePro);
  QCOMPARE(fixed.value().recordCount, 1);
}

void TestDatCache::cacheSurvivesStoreReopen() {
  // Persistence test: ingest, close the Store, reopen against the
  // same DB file, and verify the cached records come back without a
  // re-ingest. This is the whole point of the on-disk cache.
  writeDat(datPath(), kLogiqxDat);
  qint64 originalId = -1;
  {
    DatCache::Store store(cachePath());
    auto source = store.openOrIngest(datPath());
    QVERIFY(source.isOk());
    originalId = source.value().id;
  }
  // New Store, same DB file. openOrIngest must return the existing
  // row id — not a fresh INSERT.
  DatCache::Store reopened(cachePath());
  QVERIFY(reopened.isOpen());
  auto source = reopened.openOrIngest(datPath());
  QVERIFY(source.isOk());
  QCOMPARE(source.value().id, originalId);
  QCOMPARE(source.value().recordCount, 2);

  // Records should still be queryable.
  auto rec = reopened.lookup(source.value(), QString(),
                             QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), QString());
  QVERIFY(rec.has_value());
  QCOMPARE(rec->gameName, QStringLiteral("Game Alpha (USA)"));
}

void TestDatCache::clearAllWipesEverything() {
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());

  store.clearAll();

  // After clearAll the old handle is dangling — a lookup must miss.
  auto miss = store.lookup(source.value(), QString(),
                           QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), QString());
  QVERIFY(!miss.has_value());

  // Re-ingesting should work (fresh source id, fresh records).
  auto fresh = store.openOrIngest(datPath());
  QVERIFY(fresh.isOk());
  QCOMPARE(fresh.value().recordCount, 2);
}

void TestDatCache::multipleSourcesCoexistInOneCache() {
  // The multi-DAT feature relies on the cache supporting several
  // ingested sources side-by-side. Each path gets its own row in
  // dat_sources, and lookup against a given handle only returns
  // records that belong to that handle's source_id. This is the
  // contract the provider iterates over for collections with
  // multiple datFilePaths.
  const QString pathA = m_dir.filePath("a.dat");
  const QString pathB = m_dir.filePath("b.dat");
  writeDat(pathA, kLogiqxDat);
  writeDat(pathB, kMameDat);

  DatCache::Store store(cachePath());
  auto sourceA = store.openOrIngest(pathA);
  auto sourceB = store.openOrIngest(pathB);
  QVERIFY(sourceA.isOk());
  QVERIFY(sourceB.isOk());
  QVERIFY(sourceA.value().id != sourceB.value().id);
  QCOMPARE(sourceA.value().dialect, DatLookup::Dialect::Logiqx);
  QCOMPARE(sourceB.value().dialect, DatLookup::Dialect::Mame);

  // A hash that exists in A but not B must only be found via the A
  // handle — verifies the source_id filter on the SELECT is honoured.
  const QString aSha1 = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  QVERIFY(store.lookup(sourceA.value(), QString(), aSha1, QString()).has_value());
  QVERIFY(!store.lookup(sourceB.value(), QString(), aSha1, QString()).has_value());

  // And the reverse for a MAME-only hash from B.
  const QString bSha1 = QStringLiteral("e87e059c5be45753f7e9f33dff851f16d6751181");
  QVERIFY(store.lookup(sourceB.value(), QString(), bSha1, QString()).has_value());
  QVERIFY(!store.lookup(sourceA.value(), QString(), bSha1, QString()).has_value());
}

void TestDatCache::peekReturnsCachedRowWithoutIngesting() {
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());

  auto ingested = store.openOrIngest(datPath());
  QVERIFY(ingested.isOk());

  // Once ingested, peek serves the same row read-only.
  auto peeked = store.peek(datPath());
  QVERIFY(peeked.has_value());
  QCOMPARE(peeked->id, ingested.value().id);
  QCOMPARE(peeked->dialect, DatLookup::Dialect::Logiqx);
  QCOMPARE(peeked->recordCount, 2);

  // peek reports the row even when the file has changed on disk since
  // ingest — the caller reads the mtime delta as staleness, and only
  // openOrIngest re-ingests.
  const QDateTime originalMtime = QFileInfo(datPath()).lastModified();
  auto stampAdvanced = [&]() -> bool {
    writeDat(datPath(), kLogiqxDatEdited);
    return QFileInfo(datPath()).lastModified() != originalMtime;
  };
  QTRY_VERIFY_WITH_TIMEOUT(stampAdvanced(), 5000);
  auto stale = store.peek(datPath());
  QVERIFY(stale.has_value());
  QCOMPARE(stale->id, ingested.value().id);
  QCOMPARE(stale->recordCount, 2); // still the pre-edit count: nothing re-ingested
  QVERIFY(stale->mtimeUnixMs != QFileInfo(datPath()).lastModified().toMSecsSinceEpoch());
}

void TestDatCache::peekMissesUningestedAndStaysCheap() {
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());

  // Never ingested → nullopt, and — the whole point of peek — the DAT
  // must NOT get parsed/ingested as a side effect.
  QVERIFY(!store.peek(datPath()).has_value());
  QVERIFY(!store.peek(datPath()).has_value()); // still a miss: no ingest happened

  // Defensive paths: empty and missing files are misses, not errors.
  QVERIFY(!store.peek(QString()).has_value());
  QVERIFY(!store.peek(m_dir.filePath("does-not-exist.dat")).has_value());
}

void TestDatCache::headerMetadataRoundTripsThroughCache() {
  constexpr const char *withHeader = R"xml(<?xml version="1.0"?>
<datafile>
  <header>
    <name>Reference Video Set</name>
    <description>Reference Video Set - curated</description>
    <version>3.1</version>
  </header>
  <game name="Clip One">
    <rom name="Clip One.mkv" size="1" crc="deadbeef"/>
  </game>
</datafile>
)xml";
  writeDat(datPath(), withHeader);
  DatCache::Store store(cachePath());

  // Populated at ingest…
  auto ingested = store.openOrIngest(datPath());
  QVERIFY(ingested.isOk());
  QCOMPARE(ingested.value().headerName, QStringLiteral("Reference Video Set"));
  QCOMPARE(ingested.value().headerDescription, QStringLiteral("Reference Video Set - curated"));
  QCOMPARE(ingested.value().headerVersion, QStringLiteral("3.1"));

  // …served on the cache-hit path…
  auto hit = store.openOrIngest(datPath());
  QVERIFY(hit.isOk());
  QCOMPARE(hit.value().headerName, QStringLiteral("Reference Video Set"));

  // …and on the read-only peek. The header is what the DAT-library
  // matcher identifies catalogues by, so all three access paths agree.
  auto peeked = store.peek(datPath());
  QVERIFY(peeked.has_value());
  QCOMPARE(peeked->headerName, QStringLiteral("Reference Video Set"));
  QCOMPARE(peeked->headerVersion, QStringLiteral("3.1"));

  // A headerless DAT round-trips as empty strings, not NULLs/errors. Use a
  // DISTINCT path so the (path, mtime) cache key can't serve the Logiqx row
  // above when the rewrite lands in the same millisecond (the cache keys on
  // path+mtime, not contents).
  const QString mamePath = m_dir.filePath(QStringLiteral("mame.dat"));
  writeDat(mamePath, kMameDat); // no <header> — synthesised name + build version
  auto mame = store.openOrIngest(mamePath);
  QVERIFY(mame.isOk());
  QCOMPARE(mame.value().headerName, QStringLiteral("MAME"));
  QCOMPARE(mame.value().headerVersion, QStringLiteral("0.250"));
  QVERIFY(mame.value().headerDescription.isEmpty());
}

void TestDatCache::schemaBumpWipesOldCache() {
  // The cache is throwaway: a user_version mismatch (older build's file)
  // must drop the tables and rebuild, never attempt per-version migration.
  writeDat(datPath(), kLogiqxDat);
  {
    DatCache::Store store(cachePath());
    QVERIFY(store.openOrIngest(datPath()).isOk());
    QVERIFY(store.peek(datPath()).has_value());
  }
  {
    // Simulate a previous-version cache file by rewinding user_version.
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("test_datcache_rewind"));
    db.setDatabaseName(cachePath());
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 1")));
    db.close();
  }
  QSqlDatabase::removeDatabase(QStringLiteral("test_datcache_rewind"));

  DatCache::Store reopened(cachePath());
  QVERIFY(reopened.isOpen());
  // The old ingest is gone (tables were wiped on the version mismatch)…
  QVERIFY(!reopened.peek(datPath()).has_value());
  // …and a fresh ingest works against the rebuilt schema.
  QVERIFY(reopened.openOrIngest(datPath()).isOk());
  QVERIFY(reopened.peek(datPath()).has_value());
}

void TestDatCache::invalidSourceHandleSkipsLookup() {
  // Defensive: a default-constructed CachedSource (id=-1) should
  // return nullopt rather than spew SQL errors.
  DatCache::Store store(cachePath());
  DatCache::CachedSource invalid;
  auto rec = store.lookup(invalid, QString(),
                          QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), QString());
  QVERIFY(!rec.has_value());
}

void TestDatCache::forEachRecordStreamsAllRecords() {
  // forEachRecord backs the auditor's "what's in the catalogue" pass —
  // unlike lookup() it must surface every record, not just hash hits.
  writeDat(datPath(), kLogiqxDat);
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());

  QList<DatLookup::DatRecord> collected;
  const bool ok = store.forEachRecord(source.value(),
                                      [&](const DatLookup::DatRecord &r) { collected.append(r); });
  QVERIFY(ok);
  QCOMPARE(collected.size(), 2);

  QStringList names;
  for (const DatLookup::DatRecord &r : collected) {
    names << r.gameName;
  }
  QVERIFY(names.contains(QStringLiteral("Game Alpha (USA)")));
  QVERIFY(names.contains(QStringLiteral("Game Beta (Japan)")));

  // Fields carry through intact (name/size/crc) so the audit can compare
  // and rename against them.
  bool sawAlpha = false;
  for (const DatLookup::DatRecord &r : collected) {
    if (r.gameName == QStringLiteral("Game Alpha (USA)")) {
      sawAlpha = true;
      QCOMPARE(r.romName, QStringLiteral("Game Alpha (USA).bin"));
      QCOMPARE(r.crc, QStringLiteral("deadbeef"));
      QCOMPARE(r.size, qint64(32768));
    }
  }
  QVERIFY(sawAlpha);
}

void TestDatCache::forEachRecordInvalidSourceReturnsFalse() {
  // A default-constructed (invalid) source must not run the callback or
  // spew SQL errors — mirrors the lookup() defensive path.
  DatCache::Store store(cachePath());
  DatCache::CachedSource invalid;
  int calls = 0;
  const bool ok = store.forEachRecord(invalid, [&](const DatLookup::DatRecord &) { ++calls; });
  QVERIFY(!ok);
  QCOMPARE(calls, 0);
}

void TestDatCache::cloneOfRoundTripsThroughCache() {
  // cloneof (Kartend-m6qsb.13) must survive ingest -> sqlite -> read back, both
  // via lookup() and forEachRecord(), so clone provenance reaches the catalogue.
  writeDat(datPath(), R"xml(<?xml version="1.0"?>
<mame>
  <machine name="parent"><description>Parent</description>
    <rom name="p.rom" size="1" crc="aaaaaaaa"/></machine>
  <machine name="clone" cloneof="parent"><description>Clone</description>
    <rom name="c.rom" size="1" crc="bbbbbbbb"/></machine>
</mame>)xml");
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());

  auto clone = store.lookup(source.value(), QString(), QString(), QStringLiteral("bbbbbbbb"));
  QVERIFY(clone.has_value());
  QCOMPARE(clone->cloneOf, QStringLiteral("parent"));
  auto parent = store.lookup(source.value(), QString(), QString(), QStringLiteral("aaaaaaaa"));
  QVERIFY(parent.has_value());
  QVERIFY(parent->cloneOf.isEmpty());

  // Kartend-m6qsb.29: setId round-trips, and the clone's cloneof resolves to the
  // parent's setId ("parent", the name=) — NOT its gameName (the description
  // "Parent"). This is exactly why clone resolution must key on setId.
  QCOMPARE(clone->setId, QStringLiteral("clone"));
  QCOMPARE(parent->setId, QStringLiteral("parent"));
  QCOMPARE(clone->cloneOf, parent->setId);
  QVERIFY(clone->cloneOf != parent->gameName);

  QHash<QString, QString> cloneByCrc; // crc -> cloneOf, gathered via the stream
  store.forEachRecord(source.value(),
                      [&](const DatLookup::DatRecord &r) { cloneByCrc.insert(r.crc, r.cloneOf); });
  QCOMPARE(cloneByCrc.value(QStringLiteral("bbbbbbbb")), QStringLiteral("parent"));
  QVERIFY(cloneByCrc.value(QStringLiteral("aaaaaaaa")).isEmpty());
}

void TestDatCache::miaRoundTripsThroughCache() {
  // mia (Kartend-34lab) must survive ingest -> sqlite -> read back via both
  // lookup() and forEachRecord(), so the MIA flag reaches the catalogue and the
  // browser's salmon rollups.
  writeDat(datPath(), R"xml(<?xml version="1.0"?>
<mame>
  <machine name="here"><description>Here</description>
    <rom name="h.rom" size="1" crc="aaaaaaaa"/></machine>
  <machine name="gone" mia="yes"><description>Gone</description>
    <rom name="g.rom" size="1" crc="bbbbbbbb"/></machine>
</mame>)xml");
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());

  auto gone = store.lookup(source.value(), QString(), QString(), QStringLiteral("bbbbbbbb"));
  QVERIFY(gone.has_value());
  QVERIFY(gone->mia);
  auto here = store.lookup(source.value(), QString(), QString(), QStringLiteral("aaaaaaaa"));
  QVERIFY(here.has_value());
  QVERIFY(!here->mia);

  QHash<QString, bool> miaByCrc;
  store.forEachRecord(source.value(),
                      [&](const DatLookup::DatRecord &r) { miaByCrc.insert(r.crc, r.mia); });
  QVERIFY(miaByCrc.value(QStringLiteral("bbbbbbbb")));
  QVERIFY(!miaByCrc.value(QStringLiteral("aaaaaaaa")));
}

void TestDatCache::machineRecordsRoundTripThroughReopen() {
  // Normal MAME ingest -> close the store -> reopen the same on-disk file ->
  // case-insensitive set-name lookup returns every machine field intact.
  writeDat(datPath(), kMameMachinesDat);
  qint64 sourceId = -1;
  {
    DatCache::Store store(cachePath());
    auto source = store.openOrIngest(datPath());
    QVERIFY(source.isOk());
    QCOMPARE(source.value().dialect, DatLookup::Dialect::Mame);
    // Per-rom records are unchanged: one per ROM, ROM-less machines add none.
    QCOMPARE(source.value().recordCount, 3);
    sourceId = source.value().id;
  }
  QCOMPARE(countMachineRows(cachePath(), sourceId), 5);

  DatCache::Store store(cachePath());
  const auto peeked = store.peek(datPath());
  QVERIFY(peeked.has_value());
  QCOMPARE(peeked->id, sourceId);
  // Still a cache hit — the reopen didn't re-ingest.
  auto again = store.openOrIngest(datPath());
  QVERIFY(again.isOk());
  QCOMPARE(again.value().id, sourceId);
  const QList<DatCache::CachedSource> scope{*peeked};

  const auto pac = store.machinesForSet(scope, QStringLiteral("PacMan"));
  QCOMPARE(pac.size(), 1);
  QCOMPARE(pac[0].sourceId, sourceId);
  const DatLookup::MameMachine &p = pac[0].machine;
  QCOMPARE(p.setId, QStringLiteral("pacman"));
  QCOMPARE(p.description, QStringLiteral("Pac-Man (Midway)"));
  QCOMPARE(p.cloneOf, QStringLiteral("puckman"));
  QCOMPARE(p.romOf, QStringLiteral("puckman"));
  QVERIFY(!p.isBios);
  QVERIFY(!p.isDevice);
  QVERIFY(!p.isMechanical);
  QVERIFY(p.runnable);
  QCOMPARE(p.year, QStringLiteral("1980"));
  QCOMPARE(p.manufacturer, QStringLiteral("Namco (Midway license)"));
  QCOMPARE(p.driverStatus, QStringLiteral("good"));
  QCOMPARE(p.players, 2);
  QCOMPARE(p.rotate, 90);

  const auto bios = store.machinesForSet(scope, QStringLiteral("NEOGEO"));
  QCOMPARE(bios.size(), 1);
  QVERIFY(bios[0].machine.isBios);
  QVERIFY(!bios[0].machine.isDevice);
  QCOMPARE(bios[0].machine.year, QStringLiteral("1990"));

  const auto mech = store.machinesForSet(scope, QStringLiteral("flipper"));
  QCOMPARE(mech.size(), 1);
  QVERIFY(mech[0].machine.isMechanical);

  // ROM-less device: present as a machine, absent from the per-rom records.
  const auto dev = store.machinesForSet(scope, QStringLiteral("Z80"));
  QCOMPARE(dev.size(), 1);
  QCOMPARE(dev[0].machine.setId, QStringLiteral("z80"));
  QCOMPARE(dev[0].machine.description, QStringLiteral("Zilog Z80"));
  QVERIFY(dev[0].machine.isDevice);
  QVERIFY(!dev[0].machine.runnable);
  QVERIFY(store.recordsForGame(*peeked, QStringLiteral("Zilog Z80")).isEmpty());

  // ROM-less machine with nothing declared keeps the DTD defaults.
  const auto bare = store.machinesForSet(scope, QStringLiteral("bare"));
  QCOMPARE(bare.size(), 1);
  const DatLookup::MameMachine &b = bare[0].machine;
  QCOMPARE(b.setId, QStringLiteral("bare"));
  QVERIFY(b.description.isEmpty());
  QVERIFY(b.cloneOf.isEmpty());
  QVERIFY(b.romOf.isEmpty());
  QVERIFY(!b.isBios);
  QVERIFY(!b.isDevice);
  QVERIFY(!b.isMechanical);
  QVERIFY(b.runnable);
  QVERIFY(b.year.isEmpty());
  QVERIFY(b.manufacturer.isEmpty());
  QVERIFY(b.driverStatus.isEmpty());
  QCOMPARE(b.players, -1);
  QCOMPARE(b.rotate, -1);

  // Per-rom hash lookup still answers as before.
  auto rec = store.lookup(*peeked, QString(), QString(), QStringLiteral("c1e6ab10"));
  QVERIFY(rec.has_value());
  QCOMPARE(rec->gameName, QStringLiteral("Pac-Man (Midway)"));
  QVERIFY(store.machinesForSet(scope, QStringLiteral("missing")).isEmpty());
}

void TestDatCache::machineLookupIsScopedToSuppliedSources() {
  // Two DATs carry the same set name; a lookup only ever reads rows owned by
  // the sources the caller passed, in the caller's order.
  const QString pathA = m_dir.filePath("machines_a.dat");
  const QString pathB = m_dir.filePath("machines_b.dat");
  const QString pathL = m_dir.filePath("machines_logiqx.dat");
  writeDat(pathA, kMameMachinesDat);
  writeDat(pathB, kMameMachinesDatOther);
  writeDat(pathL, kLogiqxDat);
  DatCache::Store store(cachePath());
  auto a = store.openOrIngest(pathA);
  auto b = store.openOrIngest(pathB);
  auto l = store.openOrIngest(pathL);
  QVERIFY(a.isOk());
  QVERIFY(b.isOk());
  QVERIFY(l.isOk());
  const DatCache::CachedSource sa = a.value();
  const DatCache::CachedSource sb = b.value();

  const auto onlyA = store.machinesForSet({sa}, QStringLiteral("pacman"));
  QCOMPARE(onlyA.size(), 1);
  QCOMPARE(onlyA[0].sourceId, sa.id);
  QCOMPARE(onlyA[0].machine.description, QStringLiteral("Pac-Man (Midway)"));

  const auto onlyB = store.machinesForSet({sb}, QStringLiteral("pacman"));
  QCOMPARE(onlyB.size(), 1);
  QCOMPARE(onlyB[0].sourceId, sb.id);
  QCOMPARE(onlyB[0].machine.setId, QStringLiteral("PacMan"));
  QCOMPARE(onlyB[0].machine.description, QStringLiteral("Pac-Man (other DAT)"));
  QCOMPARE(onlyB[0].machine.year, QStringLiteral("1981"));

  const auto both = store.machinesForSet({sb, sa}, QStringLiteral("PACMAN"));
  QCOMPARE(both.size(), 2);
  QCOMPARE(both[0].sourceId, sb.id);
  QCOMPARE(both[1].sourceId, sa.id);

  // A set only B carries never leaks into an A-scoped (or Logiqx-scoped) lookup.
  QVERIFY(store.machinesForSet({sa}, QStringLiteral("onlyinb")).isEmpty());
  QVERIFY(store.machinesForSet({l.value()}, QStringLiteral("onlyinb")).isEmpty());
  QCOMPARE(store.machinesForSet({sa, sb}, QStringLiteral("onlyinb")).size(), 1);
  QVERIFY(store.machinesForSet({}, QStringLiteral("pacman")).isEmpty());
  QVERIFY(store.machinesForSet({DatCache::CachedSource{}}, QStringLiteral("pacman")).isEmpty());
  QVERIFY(store.machinesForSet({sa, sb}, QString()).isEmpty());
}

void TestDatCache::machineRowsFollowReplacementAndClearAll() {
  // Machine rows share their DAT's lifecycle: an mtime-driven re-ingest drops
  // the old source's machines (no stale rows), clearAll() drops all of them.
  writeDat(datPath(), kMameMachinesDat);
  DatCache::Store store(cachePath());
  auto first = store.openOrIngest(datPath());
  QVERIFY(first.isOk());
  const DatCache::CachedSource oldSource = first.value();
  QCOMPARE(countMachineRows(cachePath(), oldSource.id), 5);

  const QDateTime originalMtime = QFileInfo(datPath()).lastModified();
  auto stampAdvanced = [&]() -> bool {
    writeDat(datPath(), kMameMachinesDatEdited);
    return QFileInfo(datPath()).lastModified() != originalMtime;
  };
  QTRY_VERIFY_WITH_TIMEOUT(stampAdvanced(), 5000);

  auto second = store.openOrIngest(datPath());
  QVERIFY(second.isOk());
  const DatCache::CachedSource newSource = second.value();
  QVERIFY(newSource.id != oldSource.id);
  QCOMPARE(countMachineRows(cachePath(), oldSource.id), 0);
  QCOMPARE(countMachineRows(cachePath()), 1);

  const auto pac = store.machinesForSet({newSource}, QStringLiteral("pacman"));
  QCOMPARE(pac.size(), 1);
  QCOMPARE(pac[0].machine.description, QStringLiteral("Pac-Man (Midway, rev 2)"));
  QVERIFY(pac[0].machine.cloneOf.isEmpty());
  QVERIFY(store.machinesForSet({newSource}, QStringLiteral("z80")).isEmpty());
  QVERIFY(store.machinesForSet({oldSource}, QStringLiteral("z80")).isEmpty());
  QVERIFY(store.machinesForSet({oldSource}, QStringLiteral("pacman")).isEmpty());

  store.clearAll();
  QCOMPARE(countMachineRows(cachePath()), 0);
  QVERIFY(store.machinesForSet({newSource}, QStringLiteral("pacman")).isEmpty());
}

void TestDatCache::previousSchemaCacheReingestsMachines() {
  // A cache written by the previous schema (v5: no dat_machines table) must be
  // wiped on open so the MAME DAT re-ingests and gains its machine rows.
  writeDat(datPath(), kMameMachinesDat);
  {
    DatCache::Store store(cachePath());
    QVERIFY(store.openOrIngest(datPath()).isOk());
  }
  const QString conn = QStringLiteral("test_datcache_v5");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(cachePath());
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("DROP TABLE dat_machines")));
    QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 5")));
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);

  DatCache::Store reopened(cachePath());
  QVERIFY(reopened.isOpen());
  QVERIFY(!reopened.peek(datPath()).has_value());
  auto source = reopened.openOrIngest(datPath());
  QVERIFY(source.isOk());
  QCOMPARE(source.value().recordCount, 3);
  const auto dev = reopened.machinesForSet({source.value()}, QStringLiteral("z80"));
  QCOMPARE(dev.size(), 1);
  QVERIFY(dev[0].machine.isDevice);
  QCOMPARE(countMachineRows(cachePath()), 5);
}

void TestDatCache::nonMameIngestStoresNoMachines() {
  // Logiqx and clrmamepro ingestion is unchanged and writes no machine rows.
  const QString pathL = m_dir.filePath("plain_logiqx.dat");
  const QString pathC = m_dir.filePath("plain_cmp.dat");
  writeDat(pathL, kLogiqxDat);
  writeDat(pathC, kClrMameProDat);
  DatCache::Store store(cachePath());
  auto l = store.openOrIngest(pathL);
  auto c = store.openOrIngest(pathC);
  QVERIFY(l.isOk());
  QVERIFY(c.isOk());
  QCOMPARE(l.value().recordCount, 2);
  QCOMPARE(c.value().recordCount, 1);
  QCOMPARE(countMachineRows(cachePath()), 0);
  QVERIFY(
      store.machinesForSet({l.value(), c.value()}, QStringLiteral("Game Alpha (USA)")).isEmpty());
  QVERIFY(store
              .lookup(l.value(), QString(),
                      QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), QString())
              .has_value());
}

void TestDatCache::hideRuleFlagsBiosDeviceAndNonRunnable() {
  // Each exclusion flag qualifies on its own, read back from the on-disk cache
  // after a reopen, with the set name matched case-insensitively.
  writeDat(datPath(), kMameHideRulesDat);
  {
    DatCache::Store store(cachePath());
    QVERIFY(store.openOrIngest(datPath()).isOk());
  }
  DatCache::Store store(cachePath());
  const auto source = store.peek(datPath());
  QVERIFY(source.has_value());
  const QList<DatCache::CachedSource> scope{*source};

  QVERIFY(store.isHideableMachineSet(scope, QStringLiteral("neogeo")));
  QVERIFY(store.isHideableMachineSet(scope, QStringLiteral("NeoGeo")));
  QVERIFY(store.isHideableMachineSet(scope, QStringLiteral("NEOGEO")));
  // Device that the DAT leaves runnable: isdevice alone is enough.
  const auto dev = store.machinesForSet(scope, QStringLiteral("ym2610"));
  QCOMPARE(dev.size(), 1);
  QVERIFY(dev[0].machine.isDevice);
  QVERIFY(dev[0].machine.runnable);
  QVERIFY(store.isHideableMachineSet(scope, QStringLiteral("YM2610")));
  // runnable="no" with no BIOS / device flag.
  const auto nr = store.machinesForSet(scope, QStringLiteral("notrun"));
  QCOMPARE(nr.size(), 1);
  QVERIFY(!nr[0].machine.isBios);
  QVERIFY(!nr[0].machine.isDevice);
  QVERIFY(!nr[0].machine.runnable);
  QVERIFY(store.isHideableMachineSet(scope, QStringLiteral("NotRun")));
}

void TestDatCache::hideRuleKeepsUnflaggedAndUnknownSets() {
  // Playable, mechanical, non-working and default-only machines stay visible,
  // as do names the DAT does not carry at all.
  writeDat(datPath(), kMameHideRulesDat);
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());
  const QList<DatCache::CachedSource> scope{source.value()};

  QVERIFY(!store.isHideableMachineSet(scope, QStringLiteral("pacman")));
  QVERIFY(!store.isHideableMachineSet(scope, QStringLiteral("PACMAN")));
  const auto mech = store.machinesForSet(scope, QStringLiteral("flipper"));
  QCOMPARE(mech.size(), 1);
  QVERIFY(mech[0].machine.isMechanical);
  QVERIFY(!store.isHideableMachineSet(scope, QStringLiteral("flipper")));
  const auto broken = store.machinesForSet(scope, QStringLiteral("broken"));
  QCOMPARE(broken.size(), 1);
  QCOMPARE(broken[0].machine.driverStatus, QStringLiteral("preliminary"));
  QVERIFY(!store.isHideableMachineSet(scope, QStringLiteral("broken")));
  QCOMPARE(store.machinesForSet(scope, QStringLiteral("bare")).size(), 1);
  QVERIFY(!store.isHideableMachineSet(scope, QStringLiteral("bare")));

  QVERIFY(!store.isHideableMachineSet(scope, QStringLiteral("missing")));
  QVERIFY(!store.isHideableMachineSet(scope, QString()));
  // No source, or only an invalid handle: nothing to decide on, nothing hidden.
  QVERIFY(!store.isHideableMachineSet({}, QStringLiteral("neogeo")));
  QVERIFY(!store.isHideableMachineSet({DatCache::CachedSource{}}, QStringLiteral("neogeo")));
}

void TestDatCache::hideRuleIsScopedToSuppliedSources() {
  // A flag only in a DAT the caller did not pass never hides the same-named
  // ordinary set; passing that DAT is enough for its flag to count. Non-MAME
  // DATs carry no machine flags and never qualify a set.
  const QString pathA = m_dir.filePath("hide_a.dat");
  const QString pathB = m_dir.filePath("hide_b.dat");
  const QString pathL = m_dir.filePath("hide_logiqx.dat");
  const QString pathC = m_dir.filePath("hide_cmp.dat");
  writeDat(pathA, kMameHideRulesDat);
  writeDat(pathB, kMameHideRulesDatOther);
  writeDat(pathL, kLogiqxDat);
  writeDat(pathC, kClrMameProDat);
  DatCache::Store store(cachePath());
  auto a = store.openOrIngest(pathA);
  auto b = store.openOrIngest(pathB);
  auto l = store.openOrIngest(pathL);
  auto c = store.openOrIngest(pathC);
  QVERIFY(a.isOk());
  QVERIFY(b.isOk());
  QVERIFY(l.isOk());
  QVERIFY(c.isOk());
  const DatCache::CachedSource sa = a.value();
  const DatCache::CachedSource sb = b.value();

  QVERIFY(!store.isHideableMachineSet({sa}, QStringLiteral("pacman")));
  QVERIFY(!store.isHideableMachineSet({sa}, QStringLiteral("flipper")));
  QVERIFY(store.isHideableMachineSet({sb}, QStringLiteral("pacman")));
  QVERIFY(store.isHideableMachineSet({sb}, QStringLiteral("Flipper")));
  // Both attached: an explicit flag in either supplied MAME source suffices.
  QVERIFY(store.isHideableMachineSet({sa, sb}, QStringLiteral("pacman")));
  QVERIFY(store.isHideableMachineSet({sb, sa}, QStringLiteral("flipper")));
  // B lacks neogeo; A's flag does not leak into a B-only scope.
  QVERIFY(!store.isHideableMachineSet({sb}, QStringLiteral("neogeo")));

  // Unrelated (non-MAME) DATs alone never hide, even for a name A flags.
  const QList<DatCache::CachedSource> plain{l.value(), c.value()};
  QVERIFY(!store.isHideableMachineSet(plain, QStringLiteral("neogeo")));
  QVERIFY(!store.isHideableMachineSet(plain, QStringLiteral("Game Alpha (USA)")));
  QVERIFY(!store.isHideableMachineSet({l.value(), sa}, QStringLiteral("pacman")));
  QVERIFY(store.isHideableMachineSet({l.value(), sa}, QStringLiteral("neogeo")));
}

void TestDatCache::hideRuleLeavesCacheUntouched() {
  // Deciding is read-only: the stored machine rows, the source row and the
  // records the lookup returns are identical before and after.
  writeDat(datPath(), kMameHideRulesDat);
  DatCache::Store store(cachePath());
  auto source = store.openOrIngest(datPath());
  QVERIFY(source.isOk());
  const QList<DatCache::CachedSource> scope{source.value()};
  const QStringList rowsBefore = dumpMachineRows(cachePath());
  QCOMPARE(rowsBefore.size(), 7);
  const auto biosBefore = store.machinesForSet(scope, QStringLiteral("neogeo"));
  QCOMPARE(biosBefore.size(), 1);

  const QStringList names{QStringLiteral("pacman"),  QStringLiteral("neogeo"),
                          QStringLiteral("ym2610"),  QStringLiteral("notrun"),
                          QStringLiteral("flipper"), QStringLiteral("broken"),
                          QStringLiteral("bare"),    QStringLiteral("missing")};
  for (const QString &name : names) {
    (void)store.isHideableMachineSet(scope, name);
    (void)store.isHideableMachineSet(scope, name.toUpper());
  }

  QCOMPARE(dumpMachineRows(cachePath()), rowsBefore);
  const auto biosAfter = store.machinesForSet(scope, QStringLiteral("neogeo"));
  QCOMPARE(biosAfter.size(), 1);
  QCOMPARE(biosAfter[0].machine.setId, biosBefore[0].machine.setId);
  QVERIFY(biosAfter[0].machine.isBios);
  QVERIFY(biosAfter[0].machine.runnable);
  const auto peeked = store.peek(datPath());
  QVERIFY(peeked.has_value());
  QCOMPARE(peeked->id, source.value().id);
  QCOMPARE(peeked->mtimeUnixMs, source.value().mtimeUnixMs);
  QCOMPARE(peeked->recordCount, source.value().recordCount);
  // Still a cache hit — nothing the rule did forced a re-ingest.
  auto again = store.openOrIngest(datPath());
  QVERIFY(again.isOk());
  QCOMPARE(again.value().id, source.value().id);
}

QTEST_MAIN(TestDatCache)
#include "test_datcache.moc"
