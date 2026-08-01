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

#include <atomic>
#include <memory>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Kartend-68wbk: missing archive tool → graceful QSKIP locally, hard QFAIL in CI
// (KARTEND_REQUIRE_ARCHIVE_TOOLS=1, where Kartend-03lcs installs the tools) so
// silent skips can't hide lost coverage. Drop-in for a bare QSKIP.
#define KARTEND_ARCHIVE_TOOL_SKIP(msg)                                                             \
  do {                                                                                             \
    if (!qEnvironmentVariableIsEmpty("KARTEND_REQUIRE_ARCHIVE_TOOLS"))                             \
      QFAIL("KARTEND_REQUIRE_ARCHIVE_TOOLS is set but " msg);                                      \
    QSKIP(msg);                                                                                    \
  } while (false)

class TestRomHasher : public QObject {
  Q_OBJECT

private slots:
  void hashesKnownContent();
  void hashesEmptyFile();
  void hashesLargeMultiChunkFile();
  void hashesCrc32CanonicalVector();
  void emptyPathReturnsError();
  void missingFileReturnsError();
  void recognisesArchiveExtensions();
  void hashesInnerRomLargestFile();
  void hashInnerRomAmbiguousMultiDumpReturnsError();
  void archiveMissingPathReturnsError();
  void hashArchiveMembersHashesEveryFile();
  void hashArchiveMembersPreservesArchiveOrder();
  void hashArchiveMembersMissingPathReturnsError();
  void hashesSymlinkTargetSameAsDirect();
  void brokenSymlinkReturnsError();
  void extractorCandidates_neverOffersUnzip();
  void hashFileHonoursCancelToken();

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

// Reference CRC-32 (zip/PNG polynomial) computed over the whole buffer
// in one shot — independent of RomHasher's chunked streaming, so a
// chunk-boundary bug in the streaming accumulator would show up as a
// mismatch. Returned as 8-digit lowercase hex, the form RomHasher uses.
QString referenceCrc32Hex(const QByteArray &bytes) {
  quint32 crc = 0xFFFFFFFFu;
  for (const char ch : bytes) {
    crc ^= static_cast<quint8>(ch);
    for (int k = 0; k < 8; ++k) {
      crc = (crc & 1u) != 0u ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
  }
  return QString::number(~crc, 16).rightJustified(8, QLatin1Char('0'));
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
  QCOMPARE(h.crc, referenceCrc32Hex(bytes));
}

void TestRomHasher::hashFileHonoursCancelToken() {
  const QByteArray bytes = QByteArrayLiteral("kartend-rom-hasher-cancel-test");
  const QString path = m_dir.filePath("cancel.bin");
  writeFile(path, bytes);

  // A token already flipped true aborts the stream — the loop checks it once
  // per chunk, so even this small file returns OperationCancelled, not a hash.
  // (This is the cooperative-cancel path BatchScrapeRunner::cancel() drives so
  // an in-flight multi-GB hash stops instead of finishing on a worker thread.)
  auto cancelled = std::make_shared<std::atomic<bool>>(true);
  auto aborted = RomHasher::hashFile(path, cancelled);
  QVERIFY(aborted.isError());
  QCOMPARE(aborted.error().code, ErrorUtils::ErrorCode::OperationCancelled);

  // A null (default) token leaves the normal path unchanged — regression guard.
  auto normal = RomHasher::hashFile(path);
  QVERIFY(normal.isOk());
  QCOMPARE(normal.value().size, static_cast<qint64>(bytes.size()));

  // An un-flipped token behaves exactly like the null token.
  auto live = std::make_shared<std::atomic<bool>>(false);
  auto normal2 = RomHasher::hashFile(path, live);
  QVERIFY(normal2.isOk());
  QCOMPARE(normal2.value().md5, normal.value().md5);
}

void TestRomHasher::hashesEmptyFile() {
  const QString path = m_dir.filePath("empty.bin");
  writeFile(path, QByteArray());

  auto result = RomHasher::hashFile(path);
  QVERIFY(result.isOk());
  const auto h = result.value();
  QCOMPARE(h.size, qint64{0});
  // MD5/SHA1/CRC-32 of empty input — canonical values.
  QCOMPARE(h.md5, QStringLiteral("d41d8cd98f00b204e9800998ecf8427e"));
  QCOMPARE(h.sha1, QStringLiteral("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
  QCOMPARE(h.crc, QStringLiteral("00000000"));
}

void TestRomHasher::hashesCrc32CanonicalVector() {
  // The standard CRC-32 check value: the ASCII string "123456789"
  // hashes to 0xCBF43926 under the zip/PNG polynomial. This is the
  // canonical vector every CRC-32 implementation is tested against.
  const QString path = m_dir.filePath("crcvector.bin");
  writeFile(path, QByteArrayLiteral("123456789"));

  auto result = RomHasher::hashFile(path);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().crc, QStringLiteral("cbf43926"));
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
  // CRC-32 over a >1 MiB buffer — verifies the streaming accumulator
  // matches a whole-buffer computation across chunk boundaries.
  QCOMPARE(h.crc, referenceCrc32Hex(bytes));
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
  QVERIFY(RomHasher::isArchivePath(QStringLiteral("/foo/bar.7Z")));     // case-insensitive
  QVERIFY(RomHasher::isArchivePath(QStringLiteral("/foo/bar.tar.gz"))); // ends in .gz
  QVERIFY(RomHasher::isArchivePath(QStringLiteral("rom.RAR")));
  QVERIFY(!RomHasher::isArchivePath(QStringLiteral("/foo/bar.smc")));
  QVERIFY(!RomHasher::isArchivePath(QString()));
}

void TestRomHasher::hashesInnerRomLargestFile() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // Ubuntu 24.04's libtsan (the version in the CI runner image) has a
  // fork-after-thread-start CHECK bug — clone() from a QtTest binary
  // that's already started its watchdog thread trips
  // `tsan_rtl.cpp:253 "((!thr->slot)) != (0)"` and aborts the process.
  // The bug fires both in the test setup (QProcess running `zip`) and
  // inside RomHasher::hashArchiveInnerRom itself (QProcess running
  // 7z/bsdtar), so there's no rearrangement that makes this test
  // runnable under TSan on this distro. Re-enable once the runner image
  // ships a newer libtsan.
  QSKIP("libtsan fork CHECK bug — QProcess can't be used here under TSan");
#endif
  // Need a real extractor on PATH to build an archive AND to read it
  // back. Skip cleanly when the build environment lacks one — the
  // production code path returns an error in this case which is
  // exercised by archiveMissingPathReturnsError, so coverage isn't
  // lost.
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("zip not available — skipping archive-build half of the test");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("no archive extractor on PATH — RomHasher would error out");
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
  zipProc.start(QStringLiteral("zip"), {QStringLiteral("-q"), archivePath,
                                        QStringLiteral("game.rom"), QStringLiteral("readme.txt")});
  QVERIFY(zipProc.waitForFinished(5000));
  QCOMPARE(zipProc.exitCode(), 0);

  auto result = RomHasher::hashArchiveInnerRom(archivePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  const auto h = result.value();
  QCOMPARE(h.size, static_cast<qint64>(romBytes.size()));
  QCOMPARE(h.md5, QString::fromLatin1(
                      QCryptographicHash::hash(romBytes, QCryptographicHash::Md5).toHex()));
  QCOMPARE(h.sha1, QString::fromLatin1(
                       QCryptographicHash::hash(romBytes, QCryptographicHash::Sha1).toHex()));
  QCOMPARE(h.crc, referenceCrc32Hex(romBytes));
}

void TestRomHasher::hashInnerRomAmbiguousMultiDumpReturnsError() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("libtsan fork CHECK bug — QProcess can't be used here under TSan");
#endif
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("zip not available — skipping archive-build half of the test");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("no archive extractor on PATH — RomHasher would error out");
  }

  // Two comparably-large inner files (a multi-disc dump): "largest wins" would
  // hash an arbitrary one, so hashArchiveInnerRom must refuse instead of
  // returning a confident-but-arbitrary hash (Kartend-uhcfm).
  QByteArray disc1;
  disc1.resize(8 * 1024);
  for (int i = 0; i < disc1.size(); ++i) {
    disc1[i] = static_cast<char>((i * 31 + 7) & 0xff);
  }
  QByteArray disc2;
  disc2.resize(7 * 1024); // 7 KiB >= half of 8 KiB -> comparably large -> ambiguous
  for (int i = 0; i < disc2.size(); ++i) {
    disc2[i] = static_cast<char>((i * 17 + 3) & 0xff);
  }
  const QString workDir = m_dir.filePath("multidump");
  QVERIFY(QDir().mkpath(workDir));
  writeFile(workDir + "/disc1.bin", disc1);
  writeFile(workDir + "/disc2.bin", disc2);

  const QString archivePath = m_dir.filePath("multidump.zip");
  QProcess zipProc;
  zipProc.setWorkingDirectory(workDir);
  zipProc.start(QStringLiteral("zip"), {QStringLiteral("-q"), archivePath,
                                        QStringLiteral("disc1.bin"), QStringLiteral("disc2.bin")});
  QVERIFY(zipProc.waitForFinished(5000));
  QCOMPARE(zipProc.exitCode(), 0);

  auto result = RomHasher::hashArchiveInnerRom(archivePath);
  QVERIFY2(result.isError(), "an ambiguous multi-dump archive must not produce a hash");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestRomHasher::archiveMissingPathReturnsError() {
  auto result = RomHasher::hashArchiveInnerRom(m_dir.filePath("absent.zip"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestRomHasher::hashArchiveMembersHashesEveryFile() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("libtsan fork CHECK bug — QProcess can't be used here under TSan");
#endif
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("zip not available — skipping archive-build half of the test");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("no archive extractor on PATH — RomHasher would error out");
  }

  // Two comparably-sized members + a nested one: exactly the shape
  // hashArchiveInnerRom refuses (ambiguous multi-dump) and the
  // archive-per-item audit needs whole (Kartend-m6qsb.7).
  const QByteArray bytesA = QByteArrayLiteral("AAAAAAAAAAAAAAAA");
  const QByteArray bytesB = QByteArrayLiteral("BBBBBBBBBBBBBBB");
  const QByteArray bytesC = QByteArrayLiteral("CCCC");
  const QString workDir = m_dir.filePath("memberssrc");
  QVERIFY(QDir().mkpath(workDir + "/extras"));
  writeFile(workDir + "/part-one.bin", bytesA);
  writeFile(workDir + "/part-two.bin", bytesB);
  writeFile(workDir + "/extras/notes.txt", bytesC);

  const QString archivePath = m_dir.filePath("members.zip");
  QProcess zipProc;
  zipProc.setWorkingDirectory(workDir);
  zipProc.start(QStringLiteral("zip"),
                {QStringLiteral("-q"), QStringLiteral("-r"), archivePath, QStringLiteral(".")});
  QVERIFY(zipProc.waitForFinished(5000));
  QCOMPARE(zipProc.exitCode(), 0);

  auto result = RomHasher::hashArchiveMembers(archivePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  const auto members = result.value();
  QCOMPARE(members.size(), 3);

  bool sawA = false, sawNested = false;
  for (const RomHasher::MemberResult &m : members) {
    QVERIFY(!m.memberPath.startsWith(QLatin1Char('/')));
    if (m.memberPath == QStringLiteral("part-one.bin")) {
      sawA = true;
      QCOMPARE(m.hashes.size, qint64(bytesA.size()));
      QCOMPARE(
          m.hashes.md5,
          QString::fromLatin1(QCryptographicHash::hash(bytesA, QCryptographicHash::Md5).toHex()));
      QCOMPARE(m.hashes.crc, referenceCrc32Hex(bytesA));
    } else if (m.memberPath == QStringLiteral("extras/notes.txt")) {
      sawNested = true; // subfolder structure preserved in memberPath
      QCOMPARE(m.hashes.size, qint64(bytesC.size()));
    }
  }
  QVERIFY(sawA);
  QVERIFY(sawNested);
}

void TestRomHasher::hashArchiveMembersPreservesArchiveOrder() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("libtsan fork CHECK bug — QProcess can't be used here under TSan");
#endif
#ifndef KARTEND_HAS_LIBARCHIVE
  // Central-directory order is a guarantee of the in-process libarchive backend
  // only. Without it, hashArchiveMembers() shells out to an extractor and walks
  // the unpacked tree with QDirIterator, which yields path-sorted members — so
  // there is no archive order to assert. CI's build-no-zstd / linux-qt-newer /
  // macOS legs build without libarchive and would otherwise fail this QCOMPARE.
  QSKIP("Archive (central-directory) order is only preserved by the libarchive backend.");
#endif
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("zip not available — skipping archive-build half of the test");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("no archive extractor on PATH — RomHasher would error out");
  }

  // The ZipIndex (Kartend-7iqhl.4) is the member's position in the returned
  // list. Pin that the list is the archive's central-directory order, NOT
  // alphabetical: zip the members in an explicit, non-alphabetical argument
  // order (gamma, alpha, beta) and assert hashArchiveMembers returns them in
  // that same order. Only meaningful on the libarchive backend (guarded above).
  const QString workDir = m_dir.filePath("ordersrc");
  QVERIFY(QDir().mkpath(workDir));
  writeFile(workDir + "/gamma.bin", QByteArrayLiteral("GGGG"));
  writeFile(workDir + "/alpha.bin", QByteArrayLiteral("AAAA"));
  writeFile(workDir + "/beta.bin", QByteArrayLiteral("BBBB"));

  const QString archivePath = m_dir.filePath("ordered.zip");
  QProcess zipProc;
  zipProc.setWorkingDirectory(workDir);
  // Explicit argument order → deterministic central-directory order.
  zipProc.start(QStringLiteral("zip"), {QStringLiteral("-q"), QStringLiteral("-X"), archivePath,
                                        QStringLiteral("gamma.bin"), QStringLiteral("alpha.bin"),
                                        QStringLiteral("beta.bin")});
  QVERIFY(zipProc.waitForFinished(5000));
  QCOMPARE(zipProc.exitCode(), 0);

  auto result = RomHasher::hashArchiveMembers(archivePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  const auto members = result.value();
  QCOMPARE(members.size(), 3);
  // Archive order preserved (not sorted to alpha/beta/gamma).
  QCOMPARE(members.at(0).memberPath, QStringLiteral("gamma.bin"));
  QCOMPARE(members.at(1).memberPath, QStringLiteral("alpha.bin"));
  QCOMPARE(members.at(2).memberPath, QStringLiteral("beta.bin"));
}

void TestRomHasher::hashArchiveMembersMissingPathReturnsError() {
  auto result = RomHasher::hashArchiveMembers(m_dir.filePath("absent.zip"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
  auto empty = RomHasher::hashArchiveMembers(QString());
  QVERIFY(empty.isError());
  QCOMPARE(empty.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestRomHasher::hashesSymlinkTargetSameAsDirect() {
  // Kartend-ou0a regression test: a symlinked ROM must hash identically
  // to a direct read of its target. The original bug was a silent fallback
  // to filename-only matching when QFile failed to open via a symlink path,
  // which produced wrong-region SS matches downstream.
#ifdef Q_OS_WIN
  QSKIP("Symlink semantics on Windows differ; tested separately if needed.");
#else
  const QString target = m_dir.filePath("rom-target.bin");
  const QByteArray payload = QByteArray("XENO") + QByteArray(8192, '\xAA') + QByteArray("END");
  writeFile(target, payload);

  const QString link = m_dir.filePath("rom-link.bin");
  QVERIFY2(QFile::link(target, link),
           qPrintable(QStringLiteral("Failed to create symlink %1 -> %2").arg(link, target)));

  auto direct = RomHasher::hashFile(target);
  auto viaLink = RomHasher::hashFile(link);
  QVERIFY2(direct.isOk(), qPrintable(direct.isError() ? direct.error().message : QString()));
  QVERIFY2(viaLink.isOk(), qPrintable(viaLink.isError() ? viaLink.error().message : QString()));
  QCOMPARE(viaLink.value().md5, direct.value().md5);
  QCOMPARE(viaLink.value().sha1, direct.value().sha1);
  QCOMPARE(viaLink.value().crc, direct.value().crc);
  QCOMPARE(viaLink.value().size, direct.value().size);
#endif
}

void TestRomHasher::brokenSymlinkReturnsError() {
#ifdef Q_OS_WIN
  QSKIP("Symlink semantics on Windows differ; tested separately if needed.");
#else
  const QString missing = m_dir.filePath("absent-target.bin");
  const QString link = m_dir.filePath("broken-link.bin");
  QVERIFY2(QFile::link(missing, link),
           qPrintable(QStringLiteral("Failed to create symlink %1 -> %2").arg(link, missing)));

  auto result = RomHasher::hashFile(link);
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
#endif
}

void TestRomHasher::extractorCandidates_neverOffersUnzip() {
  // unzip recreates symlink entries and then writes through them — the
  // zip-slip-via-symlink primitive the pre-extraction safety scan exists to
  // stop — so it must never be offered for ANY format, .zip included; 7z and
  // bsdtar both cover .zip. 7z stays first so it wins when installed.
  const QStringList exts = {QStringLiteral(".zip"), QStringLiteral(".gz"),  QStringLiteral(".xz"),
                            QStringLiteral(".bz2"), QStringLiteral(".tar"), QStringLiteral(".7z"),
                            QStringLiteral(".rar"), QStringLiteral(".ZIP")};
  for (const QString &ext : exts) {
    const auto cands = RomHasher::extractorCandidates(QStringLiteral("/data/item") + ext);
    QVERIFY2(!cands.contains(QStringLiteral("unzip")), qPrintable(ext));
    QVERIFY2(cands.contains(QStringLiteral("bsdtar")), qPrintable(ext));
    QCOMPARE(cands.first(), QStringLiteral("7z"));
  }
}

QTEST_MAIN(TestRomHasher)
#include "test_romhasher.moc"
