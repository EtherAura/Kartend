// Tests for BulkEditService::run — the off-thread bulk-edit pipeline behind
// MainWindow::bulkEditInteractive. run() opens its OWN connection to a db FILE
// path and commits every row write inside one transaction, so like the
// StatisticsService tests these run against a real on-disk SQLite database: a
// writer connection applies the migration chain and seeds item_metadata rows,
// then run() is pointed at the same file and the returned Outcome plus the
// persisted rows are asserted. No DB mocking (project rule).
#include <atomic>
#include <memory>

#include <QList>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "bulkedit.h"
#include "bulkeditservice.h"
#include "dbmigrations.h"
#include "itemmetadata.h"

namespace {

// Opens a writer connection to a real on-disk file and applies the full
// migration chain so the schema matches what run()'s private connection will
// later see on the same file.
auto openFileDb(const QString &connName, const QString &path) -> QSqlDatabase {
  auto db = QSqlDatabase::addDatabase("QSQLITE", connName);
  db.setDatabaseName(path);
  if (!db.open()) {
    qWarning("Failed to open file db: %s", qPrintable(db.lastError().text()));
    return db;
  }
  QSqlQuery q(db);
  q.exec("PRAGMA journal_mode = WAL");
  q.exec("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT NOT NULL)");
  q.exec("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
         "path TEXT NOT NULL, last_modified TEXT)");
  DbMigrations::applySchemaMigrations(db, "test");
  return db;
}

auto closeAndRemove(QSqlDatabase &db, const QString &connName) -> void {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connName);
}

auto seedMetadata(QSqlDatabase &db, const QString &uuid, const QString &path, const QString &tags,
                  const QString &title = QString()) -> void {
  ItemMetadataStore::ItemMetadata md;
  md.collectionUuid = uuid;
  md.path = path;
  md.title = title;
  md.tags = tags;
  QVERIFY(ItemMetadataStore::save(db, md).isOk());
}

auto metadataRowCount(QSqlDatabase &db) -> int {
  QSqlQuery q(db);
  if (!q.exec("SELECT COUNT(*) FROM item_metadata") || !q.next()) {
    return -1;
  }
  return q.value(0).toInt();
}

auto noCancel() -> std::shared_ptr<std::atomic_bool> {
  return std::make_shared<std::atomic_bool>(false);
}

} // namespace

class TestBulkEditService : public QObject {
  Q_OBJECT
private slots:
  void run_emptyDbPath_returnsNotOk();
  void run_addTag_writesChangedRowsInOneCommit();
  void run_noRowChanged_commitsNothingButSucceeds();
  void run_preCanceled_writesNothing();
  void run_cancelMidWrite_rollsBackEverything();
};

void TestBulkEditService::run_emptyDbPath_returnsNotOk() {
  // An empty path short-circuits before touching SQLite; the caller relies on
  // ok == false to surface the "could not be applied" box.
  const auto outcome = BulkEditService::run(QString(), "u1", BulkEdit::Action::AddTag, "live",
                                            {QStringLiteral("/a")}, noCancel());
  QVERIFY(!outcome.ok);
  QVERIFY(!outcome.canceled);
  QCOMPARE(outcome.written, 0);
  QVERIFY(outcome.writtenPaths.isEmpty());
}

void TestBulkEditService::run_addTag_writesChangedRowsInOneCommit() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("media.db");
  const QString conn = "bulkedit_addtag_writer";
  auto db = openFileDb(conn, path);
  QVERIFY(db.isOpen());

  // /a already carries the tag (action is a no-op there), /b has a row
  // without it, /c has no item_metadata row at all — run() must create it
  // from a keyed stub, exactly like the per-item editor would.
  seedMetadata(db, "u1", "/a", ItemMetadataStore::serializeTags({QStringLiteral("live")}));
  seedMetadata(db, "u1", "/b", QString(), QStringLiteral("Concert B"));

  QList<QPair<int, int>> reports;
  const auto progress = [&reports](int done, int total) { reports.append({done, total}); };

  const auto outcome = BulkEditService::run(
      path, "u1", BulkEdit::Action::AddTag, "live",
      {QStringLiteral("/a"), QStringLiteral("/b"), QStringLiteral("/c")}, noCancel(), progress);

  QVERIFY(outcome.ok);
  QVERIFY(!outcome.canceled);
  QCOMPARE(outcome.written, 2);
  QCOMPARE(outcome.writtenPaths, QStringList({QStringLiteral("/b"), QStringLiteral("/c")}));

  // Every row now carries the tag; the created-from-stub row is stamped
  // source == "user" by applyAction so scraper integrations know the user
  // touched it.
  auto loaded = ItemMetadataStore::loadBatch(
      db, "u1", {QStringLiteral("/a"), QStringLiteral("/b"), QStringLiteral("/c")});
  QVERIFY(loaded.isOk());
  const auto &byPath = loaded.value();
  QCOMPARE(byPath.size(), 3);
  for (const QString &p : {QStringLiteral("/a"), QStringLiteral("/b"), QStringLiteral("/c")}) {
    QVERIFY(ItemMetadataStore::parseTags(byPath.value(p).tags).contains(QStringLiteral("live")));
  }
  QCOMPARE(byPath.value(QStringLiteral("/c")).source, QStringLiteral("user"));
  // /b kept its unrelated field through the read-modify-write.
  QCOMPARE(byPath.value(QStringLiteral("/b")).title, QStringLiteral("Concert B"));

  // Progress: 0/total prologue, total/total epilogue, done never decreasing
  // and total constant (3 rows is below the reporting stride, so no
  // intermediate reports are expected).
  QVERIFY(reports.size() >= 2);
  QCOMPARE(reports.first(), qMakePair(0, 3));
  QCOMPARE(reports.last(), qMakePair(3, 3));
  for (int i = 1; i < reports.size(); ++i) {
    QVERIFY(reports[i].first >= reports[i - 1].first);
    QCOMPARE(reports[i].second, 3);
  }

  closeAndRemove(db, conn);
}

void TestBulkEditService::run_noRowChanged_commitsNothingButSucceeds() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("media.db");
  const QString conn = "bulkedit_nochange_writer";
  auto db = openFileDb(conn, path);
  QVERIFY(db.isOpen());

  seedMetadata(db, "u1", "/a", ItemMetadataStore::serializeTags({QStringLiteral("live")}));

  // Adding an already-present tag changes nothing — the pipeline still
  // completes ok (the summary reads "Applied to 0 of N"), and no write lands.
  const auto outcome = BulkEditService::run(path, "u1", BulkEdit::Action::AddTag, "live",
                                            {QStringLiteral("/a")}, noCancel());
  QVERIFY(outcome.ok);
  QVERIFY(!outcome.canceled);
  QCOMPARE(outcome.written, 0);
  QVERIFY(outcome.writtenPaths.isEmpty());
  QCOMPARE(metadataRowCount(db), 1);

  closeAndRemove(db, conn);
}

void TestBulkEditService::run_preCanceled_writesNothing() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("media.db");
  const QString conn = "bulkedit_precancel_writer";
  auto db = openFileDb(conn, path);
  QVERIFY(db.isOpen());

  auto cancel = std::make_shared<std::atomic_bool>(true);
  const auto outcome = BulkEditService::run(path, "u1", BulkEdit::Action::MarkHidden, QString(),
                                            {QStringLiteral("/a"), QStringLiteral("/b")}, cancel);

  QVERIFY(!outcome.ok);
  QVERIFY(outcome.canceled);
  QCOMPARE(outcome.written, 0);
  QVERIFY(outcome.writtenPaths.isEmpty());
  QCOMPARE(metadataRowCount(db), 0);

  closeAndRemove(db, conn);
}

void TestBulkEditService::run_cancelMidWrite_rollsBackEverything() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("media.db");
  const QString conn = "bulkedit_midcancel_writer";
  auto db = openFileDb(conn, path);
  QVERIFY(db.isOpen());

  // 100 rows, every one changed by the action, no pre-existing metadata.
  QStringList paths;
  for (int i = 0; i < 100; ++i) {
    paths.append(QStringLiteral("/g/%1").arg(i));
  }

  // The progress hook runs synchronously on run()'s thread, so flipping the
  // token from inside it is race-free and deterministic: the first stride
  // report (done == 64) arrives after 64 rows were staged in the open
  // transaction, and the loop observes the flip on the next row.
  auto cancel = std::make_shared<std::atomic_bool>(false);
  const auto progress = [&cancel](int done, int /*total*/) {
    if (done >= 64) {
      cancel->store(true, std::memory_order_release);
    }
  };

  const auto outcome =
      BulkEditService::run(path, "u1", BulkEdit::Action::AddTag, "batch", paths, cancel, progress);

  // All-or-nothing: the 64 staged rows rolled back with the transaction.
  QVERIFY(!outcome.ok);
  QVERIFY(outcome.canceled);
  QCOMPARE(outcome.written, 0);
  QVERIFY(outcome.writtenPaths.isEmpty());
  QCOMPARE(metadataRowCount(db), 0);

  closeAndRemove(db, conn);
}

QTEST_MAIN(TestBulkEditService)
#include "test_bulkeditservice.moc"
