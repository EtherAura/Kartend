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
  void launchUriPercentEncodesCaveId();
  void coverUrlPrefersTheStill();

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
          const auto game = [&query,
                             &ok](int id, const QString &title, const QString &classification,
                                  const QString &coverUrl = {}, const QString &stillUrl = {}) {
            query.prepare(QStringLiteral("INSERT INTO games (id, title, classification, "
                                         "cover_url, still_cover_url) VALUES (?, ?, ?, ?, ?)"));
            query.addBindValue(id);
            query.addBindValue(title);
            query.addBindValue(classification);
            query.addBindValue(coverUrl);
            query.addBindValue(stillUrl);
            ok = ok && query.exec();
          };
          // An animated cover carries BOTH; the still is the one to use.
          game(1, QStringLiteral("A Free Game"), QStringLiteral("game"),
               QStringLiteral("https://img.itch.zone/1/animated.gif"),
               QStringLiteral("https://img.itch.zone/1/still.png"));
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

// Kartend-9guwj: caveId comes raw from butler.db. Unencoded, a value carrying
// '?', '#' or '/' reshapes the URI — the trailing "/launch" ends up in a query
// string or fragment, or the cave path gains segments. HeroicLibrary::launchUri
// already encoded its interpolated values; these two adjacent functions
// disagreed.
void TestItchLibrary::launchUriPercentEncodesCaveId() {
  const auto uriFor = [](const QString &caveId) {
    ItchLibrary::Game game;
    game.caveId = caveId;
    return ItchLibrary::launchUri(game);
  };

  // '?' would otherwise start a query and swallow "/launch" out of the path.
  QCOMPARE(uriFor(QStringLiteral("abc?x=1")), QStringLiteral("itch://caves/abc%3Fx%3D1/launch"));
  // '#' would otherwise start a fragment, same effect.
  QCOMPARE(uriFor(QStringLiteral("abc#frag")), QStringLiteral("itch://caves/abc%23frag/launch"));
  // '/' would otherwise add a path segment.
  QCOMPARE(uriFor(QStringLiteral("a/b")), QStringLiteral("itch://caves/a%2Fb/launch"));

  // An ordinary uuid must survive byte-for-byte — encoding must not change the
  // normal case, which is what launchUriUsesCaveId pins above.
  const QString uuid = QStringLiteral("6ad5f0ff-4b53-4b9c-9fca-d1a5bd0e6d0c");
  QCOMPARE(uriFor(uuid), QStringLiteral("itch://caves/%1/launch").arg(uuid));
}

// Kartend-g1g30: itch keeps covers as URLs only. still_cover_url is populated
// only when the cover is animated, and is what itch itself shows in listings,
// so it wins — a GIF would otherwise land in the grid as a static first frame.
void TestItchLibrary::coverUrlPrefersTheStill() {
  const QString configDir = stageDatabase(/*withGames=*/true);
  QVERIFY(!configDir.isEmpty());
  const auto result = ItchLibrary::installedGames(configDir);
  QVERIFY(!result.isError());
  QCOMPARE(result.value().at(0).title, QStringLiteral("A Free Game"));
  QCOMPARE(result.value().at(0).coverUrl, QStringLiteral("https://img.itch.zone/1/still.png"));
  // The cave whose metadata row is missing has no cover to offer, and says so
  // rather than inventing one.
  QVERIFY(result.value().at(1).coverUrl.isEmpty());
}

QTEST_MAIN(TestItchLibrary)
#include "test_itchlibrary.moc"
