/**
 * @file test_scrapejobgrouping.cpp
 * @brief Unit tests for ScrapeJobGrouping::byOwningCollection.
 *
 * Covers the shell-parent scrape routing fix (Kartend-7dng): items
 * checked under a parent collection that displays its subcollections'
 * content must be regrouped onto the subcollection that owns each one,
 * so scraped artwork + metadata land on the right collection.
 */

#include "scrapejobgrouping.h"

#include <QTest>

using Groups = QList<QPair<int, QStringList>>;

class TestScrapeJobGrouping : public QObject {
  Q_OBJECT

private slots:
  void plainCollectionPassesThrough();
  void shellParentRoutesToSubcollections();
  void parentAndSubcollectionChecked_dedupes();
  void unmappedItemFallsBackToListingCollection();
  void outOfRangeOwnerFallsBack();
  void invalidCheckedIndexSkipped();
  void ownersKeepFirstSeenOrder();
  void itemOrderPreservedWithinOwner();
  void emptyInputs();
  void emptySelectionProducesNoJob();
  void duplicateCheckedIndexDoesNotDuplicateItems();
};

void TestScrapeJobGrouping::plainCollectionPassesThrough() {
  // A non-shell collection has no owner map; every item stays with it.
  QHash<int, QStringList> selection;
  selection[2] = {"/a", "/b"};
  const Groups g = ScrapeJobGrouping::byOwningCollection({2}, selection, {}, 5);
  QCOMPARE(g.size(), 1);
  QCOMPARE(g[0].first, 2);
  QCOMPARE(g[0].second, QStringList({"/a", "/b"}));
}

void TestScrapeJobGrouping::shellParentRoutesToSubcollections() {
  // The bug: parent collection 0 displays items owned by subcollections
  // 1 and 2. Checking the parent must produce one job per owner, not a
  // single job dumping everything on collection 0.
  QHash<int, QStringList> selection;
  selection[0] = {"/a", "/b", "/c"};
  QHash<int, QHash<QString, int>> owners;
  owners[0] = {{"/a", 1}, {"/b", 1}, {"/c", 2}};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0}, selection, owners, 3);
  QCOMPARE(g.size(), 2);
  QCOMPARE(g[0].first, 1);
  QCOMPARE(g[0].second, QStringList({"/a", "/b"}));
  QCOMPARE(g[1].first, 2);
  QCOMPARE(g[1].second, QStringList({"/c"}));
}

void TestScrapeJobGrouping::parentAndSubcollectionChecked_dedupes() {
  // Both the parent row (0) and the subcollection row (1) are checked
  // and both list "/a" + "/b"; each item must land on owner 1 once.
  QHash<int, QStringList> selection;
  selection[0] = {"/a", "/b", "/c"};
  selection[1] = {"/a", "/b"};
  QHash<int, QHash<QString, int>> owners;
  owners[0] = {{"/a", 1}, {"/b", 1}, {"/c", 2}};
  owners[1] = {{"/a", 1}, {"/b", 1}};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0, 1}, selection, owners, 3);
  QCOMPARE(g.size(), 2);
  QCOMPARE(g[0].first, 1);
  QCOMPARE(g[0].second, QStringList({"/a", "/b"}));
  QCOMPARE(g[1].first, 2);
  QCOMPARE(g[1].second, QStringList({"/c"}));
}

void TestScrapeJobGrouping::unmappedItemFallsBackToListingCollection() {
  // "/b" has no owner entry — it stays with the collection it was
  // listed under (0).
  QHash<int, QStringList> selection;
  selection[0] = {"/a", "/b"};
  QHash<int, QHash<QString, int>> owners;
  owners[0] = {{"/a", 1}};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0}, selection, owners, 3);
  QCOMPARE(g.size(), 2);
  QCOMPARE(g[0].first, 1);
  QCOMPARE(g[0].second, QStringList({"/a"}));
  QCOMPARE(g[1].first, 0);
  QCOMPARE(g[1].second, QStringList({"/b"}));
}

void TestScrapeJobGrouping::outOfRangeOwnerFallsBack() {
  // An owner index past the end of the collection list is treated as
  // unmapped — the item falls back to its listing collection.
  QHash<int, QStringList> selection;
  selection[0] = {"/a"};
  QHash<int, QHash<QString, int>> owners;
  owners[0] = {{"/a", 99}};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0}, selection, owners, 3);
  QCOMPARE(g.size(), 1);
  QCOMPARE(g[0].first, 0);
  QCOMPARE(g[0].second, QStringList({"/a"}));
}

void TestScrapeJobGrouping::invalidCheckedIndexSkipped() {
  // A -1 checked entry (a tree row with no mapped collection) is
  // ignored rather than crashing or producing a bogus job.
  QHash<int, QStringList> selection;
  selection[1] = {"/a"};
  const Groups g = ScrapeJobGrouping::byOwningCollection({-1, 1}, selection, {}, 3);
  QCOMPARE(g.size(), 1);
  QCOMPARE(g[0].first, 1);
}

void TestScrapeJobGrouping::ownersKeepFirstSeenOrder() {
  // Owner 2 owns the first item, owner 1 the second — owner 2's job
  // comes first so the progress sequence follows display order.
  QHash<int, QStringList> selection;
  selection[0] = {"/c", "/a"};
  QHash<int, QHash<QString, int>> owners;
  owners[0] = {{"/c", 2}, {"/a", 1}};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0}, selection, owners, 3);
  QCOMPARE(g.size(), 2);
  QCOMPARE(g[0].first, 2);
  QCOMPARE(g[1].first, 1);
}

void TestScrapeJobGrouping::itemOrderPreservedWithinOwner() {
  // Items routed to the same owner keep their original listing order.
  QHash<int, QStringList> selection;
  selection[0] = {"/x", "/y", "/z"};
  QHash<int, QHash<QString, int>> owners;
  owners[0] = {{"/x", 1}, {"/y", 1}, {"/z", 1}};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0}, selection, owners, 2);
  QCOMPARE(g.size(), 1);
  QCOMPARE(g[0].second, QStringList({"/x", "/y", "/z"}));
}

void TestScrapeJobGrouping::emptyInputs() {
  const Groups g = ScrapeJobGrouping::byOwningCollection({}, {}, {}, 0);
  QVERIFY(g.isEmpty());
}

void TestScrapeJobGrouping::emptySelectionProducesNoJob() {
  // A checked collection with an empty item selection contributes
  // nothing.
  QHash<int, QStringList> selection;
  selection[0] = {};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0}, selection, {}, 3);
  QVERIFY(g.isEmpty());
}

void TestScrapeJobGrouping::duplicateCheckedIndexDoesNotDuplicateItems() {
  // The same collection index appearing twice in the checked list must
  // not double its items.
  QHash<int, QStringList> selection;
  selection[0] = {"/a", "/b"};
  const Groups g = ScrapeJobGrouping::byOwningCollection({0, 0}, selection, {}, 3);
  QCOMPARE(g.size(), 1);
  QCOMPARE(g[0].first, 0);
  QCOMPARE(g[0].second, QStringList({"/a", "/b"}));
}

QTEST_APPLESS_MAIN(TestScrapeJobGrouping)
#include "test_scrapejobgrouping.moc"
