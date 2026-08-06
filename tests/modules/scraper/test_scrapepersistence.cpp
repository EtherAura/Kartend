// Tests for Scraper::applyScrapedItem. File-IO assertions use a
// QTemporaryDir; DB-side assertions use a tiny inline subclass of
// MockDatabaseManager that captures saveItemMetadata + saveItemArtwork
// calls without needing a real SQLite backend.
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
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
  void imageAsset_suffixlessUrlSniffsWebpFromBytes();
  void imageAsset_suffixlessUrlUnrecognisedBytesDefaultToPng();
  void imageAsset_urlExtensionWinsOverSniff();
  void videoAsset_suffixlessUrlSniffsWebmFromBytes();
  void manualAsset_suffixlessUrlSniffsEpubVsCbz();
  void kindForType_classifiesVideoVariantsAndManual();
  void videoNormalizedAsset_routesToVideoDirectoryAsVideoKind();
  void screenshot_writesIntoTypedSubdirNotFlatRoot();
  void logo_writesIntoTypedSubdirNotFlatRoot();
  void multipleCovers_eachWriteIntoOwnTypedSubdir();
  void metadataSidecar_writesJsonWithScrapedFields();
  void metadataSidecar_skippedWhenTitleEmpty();
  void metadataSidecar_fillMissingKeepsExisting();
  void unsafeTypeSubdir_skippedNotWritten();
  void unsafeSharedScopeKey_skippedNotWritten();
  void updateChanged_identicalBytesSkipped();
  void updateChanged_sizeMismatchRewritten();
  void updateChanged_sameSizeDifferentBytesRewritten();
  void writeFailures_separatesGenuineFailuresFromPolicySkips();
  void policySkip_reportsExistingDestinationPath();
  void interactiveFailedFetchKeepsAbsentMarkerDeselectedPrunes();
  void cancelToken_stopsWriteFanOut();
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

  // Return value (mediaWritten/skipped) is irrelevant here — this test asserts
  // the metadata SAVE; explicitly discard the [[nodiscard]] result.
  (void)Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(), QStringLiteral("song"),
                                  item, {});

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

  // Return value (mediaWritten/skipped) is irrelevant here — this test asserts
  // the metadata SAVE; explicitly discard the [[nodiscard]] result.
  (void)Scraper::applyScrapedItem(&db, "u1", "/m/song.flac", tmp.path(), QStringLiteral("song"),
                                  item, {});

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

namespace {

/// Real encoded image bytes in the given Qt image format ("WEBP",
/// "PNG", ...) so the sniff path sees exactly what a CDN would serve,
/// not a hand-rolled magic header.
QByteArray encodedImageBytes(const char *format) {
  QImage img(8, 8, QImage::Format_RGB32);
  img.fill(Qt::red);
  QByteArray bytes;
  QBuffer buf(&bytes);
  if (!buf.open(QIODevice::WriteOnly) || !img.save(&buf, format)) {
    return {};
  }
  return bytes;
}

} // namespace

void TestScrapePersistence::imageAsset_suffixlessUrlSniffsWebpFromBytes() {
  // Kartend-aiws7: Steam's store CDN serves background_raw URLs with no
  // path extension and WebP bytes. The old per-kind fallback named the
  // file .png with RIFF/WebP inside — Qt still rendered it (QImageReader
  // content-sniffs on load) but the name lied to anything keying off
  // extension. The payload must now be sniffed and land as .webp.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const QByteArray webp = encodedImageBytes("WEBP");
  QVERIFY(!webp.isEmpty());
  const QList<Scraper::PendingMediaWrite> media = {makeMediaWithUrl(
      QStringLiteral("background"), QStringLiteral("Background"),
      QUrl(QStringLiteral(
          "https://store.akamai.steamstatic.com/images/storepagebackground/app/620")),
      webp)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/background/foo.webp"));
  QVERIFY(!QFile::exists(tmp.path() + "/background/foo.png"));
}

void TestScrapePersistence::imageAsset_suffixlessUrlUnrecognisedBytesDefaultToPng() {
  // A payload no image plugin recognises keeps the old per-kind
  // default — same fallback as before the sniff existed.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const QList<Scraper::PendingMediaWrite> media = {makeMediaWithUrl(
      QStringLiteral("screenshot"), QStringLiteral("S"),
      QUrl(QStringLiteral("https://example.com/media?id=1")), QByteArray("NOT_AN_IMAGE"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/screenshot/foo.png"));
}

void TestScrapePersistence::imageAsset_urlExtensionWinsOverSniff() {
  // A whitelisted URL suffix is still honoured without looking at the
  // bytes — sniffing only fills the no-usable-suffix gap. Renaming
  // suffix-carrying assets would churn FillMissing's exact-path skip
  // gate on every provider that re-encodes.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  const QByteArray webp = encodedImageBytes("WEBP");
  QVERIFY(!webp.isEmpty());
  const QList<Scraper::PendingMediaWrite> media = {
      makeMediaWithUrl(QStringLiteral("screenshot"), QStringLiteral("S"),
                       QUrl(QStringLiteral("https://example.com/shots/foo.png")), webp)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/screenshot/foo.png"));
  QVERIFY(!QFile::exists(tmp.path() + "/screenshot/foo.webp"));
}

void TestScrapePersistence::videoAsset_suffixlessUrlSniffsWebmFromBytes() {
  // EBML magic + "webm" DocType in the header → .webm instead of the
  // blind .mp4 default. The DocType string sits inside the first few
  // dozen bytes of any real WebM.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  QByteArray ebml = QByteArrayLiteral("\x1A\x45\xDF\xA3");
  ebml.append(QByteArrayLiteral("\x9F\x42\x86\x81\x01"));
  ebml.append("B\x82");
  ebml.append(QByteArrayLiteral("\x84"));
  ebml.append("webm");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMediaWithUrl(QStringLiteral("video"), QStringLiteral("V"),
                       QUrl(QStringLiteral("https://example.com/media?id=2")), ebml)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/video/foo.webm"));
  QVERIFY(!QFile::exists(tmp.path() + "/video/foo.mp4"));
}

void TestScrapePersistence::manualAsset_suffixlessUrlSniffsEpubVsCbz() {
  // Zip-magic payloads split on the EPUB marker: the spec-mandated
  // stored-first "mimetype" entry puts "application/epub+zip" verbatim
  // in the leading bytes; a zip without it is treated as CBZ.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  QByteArray epub = QByteArrayLiteral("PK\x03\x04");
  epub.append(QByteArray(26, '\0'));
  epub.append("mimetypeapplication/epub+zip");
  QByteArray cbz = QByteArrayLiteral("PK\x03\x04");
  cbz.append(QByteArray(26, '\0'));
  cbz.append("page001.png");
  const QList<Scraper::PendingMediaWrite> media = {
      makeMediaWithUrl(QStringLiteral("manual"), QStringLiteral("M"),
                       QUrl(QStringLiteral("https://example.com/media?id=3")), epub)};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);
  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/manual/foo.epub"));

  const QList<Scraper::PendingMediaWrite> media2 = {
      makeMediaWithUrl(QStringLiteral("manual"), QStringLiteral("M"),
                       QUrl(QStringLiteral("https://example.com/media?id=4")), cbz)};
  const auto result2 = Scraper::applyScrapedItem(&db, "u1", "/m/bar.bin", tmp.path(),
                                                 QStringLiteral("bar"), item, media2);
  QCOMPARE(result2.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/manual/bar.cbz"));
}

void TestScrapePersistence::kindForType_classifiesVideoVariantsAndManual() {
  // Kartend-jjyst.1: kindForType matched only the exact "video" token, so
  // ScreenScraper's "video-normalized" classified as an image — fetched under
  // the image/ Content-Type pin (always rejected) and, had it downloaded,
  // written as .png. Every "video-*" variant must classify as Video.
  QCOMPARE(Scraper::kindForType(QStringLiteral("video")), Scraper::MediaKind::Video);
  QCOMPARE(Scraper::kindForType(QStringLiteral("video-normalized")), Scraper::MediaKind::Video);
  QCOMPARE(Scraper::kindForType(QStringLiteral("Video-Normalized")), Scraper::MediaKind::Video);
  QCOMPARE(Scraper::kindForType(QStringLiteral("  video ")), Scraper::MediaKind::Video);
  QCOMPARE(Scraper::kindForType(QStringLiteral("manual")), Scraper::MediaKind::Manual);
  QCOMPARE(Scraper::kindForType(QStringLiteral("Manual")), Scraper::MediaKind::Manual);
  // Non-video/manual tags stay images — including lookalikes that only
  // contain (rather than start with) the video token.
  QCOMPARE(Scraper::kindForType(QStringLiteral("front")), Scraper::MediaKind::Image);
  QCOMPARE(Scraper::kindForType(QStringLiteral("videotheme")), Scraper::MediaKind::Image);
  QCOMPARE(Scraper::kindForType(QString()), Scraper::MediaKind::Image);
}

void TestScrapePersistence::videoNormalizedAsset_routesToVideoDirectoryAsVideoKind() {
  // End-to-end: a "video-normalized" asset must ride the Video routing —
  // video/ subdir + mp4 default extension — not the per-type image layout
  // ("video-normalized/foo.png") the old exact-token match produced.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  Scraper::ScrapedItem item;
  const QList<Scraper::PendingMediaWrite> media = {makeMediaWithUrl(
      QStringLiteral("video-normalized"), QStringLiteral("Video (normalized)"),
      QUrl(QStringLiteral("https://api.screenscraper.fr/api2/media.php?id=2")), QByteArray("V"))};

  const auto result = Scraper::applyScrapedItem(&db, "u1", "/m/foo.bin", tmp.path(),
                                                QStringLiteral("foo"), item, media);

  QCOMPARE(result.mediaWritten, 1);
  QVERIFY(QFile::exists(tmp.path() + "/video/foo.mp4"));
  QVERIFY(!QDir(tmp.path() + "/video-normalized").exists());
  // Videos are not artwork — no item_artwork row.
  QCOMPARE(db.artworkSaves.size(), 0);
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

  QCOMPARE(Scraper::writeMetadataSidecar(tmp.path(), QStringLiteral("game"), item,
                                         Scraper::RescrapeMode::Overwrite),
           Scraper::SidecarWriteOutcome::Written);

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

  QCOMPARE(Scraper::writeMetadataSidecar(tmp.path(), QStringLiteral("game"), item,
                                         Scraper::RescrapeMode::Overwrite),
           Scraper::SidecarWriteOutcome::Skipped);
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

  QCOMPARE(Scraper::writeMetadataSidecar(tmp.path(), QStringLiteral("game"), item,
                                         Scraper::RescrapeMode::FillMissing),
           Scraper::SidecarWriteOutcome::Skipped);
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

// --- UpdateChanged byte-compare (Kartend-h1e8d) ---------------------------
// writeMediaFiles is the DB-free file-I/O half, so these drive it directly
// with RescrapeMode::UpdateChanged after seeding the destination on disk.

void TestScrapePersistence::updateChanged_identicalBytesSkipped() {
  // Existing file byte-identical to the incoming payload → keep it, skip write.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto bytes = QByteArray("IDENTICAL_PAYLOAD_OF_SOME_LENGTH");
  QVERIFY(QDir().mkpath(tmp.path() + "/screenshot"));
  {
    QFile seed(tmp.path() + "/screenshot/foo.png");
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QCOMPARE(seed.write(bytes), static_cast<qint64>(bytes.size()));
  }
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"), bytes)};

  const auto result = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), media,
                                               Scraper::RescrapeMode::UpdateChanged);

  QCOMPARE(result.mediaWritten, 0);
  QCOMPARE(result.mediaSkipped, 1);
  // A rescrape-policy skip is benign — it must never read as a write failure
  // in the batch summary (Kartend-jjyst.4).
  QVERIFY(result.writeFailures.isEmpty());
  QFile after(tmp.path() + "/screenshot/foo.png");
  QVERIFY(after.open(QIODevice::ReadOnly));
  QCOMPARE(after.readAll(), bytes);
}

void TestScrapePersistence::updateChanged_sizeMismatchRewritten() {
  // Different size can't match — the size() short-circuit must report "changed"
  // (so the new bytes are written) without needing to read the existing file.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto incoming = QByteArray("FRESH_LONGER_REPLACEMENT_PAYLOAD");
  QVERIFY(QDir().mkpath(tmp.path() + "/screenshot"));
  {
    QFile seed(tmp.path() + "/screenshot/foo.png");
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QCOMPARE(seed.write(QByteArray("OLD_SHORTER")), static_cast<qint64>(11));
  }
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"), incoming)};

  const auto result = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), media,
                                               Scraper::RescrapeMode::UpdateChanged);

  QCOMPARE(result.mediaWritten, 1);
  QCOMPARE(result.mediaSkipped, 0);
  QFile after(tmp.path() + "/screenshot/foo.png");
  QVERIFY(after.open(QIODevice::ReadOnly));
  QCOMPARE(after.readAll(), incoming);
}

void TestScrapePersistence::updateChanged_sameSizeDifferentBytesRewritten() {
  // Same size but different content — the chunked compare must detect the
  // mismatch and rewrite with the incoming bytes.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto incoming = QByteArray("AAAAAAAAAAAAAAAA"); // 16 bytes
  const auto existing = QByteArray("BBBBBBBBBBBBBBBB"); // 16 bytes, same size
  QCOMPARE(incoming.size(), existing.size());
  QVERIFY(QDir().mkpath(tmp.path() + "/screenshot"));
  {
    QFile seed(tmp.path() + "/screenshot/foo.png");
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QCOMPARE(seed.write(existing), static_cast<qint64>(existing.size()));
  }
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"), incoming)};

  const auto result = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), media,
                                               Scraper::RescrapeMode::UpdateChanged);

  QCOMPARE(result.mediaWritten, 1);
  QCOMPARE(result.mediaSkipped, 0);
  QFile after(tmp.path() + "/screenshot/foo.png");
  QVERIFY(after.open(QIODevice::ReadOnly));
  QCOMPARE(after.readAll(), incoming);
}

void TestScrapePersistence::writeFailures_separatesGenuineFailuresFromPolicySkips() {
  // Kartend-jjyst.4: MediaWriteResult::firstFailures is the reason channel
  // for EVERY mediaSkipped tick, so it mixes benign rescrape-policy skips
  // with genuine failures. writeFailures must carry only the genuine ones —
  // the batch summary folds that list, and a FillMissing run's kept-existing
  // files must not read as failures. One call with both shapes: an existing
  // file under FillMissing (benign) and an unsafe traversal type (genuine).
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir().mkpath(tmp.path() + "/screenshot"));
  {
    QFile seed(tmp.path() + "/screenshot/foo.png");
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QVERIFY(seed.write(QByteArray("EXISTING")) > 0);
  }
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"),
                QByteArray("NEW_BYTES")),
      makeMedia(QStringLiteral("../evil"), QStringLiteral("Evil"), QByteArray("X"))};

  const auto result = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), media,
                                               Scraper::RescrapeMode::FillMissing);

  QCOMPARE(result.mediaWritten, 0);
  QCOMPARE(result.mediaSkipped, 2);
  QCOMPARE(result.firstFailures.size(), 2); // both reasons surfaced
  QCOMPARE(result.writeFailures.size(), 1); // only the genuine failure
  QVERIFY2(result.writeFailures.first().contains(QStringLiteral("unsafe media subdirectory")),
           qPrintable(result.writeFailures.first()));
}

void TestScrapePersistence::policySkip_reportsExistingDestinationPath() {
  // Kartend-jjyst.5: a skip-because-present must still report WHERE the
  // existing file lives (existingPaths) — the entity path wires the
  // collection-config art fields from it, and an empty writtenPaths on a
  // FillMissing re-run otherwise left the config unwired. Genuine failures
  // (unsafe type here) must NOT land in existingPaths.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir().mkpath(tmp.path() + "/screenshot"));
  {
    QFile seed(tmp.path() + "/screenshot/foo.png");
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QVERIFY(seed.write(QByteArray("EXISTING")) > 0);
  }
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"),
                QByteArray("NEW_BYTES")),
      makeMedia(QStringLiteral("../evil"), QStringLiteral("Evil"), QByteArray("X"))};

  const auto result = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), media,
                                               Scraper::RescrapeMode::FillMissing);

  QCOMPARE(result.mediaWritten, 0);
  QVERIFY(result.writtenPaths.isEmpty());
  QCOMPARE(result.existingPaths,
           QStringList{QDir(tmp.path()).filePath(QStringLiteral("screenshot/foo.png"))});

  // UpdateChanged byte-match reports the kept file the same way.
  const QList<Scraper::PendingMediaWrite> identical = {makeMedia(
      QStringLiteral("screenshot"), QStringLiteral("Screenshot"), QByteArray("EXISTING"))};
  const auto result2 = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), identical,
                                                Scraper::RescrapeMode::UpdateChanged);
  QCOMPARE(result2.mediaWritten, 0);
  QCOMPARE(result2.existingPaths,
           QStringList{QDir(tmp.path()).filePath(QStringLiteral("screenshot/foo.png"))});
}

void TestScrapePersistence::interactiveFailedFetchKeepsAbsentMarkerDeselectedPrunes() {
  // Kartend-jjyst.14: the interactive apply path (dialog → applyScrapedItem)
  // stamps ScrapedItem::mediaFetchFailedThisRun from the download dispatcher.
  // A returned type whose fetch FAILED keeps its known-absent marker; a
  // returned type the user merely DESELECTED (never dispatched, so never in
  // the failed list) still prunes — the provider does supply it.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  CapturingDb db;
  db.preloadedMetadata.collectionUuid = QStringLiteral("u1");
  db.preloadedMetadata.path = QStringLiteral("/m/g.bin");
  db.preloadedMetadata.mediaAbsent = {QStringLiteral("video"), QStringLiteral("marquee")};

  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Test");
  Scraper::MediaAsset video;
  video.type = QStringLiteral("video");
  video.url = QUrl(QStringLiteral("https://example.test/clip.mp4"));
  item.media.append(video);
  Scraper::MediaAsset marquee;
  marquee.type = QStringLiteral("marquee");
  marquee.url = QUrl(QStringLiteral("https://example.test/marquee.png"));
  item.media.append(marquee);
  // video's download failed (dispatcher reported it); marquee was deselected.
  // Neither delivered bytes, so the media write list is empty.
  item.mediaFetchFailedThisRun = {QStringLiteral("video")};

  const auto result =
      Scraper::applyScrapedItem(&db, "u1", "/m/g.bin", tmp.path(), QStringLiteral("g"), item, {},
                                Scraper::RescrapeMode::FillMissing);

  QVERIFY(result.metadataSaved);
  QCOMPARE(db.metadataSaves.size(), 1);
  QCOMPARE(db.metadataSaves.first().mediaAbsent, QStringList{QStringLiteral("video")});
}

void TestScrapePersistence::cancelToken_stopsWriteFanOut() {
  // Kartend-vi76q: a set cancel token is polled between assets, so a
  // cancelled/destructing owner (BatchScrapeRunner::cancel() / its dtor)
  // stops the fan-out before the next file lands on disk. A pre-set token
  // is the deterministic boundary case: nothing must be written at all.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QList<Scraper::PendingMediaWrite> media = {
      makeMedia(QStringLiteral("screenshot"), QStringLiteral("Screenshot"),
                QByteArray("PAYLOAD_A")),
      makeMedia(QStringLiteral("title"), QStringLiteral("Title"), QByteArray("PAYLOAD_B"))};

  const auto token = std::make_shared<std::atomic<bool>>(true);
  const auto result = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), media,
                                               Scraper::RescrapeMode::Overwrite, token);

  QCOMPARE(result.mediaWritten, 0);
  QVERIFY(!QFile::exists(tmp.path() + "/screenshot/foo.png"));
  QVERIFY(!QFile::exists(tmp.path() + "/title/foo.png"));

  // Unset token (the default-constructed shared_ptr path is covered by every
  // other slot in this file): identical media must write both assets.
  const auto result2 = Scraper::writeMediaFiles(tmp.path(), QStringLiteral("foo"), media,
                                                Scraper::RescrapeMode::Overwrite,
                                                std::make_shared<std::atomic<bool>>(false));
  QCOMPARE(result2.mediaWritten, 2);
  QVERIFY(QFile::exists(tmp.path() + "/screenshot/foo.png"));
  QVERIFY(QFile::exists(tmp.path() + "/title/foo.png"));
}

QTEST_MAIN(TestScrapePersistence)
#include "test_scrapepersistence.moc"
