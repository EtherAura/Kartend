// Tests for PlaceholderWarmer.
//
// Preflight branches are pure logic on top of QFileInfo/QDir, so we exercise
// them with QTemporaryDir-backed fixtures. The export branch is also tested
// end-to-end by injecting a fake TileFactory that returns a 1x1 red pixmap;
// this avoids pulling in ItemWidget (and a QApplication) just to verify the
// warmer's bookkeeping.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTest>

#include "collectionutils.h"
#include "placeholderwarmer.h"

namespace {

/// Touch an empty file inside @p dir at the relative path @p name. Used to
/// stand in for media files in the tests; only the suffix matters since the
/// warmer never opens the file body.
void touchFile(const QString &dir, const QString &name) {
  QFile f(dir + QLatin1Char('/') + name);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();
}

CollectionConfig collectionWith(const QString &mediaDir, const QString &artworkDir,
                                const QStringList &exts) {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Test");
  cfg.mediaDirectory = mediaDir;
  cfg.artworkDirectory = artworkDir;
  cfg.extensions = exts;
  return cfg;
}

/// 1x1 red pixmap. Cheap stand-in for the real placeholder so the test
/// doesn't need ItemWidget or any of its theme statics.
QPixmap fakeTile(int w, int h, int /*radius*/) {
  QPixmap p(std::max(1, w), std::max(1, h));
  p.fill(Qt::red);
  return p;
}

} // namespace

class TestPlaceholderWarmer : public QObject {
  Q_OBJECT
private slots:
  void preflight_rejectsEmptyMediaDirectory();
  void preflight_rejectsEmptyArtworkDirectory();
  void preflight_rejectsMissingMediaDirectory();
  void preflight_rejectsCollectionWithNoExtensions();
  void preflight_acceptsValidConfigAndCreatesArtworkDir();
  void preflight_overrideShadowsSavedArtworkDir();
  void export_writesPngsForEachUnmatchedItem();
  void export_skipsItemsWithExistingArtwork();
  void export_filtersByExtensionAndIsCaseInsensitive();
  void export_recursesIntoSubdirectories();
};

void TestPlaceholderWarmer::preflight_rejectsEmptyMediaDirectory() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  auto cfg = collectionWith(QString(), tmp.path() + "/art", {".mp4"});
  const auto pre = PlaceholderWarmer::preflight(cfg, QString());
  QCOMPARE(pre.error, PlaceholderWarmer::PreflightError::EmptyMediaDirectory);
  QVERIFY(!pre.humanMessage.isEmpty());
}

void TestPlaceholderWarmer::preflight_rejectsEmptyArtworkDirectory() {
  QTemporaryDir media;
  QVERIFY(media.isValid());
  auto cfg = collectionWith(media.path(), QString(), {".mp4"});
  const auto pre = PlaceholderWarmer::preflight(cfg, QString());
  QCOMPARE(pre.error, PlaceholderWarmer::PreflightError::EmptyArtworkDirectory);
}

void TestPlaceholderWarmer::preflight_rejectsMissingMediaDirectory() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  // Path that doesn't exist on disk; pathutils::validateAndExpandPath
  // returns empty so the error surfaces as MediaDirectoryMissing.
  auto cfg = collectionWith(tmp.path() + "/no-such-dir", tmp.path() + "/art", {".mp4"});
  const auto pre = PlaceholderWarmer::preflight(cfg, QString());
  QCOMPARE(pre.error, PlaceholderWarmer::PreflightError::MediaDirectoryMissing);
}

void TestPlaceholderWarmer::preflight_rejectsCollectionWithNoExtensions() {
  QTemporaryDir media;
  QTemporaryDir art;
  QVERIFY(media.isValid() && art.isValid());
  auto cfg = collectionWith(media.path(), art.path(), {});
  const auto pre = PlaceholderWarmer::preflight(cfg, QString());
  QCOMPARE(pre.error, PlaceholderWarmer::PreflightError::NoExtensionsConfigured);
}

void TestPlaceholderWarmer::preflight_acceptsValidConfigAndCreatesArtworkDir() {
  QTemporaryDir media;
  QTemporaryDir parent;
  QVERIFY(media.isValid() && parent.isValid());
  // Artwork dir intentionally doesn't exist yet; preflight should mkpath it.
  const QString artPath = parent.path() + "/art-fresh";
  QVERIFY(!QFileInfo(artPath).exists());

  auto cfg = collectionWith(media.path(), artPath, {".mp4"});
  const auto pre = PlaceholderWarmer::preflight(cfg, QString());
  QCOMPARE(pre.error, PlaceholderWarmer::PreflightError::None);
  QVERIFY(QFileInfo(artPath).isDir());
  QCOMPARE(pre.resolvedMediaDirectory, media.path());
  QCOMPARE(pre.resolvedArtworkDirectory, artPath);
}

void TestPlaceholderWarmer::preflight_overrideShadowsSavedArtworkDir() {
  QTemporaryDir media;
  QTemporaryDir savedArt;
  QTemporaryDir overrideArt;
  QVERIFY(media.isValid() && savedArt.isValid() && overrideArt.isValid());

  auto cfg = collectionWith(media.path(), savedArt.path(), {".mp4"});
  const auto pre = PlaceholderWarmer::preflight(cfg, overrideArt.path());
  QCOMPARE(pre.error, PlaceholderWarmer::PreflightError::None);
  // Override beats the saved value — matches the panel-line-edit pattern.
  QCOMPARE(pre.resolvedArtworkDirectory, overrideArt.path());
}

void TestPlaceholderWarmer::export_writesPngsForEachUnmatchedItem() {
  QTemporaryDir media;
  QTemporaryDir art;
  QVERIFY(media.isValid() && art.isValid());
  touchFile(media.path(), "alpha.mp4");
  touchFile(media.path(), "beta.mp4");
  touchFile(media.path(), "gamma.mp4");

  auto cfg = collectionWith(media.path(), art.path(), {".mp4"});
  const auto result = PlaceholderWarmer::exportMissingPlaceholders(cfg, QString(), 4, 4, 0,
                                                                   &fakeTile);
  QCOMPARE(result.itemsScanned, qint64(3));
  QCOMPARE(result.itemsExported, qint64(3));
  QCOMPARE(result.itemsAlreadyHadArtwork, qint64(0));
  QCOMPARE(result.itemsFailed, qint64(0));
  QVERIFY(QFileInfo(art.path() + "/alpha.png").exists());
  QVERIFY(QFileInfo(art.path() + "/beta.png").exists());
  QVERIFY(QFileInfo(art.path() + "/gamma.png").exists());
}

void TestPlaceholderWarmer::export_skipsItemsWithExistingArtwork() {
  QTemporaryDir media;
  QTemporaryDir art;
  QVERIFY(media.isValid() && art.isValid());
  touchFile(media.path(), "alpha.mp4");
  touchFile(media.path(), "beta.mp4");
  // Pre-existing cover for alpha; warmer must leave it alone and not
  // double-count.
  touchFile(art.path(), "alpha.png");

  auto cfg = collectionWith(media.path(), art.path(), {".mp4"});
  const auto result = PlaceholderWarmer::exportMissingPlaceholders(cfg, QString(), 4, 4, 0,
                                                                   &fakeTile);
  QCOMPARE(result.itemsScanned, qint64(2));
  QCOMPARE(result.itemsAlreadyHadArtwork, qint64(1));
  QCOMPARE(result.itemsExported, qint64(1));
  // Pre-existing alpha.png is untouched (still empty — fakeTile would have
  // produced a non-empty PNG).
  QCOMPARE(QFileInfo(art.path() + "/alpha.png").size(), qint64(0));
  QVERIFY(QFileInfo(art.path() + "/beta.png").size() > 0);
}

void TestPlaceholderWarmer::export_filtersByExtensionAndIsCaseInsensitive() {
  QTemporaryDir media;
  QTemporaryDir art;
  QVERIFY(media.isValid() && art.isValid());
  touchFile(media.path(), "video.MP4");      // uppercase, should match
  touchFile(media.path(), "audio.flac");     // not in extensions list
  touchFile(media.path(), "doc.pdf");        // not in extensions list
  touchFile(media.path(), "another.mp4");    // matches

  // Extensions stored both with and without leading dot — both shapes occur
  // in real CollectionConfigs depending on how the user typed them.
  auto cfg = collectionWith(media.path(), art.path(), {QStringLiteral("mp4")});
  const auto result = PlaceholderWarmer::exportMissingPlaceholders(cfg, QString(), 4, 4, 0,
                                                                   &fakeTile);
  QCOMPARE(result.itemsScanned, qint64(2));
  QCOMPARE(result.itemsExported, qint64(2));
  QVERIFY(QFileInfo(art.path() + "/video.png").exists());
  QVERIFY(QFileInfo(art.path() + "/another.png").exists());
  QVERIFY(!QFileInfo(art.path() + "/audio.png").exists());
  QVERIFY(!QFileInfo(art.path() + "/doc.png").exists());
}

void TestPlaceholderWarmer::export_recursesIntoSubdirectories() {
  QTemporaryDir media;
  QTemporaryDir art;
  QVERIFY(media.isValid() && art.isValid());
  QVERIFY(QDir().mkpath(media.path() + "/season1"));
  QVERIFY(QDir().mkpath(media.path() + "/season2/disc1"));
  touchFile(media.path() + "/season1", "episode1.mp4");
  touchFile(media.path() + "/season2/disc1", "episode2.mp4");

  auto cfg = collectionWith(media.path(), art.path(), {".mp4"});
  const auto result = PlaceholderWarmer::exportMissingPlaceholders(cfg, QString(), 4, 4, 0,
                                                                   &fakeTile);
  QCOMPARE(result.itemsScanned, qint64(2));
  QCOMPARE(result.itemsExported, qint64(2));
  // The warmer flattens output into the artwork dir keyed on basename
  // (matches the resolver's lookup); the directory hierarchy from the
  // media tree is intentionally not preserved.
  QVERIFY(QFileInfo(art.path() + "/episode1.png").exists());
  QVERIFY(QFileInfo(art.path() + "/episode2.png").exists());
}

QTEST_MAIN(TestPlaceholderWarmer)
#include "test_placeholderwarmer.moc"
