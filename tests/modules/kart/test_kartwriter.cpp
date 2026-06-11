#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include "kartcompression.h"
#include "kartreader.h"
#include "kartwriter.h"

namespace {

KartWriter::WriterParams makeMinimalParams(const QString &mediaSrc, const QString &mediaName) {
  KartWriter::WriterParams p;
  p.uuid = "writer-uuid";
  p.name = "Writer Test";
  p.version = "1.0";
  p.collectionConfig.name = "Genesis";
  p.collectionConfig.gridLayout.gridWidth = 4;
  KartWriter::ItemSource it;
  it.mediaAbs = mediaSrc;
  it.manifestItem.mediaPath = "media/" + mediaName;
  it.manifestItem.title = "Test";
  p.items.append(it);
  return p;
}

QString writeFile(const QDir &dir, const QString &name, const QByteArray &data) {
  const QString abs = dir.absoluteFilePath(name);
  QFile f(abs);
  if (!f.open(QIODevice::WriteOnly)) {
    qFatal("writeFile: open failed: %s", qPrintable(f.errorString()));
  }
  f.write(data);
  f.close();
  return abs;
}

} // namespace

class TestKartWriter : public QObject {
  Q_OBJECT

private slots:
  void testExtensionShouldCompress();
  void testWriteAndReadBackRoundTrip();
  void testRoundTripSurvivesPathVariants_data();
  void testRoundTripSurvivesPathVariants();
  void testRoundTripWithMultipleEntryKinds();
  void testWriterRefusesMissingSourceFile();
  void testWriterStoresUncompressedWhenCompressionGrowsPayload();
  void testWriterEmitsProgress();
  void testWriterCanCancel();
  void testPrepareFromCollectionScansDirs();
};

void TestKartWriter::testExtensionShouldCompress() {
  QVERIFY(KartWriter::extensionShouldCompress("foo.txt"));
  QVERIFY(KartWriter::extensionShouldCompress("foo.cfg"));
  QVERIFY(KartWriter::extensionShouldCompress("foo"));
  QVERIFY(KartWriter::extensionShouldCompress("foo.bin"));
  QVERIFY(!KartWriter::extensionShouldCompress("foo.png"));
  QVERIFY(!KartWriter::extensionShouldCompress("foo.jpg"));
  QVERIFY(!KartWriter::extensionShouldCompress("foo.MP4"));
  QVERIFY(!KartWriter::extensionShouldCompress("foo.zip"));
  QVERIFY(!KartWriter::extensionShouldCompress("foo.zst"));
}

void TestKartWriter::testWriteAndReadBackRoundTrip() {
  QTemporaryDir src;
  QByteArray rom(4096, 'X');
  for (int i = 0; i < rom.size(); ++i) rom[i] = static_cast<char>(i & 0xFF);
  const QString mediaSrc = writeFile(QDir(src.path()), "rom.bin", rom);

  auto params = makeMinimalParams(mediaSrc, "rom.bin");
  params.preferredCompression = KartCompression::zstdAvailable() ? KartFormat::Compression_Zstd
                                                                 : KartFormat::Compression_Zlib;

  QTemporaryDir outDir;
  const QString kartPath = QDir(outDir.path()).filePath("test.kart");
  KartWriter::Writer w;
  auto wr = w.writeKart(kartPath, params);
  QVERIFY2(wr.isOk(), qPrintable(wr.error().message));

  QTemporaryDir extractDir;
  KartReader::Extractor r;
  auto rd = r.extractTo(kartPath, extractDir.path());
  QVERIFY2(rd.isOk(), qPrintable(rd.error().message));
  QCOMPARE(rd.value().manifest.uuid, QString("writer-uuid"));

  QFile readBack(QDir(extractDir.path()).filePath("media/rom.bin"));
  QVERIFY(readBack.open(QIODevice::ReadOnly));
  QCOMPARE(readBack.readAll(), rom);
}

void TestKartWriter::testRoundTripSurvivesPathVariants_data() {
  // Kart archives carry user-named media files; the entry path is stored as
  // length-prefixed UTF-8, so names with spaces, non-ASCII letters and
  // multi-byte emoji must survive the write → extract round trip verbatim
  // (Kartend-1yev5 — these rows were previously only covered for ASCII).
  QTest::addColumn<QString>("fileName");

  QTest::newRow("spaces") << QStringLiteral("file with spaces.bin");
  QTest::newRow("leading-trailing-ish") << QStringLiteral("  padded name .bin");
  QTest::newRow("diacritics") << QStringLiteral("vidéo préférée — Übung.bin");
  QTest::newRow("cjk") << QStringLiteral("テスト動画.bin");
  QTest::newRow("emoji") << QStringLiteral("clip 🎬🎞.bin");
  QTest::newRow("mixed") << QStringLiteral("Bande-annonce nº1 — テスト 🎥.bin");
}

void TestKartWriter::testRoundTripSurvivesPathVariants() {
  QFETCH(QString, fileName);

  QTemporaryDir src;
  const QByteArray payload("path-variant payload bytes");
  const QString mediaSrc = writeFile(QDir(src.path()), fileName, payload);

  auto params = makeMinimalParams(mediaSrc, fileName);

  QTemporaryDir outDir;
  const QString kartPath = QDir(outDir.path()).filePath("variant.kart");
  KartWriter::Writer w;
  auto wr = w.writeKart(kartPath, params);
  QVERIFY2(wr.isOk(), qPrintable(wr.error().message));

  QTemporaryDir extractDir;
  KartReader::Extractor r;
  auto rd = r.extractTo(kartPath, extractDir.path());
  QVERIFY2(rd.isOk(), qPrintable(rd.error().message));

  // The manifest must carry the exact relative path, and the extracted file
  // must exist under it with identical bytes.
  QCOMPARE(rd.value().manifest.items.size(), 1);
  QCOMPARE(rd.value().manifest.items.first().mediaPath, QString("media/" + fileName));

  QFile readBack(QDir(extractDir.path()).filePath("media/" + fileName));
  QVERIFY2(readBack.open(QIODevice::ReadOnly), qPrintable(readBack.errorString()));
  QCOMPARE(readBack.readAll(), payload);
}

void TestKartWriter::testRoundTripWithMultipleEntryKinds() {
  QTemporaryDir src;
  QDir d(src.path());
  const QByteArray romData("rom payload bytes");
  const QByteArray artData(2048, 0x42);
  const QByteArray vidData(1024, 0x11);
  const QByteArray manData("manual contents PDF stub");
  const QString romP = writeFile(d, "g.bin", romData);
  const QString artP = writeFile(d, "g.png", artData);
  const QString vidP = writeFile(d, "g.mp4", vidData);
  const QString manP = writeFile(d, "g.txt", manData);

  KartWriter::WriterParams p;
  p.uuid = "x";
  p.name = "Multi";
  p.collectionConfig.name = "Multi";
  p.preferredCompression = KartCompression::zstdAvailable() ? KartFormat::Compression_Zstd
                                                            : KartFormat::Compression_Zlib;
  KartWriter::ItemSource it;
  it.mediaAbs = romP;
  it.artworkAbs = artP;
  it.videoAbs = vidP;
  it.manualAbs = manP;
  it.manifestItem.mediaPath = "media/g.bin";
  it.manifestItem.artworkPath = "artwork/g.png";
  it.manifestItem.videoPath = "video/g.mp4";
  it.manifestItem.manualPath = "manual/g.txt";
  p.items.append(it);

  QTemporaryDir outDir;
  const QString kartPath = QDir(outDir.path()).filePath("multi.kart");
  KartWriter::Writer w;
  auto wr = w.writeKart(kartPath, p);
  QVERIFY2(wr.isOk(), qPrintable(wr.error().message));

  QTemporaryDir extractDir;
  KartReader::Extractor r;
  auto rd = r.extractTo(kartPath, extractDir.path());
  QVERIFY2(rd.isOk(), qPrintable(rd.error().message));
  QCOMPARE(rd.value().files.size(), 4);

  auto readBack = [&](const QString &rel) {
    QFile f(QDir(extractDir.path()).filePath(rel));
    if (!f.open(QIODevice::ReadOnly)) {
      qFatal("readBack: %s", qPrintable(f.errorString()));
    }
    return f.readAll();
  };
  QCOMPARE(readBack("media/g.bin"), romData);
  QCOMPARE(readBack("artwork/g.png"), artData);
  QCOMPARE(readBack("video/g.mp4"), vidData);
  QCOMPARE(readBack("manual/g.txt"), manData);
}

void TestKartWriter::testWriterRefusesMissingSourceFile() {
  KartWriter::WriterParams p = makeMinimalParams("/nonexistent/path/should/not/exist.bin", "x.bin");
  QTemporaryDir outDir;
  KartWriter::Writer w;
  auto wr = w.writeKart(QDir(outDir.path()).filePath("x.kart"), p);
  QVERIFY(wr.isError());
  QVERIFY(wr.hasErrorCode(ErrorUtils::ErrorCode::FileReadError));
}

void TestKartWriter::testWriterStoresUncompressedWhenCompressionGrowsPayload() {
  QTemporaryDir src;
  QByteArray random(64, 0);
  for (int i = 0; i < random.size(); ++i) random[i] = static_cast<char>((i * 31 + 7) & 0xFF);
  const QString mediaSrc = writeFile(QDir(src.path()), "tiny.bin", random);

  auto params = makeMinimalParams(mediaSrc, "tiny.bin");
  params.preferredCompression = KartCompression::zstdAvailable() ? KartFormat::Compression_Zstd
                                                                 : KartFormat::Compression_Zlib;

  QTemporaryDir outDir;
  const QString kartPath = QDir(outDir.path()).filePath("tiny.kart");
  KartWriter::Writer w;
  auto wr = w.writeKart(kartPath, params);
  QVERIFY2(wr.isOk(), qPrintable(wr.error().message));

  QTemporaryDir extractDir;
  KartReader::Extractor r;
  auto rd = r.extractTo(kartPath, extractDir.path());
  QVERIFY2(rd.isOk(), qPrintable(rd.error().message));
  QFile readBack(QDir(extractDir.path()).filePath("media/tiny.bin"));
  QVERIFY(readBack.open(QIODevice::ReadOnly));
  QCOMPARE(readBack.readAll(), random);
}

void TestKartWriter::testWriterEmitsProgress() {
  QTemporaryDir src;
  QDir d(src.path());
  KartWriter::WriterParams p;
  p.uuid = "x";
  p.name = "P";
  p.collectionConfig.name = "P";
  p.preferredCompression = KartCompression::zstdAvailable() ? KartFormat::Compression_Zstd
                                                            : KartFormat::Compression_Zlib;
  for (int i = 0; i < 3; ++i) {
    QByteArray data = QByteArray(512, static_cast<char>('a' + i));
    QString name = QString("rom%1.bin").arg(i);
    QString abs = writeFile(d, name, data);
    KartWriter::ItemSource it;
    it.mediaAbs = abs;
    it.manifestItem.mediaPath = "media/" + name;
    it.manifestItem.title = name;
    p.items.append(it);
  }

  QTemporaryDir outDir;
  KartWriter::Writer w;
  QSignalSpy spy(&w, &KartWriter::Writer::progress);
  auto wr = w.writeKart(QDir(outDir.path()).filePath("p.kart"), p);
  QVERIFY2(wr.isOk(), qPrintable(wr.error().message));
  QVERIFY(spy.size() >= 3);
  QCOMPARE(spy.last().first().toDouble(), 1.0);
}

void TestKartWriter::testWriterCanCancel() {
  QTemporaryDir src;
  QDir d(src.path());
  KartWriter::WriterParams p;
  p.uuid = "x";
  p.name = "Cancel";
  p.collectionConfig.name = "Cancel";
  for (int i = 0; i < 3; ++i) {
    QByteArray data(256, 'q');
    QString name = QString("r%1.bin").arg(i);
    KartWriter::ItemSource it;
    it.mediaAbs = writeFile(d, name, data);
    it.manifestItem.mediaPath = "media/" + name;
    p.items.append(it);
  }

  QTemporaryDir outDir;
  KartWriter::Writer w;
  w.cancel();
  auto wr = w.writeKart(QDir(outDir.path()).filePath("c.kart"), p);
  QVERIFY(wr.isError());
  QVERIFY(wr.hasErrorCode(ErrorUtils::ErrorCode::OperationCancelled));
}

void TestKartWriter::testPrepareFromCollectionScansDirs() {
  QTemporaryDir root;
  QDir rootDir(root.path());
  rootDir.mkpath("media");
  rootDir.mkpath("artwork");
  rootDir.mkpath("video");
  rootDir.mkpath("manual");

  QDir mDir(rootDir.filePath("media"));
  QDir aDir(rootDir.filePath("artwork"));
  QDir vDir(rootDir.filePath("video"));
  QDir manDir(rootDir.filePath("manual"));

  writeFile(mDir, "Sonic.bin", "rom-bytes");
  writeFile(aDir, "Sonic.png", QByteArray(64, 'p'));
  writeFile(vDir, "Sonic.mp4", QByteArray(128, 'v'));
  writeFile(manDir, "Sonic.pdf", "manual-bytes");

  CollectionConfig cfg;
  cfg.name = "Genesis";
  cfg.mediaDirectory = mDir.absolutePath();
  cfg.artworkDirectory = aDir.absolutePath();
  cfg.videoDirectory = vDir.absolutePath();
  cfg.manualDirectory = manDir.absolutePath();
  cfg.extensions = {"bin"};

  auto res = KartWriter::prepareFromCollection(cfg, "u-1", {}, nullptr);
  QVERIFY2(res.isOk(), qPrintable(res.error().message));
  const auto &p = res.value();
  QCOMPARE(p.uuid, QString("u-1"));
  QCOMPARE(p.items.size(), 1);
  const auto &it = p.items.first();
  QCOMPARE(it.manifestItem.mediaPath, QString("media/Sonic.bin"));
  QCOMPARE(it.manifestItem.artworkPath, QString("artwork/Sonic.png"));
  QCOMPARE(it.manifestItem.videoPath, QString("video/Sonic.mp4"));
  QCOMPARE(it.manifestItem.manualPath, QString("manual/Sonic.pdf"));
}

QTEST_MAIN(TestKartWriter)
#include "test_kartwriter.moc"
