// EsdeLibrary::readFolderGamelist against staged per-system ROM folders
// (Kartend-1bwzs).
//
// Batocera, RetroPie, Recalbox and classic EmulationStation keep gamelist.xml
// INSIDE roms/<system>/ and write media paths into it, relative to that
// folder. The two fixture shapes below are the ones that matter: Batocera's
// ./images/ with <boxart>/<wheel>, and Skyscraper's ./media/<type>/ with
// <thumbnail>/<marquee>/<image>. Every tree is real files on disk, because the
// confinement rules (symlink escapes in particular) only mean anything there.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "esdelibrary.h"

class TestEmulationStationGamelist : public QObject {
  Q_OBJECT

private slots:
  void batoceraLayoutResolvesUnderTheSystemRoot();
  void skyscraperLayoutFallsBackToThumbnailWheelAndImage();
  void readsTheExplicitGamelistPath();
  void romPathsCannotLeaveTheSystemRoot();
  void invalidMediaEmptiesOnlyThatSlot();
  void missingOrMalformedGamelistYieldsNothing();

private:
  QTemporaryDir m_dir;
  int m_case = 0;

  QString newTree() {
    const QString dir = m_dir.filePath(QStringLiteral("es%1").arg(m_case++));
    QDir().mkpath(dir);
    return dir;
  }

  static void write(const QString &path, const QByteArray &content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  static QString canonical(const QString &path) { return QFileInfo(path).canonicalFilePath(); }
};

void TestEmulationStationGamelist::batoceraLayoutResolvesUnderTheSystemRoot() {
  const QString root = newTree() + QStringLiteral("/roms/snes");
  write(root + QStringLiteral("/Quest.sfc"), "rom");
  write(root + QStringLiteral("/Secret.sfc"), "rom");
  write(root + QStringLiteral("/Disc Games/Saga.sfc"), "rom");
  for (const char *media : {"Quest-boxart.png", "Quest-thumb.png", "Quest-marquee.png",
                            "Quest-wheel.png", "Quest-fanart.jpg", "Quest-image.png"}) {
    write(root + QStringLiteral("/images/") + QLatin1String(media), "img");
  }
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n"
                          "<game id=\"1\" source=\"ScreenScraper.fr\">\n<path>./Quest.sfc</path>\n"
                          "<name>The Quest</name>\n<desc>A long adventure.</desc>\n"
                          "<image>./images/Quest-image.png</image>\n"
                          "<thumbnail>./images/Quest-thumb.png</thumbnail>\n"
                          "<boxart>./images/Quest-boxart.png</boxart>\n"
                          "<marquee>./images/Quest-marquee.png</marquee>\n"
                          "<wheel>./images/Quest-wheel.png</wheel>\n"
                          "<fanart>./images/Quest-fanart.jpg</fanart>\n"
                          "<video>./videos/Quest-video.mp4</video>\n"
                          "<rating>0.8</rating>\n<releasedate>19910623T000000</releasedate>\n"
                          "<developer>Studio</developer>\n<publisher>Publisher Inc</publisher>\n"
                          "<genre>RPG</genre>\n<players>1-2</players>\n"
                          "<md5>0123456789abcdef0123456789abcdef</md5>\n<crc32>DEADBEEF</crc32>\n"
                          "</game>\n"
                          "<game>\n<path>./Secret.sfc</path>\n<name>Secret</name>\n"
                          "<hidden>true</hidden>\n</game>\n"
                          "<game>\n<path>./Disc Games/Saga.sfc</path>\n<name>Saga</name>\n</game>\n"
                          "</gameList>\n"));

  const QList<EsdeLibrary::FolderGamelistGame> games =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
  QCOMPARE(games.size(), 3);

  const EsdeLibrary::FolderGamelistGame &quest = games.at(0);
  QCOMPARE(quest.romPath, canonical(root + QStringLiteral("/Quest.sfc")));
  QCOMPARE(quest.name, QStringLiteral("The Quest"));
  QCOMPARE(quest.description, QStringLiteral("A long adventure."));
  QCOMPARE(quest.developer, QStringLiteral("Studio"));
  QCOMPARE(quest.publisher, QStringLiteral("Publisher Inc"));
  QCOMPARE(quest.genre, QStringLiteral("RPG"));
  QCOMPARE(quest.players, QStringLiteral("1-2"));
  QCOMPARE(quest.releaseDate, QStringLiteral("19910623T000000"));
  QVERIFY(!quest.hidden);
  // Each preferred tag wins over its fallback when both are present.
  QCOMPARE(quest.coverPath, canonical(root + QStringLiteral("/images/Quest-boxart.png")));
  QCOMPARE(quest.logoPath, canonical(root + QStringLiteral("/images/Quest-marquee.png")));
  QCOMPARE(quest.heroPath, canonical(root + QStringLiteral("/images/Quest-fanart.jpg")));

  // Hidden is RETAINED rather than filtered: whether to import it is a
  // product decision for the caller, not the reader.
  QCOMPARE(games.at(1).name, QStringLiteral("Secret"));
  QVERIFY(games.at(1).hidden);
  QVERIFY(games.at(1).coverPath.isEmpty());

  // A ROM in a subfolder resolves under the same system root.
  QCOMPARE(games.at(2).romPath, canonical(root + QStringLiteral("/Disc Games/Saga.sfc")));
}

void TestEmulationStationGamelist::skyscraperLayoutFallsBackToThumbnailWheelAndImage() {
  const QString root = newTree() + QStringLiteral("/RetroPie/roms/megadrive");
  write(root + QStringLiteral("/Sonic.md"), "rom");
  write(root + QStringLiteral("/Streets.md"), "rom");
  write(root + QStringLiteral("/media/covers/Sonic.png"), "img");
  write(root + QStringLiteral("/media/marquees/Sonic.png"), "img");
  write(root + QStringLiteral("/media/screenshots/Sonic.png"), "img");
  write(root + QStringLiteral("/media/wheels/Streets.png"), "img");
  // Skyscraper writes no <boxart>/<fanart>; RetroPie's ES leaves <path>
  // without the "./" prefix in some lists, so both spellings are staged.
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n"
                          "<game>\n<path>./Sonic.md</path>\n<name>Sonic</name>\n"
                          "<thumbnail>./media/covers/Sonic.png</thumbnail>\n"
                          "<marquee>./media/marquees/Sonic.png</marquee>\n"
                          "<image>./media/screenshots/Sonic.png</image>\n"
                          "<video>./media/videos/Sonic.mp4</video>\n"
                          "<hidden>TRUE</hidden>\n</game>\n"
                          "<game>\n<path>Streets.md</path>\n<name>Streets</name>\n"
                          "<wheel>media/wheels/Streets.png</wheel>\n</game>\n"
                          "</gameList>\n"));

  const QList<EsdeLibrary::FolderGamelistGame> games =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).romPath, canonical(root + QStringLiteral("/Sonic.md")));
  QCOMPARE(games.at(0).coverPath, canonical(root + QStringLiteral("/media/covers/Sonic.png")));
  QCOMPARE(games.at(0).logoPath, canonical(root + QStringLiteral("/media/marquees/Sonic.png")));
  QCOMPARE(games.at(0).heroPath, canonical(root + QStringLiteral("/media/screenshots/Sonic.png")));
  QVERIFY(games.at(0).hidden); // case-insensitive, as in the ES-DE reader

  QCOMPARE(games.at(1).romPath, canonical(root + QStringLiteral("/Streets.md")));
  QCOMPARE(games.at(1).logoPath, canonical(root + QStringLiteral("/media/wheels/Streets.png")));
  QVERIFY(games.at(1).coverPath.isEmpty());
  QVERIFY(games.at(1).heroPath.isEmpty());
}

void TestEmulationStationGamelist::readsTheExplicitGamelistPath() {
  const QString tree = newTree();
  const QString root = tree + QStringLiteral("/roms/nes");
  write(root + QStringLiteral("/Alpha.nes"), "rom");
  write(root + QStringLiteral("/Beta.nes"), "rom");
  // The in-folder list names Alpha; a list kept elsewhere names Beta. The
  // reader must read the path it is given, and still resolve <path> against
  // the system root rather than the gamelist's own directory.
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<gameList><game><path>./Alpha.nes</path><name>From Folder</name>"
                          "</game></gameList>"));
  const QString elsewhere = tree + QStringLiteral("/backup/nes/gamelist.xml");
  write(elsewhere, QByteArrayLiteral("<gameList><game><path>./Beta.nes</path>"
                                     "<name>From Backup</name></game></gameList>"));

  const QList<EsdeLibrary::FolderGamelistGame> fromFolder =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
  QCOMPARE(fromFolder.size(), 1);
  QCOMPARE(fromFolder.at(0).name, QStringLiteral("From Folder"));

  const QList<EsdeLibrary::FolderGamelistGame> fromBackup =
      EsdeLibrary::readFolderGamelist(elsewhere, root);
  QCOMPARE(fromBackup.size(), 1);
  QCOMPARE(fromBackup.at(0).name, QStringLiteral("From Backup"));
  QCOMPARE(fromBackup.at(0).romPath, canonical(root + QStringLiteral("/Beta.nes")));
}

void TestEmulationStationGamelist::romPathsCannotLeaveTheSystemRoot() {
  const QString tree = newTree();
  const QString root = tree + QStringLiteral("/roms/nes");
  const QString outside = tree + QStringLiteral("/outside");
  write(root + QStringLiteral("/Good.nes"), "rom");
  write(root + QStringLiteral("/sub/Inner.nes"), "rom");
  write(outside + QStringLiteral("/Evil.nes"), "rom");
  QVERIFY(QFile::link(outside + QStringLiteral("/Evil.nes"), root + QStringLiteral("/Link.nes")));
  QVERIFY(QFile::link(outside, root + QStringLiteral("/linkdir")));

  const QByteArray absoluteEvil = QFile::encodeName(outside + QStringLiteral("/Evil.nes"));
  write(
      root + QStringLiteral("/gamelist.xml"),
      QByteArrayLiteral("<gameList>\n"
                        "<game><path>") +
          absoluteEvil +
          QByteArrayLiteral("</path><name>Absolute</name></game>\n"
                            "<game><path>../../outside/Evil.nes</path><name>Up</name></game>\n"
                            "<game><path>./sub/../../../outside/Evil.nes</path>"
                            "<name>Sneaky</name></game>\n"
                            "<game><path>./Link.nes</path><name>Symlink</name></game>\n"
                            "<game><path>./linkdir/Evil.nes</path><name>Linked Dir</name></game>\n"
                            "<game><path>./Missing.nes</path><name>Missing</name></game>\n"
                            "<game><path></path><name>Empty</name></game>\n"
                            "<game><path>./Good.nes</path><name>Good</name></game>\n"
                            "<game><path>./sub/Inner.nes</path><name>Inner</name></game>\n"
                            "</gameList>\n"));

  const QList<EsdeLibrary::FolderGamelistGame> games =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
  // Only the two genuine in-root ROMs survive; nothing points outside.
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).name, QStringLiteral("Good"));
  QCOMPARE(games.at(0).romPath, canonical(root + QStringLiteral("/Good.nes")));
  QCOMPARE(games.at(1).name, QStringLiteral("Inner"));
  QCOMPARE(games.at(1).romPath, canonical(root + QStringLiteral("/sub/Inner.nes")));
  const QString outsideCanonical = canonical(outside);
  for (const EsdeLibrary::FolderGamelistGame &game : games) {
    QVERIFY(!game.romPath.startsWith(outsideCanonical));
  }
}

void TestEmulationStationGamelist::invalidMediaEmptiesOnlyThatSlot() {
  const QString tree = newTree();
  const QString root = tree + QStringLiteral("/roms/gba");
  const QString outside = tree + QStringLiteral("/outside");
  write(root + QStringLiteral("/Hero.gba"), "rom");
  write(root + QStringLiteral("/images/Hero-wheel.png"), "img");
  write(root + QStringLiteral("/images/Hero-image.png"), "img");
  write(outside + QStringLiteral("/Evil.png"), "img");
  QVERIFY(QFile::link(outside + QStringLiteral("/Evil.png"),
                      root + QStringLiteral("/images/Hero-marquee.png")));

  const QByteArray absoluteEvil = QFile::encodeName(outside + QStringLiteral("/Evil.png"));
  // Every preferred tag is unusable in a different way, so each slot must fall
  // back — or stay empty — without the game or its text being lost.
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<gameList><game><path>./Hero.gba</path><name>Hero</name>"
                          "<desc>Still here.</desc><boxart>") +
            absoluteEvil +
            QByteArrayLiteral("</boxart><thumbnail>../../outside/Evil.png</thumbnail>"
                              "<marquee>./images/Hero-marquee.png</marquee>"
                              "<wheel>./images/Hero-wheel.png</wheel>"
                              "<fanart>./images/Hero-fanart-missing.jpg</fanart>"
                              "<image>./images/Hero-image.png</image>"
                              "</game></gameList>"));

  const QList<EsdeLibrary::FolderGamelistGame> games =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
  QCOMPARE(games.size(), 1);
  const EsdeLibrary::FolderGamelistGame &game = games.at(0);
  QCOMPARE(game.name, QStringLiteral("Hero"));
  QCOMPARE(game.description, QStringLiteral("Still here."));
  QCOMPARE(game.romPath, canonical(root + QStringLiteral("/Hero.gba")));
  QVERIFY(game.coverPath.isEmpty()); // absolute boxart and traversal thumbnail both refused
  QCOMPARE(game.logoPath, canonical(root + QStringLiteral("/images/Hero-wheel.png")));
  QCOMPARE(game.heroPath, canonical(root + QStringLiteral("/images/Hero-image.png")));
}

void TestEmulationStationGamelist::missingOrMalformedGamelistYieldsNothing() {
  const QString root = newTree() + QStringLiteral("/roms/nes");
  write(root + QStringLiteral("/Game.nes"), "rom");
  QVERIFY(EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root).isEmpty());

  write(root + QStringLiteral("/gamelist.xml"), QByteArrayLiteral("<gameList><game><path>"));
  QVERIFY(EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root).isEmpty());

  // A root that does not exist confines nothing, so it admits nothing.
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<gameList><game><path>./Game.nes</path></game></gameList>"));
  QVERIFY(EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"),
                                          root + QStringLiteral("/nowhere"))
              .isEmpty());
}

QTEST_MAIN(TestEmulationStationGamelist)
#include "test_emulationstationgamelist.moc"
