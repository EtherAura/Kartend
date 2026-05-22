#include "test_searchmanager.h"

#include "applicationmanager.h"
#include "collectionutils.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"
#include "searchmanager.h"
#include "searchutils.h"
#include "timerutils.h"

#include <QLineEdit>
#include <QList>
#include <QMetaObject>
#include <QSignalSpy>
#include <QString>
#include <QStringLiteral>
#include <QTest>

namespace {

CollectionConfig makeRoot(const QString &name) {
  CollectionConfig cfg;
  cfg.name = name;
  cfg.parentCollectionIndex = -1;
  cfg.isSubcollection = false;
  // mediaDirectory left empty — hasDirectItemsForIndex returns false without
  // touching disk, which keeps allowAllFor's "other root with direct items"
  // lookup deterministic for these tests.
  return cfg;
}

CollectionConfig makeChild(const QString &name, int parentIndex) {
  CollectionConfig cfg;
  cfg.name = name;
  cfg.parentCollectionIndex = parentIndex;
  cfg.isSubcollection = true;
  return cfg;
}

} // namespace

void TestSearchManager::testInitialStateDefaults() {
  SearchManager mgr;
  // The default mode is the per-collection scope so opening the app on a
  // collection-with-items doesn't accidentally search the whole library.
  QCOMPARE(mgr.currentMode(), SearchMode::CurrentCollection);
  QVERIFY(!mgr.isSearchActive());
  QCOMPARE(mgr.currentSearchText(), QString());
  QCOMPARE(mgr.preSearchCollectionIndex(), -1);
  QCOMPARE(mgr.preSearchSelectedIndex(), -1);
  QCOMPARE(mgr.preSearchMode(), SearchMode::CurrentCollection);
}

void TestSearchManager::testSetCurrentModeRoundtrip() {
  SearchManager mgr;
  // Each enum value must round-trip through the inline setter. A regression
  // that silently mapped one of the three modes to a default would surface
  // here as a mismatched read.
  mgr.setCurrentMode(SearchMode::AllCollections);
  QCOMPARE(mgr.currentMode(), SearchMode::AllCollections);
  mgr.setCurrentMode(SearchMode::CurrentAndSubcollections);
  QCOMPARE(mgr.currentMode(), SearchMode::CurrentAndSubcollections);
  mgr.setCurrentMode(SearchMode::CurrentCollection);
  QCOMPARE(mgr.currentMode(), SearchMode::CurrentCollection);
}

void TestSearchManager::testSetSearchActiveRoundtrip() {
  SearchManager mgr;
  mgr.setSearchActive(true);
  QVERIFY(mgr.isSearchActive());
  mgr.setSearchActive(false);
  QVERIFY(!mgr.isSearchActive());
}

void TestSearchManager::testSetCurrentSearchTextRoundtrip() {
  SearchManager mgr;
  mgr.setCurrentSearchText(QStringLiteral("Tetris"));
  QCOMPARE(mgr.currentSearchText(), QStringLiteral("Tetris"));
  // Empty is the canonical cleared state — round-trip that too so a setter
  // that ignored empty strings (a tempting micro-optimization) is caught.
  mgr.setCurrentSearchText(QString());
  QCOMPARE(mgr.currentSearchText(), QString());
}

void TestSearchManager::testPreSearchAccessorsRoundtrip() {
  SearchManager mgr;
  // Pre-search state is the three-tuple SearchManager snapshots when the
  // user starts typing, so cancelling search can return them to the prior
  // collection / mode / selected row. Each accessor is independent.
  mgr.setPreSearchCollectionIndex(3);
  mgr.setPreSearchMode(SearchMode::AllCollections);
  mgr.setPreSearchSelectedIndex(7);
  QCOMPARE(mgr.preSearchCollectionIndex(), 3);
  QCOMPARE(mgr.preSearchMode(), SearchMode::AllCollections);
  QCOMPARE(mgr.preSearchSelectedIndex(), 7);
}

void TestSearchManager::testDebounceTimerExists() {
  SearchManager mgr;
  // Ctor wires the debounce timer up before any setupReferences() call so
  // InteractionManager can grab it during signal connection setup. A null
  // here would surface as a crash inside connectSearchManagerSignals.
  QVERIFY(mgr.debounceTimer() != nullptr);
}

void TestSearchManager::testItemsLoadedConnectionAccessorIsWritable() {
  SearchManager mgr;
  // itemsLoadedConnection() returns a mutable reference because the
  // performDebouncedSearch path installs a one-shot connection per query
  // and stores its handle here for later disconnect(). If the accessor
  // ever drifted to a copy-by-value, the disconnect would silently
  // become a no-op — verify the reference identity.
  QMetaObject::Connection &slot = mgr.itemsLoadedConnection();
  QMetaObject::Connection &slot2 = mgr.itemsLoadedConnection();
  QCOMPARE(&slot, &slot2);
}

void TestSearchManager::testUpdateSearchBarPlaceholderReflectsMode() {
  SearchManager mgr;
  QLineEdit bar;
  SearchManagerSetup setup;
  setup.searchBar = &bar;
  mgr.setupReferences(setup);

  // The three placeholders are user-visible labels for each scope; a typo
  // or regression in one of the switch arms would mislead the user about
  // which collections their query is hitting.
  mgr.setCurrentMode(SearchMode::CurrentCollection);
  mgr.updateSearchBarPlaceholder();
  QCOMPARE(bar.placeholderText(), QStringLiteral("Search current collection..."));

  mgr.setCurrentMode(SearchMode::CurrentAndSubcollections);
  mgr.updateSearchBarPlaceholder();
  QCOMPARE(bar.placeholderText(), QStringLiteral("Search current + subcollections..."));

  mgr.setCurrentMode(SearchMode::AllCollections);
  mgr.updateSearchBarPlaceholder();
  QCOMPARE(bar.placeholderText(), QStringLiteral("Search all collections..."));
}

void TestSearchManager::testToggleSearchModeEmitsOnModeChange() {
  SearchManager mgr;
  // Non-root collection: SearchHelpers::allowAllFor returns true via the
  // (hasSubs || isLeaf) branch — for a leaf child the cycle becomes
  // [CurrentCollection, AllCollections]. Toggling from Current must
  // advance to All and emit searchModeChanged exactly once.
  QList<CollectionConfig> collections;
  collections.append(makeRoot(QStringLiteral("Root")));
  collections.append(makeChild(QStringLiteral("Child"), /*parentIndex=*/0));
  int currentIndex = 1;

  SearchManagerSetup setup;
  setup.collections = &collections;
  setup.currentCollectionIndex = &currentIndex;
  mgr.setupReferences(setup);

  QSignalSpy spy(&mgr, &SearchManager::searchModeChanged);
  QCOMPARE(mgr.currentMode(), SearchMode::CurrentCollection);

  mgr.toggleSearchMode();

  QCOMPARE(mgr.currentMode(), SearchMode::AllCollections);
  QCOMPARE(spy.count(), 1);
  const QList<QVariant> args = spy.takeFirst();
  QCOMPARE(args.at(0).value<SearchMode>(), SearchMode::AllCollections);
}

void TestSearchManager::testToggleSearchModeNoSignalWhenSingleElementCycleMatchesCurrent() {
  SearchManager mgr;
  // Single root collection with no subs: cycle reduces to
  // [CurrentCollection] because allowAllFor's root branch requires
  // *another* root with direct items, and there isn't one. Toggling
  // when the cycle is one element and matches the current mode must
  // leave the mode untouched and emit nothing — otherwise the user
  // would see spurious mode-changed UI flicker on a no-op cycle.
  QList<CollectionConfig> collections;
  collections.append(makeRoot(QStringLiteral("OnlyRoot")));
  int currentIndex = 0;

  SearchManagerSetup setup;
  setup.collections = &collections;
  setup.currentCollectionIndex = &currentIndex;
  mgr.setupReferences(setup);

  QSignalSpy spy(&mgr, &SearchManager::searchModeChanged);
  QCOMPARE(mgr.currentMode(), SearchMode::CurrentCollection);

  mgr.toggleSearchMode();

  QCOMPARE(mgr.currentMode(), SearchMode::CurrentCollection);
  QCOMPARE(spy.count(), 0);
}

void TestSearchManager::testFixtureExposesSearchManagerInDefaultState() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  auto *interaction = win->getApplicationManager()->getInteractionManager();
  QVERIFY(interaction);

  // The live wiring exposes SearchManager via InteractionManager::searchManager()
  // after setupReferences(). A regression in InteractionManager::setupReferences
  // (interactionmanagersetup.cpp) that skipped the SearchManager setup would
  // surface either as a null pointer or as default state that drifted from
  // the spec the bare-construction tests above lock in.
  SearchManager *search = interaction->searchManager();
  QVERIFY(search);
  QCOMPARE(search->currentMode(), SearchMode::CurrentCollection);
  QVERIFY(!search->isSearchActive());
  QCOMPARE(search->currentSearchText(), QString());
  QVERIFY(search->debounceTimer() != nullptr);
}
