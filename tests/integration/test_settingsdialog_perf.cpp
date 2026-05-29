#include "test_settingsdialog_perf.h"

#include "collection/collectionconfig.h"
#include "settingsdialog.h"

#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>

namespace {

// Build the stress config Kartend-cnnq's Step 1 called for: many collections,
// each carrying a long custom-artwork-type list and fully-populated grid /
// list / background preferences. This is deliberately larger than any real
// user config (the prod profile that motivated the bug report had 18
// collections) so a future O(n^2) regression in the construction path —
// e.g. a per-collection rescan of the whole tree, or a re-layout per inserted
// row — shows up as a blown budget here rather than as a field bug report.
QList<CollectionConfig> stressConfig(int collectionCount, int artworkTypesPerCollection) {
  QStringList artworkTypes;
  artworkTypes.reserve(artworkTypesPerCollection);
  for (int i = 0; i < artworkTypesPerCollection; ++i) {
    artworkTypes << QStringLiteral("custom_artwork_type_%1").arg(i);
  }

  QList<CollectionConfig> collections;
  collections.reserve(collectionCount);
  for (int i = 0; i < collectionCount; ++i) {
    CollectionConfig c;
    c.name = QStringLiteral("Collection %1").arg(i);
    c.type = QStringLiteral("Video");
    c.mediaDirectory = QStringLiteral("/tmp/nonexistent/collection_%1").arg(i);
    c.customArtworkTypes = artworkTypes;
    c.gridLayout.gridWidth = 6;
    c.gridLayout.itemWidth = 220;
    c.gridLayout.itemHeight = 320;
    c.gridLayout.fontSize = 13;
    c.gridLayout.cornerRadius = 6;
    c.listView.listFontSize = 12;
    c.listView.listRowHeight = 40;
    c.background.vignetteIntensity = 30;
    c.background.parallaxStrength = 20;
    // Build deepish chains: every 10th collection starts a new root, the rest
    // parent to the row before them (depth up to ~9). TreeManager::populate()
    // places subcollections with a while-loop bounded by collection count that
    // re-scans every collection each pass — one pass per nesting level. Deep
    // chains force multiple passes, exercising the O(n*depth) path that a naive
    // regression could turn into O(n^2); a flat tree would converge in one pass
    // and never stress it.
    if (i % 10 != 0 && i > 0) {
      c.isSubcollection = true;
      c.parentCollectionIndex = i - 1;
    }
    collections << c;
  }
  return collections;
}

} // namespace

void TestSettingsDialogPerf::largeConfigOpensWithinBudget() {
  // 50 collections x 500 custom artwork types each — the exact stress profile
  // named in the bd's Step 1. Prior profiling on the real 18-collection config
  // measured ~100ms total (almost all in ui->setupUi(), a fixed cost
  // independent of config size); this synthetic profile clocks ~30ms warm in a
  // release build. The budget below is deliberately ~100x that warm number:
  // this is a shape guard, not a latency benchmark. A genuine config-dependent
  // regression (a per-row full-tree relayout, a per-collection DB round-trip,
  // an O(n^2) ancestor walk) blows up into multiple seconds and trips this;
  // the wide margin absorbs a loaded CI runner under ASan without flaking.
  const QList<CollectionConfig> collections = stressConfig(50, 500);

  QElapsedTimer timer;
  timer.start();
  SettingsDialog dialog(nullptr, collections, 0);
  const qint64 elapsedMs = timer.elapsed();

  // The dialog must have actually ingested the config (guards against a future
  // refactor that lazily defers population and makes the timer meaningless).
  QCOMPARE(dialog.getCollections().size(), collections.size());

  constexpr qint64 kBudgetMs = 3000;
  QVERIFY2(elapsedMs < kBudgetMs,
           qPrintable(QStringLiteral("SettingsDialog construction with 50 collections x 500 "
                                     "artwork types took %1ms (budget %2ms) — a config-"
                                     "dependent stall has regressed into the open path")
                          .arg(elapsedMs)
                          .arg(kBudgetMs)));
}
