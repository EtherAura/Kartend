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

  // Kartend-ecjq6 (argv slice): the archive-launch extraction runs the real
  // LaunchManager::extractArchiveToTemp against a stub extractor on an
  // isolated PATH that records every argv it is handed. The listing scan and
  // the extraction are told apart, the extraction must actually run in the
  // per-archive dir on the intended archive, and its argv must carry no
  // link/path-relaxing switch (bsdtar: no -P; 7z: no -snld* and kin).
  void testArchiveExtractionBsdtarSafeArguments();
  void testArchiveExtraction7zSafeArguments();
  // Kartend-ecjq6: the production extraction path into the installed bsdtar
  // (skipped where bsdtar is absent), on runtime-built hostile archives.
  void testArchiveExtractionRealBsdtarExtractsBenignArchive();
  void testArchiveExtractionRealBsdtarRefusesSymlinkChainEscape();
  void testArchiveExtractionRealBsdtarRefusesDotDotEscape();
  // Kartend-ecjq6: the same path into the installed 7z (skipped where 7z is
  // absent). 7z refuses the symlink chain; for ".." members it strips the
  // dot-dot components and extracts inside the destination, which is the
  // approved outcome as long as nothing outside it is created or changed.
  void testArchiveExtractionReal7zExtractsBenignArchive();
  void testArchiveExtractionReal7zRefusesSymlinkChainEscape();
  void testArchiveExtractionReal7zContainsDotDotEntries();
};

#endif // KARTEND_TESTS_TEST_LAUNCHFLOW_H
