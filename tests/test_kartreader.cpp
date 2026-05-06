#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

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
  file.close();
  return file.fileName();
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
  void testExtractToValidatesShaAndFailsOnTamper();
  void testExtractToHandlesTruncatedEntry();
  void testExtractToHandlesZstdEntries();
  void testExtractToHandlesZlibEntries();
  void testExtractToRefusesPathTraversal();
  void testExtractToRefusesAbsolutePaths();
  void testExtractToEmitsProgress();
  void testExtractToCanCancel();
};

void TestKartReader::testPeekManifestRejectsBadMagic() {
  QTemporaryFile f;
  writeTempKart(QByteArray("NOPE\0\0\0\1xxxxxxxxxxxxxxxxxx", 28), f);
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
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/a.bin", a, KartFormat::Flag_Media,
                                      KartFormat::Compression_None),
                           entryBytes("artwork/b.png", b, KartFormat::Flag_Artwork,
                                      KartFormat::Compression_None)}),
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

void TestKartReader::testExtractToHandlesZstdEntries() {
  if (!KartCompression::zstdAvailable()) {
    QSKIP("Built without zstd support");
  }
  QByteArray data(2048, 'A');
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/x.bin", data, KartFormat::Flag_Media,
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
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/x.bin", data, KartFormat::Flag_Media,
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
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("../escape.txt", data, KartFormat::Flag_Media,
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
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("/etc/passwd", data, KartFormat::Flag_Media,
                                      KartFormat::Compression_None)}),
                f);
  QTemporaryDir dest;
  KartReader::Extractor ex;
  auto result = ex.extractTo(f.fileName(), dest.path());
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartFormatInvalid));
}

void TestKartReader::testExtractToEmitsProgress() {
  QByteArray a(1024, 'a');
  QByteArray b(1024, 'b');
  QTemporaryFile f;
  writeTempKart(buildKart(sampleManifest(),
                          {entryBytes("media/a.bin", a, KartFormat::Flag_Media,
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

QTEST_MAIN(TestKartReader)
#include "test_kartreader.moc"
