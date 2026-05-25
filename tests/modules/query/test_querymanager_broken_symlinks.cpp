// Regression test for Kartend-9mxh: symlinks whose targets are temporarily
// unreachable (e.g. a btrfs mount that wasn't ready when Kartend launched)
// must remain visible to the scanner. Without QDir::System on the scan
// QDirIterator, Qt classifies broken symlinks as Unknown and drops them, and
// deleteMissingItemsByUuidUsingScannedItems then prunes the rows.

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <utility>

#include "collectionutils.h"
#include "querymanager.h"
#include "sessionmanager.h"
#include "workersignalspy.h"

class TestQueryManagerBrokenSymlinks : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void brokenSymlinkSurvivesScan();
};

template <typename Func> class ScopeExit {
public:
  explicit ScopeExit(Func &&func) : m_func(std::forward<Func>(func)) {}
  ~ScopeExit() { m_func(); }

  ScopeExit(const ScopeExit &) = delete;
  ScopeExit &operator=(const ScopeExit &) = delete;

private:
  Func m_func;
};

template <typename Func> auto makeScopeExit(Func &&func) -> ScopeExit<Func> {
  return ScopeExit<Func>(std::forward<Func>(func));
}

void TestQueryManagerBrokenSymlinks::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(QStringLiteral("kartend-test-broken-symlinks"));
}

static auto openInspectorDb(const QString &dbFilePath) -> QSqlDatabase {
  const QString connectionName = QStringLiteral("test_querymanager_broken_symlinks_inspect");
  if (QSqlDatabase::contains(connectionName)) {
    QSqlDatabase::removeDatabase(connectionName);
  }
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
  db.setDatabaseName(dbFilePath);
  if (!db.open()) {
    return {};
  }
  return db;
}

void TestQueryManagerBrokenSymlinks::brokenSymlinkSurvivesScan() {
#ifdef Q_OS_WIN
  // QFile::link() on Windows creates a real NTFS reparse-point symbolic
  // link, but QFileInfo::isSymLink() doesn't consistently classify every
  // reparse-point class across Qt versions on Windows. The runtime
  // scanner (QueryManager) sees broken paths through QDir / QDirIterator
  // and handles them the same way regardless of POSIX-vs-NTFS link
  // mechanics, so this btrfs-mount-not-ready regression is genuinely
  // POSIX-shaped. QSKIP rather than try to recreate the broken-link
  // shape via NTFS reparse points.
  QSKIP(
      "QFileInfo::isSymLink semantics on NTFS reparse points are inconsistent across Qt versions");
#else
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");

  const QDir dir(mediaDir.path());

  // Real file: control case, must be detected.
  {
    QFile real(dir.filePath(QStringLiteral("real.bin")));
    QVERIFY(real.open(QIODevice::WriteOnly));
    real.write("hello");
    real.close();
  }

  // Broken symlink: target deliberately does not exist. Simulates the btrfs
  // mount not being ready at scan time.
  const QString brokenLinkPath = dir.filePath(QStringLiteral("broken.bin"));
  const QString missingTarget = dir.filePath(QStringLiteral("not-yet-mounted/elsewhere.bin"));
  if (!QFile::link(missingTarget, brokenLinkPath)) {
    QSKIP("Filesystem does not support symlinks");
  }
  QVERIFY(QFileInfo(brokenLinkPath).isSymLink());
  QVERIFY(!QFileInfo(brokenLinkPath).exists()); // broken: target absent

  CollectionConfig collection;
  collection.name = QStringLiteral("BrokenSymlinkCollection");
  collection.mediaDirectory = mediaDir.path();
  collection.folderBrowsing.includeContentSubfolders = false;
  collection.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections;
  allCollections.append(collection);

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = collection;

  SessionManager sessionManager;
  auto *qm = new QueryManager(&sessionManager);
  QThread worker;
  qm->moveToThread(&worker);
  worker.start();

  const auto workerCleanup = makeScopeExit([&]() {
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

  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbFilePath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  QSqlDatabase inspectDb = openInspectorDb(dbFilePath);
  QVERIFY2(inspectDb.isValid() && inspectDb.isOpen(), "Failed to open inspector database");

  // Start from a clean slate so prior test runs don't contaminate the count.
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));
  }

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // WorkerSignalSpy (not QSignalSpy): qm lives on the worker thread, and
  // Qt 6.4's QSignalSpy connects DirectConnection with no locking, so it
  // would mutate its list on the worker thread while this thread reads it.
  WorkerSignalSpy scanCompletedSpy(qm, &QueryManager::collectionScanCompleted);
  QVERIFY(scanCompletedSpy.isValid());

  QMetaObject::invokeMethod(
      qm, [qm, ctx, allCollections]() { qm->ensureScannedForContext(ctx, allCollections); },
      Qt::QueuedConnection);

  QVERIFY2(scanCompletedSpy.wait(10'000), "Timed out waiting for collectionScanCompleted");

  QSqlQuery q(inspectDb);
  q.prepare(QStringLiteral(
      "SELECT path, rel_path FROM items WHERE collection_uuid = ? ORDER BY rel_path"));
  q.addBindValue(uuid);
  QVERIFY(q.exec());

  // Since the v13 path-convention change, items.path holds the ABSOLUTE path
  // and items.rel_path holds the media-dir-relative form.
  QStringList persistedAbsPaths;
  QStringList persistedRelPaths;
  while (q.next()) {
    persistedAbsPaths.append(q.value(0).toString());
    persistedRelPaths.append(q.value(1).toString());
  }
  QCOMPARE(persistedRelPaths,
           (QStringList{QStringLiteral("broken.bin"), QStringLiteral("real.bin")}));

  const QDir media(mediaDir.path());
  QCOMPARE(persistedAbsPaths, (QStringList{media.absoluteFilePath(QStringLiteral("broken.bin")),
                                           media.absoluteFilePath(QStringLiteral("real.bin"))}));
  for (const QString &absPath : persistedAbsPaths) {
    QVERIFY2(QDir::isAbsolutePath(absPath), qPrintable("items.path must be absolute: " + absPath));
  }
#endif
}

QTEST_MAIN(TestQueryManagerBrokenSymlinks)

#include "test_querymanager_broken_symlinks.moc"
