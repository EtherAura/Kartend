// ItchLibrary reads against a real on-disk butler.db staged per test (no DB
// mocking, per CONTRIBUTING): the caves→games join, the classification filter,
// schema tolerance, and the launch URI (Kartend-4cff2).
#include <QDir>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "itchlibrary.h"

class TestItchLibrary : public QObject {
  Q_OBJECT

private slots:
  void missingDatabaseIsAnError();
  void readsInstalledCavesWithTitles();
  void toleratesMissingGamesTable();
  void launchUriUsesCaveId();

private:
  QTemporaryDir m_dir;
  int m_dbCounter = 0;

  /// Creates `<configDir>/db/butler.db` with butler's caves/games shape and
  /// returns the config dir, or an empty string when staging failed.
  /// `withGames` omits the games table entirely, which is what a freshly
  /// migrated or partially-populated database looks like.
  QString stageDatabase(bool withGames) {
    const QString configDir = m_dir.filePath(QStringLiteral("itch%1").arg(m_dbCounter++));
    QDir().mkpath(configDir + QStringLiteral("/db"));
    const QString connection = QStringLiteral("test_butler_%1").arg(m_dbCounter);
    bool ok = true;
    {
      QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
      db.setDatabaseName(configDir + QStringLiteral("/db/butler.db"));
      ok = db.open();
      if (ok) {
        QSqlQuery query(db);
        ok = query.exec(QStringLiteral(
            "CREATE TABLE caves (id TEXT PRIMARY KEY, game_id INTEGER, upload_id INTEGER, "
            "install_folder_name TEXT, installed_size INTEGER, verdict TEXT)"));
        const auto cave = [&query, &ok](const QString &id, int gameId, const QString &folder) {
          query.prepare(QStringLiteral(
              "INSERT INTO caves (id, game_id, install_folder_name) VALUES (?, ?, ?)"));
          query.addBindValue(id);
          query.addBindValue(gameId);
          query.addBindValue(folder);
          ok = ok && query.exec();
        };
        cave(QStringLiteral("cave-celeste"), 1, QStringLiteral("celeste"));
        cave(QStringLiteral("cave-tool"), 2, QStringLiteral("some-tool"));
        cave(QStringLiteral("cave-orphan"), 3, QStringLiteral("orphaned-folder"));
        if (withGames) {
          ok = ok && query.exec(QStringLiteral(
                         "CREATE TABLE games (id INTEGER PRIMARY KEY, title TEXT, url TEXT, "
                         "classification TEXT, cover_url TEXT, still_cover_url TEXT)"));
          const auto game = [&query, &ok](int id, const QString &title,
                                          const QString &classification) {
            query.prepare(
                QStringLiteral("INSERT INTO games (id, title, classification) VALUES (?, ?, ?)"));
            query.addBindValue(id);
            query.addBindValue(title);
            query.addBindValue(classification);
            ok = ok && query.exec();
          };
          game(1, QStringLiteral("A Free Game"), QStringLiteral("game"));
          // Installed, but not a game — itch hosts tools and asset packs too.
          game(2, QStringLiteral("A Pixel Art Tool"), QStringLiteral("tool"));
        }
        db.close();
      }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok ? configDir : QString();
  }
};

void TestItchLibrary::missingDatabaseIsAnError() {
  const auto result = ItchLibrary::installedGames(m_dir.filePath(QStringLiteral("nowhere")));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestItchLibrary::readsInstalledCavesWithTitles() {
  const QString configDir = stageDatabase(/*withGames=*/true);
  QVERIFY(!configDir.isEmpty());
  const auto result = ItchLibrary::installedGames(configDir);
  QVERIFY(!result.isError());
  QCOMPARE(result.value().size(), 2);
  // Sorted by title. The tool is filtered out; the cave whose metadata row is
  // missing keeps its install folder as a title rather than vanishing.
  QCOMPARE(result.value().at(0).title, QStringLiteral("A Free Game"));
  QCOMPARE(result.value().at(0).caveId, QStringLiteral("cave-celeste"));
  QCOMPARE(result.value().at(1).title, QStringLiteral("orphaned-folder"));
}

void TestItchLibrary::toleratesMissingGamesTable() {
  const QString configDir = stageDatabase(/*withGames=*/false);
  QVERIFY(!configDir.isEmpty());
  const auto result = ItchLibrary::installedGames(configDir);
  // Every cave survives on its install folder name: no metadata is not
  // evidence that the user did not install something.
  QVERIFY(!result.isError());
  QCOMPARE(result.value().size(), 3);
  QCOMPARE(result.value().at(0).title, QStringLiteral("celeste"));
}

void TestItchLibrary::launchUriUsesCaveId() {
  ItchLibrary::Game game;
  game.caveId = QStringLiteral("6ad5f0ff-4b53-4b9c-9fca-d1a5bd0e6d0c");
  QCOMPARE(ItchLibrary::launchUri(game),
           QStringLiteral("itch://caves/6ad5f0ff-4b53-4b9c-9fca-d1a5bd0e6d0c/launch"));
}

QTEST_MAIN(TestItchLibrary)
#include "test_itchlibrary.moc"
