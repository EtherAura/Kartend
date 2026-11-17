// HeroicLibrary against a staged Heroic config tree: the four runner library
// caches, the is_installed filter, runner attribution, the launch URI, and the
// two places Heroic happens to leave a usable icon on disk (Kartend-4cff2).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
  void traversingAppNameYieldsNoLocalIcon();
  void coverUrlPrefersPortraitAndSubstitutesExtPlaceholder();
  void emptyConfigDirYieldsNothing();
  void editedTitleAndCoverWinForEpicAndSideload();
  void noEditsImportExactlyAsBefore();
  void titleEditAloneKeepsStoreArt();
  void customWideAloneBeatsStorePortrait();
  void refusedCustomPortraitFallsBackToCustomWide();
  void refusedCustomArtKeepsStoreArtButTitleApplies();
  void customCoverHostBoundary_data();
  void customCoverHostBoundary();
  void blankAndWrongTypeEditsAreIgnored();
  void malformedOverridesStoreHidesNothing();

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

  /// One installed Epic game with store art on Epic's own CDN, the base most
  /// of the override fixtures edit.
  static void writeEpicStoreGame(const QString &configDir) {
    const QByteArray epic =
        R"({"library":[{"app_name":"Quail","title":"Kena","runner":"legendary","is_installed":true,)"
        R"("art_square":"https://cdn1.epicgames.com/quail/square.{ext}",)"
        R"("art_cover":"https://cdn1.epicgames.com/quail/wide.jpg"}]})";
    writeFile(configDir + QStringLiteral("/store_cache/legendary_library.json"), epic);
  }

  /// Heroic 2.22's store/game-overrides.json, as written by its electron-store.
  static void writeOverrides(const QString &configDir, const QByteArray &content) {
    writeFile(configDir + QStringLiteral("/store/game-overrides.json"), content);
  }

  static QStringList describe(const QList<HeroicLibrary::Game> &games) {
    QStringList rows;
    for (const HeroicLibrary::Game &game : games) {
      rows.append(
          QStringList{game.appName, game.runner, game.title, game.coverUrl, game.iconPath}.join(
              QLatin1Char('|')));
    }
    return rows;
  }

  static constexpr const char *kStoreSquare = "https://cdn1.epicgames.com/quail/square.jpg";
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

// Kartend-9guwj: app_name comes from Heroic's JSON and was interpolated
// straight into "<configDir>/icons/<appName>.png", so a traversing value made
// the icon probe resolve to a file outside the Heroic config dir — which the
// importer then copied in as the game's cover.
void TestHeroicLibrary::traversingAppNameYieldsNoLocalIcon() {
  const QString configDir = newConfigDir();
  // A real .png one level above the icons dir, exactly where the hostile
  // app_name points. The icons/ directory itself must exist for the "../"
  // to resolve at all, so a decoy is written there too.
  writeFile(configDir + QStringLiteral("/private.png"), "secret");
  writeFile(configDir + QStringLiteral("/icons/decoy.png"), "x");
  QVERIFY(QFileInfo::exists(configDir + QStringLiteral("/icons/") + QStringLiteral("../private") +
                            QStringLiteral(".png")));

  writeFile(configDir + QStringLiteral("/store_cache/legendary_library.json"),
            QByteArrayLiteral("{\"library\":[") +
                game("../private", "Traversal", "legendary", true) + QByteArrayLiteral("]}"));

  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).title, QStringLiteral("Traversal"));
  QVERIFY2(
      games.at(0).iconPath.isEmpty(),
      qPrintable(QStringLiteral("traversing app_name resolved to '%1'").arg(games.at(0).iconPath)));

  // The GOG install_path branch is a separate input: an absolute path from the
  // same JSON. A ".." segment in it is refused; a plain absolute path is not,
  // because Heroic legitimately installs anywhere.
  const QString configDir2 = newConfigDir();
  const QString realInstall = m_dir.filePath(QStringLiteral("gog/title"));
  writeFile(realInstall + QStringLiteral("/support/icon.png"), "x");
  const QString climbing = realInstall + QStringLiteral("/../../gog/title");
  QVERIFY(QFileInfo::exists(climbing + QStringLiteral("/support/icon.png")));
  writeFile(configDir2 + QStringLiteral("/store_cache/gog_library.json"),
            QByteArrayLiteral("{\"games\":[") + game("climber", "Climber", "gog", true, climbing) +
                QByteArrayLiteral("]}"));
  const QList<HeroicLibrary::Game> gogGames = HeroicLibrary::installedGames(configDir2);
  QCOMPARE(gogGames.size(), 1);
  QVERIFY2(
      gogGames.at(0).iconPath.isEmpty(),
      qPrintable(
          QStringLiteral("install_path with '..' resolved to '%1'").arg(gogGames.at(0).iconPath)));
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

// Kartend-5jk77: Heroic 2.22 keeps the user's edited title and art in
// store/game-overrides.json, keyed by appName, and leaves the library caches
// untouched — so a reader of the caches alone imported the store's title and
// art. The fixture follows that file's shape; hosts are the stores' real art
// hosts, the ones the Heroic cover fetch accepts.
void TestHeroicLibrary::editedTitleAndCoverWinForEpicAndSideload() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  writeFile(configDir + QStringLiteral("/sideload_apps/library.json"),
            QByteArrayLiteral("{\"games\":[{\"app_name\":\"abc-123\",\"title\":\"A Sideloaded "
                              "Game\",\"is_installed\":true}]}"));
  const QByteArray overrides = R"({"overrides":{"Quail":{"title":"Edited title",)"
                               R"("art_cover":"https://cdn1.epicgames.com/custom-wide.png",)"
                               R"("art_square":"https://cdn1.epicgames.com/custom-square.png"},)"
                               R"("abc-123":{"title":"My Sideload",)"
                               R"("art_square":"https://images.gog.com/custom-sideload.png"}}})";
  writeOverrides(configDir, overrides);

  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 2);
  // Sorted by the EDITED title: "Edited title" now sorts before "My Sideload",
  // where the store titles had sorted "A Sideloaded Game" first.
  QCOMPARE(games.at(0).title, QStringLiteral("Edited title"));
  QCOMPARE(games.at(0).coverUrl, QStringLiteral("https://cdn1.epicgames.com/custom-square.png"));
  // Identity is untouched: the launch URI still names Heroic's own id.
  QCOMPARE(games.at(0).appName, QStringLiteral("Quail"));
  QCOMPARE(games.at(0).runner, QStringLiteral("legendary"));
  QCOMPARE(HeroicLibrary::launchUri(games.at(0)),
           QStringLiteral("heroic://launch?appName=Quail&runner=legendary"));
  QCOMPARE(games.at(1).title, QStringLiteral("My Sideload"));
  QCOMPARE(games.at(1).coverUrl, QStringLiteral("https://images.gog.com/custom-sideload.png"));
  QCOMPARE(games.at(1).appName, QStringLiteral("abc-123"));
  QCOMPARE(games.at(1).runner, QStringLiteral("sideload"));
}

void TestHeroicLibrary::noEditsImportExactlyAsBefore() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  writeFile(configDir + QStringLiteral("/store_cache/gog_library.json"),
            QByteArrayLiteral("{\"games\":[") + game("42", "A GOG Game", "gog", true) +
                QByteArrayLiteral(",") + game("43", "Uninstalled", "gog", false) +
                QByteArrayLiteral("]}"));
  const QStringList baseline = describe(HeroicLibrary::installedGames(configDir));
  QCOMPARE(baseline.size(), 2);
  QVERIFY(baseline.at(1).endsWith(QLatin1String(kStoreSquare) + QLatin1Char('|')));

  // An empty store (what Heroic leaves once every edit is cleared) and edits
  // for ids that are not installed or not known change nothing — and an edit
  // never brings an uninstalled game in.
  writeOverrides(configDir, QByteArrayLiteral("{\"overrides\":{}}"));
  QCOMPARE(describe(HeroicLibrary::installedGames(configDir)), baseline);
  const QByteArray unrelated =
      R"({"overrides":{"43":{"title":"Edited but uninstalled"},)"
      R"("someone-else":{"title":"Other","art_square":"https://images.gog.com/x.png"},)"
      R"("legendary_Quail":{"title":"Namespaced key"}}})";
  writeOverrides(configDir, unrelated);
  QCOMPARE(describe(HeroicLibrary::installedGames(configDir)), baseline);
}

void TestHeroicLibrary::titleEditAloneKeepsStoreArt() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  writeOverrides(configDir,
                 QByteArrayLiteral("{\"overrides\":{\"Quail\":{\"title\":\"Renamed\"}}}"));
  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).title, QStringLiteral("Renamed"));
  QCOMPARE(games.at(0).coverUrl, QLatin1String(kStoreSquare));
}

// The grid is portrait, but an art_cover the user CHOSE still says more
// about the game they want to see than the store's portrait does.
void TestHeroicLibrary::customWideAloneBeatsStorePortrait() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  const QByteArray overrides =
      R"({"overrides":{"Quail":{"art_cover":"https://cdn1.epicgames.com/custom-wide.{ext}"}}})";
  writeOverrides(configDir, overrides);
  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).title, QStringLiteral("Kena"));
  QCOMPARE(games.at(0).coverUrl, QStringLiteral("https://cdn1.epicgames.com/custom-wide.jpg"));
}

void TestHeroicLibrary::refusedCustomPortraitFallsBackToCustomWide() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  const QByteArray overrides =
      R"({"overrides":{"Quail":{"art_square":"https://images.example.invalid/square.png",)"
      R"("art_cover":"https://images-4.gog-statics.com/custom-wide.png"}}})";
  writeOverrides(configDir, overrides);
  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).coverUrl,
           QStringLiteral("https://images-4.gog-statics.com/custom-wide.png"));
}

// The rejection this guards: an edit the cover fetch would refuse must not
// throw away store art that would have downloaded.
void TestHeroicLibrary::refusedCustomArtKeepsStoreArtButTitleApplies() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  const QByteArray overrides = R"({"overrides":{"Quail":{"title":"Edited title",)"
                               R"("art_square":"https://images.example.invalid/square.png",)"
                               R"("art_cover":"file:///home/someone/Pictures/wide.png"}}})";
  writeOverrides(configDir, overrides);
  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).title, QStringLiteral("Edited title"));
  QCOMPARE(games.at(0).coverUrl, QLatin1String(kStoreSquare));
}

void TestHeroicLibrary::customCoverHostBoundary_data() {
  QTest::addColumn<QString>("customSquare");
  QTest::addColumn<QString>("expectedCover");
  const QString store = QLatin1String(kStoreSquare);
  const auto accepted = [](const char *tag, const char *url, const char *expected) {
    QTest::newRow(tag) << QString::fromLatin1(url) << QString::fromLatin1(expected);
  };
  const auto refused = [&store](const char *tag, const char *url) {
    QTest::newRow(tag) << QString::fromLatin1(url) << store;
  };
  // Every suffix the Heroic cover fetch pins, as the suffix itself and as a
  // subdomain of it.
  accepted("epic-cdn", "https://cdn1.epicgames.com/a.png", "https://cdn1.epicgames.com/a.png");
  accepted("epic-apex", "https://epicgames.com/a.png", "https://epicgames.com/a.png");
  accepted("unreal-ext", "https://cdn2.unrealengine.com/a.{ext}",
           "https://cdn2.unrealengine.com/a.jpg");
  accepted("gog", "https://images.gog.com/a.png", "https://images.gog.com/a.png");
  accepted("gog-statics", "https://images-1.gog-statics.com/a.jpg",
           "https://images-1.gog-statics.com/a.jpg");
  accepted("amazon", "https://m.media-amazon.com/a.jpg", "https://m.media-amazon.com/a.jpg");
  // QUrl lowercases the host before the fetch matches it; so does the reader.
  accepted("mixed-case-host", "https://CDN1.EpicGames.com/a.png",
           "https://CDN1.EpicGames.com/a.png");
  // Refused by the fetch, so the store art stays.
  refused("plain-http", "http://cdn1.epicgames.com/a.png");
  refused("ftp", "ftp://images.gog.com/a.png");
  refused("lookalike-prefix", "https://evilepicgames.com/a.png");
  refused("suffix-as-subdomain", "https://epicgames.com.attacker.invalid/a.png");
  refused("userinfo-decoy", "https://cdn1.epicgames.com@attacker.invalid/a.png");
  refused("host-in-path", "https://attacker.invalid/cdn1.epicgames.com/a.png");
  refused("bare-dot-suffix", "https://.epicgames.com/a.png");
  refused("trailing-dot", "https://cdn1.epicgames.com./a.png");
  refused("other-host", "https://images.example.invalid/a.png");
  refused("file-url", "file:///home/someone/Pictures/a.png");
  refused("local-path", "/home/someone/Pictures/a.png");
  refused("windows-path", "C:\\Users\\someone\\a.png");
  refused("no-scheme", "cdn1.epicgames.com/a.png");
}

void TestHeroicLibrary::customCoverHostBoundary() {
  QFETCH(QString, customSquare);
  QFETCH(QString, expectedCover);
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  const QJsonObject edits{{QStringLiteral("art_square"), customSquare}};
  const QJsonObject root{
      {QStringLiteral("overrides"), QJsonObject{{QStringLiteral("Quail"), edits}}}};
  writeOverrides(configDir, QJsonDocument(root).toJson());
  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).coverUrl, expectedCover);
}

void TestHeroicLibrary::blankAndWrongTypeEditsAreIgnored() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  const QStringList storeOnly = describe(HeroicLibrary::installedGames(configDir));
  QCOMPARE(storeOnly.size(), 1);
  const QList<QByteArray> fixtures = {
      R"({"overrides":{"Quail":{"title":"","art_square":"","art_cover":""}}})",
      R"({"overrides":{"Quail":{"title":"   ","art_square":"  "}}})",
      R"({"overrides":{"Quail":{"title":42,"art_square":7,"art_cover":["https://cdn1.epicgames.com/a.png"]}}})",
      R"({"overrides":{"Quail":{"title":null,"art_square":{"url":"https://cdn1.epicgames.com/a.png"}}}})",
      R"({"overrides":{"Quail":"Edited title"}})",
      R"({"overrides":{"Quail":["Edited title"]}})",
  };
  for (const QByteArray &fixture : fixtures) {
    writeOverrides(configDir, fixture);
    QCOMPARE(describe(HeroicLibrary::installedGames(configDir)), storeOnly);
  }
}

void TestHeroicLibrary::malformedOverridesStoreHidesNothing() {
  const QString configDir = newConfigDir();
  writeEpicStoreGame(configDir);
  writeFile(configDir + QStringLiteral("/store_cache/gog_library.json"),
            QByteArrayLiteral("{\"games\":[") + game("42", "A GOG Game", "gog", true) +
                QByteArrayLiteral("]}"));
  const QStringList baseline = describe(HeroicLibrary::installedGames(configDir));
  QCOMPARE(baseline.size(), 2);
  const QList<QByteArray> fixtures = {
      QByteArrayLiteral("{\"overrides\":{\"Quail\":{\"title\":\"Edit"), // truncated write
      QByteArrayLiteral("not json at all"),
      QByteArrayLiteral(""),
      QByteArrayLiteral("[{\"Quail\":{\"title\":\"Edited title\"}}]"),
      QByteArrayLiteral("{\"Quail\":{\"title\":\"Edited title\"}}"), // no "overrides" wrapper
      QByteArrayLiteral("{\"overrides\":\"Quail\"}"),
      QByteArrayLiteral("{\"overrides\":[{\"title\":\"Edited title\"}]}"),
  };
  for (const QByteArray &fixture : fixtures) {
    writeOverrides(configDir, fixture);
    QCOMPARE(describe(HeroicLibrary::installedGames(configDir)), baseline);
  }
  // A store path that is a directory rather than a file is unreadable too.
  QVERIFY(QFile::remove(configDir + QStringLiteral("/store/game-overrides.json")));
  QVERIFY(QDir().mkpath(configDir + QStringLiteral("/store/game-overrides.json")));
  QCOMPARE(describe(HeroicLibrary::installedGames(configDir)), baseline);
}

QTEST_MAIN(TestHeroicLibrary)
#include "test_heroiclibrary.moc"
