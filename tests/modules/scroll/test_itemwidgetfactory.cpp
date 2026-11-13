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
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

#include <memory>

#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "iartworkmanager.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "sessionmanager.h"

class TestItemWidgetFactory : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  // resolvePlaceholderArtwork policy
  void placeholder_ownCollectionValueWins();
  void placeholder_inheritedFromParentWhenOwnEmpty();
  void placeholder_contextFallbackWhenChainEmpty();
  void placeholder_expandsCollectionVariableWithCollectionName();
  void placeholder_contextFallbackExpandsWithContextName();
  void placeholder_danglingPathResolvesEmpty();
  void placeholder_directoryValueResolvesEmpty();
  void placeholder_allEmptyReturnsEmpty();

  // resolveSubcollectionTileArtwork precedence (Kartend-kb2vx)
  void subTile_collectionIconWins();
  void subTile_fallsBackToNamedImageInParentArtworkDir();
  void subTile_collectionIconWinsOverAPresentNamedImage();
  void subTile_whitespaceOnlyIconFallsThrough();
  void subTile_iconExpandsCollectionVariable();
  void subTile_emptyWhenNeitherSourceResolves();
  void subTile_guardsOutOfRangeAndMissingInputs();

  // Hand-linked cover precedence on the grid tile (Kartend-1js9j)
  void manualCover_paintsTileForAnItemNoNameLookupCanAnswer();
  void manualCover_outranksAnAutoDiscoveredCover();
  void manualCover_outranksTheSessionArtworkCache();
  void manualCover_emptyMapLeavesAutoDiscoveryUntouched();
  void manualCover_unlinkedItemStillFallsThroughToTheNameCascade();

  // Hand-linked covers in LIST MODE (Kartend-ni68u)
  void manualCover_listRowFlagsArtworkInsteadOfQueueingAPixmap();
  void manualCover_listRowCountsWithoutAnArtworkDirectory();
  void manualCover_unlinkedListRowStillFallsThroughToTheNameCascade();

  // Which artwork directory is authoritative (Kartend-12kzb)
  void spansCollections_falseForAPlainSingleCollectionView();
  void spansCollections_trueWhenTheConfigAggregatesChildren();
  void spansCollections_trueForBothWidenedSearchScopes();

  // Cached-only cover lookup in a large aggregated view (Kartend-birii)
  void aggregatedView_coldCascadeRequestsTheScanItWaitsOn();
  void aggregatedView_settledMissRequestsNothing();

  // List rows built through createMediaWidget over a real DatabaseManager
  // (Kartend-5msks, recovery coverage only)
  void listRow_realDb_warmCacheRowHasPreview();
  void listRow_realDb_coldRowRecoversAfterDrainAndReconfigure();
  void listRow_realDb_warmRebuildAndSettledMissStayQuiet();

  // Pending range-request bookkeeping
  void prefetchRangeAt_requestsUnloadedChunkOnce();
  void prefetchRangeAt_guardsInvalidAndLoadedInputs();
  void emptyRangeResponse_allowsBoundedReRequests();
  void clearPendingRangeRequest_resetsRetryBudget();

private:
  // Private HOME so the DatabaseManager the list-row cases construct opens its
  // media.db in a test-mode QStandardPaths tree that is deleted with the suite.
  QTemporaryDir m_homeDir;
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

/// Creates a FILE named @p name under @p root and returns its absolute path.
/// Placeholder policy inputs must be files, not directories: the resolver
/// validates with QFileInfo::isFile (Kartend-80h8o — the old QDir::exists
/// gate was true only for directories, which is why these tests passing
/// directories never caught real image files resolving to empty).
QString makeImageFile(QTemporaryDir &root, const QString &name) {
  const QString path = QDir(root.path()).absoluteFilePath(name);
  writeImage(path);
  return path;
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

/// Records what configureArtworkForWidget decided to paint. The artwork
/// pipeline itself is out of scope here — the question is which PATH the
/// factory hands it, which is the whole of the Kartend-1js9j precedence fix.
class RecordingArtworkManager : public IArtworkManager {
public:
  void cancelAllArtworkLoading() override {}
  void clearWidgetReferences() override {}
  void clearPendingArtworkForWidget(ItemWidget * /*widget*/) override {}
  void clearLoadedArtworkState() override {}
  void updateViewportArtwork() override {}
  void scheduleViewportUpdate() override {}
  void stopSilentLoading() override {}
  void startEarlyDentryPrewarm(int /*collectionIndex*/) override {}
  void addPendingArtwork(ItemWidget * /*widget*/, const QString &artworkPath) override {
    lastPath = artworkPath;
    ++calls;
  }
  void cycleArtworkType(ItemWidget * /*w*/, const QString & /*p*/, int /*i*/) override {}
  [[nodiscard]] QString artworkTypeOverrideFor(const QString & /*fullPath*/) const override {
    return typeOverride;
  }
  [[nodiscard]] bool hasArtworkForWidget(ItemWidget * /*widget*/) const override { return false; }
  [[nodiscard]] TimerUtils::Coordinator *getTimerCoordinator() const override { return nullptr; }
  void updateUserActivity() override {}

  QString lastPath;
  QString typeOverride;
  int calls = 0;
};

/// Factory wired far enough to run configureArtworkForWidget: a context with
/// an artwork directory, a real (unparented) ItemWidget to configure, and the
/// recording artwork manager reachable through the ApplicationContext.
struct ArtworkHarness {
  explicit ArtworkHarness(const QString &artworkDir, const QString &mediaDir) {
    ctx.managers.seedArtworkRoles(&artwork);
    context.config.artworkDirectory = artworkDir;
    context.config.mediaDirectory = mediaDir;
    factory.setApplicationContext(&ctx);
    factory.setCollectionContext(context);
  }
  ApplicationContext ctx;
  RecordingArtworkManager artwork;
  CollectionContext context;
  ItemWidgetFactory factory;
  ItemWidget widget;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// resolvePlaceholderArtwork policy
// ─────────────────────────────────────────────────────────────────────────────

void TestItemWidgetFactory::placeholder_ownCollectionValueWins() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  // Real image FILES — the exact shape the feature ships (Kartend-80h8o):
  // until that fix an existing file resolved to empty and the configured
  // placeholder silently never rendered anywhere.
  const QString own = makeImageFile(root, QStringLiteral("own.png"));
  const QString fallback = makeImageFile(root, QStringLiteral("fallback.png"));

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
  const QString parentPlaceholder = makeImageFile(root, QStringLiteral("parent.png"));

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
  const QString fallback = makeImageFile(root, QStringLiteral("fallback.png"));

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA"); // no placeholder anywhere

  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 0, fallback,
                                                               QStringLiteral("Ctx")),
           fallback);
}

void TestItemWidgetFactory::placeholder_expandsCollectionVariableWithCollectionName() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString expanded = makeImageFile(root, QStringLiteral("CollA.png"));

  // %collection% must expand with the INDEXED collection's name — not the
  // context's — when the index is valid.
  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA");
  collections[0].placeholderArtwork =
      QDir(root.path()).absoluteFilePath(QStringLiteral("%collection%.png"));

  QCOMPARE(ItemWidgetFactoryHelpers::resolvePlaceholderArtwork(&collections, 0, QString(),
                                                               QStringLiteral("Ctx")),
           expanded);
}

void TestItemWidgetFactory::placeholder_contextFallbackExpandsWithContextName() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString expanded = makeImageFile(root, QStringLiteral("CtxColl.png"));

  // Invalid index → the context placeholder is used AND its variables
  // expand with the context collection's name.
  const QString contextPlaceholder =
      QDir(root.path()).absoluteFilePath(QStringLiteral("%collection%.png"));
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

void TestItemWidgetFactory::placeholder_directoryValueResolvesEmpty() {
  QTemporaryDir root;
  QVERIFY(root.isValid());

  // A directory is as unusable as a missing file for the QPixmap load this
  // value feeds — it must resolve empty, not propagate (Kartend-80h8o).
  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("CollA");
  collections[0].placeholderArtwork = makeDir(root, QStringLiteral("a-directory"));

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

void TestItemWidgetFactory::subTile_iconExpandsCollectionVariable() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString icon = QDir(root.path()).absoluteFilePath(QStringLiteral("Documentaries.png"));
  writeImage(icon);

  QList<CollectionConfig> collections(1);
  collections[0].name = QStringLiteral("Documentaries");
  // A hand-edited INI or an imported .kart manifest can carry variables the
  // Browse button never produces; the tile must resolve them like Cover Flow
  // and the marquee now do (Kartend-dkh90).
  collections[0].collectionIcon =
      QDir(root.path()).absoluteFilePath(QStringLiteral("%collection%.png"));

  QCOMPARE(ItemWidgetFactoryHelpers::resolveSubcollectionTileArtwork(
               &collections, 0, QStringLiteral("Documentaries"), QString()),
           icon);
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
// Hand-linked cover precedence on the grid tile (Kartend-1js9j)
//
// configureArtworkForWidget used to resolve a cover from three name-based
// sources only — the session artwork cache, the shift+middle-click type
// override, and the ArtworkUtils name cascade — so a cover the user had linked
// by hand rendered in the sidebar gallery while the tile painted a placeholder.
// These cases pin the precedence: the link wins, and everything else is
// untouched when no link exists.
// ─────────────────────────────────────────────────────────────────────────────

void TestItemWidgetFactory::manualCover_paintsTileForAnItemNoNameLookupCanAnswer() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QTemporaryDir elsewhere;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid() && elsewhere.isValid());
  // Named nothing like the item and stored outside the artwork directory —
  // the shape the name cascade can never resolve, and the one the issue is
  // about.
  const QString linked = makeImageFile(elsewhere, QStringLiteral("hand-picked.png"));

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  h.factory.setManualCoverPaths({{item, linked}});

  h.factory.configureArtworkForWidget(&h.widget, item);
  QCOMPARE(h.artwork.calls, 1);
  QCOMPARE(h.artwork.lastPath, linked);
}

void TestItemWidgetFactory::manualCover_outranksAnAutoDiscoveredCover() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QTemporaryDir elsewhere;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid() && elsewhere.isValid());
  // Auto-discovery WOULD answer for this item: an image named after it, in
  // the artwork directory.
  makeImageFile(artRoot, QStringLiteral("Overture.png"));
  const QString linked = makeImageFile(elsewhere, QStringLiteral("hand-picked.png"));
  ArtworkUtils::clearDirectoryCache();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artRoot.path()});

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  h.factory.setManualCoverPaths({{item, linked}});

  h.factory.configureArtworkForWidget(&h.widget, item);
  // Manual beats auto — the precedence rule the rest of Kartend already obeys.
  QCOMPARE(h.artwork.lastPath, linked);
  ArtworkUtils::clearDirectoryCache();
}

void TestItemWidgetFactory::manualCover_outranksTheSessionArtworkCache() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QTemporaryDir elsewhere;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid() && elsewhere.isValid());
  const QString cached = makeImageFile(artRoot, QStringLiteral("Overture.png"));
  const QString linked = makeImageFile(elsewhere, QStringLiteral("hand-picked.png"));

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  // The startup viewport cache is name-based auto-discovery that happens to be
  // precomputed, so it must not shadow a link either — it is consulted first
  // in the function, which is why the manual check has to precede it.
  h.factory.setCachedArtworkPaths({{item, cached}});
  h.factory.setManualCoverPaths({{item, linked}});

  h.factory.configureArtworkForWidget(&h.widget, item);
  QCOMPARE(h.artwork.lastPath, linked);
}

void TestItemWidgetFactory::manualCover_emptyMapLeavesAutoDiscoveryUntouched() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid());
  const QString autoArt = makeImageFile(artRoot, QStringLiteral("Overture.png"));
  ArtworkUtils::clearDirectoryCache();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artRoot.path()});

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  // No links anywhere — the overwhelmingly common library. The name cascade
  // must still be what answers.
  h.factory.configureArtworkForWidget(&h.widget, item);
  QCOMPARE(h.artwork.lastPath, autoArt);
  ArtworkUtils::clearDirectoryCache();
}

void TestItemWidgetFactory::manualCover_unlinkedItemStillFallsThroughToTheNameCascade() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QTemporaryDir elsewhere;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid() && elsewhere.isValid());
  const QString autoArt = makeImageFile(artRoot, QStringLiteral("Nocturne.png"));
  const QString linked = makeImageFile(elsewhere, QStringLiteral("hand-picked.png"));
  ArtworkUtils::clearDirectoryCache();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artRoot.path()});

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  const QString linkedItem =
      QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  const QString otherItem =
      QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Nocturne.flac"));
  // A non-empty map is per ITEM, not per collection: a sibling with no row of
  // its own must not inherit the link, and must not lose its own auto cover.
  h.factory.setManualCoverPaths({{linkedItem, linked}});

  h.factory.configureArtworkForWidget(&h.widget, otherItem);
  QCOMPARE(h.artwork.lastPath, autoArt);
  ArtworkUtils::clearDirectoryCache();
}

// ─────────────────────────────────────────────────────────────────────────────
// Hand-linked covers in LIST MODE (Kartend-ni68u)
//
// A list row shows a preview BUTTON, not the cover, so the link has to set the
// hasArtwork flag rather than queue a pixmap into the hidden image label. List
// mode was excluded from the manual-cover check entirely while the overlay
// behind that button still resolved by name; ScrollManager::showArtworkPreview
// now routes a linked item to its exact path, so the flag is honest.
// ─────────────────────────────────────────────────────────────────────────────

void TestItemWidgetFactory::manualCover_listRowFlagsArtworkInsteadOfQueueingAPixmap() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QTemporaryDir elsewhere;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid() && elsewhere.isValid());
  // Same shape as the grid case: named nothing like the item, stored outside
  // the artwork directory, so only the link can answer.
  const QString linked = makeImageFile(elsewhere, QStringLiteral("hand-picked.png"));

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  h.widget.setListMode(true);
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  h.factory.setManualCoverPaths({{item, linked}});

  h.factory.configureArtworkForWidget(&h.widget, item);
  QVERIFY2(h.widget.hasArtwork(), "a hand-linked cover must give the list row its preview button");
  QCOMPARE(h.artwork.calls, 0); // the flag, never a decode into a hidden label
}

void TestItemWidgetFactory::manualCover_listRowCountsWithoutAnArtworkDirectory() {
  QTemporaryDir mediaRoot;
  QTemporaryDir elsewhere;
  QVERIFY(mediaRoot.isValid() && elsewhere.isValid());
  const QString linked = makeImageFile(elsewhere, QStringLiteral("hand-picked.png"));

  // No artwork directory at all — the case the name cascade cannot even try,
  // and the reason the link is checked before that guard rather than inside it.
  ArtworkHarness h(QString(), mediaRoot.path());
  h.widget.setListMode(true);
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  h.factory.setManualCoverPaths({{item, linked}});

  h.factory.configureArtworkForWidget(&h.widget, item);
  QVERIFY2(h.widget.hasArtwork(), "a link must count even with no artwork directory configured");
  QCOMPARE(h.artwork.calls, 0);
}

void TestItemWidgetFactory::manualCover_unlinkedListRowStillFallsThroughToTheNameCascade() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QTemporaryDir elsewhere;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid() && elsewhere.isValid());
  makeImageFile(artRoot, QStringLiteral("Nocturne.png"));
  const QString linked = makeImageFile(elsewhere, QStringLiteral("hand-picked.png"));
  ArtworkUtils::clearDirectoryCache();
  ArtworkUtils::DirectoryCache::instance().prewarmDirectories({artRoot.path()});

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  h.widget.setListMode(true);
  const QString linkedItem =
      QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));
  const QString otherItem =
      QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Nocturne.flac"));
  // A sibling with no row of its own must neither inherit the link nor lose
  // the auto-discovered cover it already had before this change.
  h.factory.setManualCoverPaths({{linkedItem, linked}});

  h.factory.configureArtworkForWidget(&h.widget, otherItem);
  QVERIFY2(h.widget.hasArtwork(), "an unlinked list row must still find its auto-discovered cover");
  ArtworkUtils::clearDirectoryCache();
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

// ── Which artwork directory is authoritative (Kartend-12kzb) ──────────────
//
// The per-item artwork-directory lookup is a DB hit per widget, so it only
// runs when the view can actually contain items from another collection.
// Getting that predicate wrong is invisible in the common case and produces
// hatched placeholders in exactly the widened-search case, so pin all four
// inputs rather than just the one that was broken.

void TestItemWidgetFactory::spansCollections_falseForAPlainSingleCollectionView() {
  // Ordinary browsing: every item belongs to the collection being viewed, so
  // its artworkDirectory is authoritative and the lookup is pure cost.
  CollectionContext context;
  QVERIFY(!ItemWidgetFactoryHelpers::viewMaySpanCollections(context));
}

void TestItemWidgetFactory::spansCollections_trueWhenTheConfigAggregatesChildren() {
  // The one case that always worked: showAllSubcollectionItems pulls children
  // into the parent's grid.
  CollectionContext context;
  context.config.showAllSubcollectionItems = true;
  QVERIFY(ItemWidgetFactoryHelpers::viewMaySpanCollections(context));
}

void TestItemWidgetFactory::spansCollections_trueForBothWidenedSearchScopes() {
  // The regression. A search widens which items are QUERIED without touching
  // the collection config, so the config flag above stays false while the grid
  // fills with other collections' items. Both widened scopes must count.
  {
    CollectionContext currentAndSubcollections;
    currentAndSubcollections.queryIncludeDescendants = true;
    QVERIFY2(ItemWidgetFactoryHelpers::viewMaySpanCollections(currentAndSubcollections),
             "A current+subcollections search surfaces children's items; their covers do not "
             "live in the parent's artwork directory");
  }
  {
    CollectionContext allCollections;
    allCollections.queryIncludeAllCollections = true;
    QVERIFY2(ItemWidgetFactoryHelpers::viewMaySpanCollections(allCollections),
             "An all-collections search spans the whole library");
  }
}

// ── Cached-only cover lookup in a large aggregated view (Kartend-birii) ───
//
// A showAllSubcollectionItems view over 1000+ items resolves covers from the
// directory cache only, so a cold cascade cannot answer. It used to say nothing
// and wait for ScrollManager::receiveItemsRange's prewarm, which only runs when
// a DB chunk lands: a cache cleared after the chunks around the viewport had
// landed left every tile built afterwards a placeholder until F5.

namespace {

/// The view the report was filed against: a parent aggregating its children's
/// items, large enough to take the cached-only lookup.
void makeLargeAggregatedView(ArtworkHarness &h) {
  h.context.config.showAllSubcollectionItems = true;
  h.factory.setCollectionContext(h.context);
  h.factory.setTotalItemCount(52410);
}

} // namespace

void TestItemWidgetFactory::aggregatedView_coldCascadeRequestsTheScanItWaitsOn() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid());
  makeDir(artRoot, QStringLiteral("front"));
  const QString cover = makeImageFile(artRoot, QStringLiteral("front/Overture.png"));
  // What the launcher-store enrichment did a few seconds into each start.
  ArtworkUtils::clearDirectoryCache();

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  makeLargeAggregatedView(h);
  QSignalSpy prewarm{&h.factory, &ItemWidgetFactory::requestArtworkPrewarm};
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Overture.flac"));

  h.factory.configureArtworkForWidget(&h.widget, item);
  QCOMPARE(h.artwork.calls, 0); // the cold cache cannot answer...
  QCOMPARE(prewarm.count(), 1); // ...so the tile asks for the scan it is waiting on
  QCOMPARE(prewarm.at(0).at(0).toString(), artRoot.path());

  // The scan ScrollManager::schedulePrewarmAndReconfigure runs before it
  // reconfigures: afterwards the same lookup finds the cover.
  auto &cache = ArtworkUtils::DirectoryCache::instance();
  cache.prewarmDirectories({artRoot.path()});
  cache.processQueuedDirectories();
  h.factory.configureArtworkForWidget(&h.widget, item);
  QCOMPARE(h.artwork.lastPath, cover);
  QCOMPARE(prewarm.count(), 1);
  ArtworkUtils::clearDirectoryCache();
}

void TestItemWidgetFactory::aggregatedView_settledMissRequestsNothing() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid());
  ArtworkUtils::clearDirectoryCache();

  ArtworkHarness h(artRoot.path(), mediaRoot.path());
  makeLargeAggregatedView(h);
  QSignalSpy prewarm{&h.factory, &ItemWidgetFactory::requestArtworkPrewarm};
  const QString item = QDir(mediaRoot.path()).absoluteFilePath(QStringLiteral("Silence.flac"));

  // A cold miss walks the whole cascade, queueing every directory it consults;
  // draining the queue caches them all, the subdirs that do not exist included.
  h.factory.configureArtworkForWidget(&h.widget, item);
  QCOMPARE(prewarm.count(), 1);
  ArtworkUtils::DirectoryCache::instance().processQueuedDirectories();

  // Settled now, so a miss is a real answer. An artless tile is rebuilt on
  // every scroll past it, and each request costs a scan plus a reconfigure of
  // every visible tile, so it must not ask again.
  h.factory.configureArtworkForWidget(&h.widget, item);
  QCOMPARE(h.artwork.calls, 0);
  QCOMPARE(prewarm.count(), 1);
  ArtworkUtils::clearDirectoryCache();
}

// ── List rows over a real database (Kartend-5msks) ────────────────────────
//
// createMediaWidget's list branch resolves each row's path through the
// IDatabaseManager role, so these cases build rows through a production
// DatabaseManager whose media.db lives on disk under the suite's private HOME
// (QStandardPaths test mode), with synthetic media and covers in QTemporaryDirs.
//
// RECOVERY coverage only. Everything runs inline on the GUI thread: the cache
// is drained with direct prewarmDirectories/processQueuedDirectories calls and
// the reconfigure is the factory call VirtualScrollEngine makes per active
// widget. ScrollManager's prewarm debounce timer, the thread-pool handoff and
// the active-widget iteration are NOT exercised. The cold row's own
// requestArtworkPrewarm is asserted only as an emitted signal; no slot is
// connected, so nothing here shows who services it or a live list recovering.

void TestItemWidgetFactory::initTestCase() {
  QVERIFY(m_homeDir.isValid());
  qputenv("HOME", m_homeDir.path().toUtf8());
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-itemwidgetfactory"));
}

namespace {

/// A list-mode factory wired the way ScrollManager wires it, with the
/// production DatabaseManager seeded into the database roles.
struct ListRowHarness {
  ListRowHarness(const QString &artworkDir, const QString &mediaDir, const QStringList &items) {
    ctx.managers.sessionManager = &session;
    db = std::make_unique<DatabaseManager>(&ctx);
    ctx.managers.seedDatabaseRoles(db.get());
    ctx.managers.seedArtworkRoles(&artwork);

    collections.resize(1);
    collections[0].name = QStringLiteral("Music");
    collections[0].mediaDirectory = mediaDir;
    collections[0].artworkDirectory = artworkDir;
    collections[0].viewType = ViewType::List;
    context.config.name = collections[0].name;
    context.config.mediaDirectory = mediaDir;
    context.config.artworkDirectory = artworkDir;
    context.config.viewType = ViewType::List;
    context.currentIndex = 0;
    for (const QString &item : items) {
      filePaths.append(QDir(mediaDir).absoluteFilePath(item));
    }

    factory.setApplicationContext(&ctx);
    factory.setParentWidget(&parent);
    factory.setCollections(&collections);
    factory.setCollectionContext(context);
    factory.setFileData(&filePaths, &fileNames);
    factory.setTotalItemCount(static_cast<int>(filePaths.size()));
  }

  ItemWidget *createRow(int mediaIndex) {
    int collectionIndex = -1;
    return factory.createMediaWidget(mediaIndex, collectionIndex);
  }

  SessionManager session;
  ApplicationContext ctx;
  RecordingArtworkManager artwork;
  std::unique_ptr<DatabaseManager> db;
  QList<CollectionConfig> collections;
  CollectionContext context;
  QStringList filePaths;
  QHash<QString, QString> fileNames;
  QWidget parent; // owns every row the factory creates
  ItemWidgetFactory factory;
};

/// Fails the calling test unless @p h runs on a real, migrated, on-disk
/// SQLite file — a setup failure must fail, never skip.
void verifyRealDatabase(const ListRowHarness &h, const QString &homeDir) {
  QVERIFY(h.ctx.databaseManager() == h.db.get());
  QSqlDatabase conn = QSqlDatabase::database(h.db->connectionName(), /*open=*/false);
  QVERIFY2(conn.isValid() && conn.isOpen(), "the production DatabaseManager must open media.db");
  const QFileInfo file(conn.databaseName());
  QVERIFY2(file.isFile(), qPrintable(conn.databaseName()));
#ifndef Q_OS_WIN
  // HOME steers the test-mode tree on Unix; Windows keeps Qt's own qttest root.
  QVERIFY2(file.canonicalFilePath().startsWith(QFileInfo(homeDir).canonicalFilePath()),
           qPrintable(file.canonicalFilePath()));
#else
  Q_UNUSED(homeDir);
#endif
  QSqlQuery schemaProbe(conn);
  QVERIFY2(schemaProbe.exec(QStringLiteral("SELECT COUNT(*) FROM items")),
           "the production schema bootstrap must have created the items table");
}

/// Warms every directory the cover cascade consults, the way the
/// post-prewarm drain leaves it, and checks the lookup has settled.
void warmCascade(const QString &artworkDir) {
  auto &cache = ArtworkUtils::DirectoryCache::instance();
  const QStringList cascade = ArtworkUtils::artworkLookupDirectories(artworkDir);
  cache.prewarmDirectories(cascade);
  cache.processQueuedDirectories();
  QVERIFY(cache.areDirectoriesCached(cascade));
  QVERIFY(!cache.isDirectoryQueued(artworkDir));
}

} // namespace

void TestItemWidgetFactory::listRow_realDb_warmCacheRowHasPreview() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid());
  makeDir(artRoot, QStringLiteral("front"));
  makeImageFile(artRoot, QStringLiteral("front/Overture.png"));
  ArtworkUtils::clearDirectoryCache();
  warmCascade(artRoot.path());
  if (QTest::currentTestFailed()) return;

  ListRowHarness h(artRoot.path(), mediaRoot.path(), {QStringLiteral("Overture.flac")});
  verifyRealDatabase(h, m_homeDir.path());
  if (QTest::currentTestFailed()) return;

  ItemWidget *row = h.createRow(0);
  QVERIFY(row);
  QVERIFY(row->isListMode());
  QCOMPARE(row->getFilePath(), h.filePaths.at(0));
  QVERIFY2(row->hasArtwork(), "a warm cascade with a cover must give the row its preview button");
  QCOMPARE(h.artwork.calls, 0); // list rows take the flag, never a pixmap decode
  ArtworkUtils::clearDirectoryCache();
}

void TestItemWidgetFactory::listRow_realDb_coldRowRecoversAfterDrainAndReconfigure() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid());
  makeDir(artRoot, QStringLiteral("front"));
  makeImageFile(artRoot, QStringLiteral("front/Overture.png"));
  makeImageFile(artRoot, QStringLiteral("front/Nocturne.png"));
  ArtworkUtils::clearDirectoryCache();
  warmCascade(artRoot.path());
  if (QTest::currentTestFailed()) return;

  ListRowHarness h(artRoot.path(), mediaRoot.path(),
                   {QStringLiteral("Overture.flac"), QStringLiteral("Nocturne.flac")});
  verifyRealDatabase(h, m_homeDir.path());
  if (QTest::currentTestFailed()) return;
  ItemWidget *warmRow = h.createRow(0);
  QVERIFY(warmRow);
  QVERIFY(warmRow->hasArtwork());

  // A mid-session clear (scrape, launcher import, store enrichment), then a
  // row scrolled into view afterwards.
  ArtworkUtils::clearDirectoryCache();
  auto &cache = ArtworkUtils::DirectoryCache::instance();
  QSignalSpy prewarm{&h.factory, &ItemWidgetFactory::requestArtworkPrewarm};
  ItemWidget *coldRow = h.createRow(1);
  QVERIFY(coldRow);
  QVERIFY(coldRow->isListMode());

  // The cached probe queued the cascade it could not answer — including the
  // root the sync-fallback guard checks — so the row starts without its flag.
  const QStringList cascade = ArtworkUtils::artworkLookupDirectories(artRoot.path());
  QVERIFY(cache.isDirectoryQueued(cascade.at(0)));
  QVERIFY(cache.isDirectoryQueued(artRoot.path()));
  QVERIFY2(!coldRow->hasArtwork(),
           "a cold row has no artwork until the queued directories are scanned");
  // The empty, unsettled lookup asks for the scan of its artwork root, once.
  QCOMPARE(prewarm.count(), 1);
  QCOMPARE(prewarm.at(0).at(0).toString(), artRoot.path());

  // The drain ScrollManager's prewarm runs, then the factory call
  // VirtualScrollEngine::reconfigureArtworkForActiveWidgets makes per widget.
  warmCascade(artRoot.path());
  if (QTest::currentTestFailed()) return;
  h.factory.configureArtworkForWidget(coldRow, coldRow->getFilePath(),
                                      /*forceDirectLookup=*/true);
  QVERIFY2(coldRow->hasArtwork(), "the reconfigured row must recover its preview button");
  QCOMPARE(h.artwork.calls, 0);

  // A later non-forced pass over the settled cache agrees.
  h.factory.configureArtworkForWidget(coldRow, coldRow->getFilePath());
  QVERIFY(coldRow->hasArtwork());
  QCOMPARE(prewarm.count(), 1); // the settled cascade asks for nothing more
  ArtworkUtils::clearDirectoryCache();
}

void TestItemWidgetFactory::listRow_realDb_warmRebuildAndSettledMissStayQuiet() {
  QTemporaryDir artRoot;
  QTemporaryDir mediaRoot;
  QVERIFY(artRoot.isValid() && mediaRoot.isValid());
  makeDir(artRoot, QStringLiteral("front"));
  makeImageFile(artRoot, QStringLiteral("front/Overture.png"));
  ArtworkUtils::clearDirectoryCache();
  warmCascade(artRoot.path());
  if (QTest::currentTestFailed()) return;

  ListRowHarness h(artRoot.path(), mediaRoot.path(),
                   {QStringLiteral("Overture.flac"), QStringLiteral("Silence.flac")});
  verifyRealDatabase(h, m_homeDir.path());
  if (QTest::currentTestFailed()) return;
  auto &cache = ArtworkUtils::DirectoryCache::instance();
  const QStringList cascade = ArtworkUtils::artworkLookupDirectories(artRoot.path());
  QSignalSpy prewarm{&h.factory, &ItemWidgetFactory::requestArtworkPrewarm};

  // Rows are rebuilt on every scroll past them; each rebuild must give the
  // same answer, and the artless one is a settled miss, not pending work.
  for (int pass = 0; pass < 3; ++pass) {
    ItemWidget *withCover = h.createRow(0);
    ItemWidget *artless = h.createRow(1);
    QVERIFY(withCover && artless);
    QVERIFY2(withCover->hasArtwork(), "a warm rebuild must keep the preview button");
    QVERIFY2(!artless->hasArtwork(), "a settled miss must not invent a preview button");
    h.factory.configureArtworkForWidget(artless, artless->getFilePath());
    QVERIFY(!artless->hasArtwork());
  }
  QCOMPARE(prewarm.count(), 0); // a settled cascade needs no scan
  QVERIFY(cache.areDirectoriesCached(cascade));
  QVERIFY(!cache.isDirectoryQueued(artRoot.path()));
  QCOMPARE(h.artwork.calls, 0);
  ArtworkUtils::clearDirectoryCache();
}

QTEST_MAIN(TestItemWidgetFactory)
#include "test_itemwidgetfactory.moc"
