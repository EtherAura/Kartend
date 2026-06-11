// Performance regression benchmarks for filter hot paths.
//
// Filter rebuild runs once per keystroke during search across the full visible
// item set, so it's a hot path that's easy to regress (e.g. by accidentally
// re-introducing per-item toLower() allocations). These benchmarks track
// QBENCHMARK walltime so regressions are visible in CI artifact diffs.
//
// To run only benchmarks:
//   ctest -L benchmark
//
// Wallclock numbers vary by hardware — don't fail CI on absolute thresholds.
// Compare against a stored baseline to detect drift.

#include "filterhelpers.h"

#include <QTest>

class BenchFilterHelpers : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  // Case-insensitive substring search across many subcollection names.
  // The hot path inside FilterManager::rebuildFilteredIndices.
  void subcollectionNameMatches_caseInsensitive_data();
  void subcollectionNameMatches_caseInsensitive();

  // Display-name resolution: lookup-hit and basename-fallback paths.
  // Called once per item in the filter rebuild loop.
  void displayNameForMediaEntry_lookupHit();
  void displayNameForMediaEntry_basenameFallback();

private:
  // 1024 distinct subcollection names with realistic mixed-case content.
  QStringList m_subcollectionNames;
  // Path-to-display map populated with 1024 entries for the lookup-hit bench.
  QHash<QString, QString> m_filePathToDisplayName;
};

void BenchFilterHelpers::initTestCase() {
  // Realistic spread of name lengths and prefixes so the substring search
  // can't trivially short-circuit on the first character.
  const QStringList prefixes = {QStringLiteral("Super"), QStringLiteral("Mega"),
                                QStringLiteral("Hyper"), QStringLiteral("Ultra"),
                                QStringLiteral("Final"), QStringLiteral("ZELDA"),
                                QStringLiteral("Mario"), QStringLiteral("Sonic")};
  m_subcollectionNames.reserve(1024);
  for (int i = 0; i < 1024; ++i) {
    const QString &prefix = prefixes[i % prefixes.size()];
    m_subcollectionNames.append(
        QStringLiteral("%1 Title #%2 (Region %3)").arg(prefix).arg(i).arg(i % 4));
  }

  m_filePathToDisplayName.reserve(1024);
  for (int i = 0; i < 1024; ++i) {
    m_filePathToDisplayName.insert(QStringLiteral("/data/rom_%1.zip").arg(i),
                                   QStringLiteral("Game Title %1").arg(i));
  }
}

void BenchFilterHelpers::subcollectionNameMatches_caseInsensitive_data() {
  QTest::addColumn<QString>("needle");
  QTest::newRow("hit-prefix") << QStringLiteral("super");
  QTest::newRow("hit-mid") << QStringLiteral("title");
  QTest::newRow("hit-mixed-case") << QStringLiteral("ZELDA");
  QTest::newRow("miss") << QStringLiteral("nonexistent_xyz_qq");
}

void BenchFilterHelpers::subcollectionNameMatches_caseInsensitive() {
  QFETCH(QString, needle);
  QBENCHMARK {
    int hits = 0;
    for (const QString &name : std::as_const(m_subcollectionNames)) {
      if (FilterHelpers::subcollectionNameMatches(name, needle)) {
        ++hits;
      }
    }
    // Touch the result so the optimizer can't hoist the loop.
    QVERIFY(hits >= 0);
  }
}

void BenchFilterHelpers::displayNameForMediaEntry_lookupHit() {
  // showAllSubcollectionItems=true with a populated map — realistic for the
  // current default setting. mediaDirectory is empty (unused in this branch).
  QBENCHMARK {
    int total = 0;
    for (int i = 0; i < 1024; ++i) {
      const QString rawEntry = QStringLiteral("/data/rom_%1.zip").arg(i);
      const QString name = FilterHelpers::displayNameForMediaEntry(
          rawEntry, /*showAllSubcollectionItems=*/true, /*mediaDirectory=*/QString(),
          &m_filePathToDisplayName, /*fileNames=*/nullptr);
      total += name.size();
    }
    QVERIFY(total > 0);
  }
}

void BenchFilterHelpers::displayNameForMediaEntry_basenameFallback() {
  // Empty maps + no media directory → exercise the QFileInfo fallback path.
  QBENCHMARK {
    int total = 0;
    for (int i = 0; i < 1024; ++i) {
      const QString rawEntry = QStringLiteral("/some/dir/file_%1.bin").arg(i);
      const QString name = FilterHelpers::displayNameForMediaEntry(
          rawEntry, /*showAllSubcollectionItems=*/true, /*mediaDirectory=*/QString(),
          /*filePathToDisplayName=*/nullptr, /*fileNames=*/nullptr);
      total += name.size();
    }
    QVERIFY(total > 0);
  }
}

QTEST_MAIN(BenchFilterHelpers)
#include "bench_filterhelpers.moc"
