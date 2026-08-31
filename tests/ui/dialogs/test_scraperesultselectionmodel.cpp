// Selection/dedup tests for ScrapeResultSelectionModel, decoupled from
// ScrapeResultDialog (Kartend-hhv2u). The whole point of the decouple: the
// model is now constructed with standalone widgets via setView() + a test
// collection list via setContext(), so its tree-building, check-cascade, and
// per-item dedup logic is exercised WITHOUT instantiating the dialog.
//
// Most cases drive the synchronous selection/dedup logic with a null ctx (the
// per-collection item fetch through ctx->databaseManager() then no-ops) and
// seed the item cache via preCheckSingleItem. The final case
// (realDbFetchSeedsSelectionThenDedups) closes the loop against a real SQLite
// DatabaseManager: it pages seeded items through fetchItemsRange and asserts the
// fetch->cache->select-all->dedup path end to end. No DB mocking.

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "../../support/inspectordb.h"
#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h" // CollectionUtils::computeCollectionUuid
#include "databasemanager.h"
#include "scraperesultselectionmodel.h"
#include "sessionmanager.h"

namespace {
CollectionConfig makeColl(const QString &name, int parentIndex = -1) {
  CollectionConfig c;
  c.name = name;
  c.parentCollectionIndex = parentIndex;
  c.isSubcollection = parentIndex >= 0;
  return c;
}

// Locate the tree row that maps to a given collection index.
QTreeWidgetItem *rowForIndex(QTreeWidget *tree, ScrapeResultSelectionModel *model, int index) {
  QList<QTreeWidgetItem *> stack;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    stack.append(tree->topLevelItem(i));
  }
  while (!stack.isEmpty()) {
    QTreeWidgetItem *it = stack.takeLast();
    if (model->collectionIndexForRow(it) == index) {
      return it;
    }
    for (int i = 0; i < it->childCount(); ++i) {
      stack.append(it->child(i));
    }
  }
  return nullptr;
}
} // namespace

class TestScrapeResultSelectionModel : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();

  void populateCollectionTreeMirrorsHierarchy();
  void collectionIndexForRowMapsEachRow();
  void preCheckSingleItemSeedsSelectionAndRendersRow();
  void onItemCheckChangedDedupesRepeatChecks();
  void onItemCheckChangedRemovesOnUncheck();
  void setAllItemsCheckedTogglesEnabledRows();
  void uncheckingCollectionClearsItsSelection();
  void checkingOtherCollectionDoesNotHijackItemsPane();

  // Real-SQLite: a real DatabaseManager pages seeded items via fetchItemsRange.
  void realDbFetchSeedsSelectionThenDedups();

private:
  QTreeWidget *m_tree = nullptr;
  QListWidget *m_list = nullptr;
  QLabel *m_header = nullptr;
  ScrapeResultSelectionModel *m_model = nullptr;
  QList<CollectionConfig> m_collections;
};

void TestScrapeResultSelectionModel::initTestCase() {
  // Sandbox QStandardPaths so DatabaseManager's media.db lands in an isolated
  // per-test dir (needed only by realDbFetchSeedsSelectionThenDedups).
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scraperesultselectionmodel"));
}

void TestScrapeResultSelectionModel::init() {
  m_tree = new QTreeWidget;
  m_list = new QListWidget;
  m_header = new QLabel;
  m_model = new ScrapeResultSelectionModel;
  m_model->setView(m_tree, m_list, m_header);
  // Index 0 = root "A"; index 1 = "Child" parented to A; index 2 = root "C".
  m_collections = {makeColl(QStringLiteral("A")), makeColl(QStringLiteral("Child"), 0),
                   makeColl(QStringLiteral("C"))};
  m_model->setContext(&m_collections, nullptr); // null ctx: DB fetch no-ops
}

void TestScrapeResultSelectionModel::cleanup() {
  delete m_model;
  m_model = nullptr;
  delete m_tree;
  m_tree = nullptr;
  delete m_list;
  m_list = nullptr;
  delete m_header;
  m_header = nullptr;
}

void TestScrapeResultSelectionModel::populateCollectionTreeMirrorsHierarchy() {
  m_model->populateCollectionTree();
  // Two roots (A, C); A owns one child (Child).
  QCOMPARE(m_tree->topLevelItemCount(), 2);
  QTreeWidgetItem *a = rowForIndex(m_tree, m_model, 0);
  QVERIFY(a != nullptr);
  QCOMPARE(a->text(0), QStringLiteral("A"));
  QCOMPARE(a->childCount(), 1);
  QCOMPARE(a->child(0)->text(0), QStringLiteral("Child"));
  // Every row starts unchecked.
  QCOMPARE(a->checkState(0), Qt::Unchecked);
}

void TestScrapeResultSelectionModel::collectionIndexForRowMapsEachRow() {
  m_model->populateCollectionTree();
  QCOMPARE(m_model->collectionIndexForRow(rowForIndex(m_tree, m_model, 0)), 0);
  QCOMPARE(m_model->collectionIndexForRow(rowForIndex(m_tree, m_model, 1)), 1);
  QCOMPARE(m_model->collectionIndexForRow(rowForIndex(m_tree, m_model, 2)), 2);
}

void TestScrapeResultSelectionModel::preCheckSingleItemSeedsSelectionAndRendersRow() {
  m_model->populateCollectionTree();
  m_model->preCheckSingleItem(2, QStringLiteral("/m/c1.mp4"));

  // Only collection 2's inclusion set is seeded, with exactly the one path.
  QCOMPARE(m_model->itemSelectionByCollection().value(2), QStringList{QStringLiteral("/m/c1.mp4")});
  QCOMPARE(m_model->totalCheckedItemCount(), 1);
  // Its tree row is checked.
  QCOMPARE(rowForIndex(m_tree, m_model, 2)->checkState(0), Qt::Checked);
  // The items list renders that single row, checked, carrying the path.
  QCOMPARE(m_list->count(), 1);
  QCOMPARE(m_list->item(0)->data(Qt::UserRole).toString(), QStringLiteral("/m/c1.mp4"));
  QCOMPARE(m_list->item(0)->checkState(), Qt::Checked);
}

void TestScrapeResultSelectionModel::onItemCheckChangedDedupesRepeatChecks() {
  m_model->populateCollectionTree();
  m_model->preCheckSingleItem(2, QStringLiteral("/m/c1.mp4"));
  QListWidgetItem *row = m_list->item(0);
  QVERIFY(row != nullptr);

  // Re-asserting "checked" must not duplicate the path in the inclusion set.
  row->setCheckState(Qt::Checked);
  m_model->onItemCheckChanged(row);
  m_model->onItemCheckChanged(row);
  QCOMPARE(m_model->itemSelectionByCollection().value(2).size(), 1);
  QCOMPARE(m_model->itemSelectionByCollection().value(2), QStringList{QStringLiteral("/m/c1.mp4")});
}

void TestScrapeResultSelectionModel::onItemCheckChangedRemovesOnUncheck() {
  m_model->populateCollectionTree();
  m_model->preCheckSingleItem(2, QStringLiteral("/m/c1.mp4"));
  QListWidgetItem *row = m_list->item(0);
  QVERIFY(row != nullptr);

  row->setCheckState(Qt::Unchecked);
  m_model->onItemCheckChanged(row);
  QVERIFY(m_model->itemSelectionByCollection().value(2).isEmpty());
  QCOMPARE(m_model->totalCheckedItemCount(), 0);
}

void TestScrapeResultSelectionModel::setAllItemsCheckedTogglesEnabledRows() {
  m_model->populateCollectionTree();
  m_model->preCheckSingleItem(2, QStringLiteral("/m/c1.mp4"));

  m_model->setAllItemsChecked(false);
  QVERIFY(m_model->itemSelectionByCollection().value(2).isEmpty());

  m_model->setAllItemsChecked(true);
  QCOMPARE(m_model->itemSelectionByCollection().value(2), QStringList{QStringLiteral("/m/c1.mp4")});
}

void TestScrapeResultSelectionModel::uncheckingCollectionClearsItsSelection() {
  m_model->populateCollectionTree();
  QTreeWidgetItem *c = rowForIndex(m_tree, m_model, 2);
  QVERIFY(c != nullptr);

  // Check the collection: an inclusion entry is created (empty = "include all"
  // until items land).
  c->setCheckState(0, Qt::Checked);
  m_model->onCollectionCheckChanged(c, 0);
  QVERIFY(m_model->itemSelectionByCollection().contains(2));

  // Uncheck: the inclusion entry is dropped entirely.
  c->setCheckState(0, Qt::Unchecked);
  m_model->onCollectionCheckChanged(c, 0);
  QVERIFY(!m_model->itemSelectionByCollection().contains(2));
}

void TestScrapeResultSelectionModel::checkingOtherCollectionDoesNotHijackItemsPane() {
  // Regression: with collection C on screen, CHECKING collection A's box
  // used to rebuild the items pane for A — repointing the header and
  // replacing the list with the "Loading items…" placeholder — while the
  // tree's current row stayed on C. The async completion only re-renders
  // for the current row, so the placeholder never resolved and the pane
  // was stuck for the rest of the session. Checking must seed A's
  // inclusion state and prefetch, but leave the pane on C.
  m_model->populateCollectionTree();
  m_model->preCheckSingleItem(2, QStringLiteral("/m/c1.mp4"));
  QCOMPARE(m_list->count(), 1); // pane shows collection C's row

  QTreeWidgetItem *a = rowForIndex(m_tree, m_model, 0);
  QVERIFY(a != nullptr);
  a->setCheckState(0, Qt::Checked);
  m_model->onCollectionCheckChanged(a, 0);

  // Pane and header still belong to C — no placeholder, no header swap.
  QCOMPARE(m_list->count(), 1);
  QCOMPARE(m_list->item(0)->data(Qt::UserRole).toString(), QStringLiteral("/m/c1.mp4"));
  QVERIFY(m_list->item(0)->flags() & Qt::ItemIsEnabled);
  QVERIFY(m_header->text().contains(QStringLiteral("'C'")));
  // A (and its cascade-checked child) still got their inclusion entries —
  // the "include all once items land" sentinel.
  QVERIFY(m_model->itemSelectionByCollection().contains(0));
  QVERIFY(m_model->itemSelectionByCollection().contains(1));
}

void TestScrapeResultSelectionModel::realDbFetchSeedsSelectionThenDedups() {
  // End-to-end against a real SQLite DatabaseManager: checking a collection
  // drives the async fetchItemsRange, whose itemsRangeLoaded the model caches
  // and (collection checked) turns into the inclusion set. Then dedup on the
  // real rendered rows. No DB mocking — items are seeded into the real media.db.
  SessionManager session;
  ApplicationContext appCtx;
  appCtx.managers.sessionManager = &session;
  DatabaseManager db(&appCtx);
  db.initDatabase();
  appCtx.managers.databaseManager = &db;

  QTemporaryDir mediaDir;
  QVERIFY(mediaDir.isValid());
  CollectionConfig coll;
  coll.name = QStringLiteral("RealColl");
  coll.mediaDirectory = mediaDir.path();
  coll.extensions = {QStringLiteral("mp4")};
  const QString uuid = CollectionUtils::computeCollectionUuid(coll.name, coll.mediaDirectory);

  QStringList paths;
  for (const QString &f :
       {QStringLiteral("a.mp4"), QStringLiteral("b.mp4"), QStringLiteral("c.mp4")}) {
    const QString p = QDir(mediaDir.path()).absoluteFilePath(f);
    QFile fh(p); // real file too, so a re-scan (if any) also finds them
    QVERIFY(fh.open(QIODevice::WriteOnly));
    fh.close();
    paths << p;
  }

  // Seed the collection (last_scanned set => fetchItemsRange pages the rows
  // rather than re-scanning) + its items, directly into the real media.db.
  {
    const QString dbPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                               .absoluteFilePath(QStringLiteral("media.db"));
    KartendTest::InspectorDb inspector(dbPath, QStringLiteral("test_srsm_seed"));
    QVERIFY2(inspector.isOpen(), "failed to open media.db inspector");
    QSqlDatabase idb = inspector.db();
    QSqlQuery col(idb);
    col.prepare(
        QStringLiteral("INSERT INTO collections (name, uuid, last_scanned) VALUES (?, ?, ?)"));
    col.addBindValue(coll.name);
    col.addBindValue(uuid);
    col.addBindValue(QStringLiteral("2026-01-01T00:00:00Z"));
    QVERIFY2(col.exec(), qPrintable(col.lastError().text()));
    const qint64 collectionId = col.lastInsertId().toLongLong();
    QSqlQuery ins(idb);
    QVERIFY(ins.prepare(QStringLiteral(
        "INSERT INTO items (collection_id, collection_uuid, path, rel_path, name, last_modified) "
        "VALUES (?, ?, ?, ?, ?, ?)")));
    for (const QString &p : paths) {
      ins.addBindValue(collectionId);
      ins.addBindValue(uuid);
      ins.addBindValue(p);
      ins.addBindValue(QFileInfo(p).fileName());
      ins.addBindValue(QFileInfo(p).completeBaseName());
      ins.addBindValue(QStringLiteral("2026-01-01T00:00:00Z"));
      QVERIFY2(ins.exec(), qPrintable(ins.lastError().text()));
    }
  }

  QTreeWidget tree;
  QListWidget list;
  QLabel header;
  ScrapeResultSelectionModel model;
  model.setView(&tree, &list, &header);
  QList<CollectionConfig> collections{coll};
  model.setContext(&collections, &appCtx);
  model.populateCollectionTree();

  QTreeWidgetItem *row = rowForIndex(&tree, &model, 0);
  QVERIFY(row != nullptr);
  tree.setCurrentItem(row); // so the async re-render targets this collection
  row->setCheckState(0, Qt::Checked);
  model.onCollectionCheckChanged(row, 0);

  // Pump the loop until the async fetch lands and the inclusion set fills.
  QElapsedTimer timer;
  timer.start();
  while (model.itemSelectionByCollection().value(0).size() < paths.size() &&
         timer.elapsed() < 10'000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }

  // Checking the collection selected every (real, DB-fetched) item, no dups.
  QStringList selected = model.itemSelectionByCollection().value(0);
  selected.sort();
  QStringList expected = paths;
  expected.sort();
  QCOMPARE(selected, expected);
  QCOMPARE(model.totalCheckedItemCount(), static_cast<int>(paths.size()));
  QCOMPARE(list.count(), static_cast<int>(paths.size()));

  // Rows render the DB display name (what the grid shows), not the raw
  // filename: the seeded name column holds the completeBaseName, so "a",
  // never "a.mp4".
  QStringList labels;
  for (int i = 0; i < list.count(); ++i) {
    labels << list.item(i)->text();
  }
  labels.sort();
  QCOMPARE(labels, QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));

  // Per-item dedup on a real row: uncheck removes it; re-check twice keeps one.
  QListWidgetItem *itemRow = list.item(0);
  QVERIFY(itemRow != nullptr);
  const QString p0 = itemRow->data(Qt::UserRole).toString();
  itemRow->setCheckState(Qt::Unchecked);
  model.onItemCheckChanged(itemRow);
  QVERIFY(!model.itemSelectionByCollection().value(0).contains(p0));
  itemRow->setCheckState(Qt::Checked);
  model.onItemCheckChanged(itemRow);
  model.onItemCheckChanged(itemRow);
  QCOMPARE(model.itemSelectionByCollection().value(0).count(p0), 1);
}

QTEST_MAIN(TestScrapeResultSelectionModel)
#include "test_scraperesultselectionmodel.moc"
