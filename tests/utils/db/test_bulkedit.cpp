// Tests for BulkEdit::applyAction. Pure logic over ItemMetadata, no DB.
#include <QObject>
#include <QStringList>
#include <QTest>

#include "bulkedit.h"
#include "itemmetadata.h"

using BulkEdit::Action;
using ItemMetadataStore::ItemMetadata;

class TestBulkEdit : public QObject {
  Q_OBJECT
private slots:
  void addTag_appendsAndDedupesCaseInsensitively();
  void removeTag_dropsCaseInsensitively();
  void markHidden_onlyChangesRowsThatWerentAlreadyHidden();
  void clearRating_skipsRowsAlreadyUnset();
  void changedFlagDrivesPersistencePath();
  void source_stampedOnChangedRowsOnly();
};

namespace {
ItemMetadata makeItem(const QString &path, const QString &tagsJson = QString()) {
  ItemMetadata md;
  md.collectionUuid = "u1";
  md.path = path;
  md.tags = tagsJson;
  return md;
}
} // namespace

void TestBulkEdit::addTag_appendsAndDedupesCaseInsensitively() {
  QList<ItemMetadata> items{
      makeItem("/a", ItemMetadataStore::serializeTags({"live"})),
      makeItem("/b", ItemMetadataStore::serializeTags({"LIVE"})), // dedupe → no change
      makeItem("/c"), // empty tags → new tag added fresh
  };
  const auto out = BulkEdit::applyAction(Action::AddTag, "Live", items);
  QCOMPARE(out.size(), 3);
  // "live" matches "Live" case-insensitively, so adding a second one is a
  // no-op — the row stays untouched.
  QVERIFY(!out[0].changed);
  QVERIFY(!out[1].changed); // "LIVE" exists, same case-insensitive match
  QVERIFY(out[2].changed);
  QCOMPARE(ItemMetadataStore::parseTags(out[2].metadata.tags), QStringList{"Live"});
}

void TestBulkEdit::removeTag_dropsCaseInsensitively() {
  QList<ItemMetadata> items{
      makeItem("/a", ItemMetadataStore::serializeTags({"Live", "Jazz"})),
      makeItem("/b", ItemMetadataStore::serializeTags({"rock"})), // doesn't have the tag
  };
  const auto out = BulkEdit::applyAction(Action::RemoveTag, "LIVE", items);
  QVERIFY(out[0].changed);
  QCOMPARE(ItemMetadataStore::parseTags(out[0].metadata.tags), QStringList{"Jazz"});
  QVERIFY(!out[1].changed);
}

void TestBulkEdit::markHidden_onlyChangesRowsThatWerentAlreadyHidden() {
  ItemMetadata alreadyHidden;
  alreadyHidden.isHidden = true;
  QList<ItemMetadata> items{makeItem("/a"), alreadyHidden, makeItem("/c")};
  const auto out = BulkEdit::applyAction(Action::MarkHidden, QString(), items);
  QVERIFY(out[0].changed);
  QVERIFY(out[0].metadata.isHidden);
  QVERIFY(!out[1].changed); // was already hidden
  QVERIFY(out[2].changed);
}

void TestBulkEdit::clearRating_skipsRowsAlreadyUnset() {
  ItemMetadata rated;
  rated.rating = 8;
  ItemMetadata unrated;
  unrated.rating = -1;
  QList<ItemMetadata> items{rated, unrated};
  const auto out = BulkEdit::applyAction(Action::ClearRating, QString(), items);
  QVERIFY(out[0].changed);
  QCOMPARE(out[0].metadata.rating, -1);
  QVERIFY(!out[1].changed);
}

void TestBulkEdit::changedFlagDrivesPersistencePath() {
  // The persistence flag is the only contract the dialog has with us —
  // changed = true means the row must be written back; false means skip.
  ItemMetadata alreadyHidden;
  alreadyHidden.isHidden = true;
  QList<ItemMetadata> items{makeItem("/a"), alreadyHidden};
  const auto out = BulkEdit::applyAction(Action::MarkHidden, QString(), items);
  int writes = 0;
  for (const auto &change : out) {
    if (change.changed) ++writes;
  }
  QCOMPARE(writes, 1);
}

void TestBulkEdit::source_stampedOnChangedRowsOnly() {
  // Changed rows get source="user" so future scraper integrations can
  // tell user-touched rows from scraper-populated ones. Unchanged rows
  // must keep their original source.
  ItemMetadata original;
  original.source = "screenscraper";
  ItemMetadata fresh;
  QList<ItemMetadata> items{original, fresh};
  const auto out = BulkEdit::applyAction(Action::MarkHidden, QString(), items);
  // original had isHidden=false → would be changed, so source flips
  QVERIFY(out[0].changed);
  QCOMPARE(out[0].metadata.source, QStringLiteral("user"));
  QVERIFY(out[1].changed);
  QCOMPARE(out[1].metadata.source, QStringLiteral("user"));

  // Now exercise the no-op path: clearRating on rows that are already
  // unset must NOT touch the source.
  QList<ItemMetadata> rated{original}; // source = "screenscraper", rating = -1 (unset)
  const auto out2 = BulkEdit::applyAction(Action::ClearRating, QString(), rated);
  QVERIFY(!out2[0].changed);
  QCOMPARE(out2[0].metadata.source, QStringLiteral("screenscraper"));
}

QTEST_MAIN(TestBulkEdit)
#include "test_bulkedit.moc"
