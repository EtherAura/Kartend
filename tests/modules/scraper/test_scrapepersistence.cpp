// Tests for Scraper::applyScrapedItem. File-IO assertions use a
// QTemporaryDir; DB-side assertions use a tiny inline subclass of
// MockDatabaseManager that captures saveItemMetadata + saveItemArtwork
// calls without needing a real SQLite backend.
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "../../integration/mocks/mockdatabasemanager.h"
#include "scrapepersistence.h"
#include "scrapertypes.h"

namespace {

/// MockDatabaseManager subclass that captures the metadata + artwork
/// rows the SUT tries to save, plus replays loadItemMetadata so the
/// merge-with-existing path can be exercised.
class CapturingDb : public KartendTest::MockDatabaseManager {
public:
  bool saveItemMetadata(const ItemMetadataStore::ItemMetadata &m) override {
    metadataSaves.append(m);
    return true;
  }
  bool saveItemArtwork(const ItemArtworkStore::ItemArtwork &a) override {
    artworkSaves.append(a);
    return true;
  }
  [[nodiscard]] ItemMetadataStore::ItemMetadata
  loadItemMetadata(const QString &uuid, const QString &path) const override {
    if (preloadedMetadata.collectionUuid == uuid && preloadedMetadata.path == path) {
      return preloadedMetadata;
    }
    return KartendTest::MockDatabaseManager::loadItemMetadata(uuid, path);
  }

  ItemMetadataStore::ItemMetadata preloadedMetadata;
  QList<ItemMetadataStore::ItemMetadata> metadataSaves;
  QList<ItemArtworkStore::ItemArtwork> artworkSaves;
};

Scraper::PendingMediaWrite makeMedia(const QString &type, const QString &label,
                                     const QByteArray &bytes) {
  Scraper::PendingMediaWrite w;
  w.asset.type = type;
  w.asset.label = label;
  w.asset.url = QUrl(QStringLiteral("https://example.com/%1.png").arg(type));
  w.bytes = bytes;
  return w;
}

} // namespace

class TestScrapePersistence : public QObject {
  Q_OBJECT
private slots:
  void front_writesCoverIntoTypedSubdirNotFlatRoot();
  void standardType_writesIntoPerTypeSubdir();
  void nonStandardType_writesPerTypeAndSavesItemArtworkRow();
  void emptyBytes_skipsWithFailureCounter();
  void metadataSave_overwritesScrapedFieldsButPreservesUserOnly();
  void metadataSave_mergesCustomFieldsWithExisting();
  void noDatabaseManager_stillWritesFilesAndSkipsMetadata();
  void videoAsset_routesToVideoDirectoryWithUrlExtension();
  void manualAsset_routesToManualDirectoryWithUrlExtension();
  void videoAsset_doesNotCreateItemArtworkRow();
  void videoAsset_defaultsExtensionToMp4WhenUrlHasNoSuffix();
  void screenshot_writesIntoTypedSubdirNotFlatRoot();
  void logo_writesIntoTypedSubdirNotFlatRoot();
  void multipleCovers_eachWriteIntoOwnTypedSubdir();
  void metadataSidecar_writesJsonWithScrapedFields();
  void metadataSidecar_skippedWhenTitleEmpty();
  void metadataSidecar_fillMissingKeepsExisting();
  void unsafeTypeSubdir_skippedNotWritten();
  void unsafeSharedScopeKey_skippedNotWritten();
};

void TestScrapePersistence::front_writesCoverIntoTypedSubdirNotFlatRoot() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const auto bytes = QByteArray("PNG_FRONT_BYTES");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("front"), QStringLiteral("Front cover"), bytes)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(),
                                                QStringLiteral("song"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QCOMPARE(result.mediaSkipped, 0);
  // The cover lands once, under the "front" typed subdir — the
  // cross-provider primary-cover id, a standard gallery type.
  QFile typed(tmp.path() + "/front/song.png");
  QVERIFY(typed.exists());
  QVERIFY(typed.open(QIODevice::ReadOnly));
  QCOMPARE(typed.readAll(), bytes);
  // No redundant copy at the flat artwork root — the grid tile
  // resolves the cover straight from the typed subdir.
  QVERIFY(!QFile::exists(tmp.path() + "/song.png"));
  // No item_artwork row needed — "front" is a standard type which
  // the gallery's standard-type iteration auto-discovers.
  QCOMPARE(db.artworkSaves.size(), 0);
}

void TestScrapePersistence::standardType_writesIntoPerTypeSubdir() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("box"), QStringLiteral("Box art"), QByteArray("BOX_BYTES")),
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"),
                QByteArray("SCREENSHOT_BYTES"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/g.bin", tmp.path(),
                                                QStringLiteral("g"), item, media);

  QCOMPARE(result.mediaWritten, 2);
  QVERIFY(QFileInfo(tmp.path() + "/box/g.png").exists());
  QVERIFY(QFileInfo(tmp.path() + "/screenshot/g.png").exists());
  // Standard types are auto-discovered via subdirectory — no
  // item_artwork row needed.
  QCOMPARE(db.artworkSaves.size(), 0);
  // No redundant copy at the flat artwork root — the grid resolver
  // walks the typed subdirs for the cover.
  QVERIFY(!QFile::exists(tmp.path() + "/g.png"));
}

void TestScrapePersistence::nonStandardType_writesPerTypeAndSavesItemArtworkRow() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  // "back" is a custom (non-standard) type — needs an item_artwork
  // row pointing at the file or the sidebar gallery skips it.
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("back"), QStringLiteral("Back cover"), QByteArray("BACK_BYTES"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(),
                                                QStringLiteral("song"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  const QString expectedPath = tmp.path() + "/back/song.png";
  QVERIFY(QFileInfo(expectedPath).exists());
  QCOMPARE(db.artworkSaves.size(), 1);
  QCOMPARE(db.artworkSaves.first().artworkType, QStringLiteral("back"));
  QCOMPARE(db.artworkSaves.first().manualPath, expectedPath);
  QCOMPARE(db.artworkSaves.first().collectionUuid, QStringLiteral("u1"));
  QCOMPARE(db.artworkSaves.first().path, QStringLiteral("/m/song.flac"));
}

void TestScrapePersistence::emptyBytes_skipsWithFailureCounter() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("front"), QStringLiteral("Front"), QByteArray()),
      makeMedia(QStringLiteral(""), QStringLiteral("Untyped"), QByteArray("X"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/x.flac", tmp.path(),
                                                QStringLiteral("x"), item, media);

  QCOMPARE(result.mediaWritten, 0);
  QCOMPARE(result.mediaSkipped, 2);
  QVERIFY(!result.firstFailures.isEmpty());
}

void TestScrapePersistence::metadataSave_overwritesScrapedFieldsButPreservesUserOnly() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  // Pre-existing user-entered metadata: a custom title + rating-shaped
  // contentRating that the scrape doesn't touch. The scrape returns
  // a publisher + releaseDate but no contentRating.
  db.preloadedMetadata.collectionUuid = QStringLiteral("u1");
  db.preloadedMetadata.path = QStringLiteral("/m/song.flac");
  db.preloadedMetadata.title = QStringLiteral("User-set title");
  db.preloadedMetadata.contentRating = QStringLiteral("PG");

  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("musicbrainz");
  item.title = QStringLiteral("Scraped title");
  item.publisher = QStringLiteral("Some Label");
  item.releaseDate = QStringLiteral("2020-05-01");
  // contentRating intentionally empty — preserve user value.

  Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(), QStringLiteral("song"), item,
                            {});

  QCOMPARE(db.metadataSaves.size(), 1);
  const auto saved = db.metadataSaves.first();
  // Scrape values win when present.
  QCOMPARE(saved.title, QStringLiteral("Scraped title"));
  QCOMPARE(saved.publisher, QStringLiteral("Some Label"));
  QCOMPARE(saved.releaseDate, QStringLiteral("2020-05-01"));
  // User value preserved for fields the scrape didn't fill.
  QCOMPARE(saved.contentRating, QStringLiteral("PG"));
  QCOMPARE(saved.source, QStringLiteral("musicbrainz"));
}

void TestScrapePersistence::metadataSave_mergesCustomFieldsWithExisting() {
  QTemporaryDir tmp;
  CapturingDb db;
  db.preloadedMetadata.collectionUuid = QStringLiteral("u1");
  db.preloadedMetadata.path = QStringLiteral("/m/song.flac");
  // User-entered customFields the scrape must NOT clobber.
  db.preloadedMetadata.customFields =
      QStringLiteral(R"({"my_rating":"5","catalogue_no":"ABC123"})");

  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("musicbrainz");
  item.customFields.insert(QStringLiteral("musicbrainz_id"), QStringLiteral("mbid-1"));
  // Same key as user-entered — scrape wins on shared keys (the user
  // explicitly picked this scrape).
  item.customFields.insert(QStringLiteral("catalogue_no"), QStringLiteral("XYZ999"));

  Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(), QStringLiteral("song"), item,
                            {});

  QCOMPARE(db.metadataSaves.size(), 1);
  const QString mergedJson = db.metadataSaves.first().customFields;
  QVERIFY(mergedJson.contains(QStringLiteral("\"my_rating\":\"5\"")));
  QVERIFY(mergedJson.contains(QStringLiteral("\"musicbrainz_id\":\"mbid-1\"")));
  QVERIFY(mergedJson.contains(QStringLiteral("\"catalogue_no\":\"XYZ999\"")));
  QVERIFY(!mergedJson.contains(QStringLiteral("\"catalogue_no\":\"ABC123\"")));
}

void TestScrapePersistence::noDatabaseManager_stillWritesFilesAndSkipsMetadata() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("front"), QStringLiteral("Front"), QByteArray("F"))};

  const auto result = Scraper::applyScrapedItem(nullptr, "u1", "/m/x.flac", tmp.path(),
                                                QStringLiteral("x"), item, media);

  // Files written, but metadata save flagged false (no DB to call).
  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(!result.metadataSaved);
  // The front cover lands in its typed subdir, not the flat root.
  QVERIFY(QFileInfo(tmp.path() + "/front/x.png").exists());
}

namespace {

/// makeMedia variant that lets the test set the asset URL explicitly
/// so we can exercise extension-from-URL inference (.mp4, .pdf, etc.)
/// without piggybacking on the default `.png` URL.
Scraper::PendingMediaWrite makeMediaWithUrl(const QString &type, const QString &label,
                                            const QUrl &url, const QByteArray &bytes) {
  Scraper::PendingMediaWrite w;
  w.asset.type = type;
  w.asset.label = label;
  w.asset.url = url;
  w.bytes = bytes;
  return w;
}

} // namespace

void TestScrapePersistence::videoAsset_routesToVideoDirectoryWithUrlExtension() {
  // "video" assets land in the collection's artworkDirectory/video/
  // subdir (auto-created) with the URL's own extension preserved.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const auto bytes = QByteArray("VIDEO_BYTES");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMediaWithUrl(QStringLiteral("video"), QStringLiteral("Gameplay video"),
                       QUrl(QStringLiteral("https://example.com/clips/foo.mp4")), bytes)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QCOMPARE(result.mediaSkipped, 0);
  QVERIFY(QFile::exists(tmp.path() + "/video/foo.mp4"));
  // The pre-routing layout used to write under artwork/video/foo.png
  // — confirm that orphan isn't created any more.
  QVERIFY(!QFile::exists(tmp.path() + "/video/foo.png"));
}

void TestScrapePersistence::manualAsset_routesToManualDirectoryWithUrlExtension() {
  // "manual" assets land in artworkDirectory/manual/ — same single-
  // root model as video; subdir is created on demand.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const QList<Scraper::PendingMediaWrite> media = {makeMediaWithUrl(
      QStringLiteral("manual"), QStringLiteral("Game manual"),
      QUrl(QStringLiteral("https://example.com/manuals/foo.pdf")), QByteArray("PDF"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/manual/foo.pdf"));
  // Manuals aren't artwork — no item_artwork row should be written.
  QCOMPARE(db.artworkSaves.size(), 0);
}

void TestScrapePersistence::videoAsset_doesNotCreateItemArtworkRow() {
  // Sanity: even though "video" isn't a standard artwork type, the
  // persistence layer must NOT fall through to the "save item_artwork
  // row for non-standard" branch — that branch is image-only.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const QList<Scraper::PendingMediaWrite> media = {
      makeMediaWithUrl(QStringLiteral("video"), QStringLiteral("V"),
                       QUrl(QStringLiteral("https://example.com/foo.webm")), QByteArray("V"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QCOMPARE(db.artworkSaves.size(), 0);
}

void TestScrapePersistence::videoAsset_defaultsExtensionToMp4WhenUrlHasNoSuffix() {
  // ScreenScraper returns some video URLs as `videos/getVideo.php?id=...`
  // — no recognisable suffix. The kind-default extension should kick
  // in so we still produce a `.mp4` file rather than something with no
  // extension or a `.php`-named file.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const QList<Scraper::PendingMediaWrite> media = {makeMediaWithUrl(
      QStringLiteral("video"), QStringLiteral("V"),
      QUrl(QStringLiteral("https://api.screenscraper.fr/api2/media.php?id=1")), QByteArray("V"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/video/foo.mp4"));
  // The bogus ".php" extension from the URL must NOT come through.
  QVERIFY(!QFile::exists(tmp.path() + "/video/foo.php"));
}

void TestScrapePersistence::screenshot_writesIntoTypedSubdirNotFlatRoot() {
  // SS regularly returns games without a `front` (box-2D) asset —
  // only screenshot / fanart / marquee / etc. Whatever the type, the
  // asset lands in its typed subdir and nothing is copied to the
  // flat artwork root; the grid resolver walks the typed subdirs.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const auto shotBytes = QByteArray("SHOT_BYTES");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"), shotBytes)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QFile typed(tmp.path() + "/screenshot/foo.png");
  QVERIFY(typed.exists());
  QVERIFY(typed.open(QIODevice::ReadOnly));
  QCOMPARE(typed.readAll(), shotBytes);
  // No redundant flat-root copy.
  QVERIFY(!QFile::exists(tmp.path() + "/foo.png"));
}

void TestScrapePersistence::logo_writesIntoTypedSubdirNotFlatRoot() {
  // Any image type writes to its own typed subdir; none is copied to
  // the flat artwork root.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("logo"), QStringLiteral("Logo"), QByteArray("LOGO"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/logo/foo.png"));
  QVERIFY(!QFile::exists(tmp.path() + "/foo.png"));
}

void TestScrapePersistence::multipleCovers_eachWriteIntoOwnTypedSubdir() {
  // Several cover-like assets each land in their own typed subdir.
  // The persistence layer no longer ranks them or mirrors a "winner"
  // to the flat root — cover priority is the resolver's job now.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const auto boxBytes = QByteArray("BOX3D_BYTES");
  const auto shotBytes = QByteArray("SHOT_BYTES");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"), shotBytes),
      makeMedia(QStringLiteral("box-3d"), QStringLiteral("Box (3D)"), boxBytes)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 2);
  QVERIFY(QFile::exists(tmp.path() + "/screenshot/foo.png"));
  QVERIFY(QFile::exists(tmp.path() + "/box-3d/foo.png"));
  // Nothing at the flat root.
  QVERIFY(!QFile::exists(tmp.path() + "/foo.png"));
}

void TestScrapePersistence::metadataSidecar_writesJsonWithScrapedFields() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Downtown Nekketsu Monogatari");
  item.developer = QStringLiteral("Technos");
  item.genre = QStringLiteral("Beat 'em up");
  item.sourceProviderId = QStringLiteral("screenscraper");
  item.customFields.insert(QStringLiteral("region"), QStringLiteral("jp"));

  QVERIFY(Scraper::writeMetadataSidecar(tmp.path(), QStringLiteral("game"), item,
                                        Scraper::RescrapeMode::Overwrite));

  // The sidecar lands under the metadata/ subdir.
  QFile f(tmp.path() + "/metadata/game.json");
  QVERIFY(f.exists());
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
  QCOMPARE(obj.value("title").toString(), item.title);
  QCOMPARE(obj.value("developer").toString(), item.developer);
  QCOMPARE(obj.value("genre").toString(), item.genre);
  QCOMPARE(obj.value("source").toString(), QStringLiteral("screenscraper"));
  QCOMPARE(obj.value("customFields").toObject().value("region").toString(), QStringLiteral("jp"));
  // Empty fields are omitted, not written as empty strings.
  QVERIFY(!obj.contains("description"));
}

void TestScrapePersistence::metadataSidecar_skippedWhenTitleEmpty() {
  // A title-less item (e.g. the user opted out of metadata, so the
  // batch runner stripped every text field) writes no sidecar.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  Scraper::ScrapedItem item;
  item.description = QStringLiteral("orphan description");

  QVERIFY(!Scraper::writeMetadataSidecar(tmp.path(), QStringLiteral("game"), item,
                                         Scraper::RescrapeMode::Overwrite));
  QVERIFY(!QFile::exists(tmp.path() + "/metadata/game.json"));
}

void TestScrapePersistence::metadataSidecar_fillMissingKeepsExisting() {
  // FillMissing leaves an existing sidecar untouched.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir().mkpath(tmp.path() + "/metadata"));
  const QByteArray original("{\"title\":\"user-edited\"}");
  {
    QFile f(tmp.path() + "/metadata/game.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(original);
  }
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Fresh Scrape Title");

  QVERIFY(!Scraper::writeMetadataSidecar(tmp.path(), QStringLiteral("game"), item,
                                         Scraper::RescrapeMode::FillMissing));
  QFile f(tmp.path() + "/metadata/game.json");
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), original);
}

void TestScrapePersistence::unsafeTypeSubdir_skippedNotWritten() {
  // A remote-controlled asset type that would traverse out of artworkDirectory
  // as a subdirectory must be refused at the write seam (Kartend-xncf).
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("../evil"), QStringLiteral("Evil"), QByteArray("X"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/g.bin", tmp.path(),
                                                QStringLiteral("g"), item, media);

  QCOMPARE(result.mediaWritten, 0);
  QCOMPARE(result.mediaSkipped, 1);
  QVERIFY(!result.firstFailures.isEmpty());
  QVERIFY2(result.firstFailures.first().contains(QStringLiteral("unsafe media subdirectory")),
           qPrintable(result.firstFailures.first()));
}

void TestScrapePersistence::unsafeSharedScopeKey_skippedNotWritten() {
  // A group-scoped asset whose scopeKey would traverse out of _shared/ as a
  // filename must be refused at the write seam (Kartend-xhbt). The type is
  // safe, so only the scopeKey is the offending field.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  Scraper::PendingMediaWrite w =
      makeMedia(QStringLiteral("fanart"), QStringLiteral("Fanart"), QByteArray("X"));
  w.asset.scope = Scraper::MediaScope::Group;
  w.asset.scopeKey = QStringLiteral("../../evil");
  const QList<Scraper::PendingMediaWrite> media = {w};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/g.bin", tmp.path(),
                                                QStringLiteral("g"), item, media);

  QCOMPARE(result.mediaWritten, 0);
  QCOMPARE(result.mediaSkipped, 1);
  QVERIFY(!result.firstFailures.isEmpty());
  QVERIFY2(result.firstFailures.first().contains(QStringLiteral("unsafe shared scope key")),
           qPrintable(result.firstFailures.first()));
}

QTEST_MAIN(TestScrapePersistence)
#include "test_scrapepersistence.moc"
