#include <QStringList>
#include <QtTest/QtTest>

#include "variantgrouping.h"

class TestVariantGrouping : public QObject {
  Q_OBJECT
private slots:
  void emptyInput_returnsEmpty();
  void singleItem_returnsEmpty();
  void uniqueBaseNames_returnEmpty();
  void sameBasenameDifferentExtensions_isOneGroup();
  void caseInsensitiveBasenameComparison();
  void multipleGroupsAreSorted();
  void emptyPathsAreSkipped();
  void preservesInputOrderWithinGroup();
};

void TestVariantGrouping::emptyInput_returnsEmpty() {
  QVERIFY(VariantGrouping::groupByBaseName({}).isEmpty());
}

void TestVariantGrouping::singleItem_returnsEmpty() {
  // Single file with no sibling — not a variant of anything, dropped.
  QVERIFY(VariantGrouping::groupByBaseName({"/lib/Concert.mkv"}).isEmpty());
}

void TestVariantGrouping::uniqueBaseNames_returnEmpty() {
  const QStringList input = {"/lib/A.mkv", "/lib/B.mkv", "/lib/C.mkv"};
  QVERIFY(VariantGrouping::groupByBaseName(input).isEmpty());
}

void TestVariantGrouping::sameBasenameDifferentExtensions_isOneGroup() {
  const QStringList input = {"/lib/Concert.mkv", "/lib/Concert.mp4", "/lib/Concert.flac"};
  const auto groups = VariantGrouping::groupByBaseName(input);
  QCOMPARE(groups.size(), 1);
  QCOMPARE(groups.first().baseName, QStringLiteral("Concert"));
  QCOMPARE(groups.first().paths.size(), 3);
}

void TestVariantGrouping::caseInsensitiveBasenameComparison() {
  const QStringList input = {"/lib/Concert.MKV", "/lib/concert.mp4"};
  const auto groups = VariantGrouping::groupByBaseName(input);
  QCOMPARE(groups.size(), 1);
  QCOMPARE(groups.first().paths.size(), 2);
}

void TestVariantGrouping::multipleGroupsAreSorted() {
  const QStringList input = {"/lib/Zeta.mkv",   "/lib/Zeta.mp4", "/lib/Alpha.mp3",
                             "/lib/Alpha.flac", "/lib/Mid.ogg",  "/lib/Mid.mp3"};
  const auto groups = VariantGrouping::groupByBaseName(input);
  QCOMPARE(groups.size(), 3);
  QCOMPARE(groups.at(0).baseName, QStringLiteral("Alpha"));
  QCOMPARE(groups.at(1).baseName, QStringLiteral("Mid"));
  QCOMPARE(groups.at(2).baseName, QStringLiteral("Zeta"));
}

void TestVariantGrouping::emptyPathsAreSkipped() {
  const QStringList input = {"", "   ", "/lib/X.mkv", "/lib/X.mp4"};
  const auto groups = VariantGrouping::groupByBaseName(input);
  QCOMPARE(groups.size(), 1);
  QCOMPARE(groups.first().paths.size(), 2);
}

void TestVariantGrouping::preservesInputOrderWithinGroup() {
  const QStringList input = {"/lib/X.mp4", "/lib/X.flac", "/lib/X.mkv"};
  const auto groups = VariantGrouping::groupByBaseName(input);
  QCOMPARE(groups.size(), 1);
  QCOMPARE(groups.first().paths, input);
}

QTEST_APPLESS_MAIN(TestVariantGrouping)
#include "test_variantgrouping.moc"
