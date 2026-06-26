// Lifecycle coverage for PreSearchStateCache — the save-before-search /
// restore-after-clear handoff whose failure mode ("typed in search, cleared
// it, the grid is now wrong/blank") is a known bug class (Kartend audit T-06).
//
// The cache null-guards every ItemWidget*/scroll-area deref, so its
// save -> has -> restore -> discard lifecycle (plus the double-save and
// restore-without-save edges) is exercisable with null widget pointers and
// null references — no chrome ItemWidget / laid-out QScrollArea construction
// needed. (The actual widget reparenting/repositioning is integration-tier and
// covered through the scroll/search integration paths.)
#include <QHash>
#include <QPoint>
#include <QTest>

#include "presearchstatemanager.h"

namespace {
QHash<int, ItemWidget *> nullWidgets(int count) {
  QHash<int, ItemWidget *> widgets;
  for (int i = 0; i < count; ++i) {
    widgets.insert(i, nullptr);
  }
  return widgets;
}

QPoint zeroPosition(int) {
  return QPoint(0, 0);
}
} // namespace

class TestPreSearchStateCache : public QObject {
  Q_OBJECT
private slots:
  void saveRefusesEmptyActiveSet();
  void saveSucceedsAndHandsOverActiveSet();
  void doubleSaveIsRefused();
  void restoreWithoutSaveIsNoOp();
  void restoreHandsBackSavedSetAndEmptiesCache();
  void discardClearsSavedState();
};

void TestPreSearchStateCache::saveRefusesEmptyActiveSet() {
  PreSearchStateCache cache;
  cache.setReferences(nullptr, nullptr);
  QHash<int, ItemWidget *> active; // nothing on screen to preserve
  QVERIFY(!cache.saveState(active));
  QVERIFY(!cache.hasSavedState());
}

void TestPreSearchStateCache::saveSucceedsAndHandsOverActiveSet() {
  PreSearchStateCache cache;
  cache.setReferences(nullptr, nullptr);
  QHash<int, ItemWidget *> active = nullWidgets(3);
  QVERIFY(cache.saveState(active));
  QVERIFY(cache.hasSavedState());
  QVERIFY(active.isEmpty()); // ownership of the on-screen set moved into the cache
}

void TestPreSearchStateCache::doubleSaveIsRefused() {
  PreSearchStateCache cache;
  cache.setReferences(nullptr, nullptr);
  QHash<int, ItemWidget *> first = nullWidgets(2);
  QVERIFY(cache.saveState(first));
  // A second save while a set is already held must not clobber it (else the
  // pre-search grid would be lost when search re-fires before a restore).
  QHash<int, ItemWidget *> second = nullWidgets(2);
  QVERIFY(!cache.saveState(second));
  QVERIFY(!second.isEmpty()); // the rejected set is left untouched for the caller
  QVERIFY(cache.hasSavedState());
}

void TestPreSearchStateCache::restoreWithoutSaveIsNoOp() {
  PreSearchStateCache cache;
  cache.setReferences(nullptr, nullptr);
  QHash<int, ItemWidget *> active;
  QVERIFY(!cache.restoreState(active, nullptr, nullptr, nullptr, zeroPosition, 100, 100));
  QVERIFY(active.isEmpty());
}

void TestPreSearchStateCache::restoreHandsBackSavedSetAndEmptiesCache() {
  PreSearchStateCache cache;
  cache.setReferences(nullptr, nullptr);
  QHash<int, ItemWidget *> active = nullWidgets(3);
  QVERIFY(cache.saveState(active)); // active now empty, cache holds 3

  QVERIFY(cache.restoreState(active, nullptr, nullptr, nullptr, zeroPosition, 100, 100));
  QCOMPARE(active.size(), 3);      // the saved set is handed back to the caller
  QVERIFY(!cache.hasSavedState()); // and the cache is emptied so a fresh save can occur
}

void TestPreSearchStateCache::discardClearsSavedState() {
  PreSearchStateCache cache;
  cache.setReferences(nullptr, nullptr);
  QHash<int, ItemWidget *> active = nullWidgets(2);
  QVERIFY(cache.saveState(active));
  QVERIFY(cache.hasSavedState());
  cache.discardSavedState();
  QVERIFY(!cache.hasSavedState());
  QCOMPARE(cache.savedScrollPosition(), 0); // reset
}

QTEST_GUILESS_MAIN(TestPreSearchStateCache)
#include "test_presearchstatecache.moc"
