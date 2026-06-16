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

QTEST_MAIN(TestDatCache)
#include "test_datcache.moc"
