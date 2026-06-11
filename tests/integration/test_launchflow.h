#ifndef KARTEND_TESTS_TEST_LAUNCHFLOW_H
#define KARTEND_TESTS_TEST_LAUNCHFLOW_H

#include <QObject>

/// Kartend-8h8e2: end-to-end coverage for the core user loop — select an
/// item, launch it, and have the bookkeeping land in the real SQLite
/// sandbox. Unit tests cover LaunchManager (test_launchmanager.cpp) and the
/// stores (test_usagestatsstore.cpp) in isolation; this suite exercises the
/// wiring between them that only exists in the assembled application: the
/// InteractionManager-installed onLaunched / onPlaySessionEnded callbacks
/// that forward launches into DatabaseManager::recordItemLaunch /
/// recordItemPlaySession / recordHistoryEntry.
///
/// Uses the full MainWindowFixture (no DB mocking — the no-DB-mocking rule)
/// plus a no-op shell script as the launcher executable.
class TestLaunchFlow : public QObject {
  Q_OBJECT
private slots:
  // Detached launch (runtime detection off, the default): launchItem on a
  // scanned item must bump play_count + last_played in the items table,
  // append a history row, and surface the item in loadRecentlyPlayedItems.
  void testDetachedLaunchRecordsUsageStatsAndHistory();

  // Tracked launch (runtime detection on): the child process is a script
  // that sleeps ~1s, so on exit onPlaySessionEnded must accumulate a
  // positive total_play_seconds for the item.
  void testTrackedLaunchRecordsPlaySession();
};

#endif // KARTEND_TESTS_TEST_LAUNCHFLOW_H
