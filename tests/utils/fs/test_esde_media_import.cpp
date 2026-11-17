// EsdeLibrary media discovery for the formats ES-DE 3.4/3.5 scrapes
// (Kartend-vtz2h): WebP images and video files alongside the usual JPG/PNG.
//
// ES-DE finds media by naming convention, not by gamelist tags:
// <MediaDirectory>/<system>/<type>/<ROM base name>.<ext>, with images under
// covers/, marquees/, screenshots/, fanart/ and videos under videos/. The trees
// below follow that layout and go through the real reader.
//
// What is covered: a .webp cover reaches Game::coverPath, and video files in
// videos/ never land in an image slot. What is NOT: EsdeLibrary::Game has no
// video field (nor does the importer's GameEntry), so no video path is carried
// at all. The video files here are non-empty placeholders, not encoded AV1.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "esdelibrary.h"

class TestEsdeMediaImport : public QObject {
  Q_OBJECT

private slots:
  void webpCoverIsImported();
  void existingFormatStillWinsOverWebp();
  void gameWithoutMediaHasNoCover();
  void videosDoNotFillImageSlots();

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
}

QTEST_MAIN(TestEsdeMediaImport)
#include "test_esde_media_import.moc"
