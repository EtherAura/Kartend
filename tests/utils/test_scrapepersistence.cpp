// Tests for Scraper::applyScrapedItem. File-IO assertions use a
// QTemporaryDir; DB-side assertions use a tiny inline subclass of
// MockDatabaseManager that captures saveItemMetadata + saveItemArtwork
// calls without needing a real SQLite backend.
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "../integration/mocks/mockdatabasemanager.h"
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
  void front_writesPrimaryCoverAndPerTypeSubdir();
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
  void primaryCoverFallback_usesScreenshotWhenNoFrontPresent();
  void primaryCoverFallback_skippedWhenOnlyLogoPresent();
  void primaryCoverFallback_prefersBox3dOverScreenshot();
  void primaryCoverMirror_keepsExistingFrontWhenFrontSkipped();
};

void TestScrapePersistence::front_writesPrimaryCoverAndPerTypeSubdir() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const auto bytes = QByteArray("PNG_FRONT_BYTES");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("front"), QStringLiteral("Front cover"), bytes)};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(),
                                QStringLiteral("song"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QCOMPARE(result.mediaSkipped, 0);
  // Primary cover slot — what the grid auto-discovers via
  // ArtworkUtils::findArtworkForFile.
  QFile primary(tmp.path() + "/song.png");
  QVERIFY(primary.exists());
  QVERIFY(primary.open(QIODevice::ReadOnly));
  QCOMPARE(primary.readAll(), bytes);
  // Per-type write lands under "front" — the cross-provider primary-
  // cover id, now a standard gallery type. Surfaces as the "Front
  // Cover" entry in the sidebar gallery.
  QFile typed(tmp.path() + "/front/song.png");
  QVERIFY(typed.exists());
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
      makeMedia(QStringLiteral("box"), QStringLiteral("Box art"),
                QByteArray("BOX_BYTES")),
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"),
                QByteArray("SCREENSHOT_BYTES"))};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/g.bin", tmp.path(),
                                QStringLiteral("g"), item, media);

  QCOMPARE(result.mediaWritten, 2);
  QVERIFY(QFileInfo(tmp.path() + "/box/g.png").exists());
  QVERIFY(QFileInfo(tmp.path() + "/screenshot/g.png").exists());
  // Standard types are auto-discovered via subdirectory — no
  // item_artwork row needed.
  QCOMPARE(db.artworkSaves.size(), 0);
  // The primary-cover mirror IS written, using the highest-priority
  // candidate: "box" outranks "screenshot" in coverFallbackPriority.
  // No "front" present, so the fallback path picks box for the grid
  // tile / details-pane primary slot.
  QFile primary(tmp.path() + "/g.png");
  QVERIFY(primary.exists());
  QVERIFY(primary.open(QIODevice::ReadOnly));
  QCOMPARE(primary.readAll(), QByteArray("BOX_BYTES"));
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
      makeMedia(QStringLiteral("back"), QStringLiteral("Back cover"),
                QByteArray("BACK_BYTES"))};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(),
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

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/x.flac", tmp.path(),
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

  Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(),
                            QStringLiteral("song"), item, {});

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

  Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(),
                            QStringLiteral("song"), item, {});

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

  const auto result =
      Scraper::applyScrapedItem(nullptr, "u1", "/m/x.flac", tmp.path(),
                                QStringLiteral("x"), item, media);

  // Files written, but metadata save flagged false (no DB to call).
  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(!result.metadataSaved);
  QVERIFY(QFileInfo(tmp.path() + "/x.png").exists());
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
  const QList<Scraper::PendingMediaWrite> media = {
      makeMediaWithUrl(QStringLiteral("manual"), QStringLiteral("Game manual"),
                       QUrl(QStringLiteral("https://example.com/manuals/foo.pdf")),
                       QByteArray("PDF"))};

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
                       QUrl(QStringLiteral("https://example.com/foo.webm")),
                       QByteArray("V"))};

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
  const QList<Scraper::PendingMediaWrite> media = {
      makeMediaWithUrl(QStringLiteral("video"), QStringLiteral("V"),
                       QUrl(QStringLiteral("https://api.screenscraper.fr/api2/media.php?id=1")),
                       QByteArray("V"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/video/foo.mp4"));
  // The bogus ".php" extension from the URL must NOT come through.
  QVERIFY(!QFile::exists(tmp.path() + "/video/foo.php"));
}

void TestScrapePersistence::primaryCoverFallback_usesScreenshotWhenNoFrontPresent() {
  // SS regularly returns games without a `front` (box-2D) asset —
  // only screenshot / fanart / marquee / etc. The persistence layer
  // must still populate the flat-root mirror so the grid tile and
  // details-pane primary preview aren't blank. screenshot is one of
  // the recognised cover-like fallbacks.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const auto shotBytes = QByteArray("SHOT_BYTES");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"), shotBytes)};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  // Typed copy at the standard location.
  QVERIFY(QFile::exists(tmp.path() + "/screenshot/foo.png"));
  // Mirror written from the screenshot bytes — same content, flat
  // root, so the grid tile + details-pane top preview have something
  // to display.
  QFile primary(tmp.path() + "/foo.png");
  QVERIFY(primary.exists());
  QVERIFY(primary.open(QIODevice::ReadOnly));
  QCOMPARE(primary.readAll(), shotBytes);
}

void TestScrapePersistence::primaryCoverFallback_skippedWhenOnlyLogoPresent() {
  // Logos and custom user types aren't cover candidates — they
  // wouldn't make sense as the primary tile thumbnail. The mirror
  // is intentionally left empty in those cases rather than picking
  // a poor fallback.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("logo"), QStringLiteral("Logo"), QByteArray("LOGO"))};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  // Typed copy present.
  QVERIFY(QFile::exists(tmp.path() + "/logo/foo.png"));
  // No mirror — logo isn't a cover candidate.
  QVERIFY(!QFile::exists(tmp.path() + "/foo.png"));
}

void TestScrapePersistence::primaryCoverFallback_prefersBox3dOverScreenshot() {
  // When multiple cover-like types are present but no "front", the
  // highest-priority one wins for the mirror. box-3D is preferred
  // over screenshot.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const auto boxBytes = QByteArray("BOX3D_BYTES");
  const auto shotBytes = QByteArray("SHOT_BYTES");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"), shotBytes),
      makeMedia(QStringLiteral("box-3d"), QStringLiteral("Box (3D)"), boxBytes)};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 2);
  // Mirror picks the box-3D bytes, not screenshot — box-3D has
  // higher cover-fallback priority. Order in the media list doesn't
  // matter (the priority table is the tiebreaker).
  QFile primary(tmp.path() + "/foo.png");
  QVERIFY(primary.exists());
  QVERIFY(primary.open(QIODevice::ReadOnly));
  QCOMPARE(primary.readAll(), boxBytes);
}

void TestScrapePersistence::primaryCoverMirror_keepsExistingFrontWhenFrontSkipped() {
  // Regression: in FillMissing/UpdateChanged, a skipped `front` must
  // still win mirror priority over a freshly-written `box-3d`.
  // Pre-fix, only the *written* assets contributed to the priority
  // tracker so box-3d's bytes ended up at the flat-root mirror and
  // swapped the grid thumbnail.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const QByteArray existingFront("ORIGINAL_FRONT");
  const QByteArray newFront("REFRESHED_FRONT");
  const QByteArray newBox3d("BOX3D_BYTES");
  // Seed an existing front cover + flat-root mirror.
  QVERIFY(QDir().mkpath(tmp.path() + "/front"));
  {
    QFile f(tmp.path() + "/front/foo.png");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(existingFront);
  }
  {
    QFile f(tmp.path() + "/foo.png");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(existingFront);
  }
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("front"), QStringLiteral("Front cover"), newFront),
      makeMedia(QStringLiteral("box-3d"), QStringLiteral("Box (3D)"), newBox3d)};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                QStringLiteral("foo"), item, media,
                                Scraper::RescrapeMode::FillMissing);

  // box-3d was new → written; front existed → skipped.
  QCOMPARE(result.mediaWritten, 1);
  QCOMPARE(result.mediaSkipped, 1);
  QVERIFY(QFile::exists(tmp.path() + "/box-3d/foo.png"));

  // Mirror must still hold the original front bytes — NOT box-3d.
  QFile primary(tmp.path() + "/foo.png");
  QVERIFY(primary.open(QIODevice::ReadOnly));
  QCOMPARE(primary.readAll(), existingFront);
}

QTEST_MAIN(TestScrapePersistence)
#include "test_scrapepersistence.moc"
