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

QTEST_APPLESS_MAIN(TestQueryHelpers)
#include "test_queryhelpers.moc"
