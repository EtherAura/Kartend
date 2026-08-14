// Scan-side artwork resolution (Kartend-guyc5): `items.artwork_path` is the
// column every DB-side artwork predicate reads — the HasArtwork /
// MissingArtwork smart-playlist rules, the `has:artwork` / `missing:artwork`
// search tokens, Collection Health's missing-artwork count, the artwork review
// queue — and no scan pipeline ever wrote it. On a freshly scanned library it
// was empty for every item while the tiles painted covers, so all four
// consumers reported artwork that plainly exists as missing.
//
// What is asserted here is the contract that fixes them: after a scan, an
// item's artwork_path is exactly what the render path
// (ArtworkUtils::findArtworkForFileCached) resolves for the same item — same
// directory cascade, same priority, same disc-marked fallback — and it is
// re-derived on every rescan rather than accumulated.
//
// Driven against a REAL on-disk SQLite database seeded with the production
// schema (the project forbids mocking the DB) and a real temporary filesystem,
// through the same standalone ScanService wiring
// tests/modules/query/test_scanservice_persist.cpp uses. BOTH persist
// pipelines are covered: the streaming one (ensureCollectionScanned ->
// scanAndSaveItemsToDatabase) and the in-memory one (saveItemsToDatabase).
#include <memory>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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
#include "preparedstatementcache.h"
#include "scanservice.h"

namespace {

// Real on-disk SQLite + the production schema wired to a standalone
// ScanService; per-instance temp dir so parallel test processes never share
// state. Mirrors PersistFixture in test_scanservice_persist.cpp.
class ArtworkScanFixture {
public:
  ArtworkScanFixture() {
    m_dbPath = QDir(m_tmp.path()).filePath(QStringLiteral("scan.db"));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("test_scanartwork"));
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
  ~ArtworkScanFixture() {
    m_svc.reset();
    m_cache.reset();
    if (m_opened) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("test_scanartwork"));
    // The DirectoryCache is a process-wide singleton and these tests hand it
    // directories that are about to be deleted; leaving listings behind would
    // leak into whichever test runs next.
    ArtworkUtils::DirectoryCache::instance().clear();
  }
  ArtworkScanFixture(const ArtworkScanFixture &) = delete;
  ArtworkScanFixture &operator=(const ArtworkScanFixture &) = delete;

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

CollectionConfig makeConfig(const QString &mediaDir, const QString &artworkDir) {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Covers");
  cfg.mediaDirectory = mediaDir;
  cfg.artworkDirectory = artworkDir;
  cfg.extensions = QStringList{QStringLiteral("bin")};
  return cfg;
}

// (items.name -> items.artwork_path) for the whole test database, which holds
// exactly one collection.
QHash<QString, QString> artworkByName(QSqlDatabase &db) {
  QHash<QString, QString> out;
  QSqlQuery q(db);
  if (q.exec(QStringLiteral("SELECT name, artwork_path FROM items"))) {
    while (q.next()) {
      out.insert(q.value(0).toString(), q.value(1).toString());
    }
  }
  return out;
}

// (items.name -> items.path), for tests that need the item's own file.
QHash<QString, QString> pathsByName(QSqlDatabase &db) {
  QHash<QString, QString> out;
  QSqlQuery q(db);
  if (q.exec(QStringLiteral("SELECT name, path FROM items"))) {
    while (q.next()) {
      out.insert(q.value(0).toString(), q.value(1).toString());
    }
  }
  return out;
}

// The collection uuid the scan stamped on its items — the key `item_artwork`
// rows are filed under. The test database holds exactly one collection.
QString scannedUuid(QSqlDatabase &db) {
  QSqlQuery q(db);
  if (q.exec(QStringLiteral("SELECT collection_uuid FROM items LIMIT 1")) && q.next()) {
    return q.value(0).toString();
  }
  return {};
}

// Hand-link @p manualPath to the item at @p itemPath, exactly as the Artwork
// links dialog and the Artwork Wizard do (Kartend-jkty9).
bool linkArtwork(QSqlDatabase &db, const QString &uuid, const QString &itemPath,
                 const QString &artworkType, const QString &manualPath) {
  QSqlQuery q(db);
  if (!q.prepare(QStringLiteral("INSERT INTO item_artwork (collection_uuid, path, artwork_type, "
                                "manual_path, updated_at) VALUES (?,?,?,?,?)"))) {
    return false;
  }
  q.addBindValue(uuid);
  q.addBindValue(itemPath);
  q.addBindValue(artworkType);
  q.addBindValue(manualPath);
  q.addBindValue(QStringLiteral("2026-08-13T00:00:00Z"));
  return q.exec();
}

// needsRescan() short-circuits on an unchanged directory mtime, and artwork
// appearing or vanishing never touches the MEDIA directory — so every rescan
// that only changes the artwork folder has to be forced.
void forceRescan(QSqlDatabase &db) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("UPDATE collections SET last_scanned = ?"));
  q.addBindValue(QStringLiteral("2000-01-01T00:00:00"));
  QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

// What the grid does with an item: resolve a cover by the item file's name
// against the collection's artwork directory, from a freshly warmed cache.
QString renderTimeArtwork(const QString &itemPath, const QString &artworkDir) {
  ArtworkUtils::DirectoryCache::instance().clear();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories(
      ArtworkUtils::artworkLookupDirectories(artworkDir));
  return ArtworkUtils::findArtworkForFileCached(QFileInfo(itemPath).fileName(), artworkDir);
}

} // namespace

class TestScanArtwork : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();

  void flatCover_landsInArtworkPath();
  void artlessItem_leavesArtworkPathEmpty();
  void fullNameCover_landsInArtworkPath();
  void typedCoverSubdir_landsInArtworkPath();
  void flatRootOutranksTypedCoverSubdir();
  void noArtworkDirectory_leavesEveryArtworkPathEmpty();
  void coverAddedBetweenScans_appearsAfterRescan();
  void coverRemovedBetweenScans_clearsAfterRescan();
  void storedPathMatchesRenderTimeResolution();
  void collapsedMultiDiscItem_storesPerDiscCover();
  void exactCoverOutranksDiscFallback();
  void inMemorySavePipeline_populatesArtworkPath();

  // Manual per-item links (Kartend-jkty9)
  void manualLink_makesAnArtlessItemCount();
  void manualLink_outranksAutoDiscoveredCover();
  void staleManualLink_doesNotCount();
  void staleManualLink_fallsBackToAutoDiscoveredCover();
  void manualLink_countsWithoutAnArtworkDirectory();
  void galleryOnlyTypeLink_doesNotCount();
  void frontLinkOutranksBoxLink();
};

void TestScanArtwork::initTestCase() {
  // Keeps the multi-disc managed playlist directory
  // (QStandardPaths::AppDataLocation) inside the per-test sandbox instead of
  // the developer's real profile.
  QStandardPaths::setTestModeEnabled(true);
}

// The headline regression: a cover sitting next to the library, named for the
// item, must be recorded on the item's row.
void TestScanArtwork::flatCover_landsInArtworkPath() {
  ArtworkScanFixture fx;
  QVERIFY2(fx.opened(), "failed to open + seed the test SQLite database");

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Overture.bin")), "m"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Overture.png")), "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Overture")),
           QDir(artwork.path()).filePath(QStringLiteral("Overture.png")));
}

// The other half of the predicate: an item with no cover must stay empty, or
// `missing:artwork` would report nothing at all.
void TestScanArtwork::artlessItem_leavesArtworkPathEmpty() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Arted.bin")), "m"));
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Bare.bin")), "m"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Arted.jpg")), "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> art = artworkByName(fx.db());
  QCOMPARE(art.size(), 2);
  QCOMPARE(art.value(QStringLiteral("Arted")),
           QDir(artwork.path()).filePath(QStringLiteral("Arted.jpg")));
  QVERIFY(art.value(QStringLiteral("Bare")).isEmpty());
}

// The second name key the lookup cascade probes: art named after the item's
// FULL file name, extension included.
void TestScanArtwork::fullNameCover_landsInArtworkPath() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Etude.bin")), "m"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Etude.bin.png")), "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Etude")),
           QDir(artwork.path()).filePath(QStringLiteral("Etude.bin.png")));
}

// Scrapes write covers into typed subdirectories rather than the flat root, so
// a stored path that only understood the flat root would report a scraped
// library as artless.
void TestScanArtwork::typedCoverSubdir_landsInArtworkPath() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Nocturne.bin")), "m"));
  QVERIFY(QDir(artwork.path()).mkpath(QStringLiteral("front")));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("front/Nocturne.png")), "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Nocturne")),
           QDir(artwork.path()).filePath(QStringLiteral("front/Nocturne.png")));
}

// Cascade priority has to match the render path's, or an item would be filed
// against one cover and painted with another.
void TestScanArtwork::flatRootOutranksTypedCoverSubdir() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Prelude.bin")), "m"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Prelude.png")), "flat"));
  QVERIFY(QDir(artwork.path()).mkpath(QStringLiteral("front")));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("front/Prelude.png")), "sub"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QString stored = artworkByName(fx.db()).value(QStringLiteral("Prelude"));
  QCOMPARE(stored, QDir(artwork.path()).filePath(QStringLiteral("Prelude.png")));
  const QHash<QString, QString> paths = pathsByName(fx.db());
  QCOMPARE(stored, renderTimeArtwork(paths.value(QStringLiteral("Prelude")), artwork.path()));
}

// A collection with no artwork directory configured must not be charged for
// the resolution pass at all — and must not invent a cover.
void TestScanArtwork::noArtworkDirectory_leavesEveryArtworkPathEmpty() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Solo.bin")), "m"));

  const CollectionConfig cfg = makeConfig(media.path(), QString());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QVERIFY(artworkByName(fx.db()).value(QStringLiteral("Solo")).isEmpty());
}

// Art arriving after the library was scanned. The media directory is untouched,
// so this is exactly the case a cached artwork listing would get wrong.
void TestScanArtwork::coverAddedBetweenScans_appearsAfterRescan() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Rhapsody.bin")), "m"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QVERIFY(artworkByName(fx.db()).value(QStringLiteral("Rhapsody")).isEmpty());

  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Rhapsody.png")), "px"));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Rhapsody")),
           QDir(artwork.path()).filePath(QStringLiteral("Rhapsody.png")));
}

// The symmetric case: the column is a cache of a filesystem answer, so a cover
// the user deleted must stop counting as artwork rather than linger as a path
// to a file that no longer exists.
void TestScanArtwork::coverRemovedBetweenScans_clearsAfterRescan() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Fugue.bin")), "m"));
  const QString cover = QDir(artwork.path()).filePath(QStringLiteral("Fugue.png"));
  QVERIFY(writeFile(cover, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Fugue")), cover);

  QVERIFY(QFile::remove(cover));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QVERIFY(artworkByName(fx.db()).value(QStringLiteral("Fugue")).isEmpty());
}

// The invariant the whole change exists to establish: what the database says
// about an item's artwork is what the grid resolves for it. A divergence here
// is the bug returning in a different shape.
void TestScanArtwork::storedPathMatchesRenderTimeResolution() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  const QDir mediaDir(media.path());
  const QDir artDir(artwork.path());
  QVERIFY(artDir.mkpath(QStringLiteral("front")));
  QVERIFY(artDir.mkpath(QStringLiteral("box")));
  // One item per shape the cascade can answer in, plus one it cannot.
  QVERIFY(writeFile(mediaDir.filePath(QStringLiteral("Flat.bin")), "m"));
  QVERIFY(writeFile(mediaDir.filePath(QStringLiteral("Front.bin")), "m"));
  QVERIFY(writeFile(mediaDir.filePath(QStringLiteral("Boxed.bin")), "m"));
  QVERIFY(writeFile(mediaDir.filePath(QStringLiteral("Named.bin")), "m"));
  QVERIFY(writeFile(mediaDir.filePath(QStringLiteral("Bare.bin")), "m"));
  QVERIFY(writeFile(artDir.filePath(QStringLiteral("Flat.png")), "px"));
  QVERIFY(writeFile(artDir.filePath(QStringLiteral("front/Front.jpg")), "px"));
  QVERIFY(writeFile(artDir.filePath(QStringLiteral("box/Boxed.png")), "px"));
  QVERIFY(writeFile(artDir.filePath(QStringLiteral("Named.bin.png")), "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> stored = artworkByName(fx.db());
  const QHash<QString, QString> paths = pathsByName(fx.db());
  QCOMPARE(stored.size(), 5);
  for (auto it = paths.constBegin(); it != paths.constEnd(); ++it) {
    const QString expected = renderTimeArtwork(it.value(), artwork.path());
    QCOMPARE(stored.value(it.key()), expected);
  }
  QVERIFY(stored.value(QStringLiteral("Bare")).isEmpty());
}

// Kartend-knub1 consistency: a multi-disc release collapses into ONE item named
// for the release, while the art on disk is still filed per disc. The render
// path answers that through the disc-marked fallback, so the stored column has
// to as well — otherwise the collapsed item paints a cover and reports itself
// as missing one.
void TestScanArtwork::collapsedMultiDiscItem_storesPerDiscCover() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Recital (Disc 1).bin")), "1"));
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Recital (Disc 2).bin")), "2"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Recital (Disc 1).png")), "px"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Recital (Disc 2).png")), "px"));

  CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  cfg.groupMultiDisc = true;
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  const QHash<QString, QString> stored = artworkByName(fx.db());
  QCOMPARE(stored.size(), 1);
  // The lowest disc order wins, same rule the per-item fallback follows.
  QCOMPARE(stored.value(QStringLiteral("Recital")),
           QDir(artwork.path()).filePath(QStringLiteral("Recital (Disc 1).png")));
}

// The disc fallback is a fallback: art named for the item itself outranks art
// named for one of its discs, and the stored column must not reorder that.
void TestScanArtwork::exactCoverOutranksDiscFallback() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Suite (Disc 1).bin")), "1"));
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Suite (Disc 2).bin")), "2"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Suite (Disc 1).png")), "px"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Suite.png")), "px"));

  CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  cfg.groupMultiDisc = true;
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Suite")),
           QDir(artwork.path()).filePath(QStringLiteral("Suite.png")));
}

// The non-streaming persist pipeline stages into the same table and applies
// from it, so it has to carry the column too — a library saved through this
// path would otherwise keep the old, always-empty behaviour.
void TestScanArtwork::inMemorySavePipeline_populatesArtworkPath() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Cantata.bin")), "m"));
  QVERIFY(writeFile(QDir(artwork.path()).filePath(QStringLiteral("Cantata.png")), "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QHash<QString, QDateTime> timestamps;
  QString signature;
  const QStringList found = fx.service()->scanMediaDirectory(cfg, timestamps, &signature);
  QCOMPARE(found.size(), 1);
  fx.service()->saveItemsToDatabase(/*collectionIndex=*/0, found, timestamps, cfg, signature);

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Cantata")),
           QDir(artwork.path()).filePath(QStringLiteral("Cantata.png")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Manual per-item links (Kartend-jkty9)
//
// Auto-discovery is only one of the two ways an item gets a cover; the other is
// a link the user made by hand, stored in `item_artwork`. Nothing joined the
// two, so hand-linking a cover — including through the Artwork Wizard, whose
// own queue is built from this column — left the item reported as missing
// artwork everywhere.
// ─────────────────────────────────────────────────────────────────────────────

// The headline case, and the wizard's own loop: an item no auto-discovered
// cover answers for, given one by hand, must stop counting as missing.
void TestScanArtwork::manualLink_makesAnArtlessItemCount() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Unnamed.bin")), "m"));
  // Deliberately named nothing like the item — auto-discovery cannot find it.
  const QString picked = QDir(artwork.path()).filePath(QStringLiteral("scan0042.png"));
  QVERIFY(writeFile(picked, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QVERIFY(artworkByName(fx.db()).value(QStringLiteral("Unnamed")).isEmpty());

  QVERIFY(linkArtwork(fx.db(), scannedUuid(fx.db()),
                      pathsByName(fx.db()).value(QStringLiteral("Unnamed")),
                      QStringLiteral("front"), picked));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Unnamed")), picked);
}

// The docs promise a manual link overrides auto-discovery, and the details pane
// already renders it that way — so the column has to record the link, not the
// name match it displaces.
void TestScanArtwork::manualLink_outranksAutoDiscoveredCover() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Overture.bin")), "m"));
  const QString autoCover = QDir(artwork.path()).filePath(QStringLiteral("Overture.png"));
  const QString chosen = QDir(artwork.path()).filePath(QStringLiteral("better-scan.png"));
  QVERIFY(writeFile(autoCover, "px"));
  QVERIFY(writeFile(chosen, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Overture")), autoCover);

  QVERIFY(linkArtwork(fx.db(), scannedUuid(fx.db()),
                      pathsByName(fx.db()).value(QStringLiteral("Overture")),
                      QStringLiteral("front"), chosen));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Overture")), chosen);
}

// The reason the existence check lives in the scan rather than in a JOIN: a
// link points at an arbitrary path the user chose and can go stale. Counting
// the row's mere existence would report a cover for a file that is gone, which
// is precisely the DB-vs-render disagreement Kartend-guyc5 removed.
void TestScanArtwork::staleManualLink_doesNotCount() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Ghost.bin")), "m"));
  const QString vanished = QDir(artwork.path()).filePath(QStringLiteral("deleted-by-user.png"));
  QVERIFY(writeFile(vanished, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QVERIFY(linkArtwork(fx.db(), scannedUuid(fx.db()),
                      pathsByName(fx.db()).value(QStringLiteral("Ghost")), QStringLiteral("front"),
                      vanished));
  QVERIFY(QFile::remove(vanished));

  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QVERIFY(artworkByName(fx.db()).value(QStringLiteral("Ghost")).isEmpty());
}

// A stale link must not swallow the auto-discovered cover either — the item
// still paints that cover, so the column has to keep reporting it.
void TestScanArtwork::staleManualLink_fallsBackToAutoDiscoveredCover() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Etude.bin")), "m"));
  const QString autoCover = QDir(artwork.path()).filePath(QStringLiteral("Etude.png"));
  const QString vanished = QDir(artwork.path()).filePath(QStringLiteral("gone.png"));
  QVERIFY(writeFile(autoCover, "px"));
  QVERIFY(writeFile(vanished, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QVERIFY(linkArtwork(fx.db(), scannedUuid(fx.db()),
                      pathsByName(fx.db()).value(QStringLiteral("Etude")), QStringLiteral("front"),
                      vanished));
  QVERIFY(QFile::remove(vanished));

  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Etude")), autoCover);
}

// A link is an absolute path of the user's choosing, so it does not depend on
// the collection having an artwork directory at all. The pass used to bail out
// before reading anything when that setting was empty.
void TestScanArtwork::manualLink_countsWithoutAnArtworkDirectory() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir elsewhere;
  QVERIFY(media.isValid());
  QVERIFY(elsewhere.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Solo.bin")), "m"));
  const QString picked = QDir(elsewhere.path()).filePath(QStringLiteral("cover.png"));
  QVERIFY(writeFile(picked, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), QString());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QVERIFY(artworkByName(fx.db()).value(QStringLiteral("Solo")).isEmpty());

  QVERIFY(linkArtwork(fx.db(), scannedUuid(fx.db()),
                      pathsByName(fx.db()).value(QStringLiteral("Solo")), QStringLiteral("front"),
                      picked));
  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Solo")), picked);
}

// "Has artwork" means "has a COVER". `logo` is not in the cover cascade — it is
// never auto-discovered as one — so hand-linking one must not make an
// otherwise-artless item disappear from the missing-artwork worklist.
void TestScanArtwork::galleryOnlyTypeLink_doesNotCount() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Badged.bin")), "m"));
  const QString logo = QDir(artwork.path()).filePath(QStringLiteral("badge.png"));
  QVERIFY(writeFile(logo, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  QVERIFY(linkArtwork(fx.db(), scannedUuid(fx.db()),
                      pathsByName(fx.db()).value(QStringLiteral("Badged")), QStringLiteral("logo"),
                      logo));

  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QVERIFY(artworkByName(fx.db()).value(QStringLiteral("Badged")).isEmpty());
}

// Two hand-linked cover types on one item resolve in the same priority order
// auto-discovery walks the typed subdirs in, so the column agrees with the
// gallery's idea of which slot is the primary cover.
void TestScanArtwork::frontLinkOutranksBoxLink() {
  ArtworkScanFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QTemporaryDir artwork;
  QVERIFY(media.isValid());
  QVERIFY(artwork.isValid());
  QVERIFY(writeFile(QDir(media.path()).filePath(QStringLiteral("Twin.bin")), "m"));
  const QString frontArt = QDir(artwork.path()).filePath(QStringLiteral("picked-front.png"));
  const QString boxArt = QDir(artwork.path()).filePath(QStringLiteral("picked-box.png"));
  QVERIFY(writeFile(frontArt, "px"));
  QVERIFY(writeFile(boxArt, "px"));

  const CollectionConfig cfg = makeConfig(media.path(), artwork.path());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));
  const QString uuid = scannedUuid(fx.db());
  const QString itemPath = pathsByName(fx.db()).value(QStringLiteral("Twin"));
  // Inserted box-first so a resolver that just took the first row it read
  // would answer with the wrong one.
  QVERIFY(linkArtwork(fx.db(), uuid, itemPath, QStringLiteral("box"), boxArt));
  QVERIFY(linkArtwork(fx.db(), uuid, itemPath, QStringLiteral("front"), frontArt));

  forceRescan(fx.db());
  QVERIFY(fx.service()->ensureCollectionScanned(0, cfg));

  QCOMPARE(artworkByName(fx.db()).value(QStringLiteral("Twin")), frontArt);
}

QTEST_MAIN(TestScanArtwork)
#include "test_scanartwork.moc"
