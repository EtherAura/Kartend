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

QTEST_MAIN(TestLutrisLibrary)
#include "test_lutrislibrary.moc"
