// Tests for PlaylistManager (Kartend-vlm7).
//
// PlaylistManager owns CRUD against the v10 `playlists` + `playlist_items`
// tables on the same media.db the rest of the app uses. The tests below
// stand up a fresh temp data dir, run create/rename/delete + add/remove
// against it, and assert the behaviours the synthesizer + QueryManager
// playlist branch rely on (densified positions, idempotent adds, FK
// cascades).
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "playlistmanager.h"

class TestPlaylistManager : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();

  void initialize_opensConnection();
  void createPlaylist_returnsIdAndPersists();
  void createPlaylist_rejectsBlankName();
  void renamePlaylist_updatesName();
  void deletePlaylist_cascadesItems();
  void addItem_appendsAtEndAndIsIdempotent();
  void removeItem_redensifiesPositions();
  void containsItem_reflectsCurrentMembership();
  void loadAll_returnsAllPlaylists();
  void loadItems_returnsItemsInPositionOrder();
  void parentCollectionUuid_isPersisted();
  void playlistsChangedSignal_firesOnEveryMutation();
  void ensureFavoritesPlaylist_createsExactlyOnce();
  void ensureFavoritesPlaylist_returnsExistingRow();
  void deletePlaylist_refusesReservedRow();
  void exportToJson_writesAllItemsAndRoundTrips();
  void exportToM3U_writesExtendedFormatWithBasenameTitles();
  void importFromM3U_skipsUnresolvableEntries();
  void importFromJson_rejectsMissingVersion();

private:
  QTemporaryDir m_tempDir;
  PlaylistManager *m_pm = nullptr;
};

void TestPlaylistManager::initTestCase() {
  // Tests must NOT touch the user's real ~/.local/share/kartend. setTestModeEnabled
  // re-routes QStandardPaths::AppDataLocation under XDG_DATA_HOME with a
  // /qttest/ prefix so each test run is isolated and disposable.
  QStandardPaths::setTestModeEnabled(true);
}

void TestPlaylistManager::init() {
  // Wipe any prior media.db left behind by an earlier test run so each test
  // starts from a fresh schema. Using QTemporaryDir would force us to override
  // PlaylistManager's path resolution; deleting the file is simpler and
  // exercises the real production code path.
  const QString dbPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/media.db";
  QFile::remove(dbPath);
  QFile::remove(dbPath + "-wal");
  QFile::remove(dbPath + "-shm");

  m_pm = new PlaylistManager();
  QVERIFY(m_pm->initialize());
}

void TestPlaylistManager::cleanup() {
  delete m_pm;
  m_pm = nullptr;
}

void TestPlaylistManager::initialize_opensConnection() {
  // initialize() has been called in init(); a second call should be a no-op
  // and still return true (idempotency contract callers rely on).
  QVERIFY(m_pm->initialize());
}

void TestPlaylistManager::createPlaylist_returnsIdAndPersists() {
  auto result = m_pm->createPlaylist("Favourites");
  QVERIFY(result.isOk());
  const QString id = result.value();
  QVERIFY(!id.isEmpty());

  const auto rows = m_pm->loadAll();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.first().id, id);
  QCOMPARE(rows.first().name, QString("Favourites"));
}

void TestPlaylistManager::createPlaylist_rejectsBlankName() {
  auto blank = m_pm->createPlaylist("   ");
  QVERIFY(blank.isError());
  QCOMPARE(m_pm->loadAll().size(), 0);
}

void TestPlaylistManager::renamePlaylist_updatesName() {
  auto created = m_pm->createPlaylist("Old");
  QVERIFY(created.isOk());
  const QString id = created.value();

  QVERIFY(m_pm->renamePlaylist(id, "New"));
  const auto rows = m_pm->loadAll();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.first().name, QString("New"));

  // Renaming to blank should be refused so the row never carries an empty
  // user-visible name.
  QVERIFY(!m_pm->renamePlaylist(id, "  "));
  QCOMPARE(m_pm->loadAll().first().name, QString("New"));

  // Unknown id should also fail without affecting other rows.
  QVERIFY(!m_pm->renamePlaylist("no-such-id", "Whatever"));
  QCOMPARE(m_pm->loadAll().first().name, QString("New"));
}

void TestPlaylistManager::deletePlaylist_cascadesItems() {
  auto created = m_pm->createPlaylist("Doomed");
  QVERIFY(created.isOk());
  const QString id = created.value();

  QVERIFY(m_pm->addItem(id, "uuid-a", "/games/a.rom"));
  QVERIFY(m_pm->addItem(id, "uuid-a", "/games/b.rom"));
  QCOMPARE(m_pm->loadItems(id).size(), 2);

  QVERIFY(m_pm->deletePlaylist(id));
  QCOMPARE(m_pm->loadAll().size(), 0);
  // FK ON DELETE CASCADE should have removed the items too.
  QCOMPARE(m_pm->loadItems(id).size(), 0);
}

void TestPlaylistManager::addItem_appendsAtEndAndIsIdempotent() {
  auto created = m_pm->createPlaylist("Mix");
  QVERIFY(created.isOk());
  const QString id = created.value();

  QVERIFY(m_pm->addItem(id, "uuid-a", "/a.rom"));
  QVERIFY(m_pm->addItem(id, "uuid-a", "/b.rom"));
  QVERIFY(m_pm->addItem(id, "uuid-b", "/c.iso"));

  const auto items = m_pm->loadItems(id);
  QCOMPARE(items.size(), 3);
  QCOMPARE(items[0].position, 0);
  QCOMPARE(items[0].sourcePath, QString("/a.rom"));
  QCOMPARE(items[1].position, 1);
  QCOMPARE(items[1].sourcePath, QString("/b.rom"));
  QCOMPARE(items[2].position, 2);
  QCOMPARE(items[2].sourcePath, QString("/c.iso"));

  // Re-adding the same (uuid, path) is an explicit no-op; positions don't
  // change and the loader still returns three rows. This contract lets the
  // context-menu dispatcher fire add() unconditionally without de-duping
  // upstream.
  QVERIFY(!m_pm->addItem(id, "uuid-a", "/a.rom"));
  QCOMPARE(m_pm->loadItems(id).size(), 3);
}

void TestPlaylistManager::removeItem_redensifiesPositions() {
  auto created = m_pm->createPlaylist("Mix");
  QVERIFY(created.isOk());
  const QString id = created.value();

  m_pm->addItem(id, "u", "/a");
  m_pm->addItem(id, "u", "/b");
  m_pm->addItem(id, "u", "/c");
  m_pm->addItem(id, "u", "/d");

  // Remove the middle entry; positions must collapse to 0..N-1 so a future
  // ORDER BY position keeps surfacing the surviving rows in the original
  // user-visible order.
  QVERIFY(m_pm->removeItem(id, "u", "/b"));
  const auto items = m_pm->loadItems(id);
  QCOMPARE(items.size(), 3);
  QCOMPARE(items[0].position, 0);
  QCOMPARE(items[0].sourcePath, QString("/a"));
  QCOMPARE(items[1].position, 1);
  QCOMPARE(items[1].sourcePath, QString("/c"));
  QCOMPARE(items[2].position, 2);
  QCOMPARE(items[2].sourcePath, QString("/d"));

  // Removing a non-member returns false rather than throwing, mirroring the
  // pattern in renamePlaylist for unknown ids.
  QVERIFY(!m_pm->removeItem(id, "u", "/never-added"));
}

void TestPlaylistManager::containsItem_reflectsCurrentMembership() {
  auto created = m_pm->createPlaylist("Probe");
  const QString id = created.value();
  m_pm->addItem(id, "u", "/x");

  QVERIFY(m_pm->containsItem(id, "u", "/x"));
  QVERIFY(!m_pm->containsItem(id, "u", "/y"));

  m_pm->removeItem(id, "u", "/x");
  QVERIFY(!m_pm->containsItem(id, "u", "/x"));
}

void TestPlaylistManager::loadAll_returnsAllPlaylists() {
  m_pm->createPlaylist("A");
  m_pm->createPlaylist("B");
  m_pm->createPlaylist("C");
  QCOMPARE(m_pm->loadAll().size(), 3);
}

void TestPlaylistManager::loadItems_returnsItemsInPositionOrder() {
  auto created = m_pm->createPlaylist("Ordered");
  const QString id = created.value();
  m_pm->addItem(id, "u", "/zeta");
  m_pm->addItem(id, "u", "/alpha");
  m_pm->addItem(id, "u", "/mu");

  // Position order is insertion order — alphabetic input shouldn't reorder.
  const auto items = m_pm->loadItems(id);
  QCOMPARE(items.size(), 3);
  QCOMPARE(items[0].sourcePath, QString("/zeta"));
  QCOMPARE(items[1].sourcePath, QString("/alpha"));
  QCOMPARE(items[2].sourcePath, QString("/mu"));
}

void TestPlaylistManager::parentCollectionUuid_isPersisted() {
  auto created = m_pm->createPlaylist("Nested", "parent-uuid", "favorites");
  QVERIFY(created.isOk());
  const auto rows = m_pm->loadAll();
  QCOMPARE(rows.first().parentCollectionUuid, QString("parent-uuid"));
  QCOMPARE(rows.first().reservedKind, QString("favorites"));
}

void TestPlaylistManager::playlistsChangedSignal_firesOnEveryMutation() {
  // The synthesizer wired in MainWindow re-runs on every emission of this
  // signal — this test pins the contract so a future refactor that drops an
  // emission point breaks the suite instead of the sidebar.
  QSignalSpy spy(m_pm, &PlaylistManager::playlistsChanged);

  auto created = m_pm->createPlaylist("S");
  QCOMPARE(spy.count(), 1);
  const QString id = created.value();

  m_pm->renamePlaylist(id, "Sx");
  QCOMPARE(spy.count(), 2);

  m_pm->addItem(id, "u", "/a");
  QCOMPARE(spy.count(), 3);

  m_pm->removeItem(id, "u", "/a");
  QCOMPARE(spy.count(), 4);

  m_pm->deletePlaylist(id);
  QCOMPARE(spy.count(), 5);
}

void TestPlaylistManager::ensureFavoritesPlaylist_createsExactlyOnce() {
  // First call creates the row; subsequent calls return the same id without
  // inserting duplicates. The synthesizer in MainWindow relies on this
  // idempotency so resyncPlaylistCollections() can call it on every refresh
  // without piling up rows.
  const QString first = m_pm->ensureFavoritesPlaylist();
  QVERIFY(!first.isEmpty());

  const QString second = m_pm->ensureFavoritesPlaylist();
  QCOMPARE(second, first);

  const auto rows = m_pm->loadAll();
  int favCount = 0;
  for (const auto &row : rows) {
    if (row.reservedKind == "favorites") {
      ++favCount;
    }
  }
  QCOMPARE(favCount, 1);
}

void TestPlaylistManager::ensureFavoritesPlaylist_returnsExistingRow() {
  // Pre-seed the row with a custom name (mimicking a user-renamed favorites
  // slot persisted from a prior session). ensureFavoritesPlaylist must adopt
  // the existing id rather than creating a parallel "Favorites" row.
  auto created = m_pm->createPlaylist("My Loves", QString(), "favorites");
  QVERIFY(created.isOk());
  const QString preExistingId = created.value();

  // Drop the cached id to force the SQL probe path (mirrors a fresh-process
  // ensure-on-startup, where the cache is empty but a row exists).
  delete m_pm;
  m_pm = new PlaylistManager();
  QVERIFY(m_pm->initialize());

  const QString resolvedId = m_pm->ensureFavoritesPlaylist();
  QCOMPARE(resolvedId, preExistingId);
  // The custom name survives — ensureFavoritesPlaylist never overwrites the
  // user's chosen label.
  const auto rows = m_pm->loadAll();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.first().name, QString("My Loves"));
}

void TestPlaylistManager::deletePlaylist_refusesReservedRow() {
  const QString favId = m_pm->ensureFavoritesPlaylist();
  QVERIFY(!favId.isEmpty());

  // Reserved rows can be added to and renamed, but never deleted — preserves
  // the stable id callers cache, and avoids the silent-data-loss footgun of
  // a future ensureFavoritesPlaylist() recreating it under a fresh id with
  // every prior starred reference orphaned.
  m_pm->addItem(favId, "u", "/g/1");
  QVERIFY(!m_pm->deletePlaylist(favId));
  QCOMPARE(m_pm->loadAll().size(), 1);
  QCOMPARE(m_pm->loadItems(favId).size(), 1);

  // Rename still works — users can localize the label without losing the row.
  QVERIFY(m_pm->renamePlaylist(favId, "Bookmarks"));
  QCOMPARE(m_pm->loadAll().first().name, QString("Bookmarks"));
  QCOMPARE(m_pm->loadAll().first().reservedKind, QString("favorites"));
}

// ────────────────────────────────────────────────────────────────────────────
// Import / export tests (Kartend-5pqv)
// ────────────────────────────────────────────────────────────────────────────

namespace {

// Drops the in-memory items table back to a known shape. PlaylistManager
// doesn't create or own this table — it's normally populated by
// DatabaseManager — but the M3U import path queries it to resolve paths to
// (uuid, path) pairs, so we stand up a minimal version on the same
// connection.
void seedItemsTable(QSqlDatabase db) {
  QSqlQuery q(db);
  q.exec("CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, "
         "collection_uuid TEXT, path TEXT)");
  q.exec("DELETE FROM items");
}

void insertItem(QSqlDatabase db, const QString &uuid, const QString &path) {
  QSqlQuery q(db);
  q.prepare("INSERT INTO items (collection_uuid, path) VALUES (?, ?)");
  q.addBindValue(uuid);
  q.addBindValue(path);
  q.exec();
}

QSqlDatabase openSharedConnection() {
  // PlaylistManager opens the connection name "kartend_playlists_main" on
  // its own; we ride alongside via the same name to seed the items table.
  return QSqlDatabase::database("kartend_playlists_main");
}

} // namespace

void TestPlaylistManager::exportToJson_writesAllItemsAndRoundTrips() {
  auto created = m_pm->createPlaylist("Round Trip");
  QVERIFY(created.isOk());
  const QString id = created.value();
  m_pm->addItem(id, "uuid-a", "/games/a.rom");
  m_pm->addItem(id, "uuid-b", "/movies/b.mkv");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString jsonPath = tmp.filePath("export.json");
  auto exported = m_pm->exportToJson(id, jsonPath);
  QVERIFY(exported.isOk());
  QCOMPARE(exported.value(), 2);

  // Re-import the exported file and assert all items survived with their
  // original (uuid, path) pairs preserved — this is the lossless round-trip
  // promise of the JSON format.
  auto reimport = m_pm->importFromJson(jsonPath, "Round Trip Copy");
  QVERIFY(reimport.isOk());
  const QString newId = reimport.value();

  const auto items = m_pm->loadItems(newId);
  QCOMPARE(items.size(), 2);
  QCOMPARE(items[0].sourceCollectionUuid, QString("uuid-a"));
  QCOMPARE(items[0].sourcePath, QString("/games/a.rom"));
  QCOMPARE(items[1].sourceCollectionUuid, QString("uuid-b"));
  QCOMPARE(items[1].sourcePath, QString("/movies/b.mkv"));

  // The override-name beat the embedded "Round Trip", confirming the
  // disambiguation path that lets users distinguish a re-imported copy.
  bool sawCopyName = false;
  for (const auto &row : m_pm->loadAll()) {
    if (row.id == newId && row.name == "Round Trip Copy") {
      sawCopyName = true;
    }
  }
  QVERIFY(sawCopyName);
}

void TestPlaylistManager::exportToM3U_writesExtendedFormatWithBasenameTitles() {
  auto created = m_pm->createPlaylist("Mix");
  const QString id = created.value();
  m_pm->addItem(id, "u", "/games/Sonic 2.gen");
  m_pm->addItem(id, "u", "/movies/Akira.mkv");

  QTemporaryDir tmp;
  const QString m3uPath = tmp.filePath("mix.m3u");
  auto result = m_pm->exportToM3U(id, m3uPath);
  QVERIFY(result.isOk());
  QCOMPARE(result.value(), 2);

  QFile f(m3uPath);
  QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString contents = QString::fromUtf8(f.readAll());
  // #EXTM3U on line 1 unlocks the extended-format path in most parsers; the
  // EXTINF/path pairs must follow in insertion order so the output stays
  // consistent with what the user sees in the grid.
  QVERIFY(contents.startsWith("#EXTM3U\n"));
  QVERIFY(contents.contains("#EXTINF:-1,Sonic 2"));
  QVERIFY(contents.contains("/games/Sonic 2.gen"));
  QVERIFY(contents.contains("#EXTINF:-1,Akira"));
  QVERIFY(contents.contains("/movies/Akira.mkv"));
}

void TestPlaylistManager::importFromM3U_skipsUnresolvableEntries() {
  // Seed two items in the DB; the M3U file references three. The third
  // should be skipped and counted in outSkipped — the alternative ("store
  // as broken ref") would silently bloat the playlist with unresolvable
  // rows that the existence-checked fetch SQL never surfaces anyway.
  QSqlDatabase db = openSharedConnection();
  seedItemsTable(db);
  insertItem(db, "uuid-x", "/games/found1.rom");
  insertItem(db, "uuid-y", "/games/found2.rom");

  QTemporaryDir tmp;
  const QString m3uPath = tmp.filePath("input.m3u");
  QFile f(m3uPath);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  f.write("#EXTM3U\n");
  f.write("/games/found1.rom\n");
  f.write("/games/missing.rom\n");
  f.write("#EXTINF:-1,Found Two\n");
  f.write("/games/found2.rom\n");
  f.close();

  int skipped = -1;
  auto created = m_pm->importFromM3U(m3uPath, "Imported", &skipped);
  QVERIFY(created.isOk());
  QCOMPARE(skipped, 1);

  const auto items = m_pm->loadItems(created.value());
  QCOMPARE(items.size(), 2);
  // Resolved uuids come from the items table — confirms importFromM3U is
  // doing the path → uuid lookup rather than blindly storing empty uuids.
  QCOMPARE(items[0].sourceCollectionUuid, QString("uuid-x"));
  QCOMPARE(items[1].sourceCollectionUuid, QString("uuid-y"));
}

void TestPlaylistManager::importFromJson_rejectsMissingVersion() {
  QTemporaryDir tmp;
  const QString jsonPath = tmp.filePath("bad.json");
  QFile f(jsonPath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(R"({"name": "No Version", "items": []})");
  f.close();

  // Without kartend_playlist_version, we can't know the schema shape — bail
  // rather than guess, so a future schema bump catches stale tooling
  // explicitly instead of silently accepting incompatible data.
  auto result = m_pm->importFromJson(jsonPath);
  QVERIFY(result.isError());
  QCOMPARE(m_pm->loadAll().size(), 0);
}

QTEST_MAIN(TestPlaylistManager)
#include "test_playlistmanager.moc"
