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

#include "applicationcontext.h"
#include "artworkutils.h"
#include "iartworkmanager.h"
#include "itemwidget.h"
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

QTEST_MAIN(TestItemWidgetFactory)
#include "test_itemwidgetfactory.moc"
