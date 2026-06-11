// Tests for ScrapeLogger: enabling it must tee `kartend.scrape*`
// category messages to the on-disk log file, leave other categories
// alone, and stop teeing once disabled.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTest>

#include "../../support/testsandbox.h"
#include "scrapelogger.h"

// A scrape-prefixed category (teed) and a sibling that is not.
Q_LOGGING_CATEGORY(lcScrapeLoggerTest, "kartend.scrape.loggertest")
Q_LOGGING_CATEGORY(lcOtherLoggerTest, "kartend.other.loggertest")

class TestScrapeLogger : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanup();
  void logFilePathIsScrapeLog();
  void enabledTeesScrapeCategoryMessages();
  void disabledStopsTeeing();

private:
  static QByteArray readLog();
};

void TestScrapeLogger::initTestCase() {
  // Test-scoped config dir so we never touch the real scrape.log.
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scrapelogger"));
}

void TestScrapeLogger::cleanup() {
  // Each test ends with logging off + the log files cleared, so the
  // next test starts from a known state.
  ScrapeLogger::setEnabled(false);
  QFile::remove(ScrapeLogger::logFilePath());
  QFile::remove(ScrapeLogger::logFilePath() + QStringLiteral(".old"));
}

QByteArray TestScrapeLogger::readLog() {
  QFile f(ScrapeLogger::logFilePath());
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

void TestScrapeLogger::logFilePathIsScrapeLog() {
  // The path must resolve to scrape.log inside the app config dir.
  const QString path = ScrapeLogger::logFilePath();
  QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("scrape.log"));
  const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QCOMPARE(QFileInfo(path).absolutePath(), QDir(cfgDir).absolutePath());
}

void TestScrapeLogger::enabledTeesScrapeCategoryMessages() {
  ScrapeLogger::setEnabled(true);
  QVERIFY(ScrapeLogger::isEnabled());
  QVERIFY(QFile::exists(ScrapeLogger::logFilePath()));

  // A scrape-category message must land in the file...
  qCWarning(lcScrapeLoggerTest) << "MARKER-scrape-line-a91f";
  // ...while a non-scrape category must not, even though logging is on.
  qCWarning(lcOtherLoggerTest) << "MARKER-other-line-b27c";

  const QByteArray log = readLog();
  QVERIFY(log.contains("MARKER-scrape-line-a91f"));
  QVERIFY(log.contains("kartend.scrape.loggertest"));
  QVERIFY(!log.contains("MARKER-other-line-b27c"));
}

void TestScrapeLogger::disabledStopsTeeing() {
  ScrapeLogger::setEnabled(true);
  qCWarning(lcScrapeLoggerTest) << "MARKER-before-disable-5d0e";
  const qint64 sizeWhileEnabled = QFileInfo(ScrapeLogger::logFilePath()).size();
  QVERIFY(sizeWhileEnabled > 0);

  ScrapeLogger::setEnabled(false);
  QVERIFY(!ScrapeLogger::isEnabled());

  // A scrape message emitted after disabling must not reach the file —
  // the file is left on disk but no longer written to.
  qCWarning(lcScrapeLoggerTest) << "MARKER-after-disable-3ab8";
  const QByteArray log = readLog();
  QVERIFY(log.contains("MARKER-before-disable-5d0e"));
  QVERIFY(!log.contains("MARKER-after-disable-3ab8"));
  QCOMPARE(QFileInfo(ScrapeLogger::logFilePath()).size(), sizeWhileEnabled);
}

QTEST_MAIN(TestScrapeLogger)
#include "test_scrapelogger.moc"
