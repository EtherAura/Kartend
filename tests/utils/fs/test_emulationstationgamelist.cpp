// EsdeLibrary::readFolderGamelist against staged per-system ROM folders
// (Kartend-1bwzs).
//
// Batocera, RetroPie, Recalbox and classic EmulationStation keep gamelist.xml
// INSIDE roms/<system>/ and write media paths into it, relative to that
// folder. The two fixture shapes below are the ones that matter: Batocera's
// ./images/ with <boxart>/<wheel>, and Skyscraper's ./media/<type>/ with
// <thumbnail>/<marquee>/<image>. Every tree is real files on disk, because the
// confinement rules (link escapes in particular) only mean anything there.
//
// Links are made with linkDirectory()/linkFile() below, never QFile::link: on
// Windows QFile::link writes a .lnk shell shortcut, which every filesystem API
// (and the reader) sees as an ordinary file INSIDE the root — so a fixture
// built that way stages no escape at all, and an "escaping" ROM or marquee
// looks like a genuine in-root file. Each link is checked to really resolve to
// its target before the reader is asked about it.
#include <filesystem>
#include <system_error>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include "esdelibrary.h"

namespace {

std::filesystem::path fsPath(const QString &path) {
  return std::filesystem::path(path.toStdU16String());
}

/// Same file or directory on disk, by identity (device + inode, or volume +
/// file index): independent of spelling, case, 8.3 short names or links.
bool sameFile(const QString &a, const QString &b) {
  std::error_code error;
  return !a.isEmpty() && std::filesystem::equivalent(fsPath(a), fsPath(b), error) && !error;
}

/// A real directory link at `link` resolving to `target`, creatable without
/// root or elevation on every supported platform: a symlink on Linux and macOS
/// (any user may create one), an NTFS directory junction on Windows (cmd's
/// built-in `mklink /J`, which unlike a symlink needs neither
/// SeCreateSymbolicLinkPrivilege nor Developer Mode).
bool linkDirectory(const QString &target, const QString &link) {
#ifdef Q_OS_WIN
  QProcess mklink;
  mklink.start(QStringLiteral("cmd.exe"),
               {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
  if (!mklink.waitForFinished(30000) || mklink.exitStatus() != QProcess::NormalExit ||
      mklink.exitCode() != 0) {
    return false;
  }
#else
  std::error_code error;
  std::filesystem::create_directory_symlink(fsPath(target), fsPath(link), error);
  if (error) {
    return false;
  }
#endif
  return sameFile(link, target); // a link in name only would not be the target
}

#ifndef Q_OS_WIN
/// A real leaf FILE symlink. POSIX only: Windows has no file-level link that
/// resolves elsewhere and can be made without privilege (a file symlink needs
/// SeCreateSymbolicLinkPrivilege or Developer Mode, a hard link IS the file,
/// a shortcut is an ordinary file). There the same escapes are staged through
/// a junction'd directory, which linkDirectory() can make unprivileged.
bool linkFile(const QString &target, const QString &link) {
  std::error_code error;
  std::filesystem::create_symlink(fsPath(target), fsPath(link), error);
  return !error && sameFile(link, target);
}
#endif

} // namespace

class TestEmulationStationGamelist : public QObject {
  Q_OBJECT

private slots:
  void batoceraLayoutResolvesUnderTheSystemRoot();
  void skyscraperLayoutFallsBackToThumbnailWheelAndImage();
  void readsTheExplicitGamelistPath();
  void romPathsCannotLeaveTheSystemRoot();
  void linksInsideTheRootStillResolve();
  void driveDesignatorsAreRefusedOnEveryPlatform();
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
  QVERIFY(QDir::isAbsolutePath(quest.romPath));
  QVERIFY(sameFile(quest.romPath, root + QStringLiteral("/Quest.sfc")));
  QCOMPARE(quest.name, QStringLiteral("The Quest"));
  QCOMPARE(quest.description, QStringLiteral("A long adventure."));
  QCOMPARE(quest.developer, QStringLiteral("Studio"));
  QCOMPARE(quest.publisher, QStringLiteral("Publisher Inc"));
  QCOMPARE(quest.genre, QStringLiteral("RPG"));
  QCOMPARE(quest.players, QStringLiteral("1-2"));
  QCOMPARE(quest.releaseDate, QStringLiteral("19910623T000000"));
  QVERIFY(!quest.hidden);
  // Each preferred tag wins over its fallback when both are present.
  QVERIFY(sameFile(quest.coverPath, root + QStringLiteral("/images/Quest-boxart.png")));
  QVERIFY(sameFile(quest.logoPath, root + QStringLiteral("/images/Quest-marquee.png")));
  QVERIFY(sameFile(quest.heroPath, root + QStringLiteral("/images/Quest-fanart.jpg")));

  // Hidden is RETAINED rather than filtered: whether to import it is a
  // product decision for the caller, not the reader.
  QCOMPARE(games.at(1).name, QStringLiteral("Secret"));
  QVERIFY(games.at(1).hidden);
  QVERIFY(games.at(1).coverPath.isEmpty());

  // A ROM in a subfolder resolves under the same system root.
  QVERIFY(sameFile(games.at(2).romPath, root + QStringLiteral("/Disc Games/Saga.sfc")));
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
  QVERIFY(sameFile(games.at(0).romPath, root + QStringLiteral("/Sonic.md")));
  QVERIFY(sameFile(games.at(0).coverPath, root + QStringLiteral("/media/covers/Sonic.png")));
  QVERIFY(sameFile(games.at(0).logoPath, root + QStringLiteral("/media/marquees/Sonic.png")));
  QVERIFY(sameFile(games.at(0).heroPath, root + QStringLiteral("/media/screenshots/Sonic.png")));
  QVERIFY(games.at(0).hidden); // case-insensitive, as in the ES-DE reader

  QVERIFY(sameFile(games.at(1).romPath, root + QStringLiteral("/Streets.md")));
  QVERIFY(sameFile(games.at(1).logoPath, root + QStringLiteral("/media/wheels/Streets.png")));
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
  QVERIFY(sameFile(fromBackup.at(0).romPath, root + QStringLiteral("/Beta.nes")));
}

void TestEmulationStationGamelist::romPathsCannotLeaveTheSystemRoot() {
  const QString tree = newTree();
  const QString root = tree + QStringLiteral("/roms/nes");
  const QString outside = tree + QStringLiteral("/outside");
  write(root + QStringLiteral("/Good.nes"), "rom");
  write(root + QStringLiteral("/sub/Inner.nes"), "rom");
  write(outside + QStringLiteral("/Evil.nes"), "rom");
  // Every platform: a real directory link (symlink / junction) out of the root.
  QVERIFY(linkDirectory(outside, root + QStringLiteral("/linkdir")));
#ifndef Q_OS_WIN
  // POSIX also gets a leaf file symlink; see linkFile() for why Windows can't.
  QVERIFY(linkFile(outside + QStringLiteral("/Evil.nes"), root + QStringLiteral("/Link.nes")));
#endif

  // UTF-8, as the XML declares, whatever the host's 8-bit code page.
  const QByteArray absoluteEvil = (outside + QStringLiteral("/Evil.nes")).toUtf8();
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<gameList>\n"
                          "<game><path>") +
            absoluteEvil +
            QByteArrayLiteral(
                "</path><name>Absolute</name></game>\n"
                "<game><path>../../outside/Evil.nes</path><name>Up</name></game>\n"
                "<game><path>..\\..\\outside\\Evil.nes</path><name>Backslash Up</name></game>\n"
                "<game><path>./sub/../../../outside/Evil.nes</path>"
                "<name>Sneaky</name></game>\n"
                "<game><path>\\\\server\\share\\Evil.nes</path><name>UNC</name></game>\n"
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
  QVERIFY(sameFile(games.at(0).romPath, root + QStringLiteral("/Good.nes")));
  QCOMPARE(games.at(1).name, QStringLiteral("Inner"));
  QVERIFY(sameFile(games.at(1).romPath, root + QStringLiteral("/sub/Inner.nes")));
  for (const EsdeLibrary::FolderGamelistGame &game : games) {
    QVERIFY(!sameFile(game.romPath, outside + QStringLiteral("/Evil.nes")));
  }
}

// The confinement check compares RESOLVED paths; a link that stays inside the
// root must still be followed, so a junction or symlink used to organise a
// system folder is not mistaken for an escape.
void TestEmulationStationGamelist::linksInsideTheRootStillResolve() {
  const QString root = newTree() + QStringLiteral("/roms/pce");
  write(root + QStringLiteral("/real/Bonk.pce"), "rom");
  write(root + QStringLiteral("/real/images/Bonk.png"), "img");
  QVERIFY(linkDirectory(root + QStringLiteral("/real"), root + QStringLiteral("/alias")));
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<gameList><game><path>./alias/Bonk.pce</path><name>Bonk</name>"
                          "<thumbnail>./alias/images/Bonk.png</thumbnail></game></gameList>"));

  const QList<EsdeLibrary::FolderGamelistGame> games =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
  QCOMPARE(games.size(), 1);
  QVERIFY(sameFile(games.at(0).romPath, root + QStringLiteral("/real/Bonk.pce")));
  QVERIFY(sameFile(games.at(0).coverPath, root + QStringLiteral("/real/images/Bonk.png")));
}

// A drive designator is absolute (or drive-relative) on Windows and an
// ordinary relative name on POSIX. The reader refuses it everywhere, so the
// same gamelist yields the same games on every host. On POSIX the fixture
// makes those names REAL in-root files, so a reader that only refused what
// the host calls absolute would admit them here; Windows can't create such
// names, and there the values are refused without any fixture behind them.
void TestEmulationStationGamelist::driveDesignatorsAreRefusedOnEveryPlatform() {
  const QString root = newTree() + QStringLiteral("/roms/gb");
  write(root + QStringLiteral("/Plain.gb"), "rom");
#ifndef Q_OS_WIN
  write(root + QStringLiteral("/Zelda: Awakening.gb"), "rom");
  write(root + QStringLiteral("/C:/Drive.gb"), "rom");
  write(root + QStringLiteral("/D:Relative.gb"), "rom");
  write(root + QStringLiteral("/images/E:cover.png"), "img");
#endif
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<gameList>\n"
                          "<game><path>C:/Drive.gb</path><name>Drive</name></game>\n"
                          "<game><path>D:Relative.gb</path><name>Relative</name></game>\n"
                          "<game><path>./Plain.gb</path><name>Plain</name>"
                          "<thumbnail>E:cover.png</thumbnail></game>\n"
                          "<game><path>./Zelda: Awakening.gb</path><name>Colon</name></game>\n"
                          "</gameList>\n"));

  const QList<EsdeLibrary::FolderGamelistGame> games =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
#ifdef Q_OS_WIN
  // "Zelda: Awakening.gb" is not a legal Windows name, so only Plain exists.
  QCOMPARE(games.size(), 1);
#else
  // A colon later in the name is an ordinary POSIX file name and stays.
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(1).name, QStringLiteral("Colon"));
#endif
  QCOMPARE(games.at(0).name, QStringLiteral("Plain"));
  QVERIFY(games.at(0).coverPath.isEmpty());
}

void TestEmulationStationGamelist::invalidMediaEmptiesOnlyThatSlot() {
  const QString tree = newTree();
  const QString root = tree + QStringLiteral("/roms/gba");
  const QString outside = tree + QStringLiteral("/outside");
  write(root + QStringLiteral("/Hero.gba"), "rom");
  write(root + QStringLiteral("/images/Hero-wheel.png"), "img");
  write(root + QStringLiteral("/images/Hero-image.png"), "img");
  write(outside + QStringLiteral("/Evil.png"), "img");
  QVERIFY(linkDirectory(outside, root + QStringLiteral("/images/escape")));
#ifndef Q_OS_WIN
  QVERIFY(linkFile(outside + QStringLiteral("/Evil.png"),
                   root + QStringLiteral("/images/Hero-fanart.jpg")));
#endif

  const QByteArray absoluteEvil = (outside + QStringLiteral("/Evil.png")).toUtf8();
  // Every preferred tag is unusable in a different way, so each slot must fall
  // back — or stay empty — without the game or its text being lost.
  write(root + QStringLiteral("/gamelist.xml"),
        QByteArrayLiteral("<gameList><game><path>./Hero.gba</path><name>Hero</name>"
                          "<desc>Still here.</desc><boxart>") +
            absoluteEvil +
            QByteArrayLiteral("</boxart><thumbnail>../../outside/Evil.png</thumbnail>"
                              "<marquee>./images/escape/Evil.png</marquee>"
                              "<wheel>./images/Hero-wheel.png</wheel>"
                              "<fanart>./images/Hero-fanart.jpg</fanart>"
                              "<image>./images/Hero-image.png</image>"
                              "</game></gameList>"));

  const QList<EsdeLibrary::FolderGamelistGame> games =
      EsdeLibrary::readFolderGamelist(root + QStringLiteral("/gamelist.xml"), root);
  QCOMPARE(games.size(), 1);
  const EsdeLibrary::FolderGamelistGame &game = games.at(0);
  QCOMPARE(game.name, QStringLiteral("Hero"));
  QCOMPARE(game.description, QStringLiteral("Still here."));
  QVERIFY(sameFile(game.romPath, root + QStringLiteral("/Hero.gba")));
  QVERIFY(game.coverPath.isEmpty()); // absolute boxart and traversal thumbnail both refused
  // Marquee escapes through a linked directory; wheel is the fallback.
  QVERIFY(sameFile(game.logoPath, root + QStringLiteral("/images/Hero-wheel.png")));
  // Fanart is a leaf symlink out (POSIX) or absent (Windows); image either way.
  QVERIFY(sameFile(game.heroPath, root + QStringLiteral("/images/Hero-image.png")));
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
