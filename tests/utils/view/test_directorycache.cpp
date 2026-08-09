// DirectoryCache contract tests (Kartend-s723v / Kartend-bjrw1): the
// non-blocking queue-on-cold-dir behaviour, positive lookups from a cached
// listing, the miss-probe that patches externally-dropped files into the
// cache, and the NEGATIVE caching introduced by Kartend-bjrw1 — a
// (dir, baseName) whose stat sweep found nothing must not re-stat on
// re-materialization until clear(). DirectoryCache is a process singleton,
// so every slot starts with clear() and uses its own QTemporaryDir.
#include "artworkutils.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

using ArtworkUtils::DirectoryCache;

namespace {

void writeFile(const QString &path, const QByteArray &bytes = QByteArrayLiteral("px")) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

} // namespace

class TestDirectoryCache : public QObject {
  Q_OBJECT
private slots:
  void init();

  void coldDirectory_queuesAndReturnsEmpty();
  void cachedDirectory_findsExistingFile();
  void missProbe_patchesExternallyDroppedFile();
  void negativeResult_cachedUntilClear();
  void nonexistentDirectory_cachedAsEmpty();
  void baseNameLookup_doesNotDoubleStripDottedStems();
  void schedulePrewarm_queuesEvenWhenAWalkIsAlreadyInFlight();
};

void TestDirectoryCache::init() {
  DirectoryCache::instance().clear();
}

void TestDirectoryCache::coldDirectory_queuesAndReturnsEmpty() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.path() + "/title.png");

  // Never cached: non-blocking contract returns empty and queues the dir.
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("title"), tmp.path()),
           QString());
  QVERIFY(DirectoryCache::instance().isDirectoryQueued(tmp.path()));

  // Drain the queue (what the background prewarm thread does) — the file
  // is now resolvable and the dir no longer queued.
  DirectoryCache::instance().processQueuedDirectories();
  QVERIFY(!DirectoryCache::instance().isDirectoryQueued(tmp.path()));
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("title"), tmp.path()),
           tmp.path() + "/title.png");
}

void TestDirectoryCache::cachedDirectory_findsExistingFile() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.path() + "/cover.png");
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("cover"), tmp.path()),
           tmp.path() + "/cover.png");
  // Unknown name in a cached dir misses (and negative-caches, below).
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("absent"), tmp.path()),
           QString());
}

void TestDirectoryCache::missProbe_patchesExternallyDroppedFile() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  // Dropped AFTER the dir was cached: the listing doesn't know it, but the
  // first miss for this basename stat-probes the expected per-extension
  // paths and patches the cache (the manual-artwork-drop freshness path).
  writeFile(tmp.path() + "/late.png");
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("late"), tmp.path()),
           tmp.path() + "/late.png");
  // And the patch makes the next lookup a pure cache hit.
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("late"), tmp.path()),
           tmp.path() + "/late.png");
}

void TestDirectoryCache::negativeResult_cachedUntilClear() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  // First miss runs the stat sweep, finds nothing, and caches the NEGATIVE
  // (Kartend-bjrw1).
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("ghost"), tmp.path()),
           QString());

  // A file dropped after the negative was recorded stays invisible — the
  // sweep must NOT re-run per re-materialization (that per-tile GUI-thread
  // stat storm is what Kartend-bjrw1 removes). Same staleness contract as
  // the directory listing itself.
  writeFile(tmp.path() + "/ghost.png");
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("ghost"), tmp.path()),
           QString());

  // clear() starts a fresh cache generation: the dir re-queues, a drain
  // re-scans it, and the file surfaces.
  DirectoryCache::instance().clear();
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("ghost"), tmp.path()),
           QString()); // queued, non-blocking
  DirectoryCache::instance().processQueuedDirectories();
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("ghost"), tmp.path()),
           tmp.path() + "/ghost.png");
}

void TestDirectoryCache::baseNameLookup_doesNotDoubleStripDottedStems() {
  // Regression pin: findArtworkForFileCached strips its argument itself, so
  // a caller holding an already-stripped stem must use
  // findArtworkForBaseNameCached. Passing the stem to the fileName variant
  // double-strips dotted stems ("Game v1.2" → "Game v1") and, with a
  // neighboring item of that shorter name, resolves the WRONG item's art —
  // the details-pane bug this variant exists for.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.path() + "/Game v1.png");
  writeFile(tmp.path() + "/Game v1.2.png");
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  // The stem variant probes exactly the given stem.
  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Game v1.2"), tmp.path()),
           tmp.path() + "/Game v1.2.png");
  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Game v1"), tmp.path()),
           tmp.path() + "/Game v1.png");

  // The fileName variant keeps its strip-then-probe contract for full names.
  QCOMPARE(ArtworkUtils::findArtworkForFileCached(QStringLiteral("Game v1.2.rom"), tmp.path()),
           tmp.path() + "/Game v1.2.png");
}

void TestDirectoryCache::nonexistentDirectory_cachedAsEmpty() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString missing = tmp.path() + "/no-such-subdir";
  DirectoryCache::instance().prewarmDirectories({missing});

  // Cached as an empty listing; the first basename miss sweeps once,
  // negative-caches, and subsequent lookups are pure hash hits.
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("x"), missing), QString());
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("x"), missing), QString());
  QVERIFY(!DirectoryCache::instance().isDirectoryQueued(missing));
}

// Kartend-hrgf5: schedulePrewarm caps at one in-flight walk. It used to return
// outright when it lost that race, on the assumption that the running walk
// would cover the caller's directories anyway. That is false for a caller
// asking about directories nothing else will touch — CoverFlow's pending
// artwork retry polls isDirectoryCached() on exactly those, so a dropped
// request left its cards on the placeholder forever.
//
// The contract now: losing the race may drop the WALK, but never the REQUEST —
// the directory must at least be QUEUED, because every walk drains the queue
// before it finishes.
void TestDirectoryCache::schedulePrewarm_queuesEvenWhenAWalkIsAlreadyInFlight() {
  QTemporaryDir busy;   // what the in-flight walk is working on
  QTemporaryDir wanted; // what the displaced caller asks for
  QVERIFY(busy.isValid() && wanted.isValid());
  writeFile(wanted.filePath("Game.png"));

  auto &cache = DirectoryCache::instance();

  // Occupy the single in-flight slot with a walk over a large synthetic tree,
  // so the call below is the one that loses the race.
  QStringList busyDirs;
  for (int i = 0; i < 150; ++i) {
    const QString sub = QStringLiteral("d%1").arg(i);
    QDir(busy.path()).mkpath(sub);
    busyDirs << busy.filePath(sub);
  }
  cache.schedulePrewarm(busyDirs);

  // The displaced request. Whether it wins or loses the guard is timing, but
  // either outcome must leave `wanted` reachable: cached if it ran, queued if
  // it was displaced. Silently forgetting it is the bug.
  cache.schedulePrewarm({wanted.path()});

  const bool reachable =
      cache.isDirectoryCached(wanted.path()) || cache.isDirectoryQueued(wanted.path());
  QVERIFY2(reachable, "schedulePrewarm dropped the request when it lost the in-flight race; "
                      "the directory is neither cached nor queued, so nothing will ever scan it");

  // Being queued is only half the contract — a walk must actually drain it.
  // This also stops the test leaving a walk IN FLIGHT: the pool thread would
  // outlive the test and keep touching the singleton the next slot's init()
  // clears, which segfaults at teardown under `ctest -j`.
  QTRY_VERIFY_WITH_TIMEOUT(cache.isDirectoryCached(wanted.path()), 15000);
}

QTEST_GUILESS_MAIN(TestDirectoryCache)
#include "test_directorycache.moc"
