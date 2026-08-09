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
#include <QSet>
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
  void artworkLookupDirectories_listsRootThenTypedCoverSubdirs();
  void areDirectoriesCached_falseWhileAnyCascadeEntryIsCold();
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

// Kartend-t4rjw: a cover lookup is a CASCADE, not a single directory — the
// flat artwork root followed by each typed cover subdir, which is where the
// scrape pipeline actually writes covers. artworkLookupDirectories is the one
// place that spelling lives, so callers reasoning about the lookup as a whole
// (is it warm? what should be prewarmed?) agree with what findCachedWithKeys
// really probes.
void TestDirectoryCache::artworkLookupDirectories_listsRootThenTypedCoverSubdirs() {
  QCOMPARE(ArtworkUtils::artworkLookupDirectories(QString()), QStringList());

  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QStringList dirs = ArtworkUtils::artworkLookupDirectories(root.path());

  // The flat root leads — it is probed first — and every entry after it is a
  // distinct subdirectory of it.
  QVERIFY(dirs.size() > 1);
  QCOMPARE(dirs.first(), root.path());
  QVERIFY(dirs.contains(QDir(root.path()).absoluteFilePath(QStringLiteral("front"))));
  QCOMPARE(QSet<QString>(dirs.cbegin(), dirs.cend()).size(), dirs.size());
  for (const QString &dir : dirs.mid(1)) {
    QVERIFY(dir.startsWith(root.path()));
  }
}

// The predicate CoverFlow's retry hangs its "genuinely artless" verdict on.
// isDirectoryCached(root) answers a narrower question than callers mean:
// warming is not atomic across the cascade, so a warm root says nothing about
// {root}/front. Reading it as "the lookup settled" is what left cover-flow
// cards on the placeholder until the user clicked them.
void TestDirectoryCache::areDirectoriesCached_falseWhileAnyCascadeEntryIsCold() {
  QTemporaryDir root;
  QVERIFY(root.isValid());

  auto &cache = DirectoryCache::instance();
  const QStringList cascade = ArtworkUtils::artworkLookupDirectories(root.path());

  // An empty list is "nothing known to be warm" — never a free pass.
  QVERIFY(!cache.areDirectoriesCached({}));

  // Nothing warm yet.
  QVERIFY(!cache.areDirectoriesCached(cascade));

  // Warm ONLY the flat root. prewarmDirectories expands a root with the cover
  // subdirs that exist when it runs, so creating front/ afterwards leaves it
  // cold — the same split the parallel walk produces transiently when the
  // near-empty root caches long before a front/ holding thousands of files.
  cache.prewarmDirectories({root.path()});
  QVERIFY(cache.isDirectoryCached(root.path()));

  QVERIFY(QDir(root.path()).mkpath(QStringLiteral("front")));
  writeFile(root.filePath(QStringLiteral("front/Game.png")));
  QVERIFY(!cache.isDirectoryCached(QDir(root.path()).absoluteFilePath(QStringLiteral("front"))));
  QVERIFY2(!cache.areDirectoriesCached(cascade),
           "a warm flat root was reported as a warm cascade, so an unresolved cover would be "
           "misread as 'this item has no artwork'");

  // Warming the rest settles it — including the subdirs that do not exist,
  // which ensureDirectoryCached caches as empty listings. Without that this
  // predicate could never go true for a normal collection.
  cache.prewarmDirectories(cascade);
  QVERIFY(cache.areDirectoriesCached(cascade));
}

QTEST_GUILESS_MAIN(TestDirectoryCache)
#include "test_directorycache.moc"
