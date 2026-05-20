/**
 * @file test_collectionutils_alignment.cpp
 * @brief Unit tests for CollectionUtils helpers that don't traverse the
 *        collection hierarchy — enum conversions, index validation, type
 *        categorization, UUID derivation, placeholder-artwork inheritance,
 *        and CollectionContext::isValid.
 *
 * Extracted from test_collectionutils.cpp as a cohesion split. The
 * tree-walking slots live in test_collectionutils_ancestry.cpp; the
 * wouldCreateCircularReference slots live in test_collectionutils_cycles.cpp.
 */

#include "collectionutils.h"
#include <QTest>

class TestCollectionUtilsAlignment : public QObject {
  Q_OBJECT

private slots:
  // alignmentToString / stringToAlignment
  void alignmentToString_left();
  void alignmentToString_center();
  void alignmentToString_right();
  void stringToAlignment_left();
  void stringToAlignment_center();
  void stringToAlignment_right();
  void stringToAlignment_caseInsensitive();
  void stringToAlignment_unknownDefaultsToCenter();
  void alignmentRoundTrip();

  // viewTypeToString / stringToViewType
  void viewTypeToString_grid();
  void viewTypeToString_list();
  void stringToViewType_grid();
  void stringToViewType_list();
  void stringToViewType_caseInsensitive();
  void stringToViewType_unknownDefaultsToGrid();
  void viewTypeRoundTrip();

  // isValidIndex overloads
  void isValidIndex_validIndexInRef();
  void isValidIndex_negativeIndexInRef();
  void isValidIndex_outOfBoundsInRef();
  void isValidIndex_emptyListInRef();
  void isValidIndex_validWithPointer();
  void isValidIndex_nullCollectionPointer();
  void isValidIndex_nullIndexPointer();
  void isValidIndex_validIndexPointer();

  // isInteractiveViewIndex (valid collection OR synthetic home view, index -1)
  void isInteractiveViewIndex_validIndex();
  void isInteractiveViewIndex_rootView();
  void isInteractiveViewIndex_otherNegativeRejected();
  void isInteractiveViewIndex_outOfBoundsRejected();
  void isInteractiveViewIndex_nullPointers();

  // computeCollectionUuid
  void computeCollectionUuid_deterministic();
  void computeCollectionUuid_caseInsensitive();
  void computeCollectionUuid_trimmedWhitespace();
  void computeCollectionUuid_differentInputs();
  void computeCollectionUuid_emptyInputs();
  void computeCollectionUuid_isHex40();

  // placeholder artwork inheritance
  void resolvePlaceholderArtwork_usesOwnValue();
  void resolvePlaceholderArtwork_inheritsFromParent();
  void resolvePlaceholderArtwork_returnsEmptyWhenUnset();

  // collection categorization
  void effectiveCollectionType_returnsOwnTypeWhenSet();
  void effectiveCollectionType_inheritsFromParentWhenEmpty();
  void effectiveCollectionType_walksMultipleLevels();
  void effectiveCollectionType_emptyWhenNothingTagged();
  void effectiveCollectionType_invalidIndexReturnsEmpty();
  void effectiveCollectionType_trimsWhitespace();
  void collectAllCollectionTypes_returnsSortedUnion();
  void collectAllCollectionTypes_dedupesCaseInsensitive();
  void collectAllCollectionTypes_skipsEmpty();
  void standardCollectionTypes_areTheCuratedPresets();
  void collectionTypeChoices_blankThenPresetsThenInUse();
  void collectionTypeChoices_dropsInUseTypesThatDuplicatePresets();

  // CollectionContext::isValid + synthetic root-view mode
  void context_isValid_defaultIsInvalid();
  void context_isValid_realCollectionIsValid();
  void context_isValid_rootViewWithNoIndexIsValid();
};

namespace {
// Minimal CollectionConfig builder shared across the categorization /
// placeholder fixtures below.
CollectionConfig makeCollection(const QString &name, bool isSub, int parentIdx) {
  CollectionConfig c;
  c.name = name;
  c.isSubcollection = isSub;
  c.parentCollectionIndex = parentIdx;
  return c;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// alignment helpers
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::alignmentToString_left() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Left), QStringLiteral("left"));
}

void TestCollectionUtilsAlignment::alignmentToString_center() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Center),
           QStringLiteral("center"));
}

void TestCollectionUtilsAlignment::alignmentToString_right() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Right), QStringLiteral("right"));
}

void TestCollectionUtilsAlignment::stringToAlignment_left() {
  QCOMPARE(CollectionUtils::stringToAlignment("left"), HorizontalAlignment::Left);
}

void TestCollectionUtilsAlignment::stringToAlignment_center() {
  QCOMPARE(CollectionUtils::stringToAlignment("center"), HorizontalAlignment::Center);
}

void TestCollectionUtilsAlignment::stringToAlignment_right() {
  QCOMPARE(CollectionUtils::stringToAlignment("right"), HorizontalAlignment::Right);
}

void TestCollectionUtilsAlignment::stringToAlignment_caseInsensitive() {
  QCOMPARE(CollectionUtils::stringToAlignment("LEFT"), HorizontalAlignment::Left);
  QCOMPARE(CollectionUtils::stringToAlignment("Right"), HorizontalAlignment::Right);
  QCOMPARE(CollectionUtils::stringToAlignment("CeNtEr"), HorizontalAlignment::Center);
}

void TestCollectionUtilsAlignment::stringToAlignment_unknownDefaultsToCenter() {
  QCOMPARE(CollectionUtils::stringToAlignment(""), HorizontalAlignment::Center);
  QCOMPARE(CollectionUtils::stringToAlignment("nonsense"), HorizontalAlignment::Center);
}

void TestCollectionUtilsAlignment::alignmentRoundTrip() {
  for (auto a :
       {HorizontalAlignment::Left, HorizontalAlignment::Center, HorizontalAlignment::Right}) {
    QCOMPARE(CollectionUtils::stringToAlignment(CollectionUtils::alignmentToString(a)), a);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// view type helpers
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::viewTypeToString_grid() {
  QCOMPARE(CollectionUtils::viewTypeToString(ViewType::Grid), QStringLiteral("grid"));
}

void TestCollectionUtilsAlignment::viewTypeToString_list() {
  QCOMPARE(CollectionUtils::viewTypeToString(ViewType::List), QStringLiteral("list"));
}

void TestCollectionUtilsAlignment::stringToViewType_grid() {
  QCOMPARE(CollectionUtils::stringToViewType("grid"), ViewType::Grid);
}

void TestCollectionUtilsAlignment::stringToViewType_list() {
  QCOMPARE(CollectionUtils::stringToViewType("list"), ViewType::List);
}

void TestCollectionUtilsAlignment::stringToViewType_caseInsensitive() {
  QCOMPARE(CollectionUtils::stringToViewType("LIST"), ViewType::List);
  QCOMPARE(CollectionUtils::stringToViewType("Grid"), ViewType::Grid);
}

void TestCollectionUtilsAlignment::stringToViewType_unknownDefaultsToGrid() {
  QCOMPARE(CollectionUtils::stringToViewType(""), ViewType::Grid);
  QCOMPARE(CollectionUtils::stringToViewType("garbage"), ViewType::Grid);
}

void TestCollectionUtilsAlignment::viewTypeRoundTrip() {
  for (auto v : {ViewType::Grid, ViewType::List}) {
    QCOMPARE(CollectionUtils::stringToViewType(CollectionUtils::viewTypeToString(v)), v);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// isValidIndex overloads
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::isValidIndex_validIndexInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  list.append(CollectionConfig{});
  QVERIFY(CollectionUtils::isValidIndex(0, list));
  QVERIFY(CollectionUtils::isValidIndex(1, list));
}

void TestCollectionUtilsAlignment::isValidIndex_negativeIndexInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(-1, list));
  QVERIFY(!CollectionUtils::isValidIndex(-100, list));
}

void TestCollectionUtilsAlignment::isValidIndex_outOfBoundsInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(1, list));
  QVERIFY(!CollectionUtils::isValidIndex(1000, list));
}

void TestCollectionUtilsAlignment::isValidIndex_emptyListInRef() {
  QList<CollectionConfig> list;
  QVERIFY(!CollectionUtils::isValidIndex(0, list));
  QVERIFY(!CollectionUtils::isValidIndex(-1, list));
}

void TestCollectionUtilsAlignment::isValidIndex_validWithPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(CollectionUtils::isValidIndex(0, &list));
  QVERIFY(!CollectionUtils::isValidIndex(1, &list));
}

void TestCollectionUtilsAlignment::isValidIndex_nullCollectionPointer() {
  QVERIFY(!CollectionUtils::isValidIndex(0, static_cast<QList<CollectionConfig> *>(nullptr)));
}

void TestCollectionUtilsAlignment::isValidIndex_nullIndexPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(static_cast<int *>(nullptr), &list));
}

void TestCollectionUtilsAlignment::isValidIndex_validIndexPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = 0;
  QVERIFY(CollectionUtils::isValidIndex(&idx, &list));
  idx = 1; // out of bounds for a 1-element list
  QVERIFY(!CollectionUtils::isValidIndex(&idx, &list));
}

// ─────────────────────────────────────────────────────────────────────────────
// isInteractiveViewIndex
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::isInteractiveViewIndex_validIndex() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = 0;
  QVERIFY(CollectionUtils::isInteractiveViewIndex(&idx, &list));
}

void TestCollectionUtilsAlignment::isInteractiveViewIndex_rootView() {
  // Index -1 is the synthetic home/root view — interactive even though it is
  // not a valid collection index, and even when no collections exist.
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = -1;
  QVERIFY(CollectionUtils::isInteractiveViewIndex(&idx, &list));
  QList<CollectionConfig> empty;
  QVERIFY(CollectionUtils::isInteractiveViewIndex(&idx, &empty));
}

void TestCollectionUtilsAlignment::isInteractiveViewIndex_otherNegativeRejected() {
  // Only -1 is the home view; any other negative is rejected.
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = -2;
  QVERIFY(!CollectionUtils::isInteractiveViewIndex(&idx, &list));
}

void TestCollectionUtilsAlignment::isInteractiveViewIndex_outOfBoundsRejected() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = 5;
  QVERIFY(!CollectionUtils::isInteractiveViewIndex(&idx, &list));
}

void TestCollectionUtilsAlignment::isInteractiveViewIndex_nullPointers() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = 0;
  QVERIFY(!CollectionUtils::isInteractiveViewIndex(static_cast<int *>(nullptr), &list));
  QVERIFY(!CollectionUtils::isInteractiveViewIndex(&idx, nullptr));
}

// ─────────────────────────────────────────────────────────────────────────────
// computeCollectionUuid
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::computeCollectionUuid_deterministic() {
  QString a = CollectionUtils::computeCollectionUuid("Video", "/path/to/media");
  QString b = CollectionUtils::computeCollectionUuid("Video", "/path/to/media");
  QCOMPARE(a, b);
}

void TestCollectionUtilsAlignment::computeCollectionUuid_caseInsensitive() {
  QString lower = CollectionUtils::computeCollectionUuid("video", "/x/y");
  QString upper = CollectionUtils::computeCollectionUuid("VIDEO", "/X/Y");
  QString mixed = CollectionUtils::computeCollectionUuid("Video", "/X/y");
  QCOMPARE(lower, upper);
  QCOMPARE(lower, mixed);
}

void TestCollectionUtilsAlignment::computeCollectionUuid_trimmedWhitespace() {
  QString clean = CollectionUtils::computeCollectionUuid("Video", "/x/y");
  QString padded = CollectionUtils::computeCollectionUuid("  Video  ", "/x/y  ");
  // Implementation trims the concatenated "name|mediaDir" string before
  // hashing, so trailing whitespace on the combined value is normalized.
  QString trailingPad = CollectionUtils::computeCollectionUuid("Video", "/x/y  ");
  QCOMPARE(clean, trailingPad);
  QVERIFY(!padded.isEmpty());
}

void TestCollectionUtilsAlignment::computeCollectionUuid_differentInputs() {
  QString a = CollectionUtils::computeCollectionUuid("Video", "/path/a");
  QString b = CollectionUtils::computeCollectionUuid("Video", "/path/b");
  QString c = CollectionUtils::computeCollectionUuid("Audio", "/path/a");
  QVERIFY(a != b);
  QVERIFY(a != c);
  QVERIFY(b != c);
}

void TestCollectionUtilsAlignment::computeCollectionUuid_emptyInputs() {
  QString empty = CollectionUtils::computeCollectionUuid("", "");
  QVERIFY(!empty.isEmpty());
  QCOMPARE(empty.size(), 40); // SHA1 hex
}

void TestCollectionUtilsAlignment::computeCollectionUuid_isHex40() {
  QString uuid = CollectionUtils::computeCollectionUuid("Video", "/x/y");
  QCOMPARE(uuid.size(), 40);
  for (QChar c : uuid) {
    QVERIFY((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// resolvePlaceholderArtwork
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::resolvePlaceholderArtwork_usesOwnValue() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.placeholderArtwork = QStringLiteral("/art/root.png");
  CollectionConfig child = makeCollection("Child", /*isSub=*/true, 0);
  child.placeholderArtwork = QStringLiteral("/art/child.png");
  cs << root << child;

  QCOMPARE(CollectionUtils::resolvePlaceholderArtwork(1, cs), QStringLiteral("/art/child.png"));
}

void TestCollectionUtilsAlignment::resolvePlaceholderArtwork_inheritsFromParent() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.placeholderArtwork = QStringLiteral("/art/root.png");
  CollectionConfig child = makeCollection("Child", /*isSub=*/true, 0);
  cs << root << child;

  QCOMPARE(CollectionUtils::resolvePlaceholderArtwork(1, cs), QStringLiteral("/art/root.png"));
}

void TestCollectionUtilsAlignment::resolvePlaceholderArtwork_returnsEmptyWhenUnset() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Child", /*isSub=*/true, 0);

  QVERIFY(CollectionUtils::resolvePlaceholderArtwork(1, cs).isEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Collection categorization
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::effectiveCollectionType_returnsOwnTypeWhenSet() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("Video");
  cs << root;
  QCOMPARE(CollectionUtils::effectiveCollectionType(0, cs), QStringLiteral("Video"));
}

void TestCollectionUtilsAlignment::effectiveCollectionType_inheritsFromParentWhenEmpty() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("Video");
  CollectionConfig child = makeCollection("Child", /*isSub=*/true, 0);
  // Child has no type; should inherit "Video" from root.
  cs << root << child;
  QCOMPARE(CollectionUtils::effectiveCollectionType(1, cs), QStringLiteral("Video"));
}

void TestCollectionUtilsAlignment::effectiveCollectionType_walksMultipleLevels() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("Audio");
  CollectionConfig mid = makeCollection("Mid", /*isSub=*/true, 0);
  CollectionConfig leaf = makeCollection("Leaf", /*isSub=*/true, 1);
  cs << root << mid << leaf;
  QCOMPARE(CollectionUtils::effectiveCollectionType(2, cs), QStringLiteral("Audio"));
}

void TestCollectionUtilsAlignment::effectiveCollectionType_emptyWhenNothingTagged() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  cs << makeCollection("Child", /*isSub=*/true, 0);
  QVERIFY(CollectionUtils::effectiveCollectionType(1, cs).isEmpty());
}

void TestCollectionUtilsAlignment::effectiveCollectionType_invalidIndexReturnsEmpty() {
  QList<CollectionConfig> cs;
  cs << makeCollection("Root", /*isSub=*/false, -1);
  QVERIFY(CollectionUtils::effectiveCollectionType(-1, cs).isEmpty());
  QVERIFY(CollectionUtils::effectiveCollectionType(99, cs).isEmpty());
}

void TestCollectionUtilsAlignment::effectiveCollectionType_trimsWhitespace() {
  QList<CollectionConfig> cs;
  CollectionConfig root = makeCollection("Root", /*isSub=*/false, -1);
  root.type = QStringLiteral("  Video  ");
  cs << root;
  // Whitespace gets trimmed so the toolbar dropdown doesn't show ghost
  // entries that look identical to a clean tag.
  QCOMPARE(CollectionUtils::effectiveCollectionType(0, cs), QStringLiteral("Video"));
}

void TestCollectionUtilsAlignment::collectAllCollectionTypes_returnsSortedUnion() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Audio");
  CollectionConfig b = makeCollection("B", /*isSub=*/false, -1);
  b.type = QStringLiteral("Video");
  CollectionConfig c = makeCollection("C", /*isSub=*/false, -1);
  c.type = QStringLiteral("Reference");
  cs << a << b << c;
  const QStringList result = CollectionUtils::collectAllCollectionTypes(cs);
  QCOMPARE(result, (QStringList{QStringLiteral("Audio"), QStringLiteral("Reference"),
                                QStringLiteral("Video")}));
}

void TestCollectionUtilsAlignment::collectAllCollectionTypes_dedupesCaseInsensitive() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Video");
  CollectionConfig b = makeCollection("B", /*isSub=*/false, -1);
  b.type = QStringLiteral("video"); // duplicate by case
  CollectionConfig c = makeCollection("C", /*isSub=*/true, 0);
  c.type = QStringLiteral("Audio");
  cs << a << b << c;
  const QStringList result = CollectionUtils::collectAllCollectionTypes(cs);
  // First occurrence wins for casing, so "Video" survives, "video" dropped.
  QCOMPARE(result.size(), 2);
  QVERIFY(result.contains(QStringLiteral("Video")));
  QVERIFY(result.contains(QStringLiteral("Audio")));
  QVERIFY(!result.contains(QStringLiteral("video")));
}

void TestCollectionUtilsAlignment::collectAllCollectionTypes_skipsEmpty() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Video");
  CollectionConfig b = makeCollection("B", /*isSub=*/false, -1);
  // Empty type — should be skipped entirely.
  CollectionConfig c = makeCollection("C", /*isSub=*/false, -1);
  c.type = QStringLiteral("   "); // whitespace-only also skipped
  cs << a << b << c;
  const QStringList result = CollectionUtils::collectAllCollectionTypes(cs);
  QCOMPARE(result, QStringList{QStringLiteral("Video")});
}

void TestCollectionUtilsAlignment::standardCollectionTypes_areTheCuratedPresets() {
  const QStringList presets = CollectionUtils::standardCollectionTypes();
  QCOMPARE(presets,
           (QStringList{QStringLiteral("Video"), QStringLiteral("Audio"), QStringLiteral("Images"),
                        QStringLiteral("Documents"), QStringLiteral("Games")}));
}

void TestCollectionUtilsAlignment::collectionTypeChoices_blankThenPresetsThenInUse() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("Podcasts"); // a custom type not in the presets
  cs << a;
  const QStringList choices = CollectionUtils::collectionTypeChoices(cs);
  // Leading blank entry (untagged), then the presets in display order.
  QVERIFY(!choices.isEmpty());
  QVERIFY(choices.first().isEmpty());
  QCOMPARE(choices.mid(1, 5), CollectionUtils::standardCollectionTypes());
  // The custom in-use type is appended after the presets.
  QCOMPARE(choices.last(), QStringLiteral("Podcasts"));
}

void TestCollectionUtilsAlignment::collectionTypeChoices_dropsInUseTypesThatDuplicatePresets() {
  QList<CollectionConfig> cs;
  CollectionConfig a = makeCollection("A", /*isSub=*/false, -1);
  a.type = QStringLiteral("video"); // case-insensitively duplicates the "Video" preset
  cs << a;
  const QStringList choices = CollectionUtils::collectionTypeChoices(cs);
  // blank + 5 presets, with no extra entry for the duplicate.
  QCOMPARE(choices.size(), 6);
  QVERIFY(!choices.contains(QStringLiteral("video")));
}

// ─────────────────────────────────────────────────────────────────────────────
// CollectionContext::isValid + synthetic root-view mode
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtilsAlignment::context_isValid_defaultIsInvalid() {
  CollectionContext ctx;
  QVERIFY(!ctx.isValid());
}

void TestCollectionUtilsAlignment::context_isValid_realCollectionIsValid() {
  CollectionContext ctx;
  ctx.currentIndex = 0;
  QVERIFY(ctx.isValid());
}

void TestCollectionUtilsAlignment::context_isValid_rootViewWithNoIndexIsValid() {
  CollectionContext ctx;
  ctx.currentIndex = -1;
  ctx.isRootView = true;
  QVERIFY(ctx.isValid());
}

QTEST_APPLESS_MAIN(TestCollectionUtilsAlignment)
#include "test_collectionutils_alignment.moc"
