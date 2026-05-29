#include "test_scrapedialog_perf.h"

#include "metadatalookupprovider.h"
#include "scrapertypes.h"
#include "singleitemview.h"

#include <QCheckBox>
#include <QElapsedTimer>
#include <QList>
#include <QListWidget>
#include <QString>
#include <QStringList>
#include <QTest>
#include <utility>

namespace {

// Synchronous fake provider: fetchDetail invokes its callback inline with a
// ScrapedItem carrying `mediaAssetsPerItem` assets. That makes the whole
// candidate-pick → detail-render → media-checkbox-build chain run
// synchronously inside setProviderAndCandidates (which auto-selects row 0),
// so a single QElapsedTimer span captures the full populated-path cost the
// way the real interactive flow hits it — minus the network latency, which
// is not the freeze the bd is about.
class SyncFakeProvider : public MetadataLookupProvider {
public:
  explicit SyncFakeProvider(int mediaAssetsPerItem) : m_mediaAssets(mediaAssetsPerItem) {}

  // MetadataProvider surface.
  [[nodiscard]] QString id() const override { return QStringLiteral("fake"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("Fake Provider"); }
  [[nodiscard]] QStringList categories() const override { return {QStringLiteral("Video")}; }
  [[nodiscard]] Capabilities capabilities() const override {
    return Capability::MetadataLookup | Capability::MediaFetch;
  }

  // MetadataLookupProvider surface.
  void lookup(const QString & /*query*/, LookupCallback /*callback*/) override {}
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override {
    Scraper::ScrapedItem item;
    item.title = candidate.displayName;
    item.description = QStringLiteral("Synthetic detail body for perf stress.");
    item.media.reserve(m_mediaAssets);
    for (int i = 0; i < m_mediaAssets; ++i) {
      Scraper::MediaAsset asset;
      asset.type = QStringLiteral("screenshot_%1").arg(i);
      asset.label = QStringLiteral("Screenshot %1").arg(i);
      asset.url = QUrl(QStringLiteral("https://example.invalid/asset_%1.png").arg(i));
      item.media << asset;
    }
    if (callback) {
      // Result<T> constructs implicitly from a T value on the success path.
      callback(ErrorUtils::Result<Scraper::ScrapedItem>(item));
    }
  }
  void fetchMediaBytes(const QUrl & /*url*/, MediaCallback /*callback*/) override {}

private:
  int m_mediaAssets;
};

QList<Scraper::ScrapeCandidate> syntheticCandidates(int count) {
  QList<Scraper::ScrapeCandidate> candidates;
  candidates.reserve(count);
  for (int i = 0; i < count; ++i) {
    Scraper::ScrapeCandidate c;
    c.displayName = QStringLiteral("Candidate %1").arg(i);
    c.subtitle = QStringLiteral("2026 · Region %1").arg(i % 5);
    c.providerSpecificId = QStringLiteral("id-%1").arg(i);
    c.matchScore = 100 - (i % 100);
    candidates << c;
  }
  return candidates;
}

} // namespace

void TestScrapeDialogPerf::largeCandidateListPopulatesWithinBudget() {
  // 120 candidates, each detail carrying 50 media assets — the bd's "100+
  // items x 50 media assets" stress. setProviderAndCandidates builds the
  // candidate list (120 rows) and auto-selects row 0, which synchronously
  // fetches + renders that candidate's detail and builds its 50 media-asset
  // checkbox rows through the fake provider. The budget is a shape guard (a
  // genuine per-asset relayout or O(n^2) arrival regression blows into
  // seconds), wide enough to absorb a loaded CI runner under ASan.
  SyncFakeProvider provider(/*mediaAssetsPerItem=*/50);
  SingleItemScrapeView view(nullptr);

  QElapsedTimer timer;
  timer.start();
  view.setProviderAndCandidates(&provider, syntheticCandidates(120));
  const qint64 elapsedMs = timer.elapsed();

  // The candidate list must have actually populated (guards against a future
  // refactor deferring the build and making the timer meaningless).
  QVERIFY(view.candidateList());
  QCOMPARE(view.candidateList()->count(), 120);
  // Row 0 auto-selected → its detail fetched synchronously → 50 media rows.
  QVERIFY2(view.hasDetail(), "row 0's detail must have rendered via the fake provider");
  QCOMPARE(view.mediaRows().size(), 50);

  constexpr qint64 kBudgetMs = 3000;
  QVERIFY2(elapsedMs < kBudgetMs,
           qPrintable(QStringLiteral("setProviderAndCandidates with 120 candidates x 50 media "
                                     "assets took %1ms (budget %2ms) — a construction-time stall "
                                     "has regressed into the scrape-result arrival path")
                          .arg(elapsedMs)
                          .arg(kBudgetMs)));
}
