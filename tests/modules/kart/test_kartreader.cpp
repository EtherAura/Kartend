#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtEndian>
#include <QTest>

#include <limits>

#include "kartcompression.h"
#include "kartformat.h"
#include "kartmanifest.h"
#include "kartreader.h"

namespace {

QByteArray entryBytes(const QString &relPath, const QByteArray &raw, quint8 flags,
                      KartFormat::Compression algo) {
  const QByteArray pathBytes = relPath.toUtf8();
  const QByteArray sha = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);

  QByteArray payload;
  if (algo == KartFormat::Compression_None) {
    payload = raw;
  } else {
    auto compressed = KartCompression::compress(raw, algo);
    if (compressed.isError()) {
      qFatal("entryBytes: compression failed: %s", qPrintable(compressed.error().message));
    }
    payload = compressed.value();
  }

  QByteArray out;
  QDataStream ds(&out, QIODevice::WriteOnly);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds << flags;
  ds << static_cast<quint8>(algo);
  ds << static_cast<quint16>(pathBytes.size());
  ds.writeRawData(pathBytes.constData(), pathBytes.size());
  ds << static_cast<quint64>(raw.size());
  ds << static_cast<quint64>(payload.size());
  ds.writeRawData(sha.constData(), KartFormat::SHA256_SIZE);
  ds.writeRawData(payload.constData(), payload.size());
  return out;
}

QByteArray buildKart(const KartManifest::Manifest &manifest, const QList<QByteArray> &entries) {
  QByteArray out;
  QDataStream ds(&out, QIODevice::WriteOnly);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds.writeRawData(KartFormat::MAGIC.data(), KartFormat::MAGIC_SIZE);

  const QByteArray manifestJson = KartManifest::serialize(manifest);
  ds << static_cast<quint32>(manifestJson.size());
  ds.writeRawData(manifestJson.constData(), manifestJson.size());

  for (const QByteArray &entry : entries) {
    ds.writeRawData(entry.constData(), entry.size());
  }
  return out;
}

QString writeTempKart(const QByteArray &bytes, QTemporaryFile &file) {
  file.setAutoRemove(true);
  if (!file.open()) {
    qFatal("writeTempKart: failed to open temp file: %s", qPrintable(file.errorString()));
  }
  if (file.write(bytes) != bytes.size()) {
    qFatal("writeTempKart: short write");
  }
  // flush(), not close(): the matching KartWriter test uses QTemporaryDir
  // for an explicit reason — on Windows, closing a QTemporaryFile and
  // then immediately re-opening it by name through a separate code path
  // (KartReader::peekManifest opens with QFile::ReadOnly) lands the
  // re-open against a still-pending delete or share lock that the close
  // didn't fully release. Keeping the QFile handle alive via flush()
  // pushes the bytes to disk while the underlying file shares cleanly
  // with KartReader's QFile::open(ReadOnly). The QTemporaryFile object
  // outlives the test method scope, so the file isn't reaped early.
  file.flush();
  return file.fileName();
}

// Overwrites a little-endian u64 field inside a crafted entry blob — used to
// lie about header-declared sizes after entryBytes wrote consistent ones.
// Entry field offsets: flags(0) comp(1) path_len(2) path(4) orig_size(4+N)
// payload_size(12+N) sha256(20+N) payload(52+N).
void patchU64LE(QByteArray &entry, qsizetype offset, quint64 value) {
  for (int i = 0; i < 8; ++i) {
    entry[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
  }
}

KartManifest::Manifest sampleManifest() {
  KartManifest::Manifest m;
  m.uuid = "test-uuid";
  m.name = "Test";
  m.version = "1.0";
  m.createdAt = "2026-05-05T00:00:00Z";
  m.collectionConfig.name = "Test";
  return m;
}

} // namespace

class TestKartReader : public QObject {
  Q_OBJECT

private slots:
  void testPeekManifestRejectsBadMagic();
  void testPeekManifestRejectsTooShort();
  void testPeekManifestSucceedsOnGoodKart();
  void testExtractToWritesFiles();
  void testExtractedPayloadsAreNotExecutable();
  void testExtractToWritesPathVariantEntries_data();
  void testExtractToWritesPathVariantEntries();
  void testExtractToValidatesShaAndFailsOnTamper();
  void testExtractToHandlesTruncatedEntry();
  void testExtractToRejectsOversizedDeclaredPayload();
  void testExtractToRejectsUnderdeclaredOrigSize();
  void testExtractToRejectsStoredSizeMismatch();
  void testExtractToDetectsTamperedShaOnMultiChunkEntry();
  void testExtractToHandlesZstdEntries();
  void testExtractToHandlesZlibEntries();
  void testZlibRejectsALyingSizePrefixBeforeAllocating();
  void testExtractToRefusesPathTraversal();
  void testExtractToRefusesAbsolutePaths();
  void testExtractToRefusesWindowsUnsafeSegments_data();
  void testExtractToRefusesWindowsUnsafeSegments();
  void testExtractToRefusesSymlinkEscape();
  void testExtractToEmitsProgress();
  void testExtractToCanCancel();

  // Destination and intra-bundle collision guards (Kartend-qbfk1)
  void testExtractToRefusesNonEmptyDestination();
  void testExtractToRefusesDotfileOnlyDestination();
  void testExtractToAcceptsExistingEmptyDestination();
  void testExtractToRefusesDuplicateEntryPaths();
  void testExtractToRefusesCaseCollidingEntryPaths();
  void testExtractToRefusesEntryOverEarlierDirectory();
  void testCountEntriesWalksHeadersWithoutExtracting();
  void testCountEntriesRejectsTruncatedContainer();
};

void TestKartReader::testPeekManifestRejectsBadMagic() {
  QTemporaryFile f;
  writeTempKart(QByteArray("NOPE\0\0\0\1xxxxxxxxxxxxxxxxxx", 26), f);
  auto result = KartReader::peekManifest(f.fileName());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testPeekManifestRejectsTooShort() {
  QTemporaryFile f;
  writeTempKart(QByteArray("KART", 4), f);
  auto result = KartReader::peekManifest(f.fileName());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testPeekManifestSucceedsOnGoodKart() {
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {}), f);
  auto result = KartReader::peekManifest(f.fileName());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QCOMPARE(result.value().uuid, QString("test-uuid"));
}

void TestKartReader::testExtractToWritesFiles() {
  QByteArray a("hello world");
  QByteArray b("second file content");
  QTemporaryFile f;
  writeTempKart(
      buildKart(
          sampleManifest(),
          {entryBytes("media/a.bin", a, KartFormat::Flag_Media, KartFormat::Compression_None),
           entryBytes("artwork/b.png", b, KartFormat::Flag_Artwork, KartFormat::Compression_None)}),
      f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QCOMPARE(result.value().files.size(), 2);

  QFile read1(QDir(dest.path()).filePath("media/a.bin"));
  QVERIFY(read1.open(QIODevice::ReadOnly));
  QCOMPARE(read1.readAll(), a);
  QFile read2(QDir(dest.path()).filePath("artwork/b.png"));
  QVERIFY(read2.open(QIODevice::ReadOnly));
  QCOMPARE(read2.readAll(), b);
}

void TestKartReader::testExtractedPayloadsAreNotExecutable() {
  // Kartend-f8y08: a bundle can name one of its own extracted files as the
  // launcher. That import is gated on an explicit confirmation, but the
  // extractor is the independent control underneath it: payloads land without
  // an execute bit, so the file cannot be started as a program even if the
  // gate above were ever weakened. The format carries no permission field —
  // this test exists to keep it that way, because "restore the mode on
  // extract" is an easy-looking feature that would quietly turn a bundled
  // file into a runnable one.
#ifdef Q_OS_WIN
  QSKIP("POSIX permission bits are not meaningful on Windows");
#else
  QByteArray script("#!/bin/sh\necho hi\n");
  QTemporaryFile f;
  writeTempKart(
      buildKart(sampleManifest(), {entryBytes("media/runner.sh", script, KartFormat::Flag_Media,
                                              KartFormat::Compression_None)}),
      f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  const QFileInfo extracted(QDir(dest.path()).filePath("media/runner.sh"));
  QVERIFY(extracted.exists());
  QVERIFY2(!extracted.isExecutable(),
           "extracted kart payloads must never land with an execute bit");
#endif
}

void TestKartReader::testExtractToWritesPathVariantEntries_data() {
  // Entry paths are length-prefixed UTF-8 (quint16 of BYTES, not characters),
  // so multi-byte names exercise the size-vs-length distinction in the entry
  // parser. Spaces/diacritics/CJK/emoji must extract to identically-named
  // files and pass the path-safety checks (Kartend-1yev5).
  QTest::addColumn<QString>("relPath");

  QTest::newRow("spaces") << QStringLiteral("media/file with spaces.bin");
  QTest::newRow("diacritics") << QStringLiteral("media/vidéo préférée — Übung.bin");
  QTest::newRow("cjk") << QStringLiteral("media/テスト動画.bin");
  QTest::newRow("emoji") << QStringLiteral("media/clip 🎬🎞.bin");
  QTest::newRow("unicode-subdir") << QStringLiteral("media/動画 集/clip 🎥.bin");

  // Near-misses of the Windows-portability rejections must still extract:
  // reserved-device-name checks match whole stems only, and dots/spaces are
  // legal anywhere but a segment's tail.
  QTest::newRow("reserved-name-prefix") << QStringLiteral("media/console.bin");
  QTest::newRow("com-with-two-digits") << QStringLiteral("media/COM10.bin");
  QTest::newRow("com-zero") << QStringLiteral("media/com0.bin");
  QTest::newRow("bare-lpt-stem") << QStringLiteral("media/lpt.bin");
  QTest::newRow("interior-dot-and-space") << QStringLiteral("media/a. b.bin");
}

void TestKartReader::testExtractToWritesPathVariantEntries() {
  QFETCH(QString, relPath);

  const QByteArray data("variant entry contents");
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entryBytes(relPath, data, KartFormat::Flag_Media,
                                                        KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QCOMPARE(result.value().files.size(), 1);

  QFile out(QDir(dest.path()).filePath(relPath));
  QVERIFY2(out.open(QIODevice::ReadOnly), qPrintable(out.errorString()));
  QCOMPARE(out.readAll(), data);
}

void TestKartReader::testExtractToValidatesShaAndFailsOnTamper() {
  QByteArray data("contents");
  QByteArray entry =
      entryBytes("media/x.bin", data, KartFormat::Flag_Media, KartFormat::Compression_None);
  // Flip a payload byte (the last byte of the entry sits in the payload region).
  entry[entry.size() - 1] = entry.at(entry.size() - 1) ^ 0xFF;
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entry}), f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartIntegrityCheckFailed));
}

void TestKartReader::testExtractToHandlesTruncatedEntry() {
  QByteArray data("a-medium-sized-payload-for-truncation-tests");
  QByteArray entry =
      entryBytes("media/x.bin", data, KartFormat::Flag_Media, KartFormat::Compression_None);
  QByteArray full = buildKart(sampleManifest(), {entry});
  full.truncate(full.size() - 8);
  QTemporaryFile f;
  writeTempKart(full, f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToRejectsOversizedDeclaredPayload() {
  // The entry header's size fields are attacker-controlled. A declared
  // payload_size within MAX_ENTRY_SIZE but far past the bytes actually in the
  // file must be rejected against the remaining file length BEFORE a buffer is
  // allocated for it — the earlier int-narrowed resize could wrap a multi-GiB
  // declaration into a tiny buffer while the read loop was still handed the
  // full length.
  const QByteArray pathBytes = QByteArrayLiteral("media/huge.bin");
  const quint64 declaredSize = 4ull * 1024 * 1024 * 1024; // 4 GiB, < MAX_ENTRY_SIZE
  QByteArray entry;
  QDataStream ds(&entry, QIODevice::WriteOnly);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds << static_cast<quint8>(KartFormat::Flag_Media);
  ds << static_cast<quint8>(KartFormat::Compression_None);
  ds << static_cast<quint16>(pathBytes.size());
  ds.writeRawData(pathBytes.constData(), pathBytes.size());
  ds << declaredSize; // original_size
  ds << declaredSize; // payload_size — a lie; only a few bytes follow
  const QByteArray sha(KartFormat::SHA256_SIZE, '\0');
  ds.writeRawData(sha.constData(), sha.size());
  ds.writeRawData("only-a-few-bytes", 16);

  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entry}), f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToRejectsUnderdeclaredOrigSize() {
  // Zip-bomb guard for the streaming decompressor: a payload whose
  // decompressed stream exceeds the header-declared original_size must abort
  // as soon as the excess appears, not after the whole stream materializes.
  // Craft it by compressing a multi-chunk buffer, then understating the
  // orig_size field in an otherwise-valid entry.
  if (!KartCompression::zstdAvailable()) {
    QSKIP("Built without zstd support");
  }
  const QString relPath = QStringLiteral("media/bomb.bin");
  QByteArray data(2 * (1 << 20), 'K'); // highly compressible, > 1 chunk decompressed
  QByteArray entry =
      entryBytes(relPath, data, KartFormat::Flag_Media, KartFormat::Compression_Zstd);
  const qsizetype origSizeOffset = 4 + relPath.toUtf8().size();
  patchU64LE(entry, origSizeOffset, 1024);

  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entry}), f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartCompressionFailed));
  // Nothing may land in the destination on an aborted entry.
  QVERIFY(!QFile::exists(QDir(dest.path()).filePath(relPath)));
}

void TestKartReader::testExtractToRejectsStoredSizeMismatch() {
  // Stored (Compression_None) entries always record payload_size ==
  // original_size; a header where they disagree can only be tampered and is
  // rejected before any destination file is created.
  const QString relPath = QStringLiteral("media/mismatch.bin");
  const QByteArray data("stored-mismatch-payload");
  QByteArray entry =
      entryBytes(relPath, data, KartFormat::Flag_Media, KartFormat::Compression_None);
  const qsizetype origSizeOffset = 4 + relPath.toUtf8().size();
  patchU64LE(entry, origSizeOffset, static_cast<quint64>(data.size()) - 1);

  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entry}), f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToDetectsTamperedShaOnMultiChunkEntry() {
  // The SHA-256 now accumulates incrementally per streamed block; corrupting
  // the stored hash of a multi-chunk entry must still fail the integrity
  // check after the full stream has been hashed. (The payload itself stays
  // valid so decompression succeeds and the hash comparison is what trips.)
  const KartFormat::Compression algo = KartCompression::zstdAvailable()
                                           ? KartFormat::Compression_Zstd
                                           : KartFormat::Compression_None;
  const QString relPath = QStringLiteral("media/tampered.bin");
  QByteArray data(2 * (1 << 20) + 99, '\0');
  for (qsizetype i = 0; i < data.size(); ++i) {
    data[i] = static_cast<char>((i * 131) & 0xFF);
  }
  QByteArray entry = entryBytes(relPath, data, KartFormat::Flag_Media, algo);
  const qsizetype shaOffset = 4 + relPath.toUtf8().size() + 16;
  entry[shaOffset] = static_cast<char>(entry.at(shaOffset) ^ 0x5A);

  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entry}), f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartIntegrityCheckFailed));
  // The streamed bytes were written to a QSaveFile temp; a failed integrity
  // check must leave no file behind.
  QVERIFY(!QFile::exists(QDir(dest.path()).filePath(relPath)));
}

void TestKartReader::testExtractToHandlesZstdEntries() {
  if (!KartCompression::zstdAvailable()) {
    QSKIP("Built without zstd support");
  }
  QByteArray data(2048, 'A');
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entryBytes("media/x.bin", data, KartFormat::Flag_Media,
                                                        KartFormat::Compression_Zstd)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QFile out(QDir(dest.path()).filePath("media/x.bin"));
  QVERIFY(out.open(QIODevice::ReadOnly));
  QCOMPARE(out.readAll(), data);
}

void TestKartReader::testExtractToHandlesZlibEntries() {
  QByteArray data(2048, 'B');
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entryBytes("media/x.bin", data, KartFormat::Flag_Media,
                                                        KartFormat::Compression_Zlib)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QFile out(QDir(dest.path()).filePath("media/x.bin"));
  QVERIFY(out.open(QIODevice::ReadOnly));
  QCOMPARE(out.readAll(), data);
}

void TestKartReader::testExtractToRefusesPathTraversal() {
  QByteArray data("x");
  QTemporaryFile f;
  writeTempKart(
      buildKart(sampleManifest(), {entryBytes("../escape.txt", data, KartFormat::Flag_Media,
                                              KartFormat::Compression_None)}),
      f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToRefusesAbsolutePaths() {
  QByteArray data("x");
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entryBytes("/etc/passwd", data, KartFormat::Flag_Media,
                                                        KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToRefusesWindowsUnsafeSegments_data() {
  // Rejected unconditionally (matching the backslash / drive-letter checks)
  // so a portable .kart stays importable on Windows: "." aliases the current
  // directory, Windows strips trailing dots/spaces from segments (colliding
  // "name." with "name"), and reserved device names resolve to the device
  // regardless of extension or case.
  QTest::addColumn<QString>("relPath");

  QTest::newRow("bare-dot-segment") << QStringLiteral("media/./x.bin");
  QTest::newRow("dots-and-spaces-only") << QStringLiteral("media/. ./x.bin");
  QTest::newRow("trailing-dot") << QStringLiteral("media/name.");
  QTest::newRow("trailing-space") << QStringLiteral("media/name ");
  QTest::newRow("trailing-dot-dir") << QStringLiteral("media./x.bin");
  QTest::newRow("reserved-con") << QStringLiteral("media/CON");
  QTest::newRow("reserved-con-lower-ext") << QStringLiteral("media/con.txt");
  QTest::newRow("reserved-nul-double-ext") << QStringLiteral("media/nul.tar.gz");
  QTest::newRow("reserved-com1-mixed-case") << QStringLiteral("media/Com1.bin");
  QTest::newRow("reserved-lpt9-bare") << QStringLiteral("lpt9");
  QTest::newRow("reserved-aux-dir") << QStringLiteral("aux/x.bin");
  QTest::newRow("reserved-prn-stem-space") << QStringLiteral("media/prn .txt");
}

void TestKartReader::testExtractToRefusesWindowsUnsafeSegments() {
  QFETCH(QString, relPath);

  QByteArray data("x");
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entryBytes(relPath, data, KartFormat::Flag_Media,
                                                        KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToRefusesSymlinkEscape() {
#ifdef Q_OS_WIN
  // QFile::link() on Windows creates a real NTFS reparse-point symbolic
  // link, but Qt's canonicalFilePath chain-resolution doesn't traverse
  // every reparse-point class consistently across Qt versions. The
  // production guard (KartReader's canonical-path check refuses paths
  // that escape destAbs) is still in place — the gap is that this
  // specific test fixture can't reliably build a Windows reparse-point
  // structure that exercises the escape path. Tracked separately if/when
  // symlink semantics matter on Windows.
  QSKIP("Qt symlink semantics on NTFS reparse points are inconsistent across Qt versions");
#else
  // Regression: a parent dir inside destAbs is a symlink pointing outside
  // destAbs. isPathSafe accepts the relative entry textually (no '..', not
  // absolute), and a startsWith(destAbs + '/') check passes because the
  // cleaned path still lives under destAbs lexically.
  //
  // Since Kartend-qbfk1 the FIRST line that refuses this fixture is the
  // destination-emptiness guard — a pre-planted symlink means a non-empty
  // destination, so extraction never starts. The canonical-resolve check
  // stays in extractTo as the backstop for a symlink appearing DURING
  // extraction (external mutation), which no fixture can stage
  // deterministically through the public API. This test now pins the
  // guard ordering and, unchanged, that nothing lands outside the dest.
  QByteArray data("x");
  QTemporaryFile f;
  writeTempKart(
      buildKart(sampleManifest(), {entryBytes("inner/escapee.bin", data, KartFormat::Flag_Media,
                                              KartFormat::Compression_None)}),
      f);

  QTemporaryDir dest;
  QTemporaryDir outside;
  QVERIFY(dest.isValid());
  QVERIFY(outside.isValid());

  // Pre-create dest/inner as a symlink to outside/. After the canonical
  // resolve, dest/inner/escapee.bin lands at outside/escapee.bin, which is
  // not a child of dest's canonical path.
  const QString linkPath = QDir(dest.path()).absoluteFilePath("inner");
  QVERIFY(QFile::link(outside.path(), linkPath));

  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  // The emptiness guard's code — the symlink makes the destination
  // non-empty before the canonical check is ever consulted.
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidFilePath));

  // Verify the would-be target outside the dest dir was NOT written.
  const QString shouldNotExist = QDir(outside.path()).absoluteFilePath("escapee.bin");
  QVERIFY2(!QFile::exists(shouldNotExist), qPrintable(shouldNotExist));
#endif
}

void TestKartReader::testExtractToEmitsProgress() {
  QByteArray a(1024, 'a');
  QByteArray b(1024, 'b');
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entryBytes("media/a.bin", a, KartFormat::Flag_Media,
                                                        KartFormat::Compression_None),
                                             entryBytes("media/b.bin", b, KartFormat::Flag_Media,
                                                        KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  QSignalSpy spy(&ex, &KartReader::Extractor::progress);
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QVERIFY(spy.size() >= 2);
  const double last = spy.last().first().toDouble();
  QVERIFY(last >= 0.99);
}

void TestKartReader::testExtractToCanCancel() {
  QByteArray data(4096, 'z');
  QList<QByteArray> entries;
  for (int i = 0; i < 4; ++i) {
    entries.append(entryBytes(QString("media/x%1.bin").arg(i), data, KartFormat::Flag_Media,
                              KartFormat::Compression_None));
  }
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), entries), f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  ex.cancel();
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::OperationCancelled));
}

// ----- Destination and intra-bundle collision guards (Kartend-qbfk1) -----

// The traversal checks keep entries inside the destination; these tests pin
// the complementary property — the destination itself must be fresh. A
// bundle needs no traversal to plant '.config/autostart/evil.desktop' when
// the user picks $HOME as the destination.

void TestKartReader::testExtractToRefusesNonEmptyDestination() {
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/x.bin", QByteArray("x"), KartFormat::Flag_Media,
                                      KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  {
    QFile existing(QDir(dest.path()).filePath("precious.txt"));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("user data");
  }
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidFilePath));
  // The refusal must come before any entry lands.
  QVERIFY(!QFile::exists(QDir(dest.path()).filePath("media/x.bin")));
}

void TestKartReader::testExtractToRefusesDotfileOnlyDestination() {
  // QDir::isEmpty()'s DEFAULT filters skip hidden files — a home directory
  // populated only by dotfiles must still count as non-empty, or the guard
  // is a no-op for exactly the directory the attack wants.
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/x.bin", QByteArray("x"), KartFormat::Flag_Media,
                                      KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  {
    QFile hidden(QDir(dest.path()).filePath(".bashrc"));
    QVERIFY(hidden.open(QIODevice::WriteOnly));
    hidden.write("alias ls='ls'");
  }
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::InvalidFilePath));
}

void TestKartReader::testExtractToAcceptsExistingEmptyDestination() {
  QByteArray data("payload");
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(), {entryBytes("media/x.bin", data, KartFormat::Flag_Media,
                                                        KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest; // exists and is empty — the allowed pre-existing shape
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QFile out(QDir(dest.path()).filePath("media/x.bin"));
  QVERIFY(out.open(QIODevice::ReadOnly));
  QCOMPARE(out.readAll(), data);
}

void TestKartReader::testExtractToRefusesDuplicateEntryPaths() {
  // One entry overwriting another inside the same bundle: the second write
  // would silently replace the first via QSaveFile::commit's rename.
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/x.bin", QByteArray("first"), KartFormat::Flag_Media,
                                      KartFormat::Compression_None),
                           entryBytes("media/x.bin", QByteArray("second"), KartFormat::Flag_Media,
                                      KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToRefusesCaseCollidingEntryPaths() {
  // Distinct on ext4, the same file on Windows/default macOS — rejected on
  // every platform so a portable .kart behaves identically everywhere (the
  // same over-report direction as the Windows segment checks).
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/X.bin", QByteArray("upper"), KartFormat::Flag_Media,
                                      KartFormat::Compression_None),
                           entryBytes("media/x.bin", QByteArray("lower"), KartFormat::Flag_Media,
                                      KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToRefusesEntryOverEarlierDirectory() {
  // 'a/b' creates directory 'a'; a later entry named 'a' would rename a
  // regular file over that directory's path. The pre-write existence guard
  // must refuse rather than let QSaveFile fail (or worse, succeed) opaquely.
  QTemporaryFile f;
  writeTempKart(
      buildKart(sampleManifest(), {entryBytes("a/b", QByteArray("child"), KartFormat::Flag_Media,
                                              KartFormat::Compression_None),
                                   entryBytes("a", QByteArray("clobber"), KartFormat::Flag_Media,
                                              KartFormat::Compression_None)}),
      f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::FileWriteError));
  // The directory entry that landed first must survive the refusal.
  QFile out(QDir(dest.path()).filePath("a/b"));
  QVERIFY(out.open(QIODevice::ReadOnly));
  QCOMPARE(out.readAll(), QByteArray("child"));
}

void TestKartReader::testCountEntriesWalksHeadersWithoutExtracting() {
  QByteArray big(64 * 1024, 'q'); // larger than one header so a seek bug shows
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/a.bin", big, KartFormat::Flag_Media,
                                      KartFormat::Compression_None),
                           entryBytes("media/b.bin", QByteArray("b"), KartFormat::Flag_Media,
                                      KartFormat::Compression_None),
                           entryBytes("artwork/c.png", QByteArray("c"), KartFormat::Flag_Artwork,
                                      KartFormat::Compression_None)}),
                f);
  auto count = KartReader::countEntries(f.fileName());
  QVERIFY2(count.isOk(), qPrintable(count.error().message));
  QCOMPARE(count.value(), 3u);
}

void TestKartReader::testCountEntriesRejectsTruncatedContainer() {
  QByteArray bytes = buildKart(sampleManifest(),
                               {entryBytes("media/a.bin", QByteArray(1024, 'a'),
                                           KartFormat::Flag_Media, KartFormat::Compression_None)});
  bytes.chop(512); // sever the payload so the declared length overruns the file
  QTemporaryFile f;
  writeTempKart(bytes, f);
  auto count = KartReader::countEntries(f.fileName());
  QVERIFY(count.isError());
  QVERIFY(count.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testZlibRejectsALyingSizePrefixBeforeAllocating() {
  // qCompress emits [4-byte big-endian original size][zlib stream], and
  // qUncompress sizes its output allocation from that prefix. The prefix lives
  // in the COMPRESSED bytes, so it is attacker-controlled and wholly
  // independent of the entry header's origSize that MAX_ENTRY_SIZE was checked
  // against — a tiny payload could therefore claim gigabytes and get them
  // allocated before any of our caps ran (Kartend-ohqfs).
  const QByteArray raw("kartend zlib payload");
  const QByteArray pristine = qCompress(raw);
  QVERIFY(pristine.size() >= 4);

  // Untampered, it still round-trips — the guard must not cost the happy path.
  {
    const auto ok = KartCompression::decompress(pristine, KartFormat::Compression_Zlib, raw.size());
    QVERIFY(!ok.isError());
    QCOMPARE(ok.value(), raw);
  }

  // Now the attack: a ~30-byte payload whose prefix claims ~4 GiB. Refused on
  // the prefix, so qUncompress is never handed the bytes.
  QByteArray lying = pristine;
  qToBigEndian(std::numeric_limits<quint32>::max(), reinterpret_cast<uchar *>(lying.data()));
  QVERIFY(lying.size() < 200); // the whole point: tiny input, enormous claim
  const auto result = KartCompression::decompress(lying, KartFormat::Compression_Zlib, raw.size());
  QVERIFY2(result.isError(),
           "A zlib size prefix disagreeing with the entry header must be refused");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::KartCompressionFailed);

  // A prefix that UNDERSTATES is equally a disagreement — the check is
  // equality against the header, not a ceiling.
  QByteArray understated = pristine;
  qToBigEndian(quint32{1}, reinterpret_cast<uchar *>(understated.data()));
  QVERIFY(
      KartCompression::decompress(understated, KartFormat::Compression_Zlib, raw.size()).isError());

  // Too short to even carry a prefix.
  QVERIFY(KartCompression::decompress(QByteArray("ab"), KartFormat::Compression_Zlib, raw.size())
              .isError());
}

QTEST_MAIN(TestKartReader)
#include "test_kartreader.moc"
