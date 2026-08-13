// Tests for ArtworkUtils::mirroredArtworkDirectory (Kartend-j5amz) — the one
// place that decides which directory an item's cover is looked up in when a
// collection mirrors its media subfolders into its artwork folder. Pure string
// work over absolute paths, so no filesystem is touched.
#include <QObject>
#include <QString>
#include <QTest>

#include "artworkutils.h"

namespace {
// Shapes used throughout. Absolute so QDir::relativeFilePath never consults
// the working directory.
const QString kMedia = QStringLiteral("/library/media");
const QString kArt = QStringLiteral("/library/art");
} // namespace

class TestArtworkMirror : public QObject {
  Q_OBJECT
private slots:
  void subfolderItem_mirrorsIntoArtworkTree();
  void nestedSubfolders_mirrorTheWholeChain();
  void rootItem_keepsArtworkRoot();
  void mirroringDisabled_keepsArtworkRoot();
  void coLocatedArtwork_mirrorsWithoutTheFlag();
  void generatedPlaylistOutsideMedia_keepsArtworkRoot();
  void generatedPlaylistOutsideMedia_coLocated_keepsMediaRoot();
  void canonicalPathOutsideConfiguredMedia_keepsArtworkRoot();
  void siblingDirectoryWithSharedPrefix_keepsArtworkRoot();
  void emptyInputs_returnArtworkDirectoryUnchanged();
};

void TestArtworkMirror::subfolderItem_mirrorsIntoArtworkTree() {
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(
               kArt, kMedia, QStringLiteral("/library/media/Live/Recital.flac"), true),
           QStringLiteral("/library/art/Live"));
}

void TestArtworkMirror::nestedSubfolders_mirrorTheWholeChain() {
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(
               kArt, kMedia, QStringLiteral("/library/media/Live/1998/Recital.flac"), true),
           QStringLiteral("/library/art/Live/1998"));
}

void TestArtworkMirror::rootItem_keepsArtworkRoot() {
  // Nothing to mirror for an item sitting at the media root.
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(
               kArt, kMedia, QStringLiteral("/library/media/Recital.flac"), true),
           kArt);
}

void TestArtworkMirror::mirroringDisabled_keepsArtworkRoot() {
  // includeArtworkSubfolders off and the two directories differ: the artwork
  // folder is flat, so a subfolder item still resolves against its root.
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(
               kArt, kMedia, QStringLiteral("/library/media/Live/Recital.flac"), false),
           kArt);
}

void TestArtworkMirror::coLocatedArtwork_mirrorsWithoutTheFlag() {
  // artworkDirectory == mediaDirectory means the covers ARE the media tree,
  // so the mirror applies with the flag off.
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(
               kMedia, kMedia, QStringLiteral("/library/media/Live/Recital.flac"), false),
           QStringLiteral("/library/media/Live"));
}

void TestArtworkMirror::generatedPlaylistOutsideMedia_keepsArtworkRoot() {
  // The Kartend-j5amz case: a collapsed multi-disc item's file is the playlist
  // Kartend generates under its own data directory, which is not under the
  // media directory at all. Mirroring the relative path would build
  // "/library/art/../../home/u/.local/share/kartend/..." and search a
  // directory belonging to neither setting.
  const QString playlist =
      QStringLiteral("/home/u/.local/share/kartend/multi-disc/abc-123/Recital.m3u");
  const QString resolved = ArtworkUtils::mirroredArtworkDirectory(kArt, kMedia, playlist, true);
  QCOMPARE(resolved, kArt);
  QVERIFY(!resolved.contains(QStringLiteral("..")));
}

void TestArtworkMirror::generatedPlaylistOutsideMedia_coLocated_keepsMediaRoot() {
  // Same item under the co-located layout, which triggers the mirror without
  // the flag. The answer is the artwork directory as configured.
  const QString playlist =
      QStringLiteral("/home/u/.local/share/kartend/multi-disc/abc-123/Recital.m3u");
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(kMedia, kMedia, playlist, false), kMedia);
}

void TestArtworkMirror::canonicalPathOutsideConfiguredMedia_keepsArtworkRoot() {
  // The shipped-behaviour half: a load that keys items by their canonical path
  // hands over "/mnt/store/media/..." while the collection is configured with
  // the symlink's own spelling "/library/media". Same rule, same answer.
  const QString canonical = QStringLiteral("/mnt/store/media/Live/Recital.flac");
  const QString resolved = ArtworkUtils::mirroredArtworkDirectory(kArt, kMedia, canonical, true);
  QCOMPARE(resolved, kArt);
  QVERIFY(!resolved.contains(QStringLiteral("..")));
}

void TestArtworkMirror::siblingDirectoryWithSharedPrefix_keepsArtworkRoot() {
  // "/library/media-archive" starts with the media directory's spelling but is
  // not under it — a prefix test would mirror it, a path test must not.
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(
               kArt, kMedia, QStringLiteral("/library/media-archive/Recital.flac"), true),
           kArt);
}

void TestArtworkMirror::emptyInputs_returnArtworkDirectoryUnchanged() {
  const QString item = QStringLiteral("/library/media/Live/Recital.flac");
  // No media directory: nothing to be relative to.
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(kArt, QString(), item, true), kArt);
  // No item path: nothing to place.
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(kArt, kMedia, QString(), true), kArt);
  // No artwork directory: no root to mirror into, and in particular no
  // relative "Live" that would resolve against the working directory.
  QCOMPARE(ArtworkUtils::mirroredArtworkDirectory(QString(), kMedia, item, true), QString());
}

QTEST_MAIN(TestArtworkMirror)
#include "test_artworkmirror.moc"
