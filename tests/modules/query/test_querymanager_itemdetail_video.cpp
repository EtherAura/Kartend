// Regression test for Kartend-h7xnr.11: loadItemDetail's preview-video lookup
// must use the documented two-root chain — the explicit videoDirectory first,
// then {artworkDir}/video/, the single-root layout the scraper writes to.
//
//   videoLookupUsesTwoRootChain
//     With no explicit videoDirectory, a video under {artworkDir}/video/ must
//     resolve; with both roots populated, the explicit videoDirectory wins.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "../../support/scopeexit.h"
#include "../../support/testsandbox.h"
#include "../../support/workersignalspy.h"
#include "itemdetaildata.h"
#include "querymanager.h"
#include "sessionmanager.h"

namespace {
void writeStubFile(const QString &path) {
  QFile f(path);
  QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable("Failed to create " + path));
  f.write("stub");
  f.close();
}
} // namespace

class TestQueryManagerItemDetailVideo : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void videoLookupUsesTwoRootChain();
};

void TestQueryManagerItemDetailVideo::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-itemdetail-video"));
}

void TestQueryManagerItemDetailVideo::videoLookupUsesTwoRootChain() {
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");
  QTemporaryDir artworkDir;
  QVERIFY2(artworkDir.isValid(), "Failed to create temporary artwork directory");
  QTemporaryDir videoDir;
  QVERIFY2(videoDir.isValid(), "Failed to create temporary video directory");

  const QString filePath = QDir(mediaDir.path()).absoluteFilePath(QStringLiteral("sample.bin"));
  writeStubFile(filePath);

  // The scraper's single-root layout: {artworkDir}/video/{baseName}.mp4.
  QVERIFY(QDir(artworkDir.path()).mkpath(QStringLiteral("video")));
  const QString scrapedVideo =
      QDir(artworkDir.path()).absoluteFilePath(QStringLiteral("video/sample.mp4"));
  writeStubFile(scrapedVideo);

  // A competing match under the explicit videoDirectory root.
  const QString explicitVideo =
      QDir(videoDir.path()).absoluteFilePath(QStringLiteral("sample.mp4"));
  writeStubFile(explicitVideo);

  SessionManager sessionManager;
  auto *qm = new QueryManager(&sessionManager);
  QThread worker;
  qm->moveToThread(&worker);
  worker.start();

  const auto workerCleanup = KartendTest::makeScopeExit([&]() {
    if (qm) {
      qm->requestCancelScan();
      qm->deleteLater();
    }
    if (worker.isRunning()) {
      worker.quit();
      if (!worker.wait(10'000)) {
        worker.terminate();
        worker.wait(10'000);
      }
    }
  });

  QMetaObject::invokeMethod(qm, &QueryManager::initDatabase, Qt::BlockingQueuedConnection);

  // WorkerSignalSpy (not QSignalSpy): qm lives on the worker thread, and
  // Qt 6.4's QSignalSpy connects DirectConnection with no locking.
  WorkerSignalSpy detailSpy(qm, &QueryManager::itemDetailLoaded);
  QVERIFY(detailSpy.isValid());

  // No explicit videoDirectory: the {artworkDir}/video/ fallback must resolve.
  QMetaObject::invokeMethod(
      qm,
      [qm, filePath, art = artworkDir.path()]() {
        qm->loadItemDetail(1, QString(), filePath, art, QString(), QString());
      },
      Qt::QueuedConnection);
  QVERIFY2(detailSpy.wait(10'000), "Timed out waiting for itemDetailLoaded (fallback root)");
  {
    const QList<QVariant> args = detailSpy.takeFirst();
    QCOMPARE(args.at(1).toInt(), 1);
    const auto data = args.at(0).value<ItemDetailData>();
    QVERIFY2(data.valid, "loadItemDetail must produce a valid payload");
    QCOMPARE(data.videoPath, scrapedVideo);
  }

  // Both roots populated: the explicit videoDirectory takes priority.
  QMetaObject::invokeMethod(
      qm,
      [qm, filePath, art = artworkDir.path(), vid = videoDir.path()]() {
        qm->loadItemDetail(2, QString(), filePath, art, vid, QString());
      },
      Qt::QueuedConnection);
  QVERIFY2(detailSpy.wait(10'000), "Timed out waiting for itemDetailLoaded (explicit root)");
  {
    const QList<QVariant> args = detailSpy.takeFirst();
    QCOMPARE(args.at(1).toInt(), 2);
    const auto data = args.at(0).value<ItemDetailData>();
    QVERIFY2(data.valid, "loadItemDetail must produce a valid payload");
    QCOMPARE(data.videoPath, explicitVideo);
  }
}

QTEST_MAIN(TestQueryManagerItemDetailVideo)

#include "test_querymanager_itemdetail_video.moc"
