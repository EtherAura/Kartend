// Coverage for the CollectionUtils uuid-resolution helpers added in
// Kartend audit D-07: the computeCollectionUuid(CollectionConfig&) convenience
// overload (which expands the media dir internally) and indexForUuid /
// findByUuid, which replace the hand-rolled "loop all, recompute each uuid,
// return the match" idiom that was copy-pasted across the core/dialog layers.
#include <QList>
#include <QString>
#include <QTest>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "pathutils.h"

namespace {
CollectionConfig makeCollection(const QString &name, const QString &mediaDir) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = mediaDir;
  return c;
}
} // namespace

class TestCollectionUtilsUuid : public QObject {
  Q_OBJECT
private slots:
  void configOverloadMatchesExpandedNamePair();
  void indexForUuidFindsMatchingCollection();
  void indexForUuidReturnsMinusOneForNoMatchEmptyAndEmptyList();
  void findByUuidReturnsPointerOrNull();
};

void TestCollectionUtilsUuid::configOverloadMatchesExpandedNamePair() {
  const CollectionConfig c = makeCollection(QStringLiteral("Games"), QStringLiteral("/srv/games"));
  const QString viaOverload = CollectionUtils::computeCollectionUuid(c);
  // The overload must produce exactly what the hand-rolled name + expanded-dir
  // pairing every call site used produces — otherwise migrated call sites would
  // resolve to a different row.
  const QString viaPair = CollectionUtils::computeCollectionUuid(
      c.name, PathUtils::validateAndExpandPath(c.mediaDirectory, c.name));
  QVERIFY(!viaOverload.isEmpty());
  QCOMPARE(viaOverload, viaPair);
}

void TestCollectionUtilsUuid::indexForUuidFindsMatchingCollection() {
  const QList<CollectionConfig> collections{
      makeCollection(QStringLiteral("A"), QStringLiteral("/m/a")),
      makeCollection(QStringLiteral("B"), QStringLiteral("/m/b")),
      makeCollection(QStringLiteral("C"), QStringLiteral("/m/c"))};
  QCOMPARE(CollectionUtils::indexForUuid(collections,
                                         CollectionUtils::computeCollectionUuid(collections[0])),
           0);
  QCOMPARE(CollectionUtils::indexForUuid(collections,
                                         CollectionUtils::computeCollectionUuid(collections[2])),
           2);
}

void TestCollectionUtilsUuid::indexForUuidReturnsMinusOneForNoMatchEmptyAndEmptyList() {
  const QList<CollectionConfig> collections{
      makeCollection(QStringLiteral("A"), QStringLiteral("/m/a"))};
  QCOMPARE(CollectionUtils::indexForUuid(collections, QStringLiteral("not-a-real-uuid")), -1);
  QCOMPARE(CollectionUtils::indexForUuid(collections, QString()), -1); // empty uuid is no-match
  QCOMPARE(
      CollectionUtils::indexForUuid({}, CollectionUtils::computeCollectionUuid(collections[0])),
      -1);
}

void TestCollectionUtilsUuid::findByUuidReturnsPointerOrNull() {
  const QList<CollectionConfig> collections{
      makeCollection(QStringLiteral("A"), QStringLiteral("/m/a")),
      makeCollection(QStringLiteral("B"), QStringLiteral("/m/b"))};
  const QString uuidB = CollectionUtils::computeCollectionUuid(collections[1]);
  const CollectionConfig *found = CollectionUtils::findByUuid(collections, uuidB);
  QVERIFY(found != nullptr);
  QCOMPARE(found->name, QStringLiteral("B"));
  QCOMPARE(found, &collections[1]); // points into the supplied list, not a copy
  QVERIFY(CollectionUtils::findByUuid(collections, QStringLiteral("nope")) == nullptr);
  QVERIFY(CollectionUtils::findByUuid(collections, QString()) == nullptr);
}

QTEST_APPLESS_MAIN(TestCollectionUtilsUuid)
#include "test_collectionutils_uuid.moc"
