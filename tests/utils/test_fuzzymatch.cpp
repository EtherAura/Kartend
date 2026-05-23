// Tests for FuzzyMatch::score — pure string comparison, no Qt event loop.
#include <QObject>
#include <QTest>

#include "fuzzymatch.h"

class TestFuzzyMatch : public QObject {
  Q_OBJECT
private slots:
  void emptyQuery_matchesAnything();
  void emptyCandidate_failsForNonEmptyQuery();
  void simpleSubsequenceMatches();
  void mismatchedOrderDoesNotMatch();
  void caseInsensitive();
  void wordBoundaryScoresHigherThanMidWord();
  void consecutiveMatchScoresHigherThanScatteredMatch();
};

void TestFuzzyMatch::emptyQuery_matchesAnything() {
  QCOMPARE(FuzzyMatch::score(QString(), "Open settings"), 0);
}

void TestFuzzyMatch::emptyCandidate_failsForNonEmptyQuery() {
  QCOMPARE(FuzzyMatch::score("foo", QString()), -1);
}

void TestFuzzyMatch::simpleSubsequenceMatches() {
  QVERIFY(FuzzyMatch::score("os", "Open Settings") > 0);
  QVERIFY(FuzzyMatch::score("opnst", "Open Settings") > 0);
}

void TestFuzzyMatch::mismatchedOrderDoesNotMatch() {
  // Query letters must appear IN ORDER in the candidate.
  QCOMPARE(FuzzyMatch::score("so", "Open Settings"), -1);
  // Letters not in the candidate at all also fail.
  QCOMPARE(FuzzyMatch::score("xyz", "Open Settings"), -1);
}

void TestFuzzyMatch::caseInsensitive() {
  QCOMPARE(FuzzyMatch::score("OPEN", "open settings"), FuzzyMatch::score("open", "open settings"));
  QCOMPARE(FuzzyMatch::score("Open", "OPEN SETTINGS"), FuzzyMatch::score("open", "open settings"));
}

void TestFuzzyMatch::wordBoundaryScoresHigherThanMidWord() {
  // "rs" at "Rescan" word boundary should score better than "rs" inside
  // a word like "browser". Confirms the boundary bonus actually kicks in.
  const int boundary = FuzzyMatch::score("rs", "Rescan stats");
  const int midWord = FuzzyMatch::score("rs", "Browse subset");
  QVERIFY(boundary > 0);
  QVERIFY(midWord > 0);
  QVERIFY(boundary > midWord);
}

void TestFuzzyMatch::consecutiveMatchScoresHigherThanScatteredMatch() {
  // Typing "open" against a candidate that contains "open" contiguously
  // should beat one where the same letters are scattered through other
  // words — that's the consecutive-streak bonus.
  const int contiguous = FuzzyMatch::score("open", "Open settings");
  const int scattered = FuzzyMatch::score("open", "o p e n");
  QVERIFY(contiguous > 0);
  QVERIFY(scattered > 0);
  QVERIFY(contiguous > scattered);
}

QTEST_MAIN(TestFuzzyMatch)
#include "test_fuzzymatch.moc"
