// Tests for SearchQueryParser. Pure string -> struct, no DB or Qt event loop.
#include <QObject>
#include <QStringList>
#include <QTest>

#include "searchqueryparser.h"

using SearchQueryParser::SearchQuery;
using SearchQueryParser::TriState;

class TestSearchQueryParser : public QObject {
  Q_OBJECT
private slots:
  void emptyInput_returnsEmptyQuery();
  void plainText_keepsFreeText();
  void played_truePopulatesTriState();
  void played_falsePopulatesTriState();
  void played_invalidValueIsConsumedAsUnknown();
  void favorite_supportsBritishSpelling();
  void missingAndHasArtworkArePaired();
  void tags_dedupCaseInsensitively();
  void unknownKey_collectedInUnknownTokens();
  void freeTextAndTokensMix();
  void colonWithoutKey_treatedAsFreeText();
};

void TestSearchQueryParser::emptyInput_returnsEmptyQuery() {
  const auto q = SearchQueryParser::parse(QString());
  QVERIFY(q.freeText.isEmpty());
  QCOMPARE(q.played, TriState::Unspecified);
  QCOMPARE(q.favorite, TriState::Unspecified);
  QCOMPARE(q.missingArtwork, TriState::Unspecified);
  QVERIFY(q.requiredTags.isEmpty());
  QVERIFY(q.unknownTokens.isEmpty());

  const auto q2 = SearchQueryParser::parse(QStringLiteral("   "));
  QVERIFY(q2.freeText.isEmpty());
}

void TestSearchQueryParser::plainText_keepsFreeText() {
  // Strings without colons are left untouched so existing FTS behaviour
  // stays compatible — the AC explicitly requires this.
  const auto q = SearchQueryParser::parse(QStringLiteral("harry potter goblet"));
  QCOMPARE(q.freeText, QStringLiteral("harry potter goblet"));
  QVERIFY(q.requiredTags.isEmpty());
  QVERIFY(q.unknownTokens.isEmpty());
}

void TestSearchQueryParser::played_truePopulatesTriState() {
  const auto q = SearchQueryParser::parse(QStringLiteral("played:true"));
  QCOMPARE(q.played, TriState::True);
  // Token was consumed — it must not leak into FTS.
  QVERIFY(q.freeText.isEmpty());
}

void TestSearchQueryParser::played_falsePopulatesTriState() {
  const auto q = SearchQueryParser::parse(QStringLiteral("PLAYED:False"));
  // Key matching is case-insensitive; value matching too.
  QCOMPARE(q.played, TriState::False);
}

void TestSearchQueryParser::played_invalidValueIsConsumedAsUnknown() {
  // `played:maybe` is structurally a token (recognised key) but the value
  // doesn't parse as a boolean — collect it as unknown so the caller can
  // warn, and consume it so it doesn't slip into FTS as the literal
  // "played:maybe" string.
  const auto q = SearchQueryParser::parse(QStringLiteral("played:maybe"));
  QCOMPARE(q.played, TriState::Unspecified);
  QCOMPARE(q.unknownTokens.size(), 1);
  QCOMPARE(q.unknownTokens.first(), QStringLiteral("played:maybe"));
  QVERIFY(q.freeText.isEmpty());
}

void TestSearchQueryParser::favorite_supportsBritishSpelling() {
  // `favourite:true` and `favorite:true` both map to the same TriState so
  // British-English users aren't punished.
  const auto q1 = SearchQueryParser::parse(QStringLiteral("favourite:yes"));
  QCOMPARE(q1.favorite, TriState::True);
  const auto q2 = SearchQueryParser::parse(QStringLiteral("favorite:no"));
  QCOMPARE(q2.favorite, TriState::False);
}

void TestSearchQueryParser::missingAndHasArtworkArePaired() {
  // missing:artwork sets missingArtwork=True; has:artwork sets it to False.
  // The two are stored on the same axis so the SQL builder only has to
  // check one TriState.
  const auto q1 = SearchQueryParser::parse(QStringLiteral("missing:artwork"));
  QCOMPARE(q1.missingArtwork, TriState::True);
  const auto q2 = SearchQueryParser::parse(QStringLiteral("has:artwork"));
  QCOMPARE(q2.missingArtwork, TriState::False);

  // Unknown nouns under missing/has are collected as unknown.
  const auto q3 = SearchQueryParser::parse(QStringLiteral("missing:notes"));
  QCOMPARE(q3.missingArtwork, TriState::Unspecified);
  QCOMPARE(q3.unknownTokens.first(), QStringLiteral("missing:notes"));
}

void TestSearchQueryParser::tags_dedupCaseInsensitively() {
  const auto q = SearchQueryParser::parse(QStringLiteral("tag:Jazz tag:jazz tag:LIVE"));
  // First-seen casing wins on dedup; same shape as itemmetadata.parseTags.
  QCOMPARE(q.requiredTags, (QStringList{"Jazz", "LIVE"}));
}

void TestSearchQueryParser::unknownKey_collectedInUnknownTokens() {
  const auto q = SearchQueryParser::parse(QStringLiteral("color:red"));
  QCOMPARE(q.unknownTokens.size(), 1);
  QCOMPARE(q.unknownTokens.first(), QStringLiteral("color:red"));
  // The literal token does NOT leak into freeText — otherwise FTS would
  // try to match items named "color:red" against the user's typo.
  QVERIFY(q.freeText.isEmpty());
}

void TestSearchQueryParser::freeTextAndTokensMix() {
  const auto q =
      SearchQueryParser::parse(QStringLiteral("rock concert played:true tag:live missing:artwork"));
  QCOMPARE(q.freeText, QStringLiteral("rock concert"));
  QCOMPARE(q.played, TriState::True);
  QCOMPARE(q.requiredTags, QStringList{"live"});
  QCOMPARE(q.missingArtwork, TriState::True);
  QVERIFY(q.unknownTokens.isEmpty());
}

void TestSearchQueryParser::colonWithoutKey_treatedAsFreeText() {
  // ":foo" and "foo:" aren't valid token shapes — they fall through to
  // free text so the FTS layer can decide what to do with them.
  const auto q1 = SearchQueryParser::parse(QStringLiteral(":foo"));
  QCOMPARE(q1.freeText, QStringLiteral(":foo"));
  const auto q2 = SearchQueryParser::parse(QStringLiteral("foo:"));
  QCOMPARE(q2.freeText, QStringLiteral("foo:"));
}

QTEST_MAIN(TestSearchQueryParser)
#include "test_searchqueryparser.moc"
