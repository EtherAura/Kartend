// Tests for ArtworkWizardService::prepare — the off-thread prep behind
// LibraryToolsController::artworkWizardInteractive. prepare() opens its OWN
// connection to a db FILE path and only reads, so like the BulkEditService
// tests these run against a real on-disk SQLite database: a writer connection
// creates the items table and seeds rows, then prepare() is pointed at the same
// file and the returned Prepared (queue + directory snapshot + dbOk) is
// asserted. No DB mocking (project rule).
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include "artworkwizardservice.h"

namespace {

// Opens a writer connection to a real on-disk file and creates the minimal
// items table prepare() reads (collection_uuid / name / path / artwork_path —
// the same columns DatabaseManager::loadAllItemPathsForCollection selects).
auto openItemsDb(const QString &connName, const QString &path) -> QSqlDatabase {
  auto db = QSqlDatabase::addDatabase("QSQLITE", connName);
  db.setDatabaseName(path);
  if (!db.open()) {
    qWarning("Failed to open file db: %s", qPrintable(db.lastError().text()));
    return db;
  }
  QSqlQuery q(db);
  q.exec("CREATE TABLE items (id INTEGER PRIMARY KEY, collection_uuid TEXT NOT NULL, "
         "name TEXT NOT NULL, path TEXT NOT NULL, artwork_path TEXT)");
  return db;
}

auto closeAndRemove(QSqlDatabase &db, const QString &connName) -> void {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connName);
}

// artworkPath is bound as SQL NULL when null (QVariant()) so the NULL-vs-empty
// filtering is exercised faithfully.
auto insertItem(QSqlDatabase &db, const QString &uuid, const QString &name, const QString &path,
                const QVariant &artworkPath) -> void {
  QSqlQuery q(db);
  q.prepare("INSERT INTO items (collection_uuid, name, path, artwork_path) VALUES (?, ?, ?, ?)");
  q.addBindValue(uuid);
  q.addBindValue(name);
  q.addBindValue(path);
  q.addBindValue(artworkPath);
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

auto writeFile(const QString &path) -> void {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();
}

} // namespace

class TestArtworkWizardService : public QObject {
  Q_OBJECT

private slots:
  void queuesOnlyItemsWithoutArtwork_nameOrdered();
  void scopedToCollectionUuid();
  void snapshotsArtworkDirectoryFilesOnly();
  void allItemsHaveArtwork_leavesQueueAndDirectoryEmpty();
  void emptyDbPathReturnsNotOk();
  void reportsProgressPrologueAndEpilogue();
};

void TestArtworkWizardService::queuesOnlyItemsWithoutArtwork_nameOrdered() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dbPath = tmp.filePath("lib.db");
  {
    auto db = openItemsDb("aw_filter", dbPath);
    // Inserted out of name order to prove the query's ORDER BY drives output.
    insertItem(db, "uuid-1", "Charlie", "/media/charlie.mkv", QVariant());        // NULL -> queue
    insertItem(db, "uuid-1", "Bravo", "/media/bravo.mp4", "/art/bravo.png");      // has art -> skip
    insertItem(db, "uuid-1", "Alpha", "/media/alpha.mp4", QString(""));           // empty -> queue
    insertItem(db, "uuid-1", "Delta", "/media/delta.mp4", QStringLiteral("   ")); // blank -> queue
    closeAndRemove(db, "aw_filter");
  }

  const ArtworkWizardService::Prepared prepared =
      ArtworkWizardService::prepare(dbPath, "uuid-1", tmp.path());

  QVERIFY(prepared.dbOk);
  QCOMPARE(prepared.queue.size(), 3);
  // Name-ordered (Bravo excluded): Alpha, Charlie, Delta.
  QCOMPARE(prepared.queue.at(0).filePath, QStringLiteral("/media/alpha.mp4"));
  QCOMPARE(prepared.queue.at(0).itemName, QStringLiteral("alpha"));
  QCOMPARE(prepared.queue.at(1).filePath, QStringLiteral("/media/charlie.mkv"));
  QCOMPARE(prepared.queue.at(1).itemName, QStringLiteral("charlie"));
  QCOMPARE(prepared.queue.at(2).filePath, QStringLiteral("/media/delta.mp4"));
  QCOMPARE(prepared.queue.at(2).itemName, QStringLiteral("delta"));
}

void TestArtworkWizardService::scopedToCollectionUuid() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dbPath = tmp.filePath("lib.db");
  {
    auto db = openItemsDb("aw_scope", dbPath);
    insertItem(db, "uuid-1", "One", "/media/one.mp4", QVariant());
    insertItem(db, "uuid-2", "Two", "/media/two.mp4", QVariant());
    closeAndRemove(db, "aw_scope");
  }

  const ArtworkWizardService::Prepared prepared =
      ArtworkWizardService::prepare(dbPath, "uuid-2", tmp.path());

  QVERIFY(prepared.dbOk);
  QCOMPARE(prepared.queue.size(), 1);
  QCOMPARE(prepared.queue.at(0).filePath, QStringLiteral("/media/two.mp4"));
}

void TestArtworkWizardService::snapshotsArtworkDirectoryFilesOnly() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dbPath = tmp.filePath("lib.db");
  {
    auto db = openItemsDb("aw_dir", dbPath);
    insertItem(db, "uuid-1", "One", "/media/one.mp4", QVariant());
    closeAndRemove(db, "aw_dir");
  }
  QDir artDir(tmp.path());
  QVERIFY(artDir.mkpath("art"));
  const QString artworkDir = artDir.filePath("art");
  writeFile(artworkDir + "/a.png");
  writeFile(artworkDir + "/b.jpg");
  QVERIFY(QDir(artworkDir).mkpath("nested")); // a subdirectory must be excluded

  const ArtworkWizardService::Prepared prepared =
      ArtworkWizardService::prepare(dbPath, "uuid-1", artworkDir);

  QVERIFY(prepared.dbOk);
  QCOMPARE(prepared.queue.size(), 1);
  QCOMPARE(prepared.directoryFiles.size(), 2); // files only, not the subdir
  QVERIFY(prepared.directoryFiles.contains(QDir(artworkDir).absoluteFilePath("a.png")));
  QVERIFY(prepared.directoryFiles.contains(QDir(artworkDir).absoluteFilePath("b.jpg")));
}

void TestArtworkWizardService::allItemsHaveArtwork_leavesQueueAndDirectoryEmpty() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dbPath = tmp.filePath("lib.db");
  {
    auto db = openItemsDb("aw_none", dbPath);
    insertItem(db, "uuid-1", "One", "/media/one.mp4", "/art/one.png");
    insertItem(db, "uuid-1", "Two", "/media/two.mp4", "/art/two.png");
    closeAndRemove(db, "aw_none");
  }
  // A populated artwork dir that must NOT be scanned when the queue is empty.
  writeFile(tmp.filePath("stray.png"));

  const ArtworkWizardService::Prepared prepared =
      ArtworkWizardService::prepare(dbPath, "uuid-1", tmp.path());

  QVERIFY(prepared.dbOk);
  QVERIFY(prepared.queue.isEmpty());
  QVERIFY(prepared.directoryFiles.isEmpty()); // skipped the listing — nothing to rank
}

void TestArtworkWizardService::emptyDbPathReturnsNotOk() {
  const ArtworkWizardService::Prepared prepared =
      ArtworkWizardService::prepare(QString(), "uuid-1", "/tmp");
  QVERIFY(!prepared.dbOk);
  QVERIFY(prepared.queue.isEmpty());
  QVERIFY(prepared.directoryFiles.isEmpty());
}

void TestArtworkWizardService::reportsProgressPrologueAndEpilogue() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dbPath = tmp.filePath("lib.db");
  {
    auto db = openItemsDb("aw_prog", dbPath);
    for (int i = 0; i < 5; ++i) {
      insertItem(db, "uuid-1", QStringLiteral("i%1").arg(i),
                 QStringLiteral("/media/i%1.mp4").arg(i), QVariant());
    }
    closeAndRemove(db, "aw_prog");
  }

  QList<QPair<int, int>> calls;
  ArtworkWizardService::prepare(dbPath, "uuid-1", tmp.path(),
                                [&calls](int done, int total) { calls.append({done, total}); });

  QVERIFY(!calls.isEmpty());
  QCOMPARE(calls.first(), qMakePair(0, 5)); // prologue
  QCOMPARE(calls.last(), qMakePair(5, 5));  // epilogue: every row examined
}

QTEST_MAIN(TestArtworkWizardService)
#include "test_artworkwizardservice.moc"
