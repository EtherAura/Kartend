// Collection-load sort benchmarks (Kartend-w9s8e).
//
// QueryManagerInternal::sortFiles runs on every collection load over the
// full combined file list, so its cost scales with library size and lands
// directly in the time-to-first-tile. The Date/Size modes additionally
// carry a stat-fallback (Kartend-m9r1s) whose accidental re-engagement —
// e.g. a regression that stops threading the prefetched mtime/size maps
// through — turns an in-memory sort into 10k filesystem stats. These
// QBENCHMARKs pin the in-memory costs so the nightly benchmark job makes
// drift visible.
//
// The fixture is 10k synthetic absolute paths shaped like a real library
// (mixed-case multi-token names, numbered entries across subdirectories).
// Paths intentionally do NOT exist on disk; the Date/Size benches supply
// fully-populated metadata maps so the measured work is the sort itself,
// not the missing-key stat fallback.
//
// To run only benchmarks:
//   ctest -L benchmark
//
// Wallclock numbers vary by hardware — don't fail CI on absolute
// thresholds. Compare against a stored baseline to detect drift.

#include "querymanagerhelpers.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTest>

namespace {
constexpr int kFileCount = 10000;
} // namespace

class BenchSortFiles : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();

  // The default mode every collection load pays.
  void sortFiles_nameAscending_10k();
  void sortFiles_nameDescending_10k();

  // Map-backed Date/Size sorts (the Kartend-m9r1s fast path).
  void sortFiles_dateDescending_10k_mapBacked();
  void sortFiles_sizeDescending_10k_mapBacked();

private:
  QStringList m_paths; // pre-shuffled synthetic library
  QHash<QString, qint64> m_mtimeMsByPath;
  QHash<QString, qint64> m_sizeByPath;
};

void BenchSortFiles::initTestCase() {
  const QStringList stems = {QStringLiteral("Nature Documentary"), QStringLiteral("city-tour"),
                             QStringLiteral("Cooking Course"),     QStringLiteral("OCEAN life"),
                             QStringLiteral("history lecture"),    QStringLiteral("Drum Lesson")};
  m_paths.reserve(kFileCount);
  m_mtimeMsByPath.reserve(kFileCount);
  m_sizeByPath.reserve(kFileCount);
  for (int i = 0; i < kFileCount; ++i) {
    // Spread across subdirectories and interleave stems so the comparator
    // sees realistic prefix collisions instead of pre-sorted input.
    const QString path = QStringLiteral("/library/set-%1/%2 - part %3.mp4")
                             .arg(i % 7)
                             .arg(stems[i % stems.size()])
                             .arg((i * 37) % kFileCount);
    m_paths.append(path);
    // Deterministic pseudo-random keys; the multipliers break any
    // correlation with the name order so the sorts do real work.
    m_mtimeMsByPath.insert(path, qint64((i * 2654435761U) % 1000000000));
    m_sizeByPath.insert(path, qint64((i * 40503U) % 50000000));
  }
}

void BenchSortFiles::sortFiles_nameAscending_10k() {
  QBENCHMARK {
    QStringList work = m_paths;
    QueryManagerInternal::sortFiles(work, SortMode::NameAscending);
  }
}

void BenchSortFiles::sortFiles_nameDescending_10k() {
  QBENCHMARK {
    QStringList work = m_paths;
    QueryManagerInternal::sortFiles(work, SortMode::NameDescending);
  }
}

void BenchSortFiles::sortFiles_dateDescending_10k_mapBacked() {
  QBENCHMARK {
    QStringList work = m_paths;
    QueryManagerInternal::sortFiles(work, SortMode::DateDescending, &m_mtimeMsByPath, nullptr);
  }
}

void BenchSortFiles::sortFiles_sizeDescending_10k_mapBacked() {
  QBENCHMARK {
    QStringList work = m_paths;
    QueryManagerInternal::sortFiles(work, SortMode::SizeDescending, nullptr, &m_sizeByPath);
  }
}

QTEST_MAIN(BenchSortFiles)
#include "bench_sortfiles.moc"
