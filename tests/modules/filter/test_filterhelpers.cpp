#include <QHash>
#include <QList>
#include <QTest>

#include "filterhelpers.h"

class TestFilterHelpers : public QObject {
  Q_OBJECT

private slots:
  // mapVisualToActualIndex
  void map_unfiltered_returnsPassthrough();
  void map_unfiltered_negativeReturnsItself();
  void map_filteredInRange_returnsMapped();
  void map_filteredOutOfRange_returnsMinusOne();
  void map_filteredEmpty_anyIndexReturnsMinusOne();

  // subcollectionNameMatches
  void subMatch_caseInsensitiveSubstring();
  void subMatch_emptyName_emptyNeedle_isTrue();
  void subMatch_noMatch_returnsFalse();

  // displayNameForMediaEntry
  void display_showAll_lookupHit_returnsMappedName();
  void display_showAll_lookupMiss_returnsBasename();
  void display_showAll_nullMap_returnsBasename();
  void display_perCollection_emptyMediaDir_returnsBasename();
  void display_perCollection_nullFileNamesMap_returnsBasename();
  void display_perCollection_lookupHit_returnsMappedName();
  void display_perCollection_lookupMiss_returnsBasename();

  // Extra edge cases the Kartend-o53u survey called out specifically:
  // empty rawEntry, whitespace-only mediaDirectory trimming, and a
  // rawEntry that's already an absolute path under showAllSubcollectionItems.
  void display_emptyRawEntry_returnsEmptyString();
  void display_perCollection_whitespaceMediaDir_treatedAsEmpty();
  void display_showAll_absoluteRawEntry_lookupHit_returnsMappedName();
  void display_perCollection_pathSeparatorJoinIsRobust();
};

void TestFilterHelpers::map_unfiltered_returnsPassthrough() {
  QList<int> indices{2, 4, 7};
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(0, false, indices), 0);
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(99, false, indices), 99);
}

void TestFilterHelpers::map_unfiltered_negativeReturnsItself() {
  QList<int> indices;
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(-1, false, indices), -1);
}

void TestFilterHelpers::map_filteredInRange_returnsMapped() {
  QList<int> indices{2, 4, 7};
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(0, true, indices), 2);
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(1, true, indices), 4);
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(2, true, indices), 7);
}

void TestFilterHelpers::map_filteredOutOfRange_returnsMinusOne() {
  QList<int> indices{2, 4};
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(-1, true, indices), -1);
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(2, true, indices), -1);
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(99, true, indices), -1);
}

void TestFilterHelpers::map_filteredEmpty_anyIndexReturnsMinusOne() {
  QList<int> indices;
  QCOMPARE(FilterHelpers::mapVisualToActualIndex(0, true, indices), -1);
}

void TestFilterHelpers::subMatch_caseInsensitiveSubstring() {
  QVERIFY(FilterHelpers::subcollectionNameMatches(QStringLiteral("Super Mario"),
                                                  QStringLiteral("mario")));
  QVERIFY(FilterHelpers::subcollectionNameMatches(QStringLiteral("ZELDA"), QStringLiteral("zel")));
}

void TestFilterHelpers::subMatch_emptyName_emptyNeedle_isTrue() {
  // Qt's contains("") is true (empty string is a substring of any string)
  QVERIFY(FilterHelpers::subcollectionNameMatches(QString(), QString()));
}

void TestFilterHelpers::subMatch_noMatch_returnsFalse() {
  QVERIFY(
      !FilterHelpers::subcollectionNameMatches(QStringLiteral("Sonic"), QStringLiteral("mario")));
}

void TestFilterHelpers::display_showAll_lookupHit_returnsMappedName() {
  QHash<QString, QString> p2d{{QStringLiteral("rom1.zip"), QStringLiteral("Awesome Game")}};
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("rom1.zip"), true, QString(),
                                                   &p2d, nullptr),
           QStringLiteral("Awesome Game"));
}

void TestFilterHelpers::display_showAll_lookupMiss_returnsBasename() {
  QHash<QString, QString> p2d;
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("/path/to/cool_game.zip"), true,
                                                   QString(), &p2d, nullptr),
           QStringLiteral("cool_game"));
}

void TestFilterHelpers::display_showAll_nullMap_returnsBasename() {
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("game.iso"), true, QString(),
                                                   nullptr, nullptr),
           QStringLiteral("game"));
}

void TestFilterHelpers::display_perCollection_emptyMediaDir_returnsBasename() {
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("game.zip"), false,
                                                   QStringLiteral("   "), nullptr, nullptr),
           QStringLiteral("game"));
}

void TestFilterHelpers::display_perCollection_nullFileNamesMap_returnsBasename() {
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("game.zip"), false,
                                                   QStringLiteral("/roms"), nullptr, nullptr),
           QStringLiteral("game"));
}

void TestFilterHelpers::display_perCollection_lookupHit_returnsMappedName() {
  QHash<QString, QString> fileNames{
      {QStringLiteral("/roms/game.zip"), QStringLiteral("Mapped Title")}};
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("game.zip"), false,
                                                   QStringLiteral("/roms"), nullptr, &fileNames),
           QStringLiteral("Mapped Title"));
}

void TestFilterHelpers::display_perCollection_lookupMiss_returnsBasename() {
  QHash<QString, QString> fileNames;
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("missing.zip"), false,
                                                   QStringLiteral("/roms"), nullptr, &fileNames),
           QStringLiteral("missing"));
}

void TestFilterHelpers::display_emptyRawEntry_returnsEmptyString() {
  // QFileInfo("").completeBaseName() is an empty string; the helper
  // mustn't crash or invent a synthetic name. Used as a sanity guard
  // for stale entries in m_filePaths (shouldn't happen, but defence in
  // depth keeps the filter loop robust).
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QString(), true, QString(), nullptr, nullptr),
           QString());
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QString(), false, QStringLiteral("/roms"),
                                                   nullptr, nullptr),
           QString());
}

void TestFilterHelpers::display_perCollection_whitespaceMediaDir_treatedAsEmpty() {
  // Trimmed-empty mediaDirectory must take the empty-dir fast path, not
  // construct a bogus "/   /game.zip" absolute path.
  QHash<QString, QString> fileNames{
      {QStringLiteral("/   /game.zip"), QStringLiteral("Should Not Match")},
  };
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("game.zip"), false,
                                                   QStringLiteral("\t\n  "), nullptr, &fileNames),
           QStringLiteral("game"));
}

void TestFilterHelpers::display_showAll_absoluteRawEntry_lookupHit_returnsMappedName() {
  // In showAll mode the rawEntry IS the full path (the keys in the
  // file-path-to-display-name map are absolute). Confirm a hit there
  // beats the basename fallback.
  QHash<QString, QString> p2d{
      {QStringLiteral("/games/zelda/oot.z64"), QStringLiteral("Ocarina of Time")},
  };
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("/games/zelda/oot.z64"), true,
                                                   QString(), &p2d, nullptr),
           QStringLiteral("Ocarina of Time"));
}

void TestFilterHelpers::display_perCollection_pathSeparatorJoinIsRobust() {
  // mediaDirectory with a trailing slash, mediaDirectory without one, and
  // rawEntry with a leading slash should all resolve to the same fileNames
  // entry — QDir::absoluteFilePath normalises the join.
  QHash<QString, QString> fileNames{
      {QStringLiteral("/roms/sub/game.zip"), QStringLiteral("Canonical Title")},
  };
  // trailing slash on dir
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("sub/game.zip"), false,
                                                   QStringLiteral("/roms/"), nullptr, &fileNames),
           QStringLiteral("Canonical Title"));
  // no trailing slash on dir
  QCOMPARE(FilterHelpers::displayNameForMediaEntry(QStringLiteral("sub/game.zip"), false,
                                                   QStringLiteral("/roms"), nullptr, &fileNames),
           QStringLiteral("Canonical Title"));
}

QTEST_APPLESS_MAIN(TestFilterHelpers)
#include "test_filterhelpers.moc"
