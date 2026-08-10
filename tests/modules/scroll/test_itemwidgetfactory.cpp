// ItemWidgetFactory — the non-widget decision logic.
//
// The widget-construction paths (acquire/configure/release) stay covered by
// the integration suite; these cases pin down the two decision surfaces that
// need no widget graph:
//
//   * ItemWidgetFactoryHelpers::resolvePlaceholderArtwork — the pure
//     placeholder-artwork precedence policy (own value > parent chain >
//     context fallback, plus %collection% expansion and the exists-check
//     that empties dangling paths), extracted per the ScrollHelpers pattern.
//   * The pending range-request bookkeeping — a chunk of unloaded rows is
//     requested exactly once, an empty (zero-row) response re-arms a bounded
//     number of re-requests (Kartend-ejsf), and the clear entry points reset
//     the retry budget.
//
// Placeholder paths must exist to survive expansion's validation, so the
// policy cases build real directories under a QTemporaryDir.

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "itemwidgetfactory.h"

class TestItemWidgetFactory : public QObject {
  Q_OBJECT

private slots:
  // resolvePlaceholderArtwork policy
  void placeholder_ownCollectionValueWins();
  void placeholder_inheritedFromParentWhenOwnEmpty();
  void placeholder_contextFallbackWhenChainEmpty();
  void placeholder_expandsCollectionVariableWithCollectionName();
  void placeholder_contextFallbackExpandsWithContextName();
  void placeholder_danglingPathResolvesEmpty();
  void placeholder_allEmptyReturnsEmpty();

  // resolveSubcollectionTileArtwork precedence (Kartend-kb2vx)
  void subTile_collectionIconWins();
  void subTile_fallsBackToNamedImageInParentArtworkDir();
  void subTile_collectionIconWinsOverAPresentNamedImage();
  void subTile_whitespaceOnlyIconFallsThrough();
  void subTile_emptyWhenNeitherSourceResolves();
  void subTile_guardsOutOfRangeAndMissingInputs();

  // Pending range-request bookkeeping
  void prefetchRangeAt_requestsUnloadedChunkOnce();
  void prefetchRangeAt_guardsInvalidAndLoadedInputs();
  void emptyRangeResponse_allowsBoundedReRequests();
  void clearPendingRangeRequest_resetsRetryBudget();
};

namespace {

/// Creates a real subdirectory under @p root and returns its absolute path —
/// expansion validates existence, so policy inputs must point at something.
QString makeDir(QTemporaryDir &root, const QString &name) {
  const QString path = QDir(root.path()).absoluteFilePath(name);
  QDir().mkpath(path);
  return path;
}

/// Creates a file at @p path. findArtworkForFile matches on name and existence
/// only — it never decodes — so the bytes are irrelevant.
void writeImage(const QString &path) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("px");
}

/// Factory wired to @p filePaths / @p fileNames only — enough for the
/// range-request surface, which never touches widgets.
struct RangeHarness {
  ItemWidgetFactory factory;
  QStringList filePaths;
  QHash<QString, QString> fileNames;
  QSignalSpy spy{&factory, &ItemWidgetFactory::requestItemsRange};

  explicit RangeHarness(int unloadedCount) {
    for (int i = 0; i < unloadedCount; ++i) {
      filePaths.append(QString()); // empty entry == not yet loaded from DB
    }
    factory.setFileData(&filePaths, &fileNames);
  }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// resolvePlaceholderArtwork policy
// ─────────────────────────────────────────────────────────────────────────────

void TestItemWidgetFactory::placeholder_ownCollectionValueWins() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString own = makeDir(root, QStringLiteral("own"));
  const QString fallback = makeDir(root, QStringLiteral("fallback"));

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA");
  collections[0].placeholderArtwork = own;

  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 0, fallback,
                                                               QStringLiteral("Ctx")),
           own);
}

void TestItemWidgetFactory::placeholder_inheritedFromParentWhenOwnEmpty() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString parentPlaceholder = makeDir(root, QStringLiteral("parent"));

  // Child (index 1) has no placeholder of its own; the parent-chain walk
  // must surface the parent's value.
  QList<CollectionConfig> collections(2);
  collections[0].name = QStringLiteral("Parent");
  collections[0].placeholderArtwork = parentPlaceholder;
  collections[1].name = QStringLiteral("Child");
  collections[1].isSubcollection = true; // the inheritance walk only ascends for subcollections
  collections[1].parentCollectionIndex = 0;

  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 1, QString(),
                                                               QStringLiteral("Ctx")),
           parentPlaceholder);
}

void TestItemWidgetFactory::placeholder_contextFallbackWhenChainEmpty() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString fallback = makeDir(root, QStringLiteral("fallback"));

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA"); // no placeholder anywhere

  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 0, fallback,
                                                               QStringLiteral("Ctx")),
           fallback);
}

void TestItemWidgetFactory::placeholder_expandsCollectionVariableWithCollectionName() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString expanded = makeDir(root, QStringLiteral("CollA"));

  // %collection% must expand with the INDEXED collection's name — not the
  // context's — when the index is valid.
  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA");
  collections[0].placeholderArtwork =
      QDir(root.path()).absoluteFilePath(QStringLiteral("%collection%"));

  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 0, QString(),
                                                               QStringLiteral("Ctx")),
           expanded);
}

void TestItemWidgetFactory::placeholder_contextFallbackExpandsWithContextName() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString expanded = makeDir(root, QStringLiteral("CtxColl"));

  // Invalid index → the context placeholder is used AND its variables
  // expand with the context collection's name.
  const QString contextPlaceholder =
      QDir(root.path()).absoluteFilePath(QStringLiteral("%collection%"));
  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(nullptr, -1, contextPlaceholder,
                                                               QStringLiteral("CtxColl")),
           expanded);
}

void TestItemWidgetFactory::placeholder_danglingPathResolvesEmpty() {
  QTemporaryDir root;
  QVERIFY(root.isValid());

  // The expansion step validates existence: a configured placeholder that
  // points nowhere must resolve to empty (the widget then paints its
  // synthetic placeholder) rather than propagating a dead path.
  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA");
  collections[0].placeholderArtwork =
      QDir(root.path()).absoluteFilePath(QStringLiteral("does-not-exist"));

  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 0, QString(),
                                                               QStringLiteral("Ctx")),
           QString());
}

void TestItemWidgetFactory::placeholder_allEmptyReturnsEmpty() {
  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA");
  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 0, QString(),
                                                               QStringLiteral("Ctx")),
           QString());
  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(nullptr, 3, QString(), QString()),
           QString());
}

// ─────────────────────────────────────────────────────────────────────────────
// Subcollection tile artwork precedence (Kartend-kb2vx)
//
// Two sources: the child's own collectionIcon, then an image named after the
// child in the PARENT's artwork directory. Grid and List honoured only the
// second, so collectionIcon silently did nothing there while Cover Flow and
// the marquee obeyed it — the docs described only the first.
// ─────────────────────────────────────────────────────────────────────────────

void TestItemWidgetFactory::subTile_collectionIconWins() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString icon = QDir(root.path()).absoluteFilePath(QStringLiteral("shelf-icon.png"));
  writeImage(icon);

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("Documentaries");
  collections[0].collectionIcon = icon;

  // No parent artwork directory at all: the icon alone must still resolve,
  // which the old code could not do — its lookup was gated on that directory.
  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               &collections, 0, QStringLiteral("Documentaries"), QString()),
           icon);
}

void TestItemWidgetFactory::subTile_fallsBackToNamedImageInParentArtworkDir() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString parentArt = makeDir(root, QStringLiteral("parent-art"));
  const QString named = QDir(parentArt).absoluteFilePath(QStringLiteral("Documentaries.png"));
  writeImage(named);

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("Documentaries");
  // collectionIcon deliberately unset — the pre-existing convention still
  // has to work, so the fix adds a source rather than replacing one.

  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               &collections, 0, QStringLiteral("Documentaries"), parentArt),
           named);
}

void TestItemWidgetFactory::subTile_collectionIconWinsOverAPresentNamedImage() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString parentArt = makeDir(root, QStringLiteral("parent-art"));
  writeImage(QDir(parentArt).absoluteFilePath(QStringLiteral("Documentaries.png")));
  const QString icon = QDir(root.path()).absoluteFilePath(QStringLiteral("explicit.png"));
  writeImage(icon);

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("Documentaries");
  collections[0].collectionIcon = icon;

  // Both sources available: the explicit per-collection choice must win, or
  // setting collectionIcon on a collection that already follows the naming
  // convention would appear to do nothing.
  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               &collections, 0, QStringLiteral("Documentaries"), parentArt),
           icon);
}

void TestItemWidgetFactory::subTile_whitespaceOnlyIconFallsThrough() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString parentArt = makeDir(root, QStringLiteral("parent-art"));
  const QString named = QDir(parentArt).absoluteFilePath(QStringLiteral("Documentaries.png"));
  writeImage(named);

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("Documentaries");
  collections[0].collectionIcon = QStringLiteral("   "); // e.g. a hand-edited INI

  // Whitespace is not a path: it must not shadow the naming convention with
  // an unloadable value.
  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               &collections, 0, QStringLiteral("Documentaries"), parentArt),
           named);
}

void TestItemWidgetFactory::subTile_emptyWhenNeitherSourceResolves() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString parentArt = makeDir(root, QStringLiteral("parent-art"));

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("Documentaries");

  // Nothing to show — the caller then falls back to placeholder artwork.
  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               &collections, 0, QStringLiteral("Documentaries"), parentArt),
           QString());
}

void TestItemWidgetFactory::subTile_guardsOutOfRangeAndMissingInputs() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString parentArt = makeDir(root, QStringLiteral("parent-art"));
  writeImage(QDir(parentArt).absoluteFilePath(QStringLiteral("Documentaries.png")));

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("Documentaries");
  collections[0].collectionIcon =
      QDir(root.path()).absoluteFilePath(QStringLiteral("unreachable.png"));

  // A bad index must skip step 1 rather than read out of bounds — and still
  // let step 2 answer, since that only needs the name and the parent's dir.
  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               &collections, 7, QStringLiteral("Documentaries"), parentArt),
           QDir(parentArt).absoluteFilePath(QStringLiteral("Documentaries.png")));
  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               nullptr, 0, QStringLiteral("Documentaries"), parentArt),
           QDir(parentArt).absoluteFilePath(QStringLiteral("Documentaries.png")));
  // An empty name must not probe the directory for "".
  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(&collections, 7, QString(),
                                                                     parentArt),
           QString());
}

// ─────────────────────────────────────────────────────────────────────────────
// Pending range-request bookkeeping
// ─────────────────────────────────────────────────────────────────────────────

void TestItemWidgetFactory::prefetchRangeAt_requestsUnloadedChunkOnce() {
  // Exactly one chunk of unloaded rows: adjacent-chunk prefetch has nothing
  // ahead or behind, so the emission count is the chunk-0 decision alone.
  RangeHarness h(100);

  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 1);
  QCOMPARE(h.spy.at(0).at(0).toInt(), 0);
  QCOMPARE(h.spy.at(0).at(1).toInt(), 100);

  // The chunk is now pending — a second prefetch over the same chunk must
  // not spam the DB with a duplicate request.
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 1);
}

void TestItemWidgetFactory::prefetchRangeAt_guardsInvalidAndLoadedInputs() {
  RangeHarness h(100);

  // Out-of-range starts are dropped.
  h.factory.prefetchRangeAt(-1, 100);
  h.factory.prefetchRangeAt(100, 100);
  QCOMPARE(h.spy.count(), 0);

  // A chunk whose first row is already loaded needs no request.
  h.filePaths[0] = QStringLiteral("/media/loaded.bin");
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 0);

  // No file data wired at all: silently ignored.
  ItemWidgetFactory bare;
  QSignalSpy bareSpy(&bare, &ItemWidgetFactory::requestItemsRange);
  bare.prefetchRangeAt(0, 100);
  QCOMPARE(bareSpy.count(), 0);
}

void TestItemWidgetFactory::emptyRangeResponse_allowsBoundedReRequests() {
  RangeHarness h(100);
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 1);

  // First two empty responses re-arm the chunk (a transient count/filter
  // over-report resolves quickly)...
  h.factory.onEmptyRangeResponse(0);
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 2);

  h.factory.onEmptyRangeResponse(0);
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 3);

  // ...the third exhausts the budget: the chunk stays pending so a
  // persistently-empty chunk can't spin a tight request loop.
  h.factory.onEmptyRangeResponse(0);
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 3);

  // Further empty responses past exhaustion don't sneak the budget back.
  h.factory.onEmptyRangeResponse(0);
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 3);
}

void TestItemWidgetFactory::clearPendingRangeRequest_resetsRetryBudget() {
  RangeHarness h(100);
  h.factory.prefetchRangeAt(0, 100);
  for (int i = 0; i < 3; ++i) {
    h.factory.onEmptyRangeResponse(0);
    h.factory.prefetchRangeAt(0, 100);
  }
  QCOMPARE(h.spy.count(), 3); // budget exhausted (see sibling case)

  // The chunk finally filled (or the view moved on): the per-chunk clear
  // must drop the pending marker AND restore the full empty-response
  // allowance for a later transient emptiness.
  h.factory.clearPendingRangeRequest(0);
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 4);
  h.factory.onEmptyRangeResponse(0);
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 5);

  // The bulk clear behaves the same across every chunk.
  h.factory.clearPendingRangeRequests();
  h.factory.prefetchRangeAt(0, 100);
  QCOMPARE(h.spy.count(), 6);
}

QTEST_MAIN(TestItemWidgetFactory)
#include "test_itemwidgetfactory.moc"
