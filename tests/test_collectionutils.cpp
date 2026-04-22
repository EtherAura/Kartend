/**
 * @file test_collectionutils.cpp
 * @brief Unit tests for CollectionUtils helpers and enum conversions
 */

#include "collectionutils.h"
#include <QTest>

class TestCollectionUtils : public QObject {
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

  // computeCollectionUuid
  void computeCollectionUuid_deterministic();
  void computeCollectionUuid_caseInsensitive();
  void computeCollectionUuid_trimmedWhitespace();
  void computeCollectionUuid_differentInputs();
  void computeCollectionUuid_emptyInputs();
  void computeCollectionUuid_isHex40();
};

// ─────────────────────────────────────────────────────────────────────────────
// alignment helpers
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::alignmentToString_left() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Left),
           QStringLiteral("left"));
}

void TestCollectionUtils::alignmentToString_center() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Center),
           QStringLiteral("center"));
}

void TestCollectionUtils::alignmentToString_right() {
  QCOMPARE(CollectionUtils::alignmentToString(HorizontalAlignment::Right),
           QStringLiteral("right"));
}

void TestCollectionUtils::stringToAlignment_left() {
  QCOMPARE(CollectionUtils::stringToAlignment("left"),
           HorizontalAlignment::Left);
}

void TestCollectionUtils::stringToAlignment_center() {
  QCOMPARE(CollectionUtils::stringToAlignment("center"),
           HorizontalAlignment::Center);
}

void TestCollectionUtils::stringToAlignment_right() {
  QCOMPARE(CollectionUtils::stringToAlignment("right"),
           HorizontalAlignment::Right);
}

void TestCollectionUtils::stringToAlignment_caseInsensitive() {
  QCOMPARE(CollectionUtils::stringToAlignment("LEFT"),
           HorizontalAlignment::Left);
  QCOMPARE(CollectionUtils::stringToAlignment("Right"),
           HorizontalAlignment::Right);
  QCOMPARE(CollectionUtils::stringToAlignment("CeNtEr"),
           HorizontalAlignment::Center);
}

void TestCollectionUtils::stringToAlignment_unknownDefaultsToCenter() {
  QCOMPARE(CollectionUtils::stringToAlignment(""),
           HorizontalAlignment::Center);
  QCOMPARE(CollectionUtils::stringToAlignment("nonsense"),
           HorizontalAlignment::Center);
}

void TestCollectionUtils::alignmentRoundTrip() {
  for (auto a : {HorizontalAlignment::Left, HorizontalAlignment::Center,
                 HorizontalAlignment::Right}) {
    QCOMPARE(CollectionUtils::stringToAlignment(
                 CollectionUtils::alignmentToString(a)),
             a);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// view type helpers
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::viewTypeToString_grid() {
  QCOMPARE(CollectionUtils::viewTypeToString(ViewType::Grid),
           QStringLiteral("grid"));
}

void TestCollectionUtils::viewTypeToString_list() {
  QCOMPARE(CollectionUtils::viewTypeToString(ViewType::List),
           QStringLiteral("list"));
}

void TestCollectionUtils::stringToViewType_grid() {
  QCOMPARE(CollectionUtils::stringToViewType("grid"), ViewType::Grid);
}

void TestCollectionUtils::stringToViewType_list() {
  QCOMPARE(CollectionUtils::stringToViewType("list"), ViewType::List);
}

void TestCollectionUtils::stringToViewType_caseInsensitive() {
  QCOMPARE(CollectionUtils::stringToViewType("LIST"), ViewType::List);
  QCOMPARE(CollectionUtils::stringToViewType("Grid"), ViewType::Grid);
}

void TestCollectionUtils::stringToViewType_unknownDefaultsToGrid() {
  QCOMPARE(CollectionUtils::stringToViewType(""), ViewType::Grid);
  QCOMPARE(CollectionUtils::stringToViewType("garbage"), ViewType::Grid);
}

void TestCollectionUtils::viewTypeRoundTrip() {
  for (auto v : {ViewType::Grid, ViewType::List}) {
    QCOMPARE(
        CollectionUtils::stringToViewType(CollectionUtils::viewTypeToString(v)),
        v);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// isValidIndex overloads
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::isValidIndex_validIndexInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  list.append(CollectionConfig{});
  QVERIFY(CollectionUtils::isValidIndex(0, list));
  QVERIFY(CollectionUtils::isValidIndex(1, list));
}

void TestCollectionUtils::isValidIndex_negativeIndexInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(-1, list));
  QVERIFY(!CollectionUtils::isValidIndex(-100, list));
}

void TestCollectionUtils::isValidIndex_outOfBoundsInRef() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(!CollectionUtils::isValidIndex(1, list));
  QVERIFY(!CollectionUtils::isValidIndex(1000, list));
}

void TestCollectionUtils::isValidIndex_emptyListInRef() {
  QList<CollectionConfig> list;
  QVERIFY(!CollectionUtils::isValidIndex(0, list));
  QVERIFY(!CollectionUtils::isValidIndex(-1, list));
}

void TestCollectionUtils::isValidIndex_validWithPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(CollectionUtils::isValidIndex(0, &list));
  QVERIFY(!CollectionUtils::isValidIndex(1, &list));
}

void TestCollectionUtils::isValidIndex_nullCollectionPointer() {
  QVERIFY(!CollectionUtils::isValidIndex(0,
                                         static_cast<QList<CollectionConfig> *>(
                                             nullptr)));
}

void TestCollectionUtils::isValidIndex_nullIndexPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  QVERIFY(
      !CollectionUtils::isValidIndex(static_cast<int *>(nullptr), &list));
}

void TestCollectionUtils::isValidIndex_validIndexPointer() {
  QList<CollectionConfig> list;
  list.append(CollectionConfig{});
  int idx = 0;
  QVERIFY(CollectionUtils::isValidIndex(&idx, &list));
  idx = 5;
  QVERIFY(!CollectionUtils::isValidIndex(&idx, &list));
}

// ─────────────────────────────────────────────────────────────────────────────
// computeCollectionUuid
// ─────────────────────────────────────────────────────────────────────────────

void TestCollectionUtils::computeCollectionUuid_deterministic() {
  QString a = CollectionUtils::computeCollectionUuid("Games", "/path/to/roms");
  QString b = CollectionUtils::computeCollectionUuid("Games", "/path/to/roms");
  QCOMPARE(a, b);
}

void TestCollectionUtils::computeCollectionUuid_caseInsensitive() {
  QString lower = CollectionUtils::computeCollectionUuid("games", "/x/y");
  QString upper = CollectionUtils::computeCollectionUuid("GAMES", "/X/Y");
  QString mixed = CollectionUtils::computeCollectionUuid("Games", "/X/y");
  QCOMPARE(lower, upper);
  QCOMPARE(lower, mixed);
}

void TestCollectionUtils::computeCollectionUuid_trimmedWhitespace() {
  QString clean = CollectionUtils::computeCollectionUuid("Games", "/x/y");
  QString padded =
      CollectionUtils::computeCollectionUuid("  Games  ", "/x/y  ");
  // Implementation trims the concatenated "name|mediaDir" string before
  // hashing, so trailing whitespace on the combined value is normalized.
  QString trailingPad =
      CollectionUtils::computeCollectionUuid("Games", "/x/y  ");
  QCOMPARE(clean, trailingPad);
  QVERIFY(!padded.isEmpty());
}

void TestCollectionUtils::computeCollectionUuid_differentInputs() {
  QString a = CollectionUtils::computeCollectionUuid("Games", "/path/a");
  QString b = CollectionUtils::computeCollectionUuid("Games", "/path/b");
  QString c = CollectionUtils::computeCollectionUuid("Movies", "/path/a");
  QVERIFY(a != b);
  QVERIFY(a != c);
  QVERIFY(b != c);
}

void TestCollectionUtils::computeCollectionUuid_emptyInputs() {
  QString empty = CollectionUtils::computeCollectionUuid("", "");
  QVERIFY(!empty.isEmpty());
  QCOMPARE(empty.size(), 40); // SHA1 hex
}

void TestCollectionUtils::computeCollectionUuid_isHex40() {
  QString uuid = CollectionUtils::computeCollectionUuid("Games", "/x/y");
  QCOMPARE(uuid.size(), 40);
  for (QChar c : uuid) {
    QVERIFY((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

QTEST_APPLESS_MAIN(TestCollectionUtils)
#include "test_collectionutils.moc"
