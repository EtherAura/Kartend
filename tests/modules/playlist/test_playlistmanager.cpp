// Tests for PlaylistManager.
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

#include "../../support/inspectordb.h"
#include "databaseschema.h"
#include "playlistmanager.h"
#include "smartfilter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class TestPlaylistManager : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();

  void initialize_opensConnection();
  void initialize_addsSmartColumnsToV10Table();
  void initialize_preservesSchemaVersionOnBootstrappedDb();
  void createPlaylist_returnsIdAndPersists();
  void createPlaylist_rejectsBlankName();
  void renamePlaylist_updatesName();
  void deletePlaylist_cascadesItems();
  void addItem_appendsAtEndAndIsIdempotent();
  void removeItem_redensifiesPositions();
  void removeThenAdd_stampsUpdatedAtDespiteRowidReuse();
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
  void importFromM3U_resolvesMultiCollectionPathDeterministically();
  void importFromJson_rejectsMissingVersion();
  void importFromJson_isAtomicAndDedupsDuplicates();
  void createSmartPlaylist_persistsFlagAndFilter();
  void updateSmartFilter_changesPersistedJson();
  void loadSmartFilter_rejectsStaticPlaylist();
  void exportImportSmartPlaylist_roundTripsViaV2Json();
  void multiRuleSmartPlaylist_survivesStoreAndJsonRoundTrip();
  void importV1Json_stillCreatesStaticPlaylist();

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

void TestPlaylistManager::initialize_addsSmartColumnsToV10Table() {
  // A long-lived install whose media.db last saw the v10 migration has a
  // playlists table without is_smart/smart_filter. The standalone
  // initialize() must bring it to the v11 shape via the migration ladder's
  // probe-first ensureColumn — the previous blind ALTERs discarded their
  // results, conflating the benign duplicate-column case with real
  // locked-database/IO failures.
  delete m_pm; // release the connection init() opened so the file can be replaced
  m_pm = nullptr;
  const QString dbPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/media.db";
  QFile::remove(dbPath);
  QFile::remove(dbPath + "-wal");
  QFile::remove(dbPath + "-shm");

  {
    auto seed = QSqlDatabase::addDatabase("QSQLITE", "seed_v10");
    seed.setDatabaseName(dbPath);
    QVERIFY(seed.open());
    QSqlQuery q(seed);
    // The exact v10 playlists shape (no smart-playlist columns).
    QVERIFY(q.exec("CREATE TABLE playlists ("
                   "id TEXT PRIMARY KEY, "
                   "name TEXT NOT NULL, "
                   "icon TEXT NOT NULL DEFAULT '', "
                   "parent_collection_uuid TEXT NOT NULL DEFAULT '', "
                   "reserved_kind TEXT NOT NULL DEFAULT '', "
                   "created_at TEXT NOT NULL DEFAULT '', "
                   "updated_at TEXT NOT NULL DEFAULT ''"
                   ")"));
    QVERIFY(q.exec("INSERT INTO playlists (id, name) VALUES ('pre', 'Pre-upgrade')"));
    seed.close();
  }
  QSqlDatabase::removeDatabase("seed_v10");

  m_pm = new PlaylistManager();
  QVERIFY(m_pm->initialize());

  // The columns were added…
  {
    QSqlQuery info(QSqlDatabase::database("kartend_playlists_main"));
    QVERIFY(info.exec("PRAGMA table_info(playlists)"));
    QStringList columns;
    while (info.next()) {
      columns << info.value(1).toString();
    }
    QVERIFY(columns.contains("is_smart"));
    QVERIFY(columns.contains("smart_filter"));
  }

  // …the pre-existing row survived as a static playlist (loadAll selects the
  // new columns, so it would fail outright without the upgrade)…
  const auto rows = m_pm->loadAll();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.first().id, QString("pre"));
  QCOMPARE(rows.first().name, QString("Pre-upgrade"));
  QVERIFY(!rows.first().isSmart);

  // …and the upgraded table supports smart playlists end-to-end.
  SmartFilter::Filter filter;
  filter.kind = SmartFilter::Kind::TopPlayed;
  filter.limit = 5;
  QVERIFY(m_pm->createSmartPlaylist("Top 5", {SmartFilter::MatchMode::All, {filter}}).isOk());
}

void TestPlaylistManager::initialize_preservesSchemaVersionOnBootstrappedDb() {
  // Production startup order: DatabaseManager bootstraps + migrates media.db,
  // THEN the standalone playlist connection opens the same file. Its local
  // DDL must be a pure no-op there — the probe helper sees the v11 columns
  // already in place — and it must never touch user_version (it does not run
  // the migration ladder). Runs the production bootstrap directly: the same
  // sequence as the shared MigratedDb fixture, which cannot be used here
  // because PlaylistManager resolves the AppDataLocation path itself.
  delete m_pm;
  m_pm = nullptr;
  const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QFile::remove(dataDir + "/media.db");
  QFile::remove(dataDir + "/media.db-wal");
  QFile::remove(dataDir + "/media.db-shm");

  int migratedVersion = 0;
  {
    auto boot = QSqlDatabase::addDatabase("QSQLITE", "boot_bootstrap");
    QVERIFY(DatabaseSchema::openConnection(boot, dataDir));
    DatabaseSchema::applyConnectionPragmas(boot);
    DatabaseSchema::createTables(boot); // runs the migration ladder internally
    DatabaseSchema::createIndexes(boot);
    QSqlQuery q(boot);
    QVERIFY(q.exec("PRAGMA user_version") && q.next());
    migratedVersion = q.value(0).toInt();
    QVERIFY(migratedVersion > 0);
    boot.close();
  }
  QSqlDatabase::removeDatabase("boot_bootstrap");

  m_pm = new PlaylistManager();
  QVERIFY(m_pm->initialize());
  QVERIFY(m_pm->createPlaylist("After bootstrap").isOk());

  QSqlQuery check(QSqlDatabase::database("kartend_playlists_main"));
  QVERIFY(check.exec("PRAGMA user_version") && check.next());
  QCOMPARE(check.value(0).toInt(), migratedVersion);
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

void TestPlaylistManager::removeThenAdd_stampsUpdatedAtDespiteRowidReuse() {
  // Regression for the static-playlist scope cache key (Kartend-309nh.5):
  // playlist_items is a non-AUTOINCREMENT rowid table and removeItem's
  // wipe-and-reinsert frees the top rowids, so remove-one-then-add-one
  // re-mints the previous MAX(rowid) for different contents. QueryManager's
  // scope key therefore folds in playlists.updated_at, which both mutations
  // must stamp inside their transactions.
  auto created = m_pm->createPlaylist("Scope");
  QVERIFY(created.isOk());
  const QString id = created.value();

  QVERIFY(m_pm->addItem(id, "u", "/a"));
  QVERIFY(m_pm->addItem(id, "u", "/b"));

  const QString dbPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/media.db";
  KartendTest::InspectorDb inspector(dbPath, QStringLiteral("test_playlistmanager_scope_inspect"));
  QVERIFY2(inspector.isOpen(), "Failed to open inspector database");

  struct Snapshot {
    qlonglong maxRowid = -1;
    QString updatedAt;
  };
  const auto snapshot = [&]() -> Snapshot {
    Snapshot s;
    QSqlQuery q(inspector.db());
    q.prepare(QStringLiteral(
        "SELECT (SELECT COALESCE(MAX(rowid), 0) FROM playlist_items WHERE playlist_id = ?), "
        "(SELECT updated_at FROM playlists WHERE id = ?)"));
    q.addBindValue(id);
    q.addBindValue(id);
    if (q.exec() && q.next()) {
      s.maxRowid = q.value(0).toLongLong();
      s.updatedAt = q.value(1).toString();
    }
    return s;
  };

  const Snapshot before = snapshot();
  QVERIFY(before.maxRowid > 0);
  QVERIFY(!before.updatedAt.isEmpty());

  // isoNow() has millisecond precision; make sure the mutation below lands on
  // a later tick than the setup adds so a same-stamp pass can't mask a
  // missing touch.
  QTest::qSleep(2);
  QVERIFY(m_pm->removeItem(id, "u", "/b"));
  QVERIFY(m_pm->addItem(id, "u", "/c"));

  const Snapshot after = snapshot();
  // The re-densify wipe empties the playlist's rowids, so the reinsert + add
  // re-mint the same MAX(rowid) — this is exactly why the raw rowid probe is
  // not a sufficient invalidation token on its own.
  QCOMPARE(after.maxRowid, before.maxRowid);
  // updated_at must have moved, so the folded scope key still changes.
  QVERIFY(after.updatedAt != before.updatedAt);
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
// Import / export tests
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

void TestPlaylistManager::importFromM3U_resolvesMultiCollectionPathDeterministically() {
  // Kartend-o84pt: a path present in more than one collection is ambiguous in
  // M3U (the format carries no collection identity). Import must resolve it
  // deterministically (lowest collection uuid via ORDER BY), not by SQLite row
  // order. Insert the higher uuid first so a naive LIMIT-1-by-rowid would pick
  // the wrong one.
  QSqlDatabase db = openSharedConnection();
  seedItemsTable(db);
  insertItem(db, "uuid-zeta", "/games/shared.rom");
  insertItem(db, "uuid-alpha", "/games/shared.rom");

  QTemporaryDir tmp;
  const QString m3uPath = tmp.filePath("dup.m3u");
  QFile f(m3uPath);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  f.write("#EXTM3U\n/games/shared.rom\n");
  f.close();

  auto created = m_pm->importFromM3U(m3uPath, "Imported");
  QVERIFY(created.isOk());
  const auto items = m_pm->loadItems(created.value());
  QCOMPARE(items.size(), 1);
  QCOMPARE(items[0].sourceCollectionUuid, QString("uuid-alpha")); // lowest uuid wins, deterministic
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

void TestPlaylistManager::importFromJson_isAtomicAndDedupsDuplicates() {
  // Kartend-fd7xl: items now import inside a single transaction; duplicate
  // (uuid,path) entries collapse to one (matching addItem's idempotent skip)
  // and empty-path entries are dropped, with insertion order preserved.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString jsonPath = tmp.filePath("dup.json");
  QFile f(jsonPath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(R"({
    "kartend_playlist_version": 1,
    "name": "Dups",
    "items": [
      {"source_collection_uuid": "uuid-a", "source_path": "/games/a.rom"},
      {"source_collection_uuid": "uuid-b", "source_path": "/movies/b.mkv"},
      {"source_collection_uuid": "uuid-a", "source_path": "/games/a.rom"},
      {"source_collection_uuid": "", "source_path": ""}
    ]
  })");
  f.close();

  auto result = m_pm->importFromJson(jsonPath, "Dups Copy");
  QVERIFY(result.isOk());

  const auto items = m_pm->loadItems(result.value());
  QCOMPARE(items.size(), 2); // duplicate and empty-path entries dropped
  QCOMPARE(items[0].sourceCollectionUuid, QString("uuid-a"));
  QCOMPARE(items[0].sourcePath, QString("/games/a.rom"));
  QCOMPARE(items[1].sourceCollectionUuid, QString("uuid-b"));
  QCOMPARE(items[1].sourcePath, QString("/movies/b.mkv"));
}

void TestPlaylistManager::createSmartPlaylist_persistsFlagAndFilter() {
  SmartFilter::Filter filter;
  filter.kind = SmartFilter::Kind::TopPlayed;
  filter.limit = 25;

  auto created = m_pm->createSmartPlaylist("Top 25", {SmartFilter::MatchMode::All, {filter}});
  QVERIFY(created.isOk());
  const QString id = created.value();

  const auto rows = m_pm->loadAll();
  QCOMPARE(rows.size(), 1);
  QVERIFY(rows.first().isSmart);
  QVERIFY(!rows.first().smartFilterJson.isEmpty());

  // The serialized filter must round-trip back through loadSmartFilter.
  auto loaded = m_pm->loadSmartFilter(id);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().rules.size(), 1);
  QCOMPARE(static_cast<int>(loaded.value().rules[0].kind),
           static_cast<int>(SmartFilter::Kind::TopPlayed));
  QCOMPARE(loaded.value().rules[0].limit, 25);
}

void TestPlaylistManager::updateSmartFilter_changesPersistedJson() {
  SmartFilter::Filter initial;
  initial.kind = SmartFilter::Kind::RecentlyLaunched;
  initial.limit = 10;
  auto created = m_pm->createSmartPlaylist("Recents", {SmartFilter::MatchMode::All, {initial}});
  QVERIFY(created.isOk());
  const QString id = created.value();

  SmartFilter::Filter updated;
  updated.kind = SmartFilter::Kind::ByExtension;
  updated.extensions = {"mp4", "mkv"};
  QVERIFY(m_pm->updateSmartFilter(id, {SmartFilter::MatchMode::All, {updated}}));

  auto reloaded = m_pm->loadSmartFilter(id);
  QVERIFY(reloaded.isOk());
  QCOMPARE(reloaded.value().rules.size(), 1);
  QCOMPARE(static_cast<int>(reloaded.value().rules[0].kind),
           static_cast<int>(SmartFilter::Kind::ByExtension));
  QCOMPARE(reloaded.value().rules[0].extensions, QStringList({"mp4", "mkv"}));
}

void TestPlaylistManager::loadSmartFilter_rejectsStaticPlaylist() {
  // Static playlists carry no filter; the API surfaces an error so the
  // caller can avoid showing an empty Edit dialog rather than fabricating
  // a default filter.
  auto created = m_pm->createPlaylist("Static");
  QVERIFY(created.isOk());
  auto loaded = m_pm->loadSmartFilter(created.value());
  QVERIFY(loaded.isError());
}

void TestPlaylistManager::exportImportSmartPlaylist_roundTripsViaV2Json() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  SmartFilter::Filter filter;
  filter.kind = SmartFilter::Kind::NeverPlayed;
  filter.limit = 8;
  auto created = m_pm->createSmartPlaylist("Untouched", {SmartFilter::MatchMode::All, {filter}});
  QVERIFY(created.isOk());

  const QString jsonPath = tmp.filePath("untouched.json");
  auto exported = m_pm->exportToJson(created.value(), jsonPath);
  QVERIFY(exported.isOk());

  // Sniff the on-disk shape: v2 emits is_smart + smart_filter at the top
  // level so a downstream tool can route without parsing the items list.
  QFile f(jsonPath);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const auto doc = QJsonDocument::fromJson(f.readAll());
  f.close();
  QVERIFY(doc.isObject());
  const auto root = doc.object();
  QCOMPARE(root.value("kartend_playlist_version").toInt(), 2);
  QVERIFY(root.value("is_smart").toBool(false));
  QVERIFY(!root.value("smart_filter").toString().isEmpty());

  // Round-trip: import the file under a new name and verify the smart
  // flag survives along with the filter contents.
  auto reimported = m_pm->importFromJson(jsonPath, "Untouched (Copy)");
  QVERIFY(reimported.isOk());
  const QString newId = reimported.value();
  auto reloaded = m_pm->loadSmartFilter(newId);
  QVERIFY(reloaded.isOk());
  QCOMPARE(reloaded.value().rules.size(), 1);
  QCOMPARE(static_cast<int>(reloaded.value().rules[0].kind),
           static_cast<int>(SmartFilter::Kind::NeverPlayed));
  QCOMPARE(reloaded.value().rules[0].limit, 8);
}

void TestPlaylistManager::multiRuleSmartPlaylist_survivesStoreAndJsonRoundTrip() {
  // Kartend-8pn2w. A composed playlist used to lose every rule but the
  // first, and to do it SILENTLY: the wire format mirrors rule[0] into the
  // legacy top-level fields precisely so an older build can still read the
  // playlist, which meant the single-Filter parse on the store and import
  // paths succeeded and quietly dropped the rest.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  SmartFilter::Filter neverPlayed;
  neverPlayed.kind = SmartFilter::Kind::NeverPlayed;
  neverPlayed.limit = 8;
  SmartFilter::Filter pdfs;
  pdfs.kind = SmartFilter::Kind::ByExtension;
  pdfs.extensions = {"pdf"};
  const SmartFilter::FilterSet composed{SmartFilter::MatchMode::All, {neverPlayed, pdfs}};

  auto created = m_pm->createSmartPlaylist("Unread PDFs", composed);
  QVERIFY(created.isOk());

  // Straight out of the database first: both rules and the match mode.
  auto loaded = m_pm->loadSmartFilter(created.value());
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().rules.size(), 2);
  QCOMPARE(static_cast<int>(loaded.value().match), static_cast<int>(SmartFilter::MatchMode::All));
  QCOMPARE(static_cast<int>(loaded.value().rules[1].kind),
           static_cast<int>(SmartFilter::Kind::ByExtension));
  QCOMPARE(loaded.value().rules[1].extensions, QStringList{"pdf"});

  // …then through a v2 JSON export/import, which is the path that was
  // flattening. Any is a deliberate non-default so a match mode reset to
  // the All default would show up as a failure rather than a coincidence.
  const SmartFilter::FilterSet anyOf{SmartFilter::MatchMode::Any, {neverPlayed, pdfs}};
  QVERIFY(m_pm->updateSmartFilter(created.value(), anyOf));
  const QString jsonPath = tmp.filePath("unread.json");
  QVERIFY(m_pm->exportToJson(created.value(), jsonPath).isOk());

  auto reimported = m_pm->importFromJson(jsonPath, "Unread PDFs (Copy)");
  QVERIFY(reimported.isOk());
  auto reloaded = m_pm->loadSmartFilter(reimported.value());
  QVERIFY(reloaded.isOk());
  QCOMPARE(reloaded.value().rules.size(), 2);
  QCOMPARE(static_cast<int>(reloaded.value().match), static_cast<int>(SmartFilter::MatchMode::Any));
  QCOMPARE(static_cast<int>(reloaded.value().rules[0].kind),
           static_cast<int>(SmartFilter::Kind::NeverPlayed));
  QCOMPARE(reloaded.value().rules[0].limit, 8);
  QCOMPARE(reloaded.value().rules[1].extensions, QStringList{"pdf"});
}

void TestPlaylistManager::importV1Json_stillCreatesStaticPlaylist() {
  // A pre-v2 export has no is_smart field. The importer must keep
  // accepting these as static playlists rather than rejecting them — the
  // user's existing JSON files on disk (and any third-party-tool output
  // matching v1) need to keep working through the upgrade.
  QTemporaryDir tmp;
  const QString jsonPath = tmp.filePath("v1.json");
  QFile f(jsonPath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(R"({"kartend_playlist_version": 1,
              "name": "Old style",
              "icon": "",
              "parent_collection_uuid": "",
              "items": []})");
  f.close();

  auto result = m_pm->importFromJson(jsonPath);
  QVERIFY(result.isOk());
  const auto rows = m_pm->loadAll();
  QCOMPARE(rows.size(), 1);
  QVERIFY(!rows.first().isSmart);
}

QTEST_MAIN(TestPlaylistManager)
#include "test_playlistmanager.moc"
