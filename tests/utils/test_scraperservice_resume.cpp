// Regression test for ScraperService resume: the persisted run
// summary must round-trip the media-written count. Dropping it made a
// resumed scrape come back showing "0 media" even though earlier media
// had already been downloaded.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTest>

#include "scraperservice.h"

using Scraper::ScraperService;

class TestScraperServiceResume : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void loadRestoresMediaWrittenCount();
  void loadDefaultsMediaWrittenToZeroForLegacyFile();

private:
  static void writePendingFile(const QByteArray &json);
  // Mirrors ScraperService::pendingStateFilePath() (which is private).
  // If the production path ever moves, loadPendingState() will read
  // nothing and the isValid() assertions below fail loudly.
  static QString pendingFilePath();
};

QString TestScraperServiceResume::pendingFilePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(dir).filePath(QStringLiteral("pending-scrape.json"));
}

void TestScraperServiceResume::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(QStringLiteral("kartend-test-scraperservice-resume"));
}

void TestScraperServiceResume::writePendingFile(const QByteArray &json) {
  const QString path = pendingFilePath();
  QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(json);
  f.close();
}

void TestScraperServiceResume::init() {
  // Each test starts from a clean slate so a stale file can't leak in.
  QFile::remove(pendingFilePath());
}

void TestScraperServiceResume::cleanup() {
  QFile::remove(pendingFilePath());
}

void TestScraperServiceResume::loadRestoresMediaWrittenCount() {
  // A pending-scrape snapshot with progress already made: 12 items
  // scraped and 27 media files written before the interruption.
  const QByteArray json = R"json({
    "version": 1,
    "started_at_unix_ms": 1715000000000,
    "mode": "auto",
    "write_metadata": true,
    "media_filter": ["front", "screenshot"],
    "summary_so_far": {
      "scraped": 12,
      "skipped": 3,
      "errors": 1,
      "media_written": 27,
      "first_failures": ["foo.bin: timeout"]
    },
    "queue": [
      {
        "collection_index": 0,
        "collection_uuid": "uuid-1",
        "collection_name": "Coll",
        "artwork_dir": "/art",
        "remaining": ["/m/a.bin", "/m/b.bin"]
      }
    ]
  })json";
  writePendingFile(json);

  ScraperService service;
  const ScraperService::PendingState state = service.loadPendingState(/*consumeOnLoad=*/false);

  QVERIFY(state.isValid());
  // The media-written count must survive the round-trip — this is the
  // value the dialog shows as "N media" when the run resumes.
  QCOMPARE(state.summarySoFar.mediaWritten, 27);
  // Sibling counters were already persisted; assert them too so a
  // future change can't quietly regress the whole summary block.
  QCOMPARE(state.summarySoFar.scraped, 12);
  QCOMPARE(state.summarySoFar.skipped, 3);
  QCOMPARE(state.summarySoFar.errors, 1);
}

void TestScraperServiceResume::loadDefaultsMediaWrittenToZeroForLegacyFile() {
  // A snapshot written before media_written was persisted (older
  // builds) must still load — the missing field defaults to 0 rather
  // than rejecting the whole resume.
  const QByteArray legacyJson = R"json({
    "version": 1,
    "started_at_unix_ms": 1715000000000,
    "mode": "auto",
    "write_metadata": true,
    "summary_so_far": {
      "scraped": 5,
      "skipped": 0,
      "errors": 0
    },
    "queue": [
      {
        "collection_index": 0,
        "collection_uuid": "uuid-1",
        "collection_name": "Coll",
        "artwork_dir": "/art",
        "remaining": ["/m/a.bin"]
      }
    ]
  })json";
  writePendingFile(legacyJson);

  ScraperService service;
  const ScraperService::PendingState state = service.loadPendingState(/*consumeOnLoad=*/false);

  QVERIFY(state.isValid());
  QCOMPARE(state.summarySoFar.mediaWritten, 0);
  QCOMPARE(state.summarySoFar.scraped, 5);
}

QTEST_MAIN(TestScraperServiceResume)
#include "test_scraperservice_resume.moc"
