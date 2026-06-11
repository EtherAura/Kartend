// Regression tests for Kartend-ardm7: FileMapCache::resolveRelativeFilePath
// used to fall back to a linear endsWith scan over the caller's fileNames map
// for every entry the m_relativeToFullPath index missed. Callers run it per
// unresolved item (widget creation, subcollection filtering), so the scan made
// those passes O(items^2) — multi-second GUI-thread freezes on large
// flattened views. The scan is gone; resolution now answers from:
//   1. an exact key hit in the caller's map (passthrough),
//   2. the reverse index built by replaceFromItemsLoaded (full path, leaf
//      name, media-dir-relative path — every form the scan realistically
//      matched),
//   3. the collection-index alias maps (absolutize via the file's media dir),
// and otherwise logs and returns empty (the documented log-and-skip choice).

#include <QHash>
#include <QString>
#include <QTest>

#include "filemapcache.h"

class TestFileMapCache : public QObject {
  Q_OBJECT
private slots:
  void emptyInputReturnsEmpty();
  void exactKeyPassesThrough();
  void indexResolvesFullLeafAndRelativeForms();
  void firstSeenLeafWinsOnCollision();
  void unindexedSuffixReturnsEmpty();
  void collectionIndexFallbackAbsolutizes();

private:
  // Populates @p cache with two items under distinct media dirs, one of them
  // in a subfolder so the media-dir-relative index form differs from the
  // leaf name.
  static void populate(FileMapCache &cache);
};

void TestFileMapCache::populate(FileMapCache &cache) {
  QHash<QString, QString> fileToMediaDir;
  fileToMediaDir.insert(QStringLiteral("/media/alpha/sub/foo.bin"), QStringLiteral("/media/alpha"));
  fileToMediaDir.insert(QStringLiteral("/media/beta/bar.bin"), QStringLiteral("/media/beta"));

  QHash<QString, int> fileToCollectionIndex;
  fileToCollectionIndex.insert(QStringLiteral("/media/alpha/sub/foo.bin"), 0);
  fileToCollectionIndex.insert(QStringLiteral("/media/beta/bar.bin"), 1);

  QHash<QString, QString> filteredNames;
  filteredNames.insert(QStringLiteral("/media/alpha/sub/foo.bin"), QStringLiteral("foo"));
  filteredNames.insert(QStringLiteral("/media/beta/bar.bin"), QStringLiteral("bar"));

  cache.replaceFromItemsLoaded({}, fileToMediaDir, fileToCollectionIndex, filteredNames);
}

void TestFileMapCache::emptyInputReturnsEmpty() {
  FileMapCache cache;
  populate(cache);
  QCOMPARE(cache.resolveRelativeFilePath(QString(), {}), QString());
  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("   "), {}), QString());
}

void TestFileMapCache::exactKeyPassesThrough() {
  FileMapCache cache; // deliberately UNPOPULATED: passthrough needs no index
  QHash<QString, QString> fileNames;
  fileNames.insert(QStringLiteral("/media/beta/bar.bin"), QStringLiteral("bar"));
  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("/media/beta/bar.bin"), fileNames),
           QStringLiteral("/media/beta/bar.bin"));
}

void TestFileMapCache::indexResolvesFullLeafAndRelativeForms() {
  FileMapCache cache;
  populate(cache);

  // Full path, leaf name, and media-dir-relative path all resolve from the
  // reverse index, with an EMPTY caller map — proof no caller-map scan is
  // involved.
  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("/media/alpha/sub/foo.bin"), {}),
           QStringLiteral("/media/alpha/sub/foo.bin"));
  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("foo.bin"), {}),
           QStringLiteral("/media/alpha/sub/foo.bin"));
  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("sub/foo.bin"), {}),
           QStringLiteral("/media/alpha/sub/foo.bin"));
  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("bar.bin"), {}),
           QStringLiteral("/media/beta/bar.bin"));
}

void TestFileMapCache::firstSeenLeafWinsOnCollision() {
  FileMapCache cache;
  QHash<QString, QString> fileToMediaDir;
  fileToMediaDir.insert(QStringLiteral("/media/alpha/dupe.bin"), QStringLiteral("/media/alpha"));
  fileToMediaDir.insert(QStringLiteral("/media/beta/dupe.bin"), QStringLiteral("/media/beta"));
  QHash<QString, QString> filteredNames;
  filteredNames.insert(QStringLiteral("/media/alpha/dupe.bin"), QStringLiteral("dupe"));
  filteredNames.insert(QStringLiteral("/media/beta/dupe.bin"), QStringLiteral("dupe"));
  cache.replaceFromItemsLoaded({}, fileToMediaDir, {}, filteredNames);

  // Ambiguous leaf: SOME owner resolves (first seen in build order, matching
  // the old scan's first-match semantics — which were hash-order dependent
  // too, so only "one of the owners" is contractual).
  const QString resolved = cache.resolveRelativeFilePath(QStringLiteral("dupe.bin"), {});
  QVERIFY(resolved == QStringLiteral("/media/alpha/dupe.bin") ||
          resolved == QStringLiteral("/media/beta/dupe.bin"));
}

void TestFileMapCache::unindexedSuffixReturnsEmpty() {
  FileMapCache cache;
  populate(cache);

  // Kartend-ardm7 contract change: a multi-segment suffix that is NOT the
  // media-dir-relative form (here it spans above /media/alpha) was only ever
  // resolvable by the deleted linear scan. It now logs and returns empty —
  // the accepted trade for removing the O(items^2) scan; no production
  // caller emits this shape.
  QHash<QString, QString> fileNames;
  fileNames.insert(QStringLiteral("/media/alpha/sub/foo.bin"), QStringLiteral("foo"));
  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("alpha/sub/foo.bin"), fileNames),
           QString());
}

void TestFileMapCache::collectionIndexFallbackAbsolutizes() {
  FileMapCache cache;
  // The alias maps (merged by range loads too) know this RELATIVE key even
  // though the reverse index was never built — the post-index fallback must
  // absolutize it against its media dir.
  QHash<QString, QString> fileToMediaDir;
  fileToMediaDir.insert(QStringLiteral("loose.bin"), QStringLiteral("/media/gamma"));
  QHash<QString, int> fileToCollectionIndex;
  fileToCollectionIndex.insert(QStringLiteral("loose.bin"), 2);
  cache.mergeRangeLoaded({}, fileToMediaDir, fileToCollectionIndex);

  QCOMPARE(cache.resolveRelativeFilePath(QStringLiteral("loose.bin"), {}),
           QStringLiteral("/media/gamma/loose.bin"));
}

QTEST_GUILESS_MAIN(TestFileMapCache)
#include "test_filemapcache.moc"
