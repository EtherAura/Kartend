// EsdeLibrary against a staged ES-DE tree (Kartend-ilkne).
//
// The cases here encode what a REAL ES-DE 3.4.1 install does, not what its
// documentation implies — the difference was measured by installing it and
// reading the files it wrote:
//
//   * gamelist.xml is a metadata SIDECAR. It contained one <game> for a
//     four-game system, because only one had been favourited. The library is
//     the ROM directory; the gamelist only decorates it.
//   * es_settings.xml stores value="" to mean "use the default", so an empty
//     ROMDirectory means ~/ROMs rather than "nowhere".
//
// Both are the kind of thing a hand-written fixture would have quietly got
// backwards, so they are pinned here deliberately.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "esdelibrary.h"

class TestEsdeLibrary : public QObject {
  Q_OBJECT

private slots:
  void gamesComeFromTheRomDirNotTheGamelist();
  void gamelistOverlaysMetadataAndHidesGames();
  void emptySettingValueMeansTheDefault();
  void systemsSkipDirectoriesWithoutGames();
  void mediaIsKeyedOnTheRomBaseName();
  void readsTheGamelistARealInstallWrote();

private:
  QTemporaryDir m_dir;
  int m_case = 0;

  QString newTree() {
    const QString dir = m_dir.filePath(QStringLiteral("esde%1").arg(m_case++));
    QDir().mkpath(dir + QStringLiteral("/settings"));
    return dir;
  }

  static void write(const QString &path, const QByteArray &content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  /// A settings file in the shape ES-DE really writes: NO root element, and
  /// the settings alphabetically sorted, so the two keys we care about are
  /// buried among others rather than sitting first. Both properties matter —
  /// see emptySettingValueMeansTheDefault.
  static void writeSettings(const QString &dataDir, const QString &romDir,
                            const QString &mediaDir) {
    const QString xml =
        QStringLiteral("<?xml version=\"1.0\"?>\n"
                       "<bool name=\"AlternativeEmulatorPerGame\" value=\"true\" />\n"
                       "<string name=\"MediaDirectory\" value=\"%1\" />\n"
                       "<int name=\"ScraperRetryOnErrorCount\" value=\"3\" />\n"
                       "<string name=\"ROMDirectory\" value=\"%2\" />\n"
                       "<bool name=\"VirtualKeyboard\" value=\"true\" />\n")
            .arg(mediaDir, romDir);
    write(dataDir + QStringLiteral("/settings/es_settings.xml"), xml.toUtf8());
  }
};

void TestEsdeLibrary::gamesComeFromTheRomDirNotTheGamelist() {
  const QString dataDir = newTree();
  const QString romDir = dataDir + QStringLiteral("/ROMs");
  writeSettings(dataDir, romDir, QString());

  for (const char *rom : {"Alpha.nes", "Beta.nes", "Gamma.zip"}) {
    write(romDir + QStringLiteral("/nes/") + QLatin1String(rom), "romdata");
  }
  // The gamelist mentions ONE of the three — exactly what a real install looks
  // like after a single favourite or metadata edit.
  write(dataDir + QStringLiteral("/gamelists/nes/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n<game>\n"
                          "<path>./Beta.nes</path>\n<name>Beta Prime</name>\n"
                          "<favorite>true</favorite>\n</game>\n</gameList>\n"));

  const QList<EsdeLibrary::System> systems = EsdeLibrary::systems(romDir);
  QCOMPARE(systems.size(), 1);
  QCOMPARE(systems.at(0).name, QStringLiteral("nes"));
  QCOMPARE(systems.at(0).gameCount, 3);

  const QList<EsdeLibrary::Game> games =
      EsdeLibrary::games(systems.at(0), dataDir, EsdeLibrary::mediaDirectory(dataDir));
  // All THREE — not the one the gamelist happens to know about.
  QCOMPARE(games.size(), 3);
  QCOMPARE(games.at(0).title, QStringLiteral("Alpha"));
  QCOMPARE(games.at(1).title, QStringLiteral("Beta Prime")); // gamelist name won
  QCOMPARE(games.at(2).title, QStringLiteral("Gamma"));
  QVERIFY(games.at(0).romPath.endsWith(QStringLiteral("/nes/Alpha.nes")));
}

void TestEsdeLibrary::gamelistOverlaysMetadataAndHidesGames() {
  const QString dataDir = newTree();
  const QString romDir = dataDir + QStringLiteral("/ROMs");
  writeSettings(dataDir, romDir, QString());
  write(romDir + QStringLiteral("/snes/Quest.sfc"), "rom");
  write(romDir + QStringLiteral("/snes/Secret.sfc"), "rom");
  write(dataDir + QStringLiteral("/gamelists/snes/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n"
                          "<game>\n<path>./Quest.sfc</path>\n<name>The Quest</name>\n"
                          "<desc>A long adventure.</desc>\n<developer>Studio</developer>\n"
                          "<publisher>Publisher Inc</publisher>\n<genre>RPG</genre>\n"
                          "<players>1-2</players>\n<releasedate>19910623T000000</releasedate>\n"
                          "</game>\n"
                          "<game>\n<path>./Secret.sfc</path>\n<hidden>true</hidden>\n</game>\n"
                          "</gameList>\n"));

  const QList<EsdeLibrary::System> systems = EsdeLibrary::systems(romDir);
  QCOMPARE(systems.size(), 1);
  const QList<EsdeLibrary::Game> games =
      EsdeLibrary::games(systems.at(0), dataDir, EsdeLibrary::mediaDirectory(dataDir));
  // The hidden one is gone — ES-DE hides it, so importing it would contradict
  // what the user set up.
  QCOMPARE(games.size(), 1);
  const EsdeLibrary::Game &game = games.at(0);
  QCOMPARE(game.title, QStringLiteral("The Quest"));
  QCOMPARE(game.description, QStringLiteral("A long adventure."));
  QCOMPARE(game.developer, QStringLiteral("Studio"));
  QCOMPARE(game.publisher, QStringLiteral("Publisher Inc"));
  QCOMPARE(game.genre, QStringLiteral("RPG"));
  QCOMPARE(game.players, QStringLiteral("1-2"));
  // Kept verbatim: converting ES-DE's basic ISO8601 belongs to the importer,
  // not the reader.
  QCOMPARE(game.releaseDate, QStringLiteral("19910623T000000"));
}

void TestEsdeLibrary::emptySettingValueMeansTheDefault() {
  const QString dataDir = newTree();
  // Stock ES-DE 3.4.1 shape, and BOTH of its awkward properties are load-
  // bearing here: the file has no root element (not well-formed XML — a strict
  // parser stops after the first element), and the settings are alphabetically
  // sorted so neither key we want is first. A reader that parses only the
  // opening element finds nothing on a real install.
  write(dataDir + QStringLiteral("/settings/es_settings.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n"
                          "<bool name=\"AlternativeEmulatorPerGame\" value=\"true\" />\n"
                          "<string name=\"MediaDirectory\" value=\"\" />\n"
                          "<int name=\"ScraperRetryOnErrorCount\" value=\"3\" />\n"
                          "<string name=\"ROMDirectory\" value=\"\" />\n"
                          "<bool name=\"VirtualKeyboard\" value=\"true\" />\n"));

  // Empty is "use the default", NOT "unset" — the distinction decides whether
  // a default install imports anything at all.
  QCOMPARE(EsdeLibrary::romDirectory(dataDir), QDir::homePath() + QStringLiteral("/ROMs"));
  QCOMPARE(EsdeLibrary::mediaDirectory(dataDir), dataDir + QStringLiteral("/downloaded_media"));

  // A configured value wins, and ~ in it is expanded.
  writeSettings(dataDir, QStringLiteral("~/Games/roms"), QStringLiteral("~/Games/media"));
  QCOMPARE(EsdeLibrary::romDirectory(dataDir), QDir::homePath() + QStringLiteral("/Games/roms"));
  QCOMPARE(EsdeLibrary::mediaDirectory(dataDir), QDir::homePath() + QStringLiteral("/Games/media"));
}

void TestEsdeLibrary::systemsSkipDirectoriesWithoutGames() {
  const QString dataDir = newTree();
  const QString romDir = dataDir + QStringLiteral("/ROMs");
  writeSettings(dataDir, romDir, QString());
  write(romDir + QStringLiteral("/nes/Game.nes"), "rom");
  // ES-DE seeds an empty ROM tree with per-system notes; a directory holding
  // only those is not a system to import.
  write(romDir + QStringLiteral("/gba/systeminfo.txt"), "read me");
  QVERIFY(QDir().mkpath(romDir + QStringLiteral("/n64")));

  const QList<EsdeLibrary::System> systems = EsdeLibrary::systems(romDir);
  QCOMPARE(systems.size(), 1);
  QCOMPARE(systems.at(0).name, QStringLiteral("nes"));

  // A ROM root that does not exist yields nothing rather than failing.
  QVERIFY(EsdeLibrary::systems(m_dir.filePath(QStringLiteral("nowhere"))).isEmpty());
}

void TestEsdeLibrary::mediaIsKeyedOnTheRomBaseName() {
  const QString dataDir = newTree();
  const QString romDir = dataDir + QStringLiteral("/ROMs");
  const QString mediaDir = dataDir + QStringLiteral("/media");
  writeSettings(dataDir, romDir, mediaDir);
  write(romDir + QStringLiteral("/nes/Rom Name.nes"), "rom");
  // Renamed in the gamelist — media still keys off the FILE, which is how
  // ES-DE's scraper names what it downloads.
  write(dataDir + QStringLiteral("/gamelists/nes/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n<game>\n"
                          "<path>./Rom Name.nes</path>\n<name>Pretty Title</name>\n"
                          "</game>\n</gameList>\n"));
  write(mediaDir + QStringLiteral("/nes/covers/Rom Name.jpg"), "img");
  write(mediaDir + QStringLiteral("/nes/marquees/Rom Name.png"), "img");
  write(mediaDir + QStringLiteral("/nes/screenshots/Rom Name.jpg"), "img");

  const QList<EsdeLibrary::System> systems = EsdeLibrary::systems(romDir);
  QCOMPARE(systems.size(), 1);
  const QList<EsdeLibrary::Game> games = EsdeLibrary::games(systems.at(0), dataDir, mediaDir);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).title, QStringLiteral("Pretty Title"));
  QVERIFY(games.at(0).coverPath.endsWith(QStringLiteral("covers/Rom Name.jpg")));
  QVERIFY(games.at(0).logoPath.endsWith(QStringLiteral("marquees/Rom Name.png")));
  QVERIFY(games.at(0).screenshotPath.endsWith(QStringLiteral("screenshots/Rom Name.jpg")));
  // Nothing scraped for fanart, and that is the common case.
  QVERIFY(games.at(0).fanartPath.isEmpty());
}

// The bytes below are copied verbatim from what ES-DE 3.4.1 actually wrote in
// the VM after one game was favourited. If ES-DE ever changes this shape, this
// is the case that should fail.
void TestEsdeLibrary::readsTheGamelistARealInstallWrote() {
  const QString dataDir = newTree();
  const QString romDir = dataDir + QStringLiteral("/ROMs");
  writeSettings(dataDir, romDir, QString());
  for (const char *rom :
       {"8bit Table Tennis.nes", "RoboRun.nes", "Runner.nes", "Witch n Wiz.nes"}) {
    write(romDir + QStringLiteral("/nes/") + QLatin1String(rom), "rom");
  }
  write(dataDir + QStringLiteral("/gamelists/nes/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n\t<game>\n"
                          "\t\t<path>./8bit Table Tennis.nes</path>\n"
                          "\t\t<name>8bit Table Tennis</name>\n"
                          "\t\t<favorite>true</favorite>\n\t</game>\n</gameList>\n"));

  const QList<EsdeLibrary::System> systems = EsdeLibrary::systems(romDir);
  QCOMPARE(systems.size(), 1);
  QCOMPARE(systems.at(0).gameCount, 4);
  const QList<EsdeLibrary::Game> games =
      EsdeLibrary::games(systems.at(0), dataDir, EsdeLibrary::mediaDirectory(dataDir));
  // Four games from a gamelist naming one. This is the whole finding.
  QCOMPARE(games.size(), 4);
  QCOMPARE(games.at(0).title, QStringLiteral("8bit Table Tennis"));
  QCOMPARE(games.at(1).title, QStringLiteral("RoboRun"));
  QCOMPARE(games.at(2).title, QStringLiteral("Runner"));
  QCOMPARE(games.at(3).title, QStringLiteral("Witch n Wiz"));
}

QTEST_MAIN(TestEsdeLibrary)
#include "test_esdelibrary.moc"
