// LutrisLibrary reads against a real on-disk SQLite pga.db staged per test
// (no DB mocking, per CONTRIBUTING) plus banner/coverart file lookups.
#include <QDir>
#include <QFile>
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

QTEST_MAIN(TestLutrisLibrary)
#include "test_lutrislibrary.moc"
