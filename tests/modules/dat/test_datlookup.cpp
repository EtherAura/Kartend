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

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
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
  void storeRecordsExposesFullCatalogue();
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
  void probeHeaderReadsLogiqxHeaderFields();
  void probeHeaderHandlesHeaderlessAndUnknown();
  void probeHeaderSynthesisesMameMetadata();
  void probeHeaderFromFileReadsBoundedPrefix();
  void detectsClrMameProText();
  void parsesClrMameProDat();
  void parseClrMameProSkipsNodumpAndHashless();
  void parseDatDispatchesToClrMamePro();
  void probeHeaderReadsClrMameProFields();
  void parsesCloneOfAcrossDialects();
  void parsesMiaAcrossDialects();
  void readDatFilePlainAndPrefix();
  void readDatFileUnpacksZip();

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
  QCOMPARE(records[0].sha1, QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
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
  const QByteArray xml = QByteArrayLiteral(R"xml(<?xml version="1.0"?>
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
  QVERIFY(store.lookupBySha1(QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")) !=
          nullptr);
  QVERIFY(store.lookupByMd5(QStringLiteral("22222222222222222222222222222222")) != nullptr);
  QVERIFY(store.lookupByCrc(QStringLiteral("deadbeef")) != nullptr);

  // Combined lookup picks sha1 over md5 over crc — verified by
  // pointing each kind at a different record's hash and confirming
  // the sha1 wins.
  const auto *picked = store.lookup(QStringLiteral("11111111111111111111111111111111"),
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
  const auto *r =
      store.lookup(QStringLiteral("11111111111111111111111111111111"), QString(), QString());
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

void TestDatLookup::storeRecordsExposesFullCatalogue() {
  // records() backs the auditor's "missing" computation — it must return
  // every parsed record in DAT order, not just the ones a lookup hits.
  auto parsed = DatLookup::parseNoIntroDat(QByteArray(kSampleDat));
  QVERIFY(parsed.isOk());
  DatLookup::Store store(parsed.value());

  const QList<DatLookup::DatRecord> &all = store.records();
  QCOMPARE(all.size(), 2);
  QCOMPARE(all.size(), store.recordCount());
  QCOMPARE(all.at(0).gameName, QStringLiteral("Game Alpha (USA)"));
  QCOMPARE(all.at(1).gameName, QStringLiteral("Game Beta (Japan)"));
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
  QVERIFY(store.value().lookupBySha1(QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")) !=
          nullptr);
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
  QCOMPARE(DatLookup::detectDialect(QByteArray(kSampleDat)), DatLookup::Dialect::Logiqx);
  QCOMPARE(DatLookup::detectDialect(QByteArray(kTosecWithRelease)), DatLookup::Dialect::Logiqx);
  QCOMPARE(DatLookup::detectDialect(QByteArray(kMameSample)), DatLookup::Dialect::Mame);
}

void TestDatLookup::detectsUnknownRoot() {
  // A non-DAT XML file should not be mistaken for either dialect —
  // the dispatcher uses this to surface a clear error message.
  const QByteArray xml =
      QByteArrayLiteral(R"xml(<?xml version="1.0"?><something-else><x/></something-else>)xml");
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
  QCOMPARE(records[0].sha1, QStringLiteral("cccccccccccccccccccccccccccccccccccccccc"));
  QCOMPARE(records[0].size, qint64{8192});
  // Kartend-m6qsb.29: for Logiqx the <game name=> is both title and set-id.
  QCOMPARE(records[0].setId, records[0].gameName);
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
  // Kartend-m6qsb.29: setId is the machine name= set-id, NOT the description —
  // the identity a clone's cloneof references, so it must differ from gameName.
  QCOMPARE(records[0].setId, QStringLiteral("pacman"));
  QVERIFY(records[0].setId != records[0].gameName);
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
  const auto it = std::find_if(records.begin(), records.end(), [](const DatLookup::DatRecord &r) {
    return r.romName == QStringLiteral("x.rom");
  });
  QVERIFY(it != records.end());
  QCOMPARE(it->gameName, QStringLiteral("setidonly"));
  // With no description, gameName falls back to the set-id, so the two coincide.
  QCOMPARE(it->setId, QStringLiteral("setidonly"));
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
  const QByteArray xml = QByteArrayLiteral(R"xml(<?xml version="1.0"?><not-a-dat/>)xml");
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
  QVERIFY(mame.value().lookupBySha1(QStringLiteral("e87e059c5be45753f7e9f33dff851f16d6751181")) !=
          nullptr);
}

void TestDatLookup::probeHeaderReadsLogiqxHeaderFields() {
  constexpr const char *withHeader = R"xml(<?xml version="1.0"?>
<datafile>
  <header>
    <name>Reference Audio Masters</name>
    <description>Reference Audio Masters - lossless set</description>
    <version>2026-05-01</version>
    <author>somebody</author>
  </header>
  <game name="Recording Alpha">
    <rom name="Recording Alpha.flac" size="1" crc="deadbeef"/>
  </game>
</datafile>
)xml";
  const auto h = DatLookup::probeHeader(QByteArray(withHeader));
  QCOMPARE(h.dialect, DatLookup::Dialect::Logiqx);
  QCOMPARE(h.name, QStringLiteral("Reference Audio Masters"));
  QCOMPARE(h.description, QStringLiteral("Reference Audio Masters - lossless set"));
  QCOMPARE(h.version, QStringLiteral("2026-05-01"));
}

void TestDatLookup::probeHeaderHandlesHeaderlessAndUnknown() {
  // <header> is optional in Logiqx — the probe must stop at the first
  // <game> with empty fields, not scan the record list hoping for one.
  constexpr const char *headerless = R"xml(<?xml version="1.0"?>
<datafile>
  <game name="Recording Alpha">
    <rom name="Recording Alpha.flac" size="1" crc="deadbeef"/>
  </game>
</datafile>
)xml";
  const auto h = DatLookup::probeHeader(QByteArray(headerless));
  QCOMPARE(h.dialect, DatLookup::Dialect::Logiqx);
  QVERIFY(h.name.isEmpty());
  QVERIFY(h.description.isEmpty());
  QVERIFY(h.version.isEmpty());

  // Unrecognised / non-XML bytes are an answer ("not a DAT"), not an error.
  const auto unknown = DatLookup::probeHeader(QByteArrayLiteral("not xml at all"));
  QCOMPARE(unknown.dialect, DatLookup::Dialect::Unknown);
  QVERIFY(unknown.name.isEmpty());
  const auto wrongRoot = DatLookup::probeHeader(QByteArrayLiteral("<html><body/></html>"));
  QCOMPARE(wrongRoot.dialect, DatLookup::Dialect::Unknown);
}

void TestDatLookup::probeHeaderSynthesisesMameMetadata() {
  constexpr const char *mame = R"xml(<?xml version="1.0"?>
<mame build="0.250 (mame0250)">
  <machine name="alpha"><rom name="a.bin" size="1" crc="deadbeef"/></machine>
</mame>
)xml";
  const auto h = DatLookup::probeHeader(QByteArray(mame));
  QCOMPARE(h.dialect, DatLookup::Dialect::Mame);
  QCOMPARE(h.name, QStringLiteral("MAME"));
  QCOMPARE(h.version, QStringLiteral("0.250 (mame0250)"));
  QVERIFY(h.description.isEmpty());
}

void TestDatLookup::probeHeaderFromFileReadsBoundedPrefix() {
  // A file whose header sits at the top followed by a record body far
  // larger than the 256 KiB probe window: the header must come back intact
  // even though the file is never read in full (a truncated record list is
  // invisible to a header-only probe).
  const QString path = m_dir.filePath("big.dat");
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"xml(<?xml version="1.0"?>
<datafile>
  <header><name>Big Video Catalogue</name><version>7</version></header>
)xml");
    for (int i = 0; i < 20000; ++i) {
      f.write(QStringLiteral("  <game name=\"Clip %1\"><rom name=\"Clip %1.mkv\" size=\"1\" "
                             "crc=\"deadbeef\"/></game>\n")
                  .arg(i)
                  .toUtf8());
    }
    f.write("</datafile>\n");
    QVERIFY(f.size() > 512 * 1024); // body comfortably exceeds the probe window
  }
  const auto h = DatLookup::probeHeaderFromFile(path);
  QCOMPARE(h.dialect, DatLookup::Dialect::Logiqx);
  QCOMPARE(h.name, QStringLiteral("Big Video Catalogue"));
  QCOMPARE(h.version, QStringLiteral("7"));

  // Error paths: missing file / empty path probe as Unknown.
  QCOMPARE(DatLookup::probeHeaderFromFile(m_dir.filePath("absent.dat")).dialect,
           DatLookup::Dialect::Unknown);
  QCOMPARE(DatLookup::probeHeaderFromFile(QString()).dialect, DatLookup::Dialect::Unknown);
}

namespace {
// A faithful clrmamepro text DAT: header block, two games (the second
// multi-rom), a quoted filename with spaces, mixed-case + bare hashes, a
// nodump rom, a hashless rom, and a nested block (sample) the parser ignores.
constexpr const char *kClrMameProDat = R"cmp(clrmamepro (
	name "Sample Console"
	description "Sample Console (Parent-Clone)"
	version 20230115
	author "someone"
)

game (
	name "Game Alpha (USA)"
	description "Game Alpha (USA)"
	rom ( name "Game Alpha (USA).bin" size 32768 crc DEADBEEF md5 11111111111111111111111111111111 sha1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa )
)

game (
	name "Game Beta (Japan)"
	description "Game Beta (Japan)"
	sample ( name beep )
	rom ( name "Beta Disc 1.bin" size 65536 crc cafebabe sha1 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb )
	rom ( name "Beta Disc 2.bin" size 65536 crc 0badf00d )
)
)cmp";
} // namespace

void TestDatLookup::detectsClrMameProText() {
  QCOMPARE(DatLookup::detectDialect(QByteArray(kClrMameProDat)), DatLookup::Dialect::ClrMamePro);
  // Leading whitespace + the headerless `game (` opener still sniffs.
  QCOMPARE(DatLookup::detectDialect(QByteArrayLiteral("\n  game ( name x )")),
           DatLookup::Dialect::ClrMamePro);
  // Not clrmamepro: a bare word with no '(' must stay Unknown, not misfire.
  QCOMPARE(DatLookup::detectDialect(QByteArrayLiteral("game over")), DatLookup::Dialect::Unknown);
}

void TestDatLookup::parsesClrMameProDat() {
  auto result = DatLookup::parseClrMameProDat(QByteArray(kClrMameProDat));
  QVERIFY(result.isOk());
  const auto records = result.value();
  QCOMPARE(records.size(), 3); // 1 (alpha) + 2 (beta discs)
  QCOMPARE(records[0].gameName, QStringLiteral("Game Alpha (USA)"));
  QCOMPARE(records[0].romName, QStringLiteral("Game Alpha (USA).bin"));
  QCOMPARE(records[0].size, qint64{32768});
  QCOMPARE(records[0].crc, QStringLiteral("deadbeef")); // lowercased
  QCOMPARE(records[0].sha1, QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  // Multi-rom game: both discs carry the game's name; the sample block is
  // ignored; the crc-only disc is still indexed.
  QCOMPARE(records[1].gameName, QStringLiteral("Game Beta (Japan)"));
  QCOMPARE(records[1].romName, QStringLiteral("Beta Disc 1.bin"));
  QCOMPARE(records[2].gameName, QStringLiteral("Game Beta (Japan)"));
  QCOMPARE(records[2].crc, QStringLiteral("0badf00d"));
  QVERIFY(records[2].sha1.isEmpty());
}

void TestDatLookup::parseClrMameProSkipsNodumpAndHashless() {
  constexpr const char *dat = R"cmp(game (
	name "Edge Cases"
	rom ( name good.bin size 4 crc 12345678 )
	rom ( name undumped.bin size 4 status nodump )
	rom ( name nohash.bin size 4 )
)
)cmp";
  auto result = DatLookup::parseClrMameProDat(QByteArray(dat));
  QVERIFY(result.isOk());
  const auto records = result.value();
  QCOMPARE(records.size(), 1); // nodump + hashless dropped, same as the XML dialects
  QCOMPARE(records[0].romName, QStringLiteral("good.bin"));
}

void TestDatLookup::parseDatDispatchesToClrMamePro() {
  auto result = DatLookup::parseDat(QByteArray(kClrMameProDat));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 3);
}

void TestDatLookup::probeHeaderReadsClrMameProFields() {
  const auto h = DatLookup::probeHeader(QByteArray(kClrMameProDat));
  QCOMPARE(h.dialect, DatLookup::Dialect::ClrMamePro);
  QCOMPARE(h.name, QStringLiteral("Sample Console"));
  QCOMPARE(h.description, QStringLiteral("Sample Console (Parent-Clone)"));
  QCOMPARE(h.version, QStringLiteral("20230115"));
}

void TestDatLookup::parsesCloneOfAcrossDialects() {
  // MAME listxml: cloneof on <machine>; romof is the fallback when cloneof is
  // absent (Kartend-m6qsb.13).
  const QByteArray mame = QByteArrayLiteral(R"xml(<?xml version="1.0"?>
<mame>
  <machine name="parent"><description>Parent</description>
    <rom name="p.rom" size="1" crc="aaaaaaaa"/></machine>
  <machine name="clone" cloneof="parent" romof="parent"><description>Clone</description>
    <rom name="c.rom" size="1" crc="bbbbbbbb"/></machine>
  <machine name="bioskid" romof="bios"><description>Kid</description>
    <rom name="k.rom" size="1" crc="cccccccc"/></machine>
</mame>)xml");
  const auto m = DatLookup::parseMameListXml(mame);
  QVERIFY(m.isOk());
  const auto mr = m.value();
  QCOMPARE(mr.size(), 3);
  QVERIFY(mr[0].cloneOf.isEmpty());                  // parent
  QCOMPARE(mr[1].cloneOf, QStringLiteral("parent")); // clone via cloneof
  QCOMPARE(mr[2].cloneOf, QStringLiteral("bios"));   // falls back to romof

  // Logiqx export carrying cloneof on <game>.
  const QByteArray logiqx = QByteArrayLiteral(R"xml(<?xml version="1.0"?>
<datafile>
  <game name="Base"><rom name="b.rom" size="1" crc="11111111"/></game>
  <game name="Variant" cloneof="Base"><rom name="v.rom" size="1" crc="22222222"/></game>
</datafile>)xml");
  const auto l = DatLookup::parseLogiqxDat(logiqx);
  QVERIFY(l.isOk());
  QCOMPARE(l.value().size(), 2);
  QVERIFY(l.value()[0].cloneOf.isEmpty());
  QCOMPARE(l.value()[1].cloneOf, QStringLiteral("Base"));

  // clrmamepro: cloneof key inside the game block.
  const QByteArray cmp = QByteArrayLiteral("game ( name \"Clone Set\" cloneof \"Parent Set\" "
                                           "rom ( name x.bin size 1 crc deadbeef ) )");
  const auto c = DatLookup::parseClrMameProDat(cmp);
  QVERIFY(c.isOk());
  QCOMPARE(c.value().size(), 1);
  QCOMPARE(c.value()[0].cloneOf, QStringLiteral("Parent Set"));
}

void TestDatLookup::parsesMiaAcrossDialects() {
  // MAME listxml: mia="yes" on <machine> flags all its roms; mia on a single
  // <rom> flags just that one (Kartend-34lab).
  const QByteArray mame = QByteArrayLiteral(R"xml(<?xml version="1.0"?>
<mame>
  <machine name="ok"><description>Ok</description>
    <rom name="ok.rom" size="1" crc="aaaaaaaa"/></machine>
  <machine name="gone" mia="yes"><description>Gone</description>
    <rom name="g.rom" size="1" crc="bbbbbbbb"/></machine>
  <machine name="part"><description>Part</description>
    <rom name="have.rom" size="1" crc="cccccccc"/>
    <rom name="lost.rom" size="1" crc="dddddddd" mia="yes"/></machine>
</mame>)xml");
  const auto m = DatLookup::parseMameListXml(mame);
  QVERIFY(m.isOk());
  const auto mr = m.value();
  QCOMPARE(mr.size(), 4);
  QVERIFY(!mr[0].mia); // ok
  QVERIFY(mr[1].mia);  // machine-level mia
  QVERIFY(!mr[2].mia); // have.rom in a non-mia machine
  QVERIFY(mr[3].mia);  // rom-level mia

  // Logiqx: mia on <game> and on a <rom>.
  const QByteArray logiqx = QByteArrayLiteral(R"xml(<?xml version="1.0"?>
<datafile>
  <game name="Here"><rom name="h.rom" size="1" crc="11111111"/></game>
  <game name="Lost" mia="yes"><rom name="l.rom" size="1" crc="22222222"/></game>
  <game name="Mixed"><rom name="a.rom" size="1" crc="33333333"/>
    <rom name="b.rom" size="1" crc="44444444" mia="yes"/></game>
</datafile>)xml");
  const auto l = DatLookup::parseLogiqxDat(logiqx);
  QVERIFY(l.isOk());
  const auto lr = l.value();
  QCOMPARE(lr.size(), 4);
  QVERIFY(!lr[0].mia);
  QVERIFY(lr[1].mia); // game-level
  QVERIFY(!lr[2].mia);
  QVERIFY(lr[3].mia); // rom-level

  // clrmamepro: `mia yes` inside the game block flags its roms.
  const QByteArray cmp = QByteArrayLiteral("game ( name \"Gone Set\" mia yes "
                                           "rom ( name x.bin size 1 crc deadbeef ) ) "
                                           "game ( name \"Here Set\" "
                                           "rom ( name y.bin size 1 crc feedface ) )");
  const auto c = DatLookup::parseClrMameProDat(cmp);
  QVERIFY(c.isOk());
  QCOMPARE(c.value().size(), 2);
  QVERIFY(c.value()[0].mia);
  QVERIFY(!c.value()[1].mia);
}

void TestDatLookup::readDatFilePlainAndPrefix() {
  // Plain (non-zip) DAT: returned verbatim; maxBytes caps the raw read; a
  // missing path yields empty. No archive tool involved (Kartend-m6qsb.28).
  const QString path = m_dir.filePath(QStringLiteral("plain.dat"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<datafile><game name=\"x\"/></datafile>");
  }
  QCOMPARE(DatLookup::readDatFile(path), QByteArray("<datafile><game name=\"x\"/></datafile>"));
  QCOMPARE(DatLookup::readDatFile(path, 9), QByteArray("<datafile"));
  QVERIFY(DatLookup::readDatFile(m_dir.filePath(QStringLiteral("absent.dat"))).isEmpty());
  QVERIFY(DatLookup::readDatFile(QString()).isEmpty());
}

void TestDatLookup::readDatFileUnpacksZip() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP(
      "Kartend-dhhh6: this test forks an external extractor (and builds its fixture by forking "
      "zip) on the TEST thread to exercise the real unzip path — a single-threaded fork with no "
      "cross-thread state, so faking the decompressor would test the fake, not real unzip. "
      "Kartend-yxco2 investigated a seam and found none worthwhile: the only cross-thread state on "
      "the production audit-extract worker is a std::atomic cancel token (race-free by "
      "construction) handed off via QFuture, so TSan has no real race to catch here; the DAT "
      "parse/store logic itself is already covered under TSan by the non-forking DAT tests.");
#endif
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty() ||
      (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
       QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty() &&
       QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty())) {
    QSKIP("zip/extractor not available");
  }
  // A real-world packed catalogue: one .dat inside a .zip.
  const QString srcDir = m_dir.filePath(QStringLiteral("ziproot"));
  QDir().mkpath(srcDir);
  const QByteArray dat =
      QByteArrayLiteral("<?xml version=\"1.0\"?>\n<datafile><header><name>Packed</name>"
                        "<version>9</version></header>"
                        "<game name=\"Solo\"><rom name=\"Solo.bin\" size=\"4\" crc=\"deadbeef\"/>"
                        "</game></datafile>");
  {
    QFile f(srcDir + QStringLiteral("/Packed.dat"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(dat);
  }
  const QString zip = m_dir.filePath(QStringLiteral("packed.zip"));
  QProcess p;
  p.setWorkingDirectory(srcDir);
  p.start(QStringLiteral("zip"), {QStringLiteral("-q"), zip, QStringLiteral("Packed.dat")});
  QVERIFY(p.waitForFinished(5000));
  QCOMPARE(p.exitCode(), 0);

  // readDatFile transparently unpacks the inner .dat.
  QCOMPARE(DatLookup::readDatFile(zip), dat);
  // …and the higher-level convenience + probe see through the zip too.
  auto store = DatLookup::loadStoreFromFile(zip);
  QVERIFY2(store.isOk(), qPrintable(store.isError() ? store.error().message : QString()));
  QVERIFY(store.value().lookupByCrc(QStringLiteral("deadbeef")) != nullptr);
  const auto header = DatLookup::probeHeaderFromFile(zip);
  QCOMPARE(header.name, QStringLiteral("Packed"));
  QCOMPARE(header.version, QStringLiteral("9"));
}

QTEST_MAIN(TestDatLookup)
#include "test_datlookup.moc"
