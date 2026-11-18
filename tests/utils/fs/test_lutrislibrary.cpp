// LutrisLibrary reads against a real on-disk SQLite pga.db staged per test
// (no DB mocking, per CONTRIBUTING) plus banner/coverart file lookups.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "lutrislibrary.h"

class TestLutrisLibrary : public QObject {
  Q_OBJECT

private slots:
  void missingDatabaseIsAnError();
  void readsInstalledNonHiddenGames();
  void toleratesMissingHiddenColumn();
  void artworkPrefersCoverart();
  void traversingSlugYieldsNoArtwork();
  void readsLutris0522Profile();

private:
  QTemporaryDir m_dir;
  int m_dbCounter = 0;

  /// Creates `<dataDir>/pga.db` with a `games` table (real SQLite on disk)
  /// and returns the data dir, or an empty string when staging failed (the
  /// caller QVERIFYs — QVERIFY itself can't be used here because this
  /// helper doesn't return void). `withHiddenColumn` mimics the pre-0.5.x
  /// schema when false.
  QString stageDatabase(bool withHiddenColumn) {
    const QString dataDir = m_dir.filePath(QStringLiteral("lutris%1").arg(m_dbCounter++));
    QDir().mkpath(dataDir);
    const QString connection = QStringLiteral("test_pga_%1").arg(m_dbCounter);
    bool ok = true;
    {
      QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
      db.setDatabaseName(dataDir + QStringLiteral("/pga.db"));
      ok = db.open();
      if (ok) {
        QSqlQuery query(db);
        const QString hiddenCol = withHiddenColumn ? QStringLiteral(", hidden INTEGER") : QString();
        ok = query.exec(QStringLiteral("CREATE TABLE games (id INTEGER PRIMARY KEY, name TEXT, "
                                       "slug TEXT, runner TEXT, installed INTEGER%1)")
                            .arg(hiddenCol));
        const auto insert = [&query, &ok,
                             withHiddenColumn](const QString &name, const QString &slug,
                                               const QString &runner, int installed, int hidden) {
          if (withHiddenColumn) {
            query.prepare(QStringLiteral("INSERT INTO games (name, slug, runner, installed, "
                                         "hidden) VALUES (?, ?, ?, ?, ?)"));
          } else {
            query.prepare(QStringLiteral(
                "INSERT INTO games (name, slug, runner, installed) VALUES (?, ?, ?, ?)"));
          }
          query.addBindValue(name);
          query.addBindValue(slug);
          query.addBindValue(runner);
          query.addBindValue(installed);
          if (withHiddenColumn) {
            query.addBindValue(hidden);
          }
          ok = ok && query.exec();
        };
        insert(QStringLiteral("Celeste"), QStringLiteral("celeste"), QStringLiteral("linux"), 1, 0);
        insert(QStringLiteral("Not Installed"), QStringLiteral("not-installed"),
               QStringLiteral("wine"), 0, 0);
        if (withHiddenColumn) {
          insert(QStringLiteral("Hidden Game"), QStringLiteral("hidden-game"),
                 QStringLiteral("wine"), 1, 1);
        }
        insert(QStringLiteral("A Wine Game"), QStringLiteral("a-wine-game"), QStringLiteral("wine"),
               1, 0);
        db.close();
      }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok ? dataDir : QString();
  }
};

void TestLutrisLibrary::missingDatabaseIsAnError() {
  const auto result = LutrisLibrary::installedGames(m_dir.filePath(QStringLiteral("nowhere")));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestLutrisLibrary::readsInstalledNonHiddenGames() {
  const QString dataDir = stageDatabase(/*withHiddenColumn=*/true);
  QVERIFY(!dataDir.isEmpty());
  const auto result = LutrisLibrary::installedGames(dataDir);
  QVERIFY(!result.isError());
  QCOMPARE(result.value().size(), 2);
  // Sorted by name; uninstalled + hidden rows filtered.
  QCOMPARE(result.value().at(0).name, QStringLiteral("A Wine Game"));
  QCOMPARE(result.value().at(1).slug, QStringLiteral("celeste"));
  QCOMPARE(result.value().at(1).runner, QStringLiteral("linux"));
}

void TestLutrisLibrary::toleratesMissingHiddenColumn() {
  const QString dataDir = stageDatabase(/*withHiddenColumn=*/false);
  QVERIFY(!dataDir.isEmpty());
  const auto result = LutrisLibrary::installedGames(dataDir);
  QVERIFY(!result.isError());
  QCOMPARE(result.value().size(), 2);
}

void TestLutrisLibrary::artworkPrefersCoverart() {
  const QString dataDir = m_dir.filePath(QStringLiteral("art"));
  QDir().mkpath(dataDir + QStringLiteral("/coverart"));
  QDir().mkpath(dataDir + QStringLiteral("/banners"));
  const auto touch = [](const QString &path) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
  };
  touch(dataDir + QStringLiteral("/coverart/celeste.jpg"));
  touch(dataDir + QStringLiteral("/banners/celeste.jpg"));
  touch(dataDir + QStringLiteral("/banners/banner-only.jpg"));

  const LutrisLibrary::Artwork both = LutrisLibrary::artworkFor(dataDir, QStringLiteral("celeste"));
  QVERIFY(both.cover.endsWith(QStringLiteral("coverart/celeste.jpg")));
  QVERIFY(both.banner.endsWith(QStringLiteral("banners/celeste.jpg")));

  const LutrisLibrary::Artwork bannerOnly =
      LutrisLibrary::artworkFor(dataDir, QStringLiteral("banner-only"));
  QVERIFY(bannerOnly.cover.isEmpty());
  QVERIFY(!bannerOnly.banner.isEmpty());
}

// Kartend-9guwj: slug is third-party input (a pga.db row) interpolated as a
// single path component. Before the isSafePathComponent guard, a traversing
// slug made this resolve to a real file OUTSIDE the Lutris data dir, which the
// importer then copied in as the game's cover — an information-disclosure
// primitive that surfaces a file the user never chose to share.
void TestLutrisLibrary::traversingSlugYieldsNoArtwork() {
  const QString dataDir = m_dir.filePath(QStringLiteral("escape"));
  QDir().mkpath(dataDir + QStringLiteral("/coverart"));
  // The "private" file the hostile slug aims at, one level above dataDir —
  // deliberately named .jpg because the caller appends the extension.
  const QString outsideDir = m_dir.filePath(QStringLiteral("outside"));
  QDir().mkpath(outsideDir);
  {
    QFile f(outsideDir + QStringLiteral("/private.jpg"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("secret");
  }
  // Premise: the traversal really does name that file, so a pass here would be
  // a genuine read of it rather than a miss.
  QVERIFY(QFileInfo::exists(dataDir + QStringLiteral("/coverart/") +
                            QStringLiteral("../../outside/private") + QStringLiteral(".jpg")));

  for (const QString &hostile :
       {QStringLiteral("../../outside/private"), QStringLiteral("../outside/private"),
        QStringLiteral("/etc/hostname"), QStringLiteral(".."), QStringLiteral("."), QString()}) {
    const LutrisLibrary::Artwork art = LutrisLibrary::artworkFor(dataDir, hostile);
    QVERIFY2(art.cover.isEmpty(),
             qPrintable(QStringLiteral("slug '%1' produced cover '%2'").arg(hostile, art.cover)));
    QVERIFY2(art.banner.isEmpty(),
             qPrintable(QStringLiteral("slug '%1' produced banner '%2'").arg(hostile, art.banner)));
  }
}

// Kartend-8n6id: a fresh Lutris 0.5.22 profile. The games table below follows
// the column list of that release's schema (no `hidden` column in a newly
// created database), and the media sits in the default DATA_DIR layout:
// pga.db, coverart/<slug>.{jpg,png}, banners/<slug>.{jpg,png}. A cover picked
// by URL in Lutris' game-info dialog is downloaded and saved by Lutris itself
// through the same custom-media path, so to Kartend it is just a local file
// under coverart/ with has_custom_coverart_big=1 on the row.
void TestLutrisLibrary::readsLutris0522Profile() {
  const QString dataDir = m_dir.filePath(QStringLiteral("lutris-0.5.22"));
  QVERIFY(QDir().mkpath(dataDir + QStringLiteral("/coverart")));
  QVERIFY(QDir().mkpath(dataDir + QStringLiteral("/banners")));

  const QString connection = QStringLiteral("test_pga_0522");
  bool ok = true;
  bool hasHiddenColumn = true;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(dataDir + QStringLiteral("/pga.db"));
    ok = db.open();
    if (ok) {
      QSqlQuery query(db);
      ok = query.exec(QStringLiteral(
          "CREATE TABLE games (id INTEGER PRIMARY KEY, name TEXT, sortname TEXT, slug TEXT, "
          "installer_slug TEXT, parent_slug TEXT, platform TEXT, runner TEXT, executable TEXT, "
          "directory TEXT, updated DATETIME, lastplayed INTEGER, installed INTEGER, "
          "installed_at INTEGER, year INTEGER, configpath TEXT, has_custom_banner INTEGER, "
          "has_custom_icon INTEGER, has_custom_coverart_big INTEGER, playtime REAL, "
          "service TEXT, service_id TEXT, discord_id TEXT)"));
      const auto insert = [&query, &ok](const QString &name, const QString &slug,
                                        const QString &runner, int installed, int customBanner,
                                        int customCover) {
        query.prepare(QStringLiteral(
            "INSERT INTO games (name, sortname, slug, installer_slug, platform, runner, "
            "directory, updated, lastplayed, installed, installed_at, year, configpath, "
            "has_custom_banner, has_custom_icon, has_custom_coverart_big, playtime) "
            "VALUES (?, '', ?, ?, 'Linux', ?, ?, '2025-01-01 00:00:00', 0, ?, 1735689600, "
            "2020, ?, ?, 0, ?, 1.5)"));
        query.addBindValue(name);
        query.addBindValue(slug);
        query.addBindValue(slug + QStringLiteral("-installer"));
        query.addBindValue(runner);
        query.addBindValue(QStringLiteral("/games/") + slug);
        query.addBindValue(installed);
        query.addBindValue(slug + QStringLiteral("-1735689600"));
        query.addBindValue(customBanner);
        query.addBindValue(customCover);
        ok = ok && query.exec();
      };
      // Standard media, as fetched by Lutris from its own service.
      insert(QStringLiteral("Synthetic Standard"), QStringLiteral("synthetic-standard"),
             QStringLiteral("linux"), 1, 0, 0);
      // Custom cover and banner the user chose (e.g. by URL), saved as PNG.
      insert(QStringLiteral("Custom Cover Game"), QStringLiteral("custom-cover-game"),
             QStringLiteral("wine"), 1, 1, 1);
      // Uninstalled, but with media on disk: must still be omitted.
      insert(QStringLiteral("Uninstalled Game"), QStringLiteral("uninstalled-game"),
             QStringLiteral("wine"), 0, 0, 0);

      QSqlQuery columns(db);
      ok = ok && columns.exec(QStringLiteral("PRAGMA table_info(games)"));
      hasHiddenColumn = false;
      while (ok && columns.next()) {
        if (columns.value(1).toString() == QStringLiteral("hidden")) {
          hasHiddenColumn = true;
        }
      }
      db.close();
    }
  }
  QSqlDatabase::removeDatabase(connection);
  QVERIFY(ok);
  // Premise: this is the fresh-profile shape, not an upgraded one.
  QVERIFY(!hasHiddenColumn);

  const auto touch = [](const QString &path) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("img");
  };
  touch(dataDir + QStringLiteral("/coverart/synthetic-standard.jpg"));
  touch(dataDir + QStringLiteral("/banners/synthetic-standard.jpg"));
  touch(dataDir + QStringLiteral("/coverart/custom-cover-game.png"));
  touch(dataDir + QStringLiteral("/banners/custom-cover-game.png"));
  touch(dataDir + QStringLiteral("/coverart/uninstalled-game.jpg"));

  const auto result = LutrisLibrary::installedGames(dataDir);
  QVERIFY(!result.isError());
  const QList<LutrisLibrary::Game> games = result.value();
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).name, QStringLiteral("Custom Cover Game"));
  QCOMPARE(games.at(0).slug, QStringLiteral("custom-cover-game"));
  QCOMPARE(games.at(0).runner, QStringLiteral("wine"));
  QCOMPARE(games.at(1).name, QStringLiteral("Synthetic Standard"));
  QCOMPARE(games.at(1).slug, QStringLiteral("synthetic-standard"));
  QCOMPARE(games.at(1).runner, QStringLiteral("linux"));
  for (const LutrisLibrary::Game &game : games) {
    QVERIFY(game.slug != QStringLiteral("uninstalled-game"));
  }

  const LutrisLibrary::Artwork standard = LutrisLibrary::artworkFor(dataDir, games.at(1).slug);
  QCOMPARE(
      QFileInfo(standard.cover).canonicalFilePath(),
      QFileInfo(dataDir + QStringLiteral("/coverart/synthetic-standard.jpg")).canonicalFilePath());
  QCOMPARE(
      QFileInfo(standard.banner).canonicalFilePath(),
      QFileInfo(dataDir + QStringLiteral("/banners/synthetic-standard.jpg")).canonicalFilePath());

  const LutrisLibrary::Artwork custom = LutrisLibrary::artworkFor(dataDir, games.at(0).slug);
  QCOMPARE(
      QFileInfo(custom.cover).canonicalFilePath(),
      QFileInfo(dataDir + QStringLiteral("/coverart/custom-cover-game.png")).canonicalFilePath());
  QCOMPARE(
      QFileInfo(custom.banner).canonicalFilePath(),
      QFileInfo(dataDir + QStringLiteral("/banners/custom-cover-game.png")).canonicalFilePath());
  QVERIFY(!custom.cover.isEmpty());
  QVERIFY(!custom.banner.isEmpty());
}

QTEST_MAIN(TestLutrisLibrary)
#include "test_lutrislibrary.moc"
