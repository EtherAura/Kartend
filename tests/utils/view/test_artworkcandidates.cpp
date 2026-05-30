// Tests for ArtworkCandidates::rank — pure string ranking, no filesystem.
#include <QObject>
#include <QStringList>
#include <QTest>

#include "artworkcandidates.h"

class TestArtworkCandidates : public QObject {
  Q_OBJECT
private slots:
  void emptyInputs_returnEmpty();
  void nonImageFiles_filteredOut();
  void rankedByFuzzyScoreDescending();
  void maxResultsHonored();
  void looksLikeImage_caseInsensitive();
};

void TestArtworkCandidates::emptyInputs_returnEmpty() {
  QVERIFY(ArtworkCandidates::rank("", {"/art/a.png"}, 5).isEmpty());
  QVERIFY(ArtworkCandidates::rank("alpha", {}, 5).isEmpty());
  QVERIFY(ArtworkCandidates::rank("alpha", {"/art/a.png"}, 0).isEmpty());
}

void TestArtworkCandidates::nonImageFiles_filteredOut() {
  // Only image-extension files should reach the ranker — random .txt /
  // .pdf / .mp4 entries from the same directory are dropped.
  QStringList files{"/art/cover.png", "/art/notes.txt", "/art/sample.pdf"};
  const auto out = ArtworkCandidates::rank("cover", files, 10);
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().path, QStringLiteral("/art/cover.png"));
}

void TestArtworkCandidates::rankedByFuzzyScoreDescending() {
  QStringList files{"/art/concert-hall.png", "/art/concert.png", "/art/something-else.png"};
  // "concert" matches both concert* files; the exact / shorter match
  // gets a higher fuzzy score due to consecutive-streak bonuses.
  const auto out = ArtworkCandidates::rank("concert", files, 10);
  QCOMPARE(out.size(), 2);
  QVERIFY(out.first().score >= out.last().score);
  QVERIFY(out.first().path.contains("concert"));
}

void TestArtworkCandidates::maxResultsHonored() {
  QStringList files{"/art/a.png", "/art/aa.png", "/art/aaa.png", "/art/aaaa.png"};
  const auto out = ArtworkCandidates::rank("a", files, 2);
  QCOMPARE(out.size(), 2);
}

void TestArtworkCandidates::looksLikeImage_caseInsensitive() {
  QVERIFY(ArtworkCandidates::looksLikeImage("/art/cover.PNG"));
  QVERIFY(ArtworkCandidates::looksLikeImage("/art/cover.Jpeg"));
  QVERIFY(!ArtworkCandidates::looksLikeImage("/art/cover.txt"));
  QVERIFY(!ArtworkCandidates::looksLikeImage("/art/cover"));
}

QTEST_MAIN(TestArtworkCandidates)
#include "test_artworkcandidates.moc"
