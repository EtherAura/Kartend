// Tests for DetailPagePayloadBuilder — pure helpers extracted from
// DetailPageManager so the payload-shaping transforms can be exercised
// without spinning up a DatabaseManager / DetailPageOverlay graph.

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "detailpagehelpers.h"
#include "itemartwork.h"

using DetailPagePayloadBuilder::buildArtworkOverrideMap;
using DetailPagePayloadBuilder::buildFileInfoFields;
using DetailPagePayloadBuilder::collectCustomArtworkTypes;
using DetailPagePayloadBuilder::pickDisplayTitle;

namespace {

auto makeRow(const QString &type, const QString &manualPath = QString()) -> ItemArtworkStore::ItemArtwork {
  ItemArtworkStore::ItemArtwork row;
  row.artworkType = type;
  row.manualPath = manualPath;
  return row;
}

} // namespace

class TestDetailPageHelpers : public QObject {
  Q_OBJECT
private slots:
  // pickDisplayTitle
  void pickDisplayTitle_emptyMetaFallsBackToSidebarName();
  void pickDisplayTitle_nonEmptyMetaWins();
  void pickDisplayTitle_whitespaceMetaIsKept();

  // buildArtworkOverrideMap
  void buildArtworkOverrideMap_emptyRowsYieldEmptyMap();
  void buildArtworkOverrideMap_keysByTypeWithManualPath();
  void buildArtworkOverrideMap_laterRowOverridesEarlier();

  // collectCustomArtworkTypes
  void collectCustomArtworkTypes_filtersOutStandardTypes();
  void collectCustomArtworkTypes_preservesInputOrder();
  void collectCustomArtworkTypes_emptyRowsYieldEmpty();

  // buildFileInfoFields
  void buildFileInfoFields_missingFileReturnsSentinel();
  void buildFileInfoFields_existingFilePopulatesSizeAndExtension();
  void buildFileInfoFields_extensionLowercased();
  void buildFileInfoFields_noExtensionYieldsEmptyExtensionString();
};

// --------------------------- pickDisplayTitle -------------------------------

void TestDetailPageHelpers::pickDisplayTitle_emptyMetaFallsBackToSidebarName() {
  QCOMPARE(pickDisplayTitle(QStringLiteral("sidebar.rom"), QString()),
           QStringLiteral("sidebar.rom"));
}

void TestDetailPageHelpers::pickDisplayTitle_nonEmptyMetaWins() {
  QCOMPARE(pickDisplayTitle(QStringLiteral("sidebar.rom"), QStringLiteral("Real Title")),
           QStringLiteral("Real Title"));
}

void TestDetailPageHelpers::pickDisplayTitle_whitespaceMetaIsKept() {
  // Intentional: helper does not trim. The sidebar / scraper own input
  // cleanup; the helper just prefers what's there.
  QCOMPARE(pickDisplayTitle(QStringLiteral("sidebar"), QStringLiteral("   ")),
           QStringLiteral("   "));
}

// --------------------------- buildArtworkOverrideMap ------------------------

void TestDetailPageHelpers::buildArtworkOverrideMap_emptyRowsYieldEmptyMap() {
  QCOMPARE(buildArtworkOverrideMap({}).size(), 0);
}

void TestDetailPageHelpers::buildArtworkOverrideMap_keysByTypeWithManualPath() {
  const auto map = buildArtworkOverrideMap({
      makeRow(QStringLiteral("box"), QStringLiteral("/art/box.png")),
      makeRow(QStringLiteral("logo"), QStringLiteral("/art/logo.png")),
  });
  QCOMPARE(map.size(), 2);
  QCOMPARE(map.value(QStringLiteral("box")), QStringLiteral("/art/box.png"));
  QCOMPARE(map.value(QStringLiteral("logo")), QStringLiteral("/art/logo.png"));
}

void TestDetailPageHelpers::buildArtworkOverrideMap_laterRowOverridesEarlier() {
  // Duplicates should not happen in practice (the DB key is (uuid, path, type))
  // but the helper is intentionally lenient: last-wins matches the QHash::insert
  // semantics the manager already relied on.
  const auto map = buildArtworkOverrideMap({
      makeRow(QStringLiteral("box"), QStringLiteral("/old/box.png")),
      makeRow(QStringLiteral("box"), QStringLiteral("/new/box.png")),
  });
  QCOMPARE(map.size(), 1);
  QCOMPARE(map.value(QStringLiteral("box")), QStringLiteral("/new/box.png"));
}

// --------------------------- collectCustomArtworkTypes ----------------------

void TestDetailPageHelpers::collectCustomArtworkTypes_filtersOutStandardTypes() {
  const auto customs = collectCustomArtworkTypes({
      makeRow(QStringLiteral("box")),
      makeRow(QStringLiteral("custom-cart")),
      makeRow(QStringLiteral("logo")),
      makeRow(QStringLiteral("custom-manual-scan")),
  });
  QCOMPARE(customs, (QStringList{QStringLiteral("custom-cart"),
                                  QStringLiteral("custom-manual-scan")}));
}

void TestDetailPageHelpers::collectCustomArtworkTypes_preservesInputOrder() {
  // The manager renders customs after the canonical standard set; the order
  // within the custom group is whatever the DB returned (insertion order).
  const auto customs = collectCustomArtworkTypes({
      makeRow(QStringLiteral("zebra")),
      makeRow(QStringLiteral("alpha")),
      makeRow(QStringLiteral("middle")),
  });
  QCOMPARE(customs, (QStringList{QStringLiteral("zebra"), QStringLiteral("alpha"),
                                  QStringLiteral("middle")}));
}

void TestDetailPageHelpers::collectCustomArtworkTypes_emptyRowsYieldEmpty() {
  QVERIFY(collectCustomArtworkTypes({}).isEmpty());
}

// --------------------------- buildFileInfoFields ----------------------------

void TestDetailPageHelpers::buildFileInfoFields_missingFileReturnsSentinel() {
  // -1 size + empty strings is the "unknown" path the overlay treats as
  // "don't render this row".
  const auto fields = buildFileInfoFields(QFileInfo(QStringLiteral("/no/such/file.xyz")));
  QCOMPARE(fields.fileSize, qint64{-1});
  QVERIFY(fields.fileModified.isEmpty());
  QVERIFY(fields.fileExtension.isEmpty());
}

void TestDetailPageHelpers::buildFileInfoFields_existingFilePopulatesSizeAndExtension() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("item.sfc"));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  QCOMPARE(f.write("hello"), qint64{5});
  f.close();

  const auto fields = buildFileInfoFields(QFileInfo(path));
  QCOMPARE(fields.fileSize, qint64{5});
  QCOMPARE(fields.fileExtension, QStringLiteral(".sfc"));
  QVERIFY(!fields.fileModified.isEmpty());
  // Format guarantee — DetailsPaneManager mirrors this format string, so a
  // shape change here should ripple through deliberately rather than slip
  // past unnoticed.
  QCOMPARE(fields.fileModified.size(), 16); // yyyy-MM-dd HH:mm
}

void TestDetailPageHelpers::buildFileInfoFields_extensionLowercased() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("item.NES"));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();

  const auto fields = buildFileInfoFields(QFileInfo(path));
  QCOMPARE(fields.fileExtension, QStringLiteral(".nes"));
}

void TestDetailPageHelpers::buildFileInfoFields_noExtensionYieldsEmptyExtensionString() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("README"));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();

  const auto fields = buildFileInfoFields(QFileInfo(path));
  QVERIFY(fields.fileExtension.isEmpty());
}

QTEST_APPLESS_MAIN(TestDetailPageHelpers)
#include "test_detailpagehelpers.moc"
