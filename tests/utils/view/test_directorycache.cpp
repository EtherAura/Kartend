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
#include <QFileInfo>
#include <QHash>
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

  void discMarkedArtwork_answersToTheReleaseBaseTitle();
  void discFallback_takesTheLowestDisc();
  void discFallback_keepsTheMarkerOutOfADottedStem();
  void exactMatch_outranksTheDiscFallbackAcrossTheWholeCascade();
  void discFallback_reachesTypedCoverSubdirs();
  void discFallback_ignoresTagsThatAreNotDiscMarkers();
  void discFallback_countsAsArtworkForBulkPredicates();
  void discFallback_reachesTheDirectLookupOnceTheListingIsWarm();

  void pathMap_agreesWithThePerItemCascade();
  void pathMap_skipsCachedNegativesAndColdDirectories();
  void refreshDirectories_replacesAListingCachedEarlier();
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

// ─────────────────────────────────────────────────────────────────────────────
// Kartend-knub1: artwork filed per DISC answers to the release's base title.
//
// Multi-disc grouping collapses "Recital (Disc 1).flac" + "(Disc 2).flac" into
// one item backed by a generated playlist named for the release — "Recital".
// Art beside the media is named per disc, so nothing matches that item exactly
// and it rendered the placeholder over a fully populated artwork folder. The
// rule these pin: exact names win everywhere first; only then does a
// disc-marked file stand in for the release title, lowest disc first.
// ─────────────────────────────────────────────────────────────────────────────

void TestDirectoryCache::discMarkedArtwork_answersToTheReleaseBaseTitle() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath(QStringLiteral("Recital (Disc 1).png")));
  writeFile(tmp.filePath(QStringLiteral("Recital (Disc 2).png")));
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  // What the collapsed item asks for: the generated playlist's file name,
  // which is the base title with the disc tag stripped.
  QCOMPARE(ArtworkUtils::findArtworkForFileCached(QStringLiteral("Recital.m3u"), tmp.path()),
           tmp.filePath(QStringLiteral("Recital (Disc 1).png")));

  // The discs themselves keep resolving their own art exactly (grouping off).
  QCOMPARE(
      ArtworkUtils::findArtworkForFileCached(QStringLiteral("Recital (Disc 2).flac"), tmp.path()),
      tmp.filePath(QStringLiteral("Recital (Disc 2).png")));
}

void TestDirectoryCache::discFallback_takesTheLowestDisc() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  // Deliberately not written in disc order, and mixing the marker spellings:
  // the answer must come from the parsed order, not from readdir.
  writeFile(tmp.filePath(QStringLiteral("Suite [Side A].png")));
  writeFile(tmp.filePath(QStringLiteral("Suite (CD 10).png")));
  writeFile(tmp.filePath(QStringLiteral("Suite (Disk 2).png")));
  writeFile(tmp.filePath(QStringLiteral("Suite (Disc 1).png")));
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Suite"), tmp.path()),
           tmp.filePath(QStringLiteral("Suite (Disc 1).png")));
}

void TestDirectoryCache::discFallback_keepsTheMarkerOutOfADottedStem() {
  // The marker has to be parsed off the full file NAME. Stripping the
  // extension first and parsing the stem re-strips at the dot inside the
  // title, so "Concert v1.2 (Disc 1).png" would be read as "Concert v1" with
  // no marker at all and the release would find nothing.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath(QStringLiteral("Concert v1.2 (Disc 1).png")));
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Concert v1.2"), tmp.path()),
           tmp.filePath(QStringLiteral("Concert v1.2 (Disc 1).png")));
}

void TestDirectoryCache::exactMatch_outranksTheDiscFallbackAcrossTheWholeCascade() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath(QStringLiteral("Recital.png")));
  writeFile(tmp.filePath(QStringLiteral("Recital (Disc 1).png")));

  // Art the user filed for the release itself, in a typed subdir this time,
  // still outranks a disc's art sitting at the flat root — the exact pass
  // completes over the WHOLE cascade before any fallback runs.
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("front")));
  writeFile(tmp.filePath(QStringLiteral("front/Encore.png")));
  writeFile(tmp.filePath(QStringLiteral("Encore (Disc 1).png")));
  DirectoryCache::instance().prewarmDirectories(ArtworkUtils::artworkLookupDirectories(tmp.path()));

  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Recital"), tmp.path()),
           tmp.filePath(QStringLiteral("Recital.png")));
  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Encore"), tmp.path()),
           tmp.filePath(QStringLiteral("front/Encore.png")));
}

void TestDirectoryCache::discFallback_reachesTypedCoverSubdirs() {
  // Scrapes and hand-sorted libraries put covers under front/ rather than at
  // the flat root, so the fallback has to walk the same subdir cascade the
  // exact lookup does.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("front")));
  writeFile(tmp.filePath(QStringLiteral("front/Anthology (CD1).png")));
  DirectoryCache::instance().prewarmDirectories(ArtworkUtils::artworkLookupDirectories(tmp.path()));

  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Anthology"), tmp.path()),
           tmp.filePath(QStringLiteral("front/Anthology (CD1).png")));
}

void TestDirectoryCache::discFallback_ignoresTagsThatAreNotDiscMarkers() {
  // Same vocabulary MultiDisc::group() collapses on, and no wider: a region or
  // edition tag is part of the title, not a disc, and letting it fall back
  // would hand one release's cover to another.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath(QStringLiteral("Sonata (Special Edition).png")));
  writeFile(tmp.filePath(QStringLiteral("Ballad (USA).png")));
  writeFile(tmp.filePath(QStringLiteral("Nocturne Disc 1.png"))); // no brackets
  DirectoryCache::instance().prewarmDirectories({tmp.path()});

  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Sonata"), tmp.path()),
           QString());
  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Ballad"), tmp.path()),
           QString());
  QCOMPARE(ArtworkUtils::findArtworkForBaseNameCached(QStringLiteral("Nocturne"), tmp.path()),
           QString());
}

void TestDirectoryCache::discFallback_countsAsArtworkForBulkPredicates() {
  // FilterManager's hide-missing-artwork pass tests membership in this set
  // instead of running the per-item cascade. If the two disagree, the grouped
  // item is hidden as artless while painting a cover.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath(QStringLiteral("Recital (Disc 1).png")));
  DirectoryCache::instance().prewarmDirectories(ArtworkUtils::artworkLookupDirectories(tmp.path()));

  const QSet<QString> keys = ArtworkUtils::buildArtworkKeySet(tmp.path());
  QVERIFY2(keys.contains(QStringLiteral("Recital")),
           "the release title resolves a cover per-item but was reported artless in bulk");
  QVERIFY(keys.contains(QStringLiteral("Recital (Disc 1)")));
}

void TestDirectoryCache::discFallback_reachesTheDirectLookupOnceTheListingIsWarm() {
  // findArtworkForFile is the synchronous cold-start / post-prewarm path. It
  // probes exact candidate paths, and a disc number cannot be derived from the
  // item's name, so the fallback resolves through the listing: cold means the
  // pre-Kartend-knub1 answer, and the tile picks the cover up on the
  // reconfigure that follows the prewarm.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath(QStringLiteral("Recital (Disc 1).png")));

  QCOMPARE(ArtworkUtils::findArtworkForFile(QStringLiteral("Recital.m3u"), tmp.path()), QString());

  DirectoryCache::instance().prewarmDirectories(ArtworkUtils::artworkLookupDirectories(tmp.path()));
  QCOMPARE(ArtworkUtils::findArtworkForFile(QStringLiteral("Recital.m3u"), tmp.path()),
           tmp.filePath(QStringLiteral("Recital (Disc 1).png")));

  // An exact match still short-circuits ahead of the fallback on this path too.
  writeFile(tmp.filePath(QStringLiteral("Recital.png")));
  QCOMPARE(ArtworkUtils::findArtworkForFile(QStringLiteral("Recital.m3u"), tmp.path()),
           tmp.filePath(QStringLiteral("Recital.png")));
}

// Kartend-guyc5. buildArtworkPathMap is the bulk form of the per-item lookup,
// for callers that must STORE what it resolves (the scan pipeline, filling
// items.artwork_path). If the two ever disagree, an item is filed against one
// cover and painted with another — so the map is asserted against
// findArtworkForFileCached itself rather than against hand-written paths.
void TestDirectoryCache::pathMap_agreesWithThePerItemCascade() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QDir root(tmp.path());
  QVERIFY(root.mkpath(QStringLiteral("front")));
  QVERIFY(root.mkpath(QStringLiteral("box")));
  // One name per shape the cascade can answer in: flat root, a typed subdir, a
  // lower-priority typed subdir, the full-file-name key, and a release title
  // that only disc-marked art answers for.
  writeFile(root.filePath(QStringLiteral("Flat.png")));
  writeFile(root.filePath(QStringLiteral("front/Front.png")));
  writeFile(root.filePath(QStringLiteral("box/Boxed.png")));
  writeFile(root.filePath(QStringLiteral("Named.bin.png")));
  writeFile(root.filePath(QStringLiteral("Recital (Disc 2).png")));
  writeFile(root.filePath(QStringLiteral("Recital (Disc 1).png")));
  // Both a flat-root cover AND a typed-subdir one: the flat root must win in
  // the map exactly as it does in the cascade.
  writeFile(root.filePath(QStringLiteral("Both.png")));
  writeFile(root.filePath(QStringLiteral("front/Both.png")));

  DirectoryCache::instance().prewarmDirectories(ArtworkUtils::artworkLookupDirectories(tmp.path()));
  const QHash<QString, QString> map = ArtworkUtils::buildArtworkPathMap(tmp.path());

  const QStringList itemFiles{QStringLiteral("Flat.bin"),    QStringLiteral("Front.bin"),
                              QStringLiteral("Boxed.bin"),   QStringLiteral("Named.bin"),
                              QStringLiteral("Recital.m3u"), QStringLiteral("Both.bin"),
                              QStringLiteral("Absent.bin")};
  for (const QString &itemFile : itemFiles) {
    const QString baseName = QFileInfo(itemFile).completeBaseName();
    QString viaMap = map.value(ArtworkUtils::baseMatchKey(baseName));
    if (viaMap.isEmpty()) {
      viaMap = map.value(ArtworkUtils::baseMatchKey(itemFile));
    }
    const QString viaCascade = ArtworkUtils::findArtworkForFileCached(itemFile, tmp.path());
    QCOMPARE(viaMap, viaCascade);
  }
  // Spot-check the two answers that are easy to get subtly wrong.
  QCOMPARE(map.value(ArtworkUtils::baseMatchKey(QStringLiteral("Both"))),
           root.filePath(QStringLiteral("Both.png")));
  QCOMPARE(map.value(ArtworkUtils::baseMatchKey(QStringLiteral("Recital"))),
           root.filePath(QStringLiteral("Recital (Disc 1).png")));
  QVERIFY(!map.contains(ArtworkUtils::baseMatchKey(QStringLiteral("Absent"))));
}

// The map reads only what the cache holds. A cached NEGATIVE must not claim a
// key (the per-item cascade keeps probing the remaining directories after one),
// and a directory nobody has scanned contributes nothing but gets queued —
// mirroring findInDirectory's non-blocking contract.
void TestDirectoryCache::pathMap_skipsCachedNegativesAndColdDirectories() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QDir root(tmp.path());
  QVERIFY(root.mkpath(QStringLiteral("front")));
  writeFile(root.filePath(QStringLiteral("front/Solo.png")));

  // Cache the flat root ONLY — refreshDirectories takes the list literally,
  // where prewarmDirectories would expand it with the typed cover subdirs and
  // warm front/ along with it. Then miss on the root, which caches a negative
  // for "Solo" there while the real cover sits in front/.
  DirectoryCache::instance().refreshDirectories(QStringList{tmp.path()});
  QCOMPARE(DirectoryCache::instance().findInDirectory(QStringLiteral("Solo"), tmp.path()),
           QString());

  // front/ is still cold: it contributes nothing and is queued, so the map is
  // empty rather than wrong.
  const QHash<QString, QString> cold = ArtworkUtils::buildArtworkPathMap(tmp.path());
  QVERIFY(!cold.contains(ArtworkUtils::baseMatchKey(QStringLiteral("Solo"))));
  QVERIFY(DirectoryCache::instance().isDirectoryQueued(root.filePath(QStringLiteral("front"))));

  DirectoryCache::instance().processQueuedDirectories();
  const QHash<QString, QString> warm = ArtworkUtils::buildArtworkPathMap(tmp.path());
  QCOMPARE(warm.value(ArtworkUtils::baseMatchKey(QStringLiteral("Solo"))),
           root.filePath(QStringLiteral("front/Solo.png")));
}

// prewarmDirectories fills gaps and leaves an existing listing alone, so within
// one session a listing can be arbitrarily old. refreshDirectories is the
// re-derive: images added since must appear and images deleted since must go.
void TestDirectoryCache::refreshDirectories_replacesAListingCachedEarlier() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QDir root(tmp.path());
  writeFile(root.filePath(QStringLiteral("Stays.png")));
  writeFile(root.filePath(QStringLiteral("Goes.png")));

  const QStringList cascade = ArtworkUtils::artworkLookupDirectories(tmp.path());
  DirectoryCache::instance().prewarmDirectories(cascade);
  QCOMPARE(ArtworkUtils::findArtworkForFileCached(QStringLiteral("Goes.bin"), tmp.path()),
           root.filePath(QStringLiteral("Goes.png")));

  QVERIFY(QFile::remove(root.filePath(QStringLiteral("Goes.png"))));
  writeFile(root.filePath(QStringLiteral("Arrives.png")));
  writeFile(root.filePath(QStringLiteral("Recital (Disc 1).png")));

  // Warming is a no-op on an already-cached directory: the stale answers stand.
  DirectoryCache::instance().prewarmDirectories(cascade);
  QCOMPARE(ArtworkUtils::findArtworkForFileCached(QStringLiteral("Goes.bin"), tmp.path()),
           root.filePath(QStringLiteral("Goes.png")));

  const quint64 before = DirectoryCache::instance().contentsGeneration();
  DirectoryCache::instance().refreshDirectories(cascade);
  QVERIFY(DirectoryCache::instance().contentsGeneration() > before);

  const QHash<QString, QString> map = ArtworkUtils::buildArtworkPathMap(tmp.path());
  QCOMPARE(map.value(ArtworkUtils::baseMatchKey(QStringLiteral("Stays"))),
           root.filePath(QStringLiteral("Stays.png")));
  QCOMPARE(map.value(ArtworkUtils::baseMatchKey(QStringLiteral("Arrives"))),
           root.filePath(QStringLiteral("Arrives.png")));
  QVERIFY(!map.contains(ArtworkUtils::baseMatchKey(QStringLiteral("Goes"))));
  // The disc-marked half of the listing is rebuilt too, not just the exact one.
  QCOMPARE(map.value(ArtworkUtils::baseMatchKey(QStringLiteral("Recital"))),
           root.filePath(QStringLiteral("Recital (Disc 1).png")));
}

QTEST_GUILESS_MAIN(TestDirectoryCache)
#include "test_directorycache.moc"
