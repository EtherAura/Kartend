#include <QTest>

#include "queryhelpers.h"

class TestQueryHelpers : public QObject {
  Q_OBJECT

private slots:
  // buildFtsPrefixQuery
  void fts_emptyInput_returnsEmpty();
  void fts_whitespaceOnly_returnsEmpty();
  void fts_punctuationOnly_returnsEmpty();
  void fts_singleTerm_appendsStar();
  void fts_multipleTerms_joinedWithAnd();
  void fts_punctuationCollapsed();
  void fts_unicodeRetained();
  void fts_underscorePreserved();

  // displayNameForBase
  void display_underscoresReplaced();
  void display_collapsedWhitespace();
  void display_emptyInput_returnsEmpty();
  void display_noUnderscores_returnsTrimmed();

  // characterSortPriority
  void priority_emptyString_returns3();
  void priority_bracketed_returns0();
  void priority_paren_returns0();
  void priority_apostrophePrefixDigit_returns2();
  void priority_apostrophePrefixLetter_returns3();
  void priority_apostropheAlone_returns1();
  void priority_digit_returns2();
  void priority_letter_returns3();
  void priority_otherPunctuation_returns1();

  // orderByForSortMode
  void orderBy_nameAscending_bare_noLimit();
  void orderBy_nameDescending_bare();
  void orderBy_dateDescending_withSortPrefix_andLimit();
  void orderBy_collectionAscending_collectionUuidStaysUnprefixed();
  void orderBy_artworkFirst_fallsThroughToNameAsc();
  void orderBy_random_fallsThroughToNameAsc();
  void orderBy_appendLimitOffset_whenRequested();

  // placeholderList
  void placeholderList_zero_returnsEmpty();
  void placeholderList_negative_returnsEmpty();
  void placeholderList_one_returnsSingleQuestionMark();
  void placeholderList_three_returnsCommaSeparated();
};

void TestQueryHelpers::fts_emptyInput_returnsEmpty() {
  QCOMPARE(QueryHelpers::buildFtsPrefixQuery(QString()), QString());
}

void TestQueryHelpers::fts_whitespaceOnly_returnsEmpty() {
  QCOMPARE(QueryHelpers::buildFtsPrefixQuery(QStringLiteral("   \t  ")),
           QString());
}

void TestQueryHelpers::fts_punctuationOnly_returnsEmpty() {
  QCOMPARE(QueryHelpers::buildFtsPrefixQuery(QStringLiteral("!!!---@@@")),
           QString());
}

void TestQueryHelpers::fts_singleTerm_appendsStar() {
  QCOMPARE(QueryHelpers::buildFtsPrefixQuery(QStringLiteral("zelda")),
           QStringLiteral("zelda*"));
}

void TestQueryHelpers::fts_multipleTerms_joinedWithAnd() {
  QCOMPARE(
      QueryHelpers::buildFtsPrefixQuery(QStringLiteral("super mario world")),
      QStringLiteral("super* AND mario* AND world*"));
}

void TestQueryHelpers::fts_punctuationCollapsed() {
  // Apostrophes, hyphens, periods → spaces between tokens
  QCOMPARE(
      QueryHelpers::buildFtsPrefixQuery(QStringLiteral("super-mario.bros")),
      QStringLiteral("super* AND mario* AND bros*"));
}

void TestQueryHelpers::fts_unicodeRetained() {
  // Unicode letters (e.g. ñ, é) preserved
  QCOMPARE(QueryHelpers::buildFtsPrefixQuery(QStringLiteral("pokémon ñ")),
           QStringLiteral("pokémon* AND ñ*"));
}

void TestQueryHelpers::fts_underscorePreserved() {
  // Underscore is in the keep-set (treated as part of a token)
  QCOMPARE(QueryHelpers::buildFtsPrefixQuery(QStringLiteral("rom_hack v1")),
           QStringLiteral("rom_hack* AND v1*"));
}

void TestQueryHelpers::display_underscoresReplaced() {
  QCOMPARE(QueryHelpers::displayNameForBase(QStringLiteral("super_mario_bros")),
           QStringLiteral("super mario bros"));
}

void TestQueryHelpers::display_collapsedWhitespace() {
  // simplified() trims and collapses; underscores → spaces first.
  QCOMPARE(QueryHelpers::displayNameForBase(QStringLiteral("a__b___c")),
           QStringLiteral("a b c"));
}

void TestQueryHelpers::display_emptyInput_returnsEmpty() {
  QCOMPARE(QueryHelpers::displayNameForBase(QString()), QString());
}

void TestQueryHelpers::display_noUnderscores_returnsTrimmed() {
  QCOMPARE(QueryHelpers::displayNameForBase(QStringLiteral("  hello world  ")),
           QStringLiteral("hello world"));
}

void TestQueryHelpers::priority_emptyString_returns3() {
  QCOMPARE(QueryHelpers::characterSortPriority(QString()), 3);
}

void TestQueryHelpers::priority_bracketed_returns0() {
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("[BIOS]")), 0);
}

void TestQueryHelpers::priority_paren_returns0() {
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("(USA) game")),
           0);
}

void TestQueryHelpers::priority_apostrophePrefixDigit_returns2() {
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("'89")), 2);
}

void TestQueryHelpers::priority_apostrophePrefixLetter_returns3() {
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("'Allo")), 3);
}

void TestQueryHelpers::priority_apostropheAlone_returns1() {
  // Only an apostrophe with no follow-up alphanumeric → falls to general
  // punctuation bucket (1).
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("'")), 1);
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("'!")), 1);
}

void TestQueryHelpers::priority_digit_returns2() {
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("3D Game")), 2);
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("007")), 2);
}

void TestQueryHelpers::priority_letter_returns3() {
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("Zelda")), 3);
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("a")), 3);
}

void TestQueryHelpers::priority_otherPunctuation_returns1() {
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("-game")), 1);
  QCOMPARE(QueryHelpers::characterSortPriority(QStringLiteral("@home")), 1);
}

void TestQueryHelpers::orderBy_nameAscending_bare_noLimit() {
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::NameAscending, false, false),
           QStringLiteral("ORDER BY name COLLATE NOCASE"));
}

void TestQueryHelpers::orderBy_nameDescending_bare() {
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::NameDescending, false, false),
           QStringLiteral("ORDER BY name COLLATE NOCASE DESC"));
}

void TestQueryHelpers::orderBy_dateDescending_withSortPrefix_andLimit() {
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::DateDescending, true, true),
           QStringLiteral(
               "ORDER BY sort_last_modified DESC, sort_name COLLATE NOCASE LIMIT ? OFFSET ?"));
}

void TestQueryHelpers::orderBy_collectionAscending_collectionUuidStaysUnprefixed() {
  // collection_uuid is the GROUP BY key; never aliased even with sort prefix.
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::CollectionAscending, true, false),
           QStringLiteral("ORDER BY collection_uuid, sort_name COLLATE NOCASE"));
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::CollectionAscending, false, false),
           QStringLiteral("ORDER BY collection_uuid, name COLLATE NOCASE"));
}

void TestQueryHelpers::orderBy_artworkFirst_fallsThroughToNameAsc() {
  // ArtworkFirst/ArtworkLast/Random/NameAscending all share the same SQL
  // ORDER BY (alphabetical asc). The non-alphabetical behavior comes from
  // sorted_items_cache, not from SQL.
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::ArtworkFirst, false, false),
           QueryHelpers::orderByForSortMode(SortMode::NameAscending, false, false));
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::ArtworkLast, false, false),
           QueryHelpers::orderByForSortMode(SortMode::NameAscending, false, false));
}

void TestQueryHelpers::orderBy_random_fallsThroughToNameAsc() {
  QCOMPARE(QueryHelpers::orderByForSortMode(SortMode::Random, true, true),
           QueryHelpers::orderByForSortMode(SortMode::NameAscending, true, true));
}

void TestQueryHelpers::orderBy_appendLimitOffset_whenRequested() {
  const QString without = QueryHelpers::orderByForSortMode(SortMode::SizeAscending, true, false);
  const QString with = QueryHelpers::orderByForSortMode(SortMode::SizeAscending, true, true);
  QVERIFY(!without.endsWith(QStringLiteral("LIMIT ? OFFSET ?")));
  QVERIFY(with.endsWith(QStringLiteral(" LIMIT ? OFFSET ?")));
  QCOMPARE(with, without + QStringLiteral(" LIMIT ? OFFSET ?"));
}

void TestQueryHelpers::placeholderList_zero_returnsEmpty() {
  QCOMPARE(QueryHelpers::placeholderList(0), QString());
}

void TestQueryHelpers::placeholderList_negative_returnsEmpty() {
  QCOMPARE(QueryHelpers::placeholderList(-3), QString());
}

void TestQueryHelpers::placeholderList_one_returnsSingleQuestionMark() {
  QCOMPARE(QueryHelpers::placeholderList(1), QStringLiteral("?"));
}

void TestQueryHelpers::placeholderList_three_returnsCommaSeparated() {
  QCOMPARE(QueryHelpers::placeholderList(3), QStringLiteral("?, ?, ?"));
}

QTEST_APPLESS_MAIN(TestQueryHelpers)
#include "test_queryhelpers.moc"
