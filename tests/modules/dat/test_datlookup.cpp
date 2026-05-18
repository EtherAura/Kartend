/**
 * @file test_datlookup.cpp
 * @brief Unit tests for DAT-file parsers + indexed lookup store.
 *
 * Covers the Logiqx XML shape (No-Intro / Redump / TOSEC), the MAME
 * listxml dialect, dialect auto-detection from the root element, a
 * few defensive parsing cases (mixed-case hashes, missing hash
 * fields, malformed XML), the indexed-store lookup priority order
 * (sha1 → md5 → crc), and the convenience file-loader's error
 * paths.
 */

#include "datlookup.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestDatLookup : public QObject {
  Q_OBJECT

private slots:
  void parsesMinimalNoIntroSnippet();
  void normalisesMixedCaseHashes();
  void skipsEntriesWithoutHashes();
  void parseErrorReportsLineColumn();
  void storeLooksUpBySha1MD5Crc();
  void storeLookupSkipsEmptyHashes();
  void storeLookupReturnsNullOnMiss();
  void loadStoreFromFileReadsParsesAndIndexes();
  void loadStoreFromFileReportsMissingFile();
  void loadStoreFromFileReportsEmptyPath();
  void detectsLogiqxAndMameRoots();
  void detectsUnknownRoot();
  void tosecReleaseChildIsIgnored();
  void parsesMameListXmlWithDescription();
  void parseMameSkipsNodumpEntries();
  void parseMameFallsBackToSetIdWhenNoDescription();
  void parseDatDispatchesByRoot();
  void parseDatErrorsOnUnknownRoot();
  void loadStoreFromFileTagsDialect();

private:
  QTemporaryDir m_dir;
};

namespace {
constexpr const char *kSampleDat = R"xml(<?xml version="1.0"?>
<datafile>
  <header>
    <name>Sample Console</name>
  </header>
  <game name="Game Alpha (USA)">
    <description>Game Alpha (USA)</description>
    <rom name="Game Alpha (USA).bin" size="32768"
         crc="DEADBEEF" md5="11111111111111111111111111111111"
         sha1="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"/>
  </game>
  <game name="Game Beta (Japan)">
    <description>Game Beta (Japan)</description>
    <rom name="Game Beta (Japan).bin" size="65536"
         crc="cafebabe" md5="22222222222222222222222222222222"
         sha1="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"/>
  </game>
</datafile>
)xml";
} // namespace

void TestDatLookup::parsesMinimalNoIntroSnippet() {
  auto result = DatLookup::parseNoIntroDat(QByteArray(kSampleDat));
  QVERIFY(result.isOk());
  const auto records = result.value();
  QCOMPARE(records.size(), 2);
  QCOMPARE(records[0].gameName, QStringLiteral("Game Alpha (USA)"));
  QCOMPARE(records[0].romName, QStringLiteral("Game Alpha (USA).bin"));
  QCOMPARE(records[0].size, qint64{32768});
  QCOMPARE(records[0].crc, QStringLiteral("deadbeef"));
  QCOMPARE(records[0].sha1,
           QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  QCOMPARE(records[1].gameName, QStringLiteral("Game Beta (Japan)"));
}

void TestDatLookup::normalisesMixedCaseHashes() {
  // Real-world DATs ship a mix of cases; the lookup keys are stored
  // lowercase so a caller can pass either case to lookup().
  auto result = DatLookup::parseNoIntroDat(QByteArray(kSampleDat));
  QVERIFY(result.isOk());
  for (const auto &r : result.value()) {
    QCOMPARE(r.crc, r.crc.toLower());
    QCOMPARE(r.md5, r.md5.toLower());
    QCOMPARE(r.sha1, r.sha1.toLower());
  }
}

void TestDatLookup::skipsEntriesWithoutHashes() {
  // A `<rom>` with no hash attributes can't be matched against
  // anything — it's deliberately dropped during parse rather than
  // being kept as dead weight.
  const QByteArray xml =
      QByteArrayLiteral(R"xml(<?xml version="1.0"?>
<datafile>
  <game name="With Hash">
    <rom name="x" sha1="cccccccccccccccccccccccccccccccccccccccc"/>
  </game>
  <game name="Without Hash">
    <rom name="y" size="100"/>
  </game>
</datafile>
)xml");
  auto result = DatLookup::parseNoIntroDat(xml);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0].gameName, QStringLiteral("With Hash"));
}

void TestDatLookup::parseErrorReportsLineColumn() {
  // Truncated XML — make sure the error context surfaces something
  // useful so a user pointing the picker at a corrupt file gets a
  // diagnosis, not just "failed".
  const QByteArray xml = QByteArrayLiteral(R"xml(<datafile><game name="x"><rom)xml");
  auto result = DatLookup::parseNoIntroDat(xml);
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
  QVERIFY(!result.error().details.isEmpty());
}

void TestDatLookup::storeLooksUpBySha1MD5Crc() {
  auto parsed = DatLookup::parseNoIntroDat(QByteArray(kSampleDat));
  QVERIFY(parsed.isOk());
  DatLookup::Store store(parsed.value());
  QCOMPARE(store.recordCount(), 2);

  // Direct per-kind lookups
  QVERIFY(store.lookupBySha1(
              QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")) != nullptr);
  QVERIFY(store.lookupByMd5(QStringLiteral("22222222222222222222222222222222")) != nullptr);
  QVERIFY(store.lookupByCrc(QStringLiteral("deadbeef")) != nullptr);

  // Combined lookup picks sha1 over md5 over crc — verified by
  // pointing each kind at a different record's hash and confirming
  // the sha1 wins.
  const auto *picked =
      store.lookup(QStringLiteral("11111111111111111111111111111111"),
                   QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
                   QStringLiteral("deadbeef"));
  QVERIFY(picked != nullptr);
  QCOMPARE(picked->gameName, QStringLiteral("Game Beta (Japan)"));
}

void TestDatLookup::storeLookupSkipsEmptyHashes() {
  auto parsed = DatLookup::parseNoIntroDat(QByteArray(kSampleDat));
  QVERIFY(parsed.isOk());
  DatLookup::Store store(parsed.value());
  // Only md5 supplied — should still find the right record.
  const auto *r = store.lookup(QStringLiteral("11111111111111111111111111111111"),
                               QString(), QString());
  QVERIFY(r != nullptr);
  QCOMPARE(r->gameName, QStringLiteral("Game Alpha (USA)"));
}

void TestDatLookup::storeLookupReturnsNullOnMiss() {
  auto parsed = DatLookup::parseNoIntroDat(QByteArray(kSampleDat));
  QVERIFY(parsed.isOk());
  DatLookup::Store store(parsed.value());
  QVERIFY(store.lookup(QStringLiteral("ffffffffffffffffffffffffffffffff"),
                       QStringLiteral("ffffffffffffffffffffffffffffffffffffffff"),
                       QStringLiteral("ffffffff")) == nullptr);
  QVERIFY(store.lookup(QString(), QString(), QString()) == nullptr);
}

void TestDatLookup::loadStoreFromFileReadsParsesAndIndexes() {
  const QString path = m_dir.filePath("sample.dat");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(kSampleDat);
  f.close();

  auto store = DatLookup::loadStoreFromFile(path);
  QVERIFY(store.isOk());
  QCOMPARE(store.value().recordCount(), 2);
  QVERIFY(store.value().lookupBySha1(
              QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")) != nullptr);
}

void TestDatLookup::loadStoreFromFileReportsMissingFile() {
  auto store = DatLookup::loadStoreFromFile(m_dir.filePath("nope.dat"));
  QVERIFY(store.isError());
  QCOMPARE(store.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestDatLookup::loadStoreFromFileReportsEmptyPath() {
  auto store = DatLookup::loadStoreFromFile(QString());
  QVERIFY(store.isError());
  QCOMPARE(store.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

namespace {
constexpr const char *kTosecWithRelease = R"xml(<?xml version="1.0"?>
<datafile>
  <header><name>TOSEC</name></header>
  <game name="Game Title (1992)(Acme)">
    <description>Game Title (1992)(Acme)</description>
    <release name="Game Title (1992)(Acme)" region="EU" language="en" date="1992"/>
    <rom name="Game Title (1992)(Acme).bin" size="8192"
         crc="12345678" sha1="cccccccccccccccccccccccccccccccccccccccc"/>
  </game>
</datafile>
)xml";

constexpr const char *kMameSample = R"xml(<?xml version="1.0"?>
<mame build="0.250" mameconfig="10">
  <machine name="pacman" sourcefile="pacman.cpp">
    <description>Pac-Man (Midway)</description>
    <year>1980</year>
    <manufacturer>Namco / Midway</manufacturer>
    <rom name="pacman.6e" size="4096"
         crc="c1e6ab10" sha1="e87e059c5be45753f7e9f33dff851f16d6751181"/>
    <rom name="pacman.6f" size="4096"
         crc="1a6fb2d4" sha1="674d3a7f00d8be5e38b1fdc208ebef5a92d38329"/>
    <rom name="undumped.bin" size="4096" status="nodump"
         crc="00000000" sha1="0000000000000000000000000000000000000000"/>
  </machine>
  <machine name="setidonly" sourcefile="x.cpp">
    <rom name="x.rom" size="2048"
         crc="deadc0de" sha1="dddddddddddddddddddddddddddddddddddddddd"/>
  </machine>
</mame>
)xml";
} // namespace

void TestDatLookup::detectsLogiqxAndMameRoots() {
  QCOMPARE(DatLookup::detectDialect(QByteArray(kSampleDat)),
           DatLookup::Dialect::Logiqx);
  QCOMPARE(DatLookup::detectDialect(QByteArray(kTosecWithRelease)),
           DatLookup::Dialect::Logiqx);
  QCOMPARE(DatLookup::detectDialect(QByteArray(kMameSample)),
           DatLookup::Dialect::Mame);
}

void TestDatLookup::detectsUnknownRoot() {
  // A non-DAT XML file should not be mistaken for either dialect —
  // the dispatcher uses this to surface a clear error message.
  const QByteArray xml = QByteArrayLiteral(
      R"xml(<?xml version="1.0"?><something-else><x/></something-else>)xml");
  QCOMPARE(DatLookup::detectDialect(xml), DatLookup::Dialect::Unknown);
  // Empty input also reports Unknown rather than crashing or being
  // ambiguous with a valid root.
  QCOMPARE(DatLookup::detectDialect(QByteArray()), DatLookup::Dialect::Unknown);
}

void TestDatLookup::tosecReleaseChildIsIgnored() {
  // TOSEC's optional `<release>` metadata element shouldn't break
  // the parser — and the inner `<rom>` should still come through
  // with its hashes intact.
  auto result = DatLookup::parseLogiqxDat(QByteArray(kTosecWithRelease));
  QVERIFY(result.isOk());
  const auto records = result.value();
  QCOMPARE(records.size(), 1);
  QCOMPARE(records[0].gameName, QStringLiteral("Game Title (1992)(Acme)"));
  QCOMPARE(records[0].sha1,
           QStringLiteral("cccccccccccccccccccccccccccccccccccccccc"));
  QCOMPARE(records[0].size, qint64{8192});
}

void TestDatLookup::parsesMameListXmlWithDescription() {
  // MAME listxml: gameName should be the human-readable
  // `<description>` text, not the cryptic `name=` set-id. Multiple
  // `<rom>` children should each yield a record sharing that name.
  auto result = DatLookup::parseMameListXml(QByteArray(kMameSample));
  QVERIFY(result.isOk());
  const auto records = result.value();
  // 2 dumped pacman roms + 1 setidonly rom = 3 (the nodump is dropped).
  QCOMPARE(records.size(), 3);
  QCOMPARE(records[0].gameName, QStringLiteral("Pac-Man (Midway)"));
  QCOMPARE(records[0].romName, QStringLiteral("pacman.6e"));
  QCOMPARE(records[1].gameName, QStringLiteral("Pac-Man (Midway)"));
  QCOMPARE(records[1].romName, QStringLiteral("pacman.6f"));
}

void TestDatLookup::parseMameSkipsNodumpEntries() {
  // The zero-hash `<rom status="nodump">` placeholder in the sample
  // must not appear in the output — keeping it would create a
  // bogus all-zeros hash key that would collide with any genuinely
  // zero-hash file.
  auto result = DatLookup::parseMameListXml(QByteArray(kMameSample));
  QVERIFY(result.isOk());
  for (const auto &r : result.value()) {
    QVERIFY(r.romName != QStringLiteral("undumped.bin"));
    QVERIFY(r.sha1 != QStringLiteral("0000000000000000000000000000000000000000"));
  }
}

void TestDatLookup::parseMameFallsBackToSetIdWhenNoDescription() {
  // The second machine in the sample has no `<description>` — its
  // gameName should fall back to the set-id attribute so the
  // record still has a useful display name.
  auto result = DatLookup::parseMameListXml(QByteArray(kMameSample));
  QVERIFY(result.isOk());
  const auto records = result.value();
  const auto it = std::find_if(records.begin(), records.end(),
                               [](const DatLookup::DatRecord &r) {
                                 return r.romName == QStringLiteral("x.rom");
                               });
  QVERIFY(it != records.end());
  QCOMPARE(it->gameName, QStringLiteral("setidonly"));
}

void TestDatLookup::parseDatDispatchesByRoot() {
  // parseDat should pick the right dialect parser based on the root
  // element — Logiqx for `<datafile>`, MAME for `<mame>`.
  auto logiqx = DatLookup::parseDat(QByteArray(kSampleDat));
  QVERIFY(logiqx.isOk());
  QCOMPARE(logiqx.value().size(), 2);

  auto mame = DatLookup::parseDat(QByteArray(kMameSample));
  QVERIFY(mame.isOk());
  QCOMPARE(mame.value().size(), 3);
  QCOMPARE(mame.value()[0].gameName, QStringLiteral("Pac-Man (Midway)"));
}

void TestDatLookup::parseDatErrorsOnUnknownRoot() {
  // Pointing the dispatcher at something that isn't a recognised
  // DAT file should surface a clear InvalidArgument error rather
  // than silently returning an empty record list.
  const QByteArray xml = QByteArrayLiteral(
      R"xml(<?xml version="1.0"?><not-a-dat/>)xml");
  auto result = DatLookup::parseDat(xml);
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestDatLookup::loadStoreFromFileTagsDialect() {
  // The convenience loader should tag the Store with the dialect it
  // detected so UI status strings can show "Loaded from MAME
  // listxml" without re-sniffing the file.
  const QString logiqxPath = m_dir.filePath("logiqx.dat");
  {
    QFile f(logiqxPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kSampleDat);
  }
  auto logiqx = DatLookup::loadStoreFromFile(logiqxPath);
  QVERIFY(logiqx.isOk());
  QCOMPARE(logiqx.value().detectedDialect(), DatLookup::Dialect::Logiqx);

  const QString mamePath = m_dir.filePath("mame.xml");
  {
    QFile f(mamePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kMameSample);
  }
  auto mame = DatLookup::loadStoreFromFile(mamePath);
  QVERIFY(mame.isOk());
  QCOMPARE(mame.value().detectedDialect(), DatLookup::Dialect::Mame);
  QVERIFY(mame.value().lookupBySha1(
              QStringLiteral("e87e059c5be45753f7e9f33dff851f16d6751181")) != nullptr);
}

QTEST_MAIN(TestDatLookup)
#include "test_datlookup.moc"
