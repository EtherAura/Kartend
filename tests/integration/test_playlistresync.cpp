#include "test_playlistresync.h"

#include "applicationmanager.h"
#include "collection/collectionconfig.h"
#include "mainwindow.h"
#include "mocks/mockedmainwindowfixture.h"
#include "playlistmanager.h"

#include <QTest>

namespace {

int favoritesConfigCount(const QList<CollectionConfig> &collections) {
  int count = 0;
  for (const CollectionConfig &cfg : collections) {
    if (cfg.isPlaylist && cfg.playlistReservedKind == QStringLiteral("favorites")) {
      ++count;
    }
  }
  return count;
}

int favoritesRowCount(PlaylistManager *pm) {
  int count = 0;
  const auto rows = pm->loadAll();
  for (const auto &row : rows) {
    if (row.reservedKind == QStringLiteral("favorites")) {
      ++count;
    }
  }
  return count;
}

} // namespace

// Kartend-e120j: on a first run the wizard gates the deferred startup resync,
// but setupUI wires playlistsChanged -> resyncPlaylistCollections regardless.
// The first resync that actually executes then re-enters itself: its
// ensureFavoritesPlaylist() creates the reserved row and synchronously emits
// playlistsChanged, the wired handler runs a nested resync that appends a
// Favorites config, and the outer pass appends a second one. This test
// recreates that wiring topology directly — signal connected, favorites row
// absent, then one resync call — and requires the synthesized config to come
// out exactly once.
void TestPlaylistResync::reentrantPlaylistsChanged_doesNotDuplicateFavorites() {
  CollectionConfig shell;
  shell.name = QStringLiteral("Shell");
  KartendTest::MockedMainWindowFixture fixture({shell});
  MainWindow *win = fixture.window();
  QVERIFY(win);

  PlaylistManager *pm = win->applicationManager()->getPlaylistManager();
  QVERIFY(pm);

  // Preconditions that make the re-entry reachable: the reserved row must not
  // exist yet (so ensureFavoritesPlaylist inserts and emits), and no resync
  // may have run (the deferred startup singleShot has not fired — the event
  // loop was never pumped). If either fails, the test is no longer exercising
  // the first-run path and must be rethought, not relaxed.
  QCOMPARE(favoritesRowCount(pm), 0);
  QCOMPARE(favoritesConfigCount(win->collections()), 0);

  // Mirror the production playlistsChanged -> resync edge from setupUI.
  QObject::connect(pm, &PlaylistManager::playlistsChanged, win,
                   [win]() { win->resyncPlaylistCollections(); });

  win->resyncPlaylistCollections();

  QCOMPARE(favoritesRowCount(pm), 1);
  QCOMPARE(favoritesConfigCount(win->collections()), 1);

  // A follow-up resync (any later playlist edit lands here) must stay stable.
  win->resyncPlaylistCollections();
  QCOMPARE(favoritesConfigCount(win->collections()), 1);
}
