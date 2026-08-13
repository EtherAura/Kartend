// Multi-disc scan integration (Kartend-3mq7v): the collapse post-pass that
// turns "Recital (Disc 1).bin" + "Recital (Disc 2).bin" into ONE library item
// backed by a generated .m3u.
//
// MultiDisc's pure half (parse/group/buildM3uContents/mergeDiscs) is covered by
// tests/utils/fs/test_multidisc.cpp. What is exercised here is everything that
// only exists once a database and a filesystem are involved: the staged-row
// rewrite, the managed playlist directory, the regeneration rules when a disc
// appears or disappears, the cleanup when the per-collection setting is turned
// off, and the metadata merge write-back.
//
// Driven against a REAL on-disk SQLite database seeded with the production
// schema (the project forbids mocking the DB) — the same standalone
// ScanService wiring tests/modules/query/test_scanservice_persist.cpp uses.
#include <memory>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "databaseschema.h"
#include "itemmetadata.h"
#include "multidisc.h"
#include "preparedstatementcache.h"
#include "scanservice.h"

namespace {

// Real on-disk SQLite + the production schema wired to a standalone
// ScanService; per-instance temp dir so parallel test processes never share
// state. Mirrors PersistFixture in test_scanservice_persist.cpp.
class CollapseFixture {
public:
  CollapseFixture() {
    m_dbPath = QDir(m_tmp.path()).filePath(QStringLiteral("scan.db"));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("test_multidisccollapse"));
    m_db.setDatabaseName(m_dbPath);
    m_opened = m_db.open();
    if (m_opened) {
      DatabaseSchema::applyConnectionPragmas(m_db);
      DatabaseSchema::createTables(m_db);
      DatabaseSchema::createIndexes(m_db);
    }
    m_cache = std::make_unique<PreparedStatementCache>();
    m_cache->setDatabase(m_db);
    m_svc = std::make_unique<ScanService>(m_db, *m_cache, m_txnDepth);
  }
  ~CollapseFixture() {
    m_svc.reset();
    m_cache.reset();
    if (m_opened) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("test_multidisccollapse"));
  }
  CollapseFixture(const CollapseFixture &) = delete;
  CollapseFixture &operator=(const CollapseFixture &) = delete;

  ScanService *service() { return m_svc.get(); }
  [[nodiscard]] bool opened() const { return m_opened; }
  QSqlDatabase &db() { return m_db; }

private:
  QTemporaryDir m_tmp;
  QString m_dbPath;
  QSqlDatabase m_db;
  bool m_opened = false;
  std::unique_ptr<PreparedStatementCache> m_cache;
  int m_txnDepth = 0;
  std::unique_ptr<ScanService> m_svc;
};

bool writeFile(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  const bool ok = f.write(bytes) == bytes.size();
  f.close();
  return ok;
}

CollectionConfig makeConfig(const QString &mediaDir, bool groupMultiDisc, bool recursive = false) {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Discs");
  cfg.mediaDirectory = mediaDir;
  cfg.extensions = QStringList{QStringLiteral("bin")};
  cfg.groupMultiDisc = groupMultiDisc;
  cfg.folderBrowsing.includeContentSubfolders = recursive;
  return cfg;
}

QString uuidOf(const CollectionConfig &cfg) {
  return CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);
}

QString playlistDirOf(const CollectionConfig &cfg) {
  return MultiDisc::playlistDirFor(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), uuidOf(cfg));
}

// needsRescan() short-circuits on an unchanged directory mtime, and a test that
// adds a file within the same mtime granularity as the previous scan would flap.
// Backdating last_scanned makes every rescan in these tests unconditional.
void forceRescan(QSqlDatabase &db) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("UPDATE collections SET last_scanned = ?"));
  q.addBindValue(QStringLiteral("2000-01-01T00:00:00"));
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

// The mirror of forceRescan: last_scanned is persisted at second resolution
// while the media directory's mtime carries sub-second precision, so a
// just-completed scan can legitimately still look stale to needsRescan().
// Dating the stamp forward settles that, leaving the scan-signature comparison
// as the only thing that can still ask for a rescan.
void settleScan(QSqlDatabase &db) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("UPDATE collections SET last_scanned = ?"));
  q.addBindValue(QDateTime::currentDateTime().addSecs(3600).toString(Qt::ISODate));
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

// (name -> absolute path) for every row in items.
QHash<QString, QString> itemsByName(QSqlDatabase &db) {
  QHash<QString, QString> out;
  QSqlQuery q(db);
  if (q.exec(QStringLiteral("SELECT name, path FROM items"))) {
    while (q.next()) {
      out.insert(q.value(0).toString(), q.value(1).toString());
    }
  }
  return out;
}

QStringList m3uLines(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  const QString body = QString::fromUtf8(f.readAll());
  f.close();
  QStringList lines = body.split(QLatin1Char('\n'));
  lines.removeAll(QString());
  return lines;
}

QStringList playlistFilesIn(const QString &dir) {
  return QDir(dir).entryList(QStringList{QStringLiteral("*.m3u")}, QDir::Files, QDir::Name);
}

void seedMetadata(QSqlDatabase &db, const QString &uuid, const QString &path, const QString &notes,
                  const QString &sourceUrl, const QStringList &tags, int rating = -1) {
  ItemMetadataStore::ItemMetadata md;
  md.collectionUuid = uuid;
  md.path = path;
  md.notes = notes;
  md.sourceUrl = sourceUrl;
  md.tags = ItemMetadataStore::serializeTags(tags);
  md.rating = rating;
  const auto saved = ItemMetadataStore::save(db, md);
  QVERIFY2(saved.isOk(), qPrintable(saved.error().message));
}

} // namespace

class TestMultiDiscCollapse : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();

  void groupingOff_keepsOneItemPerDisc();
  void groupingOn_collapsesDiscsIntoOnePlaylistItem();
  void playlistBody_listsMembersInDiscOrder();
  void loneDiscFile_isNotCollapsed();
  void discAdded_regeneratesPlaylistInPlace();
  void discRemoved_belowTwo_dropsPlaylistAndRestoresFile();
  void releaseDeleted_sweepsOrphanPlaylist();
  void groupingTurnedOff_removesManagedDirAndRestoresDiscs();
  void togglingSetting_forcesRescan();
  void memberMetadata_mergesOntoCollapsedItem();
  void collapsedItemEdits_surviveRescan();
  void memberArtwork_fillsCollapsedItemOnce();
  void firstScan_collapsedItemResolvesPerDiscArtwork();
  void sameBaseInDifferentFolders_getDistinctPlaylists();
  void relativePath_placesCollapsedItemInItsDiscsFolder();
  void inMemorySavePipeline_collapsesToo();
};

void TestMultiDiscCollapse::initTestCase() {
  // Keeps the managed playlist directory (QStandardPaths::AppDataLocation)
  // inside the per-test sandbox instead of the developer's real profile.
  QStandardPaths::setTestModeEnabled(true);
}

void TestMultiDiscCollapse::groupingOff_keepsOneItemPerDisc() {
  CollapseFixture fx;
  QVERIFY2(fx.opened(), "failed to open + seed the test SQLite database");

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Recital (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Recital (Disc 2).bin")), "2"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Solo.bin")), "s"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/false);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 3);
  QVERIFY(items.contains(QStringLiteral("Recital (Disc 1)")));
  QVERIFY(items.contains(QStringLiteral("Recital (Disc 2)")));
  QVERIFY(items.contains(QStringLiteral("Solo")));
  QVERIFY2(!QDir(playlistDirOf(cfg)).exists(),
           "an opted-out collection must not get a managed playlist directory");
}

void TestMultiDiscCollapse::groupingOn_collapsesDiscsIntoOnePlaylistItem() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Recital (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Recital (Disc 2).bin")), "22"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Solo.bin")), "s"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 2);
  QVERIFY2(items.contains(QStringLiteral("Recital")),
           "the collapsed item takes the base title, disc tag stripped");
  QVERIFY(items.contains(QStringLiteral("Solo")));
  QVERIFY2(!items.contains(QStringLiteral("Recital (Disc 1)")),
           "member rows must be replaced, not kept alongside the collapsed item");

  const QString playlistPath = items.value(QStringLiteral("Recital"));
  QCOMPARE(QFileInfo(playlistPath).suffix(), QStringLiteral("m3u"));
  QVERIFY2(QFile::exists(playlistPath), "the collapsed item must point at a real playlist");
  QCOMPARE(QFileInfo(playlistPath).absolutePath(), QDir(playlistDirOf(cfg)).absolutePath());
  QVERIFY2(!playlistPath.startsWith(QDir(media.path()).absolutePath()),
           "generated playlists must never be written into the user's media folder");

  // file_size rolls the release up rather than reporting the playlist's own
  // handful of bytes.
  QSqlQuery size(fx.db());
  QVERIFY(size.exec(QStringLiteral("SELECT file_size FROM items WHERE name = 'Recital'")));
  QVERIFY(size.next());
  QCOMPARE(size.value(0).toLongLong(), 3LL);
}

void TestMultiDiscCollapse::playlistBody_listsMembersInDiscOrder() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  // Created out of order, and with a two-digit disc, so a lexicographic or
  // discovery-order playlist would be visibly wrong.
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Opus (Disc 10).bin")), "10"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Opus (Disc 2).bin")), "2"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Opus (Disc 1).bin")), "1"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QString playlistPath = itemsByName(fx.db()).value(QStringLiteral("Opus"));
  QVERIFY(!playlistPath.isEmpty());
  const QStringList lines = m3uLines(playlistPath);
  QCOMPARE(lines.size(), 3);
  // The playlist lives outside the media folder, so every member is absolute.
  QCOMPARE(QFileInfo(lines.at(0)).fileName(), QStringLiteral("Opus (Disc 1).bin"));
  QCOMPARE(QFileInfo(lines.at(1)).fileName(), QStringLiteral("Opus (Disc 2).bin"));
  QCOMPARE(QFileInfo(lines.at(2)).fileName(), QStringLiteral("Opus (Disc 10).bin"));
}

void TestMultiDiscCollapse::loneDiscFile_isNotCollapsed() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Single (Disc 1).bin")), "1"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 1);
  QVERIFY2(items.contains(QStringLiteral("Single (Disc 1)")),
           "a labelled file with no siblings keeps its own name and its own row");
  QCOMPARE(playlistFilesIn(playlistDirOf(cfg)).size(), 0);
}

void TestMultiDiscCollapse::discAdded_regeneratesPlaylistInPlace() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Suite (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Suite (Disc 2).bin")), "2"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  const QString playlistPath = itemsByName(fx.db()).value(QStringLiteral("Suite"));
  QCOMPARE(m3uLines(playlistPath).size(), 2);

  QVERIFY(writeFile(dir.filePath(QStringLiteral("Suite (Disc 3).bin")), "3"));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 1);
  QCOMPARE(items.value(QStringLiteral("Suite")), playlistPath);
  const QStringList lines = m3uLines(playlistPath);
  QCOMPARE(lines.size(), 3);
  QCOMPARE(QFileInfo(lines.at(2)).fileName(), QStringLiteral("Suite (Disc 3).bin"));
  QCOMPARE(playlistFilesIn(playlistDirOf(cfg)).size(), 1);
}

void TestMultiDiscCollapse::discRemoved_belowTwo_dropsPlaylistAndRestoresFile() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Duet (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Duet (Disc 2).bin")), "2"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QCOMPARE(itemsByName(fx.db()).size(), 1);

  // One disc left: no longer a release split across discs.
  QVERIFY(QFile::remove(dir.filePath(QStringLiteral("Duet (Disc 2).bin"))));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 1);
  QVERIFY2(items.contains(QStringLiteral("Duet (Disc 1)")),
           "the surviving disc must come back as an ordinary item");
  QVERIFY2(playlistFilesIn(playlistDirOf(cfg)).isEmpty(),
           "the stale playlist must be swept, not left pointing at one file");
}

void TestMultiDiscCollapse::releaseDeleted_sweepsOrphanPlaylist() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Gone (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Gone (Disc 2).bin")), "2"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Kept.bin")), "k"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QCOMPARE(playlistFilesIn(playlistDirOf(cfg)).size(), 1);

  QVERIFY(QFile::remove(dir.filePath(QStringLiteral("Gone (Disc 1).bin"))));
  QVERIFY(QFile::remove(dir.filePath(QStringLiteral("Gone (Disc 2).bin"))));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 1);
  QVERIFY(items.contains(QStringLiteral("Kept")));
  QVERIFY2(playlistFilesIn(playlistDirOf(cfg)).isEmpty(),
           "no release left to describe — the playlist must not survive it");
}

void TestMultiDiscCollapse::groupingTurnedOff_removesManagedDirAndRestoresDiscs() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Pair (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Pair (Disc 2).bin")), "2"));

  CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QCOMPARE(itemsByName(fx.db()).size(), 1);
  QVERIFY(QDir(playlistDirOf(cfg)).exists());

  cfg.groupMultiDisc = false;
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 2);
  QVERIFY(items.contains(QStringLiteral("Pair (Disc 1)")));
  QVERIFY(items.contains(QStringLiteral("Pair (Disc 2)")));
  QVERIFY2(!QDir(playlistDirOf(cfg)).exists(),
           "turning the feature off must leave nothing behind in Kartend's data dir");
}

void TestMultiDiscCollapse::togglingSetting_forcesRescan() {
  // The previous test relies on this: without groupMultiDisc in the collection's
  // stored scan signature, flipping the setting would change nothing until some
  // unrelated event happened to trigger a rescan.
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("A.bin")), "a"));

  CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/false);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  settleScan(fx.db());
  QVERIFY2(!fx.service()->needsRescan(0, cfg), "an unchanged collection must not rescan");

  cfg.groupMultiDisc = true;
  QVERIFY2(fx.service()->needsRescan(0, cfg),
           "enabling multi-disc grouping must invalidate the stored scan");

  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  settleScan(fx.db());
  QVERIFY(!fx.service()->needsRescan(0, cfg));

  cfg.groupMultiDisc = false;
  QVERIFY2(fx.service()->needsRescan(0, cfg),
           "disabling multi-disc grouping must invalidate the stored scan too");
}

void TestMultiDiscCollapse::memberMetadata_mergesOntoCollapsedItem() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  const QString disc1 = dir.filePath(QStringLiteral("Anthology (Disc 1).bin"));
  const QString disc2 = dir.filePath(QStringLiteral("Anthology (Disc 2).bin"));
  QVERIFY(writeFile(disc1, "1"));
  QVERIFY(writeFile(disc2, "2"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  const QString uuid = uuidOf(cfg);

  // Disc 1 carries notes + a rating; disc 2 carries a source URL disc 1 lacks
  // and a tag of its own. Collapsing must keep all of it.
  seedMetadata(fx.db(), uuid, disc1, QStringLiteral("liner notes"), QString(),
               QStringList{QStringLiteral("live")}, /*rating=*/8);
  seedMetadata(fx.db(), uuid, disc2, QString(), QStringLiteral("https://example.invalid/a"),
               QStringList{QStringLiteral("remaster")});

  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  const QString playlistPath = itemsByName(fx.db()).value(QStringLiteral("Anthology"));
  QVERIFY(!playlistPath.isEmpty());

  const auto merged = ItemMetadataStore::load(fx.db(), uuid, playlistPath);
  QVERIFY(merged.isOk());
  QCOMPARE(merged.value().notes, QStringLiteral("liner notes"));
  QCOMPARE(merged.value().rating, 8);
  QCOMPARE(merged.value().sourceUrl, QStringLiteral("https://example.invalid/a"));
  QCOMPARE(ItemMetadataStore::parseTags(merged.value().tags),
           (QStringList{QStringLiteral("live"), QStringLiteral("remaster")}));

  // The per-disc rows survive, so turning grouping back off restores what the
  // user recorded against each disc.
  const auto disc2Row = ItemMetadataStore::load(fx.db(), uuid, disc2);
  QVERIFY(disc2Row.isOk());
  QCOMPARE(disc2Row.value().sourceUrl, QStringLiteral("https://example.invalid/a"));
}

void TestMultiDiscCollapse::collapsedItemEdits_surviveRescan() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  const QString disc1 = dir.filePath(QStringLiteral("Cycle (Disc 1).bin"));
  QVERIFY(writeFile(disc1, "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Cycle (Disc 2).bin")), "2"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  const QString uuid = uuidOf(cfg);
  seedMetadata(fx.db(), uuid, disc1, QStringLiteral("from disc one"), QString(), QStringList{});

  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  const QString playlistPath = itemsByName(fx.db()).value(QStringLiteral("Cycle"));
  QVERIFY(!playlistPath.isEmpty());

  // The user rewrites the note on the collapsed item.
  seedMetadata(fx.db(), uuid, playlistPath, QStringLiteral("my own note"), QString(),
               QStringList{QStringLiteral("mine")});

  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const auto after = ItemMetadataStore::load(fx.db(), uuid, playlistPath);
  QVERIFY(after.isOk());
  QCOMPARE(after.value().notes, QStringLiteral("my own note"));
  QCOMPARE(ItemMetadataStore::parseTags(after.value().tags), (QStringList{QStringLiteral("mine")}));
}

void TestMultiDiscCollapse::memberArtwork_fillsCollapsedItemOnce() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Boxset (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Boxset (Disc 2).bin")), "2"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/false);
  const QString uuid = uuidOf(cfg);

  // Scan uncollapsed first so the per-disc rows exist, then attach art to
  // disc 2 only — disc 1 has none, so the merge has to reach past it.
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  {
    QSqlQuery art(fx.db());
    art.prepare(QStringLiteral("UPDATE items SET artwork_path = ? WHERE name = ?"));
    art.addBindValue(QStringLiteral("/art/boxset.png"));
    art.addBindValue(QStringLiteral("Boxset (Disc 2)"));
    QVERIFY(art.exec());
  }

  CollectionConfig grouped = cfg;
  grouped.groupMultiDisc = true;
  QVERIFY(fx.service()->ensureCollectionScanned(0, grouped));

  QSqlQuery q(fx.db());
  QVERIFY(q.exec(QStringLiteral("SELECT artwork_path FROM items WHERE name = 'Boxset'")));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QStringLiteral("/art/boxset.png"));
}

// Kartend-knub1. memberArtwork_fillsCollapsedItemOnce covers the library that
// was scanned per disc BEFORE grouping was enabled: the discs' rows exist and
// their resolved artwork_path merges onto the collapsed item. A collection that
// opts in before its first scan has no such rows to inherit from — and the
// artwork the tile paints is not that column anyway, but a name match against
// the artwork directory. The collapsed item is named for the release, the art
// on disk is named per disc, and the two never met.
void TestMultiDiscCollapse::firstScan_collapsedItemResolvesPerDiscArtwork() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Recital (Disc 1).bin")), "1"));
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Recital (Disc 2).bin")), "2"));
  // A fully populated art folder, filed the way a per-disc library is filed.
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Recital (Disc 1).png")), "px"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Recital (Disc 2).png")), "px"));

  CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  cfg.artworkDirectory = artwork.path();
  // Grouping is on for the FIRST scan: no per-disc item ever exists.
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 1);
  const QString playlistPath = items.value(QStringLiteral("Recital"));
  QVERIFY(!playlistPath.isEmpty());

  // What the grid does with that item: resolve a cover from the collection's
  // artwork directory by the item file's name.
  ArtworkUtils::DirectoryCache::instance().clear();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories(
      ArtworkUtils::artworkLookupDirectories(artwork.path()));
  const QString resolved =
      ArtworkUtils::findArtworkForFileCached(QFileInfo(playlistPath).fileName(), artwork.path());
  QCOMPARE(resolved, QDir(artwork.path()).filePath(QStringLiteral("Recital (Disc 1).png")));
  ArtworkUtils::DirectoryCache::instance().clear();
}

void TestMultiDiscCollapse::sameBaseInDifferentFolders_getDistinctPlaylists() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir root(media.path());
  QVERIFY(root.mkpath(QStringLiteral("left")));
  QVERIFY(root.mkpath(QStringLiteral("right")));
  for (const QString &side : {QStringLiteral("left"), QStringLiteral("right")}) {
    const QDir sub(root.filePath(side));
    QVERIFY(writeFile(sub.filePath(QStringLiteral("Twin (Disc 1).bin")), "1"));
    QVERIFY(writeFile(sub.filePath(QStringLiteral("Twin (Disc 2).bin")), "2"));
  }

  const CollectionConfig cfg =
      makeConfig(media.path(), /*groupMultiDisc=*/true, /*recursive=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QSqlQuery q(fx.db());
  QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items")));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);
  QCOMPARE(playlistFilesIn(playlistDirOf(cfg)).size(), 2);

  // Both playlists survive a rescan under the same names: the disambiguating
  // suffix is derived from the release's folder, not from walk order.
  const QStringList firstPass = playlistFilesIn(playlistDirOf(cfg));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QCOMPARE(playlistFilesIn(playlistDirOf(cfg)), firstPass);
}

void TestMultiDiscCollapse::relativePath_placesCollapsedItemInItsDiscsFolder() {
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir root(media.path());
  QVERIFY(root.mkpath(QStringLiteral("archive")));
  const QDir sub(root.filePath(QStringLiteral("archive")));
  QVERIFY(writeFile(sub.filePath(QStringLiteral("Deep (Disc 1).bin")), "1"));
  QVERIFY(writeFile(sub.filePath(QStringLiteral("Deep (Disc 2).bin")), "2"));

  const CollectionConfig cfg =
      makeConfig(media.path(), /*groupMultiDisc=*/true, /*recursive=*/true);
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QSqlQuery q(fx.db());
  QVERIFY(q.exec(QStringLiteral("SELECT rel_path FROM items WHERE name = 'Deep'")));
  QVERIFY(q.next());
  // The playlist file lives in Kartend's data dir, but the ITEM belongs in the
  // folder its discs are in — that is what folder browsing keys on.
  QCOMPARE(QFileInfo(q.value(0).toString()).path(), QStringLiteral("archive"));
  QCOMPARE(QFileInfo(q.value(0).toString()).suffix(), QStringLiteral("m3u"));
}

void TestMultiDiscCollapse::inMemorySavePipeline_collapsesToo() {
  // saveItemsToDatabase is the non-streaming half of the persist layer; it
  // stages into the same scanned_items table, so the collapse has to apply
  // there as well or the two pipelines disagree about what a library holds.
  CollapseFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Memo (Disc 1).bin")), "1"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("Memo (Disc 2).bin")), "2"));

  const CollectionConfig cfg = makeConfig(media.path(), /*groupMultiDisc=*/true);
  QHash<QString, QDateTime> timestamps;
  QString signature;
  const QStringList found = fx.service()->scanMediaDirectory(cfg, timestamps, &signature);
  QCOMPARE(found.size(), 2);
  fx.service()->saveItemsToDatabase(0, found, timestamps, cfg, signature);

  const QHash<QString, QString> items = itemsByName(fx.db());
  QCOMPARE(items.size(), 1);
  QVERIFY(items.contains(QStringLiteral("Memo")));
  QCOMPARE(m3uLines(items.value(QStringLiteral("Memo"))).size(), 2);
}

QTEST_MAIN(TestMultiDiscCollapse)
#include "test_multidisccollapse.moc"
