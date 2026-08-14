// Tests for ItemArtworkStore::load / save / remove and the standard-type
// resolution helpers against an in-memory SQLite database with the v6 schema
// applied via DbMigrations.
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "dbmigrations.h"
#include "itemartwork.h"

using ItemArtworkStore::ItemArtwork;

namespace {

QSqlDatabase openMemoryDb(const QString &conn) {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    return db;
  }
  // Stub the minimum base schema the migrations expect; v6 only adds a brand-
  // new table but earlier migrations touch collections/items.
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

bool touchFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.close();
  return true;
}

} // namespace

class TestItemArtwork : public QObject {
  Q_OBJECT
private slots:
  void standardTypesAreOrderedAndStable();
  void isStandardTypeRecognisesCanonicalIds();
  void displayNameMatchesStandardTypes();
  void loadReturnsKeyedRowForMissingEntry();
  void saveAndLoadRoundTrip();
  void saveOverwritesExistingRow();
  void distinguishesByArtworkType();
  void loadAllForItemReturnsAllRowsInInsertionOrder();
  void removeDeletesOnlyTargetedRow();
  void saveRejectsEmptyPathOrType();
  void findStandardArtworkLooksInTypeSubdirectory();
  void findStandardArtworkRejectsNonStandardTypes();
  void findStandardArtworkMatchesCaseInsensitiveExtensions();
  void resolveArtworkPathPrefersValidOverride();
  void resolveArtworkPathFallsBackForStandardTypes();
  void resolveArtworkPathMissingOverrideReturnsEmpty();
  void resolveArtworkPathCustomTypeOnlyHonoursOverride();
  void resolveArtworkPathTildeOnlyExpandsHomePrefix();
  void resolveCoverPathFallsBackToAutoDiscoveryWithNoLinks();
  void resolveCoverPathPrefersAManualCoverLink();
  void resolveCoverPathSkipsALinkWhoseFileIsGone();
  void resolveCoverPathIgnoresGalleryOnlyTypes();
  void resolveCoverPathFollowsCoverTypePriority();
};

void TestItemArtwork::standardTypesAreOrderedAndStable() {
  // The order is meaningful — sidebar gallery renders in this sequence and
  // the strings double as on-disk subdirectory names. A reorder or rename
  // here is breakage; this test pins the contract. "front" is the cross-
  // provider primary-cover id and leads the list; "box" follows for
  // libraries with hand-curated cover art.
  const auto &types = ItemArtworkStore::standardTypes();
  QCOMPARE(types.size(), 7);
  QCOMPARE(types[0], QStringLiteral("front"));
  QCOMPARE(types[1], QStringLiteral("box"));
  QCOMPARE(types[2], QStringLiteral("screenshot"));
  QCOMPARE(types[3], QStringLiteral("title"));
  QCOMPARE(types[4], QStringLiteral("marquee"));
  QCOMPARE(types[5], QStringLiteral("fanart"));
  QCOMPARE(types[6], QStringLiteral("logo"));
}

void TestItemArtwork::isStandardTypeRecognisesCanonicalIds() {
  for (const QString &id : ItemArtworkStore::standardTypes()) {
    QVERIFY(ItemArtworkStore::isStandardType(id));
  }
  QVERIFY(!ItemArtworkStore::isStandardType(QString()));
  QVERIFY(!ItemArtworkStore::isStandardType("Box")); // case-sensitive
  QVERIFY(!ItemArtworkStore::isStandardType("concept_art"));
}

void TestItemArtwork::displayNameMatchesStandardTypes() {
  QCOMPARE(ItemArtworkStore::standardTypeDisplayName("box"), QStringLiteral("Box"));
  QCOMPARE(ItemArtworkStore::standardTypeDisplayName("screenshot"), QStringLiteral("Screenshot"));
  QCOMPARE(ItemArtworkStore::standardTypeDisplayName("title"), QStringLiteral("Title Screen"));
  QCOMPARE(ItemArtworkStore::standardTypeDisplayName("marquee"), QStringLiteral("Marquee"));
  QCOMPARE(ItemArtworkStore::standardTypeDisplayName("fanart"), QStringLiteral("Fan Art"));
  QCOMPARE(ItemArtworkStore::standardTypeDisplayName("logo"), QStringLiteral("Logo"));
  // Non-standard types yield empty so callers can supply their own label.
  QVERIFY(ItemArtworkStore::standardTypeDisplayName("concept_art").isEmpty());
  QVERIFY(ItemArtworkStore::standardTypeDisplayName(QString()).isEmpty());
}

void TestItemArtwork::loadReturnsKeyedRowForMissingEntry() {
  const QString conn = "ia_load_missing";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());

  auto result = ItemArtworkStore::load(db, "uuid-1", "/missing", "box");
  QVERIFY(result.isOk());
  ItemArtwork a = result.value();
  QCOMPARE(a.collectionUuid, QStringLiteral("uuid-1"));
  QCOMPARE(a.path, QStringLiteral("/missing"));
  QCOMPARE(a.artworkType, QStringLiteral("box"));
  QVERIFY(a.manualPath.isEmpty());
  QVERIFY(a.updatedAt.isEmpty());

  closeAndRemove(db, conn);
}

void TestItemArtwork::saveAndLoadRoundTrip() {
  const QString conn = "ia_round_trip";
  auto db = openMemoryDb(conn);

  ItemArtwork a;
  a.collectionUuid = "uuid-1";
  a.path = "/games/sonic.bin";
  a.artworkType = "box";
  a.manualPath = "/art/box/sonic.png";
  QVERIFY(ItemArtworkStore::save(db, a).isOk());

  auto loaded = ItemArtworkStore::load(db, "uuid-1", "/games/sonic.bin", "box");
  QVERIFY(loaded.isOk());
  ItemArtwork r = loaded.value();
  QCOMPARE(r.collectionUuid, a.collectionUuid);
  QCOMPARE(r.path, a.path);
  QCOMPARE(r.artworkType, a.artworkType);
  QCOMPARE(r.manualPath, a.manualPath);
  QVERIFY(!r.updatedAt.isEmpty()); // save() stamps current UTC ISO time

  closeAndRemove(db, conn);
}

void TestItemArtwork::saveOverwritesExistingRow() {
  const QString conn = "ia_upsert";
  auto db = openMemoryDb(conn);

  ItemArtwork a;
  a.collectionUuid = "uuid-1";
  a.path = "/p";
  a.artworkType = "box";
  a.manualPath = "/first.png";
  QVERIFY(ItemArtworkStore::save(db, a).isOk());

  a.manualPath = "/second.png";
  QVERIFY(ItemArtworkStore::save(db, a).isOk());

  QCOMPARE(ItemArtworkStore::load(db, "uuid-1", "/p", "box").value().manualPath,
           QStringLiteral("/second.png"));

  // Only one row should exist for the (uuid, path, type) triple.
  QSqlQuery q(db);
  QVERIFY(q.exec("SELECT COUNT(*) FROM item_artwork"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  closeAndRemove(db, conn);
}

void TestItemArtwork::distinguishesByArtworkType() {
  const QString conn = "ia_by_type";
  auto db = openMemoryDb(conn);

  ItemArtwork box;
  box.collectionUuid = "uuid-1";
  box.path = "/p";
  box.artworkType = "box";
  box.manualPath = "/box.png";
  QVERIFY(ItemArtworkStore::save(db, box).isOk());

  ItemArtwork shot;
  shot.collectionUuid = "uuid-1";
  shot.path = "/p";
  shot.artworkType = "screenshot";
  shot.manualPath = "/shot.png";
  QVERIFY(ItemArtworkStore::save(db, shot).isOk());

  QCOMPARE(ItemArtworkStore::load(db, "uuid-1", "/p", "box").value().manualPath,
           QStringLiteral("/box.png"));
  QCOMPARE(ItemArtworkStore::load(db, "uuid-1", "/p", "screenshot").value().manualPath,
           QStringLiteral("/shot.png"));

  closeAndRemove(db, conn);
}

void TestItemArtwork::loadAllForItemReturnsAllRowsInInsertionOrder() {
  const QString conn = "ia_load_all";
  auto db = openMemoryDb(conn);

  // Insert in non-canonical order; loadAllForItem orders by id (insertion),
  // not by the standardTypes() priority. Callers that want display order
  // should re-sort themselves so the storage stays index-friendly.
  for (const QString &type :
       {QStringLiteral("logo"), QStringLiteral("box"), QStringLiteral("screenshot")}) {
    ItemArtwork a;
    a.collectionUuid = "uuid-1";
    a.path = "/p";
    a.artworkType = type;
    a.manualPath = "/" + type + ".png";
    QVERIFY(ItemArtworkStore::save(db, a).isOk());
  }

  auto loaded = ItemArtworkStore::loadAllForItem(db, "uuid-1", "/p");
  QVERIFY(loaded.isOk());
  const auto rows = loaded.value();
  QCOMPARE(rows.size(), 3);
  QCOMPARE(rows[0].artworkType, QStringLiteral("logo"));
  QCOMPARE(rows[1].artworkType, QStringLiteral("box"));
  QCOMPARE(rows[2].artworkType, QStringLiteral("screenshot"));

  // Empty for an unknown item.
  auto none = ItemArtworkStore::loadAllForItem(db, "uuid-1", "/missing");
  QVERIFY(none.isOk());
  QVERIFY(none.value().isEmpty());

  closeAndRemove(db, conn);
}

void TestItemArtwork::removeDeletesOnlyTargetedRow() {
  const QString conn = "ia_remove_one";
  auto db = openMemoryDb(conn);

  for (const QString &type : {QStringLiteral("box"), QStringLiteral("screenshot")}) {
    ItemArtwork a;
    a.collectionUuid = "uuid-1";
    a.path = "/p";
    a.artworkType = type;
    a.manualPath = "/" + type + ".png";
    QVERIFY(ItemArtworkStore::save(db, a).isOk());
  }

  QVERIFY(ItemArtworkStore::remove(db, "uuid-1", "/p", "box").isOk());
  // Removing again must succeed (idempotent).
  QVERIFY(ItemArtworkStore::remove(db, "uuid-1", "/p", "box").isOk());

  QVERIFY(ItemArtworkStore::load(db, "uuid-1", "/p", "box").value().manualPath.isEmpty());
  QCOMPARE(ItemArtworkStore::load(db, "uuid-1", "/p", "screenshot").value().manualPath,
           QStringLiteral("/screenshot.png"));

  closeAndRemove(db, conn);
}

void TestItemArtwork::saveRejectsEmptyPathOrType() {
  const QString conn = "ia_reject_empty";
  auto db = openMemoryDb(conn);

  ItemArtwork noPath;
  noPath.collectionUuid = "uuid-1";
  noPath.artworkType = "box";
  auto pathResult = ItemArtworkStore::save(db, noPath);
  QVERIFY(pathResult.isError());
  QCOMPARE(pathResult.error().code, ErrorUtils::ErrorCode::InvalidArgument);

  ItemArtwork noType;
  noType.collectionUuid = "uuid-1";
  noType.path = "/p";
  auto typeResult = ItemArtworkStore::save(db, noType);
  QVERIFY(typeResult.isError());
  QCOMPARE(typeResult.error().code, ErrorUtils::ErrorCode::InvalidArgument);

  closeAndRemove(db, conn);
}

void TestItemArtwork::findStandardArtworkLooksInTypeSubdirectory() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Layout: {artDir}/box/Sonic.png — the subdirectory IS the type id.
  QDir(dir.path()).mkdir("box");
  const QString boxPath = dir.filePath("box/Sonic.png");
  QVERIFY(touchFile(boxPath));

  QCOMPARE(ItemArtworkStore::findStandardArtwork("Sonic", dir.path(), "box"), boxPath);
  // Different basename -> no match (we don't substring-match).
  QVERIFY(ItemArtworkStore::findStandardArtwork("Other", dir.path(), "box").isEmpty());
  // Type subdirectory missing -> no match (no recursion to root).
  QVERIFY(ItemArtworkStore::findStandardArtwork("Sonic", dir.path(), "marquee").isEmpty());
}

void TestItemArtwork::findStandardArtworkRejectsNonStandardTypes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Even if the user creates a custom-named subdir on disk, find() refuses it.
  // Custom types route through manual-link only — that contract is enforced
  // here, not at the call site.
  QDir(dir.path()).mkdir("concept_art");
  QVERIFY(touchFile(dir.filePath("concept_art/Sonic.png")));

  QVERIFY(ItemArtworkStore::findStandardArtwork("Sonic", dir.path(), "concept_art").isEmpty());
  QVERIFY(ItemArtworkStore::findStandardArtwork("Sonic", dir.path(), QString()).isEmpty());
  QVERIFY(ItemArtworkStore::findStandardArtwork(QString(), dir.path(), "box").isEmpty());
  QVERIFY(ItemArtworkStore::findStandardArtwork("Sonic", QString(), "box").isEmpty());
}

void TestItemArtwork::findStandardArtworkMatchesCaseInsensitiveExtensions() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QDir(dir.path()).mkdir("screenshot");
  // Drop a screenshot with an uppercase extension to exercise the upper-case
  // probe in findStandardArtwork.
  const QString upper = dir.filePath("screenshot/Sonic.PNG");
  QVERIFY(touchFile(upper));
  // On Windows's case-insensitive filesystem the matcher's lowercase
  // probe finds "Sonic.PNG" but returns the lowercase candidate string;
  // Qt's canonicalFilePath doesn't normalise case on Windows (only
  // resolves symlinks/dots), so compare via case-insensitive match plus
  // a file-exists guard.
  const QString found = ItemArtworkStore::findStandardArtwork("Sonic", dir.path(), "screenshot");
  QVERIFY2(QFile::exists(found), qPrintable(QString("result does not exist: %1").arg(found)));
  QVERIFY2(QString::compare(found, upper, Qt::CaseInsensitive) == 0,
           qPrintable(QString("found %1 != upper %2 (case-insensitive)").arg(found, upper)));
}

void TestItemArtwork::resolveArtworkPathPrefersValidOverride() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QDir(dir.path()).mkdir("box");
  const QString autoPath = dir.filePath("box/Sonic.png");
  const QString overridePath = dir.filePath("box/Custom.png");
  QVERIFY(touchFile(autoPath));
  QVERIFY(touchFile(overridePath));

  // Override wins, even when subdirectory auto-discovery would have returned
  // a different file.
  QCOMPARE(ItemArtworkStore::resolveArtworkPath(overridePath, "Sonic", dir.path(), "box"),
           overridePath);
}

void TestItemArtwork::resolveArtworkPathFallsBackForStandardTypes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QDir(dir.path()).mkdir("box");
  const QString autoPath = dir.filePath("box/Sonic.png");
  QVERIFY(touchFile(autoPath));

  // No override -> auto-discovery in {artDir}/{type}/ kicks in for standards.
  QCOMPARE(ItemArtworkStore::resolveArtworkPath(QString(), "Sonic", dir.path(), "box"), autoPath);
  // Whitespace-only override is treated as unset (matches the manual-link
  // resolver's behaviour).
  QCOMPARE(ItemArtworkStore::resolveArtworkPath("   ", "Sonic", dir.path(), "box"), autoPath);
}

void TestItemArtwork::resolveArtworkPathMissingOverrideReturnsEmpty() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QDir(dir.path()).mkdir("box");
  // An auto-discoverable file exists, but the user set a stale override
  // pointing at a non-existent path. We deliberately do NOT silently fall
  // back to auto-discovery — silent fallback would mask a typo'd override.
  const QString autoPath = dir.filePath("box/Sonic.png");
  QVERIFY(touchFile(autoPath));
  const QString stale = dir.filePath("box/Missing.png");
  QVERIFY(ItemArtworkStore::resolveArtworkPath(stale, "Sonic", dir.path(), "box").isEmpty());
}

void TestItemArtwork::resolveArtworkPathCustomTypeOnlyHonoursOverride() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // Custom types route through manual-link only — even if the user creates
  // a `concept_art/` subdirectory on disk it must NOT be auto-discovered.
  QDir(dir.path()).mkdir("concept_art");
  const QString shadowAuto = dir.filePath("concept_art/Sonic.png");
  QVERIFY(touchFile(shadowAuto));

  // No override + custom type -> empty, regardless of disk layout.
  QVERIFY(ItemArtworkStore::resolveArtworkPath(QString(), "Sonic", dir.path(), "concept_art")
              .isEmpty());

  // With a valid override the file resolves normally.
  const QString overridePath = dir.filePath("concept_art/Custom.png");
  QVERIFY(touchFile(overridePath));
  QCOMPARE(ItemArtworkStore::resolveArtworkPath(overridePath, "Sonic", dir.path(), "concept_art"),
           overridePath);
}

void TestItemArtwork::resolveArtworkPathTildeOnlyExpandsHomePrefix() {
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

  QVERIFY(touchFile(home + "/Cover.png"));
  // A "~/" override expands to the home directory.
  QCOMPARE(ItemArtworkStore::resolveArtworkPath("~/Cover.png", "Guide", QString(), "box"),
           home + "/Cover.png");

  // Regression: the old expandTilde turned ANY leading '~' into the home
  // path, so "~backup/..." wrongly resolved to "<home>backup/...". Plant a
  // file at exactly that wrong location and verify the override does NOT
  // resolve: a tilde not followed by a slash is a literal file name.
  const QString wrongDir = home + "backup";
  QVERIFY(QDir().mkpath(wrongDir));
  QVERIFY(touchFile(wrongDir + "/Cover.png"));
  QVERIFY(ItemArtworkStore::resolveArtworkPath("~backup/Cover.png", "Guide", QString(), "box")
              .isEmpty());

  // Same rules apply to the artwork directory used for auto-discovery.
  QVERIFY(QDir().mkpath(home + "/artwork/box"));
  QVERIFY(touchFile(home + "/artwork/box/Guide.png"));
  QCOMPARE(ItemArtworkStore::resolveArtworkPath(QString(), "Guide", "~/artwork", "box"),
           home + "/artwork/box/Guide.png");
  QVERIFY(ItemArtworkStore::resolveArtworkPath(QString(), "Guide", "~artwork", "box").isEmpty());
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// resolveCoverPath — the one rule that decides what `items.artwork_path` holds
// (Kartend-jkty9). The scan pass and the save-time write-through are both
// callers of this; drift between them would put the DB-side predicates back
// into disagreement with each other.
// ─────────────────────────────────────────────────────────────────────────────

void TestItemArtwork::resolveCoverPathFallsBackToAutoDiscoveryWithNoLinks() {
  // The overwhelmingly common case — an item the user has never hand-linked.
  QCOMPARE(ItemArtworkStore::resolveCoverPath({}, QStringLiteral("/art/Overture.png")),
           QStringLiteral("/art/Overture.png"));
  QVERIFY(ItemArtworkStore::resolveCoverPath({}, QString()).isEmpty());
}

void TestItemArtwork::resolveCoverPathPrefersAManualCoverLink() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString picked = QDir(dir.path()).filePath(QStringLiteral("picked.png"));
  QVERIFY(touchFile(picked));

  const QHash<QString, QString> links{{QStringLiteral("front"), picked}};
  // A link outranks auto-discovery, matching what the sidebar gallery renders.
  QCOMPARE(ItemArtworkStore::resolveCoverPath(links, QStringLiteral("/art/auto.png")), picked);
  // And it supplies a cover for an item auto-discovery found nothing for.
  QCOMPARE(ItemArtworkStore::resolveCoverPath(links, QString()), picked);
}

void TestItemArtwork::resolveCoverPathSkipsALinkWhoseFileIsGone() {
  const QHash<QString, QString> links{
      {QStringLiteral("front"), QStringLiteral("/nowhere/deleted.png")}};
  // Counting the row's mere existence would report a cover no render path can
  // resolve — the whole reason the check lives here rather than in a JOIN.
  QVERIFY(ItemArtworkStore::resolveCoverPath(links, QString()).isEmpty());
  // A stale link must not swallow the auto-discovered cover either.
  QCOMPARE(ItemArtworkStore::resolveCoverPath(links, QStringLiteral("/art/auto.png")),
           QStringLiteral("/art/auto.png"));
}

void TestItemArtwork::resolveCoverPathIgnoresGalleryOnlyTypes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString logo = QDir(dir.path()).filePath(QStringLiteral("badge.png"));
  QVERIFY(touchFile(logo));

  // `logo` is absent from ArtworkUtils::coverSubdirPriority(), so it is never
  // auto-discovered as a cover and a link on it is gallery-only.
  QVERIFY(
      ItemArtworkStore::resolveCoverPath({{QStringLiteral("logo"), logo}}, QString()).isEmpty());
  QVERIFY(ItemArtworkStore::resolveCoverPath({{QStringLiteral("mycustomtype"), logo}}, QString())
              .isEmpty());
}

void TestItemArtwork::resolveCoverPathFollowsCoverTypePriority() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString front = QDir(dir.path()).filePath(QStringLiteral("front.png"));
  const QString box = QDir(dir.path()).filePath(QStringLiteral("box.png"));
  const QString screenshot = QDir(dir.path()).filePath(QStringLiteral("shot.png"));
  QVERIFY(touchFile(front));
  QVERIFY(touchFile(box));
  QVERIFY(touchFile(screenshot));

  QCOMPARE(ItemArtworkStore::resolveCoverPath({{QStringLiteral("box"), box},
                                               {QStringLiteral("front"), front},
                                               {QStringLiteral("screenshot"), screenshot}},
                                              QString()),
           front);
  // With the primary slot unlinked the next cover type in the cascade answers.
  QCOMPARE(
      ItemArtworkStore::resolveCoverPath(
          {{QStringLiteral("box"), box}, {QStringLiteral("screenshot"), screenshot}}, QString()),
      box);
}

QTEST_MAIN(TestItemArtwork)
#include "test_itemartwork.moc"
