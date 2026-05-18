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

QTEST_MAIN(TestExtensionUtils)
#include "test_extensionutils.moc"
