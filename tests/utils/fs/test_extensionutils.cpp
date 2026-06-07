#include <QImageReader>
#include <QTest>

#include "extensionutils.h"

// Covers ExtensionUtils::isDecodableImagePath — the allowlist guard that
// keeps non-image files out of QImageReader / QPixmap. Regression test for
// the large-scrape crash where a scraped `.pdf` manual reached Qt's
// PDFium-backed PDF image plugin, which abort()ed the whole process.
class TestExtensionUtils : public QObject {
  Q_OBJECT
private slots:
  void imagePathsAreDecodable();
  void nonImagePathsAreRejected();
  void pdfManualIsRejected();
  void extensionMatchIsCaseInsensitive();
  void webpIsAlwaysDecodable();
  void avifTracksRuntimePluginSupport();
  void videoBaseExtensions_returnsKnownExtensions();
};

void TestExtensionUtils::imagePathsAreDecodable() {
  const QStringList paths = {QStringLiteral("/art/front.png"), QStringLiteral("cover.jpg"),
                             QStringLiteral("box.jpeg"), QStringLiteral("logo.bmp"),
                             QStringLiteral("anim.gif")};
  for (const QString &p : paths) {
    QVERIFY2(ExtensionUtils::isDecodableImagePath(p), qPrintable(p));
  }
}

void TestExtensionUtils::nonImagePathsAreRejected() {
  const QStringList paths = {QString(),
                             QStringLiteral("noextension"),
                             QStringLiteral("/a/manual.epub"),
                             QStringLiteral("readme.txt"),
                             QStringLiteral("bundle.zip"),
                             QStringLiteral("clip.mp4")};
  for (const QString &p : paths) {
    QVERIFY2(!ExtensionUtils::isDecodableImagePath(p),
             qPrintable(p.isEmpty() ? QStringLiteral("<empty>") : p));
  }
}

void TestExtensionUtils::pdfManualIsRejected() {
  // The exact crash vector: a scraped manual written as a .pdf must never
  // be treated as a decodable image.
  QVERIFY(!ExtensionUtils::isDecodableImagePath(
      QStringLiteral("/games/Artwork/manual/Family Mahjong (Japan).pdf")));
  QVERIFY(!ExtensionUtils::isDecodableImagePath(QStringLiteral("x.PDF")));
}

void TestExtensionUtils::extensionMatchIsCaseInsensitive() {
  QVERIFY(ExtensionUtils::isDecodableImagePath(QStringLiteral("A.PNG")));
  QVERIFY(ExtensionUtils::isDecodableImagePath(QStringLiteral("scan.JpEg")));
  // Dotted base names: only the final suffix decides.
  QVERIFY(ExtensionUtils::isDecodableImagePath(QStringLiteral("My.Game.v1.2.jpg")));
  QVERIFY(!ExtensionUtils::isDecodableImagePath(QStringLiteral("My.png.manual.pdf")));
}

void TestExtensionUtils::webpIsAlwaysDecodable() {
  // qt6-imageformats is a hard dep in our packaging, so webp is admitted
  // unconditionally. Modern scrapers (ScreenScraper, IGDB, TheGamesDB)
  // routinely return webp for thumbs.
  QVERIFY(ExtensionUtils::isDecodableImagePath(QStringLiteral("cover.webp")));
  QVERIFY(ExtensionUtils::isDecodableImagePath(QStringLiteral("art.WEBP")));
}

void TestExtensionUtils::avifTracksRuntimePluginSupport() {
  // avif is gated on QImageReader's reported plugin set — the unit
  // contract is: the extension is admitted iff Qt actually has a decoder
  // for it. This test pins that contract so a regression that
  // unconditionally admits avif (or unconditionally rejects it) fails.
  const auto formats = QImageReader::supportedImageFormats();
  const bool hasAvif =
      formats.contains(QByteArrayLiteral("avif")) || formats.contains(QByteArrayLiteral("AVIF"));
  const bool admitted = ExtensionUtils::isDecodableImagePath(QStringLiteral("cover.avif"));
  QCOMPARE(admitted, hasAvif);
}

void TestExtensionUtils::videoBaseExtensions_returnsKnownExtensions() {
  const QStringList &exts = ExtensionUtils::videoBaseExtensions();
  QVERIFY(exts.contains(QStringLiteral("mp4")));
  QVERIFY(exts.contains(QStringLiteral("webm")));
  QVERIFY(exts.contains(QStringLiteral("mkv")));
  QVERIFY(exts.contains(QStringLiteral("mov")));
  QVERIFY(exts.contains(QStringLiteral("avi")));
}

QTEST_MAIN(TestExtensionUtils)
#include "test_extensionutils.moc"
