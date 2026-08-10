// Coverage for CollectionUtils::resolvedCollectionIcon (Kartend-dkh90): the
// single seam every collectionIcon consumer (Cover Flow card, marquee banner,
// Grid/List subcollection tile) resolves the key through. The value is a
// SINGLE ASSET FILE path, so the resolver must expand `~` and `%collection%`
// WITHOUT requiring the path to exist — validateAndExpandPath's existence
// check is QDir::exists, which is true only for directories, and would empty
// every real image file (Kartend-80h8o).
#include <QDir>
#include <QString>
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

QTEST_MAIN(TestCollectionUtilsIcon)
#include "test_collectionutils_icon.moc"
