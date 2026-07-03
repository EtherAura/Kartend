// SingleItemScrapeView: the candidate / detail / media surface of the scrape
// result dialog, driven standalone with a stub MetadataLookupProvider (no
// dialog, no network, never shown). Covers the candidate-list population and
// label format (subtitle indent + match score), the single-result
// auto-hide-list heuristic, the detail fetch lifecycle
// (loading → loaded / failed signals, per-row cache, the stale-callback
// guard for superseded rows), the rendered detail HTML (field rows skipped
// when blank, HTML-escaping of provider text, runtime + custom-field rows),
// and the media checkbox rows (pre-checked, label fallback to the asset
// type, shared-scope hints).

#include "singleitemview.h"

#include "metadatalookupprovider.h"

#include <QApplication>
#include <QCheckBox>
#include <QListWidget>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QTextBrowser>

namespace {

// Detail-stack page indices (mirrors the file-local kStack*Index constants
// in singleitemview.cpp — setDetailPage maps Empty/Loading/Detail onto them
// in declaration order).
constexpr int kEmptyPage = 0;
constexpr int kDetailPage = 2;

/// Answers fetchDetail synchronously from detailById (empty item when the
/// id is unknown), fails ids listed in failIds, or parks the callback for
/// the test to fire by hand when park is set.
class StubDetailProvider : public MetadataLookupProvider {
public:
  bool park = false;
  QHash<QString, Scraper::ScrapedItem> detailById;
  QSet<QString> failIds;
  QList<QPair<QString, DetailCallback>> parkedCalls;
  int fetchDetailCalls = 0;

  QString id() const override { return QStringLiteral("stub"); }
  QString displayName() const override { return QStringLiteral("Stub"); }
  QStringList categories() const override { return {}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  void lookup(const QString &, LookupCallback) override {}
  void fetchMediaBytes(const QUrl &, MediaCallback) override {}
  void fetchDetail(const Scraper::ScrapeCandidate &c, DetailCallback cb) override {
    ++fetchDetailCalls;
    if (park) {
      parkedCalls.append({c.providerSpecificId, std::move(cb)});
      return;
    }
    if (failIds.contains(c.providerSpecificId)) {
      cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                         QStringLiteral("detail fetch failed"))
             .withHttpStatus(500));
      return;
    }
    cb(detailById.value(c.providerSpecificId));
  }
};

Scraper::ScrapeCandidate makeCandidate(const QString &name, const QString &subtitle,
                                       const QString &id, int score) {
  Scraper::ScrapeCandidate c;
  c.displayName = name;
  c.subtitle = subtitle;
  c.providerSpecificId = id;
  c.matchScore = score;
  return c;
}

Scraper::ScrapedItem makeDetail(const QString &title) {
  Scraper::ScrapedItem d;
  d.title = title;
  return d;
}

Scraper::MediaAsset makeAsset(const QString &type, const QString &label,
                              Scraper::MediaScope scope) {
  Scraper::MediaAsset a;
  a.type = type;
  a.label = label;
  a.url = QUrl(QStringLiteral("https://example.test/%1.png").arg(type));
  a.scope = scope;
  return a;
}

/// The media QListWidget (the view owns two lists; the other is the
/// candidate list exposed via candidateList()).
QListWidget *mediaList(SingleItemScrapeView &view) {
  const auto lists = view.findChildren<QListWidget *>();
  for (QListWidget *l : lists) {
    if (l != view.candidateList()) {
      return l;
    }
  }
  return nullptr;
}

} // namespace

class TestSingleItemScrapeView : public QObject {
  Q_OBJECT

private slots:
  void emptyCandidatesShowEmptyPageAndFailDetail();
  void singleCandidateAutoSelectsAndHidesList();
  void multiCandidateLabelsIncludeSubtitleAndScore();
  void detailHtmlRendersFieldsAndEscapes();
  void mediaRowsPrecheckedWithScopeHints();
  void detailCacheAvoidsRefetchOnReselect();
  void detailErrorRendersFailureAndClearsMedia();
  void staleDetailCallbackForSupersededRowIgnored();
};

void TestSingleItemScrapeView::emptyCandidatesShowEmptyPageAndFailDetail() {
  StubDetailProvider provider;
  SingleItemScrapeView view;
  QSignalSpy failedSpy(&view, &SingleItemScrapeView::detailFailed);

  view.setProviderAndCandidates(&provider, {});

  QCOMPARE(failedSpy.count(), 1); // host disables Apply off this
  QCOMPARE(view.candidateList()->count(), 0);
  QVERIFY(!view.candidateList()->isVisibleTo(&view));
  QVERIFY(!view.hasDetail());
  QCOMPARE(provider.fetchDetailCalls, 0);
  auto *stack = view.findChild<QStackedWidget *>();
  QVERIFY(stack);
  QCOMPARE(stack->currentIndex(), kEmptyPage);
}

void TestSingleItemScrapeView::singleCandidateAutoSelectsAndHidesList() {
  StubDetailProvider provider;
  provider.detailById.insert(QStringLiteral("id-a"), makeDetail(QStringLiteral("Alpha Detail")));
  SingleItemScrapeView view;
  QSignalSpy loadingSpy(&view, &SingleItemScrapeView::detailLoading);
  QSignalSpy loadedSpy(&view, &SingleItemScrapeView::detailLoaded);

  view.setProviderAndCandidates(
      &provider,
      {makeCandidate(QStringLiteral("Alpha"), QStringLiteral("1997"), QStringLiteral("id-a"), 90)});

  // Row 0 auto-selected → one fetch, loading then loaded.
  QCOMPARE(provider.fetchDetailCalls, 1);
  QCOMPARE(loadingSpy.count(), 1);
  QCOMPARE(loadedSpy.count(), 1);
  QVERIFY(view.hasDetail());
  QCOMPARE(view.currentDetail().title, QStringLiteral("Alpha Detail"));
  // Single-result responses hide the candidate list entirely; the row label
  // still carries the subtitle indent + score for when the list is shown.
  QCOMPARE(view.candidateList()->count(), 1);
  QVERIFY(!view.candidateList()->isVisibleTo(&view));
  QCOMPARE(view.candidateList()->item(0)->text(), QStringLiteral("Alpha\n  1997  (90)"));
  auto *stack = view.findChild<QStackedWidget *>();
  QVERIFY(stack);
  QCOMPARE(stack->currentIndex(), kDetailPage);
}

void TestSingleItemScrapeView::multiCandidateLabelsIncludeSubtitleAndScore() {
  StubDetailProvider provider;
  SingleItemScrapeView view;

  view.setProviderAndCandidates(
      &provider,
      {makeCandidate(QStringLiteral("Alpha"), QStringLiteral("1997"), QStringLiteral("id-a"), 95),
       makeCandidate(QStringLiteral("Beta"), QString(), QStringLiteral("id-b"), 42),
       makeCandidate(QStringLiteral("Gamma"), QString(), QStringLiteral("id-c"), -1)});

  QVERIFY(view.candidateList()->isVisibleTo(&view)); // picking is the point
  QCOMPARE(view.candidateList()->count(), 3);
  QCOMPARE(view.candidateList()->item(0)->text(), QStringLiteral("Alpha\n  1997  (95)"));
  QCOMPARE(view.candidateList()->item(1)->text(), QStringLiteral("Beta  (42)"));
  QCOMPARE(view.candidateList()->item(2)->text(), QStringLiteral("Gamma"));
  QCOMPARE(view.candidateList()->currentRow(), 0);
}

void TestSingleItemScrapeView::detailHtmlRendersFieldsAndEscapes() {
  StubDetailProvider provider;
  Scraper::ScrapedItem d;
  d.title = QStringLiteral("A <b>Game</b>");
  d.publisher = QStringLiteral("PubCo");
  d.developer = QStringLiteral("DevWorks");
  d.releaseDate = QStringLiteral("1997");
  // genre left blank → its row must be skipped, not rendered empty.
  d.runtimeSeconds = 90;
  d.description = QStringLiteral("Long body text.");
  d.customFields.insert(QStringLiteral("other_key"), QStringLiteral("other value"));
  provider.detailById.insert(QStringLiteral("id-a"), d);
  SingleItemScrapeView view;

  view.setProviderAndCandidates(
      &provider, {makeCandidate(QStringLiteral("Alpha"), QString(), QStringLiteral("id-a"), -1)});

  auto *browser = view.findChild<QTextBrowser *>();
  QVERIFY(browser);
  const QString plain = browser->toPlainText();
  // The raw "<b>" survives as text — proof the title was HTML-escaped
  // rather than parsed as markup.
  QVERIFY(plain.contains(QStringLiteral("A <b>Game</b>")));
  QVERIFY(plain.contains(QStringLiteral("Publisher: PubCo")));
  QVERIFY(plain.contains(QStringLiteral("Developer: DevWorks")));
  QVERIFY(plain.contains(QStringLiteral("Released: 1997")));
  QVERIFY(plain.contains(QStringLiteral("Runtime: 90s")));
  QVERIFY(!plain.contains(QStringLiteral("Genre:")));
  QVERIFY(plain.contains(QStringLiteral("Long body text.")));
  QVERIFY(plain.contains(QStringLiteral("other_key: other value")));
}

void TestSingleItemScrapeView::mediaRowsPrecheckedWithScopeHints() {
  StubDetailProvider provider;
  Scraper::ScrapedItem d = makeDetail(QStringLiteral("Alpha Detail"));
  d.media.append(
      makeAsset(QStringLiteral("front"), QStringLiteral("Front cover"), Scraper::MediaScope::Game));
  d.media.append(makeAsset(QStringLiteral("background"), QStringLiteral("Theme background"),
                           Scraper::MediaScope::Group));
  // Empty label → the row falls back to the asset type.
  d.media.append(makeAsset(QStringLiteral("wheel"), QString(), Scraper::MediaScope::Company));
  d.media.append(makeAsset(QStringLiteral("bezel-16-9"), QStringLiteral("Bezel"),
                           Scraper::MediaScope::Platform));
  provider.detailById.insert(QStringLiteral("id-a"), d);
  SingleItemScrapeView view;

  view.setProviderAndCandidates(
      &provider, {makeCandidate(QStringLiteral("Alpha"), QString(), QStringLiteral("id-a"), -1)});

  const auto &rows = view.mediaRows();
  QCOMPARE(rows.size(), 4);
  for (const auto &row : rows) {
    QVERIFY(row.first->isChecked()); // everything pre-checked; user opts out
  }
  QCOMPARE(rows[0].first->text(), QStringLiteral("Front cover"));
  QCOMPARE(rows[1].first->text(), QStringLiteral("Theme background — theme/family (shared)"));
  QCOMPARE(rows[2].first->text(), QStringLiteral("wheel — publisher (shared)"));
  QCOMPARE(rows[3].first->text(), QStringLiteral("Bezel — platform (shared)"));
  QCOMPARE(rows[1].second.type, QStringLiteral("background")); // asset payload preserved

  view.clearMediaRows();
  QVERIFY(view.mediaRows().isEmpty());
  QListWidget *media = mediaList(view);
  QVERIFY(media);
  QCOMPARE(media->count(), 0);
}

void TestSingleItemScrapeView::detailCacheAvoidsRefetchOnReselect() {
  StubDetailProvider provider;
  provider.detailById.insert(QStringLiteral("id-a"), makeDetail(QStringLiteral("Alpha Detail")));
  provider.detailById.insert(QStringLiteral("id-b"), makeDetail(QStringLiteral("Beta Detail")));
  SingleItemScrapeView view;
  QSignalSpy loadedSpy(&view, &SingleItemScrapeView::detailLoaded);

  view.setProviderAndCandidates(
      &provider, {makeCandidate(QStringLiteral("Alpha"), QString(), QStringLiteral("id-a"), -1),
                  makeCandidate(QStringLiteral("Beta"), QString(), QStringLiteral("id-b"), -1)});
  QCOMPARE(provider.fetchDetailCalls, 1); // row 0 auto-selected

  view.candidateList()->setCurrentRow(1);
  QCOMPARE(provider.fetchDetailCalls, 2);
  QCOMPARE(view.currentDetail().title, QStringLiteral("Beta Detail"));

  // Back to row 0: served from the per-row cache, no third fetch, but the
  // loaded signal still fires so the host re-enables Apply.
  view.candidateList()->setCurrentRow(0);
  QCOMPARE(provider.fetchDetailCalls, 2);
  QCOMPARE(view.currentDetail().title, QStringLiteral("Alpha Detail"));
  QCOMPARE(loadedSpy.count(), 3);
}

void TestSingleItemScrapeView::detailErrorRendersFailureAndClearsMedia() {
  StubDetailProvider provider;
  provider.failIds.insert(QStringLiteral("id-a"));
  SingleItemScrapeView view;
  QSignalSpy loadedSpy(&view, &SingleItemScrapeView::detailLoaded);
  QSignalSpy failedSpy(&view, &SingleItemScrapeView::detailFailed);

  view.setProviderAndCandidates(
      &provider, {makeCandidate(QStringLiteral("Alpha"), QString(), QStringLiteral("id-a"), -1)});

  QCOMPARE(failedSpy.count(), 1);
  QCOMPARE(loadedSpy.count(), 0);
  QVERIFY(!view.hasDetail());
  QVERIFY(view.mediaRows().isEmpty());
  // The failure is rendered on the detail page (enriched summary), not
  // swallowed behind the empty placeholder.
  auto *stack = view.findChild<QStackedWidget *>();
  QVERIFY(stack);
  QCOMPARE(stack->currentIndex(), kDetailPage);
  auto *browser = view.findChild<QTextBrowser *>();
  QVERIFY(browser);
  QVERIFY(browser->toPlainText().contains(QStringLiteral("detail fetch failed")));
}

void TestSingleItemScrapeView::staleDetailCallbackForSupersededRowIgnored() {
  StubDetailProvider provider;
  provider.park = true; // the test fires the callbacks by hand
  SingleItemScrapeView view;
  QSignalSpy loadedSpy(&view, &SingleItemScrapeView::detailLoaded);

  view.setProviderAndCandidates(
      &provider, {makeCandidate(QStringLiteral("Alpha"), QString(), QStringLiteral("id-a"), -1),
                  makeCandidate(QStringLiteral("Beta"), QString(), QStringLiteral("id-b"), -1)});
  QCOMPARE(provider.parkedCalls.size(), 1); // row 0's fetch in flight

  // User moves on to row 1 while row 0's fetch is still pending.
  view.candidateList()->setCurrentRow(1);
  QCOMPARE(provider.parkedCalls.size(), 2);

  // The late row-0 result must not clobber the row-1 view state.
  QCOMPARE(provider.parkedCalls[0].first, QStringLiteral("id-a"));
  provider.parkedCalls[0].second(makeDetail(QStringLiteral("Alpha Detail")));
  QCOMPARE(loadedSpy.count(), 0);
  QVERIFY(!view.hasDetail());

  // The current row's result lands normally.
  provider.parkedCalls[1].second(makeDetail(QStringLiteral("Beta Detail")));
  QCOMPARE(loadedSpy.count(), 1);
  QCOMPARE(view.currentDetail().title, QStringLiteral("Beta Detail"));
}

QTEST_MAIN(TestSingleItemScrapeView)
#include "test_singleitemview.moc"
