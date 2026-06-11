// Artwork path-resolution benchmarks (Kartend-w9s8e).
//
// ArtworkUtils::findArtworkForFile / findArtworkForFileCached run once per
// visible tile during scroll (and once per item in bulk subcollection
// loads), so a regression here — e.g. re-introducing a per-candidate
// QFileInfo::exists() storm or losing the DirectoryCache fast path — turns
// directly into scroll jank. Several historical regressions in exactly this
// area were only caught by manual trace sessions; these QBENCHMARKs make
// the cost visible in the nightly benchmark job instead.
//
// The fixture is a synthetic temp-dir tree: one artwork directory holding
// 2000 .png files, half of which match the synthetic media names. The hit
// and miss paths are measured separately for both the stat-per-candidate
// uncached entry point and the DirectoryCache-backed one (warm + cold).
//
// To run only benchmarks:
//   ctest -L benchmark
//
// Wallclock numbers vary by hardware — don't fail CI on absolute
// thresholds. Compare against a stored baseline to detect drift.

#include "artworkutils.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {
constexpr int kArtworkFiles = 2000;
constexpr int kLookupsPerIteration = 200;
} // namespace

class BenchArtworkResolve : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();

  // Uncached path: every lookup probes candidate extensions on disk.
  void findArtworkForFile_hit();
  void findArtworkForFile_miss();

  // DirectoryCache fast path, directory listing already cached.
  void findArtworkForFileCached_warmHit();
  void findArtworkForFileCached_warmMiss();

  // First-touch cost: each iteration drops the cache and rebuilds the
  // 2000-entry directory listing — the walk a collection switch pays
  // before cached lookups go warm.
  void directoryCache_coldPrewarm();

private:
  QTemporaryDir m_tree;
  QString m_artworkDir;
  QStringList m_hitNames;  // media names with a matching .png
  QStringList m_missNames; // media names with no artwork at all
};

void BenchArtworkResolve::initTestCase() {
  QVERIFY(m_tree.isValid());
  m_artworkDir = QDir(m_tree.path()).filePath(QStringLiteral("artwork"));
  QVERIFY(QDir().mkpath(m_artworkDir));

  // 2000 artwork files; media items 0..999 have art, 1000..1199 don't.
  // Names are realistic multi-token basenames so candidate composition
  // (basename + extension probing) isn't trivially short.
  QDir artDir(m_artworkDir);
  for (int i = 0; i < kArtworkFiles; ++i) {
    const QString name = QStringLiteral("nature-episode-%1-remastered.png").arg(i);
    QFile f(artDir.filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("png");
    f.close();
  }
  m_hitNames.reserve(kLookupsPerIteration);
  m_missNames.reserve(kLookupsPerIteration);
  for (int i = 0; i < kLookupsPerIteration; ++i) {
    m_hitNames.append(QStringLiteral("nature-episode-%1-remastered.mp4").arg(i));
    m_missNames.append(QStringLiteral("unscraped-episode-%1.mp4").arg(i));
  }
}

void BenchArtworkResolve::findArtworkForFile_hit() {
  qsizetype found = 0;
  QBENCHMARK {
    for (const QString &name : std::as_const(m_hitNames)) {
      found += ArtworkUtils::findArtworkForFile(name, m_artworkDir).size();
    }
  }
  QVERIFY(found > 0);
}

void BenchArtworkResolve::findArtworkForFile_miss() {
  qsizetype found = 0;
  QBENCHMARK {
    for (const QString &name : std::as_const(m_missNames)) {
      found += ArtworkUtils::findArtworkForFile(name, m_artworkDir).size();
    }
  }
  QCOMPARE(found, qsizetype(0));
}

void BenchArtworkResolve::findArtworkForFileCached_warmHit() {
  ArtworkUtils::clearDirectoryCache();
  // Warm the cache outside the measured region. findArtworkForFileCached
  // itself is non-blocking on a cold directory (it queues a background
  // scan and returns empty), so warm synchronously via prewarmDirectories.
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({m_artworkDir});
  qsizetype found = 0;
  QBENCHMARK {
    for (const QString &name : std::as_const(m_hitNames)) {
      found += ArtworkUtils::findArtworkForFileCached(name, m_artworkDir).size();
    }
  }
  QVERIFY(found > 0);
}

void BenchArtworkResolve::findArtworkForFileCached_warmMiss() {
  ArtworkUtils::clearDirectoryCache();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({m_artworkDir});
  qsizetype found = 0;
  QBENCHMARK {
    for (const QString &name : std::as_const(m_missNames)) {
      found += ArtworkUtils::findArtworkForFileCached(name, m_artworkDir).size();
    }
  }
  QCOMPARE(found, qsizetype(0));
}

void BenchArtworkResolve::directoryCache_coldPrewarm() {
  QBENCHMARK {
    ArtworkUtils::clearDirectoryCache();
    ArtworkUtils::DirectoryCache::instance().prewarmDirectories({m_artworkDir});
  }
}

QTEST_MAIN(BenchArtworkResolve)
#include "bench_artworkresolve.moc"
