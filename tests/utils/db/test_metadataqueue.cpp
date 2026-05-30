// Tests for MetadataQueue::build / reevaluate. Pure logic over a
// caller-supplied metadata loader closure — no DB.
#include <QObject>
#include <QTest>

#include "itemmetadata.h"
#include "metadataqueue.h"

using ItemMetadataStore::ItemMetadata;
using MetadataQueue::Entry;
using MetadataQueue::InputRow;

class TestMetadataQueue : public QObject {
  Q_OBJECT
private slots:
  void itemWithFullMetadataIsExcluded();
  void itemMissingTitle_isQueuedWithTitleField();
  void itemMissingArtworkOnDisk_isQueuedRegardlessOfMetadata();
  void multipleMissingFields_listedInStableOrder();
  void reevaluate_removesEntryWhenAllFieldsFilledIn();
  void reevaluate_keepsEntryIfArtworkStillMissing();
  void orderPreservedFromInput();
};

namespace {
ItemMetadata fullMetadata() {
  ItemMetadata md;
  md.title = "Concert";
  md.description = "A live recording.";
  md.genre = "Jazz";
  return md;
}
} // namespace

void TestMetadataQueue::itemWithFullMetadataIsExcluded() {
  QList<InputRow> inputs{InputRow{"/a/full.mp4", "Full Item", true}};
  const auto out = MetadataQueue::build(
      "u1", inputs, [](const QString &, const QString &) { return fullMetadata(); });
  QVERIFY(out.isEmpty());
}

void TestMetadataQueue::itemMissingTitle_isQueuedWithTitleField() {
  QList<InputRow> inputs{InputRow{"/a/notitle.mp4", "No Title", true}};
  const auto out = MetadataQueue::build("u1", inputs, [](const QString &, const QString &) {
    ItemMetadata md;
    md.description = "filled";
    md.genre = "filled";
    // title missing
    return md;
  });
  QCOMPARE(out.size(), 1);
  QVERIFY(out.first().missingFields.contains("Title"));
  QVERIFY(!out.first().missingFields.contains("Description"));
}

void TestMetadataQueue::itemMissingArtworkOnDisk_isQueuedRegardlessOfMetadata() {
  // Artwork is sourced from items.artwork_path (the scanner-resolved
  // file), not item_metadata — so even a fully-populated metadata row
  // gets queued if no artwork file matched.
  QList<InputRow> inputs{InputRow{"/a/coverless.mp4", "No Cover", /*hasArtwork=*/false}};
  const auto out = MetadataQueue::build(
      "u1", inputs, [](const QString &, const QString &) { return fullMetadata(); });
  QCOMPARE(out.size(), 1);
  QVERIFY(out.first().missingFields.contains("Artwork"));
}

void TestMetadataQueue::multipleMissingFields_listedInStableOrder() {
  QList<InputRow> inputs{InputRow{"/a/empty.mp4", "Empty Item", false}};
  const auto out = MetadataQueue::build(
      "u1", inputs, [](const QString &, const QString &) { return ItemMetadata{}; });
  QCOMPARE(out.size(), 1);
  // Order is deterministic: Title, Description, Genre, then Artwork.
  // Tests stability so the UI's bulleted list reads predictably.
  QCOMPARE(out.first().missingFields, (QStringList{"Title", "Description", "Genre", "Artwork"}));
}

void TestMetadataQueue::reevaluate_removesEntryWhenAllFieldsFilledIn() {
  Entry e;
  e.hasArtworkOnDisk = true;
  e.missingFields = {"Title"};
  const bool stillMissing = MetadataQueue::reevaluate(e, fullMetadata());
  QVERIFY(!stillMissing);
  QVERIFY(e.missingFields.isEmpty());
}

void TestMetadataQueue::reevaluate_keepsEntryIfArtworkStillMissing() {
  Entry e;
  e.hasArtworkOnDisk = false; // no artwork match at scan time
  e.missingFields = {"Title", "Artwork"};
  // Metadata is now fully populated, but artwork is a scanner-level
  // attribute. The entry must stay queued until the next rescan
  // refreshes hasArtworkOnDisk.
  const bool stillMissing = MetadataQueue::reevaluate(e, fullMetadata());
  QVERIFY(stillMissing);
  QCOMPARE(e.missingFields, QStringList{"Artwork"});
}

void TestMetadataQueue::orderPreservedFromInput() {
  // Caller usually supplies items in name-sort order so the queue
  // matches the grid. build() must NOT shuffle.
  QList<InputRow> inputs{InputRow{"/a/a.mp4", "Alpha", false}, InputRow{"/a/b.mp4", "Beta", false},
                         InputRow{"/a/c.mp4", "Gamma", false}};
  const auto out = MetadataQueue::build(
      "u1", inputs, [](const QString &, const QString &) { return ItemMetadata{}; });
  QCOMPARE(out.size(), 3);
  QCOMPARE(out[0].itemName, QStringLiteral("Alpha"));
  QCOMPARE(out[1].itemName, QStringLiteral("Beta"));
  QCOMPARE(out[2].itemName, QStringLiteral("Gamma"));
}

QTEST_MAIN(TestMetadataQueue)
#include "test_metadataqueue.moc"
