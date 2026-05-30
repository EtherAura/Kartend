/**
 * @file test_searchmanager.h
 * @brief Integration tests for SearchManager's public coordinator surface.
 *
 * State-only accessors (mode, active flag, current text, pre-search snapshot)
 * are exercised on a bare SearchManager instance — they touch no other
 * managers and have a clean DI seam. The mode-cycle and placeholder paths
 * use a partial SearchManagerSetup so the test can supply a real QLineEdit
 * and a synthetic collection list without standing up the full MainWindow.
 *
 * The fixture-backed test verifies the live wiring through
 * InteractionManager::searchManager() so a regression that breaks
 * setupReferences inside applicationmanager / interactionmanager_setup
 * is caught here too.
 *
 * The full debounced search path (onSearchTextChanged, performDebouncedSearch)
 * needs NavigationManager + ScrollManager + DatabaseManager wired together;
 * its end-to-end behavior is covered by the existing scroll/navigation
 * integration tests via the same MainWindowFixture. Here we stop at the
 * coordinator's local state machine.
 */

#ifndef KARTEND_TESTS_TEST_SEARCHMANAGER_H
#define KARTEND_TESTS_TEST_SEARCHMANAGER_H

#include <QObject>

class TestSearchManager : public QObject {
  Q_OBJECT

private slots:
  void testInitialStateDefaults();
  void testSetCurrentModeRoundtrip();
  void testSetSearchActiveRoundtrip();
  void testSetCurrentSearchTextRoundtrip();
  void testPreSearchAccessorsRoundtrip();
  void testDebounceTimerExists();
  void testItemsLoadedConnectionAccessorIsWritable();

  void testUpdateSearchBarPlaceholderReflectsMode();
  void testToggleSearchModeEmitsOnModeChange();
  void testToggleSearchModeNoSignalWhenSingleElementCycleMatchesCurrent();

  void testFixtureExposesSearchManagerInDefaultState();
};

#endif // KARTEND_TESTS_TEST_SEARCHMANAGER_H
