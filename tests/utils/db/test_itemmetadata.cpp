// Tests for ItemMetadataStore::load / save / remove against an in-memory
// SQLite database with the v5 schema applied via DbMigrations.
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "dbmigrations.h"
#include "itemmetadata.h"

using ItemMetadataStore::ItemMetadata;

namespace {

QSqlDatabase openMemoryDb(const QString &conn) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    return db;
  }
  // Mirror the minimum base schema the migrations expect to find. The v5
  // migration only adds a brand-new table, but earlier migrations touch
  // collections/items so we provide stub tables.
  QSqlQuery q(db);
  q.exec("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)");
  q.exec("CREATE TABLE items (id INTEGER PRIMARY KEY AUTOINCREMENT, "
         "name TEXT, path TEXT, last_modified TEXT)");
  DbMigrations::applySchemaMigrations(db, "test");
  return db;
}

void closeAndRemove(QSqlDatabase &db, const QString &conn) {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(conn);
}

} // namespace

class TestItemMetadata : public QObject {
  Q_OBJECT
private slots:
  void isEmptyOnDefaultConstructed();
  void isEmptyIgnoresKeys();
  void isNotEmptyForAnyPopulatedField();
  void loadReturnsEmptyForMissingRow();
  void saveAndLoadRoundTrip();
  void saveOverwritesExistingRow();
  void distinguishesByCollectionUuid();
  void removeDeletesRow();
  void runtimeSecondsNullableRoundTrip();
  void launcherIndexNullableRoundTrip();
  void ratingNullableRoundTrip();
  void stateFlagsRoundTrip();
  void saveRejectsEmptyPath();
  void parseTagsHandlesEmptyAndMalformed();
  void parseTagsPreservesOrderAndDedupes();
  void serializeTagsPrunesEmptyAndDeduplicates();
  void tagsRoundTripThroughDb();
  void mediaAbsentRoundTripsThroughDb();
  void parseCustomFieldsHandlesEmptyAndMalformed();
  void parseCustomFieldsPreservesOrderAndCoercesValues();
  void serializeCustomFieldsPrunesEmptyKeys();
  void customFieldsRoundTripThroughDb();
  void manualExtensionsAreNonEmpty();
  void findManualReturnsEmptyForMissingDirectory();
  void findManualMatchesExtensionsCaseInsensitively();
  void findManualPicksFirstExtensionInOrder();
  void resolveManualFilePrefersValidOverride();
  void resolveManualFileFallsBackToAutoDiscovery();
  void resolveManualFileMissingOverrideReturnsEmpty();
  void resolveManualFileTildeOnlyExpandsHomePrefix();
};

void TestItemMetadata::isEmptyOnDefaultConstructed() {
  ItemMetadata m;
  QVERIFY(m.isEmpty());
}

void TestItemMetadata::isEmptyIgnoresKeys() {
  // collectionUuid / path / source / updatedAt are bookkeeping, not display
  // content, so they should not flip isEmpty(). Otherwise the sidebar would
  // render an empty Details section just because we asked the DB for keys.
  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/some/file.rom";
  m.source = "user";
  m.updatedAt = "2026-01-01T00:00:00Z";
  QVERIFY(m.isEmpty());
}

void TestItemMetadata::isNotEmptyForAnyPopulatedField() {
  for (auto &mutate : std::initializer_list<std::function<void(ItemMetadata &)>>{
           [](ItemMetadata &m) { m.title = "T"; },
           [](ItemMetadata &m) { m.description = "D"; },
           [](ItemMetadata &m) { m.genre = "G"; },
           [](ItemMetadata &m) { m.developer = "Dev"; },
           [](ItemMetadata &m) { m.publisher = "Pub"; },
           [](ItemMetadata &m) { m.releaseDate = "2026"; },
           [](ItemMetadata &m) { m.contentRating = "T"; },
           [](ItemMetadata &m) { m.players = "1-2"; },
           [](ItemMetadata &m) { m.runtimeSeconds = 0; },
           [](ItemMetadata &m) { m.tags = "a"; },
           [](ItemMetadata &m) { m.customFields = "{}"; },
           [](ItemMetadata &m) { m.manualPath = "/x"; },
           [](ItemMetadata &m) { m.launcherIndex = 0; },
           [](ItemMetadata &m) { m.notes = "N"; },
           [](ItemMetadata &m) { m.sourceUrl = "https://example.org"; },
           [](ItemMetadata &m) { m.rating = 0; },
           [](ItemMetadata &m) { m.isPinned = true; },
           [](ItemMetadata &m) { m.isHidden = true; },
           [](ItemMetadata &m) { m.continueLater = true; },
       }) {
    ItemMetadata m;
    mutate(m);
    QVERIFY(!m.isEmpty());
  }
}

void TestItemMetadata::loadReturnsEmptyForMissingRow() {
  const QString conn = "im_load_missing";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  auto result = ItemMetadataStore::load(db, "uuid-1", "/missing");
  QVERIFY(result.isOk());
  ItemMetadata m = result.value();
  QCOMPARE(m.collectionUuid, QStringLiteral("uuid-1"));
  QCOMPARE(m.path, QStringLiteral("/missing"));
  QVERIFY(m.isEmpty());

  closeAndRemove(db, conn);
}

void TestItemMetadata::saveAndLoadRoundTrip() {
  const QString conn = "im_round_trip";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/videos/clip.mp4";
  m.title = "Concert Recording";
  m.description = "Live show from the city hall.";
  m.genre = "Concert";
  m.developer = "Studio";
  m.publisher = "Label";
  m.releaseDate = "1991-06-23";
  m.contentRating = "G";
  m.players = "1";
  m.runtimeSeconds = 120;
  m.tags = "[\"live\",\"jazz\"]";
  m.customFields = "{\"shelf\":\"A1\"}";
  m.notes = "Recorded with two cameras; left audio channel is missing.";
  m.sourceUrl = "https://archive.example/clip";
  m.rating = 8; // 4 stars
  m.manualPath = "/docs/clip.pdf";
  m.launcherIndex = 2;
  m.source = "user";

  auto saved = ItemMetadataStore::save(db, m);
  QVERIFY(saved.isOk());
  QVERIFY(saved.value());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/videos/clip.mp4");
  QVERIFY(loaded.isOk());
  ItemMetadata r = loaded.value();
  QCOMPARE(r.title, m.title);
  QCOMPARE(r.description, m.description);
  QCOMPARE(r.genre, m.genre);
  QCOMPARE(r.developer, m.developer);
  QCOMPARE(r.publisher, m.publisher);
  QCOMPARE(r.releaseDate, m.releaseDate);
  QCOMPARE(r.contentRating, m.contentRating);
  QCOMPARE(r.players, m.players);
  QCOMPARE(r.runtimeSeconds, 120);
  QCOMPARE(r.tags, m.tags);
  QCOMPARE(r.customFields, m.customFields);
  QCOMPARE(r.notes, m.notes);
  QCOMPARE(r.sourceUrl, m.sourceUrl);
  QCOMPARE(r.rating, 8);
  QCOMPARE(r.manualPath, m.manualPath);
  QCOMPARE(r.launcherIndex, 2);
  QCOMPARE(r.source, m.source);
  QVERIFY(!r.updatedAt.isEmpty()); // save() stamps current UTC ISO time

  closeAndRemove(db, conn);
}

void TestItemMetadata::saveOverwritesExistingRow() {
  const QString conn = "im_upsert";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "First";
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  m.title = "Second";
  m.genre = "Action";
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/p").value();
  QCOMPARE(loaded.title, QStringLiteral("Second"));
  QCOMPARE(loaded.genre, QStringLiteral("Action"));

  // Only one row should exist for the (uuid, path) pair.
  QSqlQuery q(db);
  QVERIFY(q.exec("SELECT COUNT(*) FROM item_metadata"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  closeAndRemove(db, conn);
}

void TestItemMetadata::distinguishesByCollectionUuid() {
  const QString conn = "im_distinct_uuid";
  auto db = openMemoryDb(conn);

  ItemMetadata a;
  a.collectionUuid = "uuid-A";
  a.path = "/shared";
  a.title = "A title";
  QVERIFY(ItemMetadataStore::save(db, a).isOk());

  ItemMetadata b;
  b.collectionUuid = "uuid-B";
  b.path = "/shared";
  b.title = "B title";
  QVERIFY(ItemMetadataStore::save(db, b).isOk());

  QCOMPARE(ItemMetadataStore::load(db, "uuid-A", "/shared").value().title,
           QStringLiteral("A title"));
  QCOMPARE(ItemMetadataStore::load(db, "uuid-B", "/shared").value().title,
           QStringLiteral("B title"));

  closeAndRemove(db, conn);
}

void TestItemMetadata::removeDeletesRow() {
  const QString conn = "im_remove";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "T";
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto removed = ItemMetadataStore::remove(db, "uuid-1", "/p");
  QVERIFY(removed.isOk());

  // Removing a non-existent row must still succeed.
  auto removedAgain = ItemMetadataStore::remove(db, "uuid-1", "/p");
  QVERIFY(removedAgain.isOk());

  QVERIFY(ItemMetadataStore::load(db, "uuid-1", "/p").value().isEmpty());

  closeAndRemove(db, conn);
}

void TestItemMetadata::runtimeSecondsNullableRoundTrip() {
  const QString conn = "im_runtime_null";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "T";
  // runtimeSeconds defaults to -1 ("unset"); save() must persist NULL.
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/p").value();
  QCOMPARE(loaded.runtimeSeconds, -1);

  // Now set a real value and confirm it round-trips.
  m.runtimeSeconds = 0;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().runtimeSeconds, 0);

  closeAndRemove(db, conn);
}

void TestItemMetadata::launcherIndexNullableRoundTrip() {
  // Mirrors the runtime-seconds NULL contract for the per-item launcher
  // override: -1 in the struct means "no override" and
  // serializes to NULL; setting a non-negative value round-trips intact;
  // returning to -1 must clear the column back to NULL.
  const QString conn = "im_launcher_null";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "T";
  // launcherIndex defaults to -1 ("no override"); save() must persist NULL.
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().launcherIndex, -1);

  // Pin a real index — round-trip must preserve it exactly.
  m.launcherIndex = 3;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().launcherIndex, 3);

  // Clearing back to -1 must round-trip to NULL, not leak the previous value.
  m.launcherIndex = -1;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().launcherIndex, -1);

  closeAndRemove(db, conn);
}

void TestItemMetadata::ratingNullableRoundTrip() {
  // Mirrors the launcherIndex contract: -1 in the struct serializes to NULL
  // ("unset"), a 0-10 value round-trips intact, returning to -1 must clear
  // the column back to NULL (otherwise stale ratings would leak forward).
  const QString conn = "im_rating_null";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.title = "T";
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().rating, -1);

  // Mid-scale half-star.
  m.rating = 7;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().rating, 7);

  // Edge value (max).
  m.rating = 10;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().rating, 10);

  // Clearing back to -1 must round-trip to NULL.
  m.rating = -1;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QCOMPARE(ItemMetadataStore::load(db, "uuid-1", "/p").value().rating, -1);

  closeAndRemove(db, conn);
}

void TestItemMetadata::stateFlagsRoundTrip() {
  // Per-item state flags (pinned / hidden / continue-later) must round-trip
  // through save/load without bleeding into one another, and the cleared
  // state must persist back as 0 — otherwise a "Pin then unpin" sequence
  // would leak the pinned state forward.
  const QString conn = "im_state_flags";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.isPinned = true;
  m.isHidden = false;
  m.continueLater = true;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  ItemMetadata r = ItemMetadataStore::load(db, "uuid-1", "/p").value();
  QVERIFY(r.isPinned);
  QVERIFY(!r.isHidden);
  QVERIFY(r.continueLater);

  // Toggle each independently and confirm the others stay put.
  m.isPinned = false;
  m.isHidden = true;
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  r = ItemMetadataStore::load(db, "uuid-1", "/p").value();
  QVERIFY(!r.isPinned);
  QVERIFY(r.isHidden);
  QVERIFY(r.continueLater); // unchanged

  closeAndRemove(db, conn);
}

void TestItemMetadata::parseTagsHandlesEmptyAndMalformed() {
  using ItemMetadataStore::parseTags;
  QVERIFY(parseTags(QString()).isEmpty());
  QVERIFY(parseTags("   ").isEmpty());
  QVERIFY(parseTags("not json").isEmpty());
  // Objects and primitives are not arrays -> no tags.
  QVERIFY(parseTags("{\"a\":1}").isEmpty());
  QVERIFY(parseTags("\"hello\"").isEmpty());
}

void TestItemMetadata::parseTagsPreservesOrderAndDedupes() {
  using ItemMetadataStore::parseTags;
  // First-seen casing wins on case-insensitive dedupe so the user's choice
  // survives a save/reload round-trip rather than being lowercased.
  const QStringList out = parseTags("[\"Jazz\",\"live\",\"jazz\",\"  \",\"LIVE\"]");
  QCOMPARE(out, (QStringList{"Jazz", "live"}));
}

void TestItemMetadata::serializeTagsPrunesEmptyAndDeduplicates() {
  using ItemMetadataStore::serializeTags;
  // Empty input -> empty string (round-trips to NULL in the DB).
  QVERIFY(serializeTags({}).isEmpty());
  // Trimming + dedup happens on serialize too so the editor can be sloppy.
  const QString json = serializeTags({" Jazz ", "live", "JAZZ", "", "  "});
  QCOMPARE(json, QStringLiteral("[\"Jazz\",\"live\"]"));
}

void TestItemMetadata::tagsRoundTripThroughDb() {
  // End-to-end: a tag JSON string survives save/load unchanged so smart
  // playlist evaluators and the upcoming search-token feature can rely on
  // the stored shape matching what the editor wrote.
  const QString conn = "im_tags_roundtrip";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/v";
  m.tags = ItemMetadataStore::serializeTags({"live", "concert", "jazz"});
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/v").value();
  QCOMPARE(loaded.tags, m.tags);
  QCOMPARE(ItemMetadataStore::parseTags(loaded.tags), (QStringList{"live", "concert", "jazz"}));

  closeAndRemove(db, conn);
}

void TestItemMetadata::mediaAbsentRoundTripsThroughDb() {
  // Kartend-kihyx: the known-absent media set survives save/load and loadBatch
  // unchanged — which also proves the v28 `media_absent` column exists after
  // migration. An empty set round-trips to NULL (no stray "[]").
  const QString conn = "im_media_absent_roundtrip";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/g";
  m.mediaAbsent = {"map", "marquee", "pictoliste"};
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/g").value();
  QCOMPARE(loaded.mediaAbsent, (QStringList{"map", "marquee", "pictoliste"}));

  // Same via the batched loader the scrape pre-filter uses.
  auto batch = ItemMetadataStore::loadBatch(db, "uuid-1", {"/g"}).value();
  QCOMPARE(batch.value("/g").mediaAbsent, (QStringList{"map", "marquee", "pictoliste"}));

  // Empty set -> NULL column -> empty list on reload (no "[]").
  ItemMetadata blank;
  blank.collectionUuid = "uuid-1";
  blank.path = "/h";
  QVERIFY(ItemMetadataStore::save(db, blank).isOk());
  QVERIFY(ItemMetadataStore::load(db, "uuid-1", "/h").value().mediaAbsent.isEmpty());

  closeAndRemove(db, conn);
}

void TestItemMetadata::saveRejectsEmptyPath() {
  const QString conn = "im_empty_path";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1"; // path intentionally empty
  auto result = ItemMetadataStore::save(db, m);
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);

  closeAndRemove(db, conn);
}

void TestItemMetadata::parseCustomFieldsHandlesEmptyAndMalformed() {
  using ItemMetadataStore::parseCustomFields;
  QVERIFY(parseCustomFields(QString()).isEmpty());
  QVERIFY(parseCustomFields("   ").isEmpty());
  QVERIFY(parseCustomFields("not json").isEmpty());
  // JSON arrays and primitives are not objects -> no fields.
  QVERIFY(parseCustomFields("[1,2,3]").isEmpty());
  QVERIFY(parseCustomFields("\"hello\"").isEmpty());
}

void TestItemMetadata::parseCustomFieldsPreservesOrderAndCoercesValues() {
  // QJsonObject sorts keys alphabetically (case-sensitive, uppercase first)
  // so parseCustomFields surfaces rows in a stable, predictable order.
  const QString json = R"({"alpha":"first","Beta":"second","gamma":"third"})";
  const auto fields = ItemMetadataStore::parseCustomFields(json);
  QCOMPARE(fields.size(), 3);
  QCOMPARE(fields[0].first, QStringLiteral("Beta"));
  QCOMPARE(fields[0].second, QStringLiteral("second"));
  QCOMPARE(fields[1].first, QStringLiteral("alpha"));
  QCOMPARE(fields[2].first, QStringLiteral("gamma"));

  // Non-string values are coerced via QJsonValue::toString(), which yields
  // an empty string for numbers/bools/null. The keys should still appear so
  // the user can re-edit them rather than silently losing the row.
  const auto coerced =
      ItemMetadataStore::parseCustomFields(R"({"num":42,"flag":true,"nope":null,"":"x"})");
  QCOMPARE(coerced.size(), 3); // empty key dropped
  // Sorted: "flag", "nope", "num".
  QCOMPARE(coerced[0].first, QStringLiteral("flag"));
  QCOMPARE(coerced[1].first, QStringLiteral("nope"));
  QCOMPARE(coerced[2].first, QStringLiteral("num"));
  QCOMPARE(coerced[2].second, QString());
}

void TestItemMetadata::serializeCustomFieldsPrunesEmptyKeys() {
  using ItemMetadataStore::CustomFieldList;
  using ItemMetadataStore::serializeCustomFields;

  // All-empty list -> empty string so the DB column stores NULL via the
  // existing nullableString() bind path, instead of literal `{}`.
  QVERIFY(serializeCustomFields({}).isEmpty());
  QVERIFY(serializeCustomFields(CustomFieldList{{QString(), "v"}, {"   ", "v"}}).isEmpty());

  const QString out = serializeCustomFields(
      CustomFieldList{{"first", "1"}, {"second", QString()}, {"   ", "skip"}, {"third", "3"}});
  // Round-trip back through the parser. Output ordering is alphabetical:
  // "first", "second", "third".
  const auto reparsed = ItemMetadataStore::parseCustomFields(out);
  QCOMPARE(reparsed.size(), 3);
  QCOMPARE(reparsed[0].first, QStringLiteral("first"));
  QCOMPARE(reparsed[1].first, QStringLiteral("second"));
  QCOMPARE(reparsed[1].second, QString());
  QCOMPARE(reparsed[2].first, QStringLiteral("third"));
}

void TestItemMetadata::customFieldsRoundTripThroughDb() {
  const QString conn = "im_custom_fields_db";
  auto db = openMemoryDb(conn);

  ItemMetadata m;
  m.collectionUuid = "uuid-1";
  m.path = "/p";
  m.customFields = ItemMetadataStore::serializeCustomFields(
      ItemMetadataStore::CustomFieldList{{"developer-note", "fixed crash"}, {"shelf", "A12"}});
  QVERIFY(ItemMetadataStore::save(db, m).isOk());

  auto loaded = ItemMetadataStore::load(db, "uuid-1", "/p").value();
  // isEmpty() must flip to false even when the only populated field is the
  // custom_fields blob -- the sidebar depends on this to show Details.
  QVERIFY(!loaded.isEmpty());
  const auto parsed = ItemMetadataStore::parseCustomFields(loaded.customFields);
  QCOMPARE(parsed.size(), 2);
  // Alphabetical: "developer-note" < "shelf".
  QCOMPARE(parsed[0].first, QStringLiteral("developer-note"));
  QCOMPARE(parsed[0].second, QStringLiteral("fixed crash"));
  QCOMPARE(parsed[1].first, QStringLiteral("shelf"));

  // Saving an empty list clears the row's customFields back to NULL.
  m.customFields = ItemMetadataStore::serializeCustomFields({});
  QVERIFY(m.customFields.isEmpty());
  QVERIFY(ItemMetadataStore::save(db, m).isOk());
  QVERIFY(ItemMetadataStore::load(db, "uuid-1", "/p").value().customFields.isEmpty());

  closeAndRemove(db, conn);
}

void TestItemMetadata::manualExtensionsAreNonEmpty() {
  // Sanity: the canonical list must include at least the headline formats so
  // findManualForBaseName has something to match against.
  const auto &exts = ItemMetadataStore::manualExtensions();
  QVERIFY(!exts.isEmpty());
  QVERIFY(exts.contains(QStringLiteral("pdf")));
  QVERIFY(exts.contains(QStringLiteral("epub")));
}

void TestItemMetadata::findManualReturnsEmptyForMissingDirectory() {
  // No tilde expansion or filesystem call should fail loudly when the inputs
  // are unusable; the resolver just returns empty so the sidebar hides the
  // Manual button.
  QVERIFY(ItemMetadataStore::findManualForBaseName(QString(), "/tmp").isEmpty());
  QVERIFY(ItemMetadataStore::findManualForBaseName("foo", QString()).isEmpty());
  QVERIFY(ItemMetadataStore::findManualForBaseName("foo", "/this/path/does/not/exist").isEmpty());
}

void TestItemMetadata::findManualMatchesExtensionsCaseInsensitively() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Drop a manual with an uppercase extension so we exercise the upper-case
  // probe in findManualForBaseName(). The lower-case probe is exercised by
  // findManualPicksFirstExtensionInOrder().
  const QString upperPath = dir.filePath("Game.PDF");
  QFile f(upperPath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();

  // On Windows's case-insensitive filesystem the matcher's lowercase
  // probe finds "Game.PDF" but returns the lowercase candidate string;
  // Qt's canonicalFilePath doesn't normalise case on Windows (only
  // resolves symlinks/dots), so compare via case-insensitive match plus
  // a file-exists guard.
  const QString found = ItemMetadataStore::findManualForBaseName("Game", dir.path());
  QVERIFY2(QFile::exists(found), qPrintable(QString("result does not exist: %1").arg(found)));
  QVERIFY2(
      QString::compare(found, upperPath, Qt::CaseInsensitive) == 0,
      qPrintable(QString("found %1 != upperPath %2 (case-insensitive)").arg(found, upperPath)));
  // Different basename -> no match (we don't substring-match).
  QVERIFY(ItemMetadataStore::findManualForBaseName("Other", dir.path()).isEmpty());
}

void TestItemMetadata::findManualPicksFirstExtensionInOrder() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Both pdf and epub exist; the order in manualExtensions() lists pdf first
  // so the resolver must return the pdf path. This pins the precedence so a
  // future reorder of the canonical list is a deliberate decision.
  const QString pdf = dir.filePath("Sonic.pdf");
  const QString epub = dir.filePath("Sonic.epub");
  for (const QString &p : {pdf, epub}) {
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
  }
  QCOMPARE(ItemMetadataStore::findManualForBaseName("Sonic", dir.path()), pdf);
}

void TestItemMetadata::resolveManualFilePrefersValidOverride() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString autoPath = dir.filePath("Sonic.pdf");
  const QString overridePath = dir.filePath("CustomManual.pdf");
  for (const QString &p : {autoPath, overridePath}) {
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
  }
  // Override exists -> auto-discovery is skipped entirely.
  QCOMPARE(ItemMetadataStore::resolveManualFile(overridePath, "Sonic", dir.path()), overridePath);
}

void TestItemMetadata::resolveManualFileFallsBackToAutoDiscovery() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString autoPath = dir.filePath("Sonic.pdf");
  QFile f(autoPath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();
  // No override -> auto-discovery picks up the directory match.
  QCOMPARE(ItemMetadataStore::resolveManualFile(QString(), "Sonic", dir.path()), autoPath);
  // Whitespace-only override is treated as unset.
  QCOMPARE(ItemMetadataStore::resolveManualFile("   ", "Sonic", dir.path()), autoPath);
}

void TestItemMetadata::resolveManualFileMissingOverrideReturnsEmpty() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // An auto-discoverable manual exists, but the user set a stale override
  // pointing at a non-existent file. We deliberately do NOT fall back to the
  // auto-discovered file -- silent fallback would hide a typo'd override and
  // make it hard to notice the override is broken.
  const QString autoPath = dir.filePath("Sonic.pdf");
  QFile f(autoPath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();
  const QString stale = dir.filePath("MissingManual.pdf");
  QVERIFY(ItemMetadataStore::resolveManualFile(stale, "Sonic", dir.path()).isEmpty());
}

void TestItemMetadata::resolveManualFileTildeOnlyExpandsHomePrefix() {
#ifdef Q_OS_WIN
  QSKIP("QDir::homePath() resolves via USERPROFILE on Windows; the HOME override has no effect.");
#else
  // Point HOME at a private directory inside a temp root so the "~/" case can
  // plant a real file without touching the developer's home directory (Unix
  // QDir::homePath() re-reads HOME on every call, so a scoped override works).
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString home = root.filePath("home");
  QVERIFY(QDir().mkpath(home));
  const QByteArray oldHome = qgetenv("HOME");
  qputenv("HOME", home.toUtf8());
  const auto restoreHome = qScopeGuard([&oldHome]() { qputenv("HOME", oldHome); });

  QFile manual(home + "/Manual.pdf");
  QVERIFY(manual.open(QIODevice::WriteOnly));
  manual.close();
  // A "~/" override expands to the home directory.
  QCOMPARE(ItemMetadataStore::resolveManualFile("~/Manual.pdf", "Guide", QString()),
           home + "/Manual.pdf");

  // Regression: the old inline expansion turned ANY leading '~' into the home
  // path, so "~backup/..." wrongly resolved to "<home>backup/...". Plant a
  // file at exactly that wrong location and verify the override does NOT
  // resolve: a tilde not followed by a slash is a literal file name.
  const QString wrongDir = home + "backup";
  QVERIFY(QDir().mkpath(wrongDir));
  QFile wrong(wrongDir + "/Manual.pdf");
  QVERIFY(wrong.open(QIODevice::WriteOnly));
  wrong.close();
  QVERIFY(ItemMetadataStore::resolveManualFile("~backup/Manual.pdf", "Guide", QString()).isEmpty());

  // Same rules apply to the auto-discovery directory: "~/manuals" expands...
  const QString manualsDir = home + "/manuals";
  QVERIFY(QDir().mkpath(manualsDir));
  QFile autoManual(manualsDir + "/Guide.pdf");
  QVERIFY(autoManual.open(QIODevice::WriteOnly));
  autoManual.close();
  QCOMPARE(ItemMetadataStore::resolveManualFile(QString(), "Guide", "~/manuals"),
           manualsDir + "/Guide.pdf");
  // ...while a tilde-prefixed directory name stays literal and matches nothing.
  QVERIFY(ItemMetadataStore::resolveManualFile(QString(), "Guide", "~manuals").isEmpty());
#endif
}

QTEST_MAIN(TestItemMetadata)
#include "test_itemmetadata.moc"
