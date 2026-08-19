// Coverage for CollectionUtils::resolvedCollectionIcon (Kartend-dkh90): the
// single seam every collectionIcon consumer (Cover Flow card, marquee banner,
// Grid/List subcollection tile) resolves the key through. The value is a
// SINGLE ASSET FILE path, so the resolver must expand `~` and `%collection%`
// WITHOUT requiring the path to exist — validateAndExpandPath's existence
// check is QDir::exists, which is true only for directories, and would empty
// every real image file (Kartend-80h8o).
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"

namespace {
CollectionConfig makeCollection(const QString &name, const QString &icon) {
  CollectionConfig c;
  c.name = name;
  c.collectionIcon = icon;
  return c;
}
} // namespace

class TestCollectionUtilsIcon : public QObject {
  Q_OBJECT
private slots:
  void emptyAndWhitespaceYieldEmpty();
  void absolutePathPassesThroughUntouched();
  void nonexistentFileIsNotRejected();
  void tildeExpandsToHome();
  void collectionVariableExpandsWithCollectionName();
  void traversalUnsafeNameLeavesVariableLiteral();

  // resolvedAssetPath — the generalised seam (Kartend-4wa6i) the icon
  // resolver delegates to; background image/video and header logo route
  // their raw config values through it.
  void assetPathSharesTheIconSeamSemantics();

  // resolveCollectionTileArtwork — the full two-step tile policy
  // (Kartend-kb2vx order), factored out of ItemWidgetFactoryHelpers so the
  // collection tree (Kartend-ob1c9.1) shares it rather than copying it.
  void tileArtworkPrefersOwnIconOverParentDirMatch();
  void tileArtworkFallsBackToParentDirImageNamedAfterChild();
  void tileArtworkEmptyForRootsAndUnknownNames();
};

void TestCollectionUtilsIcon::emptyAndWhitespaceYieldEmpty() {
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(makeCollection("Films", QString())), QString());
  // Whitespace is not a path: it must not survive as an unloadable value.
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(makeCollection("Films", "   ")), QString());
}

void TestCollectionUtilsIcon::absolutePathPassesThroughUntouched() {
  const QString icon = QStringLiteral("/srv/icons/films.png");
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(makeCollection("Films", icon)), icon);
  // Trimming is part of the seam — a hand-edited INI can carry stray spaces.
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(makeCollection("Films", "  " + icon + " ")),
           icon);
}

void TestCollectionUtilsIcon::nonexistentFileIsNotRejected() {
  // The consumers tolerate a missing file (the load just fails); the resolver
  // rejecting it here would instead silently blank an icon the user is about
  // to create — and is exactly the directory-shaped existence trap this
  // helper deliberately avoids.
  const QString icon = QStringLiteral("/nonexistent/kartend-test/12345.png");
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(makeCollection("Films", icon)), icon);
}

void TestCollectionUtilsIcon::tildeExpandsToHome() {
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(makeCollection("Films", "~/icons/films.png")),
           QDir::homePath() + QStringLiteral("/icons/films.png"));
}

void TestCollectionUtilsIcon::collectionVariableExpandsWithCollectionName() {
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(
               makeCollection("Films", "/srv/icons/%collection%.png")),
           QStringLiteral("/srv/icons/Films.png"));
}

void TestCollectionUtilsIcon::traversalUnsafeNameLeavesVariableLiteral() {
  // Same seam guard as every other %collection% expansion (Kartend-2ml9): a
  // traversal-unsafe collection name must not be substituted; the literal
  // placeholder then fails the downstream load instead of escaping the root.
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(
               makeCollection("../../etc", "/srv/icons/%collection%.png")),
           QStringLiteral("/srv/icons/%collection%.png"));
}

void TestCollectionUtilsIcon::assetPathSharesTheIconSeamSemantics() {
  // Same trim/expansion rules as the icon overload — pinned so the two can
  // never drift apart (resolvedCollectionIcon is a delegate).
  QCOMPARE(CollectionUtils::resolvedAssetPath(QString(), QStringLiteral("Films")), QString());
  QCOMPARE(CollectionUtils::resolvedAssetPath(QStringLiteral("   "), QStringLiteral("Films")),
           QString());
  QCOMPARE(CollectionUtils::resolvedAssetPath(QStringLiteral("~/bg/wall.mp4"), QString()),
           QDir::homePath() + QStringLiteral("/bg/wall.mp4"));
  QCOMPARE(CollectionUtils::resolvedAssetPath(QStringLiteral("/srv/bg/%collection%.png"),
                                              QStringLiteral("Films")),
           QStringLiteral("/srv/bg/Films.png"));
  QCOMPARE(CollectionUtils::resolvedCollectionIcon(makeCollection("Films", "~/icons/films.png")),
           CollectionUtils::resolvedAssetPath(QStringLiteral("~/icons/films.png"),
                                              QStringLiteral("Films")));
}

void TestCollectionUtilsIcon::tileArtworkPrefersOwnIconOverParentDirMatch() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  // Parent dir carries an image named after the child — the FALLBACK. The
  // child's explicit collectionIcon must still win (Kartend-kb2vx order; the
  // historical bug was exactly this inverted).
  QFile parentDirImage(tmp.path() + QStringLiteral("/Child A.png"));
  QVERIFY(parentDirImage.open(QIODevice::WriteOnly));
  parentDirImage.write("png");
  parentDirImage.close();
  QList<CollectionConfig> collections = {
      makeCollection(QStringLiteral("Child A"), QStringLiteral("/explicit/icon.png"))};
  QCOMPARE(CollectionUtils::resolveCollectionTileArtwork(&collections, 0, QStringLiteral("Child A"),
                                                         tmp.path()),
           QStringLiteral("/explicit/icon.png"));
}

void TestCollectionUtilsIcon::tileArtworkFallsBackToParentDirImageNamedAfterChild() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile parentDirImage(tmp.path() + QStringLiteral("/Child B.png"));
  QVERIFY(parentDirImage.open(QIODevice::WriteOnly));
  parentDirImage.write("png");
  parentDirImage.close();
  QList<CollectionConfig> collections = {
      makeCollection(QStringLiteral("Child B"), /*icon=*/QString())};
  QCOMPARE(CollectionUtils::resolveCollectionTileArtwork(&collections, 0, QStringLiteral("Child B"),
                                                         tmp.path()),
           tmp.path() + QStringLiteral("/Child B.png"));
}

void TestCollectionUtilsIcon::tileArtworkEmptyForRootsAndUnknownNames() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QList<CollectionConfig> collections = {
      makeCollection(QStringLiteral("Root"), /*icon=*/QString())};
  // A root has no parent artwork dir — step 2 is skipped, empty means
  // "text-only row", never an error and never a placeholder.
  QCOMPARE(CollectionUtils::resolveCollectionTileArtwork(&collections, 0, QStringLiteral("Root"),
                                                         QString()),
           QString());
  // Parent dir present but holds nothing named after the child.
  QCOMPARE(CollectionUtils::resolveCollectionTileArtwork(&collections, 0, QStringLiteral("Root"),
                                                         tmp.path()),
           QString());
  // Out-of-range index degrades to the name-only fallback, not a crash.
  QCOMPARE(CollectionUtils::resolveCollectionTileArtwork(&collections, 99, QStringLiteral("Root"),
                                                         tmp.path()),
           QString());
}

QTEST_MAIN(TestCollectionUtilsIcon)
#include "test_collectionutils_icon.moc"
