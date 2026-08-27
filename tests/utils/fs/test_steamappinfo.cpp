// SteamAppInfo binary parser against synthetic V28/V29 fixtures built by
// tests/support/appinfofixture.h, plus the store-taxonomy mapping helpers.
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "../../support/appinfofixture.h"
#include "steamappinfo.h"

using KartendTest::AppInfoFixture;

class TestSteamAppInfo : public QObject {
  Q_OBJECT

private slots:
  void parsesV29();
  void parsesV28();
  void wantedFilterSkipsOtherApps();
  void malformedRecordIsSkippedNotFatal();
  void associationsFallbackAndToolType();
  void libraryAssetPathsExtracted();
  void rejectsUnknownMagic();
  void rejectsOverflowingStringTableOffset();
  void rejectsOversizedStringTableCount();
  void rejectsImplausiblyLargeFile();
  void taxonomyHelpers();

private:
  QTemporaryDir m_dir;
  int m_fileCounter = 0;

  QString writeFixture(const QByteArray &bytes) {
    const QString path = m_dir.filePath(QStringLiteral("appinfo%1.vdf").arg(m_fileCounter++));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      return {};
    }
    file.write(bytes);
    return path;
  }

  void verifyGameFields(const QString &path) {
    const auto parsed = SteamAppInfo::read(path, {});
    QVERIFY(!parsed.isError());
    QCOMPARE(parsed.value().size(), 1);
    const SteamAppInfo::AppMetadata meta = parsed.value().value(QStringLiteral("620"));
    QCOMPARE(meta.name, QStringLiteral("Portal 2"));
    QVERIFY(meta.isGameType());
    QCOMPARE(meta.developer, QStringLiteral("Valve"));
    QCOMPARE(meta.publisher, QStringLiteral("Valve Publishing"));
    QCOMPARE(meta.releaseDateUtc, qint64(1303171200)); // 2011-04-19
    QCOMPARE(meta.genreIds, (QList<int>{1, 25}));
    QCOMPARE(meta.categoryIds, (QList<int>{2, 38}));
    QCOMPARE(meta.metacriticScore, 95);
    QCOMPARE(meta.reviewPercentage, 98);
    QCOMPARE(meta.controllerSupport, QStringLiteral("full"));
  }
};

void TestSteamAppInfo::parsesV29() {
  AppInfoFixture fixture(/*v29=*/true);
  fixture.addGame(620, QStringLiteral("Portal 2"), QStringLiteral("Valve"),
                  QStringLiteral("Valve Publishing"), 1303171200);
  const QString path = writeFixture(fixture.build());
  QVERIFY(!path.isEmpty());
  verifyGameFields(path);
}

void TestSteamAppInfo::parsesV28() {
  AppInfoFixture fixture(/*v29=*/false);
  fixture.addGame(620, QStringLiteral("Portal 2"), QStringLiteral("Valve"),
                  QStringLiteral("Valve Publishing"), 1303171200);
  const QString path = writeFixture(fixture.build());
  QVERIFY(!path.isEmpty());
  verifyGameFields(path);
}

void TestSteamAppInfo::wantedFilterSkipsOtherApps() {
  AppInfoFixture fixture(/*v29=*/true);
  fixture.addGame(620, QStringLiteral("Portal 2"), QStringLiteral("Valve"),
                  QStringLiteral("Valve Publishing"), 0);
  fixture.addGame(400, QStringLiteral("Portal"), QStringLiteral("Valve"),
                  QStringLiteral("Valve Publishing"), 0);
  const QString path = writeFixture(fixture.build());

  const auto onlyPortal = SteamAppInfo::read(path, {QStringLiteral("400")});
  QVERIFY(!onlyPortal.isError());
  QCOMPARE(onlyPortal.value().size(), 1);
  QVERIFY(onlyPortal.value().contains(QStringLiteral("400")));

  const auto none = SteamAppInfo::read(path, {QStringLiteral("999")});
  QVERIFY(!none.isError());
  QVERIFY(none.value().isEmpty());
}

void TestSteamAppInfo::malformedRecordIsSkippedNotFatal() {
  AppInfoFixture fixture(/*v29=*/true);
  // First app: an unknown node type wrecks its keyvalues blob.
  fixture.beginApp(111);
  fixture.putRawByte(0x7F);
  fixture.endApp();
  fixture.addGame(620, QStringLiteral("Portal 2"), QStringLiteral("Valve"),
                  QStringLiteral("Valve Publishing"), 0);
  const QString path = writeFixture(fixture.build());

  const auto parsed = SteamAppInfo::read(path, {});
  QVERIFY(!parsed.isError());
  // The corrupt record is dropped; the length prefix keeps the next one
  // reachable.
  QCOMPARE(parsed.value().size(), 1);
  QVERIFY(parsed.value().contains(QStringLiteral("620")));
}

void TestSteamAppInfo::associationsFallbackAndToolType() {
  AppInfoFixture fixture(/*v29=*/true);
  // No extended section: developer/publisher must come from associations.
  fixture.beginApp(700);
  fixture.beginMap(QStringLiteral("appinfo"));
  fixture.beginMap(QStringLiteral("common"));
  fixture.putString(QStringLiteral("name"), QStringLiteral("Proton 10.0"));
  fixture.putString(QStringLiteral("type"), QStringLiteral("Tool"));
  fixture.beginMap(QStringLiteral("associations"));
  fixture.beginMap(QStringLiteral("0"));
  fixture.putString(QStringLiteral("type"), QStringLiteral("developer"));
  fixture.putString(QStringLiteral("name"), QStringLiteral("CodeWeavers"));
  fixture.endMap();
  fixture.beginMap(QStringLiteral("1"));
  fixture.putString(QStringLiteral("type"), QStringLiteral("publisher"));
  fixture.putString(QStringLiteral("name"), QStringLiteral("Valve"));
  fixture.endMap();
  fixture.endMap(); // associations
  fixture.endMap(); // common
  fixture.endMap(); // appinfo
  fixture.endApp();
  const QString path = writeFixture(fixture.build());

  const auto parsed = SteamAppInfo::read(path, {});
  QVERIFY(!parsed.isError());
  const SteamAppInfo::AppMetadata meta = parsed.value().value(QStringLiteral("700"));
  QVERIFY(!meta.isGameType());
  QCOMPARE(meta.developer, QStringLiteral("CodeWeavers"));
  QCOMPARE(meta.publisher, QStringLiteral("Valve"));
}

void TestSteamAppInfo::libraryAssetPathsExtracted() {
  AppInfoFixture fixture(/*v29=*/true);
  fixture.beginApp(1071870);
  fixture.beginMap(QStringLiteral("appinfo"));
  fixture.beginMap(QStringLiteral("common"));
  fixture.putString(QStringLiteral("name"), QStringLiteral("Biped"));
  fixture.putString(QStringLiteral("type"), QStringLiteral("Game"));
  fixture.beginMap(QStringLiteral("library_assets_full"));
  // Plain filename, english variant present.
  fixture.beginMap(QStringLiteral("library_capsule"));
  fixture.beginMap(QStringLiteral("image"));
  fixture.putString(QStringLiteral("english"), QStringLiteral("library_600x900.jpg"));
  fixture.endMap();
  fixture.endMap();
  // Hash-subdir shape (the Kartend-40i6h case), no english key — first
  // available language wins.
  fixture.beginMap(QStringLiteral("library_hero"));
  fixture.beginMap(QStringLiteral("image"));
  fixture.putString(QStringLiteral("schinese"),
                    QStringLiteral("bcc13f58/library_hero_schinese.jpg"));
  fixture.endMap();
  fixture.endMap();
  // Traversal-hostile value — must be rejected, not resolved.
  fixture.beginMap(QStringLiteral("library_logo"));
  fixture.beginMap(QStringLiteral("image"));
  fixture.putString(QStringLiteral("english"), QStringLiteral("../../../etc/logo.png"));
  fixture.endMap();
  fixture.endMap();
  fixture.endMap(); // library_assets_full
  fixture.endMap(); // common
  fixture.endMap(); // appinfo
  fixture.endApp();
  const QString path = writeFixture(fixture.build());

  const auto parsed = SteamAppInfo::read(path, {});
  QVERIFY(!parsed.isError());
  const SteamAppInfo::AppMetadata meta = parsed.value().value(QStringLiteral("1071870"));
  QCOMPARE(meta.libraryCoverPath, QStringLiteral("library_600x900.jpg"));
  QCOMPARE(meta.libraryHeroPath, QStringLiteral("bcc13f58/library_hero_schinese.jpg"));
  QCOMPARE(meta.libraryLogoPath, QString());
}

void TestSteamAppInfo::rejectsUnknownMagic() {
  const QString path = writeFixture(QByteArrayLiteral("\x27\x44\x56\x07\x01\x00\x00\x00"));
  const auto parsed = SteamAppInfo::read(path, {});
  QVERIFY(parsed.isError());
}

// Security regression: appinfo.vdf is third-party data (Steam writes it, but
// anything with write access to the Steam dir can tamper with it), and the V29
// header's string-table offset is eight fully attacker-controlled bytes.
// The bound check must not be written as `tableOffset + 4 > data.size()` —
// that addition overflows signed 64-bit near INT64_MAX and wraps negative,
// passing the test and letting the subsequent read index far past the buffer.
void TestSteamAppInfo::rejectsOverflowingStringTableOffset() {
  AppInfoFixture fixture(/*v29=*/true);
  fixture.addGame(620, QStringLiteral("Portal 2"), QStringLiteral("Valve"),
                  QStringLiteral("Valve Publishing"), 0);
  QByteArray bytes = fixture.build();
  QVERIFY(bytes.size() > 16);

  // Overwrite the offset with values whose `+ 4` overflows or is otherwise out
  // of range. Each must be refused, not crash and not silently parse.
  const QList<quint64> hostileOffsets{
      0x7FFFFFFFFFFFFFFFULL, // INT64_MAX: +4 overflows to negative
      0x7FFFFFFFFFFFFFFCULL, // first value where +4 overflows
      0xFFFFFFFFFFFFFFFFULL, // reads as -1
      quint64(bytes.size()), // just past the end, no overflow
  };
  for (const quint64 offset : hostileOffsets) {
    QByteArray tampered = bytes;
    for (int i = 0; i < 8; ++i) {
      tampered[8 + i] = char(offset >> (8 * i) & 0xFF);
    }
    const auto parsed = SteamAppInfo::read(writeFixture(tampered), {});
    QVERIFY2(parsed.isError(),
             qPrintable(QStringLiteral("offset 0x%1 must be rejected").arg(offset, 0, 16)));
  }
}

// Hostile string-table entry count: a raw u32 immediately after the table
// offset. Reserving it verbatim asks for count * sizeof(QString) (~103 GB at
// 0xFFFFFFFF) before the per-entry truncation check can fire; the parser now
// clamps the reservation to the bytes that actually remain.
//
// Scope, stated honestly: this test asserts only that such a file is REJECTED
// rather than crashing. It does not by itself prove the clamp — without it the
// parse still ends in the same truncation error, because Linux overcommit lets
// an oversized reserve() map address space it never touches. Proving the clamp
// would need allocation instrumentation that is not portable. Its value is
// pinning the reject-don't-crash contract, not the allocation bound.
void TestSteamAppInfo::rejectsOversizedStringTableCount() {
  AppInfoFixture fixture(/*v29=*/true);
  fixture.addGame(620, QStringLiteral("Portal 2"), QStringLiteral("Valve"),
                  QStringLiteral("Valve Publishing"), 0);
  QByteArray bytes = fixture.build();

  // Recover the table offset the fixture wrote, then overwrite the count that
  // lives at it. The table cannot possibly hold 0xFFFFFFFF entries, so the
  // parse must fail on truncation rather than attempting the reservation.
  quint64 tableOffset = 0;
  for (int i = 0; i < 8; ++i) {
    tableOffset |= quint64(quint8(bytes[8 + i])) << (8 * i);
  }
  QVERIFY(qsizetype(tableOffset) + 4 <= bytes.size());
  for (int i = 0; i < 4; ++i) {
    bytes[qsizetype(tableOffset) + i] = char(0xFF);
  }

  const auto parsed = SteamAppInfo::read(writeFixture(bytes), {});
  QVERIFY(parsed.isError());
}

// Kartend-v3u04: the whole-file read had no ceiling, unlike the DAT path's
// kMaxDatBytes. It cannot be streamed — the V29 string table is addressed by
// absolute offset from the tail — so the guard has to refuse before the read
// rather than bound it during. Compounding it, parseKeyValues expands every
// record into a QVariantMap at roughly 25-40x the on-disk bytes.
//
// A 512 MiB fixture would be absurd to write, so this drives the guard from
// the other side: a sparse file whose reported size is over the ceiling but
// which occupies almost no disk. That is exactly what the guard consults —
// QFile::size() before readAll() — so it is the real path, not a stand-in.
void TestSteamAppInfo::rejectsImplausiblyLargeFile() {
  const QString path = m_dir.filePath(QStringLiteral("huge.vdf"));
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // Valid V29 magic up front, so a refusal cannot be mistaken for the
    // unknown-magic rejection that already existed.
    QVERIFY(file.write(QByteArrayLiteral("\x29\x44\x56\x07")) == 4);
    QVERIFY(file.resize(513LL * 1024 * 1024));
  }
  QCOMPARE(QFileInfo(path).size(), 513LL * 1024 * 1024);

  const auto parsed = SteamAppInfo::read(path, {});
  QVERIFY2(parsed.isError(), "an appinfo.vdf past the size ceiling must be refused");
  QCOMPARE(parsed.error().code, ErrorUtils::ErrorCode::ResourceLimitExceeded);
}

void TestSteamAppInfo::taxonomyHelpers() {
  QCOMPARE(SteamAppInfo::genreName(3), QStringLiteral("RPG"));
  QCOMPARE(SteamAppInfo::genreName(9999), QString());
  QCOMPARE(SteamAppInfo::genreNames({1, 9999, 25, 1}),
           (QStringList{QStringLiteral("Action"), QStringLiteral("Adventure")}));
  // Canonical ordering regardless of input order; non-player flags ignored.
  QCOMPARE(SteamAppInfo::playersDescription({38, 22, 2, 9}),
           QStringLiteral("Single-player, Co-op, Online Co-op"));
  QCOMPARE(SteamAppInfo::playersDescription({22, 23}), QString());
  QCOMPARE(SteamAppInfo::contentDescriptorNames({2, 5}),
           (QStringList{QStringLiteral("Frequent Violence or Gore"),
                        QStringLiteral("General Mature Content")}));
}

QTEST_MAIN(TestSteamAppInfo)
#include "test_steamappinfo.moc"
