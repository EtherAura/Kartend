// Smoke tests for searchutils types.
//
// These types are tiny POD-like structs/enums; this test exists so that
// accidental layout changes (e.g. removing a field, reordering enum) get
// flagged and to lock in the documented default values.

#include <QTest>

#include "searchutils.h"

class TestSearchUtils : public QObject {
  Q_OBJECT
private slots:
  void searchModeEnumValuesDistinct();
  void searchContextDefaultsAreSafe();
  void searchContextFieldsAreIndependent();
};

void TestSearchUtils::searchModeEnumValuesDistinct() {
  // Must remain three distinct ordered modes — many cycle-helpers depend on
  // the ordering Current -> Current+Sub -> All.
  QVERIFY(SearchMode::CurrentCollection != SearchMode::CurrentAndSubcollections);
  QVERIFY(SearchMode::CurrentAndSubcollections != SearchMode::AllCollections);
  QVERIFY(SearchMode::CurrentCollection != SearchMode::AllCollections);

  QCOMPARE(static_cast<int>(SearchMode::CurrentCollection), 0);
  QCOMPARE(static_cast<int>(SearchMode::CurrentAndSubcollections), 1);
  QCOMPARE(static_cast<int>(SearchMode::AllCollections), 2);
}

void TestSearchUtils::searchContextDefaultsAreSafe() {
  // All flags must default to false so a freshly-constructed context never
  // accidentally widens search scope or claims sub-items exist.
  SearchContext ctx;
  QVERIFY(!ctx.hasSubs);
  QVERIFY(!ctx.realDirectItems);
  QVERIFY(!ctx.allowAll);
  QVERIFY(!ctx.isContainer);
}

void TestSearchUtils::searchContextFieldsAreIndependent() {
  // Setting one flag must not perturb the others — guards against any
  // future refactor that bit-packs into a single int without preserving
  // independent assignment semantics.
  SearchContext ctx;
  ctx.hasSubs = true;
  QVERIFY(ctx.hasSubs);
  QVERIFY(!ctx.realDirectItems);
  QVERIFY(!ctx.allowAll);
  QVERIFY(!ctx.isContainer);

  ctx.allowAll = true;
  QVERIFY(ctx.hasSubs);
  QVERIFY(ctx.allowAll);
  QVERIFY(!ctx.realDirectItems);
  QVERIFY(!ctx.isContainer);

  ctx.isContainer = true;
  ctx.realDirectItems = true;
  QVERIFY(ctx.hasSubs);
  QVERIFY(ctx.allowAll);
  QVERIFY(ctx.isContainer);
  QVERIFY(ctx.realDirectItems);
}

QTEST_MAIN(TestSearchUtils)
#include "test_searchutils.moc"
