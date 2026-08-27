// Pre-extraction archive safety scan: symlink/hardlink entries and
// path-escape attempts must be rejected BEFORE any extractor writes a byte —
// the write-through-symlink zip-slip class lands its payload outside the
// extraction root, where the post-extraction NoSymLinks walks never look.

#include <filesystem>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
  void zeroParsedEntriesFailsClosedWithoutSecurityVerdict();
  void sevenZipLinkVocabularyGuardAcceptsEverySupportedFormat();
  void sevenZipMissingLinkVocabularyFailsClosedWithoutSecurityVerdict();

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

void TestArchiveSafety::zeroParsedEntriesFailsClosedWithoutSecurityVerdict() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
  // A listing run that yields zero parseable entries used to inspect NOTHING
  // and return success — fail-open on any output shape the parser didn't
  // recognise. The scanners now refuse that case. The fixture is a VALID but
  // genuinely empty tar (created from an empty file list): bsdtar's flat
  // listing cannot distinguish "empty archive" from "unparseable output"
  // (both print nothing on a zero-exit run), so its scanner must refuse —
  // with a NON-security error, so scanArchiveEntries' fallback chain still
  // consults the next tool. 7z CAN tell the two apart (its "----------"
  // entry-table separator is printed even with no rows after it), so the 7z
  // scanner passes the same empty archive.
  //
  // There is no parse seam to inject a fabricated listing (runListing is
  // fused to QProcess), so this pins the behaviour through the real tools.
  if (QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    QSKIP("bsdtar not available to build the fixture");
  }
  QTemporaryDir out;
  QVERIFY(out.isValid());
  // An empty -T/--files-from source produces a valid tar with zero entries
  // (unlike taring an empty directory, which still emits a "./" entry).
  const QString emptyListPath = out.path() + QStringLiteral("/empty-list.txt");
  {
    QFile f(emptyListPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
  }
  const QString tarPath = out.path() + QStringLiteral("/empty.tar");
  QProcess tar;
  tar.start(QStringLiteral("bsdtar"),
            {QStringLiteral("-cf"), tarPath, QStringLiteral("-T"), emptyListPath});
  QVERIFY(tar.waitForStarted(10000) && tar.waitForFinished(30000));
  QVERIFY(tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0);
  QVERIFY(QFileInfo(tarPath).size() > 0); // empty tar ≠ empty file (zero blocks)

  const auto bsdtarScan =
      ArchiveSafety::scanArchiveEntriesWithTool(tarPath, QStringLiteral("bsdtar"));
  QVERIFY2(bsdtarScan.isError(), "zero parsed entries must not scan as safe");
  QVERIFY2(!ArchiveSafety::isSecurityRejection(bsdtarScan.error()),
           "an uninterpretable listing is not a security verdict — the "
           "fallback-tool chain must stay available");

  if (!QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
    const auto sevenZipScan =
        ArchiveSafety::scanArchiveEntriesWithTool(tarPath, QStringLiteral("7z"));
    QVERIFY2(sevenZipScan.isOk(),
             qPrintable(QStringLiteral("7z must pass a genuinely empty archive (separator with "
                                       "no rows): %1")
                            .arg(sevenZipScan.isError() ? sevenZipScan.error().userFacingSummary()
                                                        : QString())));
  }
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

// Kartend-mm5sp: scanWith7z now requires that the field carrying THIS codec's
// link verdict actually appeared before it reports safe. The vocabulary differs
// per codec (measured: tar emits "Symbolic Link ="/"Hard Link =" and no
// Attributes; zip and 7z emit "Attributes =" and no link keys; gzip/bzip2/xz
// wrap one stream and emit neither), so the guard is the obvious way to
// introduce a FALSE POSITIVE that refuses real archives on a 7z-only host.
// This pins every container format ExtensionUtils::archiveBaseExtensions
// admits and that can be built here.
void TestArchiveSafety::sevenZipLinkVocabularyGuardAcceptsEverySupportedFormat() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
#ifdef Q_OS_WIN
  QSKIP("PATH sandbox is POSIX-only here");
#else
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

  const auto run = [](const QString &program, const QStringList &args, const QString &cwd) {
    QProcess p;
    p.setWorkingDirectory(cwd);
    p.start(program, args);
    return p.waitForStarted(10000) && p.waitForFinished(30000) &&
           p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
  };

  // Build one archive per codec family. Each is skipped individually rather
  // than failing the slot, so a host missing a packer still runs the rest.
  QStringList fixtures;
  if (run(sevenZip,
          {QStringLiteral("a"), QStringLiteral("-bso0"), QStringLiteral("-bsp0"),
           out.path() + QStringLiteral("/a.zip"), src.path() + QStringLiteral("/*")},
          src.path())) {
    fixtures << out.path() + QStringLiteral("/a.zip");
  }
  if (run(sevenZip,
          {QStringLiteral("a"), QStringLiteral("-bso0"), QStringLiteral("-bsp0"),
           out.path() + QStringLiteral("/a.7z"), src.path() + QStringLiteral("/*")},
          src.path())) {
    fixtures << out.path() + QStringLiteral("/a.7z");
  }
  const QString plainTar = out.path() + QStringLiteral("/a.tar");
  if (makeTar(plainTar, src.path())) {
    fixtures << plainTar;
    // gzip/bzip2/xz: 7z sees the CONTAINER — one entry for the wrapped tar,
    // with no link vocabulary at all. Those codecs cannot express a link, so
    // the guard must exempt them rather than refuse.
    if (!QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
      for (const auto &pair : {std::pair{QStringLiteral("-czf"), QStringLiteral("/a.tar.gz")},
                               std::pair{QStringLiteral("-cjf"), QStringLiteral("/a.tar.bz2")},
                               std::pair{QStringLiteral("-cJf"), QStringLiteral("/a.tar.xz")}}) {
        const QString path = out.path() + pair.second;
        if (run(QStringLiteral("bsdtar"), {pair.first, path, QStringLiteral(".")}, src.path())) {
          fixtures << path;
        }
      }
    }
  }
  if (fixtures.isEmpty()) {
    QSKIP("no archive fixture could be built");
  }

  // 7z-only PATH so the fallback branch under test is the one that runs.
  QVERIFY(QFile::link(sevenZip, bin.path() + QStringLiteral("/7z")));
  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", bin.path().toUtf8());
  QList<std::pair<QString, QString>> failures;
  for (const QString &fixture : fixtures) {
    const auto scan = ArchiveSafety::scanArchiveEntries(fixture);
    if (!scan.isOk()) {
      failures.append({fixture, scan.error().userFacingSummary()});
    }
  }
  qputenv("PATH", oldPath);

  for (const auto &failure : failures) {
    qWarning("false positive: %s -> %s", qPrintable(failure.first), qPrintable(failure.second));
  }
  QVERIFY2(failures.isEmpty(), "the link-vocabulary guard rejected a legitimate archive");
#endif
}

// The gap itself: a listing that parses into entries while the link field is
// named something we do not recognise. The real 7z here never does that (its
// -slt keys are locale-invariant — verified across C/fr_FR/de_DE), so the
// scenario is staged with a stub `7z` on a sandboxed PATH that emits a Type =
// tar listing with the link fields renamed. Before this guard the scan reached
// no link verdict and still reported SAFE.
void TestArchiveSafety::sevenZipMissingLinkVocabularyFailsClosedWithoutSecurityVerdict() {
  KARTEND_SKIP_QPROCESS_UNDER_TSAN();
#ifdef Q_OS_WIN
  QSKIP("shell-stub PATH sandbox is POSIX-only");
#else
  QTemporaryDir out;
  QTemporaryDir bin;
  QVERIFY(out.isValid() && bin.isValid());

  // A non-empty file to stand in for the archive; the stub never reads it.
  const QString fakeArchive = out.path() + QStringLiteral("/mystery.tar");
  {
    QFile f(fakeArchive);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not really a tar, the stub does the talking");
  }

  const QString stub = bin.path() + QStringLiteral("/7z");
  {
    QFile f(stub);
    QVERIFY(f.open(QIODevice::WriteOnly));
    // printf is a SHELL BUILTIN — deliberate. PATH is stripped to the sandbox
    // below, so a stub calling `cat` dies with "command not found" and the scan
    // then fails for the wrong reason ("Archive listing tool failed"), which
    // silently turns this into a test that passes without exercising anything.
    f.write("#!/bin/sh\n"
            "printf '%s\\n' \\\n"
            "  'Listing archive: mystery.tar' \\\n"
            "  '' \\\n"
            "  'Type = tar' \\\n"
            "  '' \\\n"
            "  '----------' \\\n"
            "  'Path = payload.bin' \\\n"
            "  'Size = 5' \\\n"
            "  'Verknuepfung = ' \\\n"
            "  '' \\\n"
            "  'Path = sub/other.bin' \\\n"
            "  'Size = 7' \\\n"
            "  'Verknuepfung = '\n");
    f.close();
    QVERIFY(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                             QFile::ReadUser | QFile::ExeUser));
  }

  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", bin.path().toUtf8());
  const auto scan = ArchiveSafety::scanArchiveEntriesWithTool(fakeArchive, QStringLiteral("7z"));
  qputenv("PATH", oldPath);

  // The stub must have RUN — a "tool failed" error would mean the listing was
  // never parsed and this slot proved nothing.
  QVERIFY2(
      !scan.isError() || !scan.error().userFacingSummary().contains(QStringLiteral("tool failed")),
      qPrintable(QStringLiteral("stub 7z did not run: %1").arg(scan.error().userFacingSummary())));
  QVERIFY2(scan.isError(), "entries parsed but no link verdict possible must not scan as safe");
  // Fails closed as a TOOL failure, not a security verdict, so
  // scanArchiveEntries still falls through to bsdtar on a host that has it —
  // only the 7z-only host of the reported scenario refuses outright.
  QVERIFY2(!ArchiveSafety::isSecurityRejection(scan.error()),
           "a missing link vocabulary is an uninterpretable listing, not an unsafe entry");
#endif
}

QTEST_MAIN(TestArchiveSafety)
#include "test_archivesafety.moc"
