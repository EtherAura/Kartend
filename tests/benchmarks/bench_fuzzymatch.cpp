// Walltime benchmark for FuzzyMatch::score — invoked per candidate per keystroke
// in the command palette, so its cost is multiplied across the whole entry list
// on every character typed. QBENCHMARK over a large synthetic candidate list so
// a scoring-cost regression is visible in CI artifact diffs. The suite is
// labelled "benchmark" and never fails CI on an absolute threshold.
#include "fuzzymatch.h"

#include <algorithm>

#include <QString>
#include <QStringList>
#include <QTest>

class BenchFuzzyMatch : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void score_largeCandidateList();

private:
  QStringList m_candidates;
};

void BenchFuzzyMatch::initTestCase() {
  m_candidates.reserve(5000);
  for (int i = 0; i < 5000; ++i) {
    m_candidates.append(QStringLiteral("Collection %1 - Sample Title %2").arg(i % 50).arg(i));
  }
}

void BenchFuzzyMatch::score_largeCandidateList() {
  // One keystroke = scoring the query against every candidate.
  const QString query = QStringLiteral("samp");
  volatile int sink = 0;
  QBENCHMARK {
    int best = -1;
    for (const QString &cand : m_candidates) {
      best = std::max(best, FuzzyMatch::score(query, cand));
    }
    sink += best;
  }
  Q_UNUSED(sink);
}

QTEST_MAIN(BenchFuzzyMatch)
#include "bench_fuzzymatch.moc"
