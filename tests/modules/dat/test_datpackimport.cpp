/**
 * @file test_datpackimport.cpp
 * @brief Unit tests for source-agnostic DAT-pack import (Kartend-m6qsb.22).
 *
 * The folder and single-file paths need no external tooling; the archive path
 * delegates to NoIntroDownload::extractDatsTo (covered by its own gated test),
 * so it's exercised lightly here only when a zip tool is present.
 */

#include "datpackimport.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeFile(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

} // namespace

class TestDatPackImport : public QObject {
  Q_OBJECT

private slots:
  void importsFolderOfDatsFlattened();
  void importsSingleDatFile();
  void rejectsNonDatFile();
  void errorsOnMissingSourceAndEmptyFolder();
  void importsZipWhenToolPresent();

private:
  QTemporaryDir m_dir;
  int m_n = 0;
  QString fresh(const QString &leaf) {
    const QString p = m_dir.filePath(QStringLiteral("c%1-%2").arg(m_n++).arg(leaf));
    QDir().mkpath(QFileInfo(p).path());
    return p;
  }
};

void TestDatPackImport::importsFolderOfDatsFlattened() {
  const QString src = fresh(QStringLiteral("src"));
  QDir().mkpath(src + QStringLiteral("/nested"));
  writeFile(src + QStringLiteral("/a.dat"), "<datafile/>");
  writeFile(src + QStringLiteral("/nested/b.dat"), "<datafile/>");
  writeFile(src + QStringLiteral("/notes.txt"), "ignore");
  const QString lib = fresh(QStringLiteral("lib"));

  auto res = DatPackImport::importInto(src, lib);
  QVERIFY2(res.isOk(), qPrintable(res.isError() ? res.error().message : QString()));
  QCOMPARE(res.value().size(), 2); // both .dat, flattened; .txt skipped
  QVERIFY(QFile::exists(lib + QStringLiteral("/a.dat")));
  QVERIFY(QFile::exists(lib + QStringLiteral("/b.dat")));
  QVERIFY(!QFile::exists(lib + QStringLiteral("/notes.txt")));
}

void TestDatPackImport::importsSingleDatFile() {
  const QString datDir = fresh(QStringLiteral("one"));
  QDir().mkpath(datDir);
  const QString dat = datDir + QStringLiteral("/Catalogue.dat");
  writeFile(dat, "<datafile/>");
  const QString lib = fresh(QStringLiteral("lib"));

  auto res = DatPackImport::importInto(dat, lib);
  QVERIFY(res.isOk());
  QCOMPARE(res.value().size(), 1);
  QVERIFY(QFile::exists(lib + QStringLiteral("/Catalogue.dat")));
}

void TestDatPackImport::rejectsNonDatFile() {
  const QString d = fresh(QStringLiteral("bad"));
  QDir().mkpath(d);
  const QString txt = d + QStringLiteral("/readme.txt");
  writeFile(txt, "nope");
  auto res = DatPackImport::importInto(txt, fresh(QStringLiteral("lib")));
  QVERIFY(res.isError());
}

void TestDatPackImport::errorsOnMissingSourceAndEmptyFolder() {
  QVERIFY(DatPackImport::importInto(m_dir.filePath(QStringLiteral("nope")),
                                    fresh(QStringLiteral("lib")))
              .isError());
  const QString empty = fresh(QStringLiteral("empty"));
  QDir().mkpath(empty);
  QVERIFY(DatPackImport::importInto(empty, fresh(QStringLiteral("lib"))).isError());
}

void TestDatPackImport::importsZipWhenToolPresent() {
  if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty() ||
      (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty() &&
       QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty() &&
       QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty())) {
    QSKIP("zip/extractor not available");
  }
  const QString src = fresh(QStringLiteral("zsrc"));
  QDir().mkpath(src);
  writeFile(src + QStringLiteral("/Set.dat"), "<datafile/>");
  const QString zip = fresh(QStringLiteral("pack.zip"));
  QProcess p;
  p.setWorkingDirectory(src);
  p.start(QStringLiteral("zip"), {QStringLiteral("-q"), zip, QStringLiteral("Set.dat")});
  QVERIFY(p.waitForFinished(5000));
  QCOMPARE(p.exitCode(), 0);

  const QString lib = fresh(QStringLiteral("lib"));
  auto res = DatPackImport::importInto(zip, lib);
  QVERIFY2(res.isOk(), qPrintable(res.isError() ? res.error().message : QString()));
  QCOMPARE(res.value().size(), 1);
  QVERIFY(QFile::exists(lib + QStringLiteral("/Set.dat")));
}

QTEST_MAIN(TestDatPackImport)
#include "test_datpackimport.moc"
