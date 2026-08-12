// HeroicLibrary against a staged Heroic config tree: the four runner library
// caches, the is_installed filter, runner attribution, the launch URI, and the
// two places Heroic happens to leave a usable icon on disk (Kartend-4cff2).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "heroiclibrary.h"

class TestHeroicLibrary : public QObject {
  Q_OBJECT

private slots:
  void readsInstalledGamesAcrossRunners();
  void skipsUnreadableCacheWithoutLosingOthers();
  void findsLocalIconsWhereHeroicLeavesThem();
  void launchUriIsPercentEncoded();
  void coverUrlPrefersPortraitAndSubstitutesExtPlaceholder();
  void emptyConfigDirYieldsNothing();

private:
  QTemporaryDir m_dir;
  int m_counter = 0;

  QString newConfigDir() {
    const QString dir = m_dir.filePath(QStringLiteral("heroic%1").arg(m_counter++));
    QDir().mkpath(dir);
    return dir;
  }

  static void writeFile(const QString &filePath, const QByteArray &content) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  /// One GameInfo record in the shape Heroic's library caches hold.
  static QByteArray game(const char *appName, const char *title, const char *runner, bool installed,
                         const QString &installPath = {}) {
    return QByteArrayLiteral("{\"app_name\":\"") + appName + QByteArrayLiteral("\",\"title\":\"") +
           title + QByteArrayLiteral("\",\"runner\":\"") + runner +
           QByteArrayLiteral("\",\"is_installed\":") + (installed ? "true" : "false") +
           QByteArrayLiteral(",\"art_square\":\"https://cdn.example.invalid/{ext}\"") +
           (installPath.isEmpty() ? QByteArray()
                                  : QByteArrayLiteral(",\"install\":{\"install_path\":\"") +
                                        installPath.toUtf8() + QByteArrayLiteral("\"}")) +
           QByteArrayLiteral("}");
  }
};

void TestHeroicLibrary::readsInstalledGamesAcrossRunners() {
  const QString configDir = newConfigDir();
  // Epic and Amazon hang their array off "library"; GOG and sideloads off
  // "games" — two different store helpers, one shape per file.
  writeFile(configDir + QStringLiteral("/store_cache/legendary_library.json"),
            QByteArrayLiteral("{\"library\":[") + game("Quail", "Kena", "legendary", true) +
                QByteArrayLiteral(",") + game("Snail", "Not Installed", "legendary", false) +
                QByteArrayLiteral("],\"__timestamp.library\":\"Tue Aug 11 2026\"}"));
  writeFile(configDir + QStringLiteral("/store_cache/gog_library.json"),
            QByteArrayLiteral("{\"games\":[") +
                game("1207658930", "Beneath a Steel Sky", "gog", true) + QByteArrayLiteral("]}"));
  writeFile(configDir + QStringLiteral("/store_cache/nile_library.json"),
            QByteArrayLiteral("{\"library\":[") + game("amzn1", "An Amazon Game", "nile", true) +
                QByteArrayLiteral("]}"));
  // A sideloaded entry with no runner field falls back to the file's runner.
  writeFile(configDir + QStringLiteral("/sideload_apps/library.json"),
            QByteArrayLiteral("{\"games\":[{\"app_name\":\"abc-123\",\"title\":\"A Sideloaded "
                              "Game\",\"is_installed\":true}]}"));

  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 4);
  // Sorted by title; the uninstalled Epic entry is gone.
  QCOMPARE(games.at(0).title, QStringLiteral("A Sideloaded Game"));
  QCOMPARE(games.at(0).runner, QStringLiteral("sideload"));
  QCOMPARE(games.at(1).title, QStringLiteral("An Amazon Game"));
  QCOMPARE(games.at(1).runner, QStringLiteral("nile"));
  QCOMPARE(games.at(2).title, QStringLiteral("Beneath a Steel Sky"));
  QCOMPARE(games.at(2).appName, QStringLiteral("1207658930"));
  QCOMPARE(games.at(3).title, QStringLiteral("Kena"));
  QCOMPARE(games.at(3).runner, QStringLiteral("legendary"));

  // Detection keys off the same files, so this tree is what makes Heroic
  // "available" when it sits at one of the default locations.
  QVERIFY(!HeroicLibrary::installedGames(configDir).isEmpty());
}

void TestHeroicLibrary::skipsUnreadableCacheWithoutLosingOthers() {
  const QString configDir = newConfigDir();
  writeFile(configDir + QStringLiteral("/store_cache/legendary_library.json"),
            QByteArrayLiteral("{\"library\":[ this is not json"));
  writeFile(configDir + QStringLiteral("/store_cache/gog_library.json"),
            QByteArrayLiteral("{\"games\":[") + game("42", "A GOG Game", "gog", true) +
                QByteArrayLiteral("]}"));

  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).title, QStringLiteral("A GOG Game"));
}

void TestHeroicLibrary::findsLocalIconsWhereHeroicLeavesThem() {
  const QString configDir = newConfigDir();
  const QString installPath = m_dir.filePath(QStringLiteral("games/steelsky"));
  writeFile(installPath + QStringLiteral("/support/icon.png"), "x");
  // Shortcut icon Heroic downloads when the user creates a desktop entry.
  writeFile(configDir + QStringLiteral("/icons/Quail.jpg"), "x");
  writeFile(configDir + QStringLiteral("/store_cache/legendary_library.json"),
            QByteArrayLiteral("{\"library\":[") + game("Quail", "Kena", "legendary", true) +
                QByteArrayLiteral("]}"));
  writeFile(configDir + QStringLiteral("/store_cache/gog_library.json"),
            QByteArrayLiteral("{\"games\":[") +
                game("1207658930", "Beneath a Steel Sky", "gog", true, installPath) +
                QByteArrayLiteral(",") + game("999", "No Art At All", "gog", true) +
                QByteArrayLiteral("]}"));

  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 3);
  QVERIFY(games.at(0).iconPath.endsWith(QStringLiteral("support/icon.png")));
  QCOMPARE(games.at(1).title, QStringLiteral("Kena"));
  QVERIFY(games.at(1).iconPath.endsWith(QStringLiteral("icons/Quail.jpg")));
  // Everything else in a Heroic record is a remote URL, so no art at all is
  // the normal case — and must never be mistaken for a path.
  QCOMPARE(games.at(2).title, QStringLiteral("No Art At All"));
  QVERIFY(games.at(2).iconPath.isEmpty());
}

void TestHeroicLibrary::launchUriIsPercentEncoded() {
  HeroicLibrary::Game game;
  game.appName = QStringLiteral("Quail");
  game.runner = QStringLiteral("legendary");
  QCOMPARE(HeroicLibrary::launchUri(game),
           QStringLiteral("heroic://launch?appName=Quail&runner=legendary"));

  // Sideloaded ids are user-supplied and can contain spaces and separators.
  game.appName = QStringLiteral("My Game & Co");
  game.runner = QStringLiteral("sideload");
  QCOMPARE(HeroicLibrary::launchUri(game),
           QStringLiteral("heroic://launch?appName=My%20Game%20%26%20Co&runner=sideload"));
}

// Kartend-g1g30: Heroic keeps covers as URLs, so the reader has to surface a
// USABLE one — portrait for a portrait grid, and with the "{ext}" placeholder
// Heroic writes into Epic URLs already substituted (left in place it 404s).
void TestHeroicLibrary::coverUrlPrefersPortraitAndSubstitutesExtPlaceholder() {
  const QString configDir = newConfigDir();
  // Raw strings assigned first, never passed through QByteArrayLiteral: the
  // parentheses in R"(...)" terminate the macro argument early.
  const QByteArray epic =
      R"({"library":[{"app_name":"Quail","title":"Kena","runner":"legendary","is_installed":true,)"
      R"("art_square":"https://cdn1.epicgames.com/quail/square.{ext}",)"
      R"("art_cover":"https://cdn1.epicgames.com/quail/wide.jpg"}]})";
  writeFile(configDir + QStringLiteral("/store_cache/legendary_library.json"), epic);
  // No art_square: the wide cover is better than nothing.
  const QByteArray gog =
      R"({"games":[{"app_name":"42","title":"A GOG Game","runner":"gog","is_installed":true,)"
      R"("art_cover":"https://images.gog.com/42/wide.png"}]})";
  writeFile(configDir + QStringLiteral("/store_cache/gog_library.json"), gog);

  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).title, QStringLiteral("A GOG Game"));
  QCOMPARE(games.at(0).coverUrl, QStringLiteral("https://images.gog.com/42/wide.png"));
  QCOMPARE(games.at(1).title, QStringLiteral("Kena"));
  // Portrait won, and {ext} is gone — a URL containing it is not fetchable.
  QCOMPARE(games.at(1).coverUrl, QStringLiteral("https://cdn1.epicgames.com/quail/square.jpg"));
  QVERIFY(!games.at(1).coverUrl.contains(QStringLiteral("{ext}")));
}

void TestHeroicLibrary::emptyConfigDirYieldsNothing() {
  QVERIFY(HeroicLibrary::installedGames(QString()).isEmpty());
  QVERIFY(HeroicLibrary::installedGames(m_dir.filePath(QStringLiteral("nowhere"))).isEmpty());
}

QTEST_MAIN(TestHeroicLibrary)
#include "test_heroiclibrary.moc"
