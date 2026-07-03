// Pre-extraction archive safety scan: symlink/hardlink entries and
// path-escape attempts must be rejected BEFORE any extractor writes a byte —
// the write-through-symlink zip-slip class lands its payload outside the
// extraction root, where the post-extraction NoSymLinks walks never look.

#include <filesystem>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "archivesafety.h"

// Under TSan, fork() inside QProcess aborts libtsan (tsan_rtl.cpp
// FindSlotAndLock CHECK) before the listing tool can run, so any slot that
// spawns bsdtar/7z can't execute. The scan logic is exercised in the non-TSan
// build configs; skip only the spawning slots here.
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
#define KARTEND_SKIP_QPROCESS_UNDER_TSAN()                                                         \
  QSKIP("QProcess fork() aborts libtsan — archive listing tools can't spawn under TSan; "          \
        "covered in non-TSan builds")
#else
#define KARTEND_SKIP_QPROCESS_UNDER_TSAN() ((void)0)
#endif

class TestArchiveSafety : public QObject {
  Q_OBJECT
private slots:
  void entryPathEscapesFlagsAbsoluteAndDotDot();
  void cleanArchivePassesScan();
  void symlinkEntryIsRejected();
  void sevenZipFallbackAcceptsPlainTarAndRejectsLinks();
  void isSecurityRejectionSeparatesUnsafeFromToolFailure();
  void missingArchiveFailsClosed();

private:
  // Builds a tar at `tarPath` from the contents of `srcDir` using bsdtar.
  // Returns false when bsdtar is unavailable (caller skips).
  static bool makeTar(const QString &tarPath, const QString &srcDir);
};

bool TestArchiveSafety::makeTar(const QString &tarPath, const QString &srcDir) {
  if (QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    return false;
  }
  QProcess tar;
  tar.setWorkingDirectory(srcDir);
  tar.start(QStringLiteral("bsdtar"), {QStringLiteral("-cf"), tarPath, QStringLiteral(".")});
  if (!tar.waitForStarted(10000) || !tar.waitForFinished(30000)) {
    return false;
  }
  return tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0;
}

void TestArchiveSafety::entryPathEscapesFlagsAbsoluteAndDotDot() {
  // Escapes by construction:
  QVERIFY(ArchiveSafety::entryPathEscapes(QStringLiteral("/etc/passwd")));
  QVERIFY(ArchiveSafety::entryPathEscapes(QStringLiteral("C:\\Users\\evil")));
  QVERIFY(ArchiveSafety::entryPathEscapes(QStringLiteral("c:/evil")));
  QVERIFY(ArchiveSafety::entryPathEscapes(QStringLiteral("\\\\host\\share\\x")));
  QVERIFY(ArchiveSafety::entryPathEscapes(QStringLiteral("../outside")));
  QVERIFY(ArchiveSafety::entryPathEscapes(QStringLiteral("nested/../../outside")));
  QVERIFY(ArchiveSafety::entryPathEscapes(QStringLiteral("..\\windows\\style")));
  // Contained paths — including the "./name" form tar listings use and a
  // name that merely CONTAINS dots:
  QVERIFY(!ArchiveSafety::entryPathEscapes(QStringLiteral("file.png")));
  QVERIFY(!ArchiveSafety::entryPathEscapes(QStringLiteral("./sub/file.png")));
  QVERIFY(!ArchiveSafety::entryPathEscapes(QStringLiteral("weird..name/file")));
}

void TestArchiveSafety::cleanArchivePassesScan() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
  QTemporaryDir src;
  QTemporaryDir out;
  QVERIFY(src.isValid() && out.isValid());
  {
    QFile f(src.path() + QStringLiteral("/plain.dat"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("clean");
  }
  const QString tarPath = out.path() + QStringLiteral("/clean.tar");
  if (!makeTar(tarPath, src.path())) {
    QSKIP("bsdtar not available to build the fixture");
  }
  const auto scan = ArchiveSafety::scanArchiveEntries(tarPath);
  QVERIFY2(scan.isOk(), qPrintable(scan.error().userFacingSummary()));
}

void TestArchiveSafety::symlinkEntryIsRejected() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
#ifdef Q_OS_WIN
  QSKIP("symlink fixture creation is POSIX-only");
#else
  QTemporaryDir src;
  QTemporaryDir out;
  QVERIFY(src.isValid() && out.isValid());
  {
    QFile f(src.path() + QStringLiteral("/decoy.dat"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("decoy");
  }
  // The zip-slip primitive's first stage: a symlink pointing outside the
  // extraction root. Its mere presence must fail the scan.
  QVERIFY(QFile::link(QStringLiteral("/tmp"), src.path() + QStringLiteral("/escape")));
  const QString tarPath = out.path() + QStringLiteral("/evil.tar");
  if (!makeTar(tarPath, src.path())) {
    QSKIP("bsdtar not available to build the fixture");
  }
  const auto scan = ArchiveSafety::scanArchiveEntries(tarPath);
  QVERIFY2(scan.isError(), "a symlink entry must fail the safety scan");
#endif
}

void TestArchiveSafety::sevenZipFallbackAcceptsPlainTarAndRejectsLinks() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
#ifdef Q_OS_WIN
  QSKIP("PATH sandbox + symlink fixture creation are POSIX-only here");
#else
  // scanArchiveEntries prefers bsdtar whenever it is on PATH, so the 7z
  // fallback is only reachable with a PATH that offers 7z alone. That branch
  // has its own parser — and 7z's tar codec emits EMPTY "Symbolic Link =" /
  // "Hard Link =" fields on every entry (regular files included), so a
  // presence-only check rejects all tars. These three fixtures pin the
  // value-based parsing: plain passes, real symlink/hardlink targets reject.
  const QString sevenZip = QStandardPaths::findExecutable(QStringLiteral("7z"));
  if (sevenZip.isEmpty()) {
    QSKIP("7z not available");
  }
  QTemporaryDir src;
  QTemporaryDir out;
  QTemporaryDir bin;
  QVERIFY(src.isValid() && out.isValid() && bin.isValid());
  {
    QFile f(src.path() + QStringLiteral("/plain.dat"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("clean");
  }
  const QString plainTar = out.path() + QStringLiteral("/plain.tar");
  if (!makeTar(plainTar, src.path())) {
    QSKIP("bsdtar not available to build the fixtures");
  }
  QVERIFY(QFile::link(QStringLiteral("/tmp"), src.path() + QStringLiteral("/escape")));
  const QString symTar = out.path() + QStringLiteral("/sym.tar");
  QVERIFY(makeTar(symTar, src.path()));
  QVERIFY(QFile::remove(src.path() + QStringLiteral("/escape")));
  std::error_code hardErr;
  std::filesystem::create_hard_link((src.path() + QStringLiteral("/plain.dat")).toStdString(),
                                    (src.path() + QStringLiteral("/alias.dat")).toStdString(),
                                    hardErr);
  QVERIFY2(!hardErr, "hardlink fixture creation failed");
  const QString hardTar = out.path() + QStringLiteral("/hard.tar");
  QVERIFY(makeTar(hardTar, src.path()));

  // A PATH holding only a 7z symlink hides bsdtar from findExecutable and
  // still lets QProcess resolve "7z". Restore before asserting — a failing
  // QVERIFY returns out of the slot and must not leak the stripped PATH
  // into later slots.
  QVERIFY(QFile::link(sevenZip, bin.path() + QStringLiteral("/7z")));
  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", bin.path().toUtf8());
  const auto plainScan = ArchiveSafety::scanArchiveEntries(plainTar);
  const auto symScan = ArchiveSafety::scanArchiveEntries(symTar);
  const auto hardScan = ArchiveSafety::scanArchiveEntries(hardTar);
  qputenv("PATH", oldPath);

  QVERIFY2(plainScan.isOk(), qPrintable(plainScan.error().userFacingSummary()));
  QVERIFY2(symScan.isError(), "7z fallback must reject a symlink entry");
  QVERIFY(symScan.error().userFacingSummary().contains(QStringLiteral("symlink")));
  QVERIFY2(hardScan.isError(), "7z fallback must reject a hardlink entry");
  QVERIFY(hardScan.error().userFacingSummary().contains(QStringLiteral("hardlink")));
#endif
}

void TestArchiveSafety::isSecurityRejectionSeparatesUnsafeFromToolFailure() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
  // The per-tool scan + isSecurityRejection predicate are what let the launch
  // extractor fall through to the next tool ONLY on a format failure while
  // still aborting hard on an unsafe entry. A symlink verdict must read as a
  // security rejection; a tool that isn't installed (a stand-in for "can't
  // list this format") must NOT.
  if (QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    QSKIP("bsdtar not available to build the fixture");
  }
#ifndef Q_OS_WIN
  QTemporaryDir src;
  QTemporaryDir out;
  QVERIFY(src.isValid() && out.isValid());
  {
    QFile f(src.path() + QStringLiteral("/decoy.dat"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("decoy");
  }
  QVERIFY(QFile::link(QStringLiteral("/tmp"), src.path() + QStringLiteral("/escape")));
  const QString symTar = out.path() + QStringLiteral("/evil.tar");
  QVERIFY(makeTar(symTar, src.path()));

  const auto unsafe = ArchiveSafety::scanArchiveEntriesWithTool(symTar, QStringLiteral("bsdtar"));
  QVERIFY2(unsafe.isError(), "a symlink entry must fail the per-tool scan");
  QVERIFY2(ArchiveSafety::isSecurityRejection(unsafe.error()),
           "a symlink verdict must read as a security rejection (abort, don't retry)");
#endif

  // A tool that can't process the archive (here: not installed) is NOT a
  // security rejection — the launch extractor is free to try the next tool.
  QTemporaryDir cleanSrc;
  QTemporaryDir cleanOut;
  QVERIFY(cleanSrc.isValid() && cleanOut.isValid());
  {
    QFile f(cleanSrc.path() + QStringLiteral("/plain.dat"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("clean");
  }
  const QString cleanTar = cleanOut.path() + QStringLiteral("/clean.tar");
  QVERIFY(makeTar(cleanTar, cleanSrc.path()));
  const auto toolMissing =
      ArchiveSafety::scanArchiveEntriesWithTool(cleanTar, QStringLiteral("no-such-tool-xyz"));
  QVERIFY2(toolMissing.isError(), "an unavailable listing tool must error");
  QVERIFY2(!ArchiveSafety::isSecurityRejection(toolMissing.error()),
           "a tool-unavailable failure must NOT read as a security rejection");
}

void TestArchiveSafety::missingArchiveFailsClosed() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
  if (QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
    QSKIP("no listing tool available");
  }
  const auto scan = ArchiveSafety::scanArchiveEntries(QStringLiteral("/nonexistent/archive.tar"));
  QVERIFY2(scan.isError(), "an unlistable archive must fail closed");
}

QTEST_MAIN(TestArchiveSafety)
#include "test_archivesafety.moc"
