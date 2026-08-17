// Regression tests for Kartend-4te3: items.path now stores the ABSOLUTE
// filesystem path app-wide; items.rel_path holds the media-dir-relative form
// that the subfolder / virtual-folder queries need.
//
//   scanStoresAbsolutePathAndRelPath
//     A fresh scan of a flat file plus a nested-subdir file must persist an
//     absolute items.path and a media-dir-relative items.rel_path.
//
//   reconcileAbsolutizesExistingRowsPreservingStats
//     A pre-v13 items row (relative path, NULL rel_path, non-zero play_count
//     / date_added) must be rewritten in place by
//     QueryManager::maybeAbsolutizeItemPaths — exercised here via the call it
//     makes at the top of loadAllCollections — with path becoming absolute,
//     rel_path backfilled, and every other column preserved.

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "../../support/inspectordb.h"
#include "../../support/scopeexit.h"
#include "../../support/testsandbox.h"
#include "../../support/workersignalspy.h"
#include "collection/collectioncontext.h"
#include "collection/typehelpers.h"
#include "querymanager.h"
#include "querymanagerhelpers.h"
#include "sessionmanager.h"

class TestQueryManagerAbsPath : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void scanStoresAbsolutePathAndRelPath();
  void reconcileAbsolutizesExistingRowsPreservingStats();
  void cachedLoadUsesStoredPathWhenRelPathPointsElsewhere();
};

void TestQueryManagerAbsPath::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-abspath"));
}

void TestQueryManagerAbsPath::scanStoresAbsolutePathAndRelPath() {
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");

  const QDir dir(mediaDir.path());

  // Flat file at the collection root.
  {
    QFile flat(dir.filePath(QStringLiteral("flat.bin")));
    QVERIFY(flat.open(QIODevice::WriteOnly));
    flat.write("flat");
    flat.close();
  }

  // Nested file inside a subdirectory.
  QVERIFY(dir.mkpath(QStringLiteral("sub")));
  {
    QFile nested(dir.filePath(QStringLiteral("sub/nested.bin")));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write("nested");
    nested.close();
  }

  CollectionConfig collection;
  collection.name = QStringLiteral("AbsPathCollection");
  collection.mediaDirectory = mediaDir.path();
  collection.folderBrowsing.includeContentSubfolders = true; // so the nested file is scanned
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

  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbFilePath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  KartendTest::InspectorDb inspector(dbFilePath, QStringLiteral("test_qm_abspath_scan_inspect"));
  QVERIFY2(inspector.isOpen(), "Failed to open inspector database");
  QSqlDatabase inspectDb = inspector.db();

  // Clean slate so prior runs don't contaminate the assertions.
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

  QHash<QString, QString> relToAbs;
  while (q.next()) {
    const QString absPath = q.value(0).toString();
    const QString relPath = q.value(1).toString();
    QVERIFY2(QDir::isAbsolutePath(absPath), qPrintable("items.path must be absolute: " + absPath));
    QVERIFY2(!relPath.isEmpty(), "items.rel_path must be populated by the scanner");
    QVERIFY2(!QDir::isAbsolutePath(relPath),
             qPrintable("items.rel_path must be media-dir-relative: " + relPath));
    relToAbs.insert(relPath, absPath);
  }

  QCOMPARE(relToAbs.size(), 2);

  // Flat file: rel_path is the bare filename, path is mediaDir/flat.bin.
  QVERIFY(relToAbs.contains(QStringLiteral("flat.bin")));
  QCOMPARE(relToAbs.value(QStringLiteral("flat.bin")),
           dir.absoluteFilePath(QStringLiteral("flat.bin")));

  // Nested file: rel_path keeps the subdir component, path is fully absolute.
  QVERIFY(relToAbs.contains(QStringLiteral("sub/nested.bin")));
  QCOMPARE(relToAbs.value(QStringLiteral("sub/nested.bin")),
           dir.absoluteFilePath(QStringLiteral("sub/nested.bin")));
}

void TestQueryManagerAbsPath::reconcileAbsolutizesExistingRowsPreservingStats() {
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");

  const QDir dir(mediaDir.path());

  // The file must exist on disk so the post-reconcile scan keeps the row
  // (deleteMissingItemsByUuidUsingScannedItems would otherwise prune it).
  {
    QFile real(dir.filePath(QStringLiteral("legacy.bin")));
    QVERIFY(real.open(QIODevice::WriteOnly));
    real.write("legacy");
    real.close();
  }

  CollectionConfig collection;
  collection.name = QStringLiteral("ReconcileCollection");
  collection.mediaDirectory = mediaDir.path();
  collection.folderBrowsing.includeContentSubfolders = false;
  collection.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections;
  allCollections.append(collection);

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

  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbFilePath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  KartendTest::InspectorDb inspector(dbFilePath,
                                     QStringLiteral("test_qm_abspath_reconcile_inspect"));
  QVERIFY2(inspector.isOpen(), "Failed to open inspector database");
  QSqlDatabase inspectDb = inspector.db();

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Seed a pre-v13 row: relative path, NULL rel_path, non-zero usage stats and
  // date_added — exactly what an upgraded database carries before the
  // reconcile runs. Reset the meta gate to '0' so maybeAbsolutizeItemPaths
  // actually executes on the next load.
  constexpr qint64 kDateAdded = 1'700'000'000;
  constexpr qint64 kPlayCount = 7;
  {
    QSqlQuery q(inspectDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM items")));
    QVERIFY(q.exec(QStringLiteral("DELETE FROM collections")));

    // A collections row to satisfy the items.collection_id FK; its legacy id
    // is irrelevant to the path reconcile. last_scanned stays in the past so
    // the post-reconcile scan still re-stamps the row.
    QSqlQuery col(inspectDb);
    col.prepare(
        QStringLiteral("INSERT INTO collections (name, uuid, last_scanned) VALUES (?, ?, ?)"));
    col.addBindValue(collection.name);
    col.addBindValue(uuid);
    col.addBindValue(QStringLiteral("1970-01-01T00:00:00Z"));
    QVERIFY2(col.exec(), qPrintable(col.lastError().text()));
    const qint64 legacyId = col.lastInsertId().toLongLong();

    QSqlQuery ins(inspectDb);
    ins.prepare(QStringLiteral(
        "INSERT INTO items (collection_id, collection_uuid, path, rel_path, name, "
        "last_modified, play_count, date_added) VALUES (?, ?, ?, NULL, ?, ?, ?, ?)"));
    ins.addBindValue(legacyId);
    ins.addBindValue(uuid);
    ins.addBindValue(QStringLiteral("legacy.bin")); // RELATIVE path (pre-v13)
    ins.addBindValue(QStringLiteral("legacy"));
    ins.addBindValue(QStringLiteral("2020-01-01T00:00:00Z"));
    ins.addBindValue(kPlayCount);
    ins.addBindValue(kDateAdded);
    QVERIFY2(ins.exec(), qPrintable(ins.lastError().text()));

    QSqlQuery meta(inspectDb);
    QVERIFY(meta.exec(
        QStringLiteral("INSERT INTO meta(key, value) VALUES('items_paths_absolutized', '0') "
                       "ON CONFLICT(key) DO UPDATE SET value='0'")));
  }

  // loadAllCollections calls maybeAbsolutizeItemPaths before its scan loop.
  // WorkerSignalSpy (not QSignalSpy): see the threading note above.
  WorkerSignalSpy itemsLoadedSpy(qm, &QueryManager::itemsLoaded);
  QVERIFY(itemsLoadedSpy.isValid());

  QMetaObject::invokeMethod(
      qm, [qm, allCollections]() { qm->loadAllCollections(allCollections); }, Qt::QueuedConnection);

  QVERIFY2(itemsLoadedSpy.wait(10'000), "Timed out waiting for itemsLoaded");

  // The row must now hold an absolute path + the backfilled rel_path, with the
  // usage stats / date_added preserved untouched.
  QSqlQuery q(inspectDb);
  q.prepare(QStringLiteral(
      "SELECT path, rel_path, play_count, date_added FROM items WHERE collection_uuid = ?"));
  q.addBindValue(uuid);
  QVERIFY(q.exec());
  QVERIFY2(q.next(), "Reconciled row went missing");

  const QString absPath = q.value(0).toString();
  const QString relPath = q.value(1).toString();
  QCOMPARE(absPath, dir.absoluteFilePath(QStringLiteral("legacy.bin")));
  QVERIFY2(QDir::isAbsolutePath(absPath), "items.path must be absolute after reconcile");
  QCOMPARE(relPath, QStringLiteral("legacy.bin"));
  QCOMPARE(q.value(2).toLongLong(), kPlayCount);
  QCOMPARE(q.value(3).toLongLong(), kDateAdded);
  QVERIFY2(!q.next(), "Reconcile must not duplicate the row");

  // The meta gate must now be flipped so subsequent loads skip the reconcile.
  QSqlQuery flag(inspectDb);
  QVERIFY(
      flag.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'items_paths_absolutized'")));
  QVERIFY(flag.next());
  QCOMPARE(flag.value(0).toString(), QStringLiteral("1"));
}

void TestQueryManagerAbsPath::cachedLoadUsesStoredPathWhenRelPathPointsElsewhere() {
  // Kartend-yxahw, at the layer the bug lived in. Every consumer of the cached
  // load re-derives an item's absolute path by joining its media-dir-relative
  // key onto the collection's media directory. That holds for every ordinary
  // row and is DELIBERATELY false for the collapsed multi-disc item, whose
  // path is a generated playlist under <appDataDir>/multi-disc/<uuid>/ while
  // its rel_path places it beside its discs for folder browsing. The join
  // produced a file that does not exist, and everything downstream — launch,
  // details pane, artwork — got that nonexistent path.
  //
  // Driven through the pure helper rather than a live load: the alternative is
  // to craft the row in SQLite and then defeat every rescan heuristic to reach
  // the cached branch, which tests the heuristics more than the fix.
  QTemporaryDir mediaDir;
  QVERIFY(mediaDir.isValid());
  const QDir dir(mediaDir.path());

  CollectionConfig collection;
  collection.name = QStringLiteral("OverrideCollection");
  collection.mediaDirectory = mediaDir.path();

  // What the cached load hands over: media-dir-relative keys.
  const QStringList filePaths = {QStringLiteral("ordinary.bin"), QStringLiteral("Recital.m3u")};
  // ...plus the true path for the one row the join cannot reconstruct.
  const QString playlistPath = QStringLiteral("/somewhere/else/multi-disc/abc123/Recital.m3u");
  QHash<QString, QString> storedAbsByKey;
  storedAbsByKey.insert(QStringLiteral("Recital.m3u"), playlistPath);

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  QueryManagerInternal::appendFileMapsAndListCanonical(
      0, collection, collection.artworkDirectory, filePaths, allFilePaths, allFileNames,
      fileToArtworkDir, fileToMediaDir, fileToCollectionIndex, false, &storedAbsByKey);

  // The ordinary row still resolves by joining — unchanged behaviour.
  QVERIFY2(allFilePaths.contains(dir.absoluteFilePath(QStringLiteral("ordinary.bin"))),
           qPrintable(QStringLiteral("ordinary row must still join: %1")
                          .arg(allFilePaths.join(QStringLiteral(", ")))));

  // The collapsed row resolves to its real playlist, NOT to the rejoin.
  QVERIFY2(allFilePaths.contains(playlistPath),
           qPrintable(QStringLiteral("expected the stored path %1, got: %2")
                          .arg(playlistPath, allFilePaths.join(QStringLiteral(", ")))));
  QVERIFY2(!allFilePaths.contains(dir.absoluteFilePath(QStringLiteral("Recital.m3u"))),
           "rel_path was rejoined onto the media dir for the collapsed item");

  // And its display name comes from the key, so the tile still reads "Recital"
  // rather than anything derived from the playlist's location.
  QCOMPARE(allFileNames.value(playlistPath), QStringLiteral("Recital"));
}

QTEST_MAIN(TestQueryManagerAbsPath)

#include "test_querymanager_abspath.moc"
