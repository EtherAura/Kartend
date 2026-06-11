#include <QImageReader>
#include <QRegularExpression>
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
  void normalizeStoredExtensions_canonicalizesToBare();
  void normalizeStoredExtensions_isIdempotent();
  void parseUserExtensionList_handlesSeparatorsAndForms();
  void toNameFilters_singleGlobPrefixFromAnyForm();
  void nameFiltersMatchSingleDotFilenames();
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

// Kartend-693zb: the canonical stored form for collection extensions is
// BARE lowercase ("mp4"). The old canonical glob form ("*.mp4") made every
// consumer that prepends "*." compose the never-matching "*.*.mp4" — Qt
// wildcard matching requires TWO literal dots for that pattern, so
// extension-filtered scans found nothing after an INI round-trip.
void TestExtensionUtils::normalizeStoredExtensions_canonicalizesToBare() {
  const QStringList raw = {QStringLiteral("*.MP4"),
                           QStringLiteral(".mkv"),
                           QStringLiteral(" avi "),
                           QStringLiteral("mp4"),
                           QString(),
                           QStringLiteral("  ")};
  const QStringList expected = {QStringLiteral("mp4"), QStringLiteral("mkv"),
                                QStringLiteral("avi")};
  QCOMPARE(ExtensionUtils::normalizeStoredExtensions(raw), expected);
}

void TestExtensionUtils::normalizeStoredExtensions_isIdempotent() {
  // The INI load path normalizes on every startup and rewrites when the
  // result differs — a non-idempotent normalize would rewrite forever.
  const QStringList once =
      ExtensionUtils::normalizeStoredExtensions({QStringLiteral("*.mp4"), QStringLiteral("Mkv")});
  QCOMPARE(ExtensionUtils::normalizeStoredExtensions(once), once);
}

void TestExtensionUtils::parseUserExtensionList_handlesSeparatorsAndForms() {
  const QStringList parsed =
      ExtensionUtils::parseUserExtensionList(QStringLiteral("mp4, *.MKV; .avi\tmov  mp4"));
  const QStringList expected = {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
                                QStringLiteral("mov")};
  QCOMPARE(parsed, expected);
}

void TestExtensionUtils::toNameFilters_singleGlobPrefixFromAnyForm() {
  // Glob composition must be tolerant of every historical stored form so a
  // pre-fix glob token can never double-prefix.
  const QStringList filters = ExtensionUtils::toNameFilters(
      {QStringLiteral("mp4"), QStringLiteral("*.mkv"), QStringLiteral(".avi"), QString()});
  const QStringList expected = {QStringLiteral("*.mp4"), QStringLiteral("*.mkv"),
                                QStringLiteral("*.avi")};
  QCOMPARE(filters, expected);
}

void TestExtensionUtils::nameFiltersMatchSingleDotFilenames() {
  // The empirical core of Kartend-693zb: "*.mp4" matches "video.mp4" but
  // "*.*.mp4" does not. Pin the composed filter against Qt's own matcher.
  const QStringList filters =
      ExtensionUtils::toNameFilters({QStringLiteral("*.mp4")}); // worst-case legacy form
  QCOMPARE(filters, QStringList{QStringLiteral("*.mp4")});
  const QRegularExpression rx =
      QRegularExpression::fromWildcard(filters.first(), Qt::CaseInsensitive);
  QVERIFY(rx.match(QStringLiteral("video.mp4")).hasMatch());
  // And the pre-fix composed pattern really never matched single-dot names.
  const QRegularExpression broken =
      QRegularExpression::fromWildcard(QStringLiteral("*.*.mp4"), Qt::CaseInsensitive);
  QVERIFY(!broken.match(QStringLiteral("video.mp4")).hasMatch());
}

QTEST_MAIN(TestExtensionUtils)
#include "test_extensionutils.moc"
