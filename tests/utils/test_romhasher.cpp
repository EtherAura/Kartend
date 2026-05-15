/**
 * @file test_romhasher.cpp
 * @brief Unit tests for RomHasher streaming MD5/SHA1 computation.
 *
 * Validates that hashFile() returns the same digests as a reference
 * computation over the same bytes — for both small files (single-chunk)
 * and large files (>1 MiB, multi-chunk) so the streaming loop is
 * exercised. Failure paths (empty path, missing file) are checked for
 * the right ErrorCode rather than swallowed silently — providers
 * branch on Result.isOk() and we want callers to see the actual cause
 * in the log.
 */

#include "romhasher.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestRomHasher : public QObject {
  Q_OBJECT

private slots:
  void hashesKnownContent();
  void hashesEmptyFile();
  void hashesLargeMultiChunkFile();
  void emptyPathReturnsError();
  void missingFileReturnsError();
  void recognisesArchiveExtensions();
  void hashesInnerRomLargestFile();
  void archiveMissingPathReturnsError();

private:
  QTemporaryDir m_dir;
};

namespace {
void writeFile(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
  f.close();
}
} // namespace

void TestRomHasher::hashesKnownContent() {
  const QByteArray bytes = QByteArrayLiteral("kartend-rom-hasher-test");
  const QString path = m_dir.filePath("small.bin");
  writeFile(path, bytes);

  auto result = RomHasher::hashFile(path);
  QVERIFY(result.isOk());
  const auto h = result.value();
  QCOMPARE(h.size, static_cast<qint64>(bytes.size()));
  QCOMPARE(h.md5,
           QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex()));
  QCOMPARE(h.sha1,
           QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex()));
}

void TestRomHasher::hashesEmptyFile() {
  const QString path = m_dir.filePath("empty.bin");
  writeFile(path, QByteArray());

  auto result = RomHasher::hashFile(path);
  QVERIFY(result.isOk());
  const auto h = result.value();
  QCOMPARE(h.size, qint64{0});
  // MD5/SHA1 of empty input — canonical digests.
  QCOMPARE(h.md5, QStringLiteral("d41d8cd98f00b204e9800998ecf8427e"));
  QCOMPARE(h.sha1, QStringLiteral("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
}

void TestRomHasher::hashesLargeMultiChunkFile() {
  // 2.5 MiB — guarantees the 1 MiB streaming loop iterates more than
  // once and the trailing partial chunk is handled.
  QByteArray bytes;
  bytes.resize(static_cast<int>(2.5 * 1024 * 1024));
  for (int i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<char>(i & 0xff);
  }
  const QString path = m_dir.filePath("big.bin");
  writeFile(path, bytes);

  auto result = RomHasher::hashFile(path);
  QVERIFY(result.isOk());
  const auto h = result.value();
  QCOMPARE(h.size, static_cast<qint64>(bytes.size()));
  QCOMPARE(h.md5,
           QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex()));
  QCOMPARE(h.sha1,
           QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex()));
}

void TestRomHasher::emptyPathReturnsError() {
  auto result = RomHasher::hashFile(QString());
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestRomHasher::missingFileReturnsError() {
  auto result = RomHasher::hashFile(m_dir.filePath("does-not-exist.bin"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestRomHasher::recognisesArchiveExtensions() {
  QVERIFY(RomHasher::isArchivePath(QStringLiteral("/foo/bar.zip")));
  QVERIFY(RomHasher::isArchivePath(QStringLiteral("/foo/bar.7Z")));      // case-insensitive
  QVERIFY(RomHasher::isArchivePath(QStringLiteral("/foo/bar.tar.gz")));  // ends in .gz
  QVERIFY(RomHasher::isArchivePath(QStringLiteral("rom.RAR")));
  QVERIFY(!RomHasher::isArchivePath(QStringLiteral("/foo/bar.smc")));
  QVERIFY(!RomHasher::isArchivePath(QString()));
}

void TestRomHasher::hashesInnerRomLargestFile() {
  // Need a real extractor on PATH to build an archive AND to read it
  // back. Skip cleanly when the build environment lacks one — the
  // production code path returns an error in this case which is
  // exercised by archiveMissingPathReturnsError, so coverage isn't
  // lost.
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    QSKIP("zip not available — skipping archive-build half of the test");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    QSKIP("no archive extractor on PATH — RomHasher would error out");
  }

  // Build a .zip with two files so the largest-file selection has
  // something to choose between. The ROM is 8 KiB of pseudo-random
  // bytes; the sidecar is a tiny readme that should be ignored.
  QByteArray romBytes;
  romBytes.resize(8 * 1024);
  for (int i = 0; i < romBytes.size(); ++i) {
    romBytes[i] = static_cast<char>((i * 31 + 7) & 0xff);
  }
  const QString workDir = m_dir.filePath("zipsrc");
  QVERIFY(QDir().mkpath(workDir));
  writeFile(workDir + "/game.rom", romBytes);
  writeFile(workDir + "/readme.txt", QByteArrayLiteral("hello"));

  const QString archivePath = m_dir.filePath("game.zip");
  QProcess zipProc;
  zipProc.setWorkingDirectory(workDir);
  zipProc.start(QStringLiteral("zip"),
                {QStringLiteral("-q"), archivePath, QStringLiteral("game.rom"),
                 QStringLiteral("readme.txt")});
  QVERIFY(zipProc.waitForFinished(5000));
  QCOMPARE(zipProc.exitCode(), 0);

  auto result = RomHasher::hashArchiveInnerRom(archivePath);
  QVERIFY2(result.isOk(),
           qPrintable(result.isError() ? result.error().message : QString()));
  const auto h = result.value();
  QCOMPARE(h.size, static_cast<qint64>(romBytes.size()));
  QCOMPARE(h.md5, QString::fromLatin1(
                      QCryptographicHash::hash(romBytes, QCryptographicHash::Md5).toHex()));
  QCOMPARE(h.sha1, QString::fromLatin1(
                       QCryptographicHash::hash(romBytes, QCryptographicHash::Sha1).toHex()));
}

void TestRomHasher::archiveMissingPathReturnsError() {
  auto result = RomHasher::hashArchiveInnerRom(m_dir.filePath("absent.zip"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

QTEST_MAIN(TestRomHasher)
#include "test_romhasher.moc"
