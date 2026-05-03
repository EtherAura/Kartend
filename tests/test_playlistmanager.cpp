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

QTEST_MAIN(TestPlaylistManager)
#include "test_playlistmanager.moc"
