/**
 * @file test_nointrodownloader.cpp
 * @brief Tests for the testable slice of the No-Intro downloader (Kartend-m6qsb.16).
 *
 * The 4-step network flow is runtime-gated (live, anti-bot site) and verified
 * manually; what is unit-testable is extractDatsTo() — the pack-unpacking step
 * — which we exercise against a real zip built with the `zip` tool, skipping
 * cleanly when no archive tooling is present (mirrors test_romhasher).
 */

#include "nointrodownloader.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestNoIntroDownloader : public QObject {
  Q_OBJECT

private slots:
  void extractDatsToPullsDatsFlattened();
  void extractDatsToErrorsOnMissingPack();
};

void TestNoIntroDownloader::extractDatsToPullsDatsFlattened() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP(
      "Kartend-dhhh6: this test forks an external extractor (and builds its fixture by forking "
      "zip) on the TEST thread to exercise the real unpack path — a single-threaded fork with no "
      "cross-thread state, so faking the decompressor would test the fake, not real extraction. "
      "Kartend-yxco2 investigated a seam and found none worthwhile: extractDatsTo's only "
      "cross-thread state on the audit worker is a std::atomic cancel token (race-free) handed off "
      "via QFuture, so TSan has no real race to catch; the flatten/exclusion logic asserted here "
      "is pure post-extraction file bookkeeping.");
#endif
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
    QSKIP("zip not available — cannot build the pack fixture");
  }
  if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
    QSKIP("no extractor on PATH");
  }

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Mirror a real No-Intro pack: DATs nested one folder deep, plus a non-DAT
  // that must NOT be imported.
  const QString src = dir.filePath(QStringLiteral("src/No-Intro"));
  QVERIFY(QDir().mkpath(src));
  for (const QString &n : {QStringLiteral("Reference Video Set (2026).dat"),
                           QStringLiteral("Audio Masters (2026).dat")}) {
    QFile f(src + "/" + n);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<datafile><header><name>x</name></header></datafile>");
  }
  {
    QFile f(src + "/readme.txt");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not a dat");
  }
  const QString zipPath = dir.filePath(QStringLiteral("pack.zip"));
  QProcess zip;
  zip.setWorkingDirectory(dir.filePath(QStringLiteral("src")));
  zip.start(QStringLiteral("zip"),
            {QStringLiteral("-q"), QStringLiteral("-r"), zipPath, QStringLiteral("No-Intro")});
  QVERIFY(zip.waitForFinished(5000));
  QCOMPARE(zip.exitCode(), 0);

  const QString lib = dir.filePath(QStringLiteral("library"));
  auto res = NoIntroDownload::extractDatsTo(zipPath, lib, {});
  QVERIFY2(res.isOk(), qPrintable(res.isError() ? res.error().message : QString()));
  QCOMPARE(res.value().size(), 2); // the two DATs, flattened; readme.txt excluded
  QVERIFY(QFile::exists(lib + "/Reference Video Set (2026).dat"));
  QVERIFY(QFile::exists(lib + "/Audio Masters (2026).dat"));
  QVERIFY(!QFile::exists(lib + "/readme.txt"));
}

void TestNoIntroDownloader::extractDatsToErrorsOnMissingPack() {
  QTemporaryDir dir;
  auto res = NoIntroDownload::extractDatsTo(dir.filePath(QStringLiteral("absent.zip")),
                                            dir.filePath(QStringLiteral("lib")), {});
  QVERIFY(res.isError());
}

QTEST_MAIN(TestNoIntroDownloader)
#include "test_nointrodownloader.moc"
