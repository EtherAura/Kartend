// EsdeLibrary media discovery for the formats ES-DE 3.4/3.5 scrapes
// (Kartend-vtz2h): WebP images and video files alongside the usual JPG/PNG.
//
// ES-DE finds media by naming convention, not by gamelist tags:
// <MediaDirectory>/<system>/<type>/<ROM base name>.<ext>, with images under
// covers/, marquees/, screenshots/, fanart/ and videos under videos/. The trees
// below follow that layout and go through the real reader.
//
// What is covered: a .webp cover reaches Game::coverPath, video files in
// videos/ never land in an image slot, and the video path is carried through
// the reader (Game::videoPath), the importer (LauncherImportService::esdeGames
// → GameEntry::videoPath) and the sync into the collection's artwork/video/,
// where VideoUtils::findVideoForFile finds it for the imported stub. The video
// files here are non-empty placeholders, not encoded AV1: this proves path
// import only, never decoding or playback.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "esdelibrary.h"
#include "launcherimportservice.h"
#include "videoutils.h"

class TestEsdeMediaImport : public QObject {
  Q_OBJECT

private slots:
  void webpCoverIsImported();
  void existingFormatStillWinsOverWebp();
  void gameWithoutMediaHasNoCover();
  void videosDoNotFillImageSlots();
  void mp4VideoIsDiscovered();
  void mkvVideoIsDiscovered();
  void videoDoesNotLeakToAnotherRom();
  void importedItemKeepsWebpCoverAndVideo();
  void importedItemKeepsMkvVideo();
  void importDoesNotClobberExistingVideo();

private:
  QTemporaryDir m_dir;
  int m_case = 0;

  struct Tree {
    QString dataDir;
    QString romDir;
    QString mediaDir;
  };

  /// A fresh ES-DE data dir with its ROM and media roots configured in
  /// es_settings.xml, in the root-less, alphabetically sorted shape ES-DE
  /// really writes.
  Tree newTree() {
    Tree tree;
    tree.dataDir = m_dir.filePath(QStringLiteral("esde%1").arg(m_case++));
    tree.romDir = tree.dataDir + QStringLiteral("/ROMs");
    tree.mediaDir = tree.dataDir + QStringLiteral("/downloaded_media");
    const QString xml =
        QStringLiteral("<?xml version=\"1.0\"?>\n"
                       "<bool name=\"AlternativeEmulatorPerGame\" value=\"true\" />\n"
                       "<string name=\"MediaDirectory\" value=\"%1\" />\n"
                       "<string name=\"ROMDirectory\" value=\"%2\" />\n"
                       "<bool name=\"VirtualKeyboard\" value=\"true\" />\n")
            .arg(tree.mediaDir, tree.romDir);
    write(tree.dataDir + QStringLiteral("/settings/es_settings.xml"), xml.toUtf8());
    return tree;
  }

  static void write(const QString &path, const QByteArray &content) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  /// The games of the tree's only system, read through the same calls the
  /// importer makes: the media root from the settings, then games().
  static QList<EsdeLibrary::Game> readGames(const Tree &tree) {
    const QList<EsdeLibrary::System> systems = EsdeLibrary::systems(tree.romDir);
    if (systems.size() != 1) {
      return {};
    }
    return EsdeLibrary::games(systems.at(0), tree.dataDir,
                              EsdeLibrary::mediaDirectory(tree.dataDir));
  }

  // Header bytes only: the reader matches paths, it never decodes media.
  static QByteArray webpBytes() { return QByteArrayLiteral("RIFF\x1a\0\0\0WEBPVP8L"); }
  static QByteArray videoBytes() {
    return QByteArrayLiteral("\0\0\0\x1c"
                             "ftypisom placeholder");
  }
  static QByteArray mkvBytes() {
    return QByteArrayLiteral("\x1a\x45\xdf\xa3 matroska placeholder");
  }

  static QByteArray readAll(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      return {};
    }
    return f.readAll();
  }

  /// The synced stub for `title`, or an empty path.
  static QString stubFor(const LauncherImportService::SyncResult &result, const QString &title) {
    for (const LauncherImportService::SyncedStub &stub : result.syncedStubs) {
      if (stub.title == title) {
        return stub.path;
      }
    }
    return {};
  }
};

void TestEsdeMediaImport::webpCoverIsImported() {
  const Tree tree = newTree();
  QCOMPARE(EsdeLibrary::mediaDirectory(tree.dataDir), tree.mediaDir);
  write(tree.romDir + QStringLiteral("/snes/Star Quest.sfc"), "rom");
  write(tree.dataDir + QStringLiteral("/gamelists/snes/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n\t<game>\n"
                          "\t\t<path>./Star Quest.sfc</path>\n"
                          "\t\t<name>Star Quest Deluxe</name>\n"
                          "\t</game>\n</gameList>\n"));
  const QString cover = tree.mediaDir + QStringLiteral("/snes/covers/Star Quest.webp");
  write(cover, webpBytes());
  write(tree.mediaDir + QStringLiteral("/snes/videos/Star Quest.mp4"), videoBytes());

  const QList<EsdeLibrary::Game> games = readGames(tree);
  QCOMPARE(games.size(), 1);
  const EsdeLibrary::Game &game = games.at(0);
  QCOMPARE(game.title, QStringLiteral("Star Quest Deluxe"));
  QCOMPARE(game.romPath,
           QFileInfo(tree.romDir + QStringLiteral("/snes/Star Quest.sfc")).absoluteFilePath());
  QCOMPARE(game.coverPath, cover);
  // The video beside it fills no image slot.
  QVERIFY(game.logoPath.isEmpty());
  QVERIFY(game.screenshotPath.isEmpty());
  QVERIFY(game.fanartPath.isEmpty());
}

void TestEsdeMediaImport::existingFormatStillWinsOverWebp() {
  // A re-scrape can leave both an old PNG and a new WebP; the reader's probe
  // order keeps the PNG, so adding WebP did not change existing imports.
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/nes/Game.nes"), "rom");
  const QString png = tree.mediaDir + QStringLiteral("/nes/covers/Game.png");
  write(png, "png");
  write(tree.mediaDir + QStringLiteral("/nes/covers/Game.webp"), webpBytes());
  const QString marquee = tree.mediaDir + QStringLiteral("/nes/marquees/Game.webp");
  write(marquee, webpBytes());

  const QList<EsdeLibrary::Game> games = readGames(tree);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).coverPath, png);
  // WebP alone in its folder is still found.
  QCOMPARE(games.at(0).logoPath, marquee);
}

void TestEsdeMediaImport::gameWithoutMediaHasNoCover() {
  // The WebP cover belongs to its own ROM only, never to a neighbour.
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/nes/Alpha.nes"), "rom");
  write(tree.romDir + QStringLiteral("/nes/Beta.nes"), "rom");
  const QString cover = tree.mediaDir + QStringLiteral("/nes/covers/Alpha.webp");
  write(cover, webpBytes());

  const QList<EsdeLibrary::Game> games = readGames(tree);
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).title, QStringLiteral("Alpha"));
  QCOMPARE(games.at(0).coverPath, cover);
  QCOMPARE(games.at(1).title, QStringLiteral("Beta"));
  QVERIFY(games.at(1).coverPath.isEmpty());
  QVERIFY(games.at(0).videoPath.isEmpty());
  QVERIFY(games.at(1).videoPath.isEmpty());
}

void TestEsdeMediaImport::videosDoNotFillImageSlots() {
  // A game whose only scraped media is video (.mp4 and .mkv, as ES-DE may
  // download) imports with every image slot empty.
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/nes/Game.nes"), "rom");
  write(tree.mediaDir + QStringLiteral("/nes/videos/Game.mp4"), videoBytes());
  write(tree.mediaDir + QStringLiteral("/nes/videos/Game.mkv"), videoBytes());

  const QList<EsdeLibrary::Game> games = readGames(tree);
  QCOMPARE(games.size(), 1);
  QVERIFY(games.at(0).coverPath.isEmpty());
  QVERIFY(games.at(0).logoPath.isEmpty());
  QVERIFY(games.at(0).screenshotPath.isEmpty());
  QVERIFY(games.at(0).fanartPath.isEmpty());
  // Both are carried as the video, never as an image; .mp4 is probed first.
  QCOMPARE(games.at(0).videoPath, tree.mediaDir + QStringLiteral("/nes/videos/Game.mp4"));
}

void TestEsdeMediaImport::mp4VideoIsDiscovered() {
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/snes/Star Quest.sfc"), "rom");
  const QString video = tree.mediaDir + QStringLiteral("/snes/videos/Star Quest.mp4");
  write(video, videoBytes());

  const QList<EsdeLibrary::Game> games = readGames(tree);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).videoPath, video);
}

void TestEsdeMediaImport::mkvVideoIsDiscovered() {
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/snes/Star Quest.sfc"), "rom");
  const QString video = tree.mediaDir + QStringLiteral("/snes/videos/Star Quest.mkv");
  write(video, mkvBytes());

  const QList<EsdeLibrary::Game> games = readGames(tree);
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).videoPath, video);
  QVERIFY(games.at(0).coverPath.isEmpty());
}

void TestEsdeMediaImport::videoDoesNotLeakToAnotherRom() {
  // Media is keyed on the ROM base name: Beta has no video of its own, and a
  // game with no media at all has an empty video slot.
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/nes/Alpha.nes"), "rom");
  write(tree.romDir + QStringLiteral("/nes/Beta.nes"), "rom");
  const QString video = tree.mediaDir + QStringLiteral("/nes/videos/Alpha.mp4");
  write(video, videoBytes());

  const QList<EsdeLibrary::Game> games = readGames(tree);
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).title, QStringLiteral("Alpha"));
  QCOMPARE(games.at(0).videoPath, video);
  QCOMPARE(games.at(1).title, QStringLiteral("Beta"));
  QVERIFY(games.at(1).videoPath.isEmpty());

  // The same holds through the importer and the sync.
  const QList<LauncherImportService::GameEntry> entries =
      LauncherImportService::esdeGames(tree.dataDir, QStringLiteral("nes"));
  QCOMPARE(entries.size(), 2);
  QCOMPARE(entries.at(0).videoPath, video);
  QVERIFY(entries.at(1).videoPath.isEmpty());
  const QString stubDir = tree.dataDir + QStringLiteral("/import/games");
  const QString artworkDir = tree.dataDir + QStringLiteral("/import/artwork");
  const LauncherImportService::SyncResult result = LauncherImportService::syncEntries(
      entries, QLatin1String(LauncherImportService::kSourceEsde), stubDir, artworkDir);
  QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
  const QString videoDir = artworkDir + QStringLiteral("/video");
  QCOMPARE(VideoUtils::findVideoForFile(stubFor(result, QStringLiteral("Alpha")), videoDir),
           videoDir + QStringLiteral("/Alpha.mp4"));
  QVERIFY(
      VideoUtils::findVideoForFile(stubFor(result, QStringLiteral("Beta")), videoDir).isEmpty());
}

void TestEsdeMediaImport::importedItemKeepsWebpCoverAndVideo() {
  // The full import path: the real reader, the importer's mapping, and the
  // sync that writes the stub and fills its artwork folder.
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/snes/Star Quest.sfc"), "rom");
  write(tree.dataDir + QStringLiteral("/gamelists/snes/gamelist.xml"),
        QByteArrayLiteral("<?xml version=\"1.0\"?>\n<gameList>\n\t<game>\n"
                          "\t\t<path>./Star Quest.sfc</path>\n"
                          "\t\t<name>Star Quest Deluxe</name>\n"
                          "\t</game>\n</gameList>\n"));
  const QString cover = tree.mediaDir + QStringLiteral("/snes/covers/Star Quest.webp");
  write(cover, webpBytes());
  const QString video = tree.mediaDir + QStringLiteral("/snes/videos/Star Quest.mp4");
  write(video, videoBytes());

  const QList<LauncherImportService::GameEntry> entries =
      LauncherImportService::esdeGames(tree.dataDir, QStringLiteral("snes"));
  QCOMPARE(entries.size(), 1);
  const LauncherImportService::GameEntry &entry = entries.at(0);
  QCOMPARE(entry.title, QStringLiteral("Star Quest Deluxe"));
  QCOMPARE(entry.coverPath, cover);
  QCOMPARE(entry.videoPath, video);
  QVERIFY(entry.logoPath.isEmpty());
  QVERIFY(entry.heroPath.isEmpty());

  const QString stubDir = tree.dataDir + QStringLiteral("/import/games");
  const QString artworkDir = tree.dataDir + QStringLiteral("/import/artwork");
  const LauncherImportService::SyncResult result = LauncherImportService::syncEntries(
      entries, QLatin1String(LauncherImportService::kSourceEsde), stubDir, artworkDir);
  QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
  QCOMPARE(result.written, 1);
  QCOMPARE(result.artworkCopied, 2);
  const QString stub = stubFor(result, QStringLiteral("Star Quest Deluxe"));
  QVERIFY(!stub.isEmpty());

  // The SAME item carries both: its cover in front/ and its video in video/,
  // each under the stub's base name and byte-identical to the ES-DE file.
  const QString storedCover = artworkDir + QStringLiteral("/front/Star Quest Deluxe.webp");
  QCOMPARE(readAll(storedCover), webpBytes());
  const QString storedVideo = artworkDir + QStringLiteral("/video/Star Quest Deluxe.mp4");
  QCOMPARE(VideoUtils::findVideoForFile(stub, artworkDir + QStringLiteral("/video")), storedVideo);
  QCOMPARE(readAll(storedVideo), videoBytes());
  // The video went nowhere else.
  QVERIFY(!QFileInfo::exists(artworkDir + QStringLiteral("/fanart")));
  QVERIFY(!QFileInfo::exists(artworkDir + QStringLiteral("/logo")));
}

void TestEsdeMediaImport::importedItemKeepsMkvVideo() {
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/nes/Game.nes"), "rom");
  const QString cover = tree.mediaDir + QStringLiteral("/nes/covers/Game.webp");
  write(cover, webpBytes());
  const QString video = tree.mediaDir + QStringLiteral("/nes/videos/Game.mkv");
  write(video, mkvBytes());

  const QList<LauncherImportService::GameEntry> entries =
      LauncherImportService::esdeGames(tree.dataDir, QStringLiteral("nes"));
  QCOMPARE(entries.size(), 1);
  QCOMPARE(entries.at(0).coverPath, cover);
  QCOMPARE(entries.at(0).videoPath, video);

  const QString stubDir = tree.dataDir + QStringLiteral("/import/games");
  const QString artworkDir = tree.dataDir + QStringLiteral("/import/artwork");
  const LauncherImportService::SyncResult result = LauncherImportService::syncEntries(
      entries, QLatin1String(LauncherImportService::kSourceEsde), stubDir, artworkDir);
  QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
  const QString stub = stubFor(result, QStringLiteral("Game"));
  QCOMPARE(readAll(artworkDir + QStringLiteral("/front/Game.webp")), webpBytes());
  const QString storedVideo = artworkDir + QStringLiteral("/video/Game.mkv");
  QCOMPARE(VideoUtils::findVideoForFile(stub, artworkDir + QStringLiteral("/video")), storedVideo);
  QCOMPARE(readAll(storedVideo), mkvBytes());
}

void TestEsdeMediaImport::importDoesNotClobberExistingVideo() {
  // Fill-missing, like the images: a video already in the slot (scraped or
  // hand-placed, in any video format) survives an import and a re-sync.
  const Tree tree = newTree();
  write(tree.romDir + QStringLiteral("/nes/Game.nes"), "rom");
  write(tree.mediaDir + QStringLiteral("/nes/videos/Game.mp4"), videoBytes());
  const QString artworkDir = tree.dataDir + QStringLiteral("/import/artwork");
  const QString existing = artworkDir + QStringLiteral("/video/Game.webm");
  write(existing, "hand-placed");

  const QList<LauncherImportService::GameEntry> entries =
      LauncherImportService::esdeGames(tree.dataDir, QStringLiteral("nes"));
  QCOMPARE(entries.size(), 1);
  const QString stubDir = tree.dataDir + QStringLiteral("/import/games");
  for (int pass = 0; pass < 2; ++pass) {
    const LauncherImportService::SyncResult result = LauncherImportService::syncEntries(
        entries, QLatin1String(LauncherImportService::kSourceEsde), stubDir, artworkDir);
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.artworkCopied, 0);
  }
  QVERIFY(!QFileInfo::exists(artworkDir + QStringLiteral("/video/Game.mp4")));
  QCOMPARE(readAll(existing), QByteArrayLiteral("hand-placed"));
}

QTEST_MAIN(TestEsdeMediaImport)
#include "test_esde_media_import.moc"
