// Tests for VideoUtils per-item video file lookup.
#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "videoutils.h"

class TestVideoUtils : public QObject {
  Q_OBJECT
private slots:
  void videoBaseExtensions_returnsKnownExtensions();
  void videoFilters_prependsWildcardDot();
  void findVideoForFile_returnsEmpty_whenInputsEmpty();
  void findVideoForFile_returnsEmpty_whenDirectoryMissing();
  void findVideoForFile_findsLowercaseExtension();
  void findVideoForFile_findsUppercaseExtension();
  void findVideoForFile_prefersFirstExtensionInList();
  void findVideoForFile_matchesByCompleteBaseName();
  void findVideoForFile_returnsEmptyWhenNoMatch();
};

void TestVideoUtils::videoBaseExtensions_returnsKnownExtensions() {
  const QStringList &exts = VideoUtils::videoBaseExtensions();
  QVERIFY(exts.contains("mp4"));
  QVERIFY(exts.contains("webm"));
  QVERIFY(exts.contains("mkv"));
  QVERIFY(exts.contains("mov"));
  QVERIFY(exts.contains("avi"));
}

void TestVideoUtils::videoFilters_prependsWildcardDot() {
  const QStringList filters = VideoUtils::videoFilters();
  QCOMPARE(filters.size(), VideoUtils::videoBaseExtensions().size());
  for (const QString &f : filters) {
    QVERIFY(f.startsWith("*."));
  }
}

void TestVideoUtils::findVideoForFile_returnsEmpty_whenInputsEmpty() {
  QCOMPARE(VideoUtils::findVideoForFile(QString(), "/tmp"), QString());
  QCOMPARE(VideoUtils::findVideoForFile("game.smc", QString()), QString());
}

void TestVideoUtils::findVideoForFile_returnsEmpty_whenDirectoryMissing() {
  QCOMPARE(VideoUtils::findVideoForFile("game.smc", "/this/path/does/not/exist/123"), QString());
}

void TestVideoUtils::findVideoForFile_findsLowercaseExtension() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile f(tmp.filePath("game.mp4"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();

  const QString result = VideoUtils::findVideoForFile("game.smc", tmp.path());
  QCOMPARE(result, QFileInfo(tmp.filePath("game.mp4")).absoluteFilePath());
}

void TestVideoUtils::findVideoForFile_findsUppercaseExtension() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile f(tmp.filePath("game.MP4"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();

  const QString result = VideoUtils::findVideoForFile("game.smc", tmp.path());
  // On Linux's case-sensitive filesystem the matcher's uppercase probe
  // finds "game.MP4"; on Windows's case-insensitive filesystem the
  // lowercase probe finds the same file and the returned string carries
  // lowercase casing. Qt's canonicalFilePath doesn't normalise case on
  // Windows (only resolves symlinks/dots), so compare via case-
  // insensitive string match plus a file-exists guard.
  const QString expected = QFileInfo(tmp.filePath("game.MP4")).absoluteFilePath();
  QVERIFY2(QFile::exists(result), qPrintable(QString("result does not exist: %1").arg(result)));
  QVERIFY2(
      QString::compare(result, expected, Qt::CaseInsensitive) == 0,
      qPrintable(QString("result %1 != expected %2 (case-insensitive)").arg(result, expected)));
}

void TestVideoUtils::findVideoForFile_prefersFirstExtensionInList() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  // Create both .mp4 and .webm — mp4 listed first should win.
  QFile a(tmp.filePath("game.mp4"));
  QVERIFY(a.open(QIODevice::WriteOnly));
  a.close();
  QFile b(tmp.filePath("game.webm"));
  QVERIFY(b.open(QIODevice::WriteOnly));
  b.close();

  const QString result = VideoUtils::findVideoForFile("game.smc", tmp.path());
  QVERIFY(result.endsWith(".mp4"));
}

void TestVideoUtils::findVideoForFile_matchesByCompleteBaseName() {
  // "Super.Mario.World.smc" -> base name "Super.Mario.World"
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile f(tmp.filePath("Super.Mario.World.mp4"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();

  const QString result = VideoUtils::findVideoForFile("Super.Mario.World.smc", tmp.path());
  QCOMPARE(result, QFileInfo(tmp.filePath("Super.Mario.World.mp4")).absoluteFilePath());
}

void TestVideoUtils::findVideoForFile_returnsEmptyWhenNoMatch() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile f(tmp.filePath("other.mp4"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();

  QCOMPARE(VideoUtils::findVideoForFile("game.smc", tmp.path()), QString());
}

QTEST_MAIN(TestVideoUtils)
#include "test_videoutils.moc"
