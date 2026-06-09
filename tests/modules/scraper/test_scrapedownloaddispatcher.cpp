// Unit tests for Scraper::ScrapeDownloadDispatcher (Kartend-dpehr) — the
// non-UI download orchestration extracted from ScrapeResultDialog. No QDialog:
// a fake MetadataLookupProvider feeds canned fetchMediaBytes responses so the
// dispatcher's tally / progress / completion / skip logic is exercised in
// isolation. Signals are captured via direct lambda connections (same thread),
// so QList<PendingMediaWrite> needs no metatype registration.
#include <QtTest/QtTest>

#include <tuple>
#include <vector>

#include "metadatalookupprovider.h"
#include "scrapedownloaddispatcher.h"

using Scraper::MediaAsset;
using Scraper::PendingMediaWrite;
using Scraper::ScrapeDownloadDispatcher;

namespace {

// Minimal MetadataLookupProvider: only fetchMediaBytes matters. Each call
// immediately invokes the callback with a per-URL canned response (default: a
// fixed byte body), and counts invocations.
class FakeProvider : public MetadataLookupProvider {
public:
  QHash<QString, QByteArray> okBytes; // url -> success body
  QSet<QString> errorUrls;            // url -> return a fetch error
  QByteArray fallback = QByteArray("BYTES");
  int fetchCalls = 0;

  [[nodiscard]] QString id() const override { return QStringLiteral("fake"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("Fake"); }
  [[nodiscard]] QStringList categories() const override { return {}; }
  [[nodiscard]] Capabilities capabilities() const override { return Capability::MediaFetch; }

  void lookup(const QString &, LookupCallback) override {}
  void fetchDetail(const Scraper::ScrapeCandidate &, DetailCallback) override {}

  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override {
    ++fetchCalls;
    const QString key = url.toString();
    if (errorUrls.contains(key)) {
      callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                               QStringLiteral("404"), QStringLiteral("fake")));
      return;
    }
    const auto it = okBytes.constFind(key);
    callback(ErrorUtils::Result<QByteArray>(it != okBytes.constEnd() ? it.value() : fallback));
  }
};

MediaAsset asset(const QString &type, const QString &url) {
  MediaAsset a;
  a.type = type;
  a.label = type;
  a.url = QUrl(url);
  return a;
}

// Captures the dispatcher's finished()/progressed() signals into locals.
struct Capture {
  int finishedCount = 0;
  QList<PendingMediaWrite> downloads;
  qint64 totalBytes = -1;
  std::vector<std::tuple<int, int, qint64>> progress; // (completed, total, bytesSoFar)

  void wire(ScrapeDownloadDispatcher &d) {
    QObject::connect(&d, &ScrapeDownloadDispatcher::finished, &d,
                     [this](const QList<PendingMediaWrite> &dl, qint64 bytes) {
                       ++finishedCount;
                       downloads = dl;
                       totalBytes = bytes;
                     });
    QObject::connect(&d, &ScrapeDownloadDispatcher::progressed, &d,
                     [this](int completed, int total, qint64 bytesSoFar) {
                       progress.emplace_back(completed, total, bytesSoFar);
                     });
  }
};

} // namespace

class TestScrapeDownloadDispatcher : public QObject {
  Q_OBJECT
private slots:
  void allNetworkSuccess_tallisesAndFinishesOnce();
  void networkError_isSkippedButStillFinishes();
  void hashShortCircuitBody_isNotPersisted();
  void noProvider_finishesEmptyImmediately();
  void progressedFiresPerCompletionWithRunningBytes();
};

void TestScrapeDownloadDispatcher::allNetworkSuccess_tallisesAndFinishesOnce() {
  FakeProvider provider;
  provider.okBytes[QStringLiteral("http://x/a")] = QByteArray("AA");
  provider.okBytes[QStringLiteral("http://x/b")] = QByteArray("BBBB");

  ScrapeDownloadDispatcher d;
  ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = &provider;
  d.setConfig(cfg);
  Capture cap;
  cap.wire(d);

  d.dispatch({asset("front", "http://x/a"), asset("back", "http://x/b")});

  QCOMPARE(cap.finishedCount, 1);
  QCOMPARE(cap.downloads.size(), 2);
  QCOMPARE(cap.totalBytes, qint64(2 + 4));
  QCOMPARE(provider.fetchCalls, 2);
}

void TestScrapeDownloadDispatcher::networkError_isSkippedButStillFinishes() {
  FakeProvider provider;
  provider.okBytes[QStringLiteral("http://x/ok")] = QByteArray("OKBYTES");
  provider.errorUrls.insert(QStringLiteral("http://x/fail"));

  ScrapeDownloadDispatcher d;
  ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = &provider;
  d.setConfig(cfg);
  Capture cap;
  cap.wire(d);

  d.dispatch({asset("front", "http://x/ok"), asset("back", "http://x/fail")});

  QCOMPARE(cap.finishedCount, 1);
  // The failed asset is dropped; partial success is preserved.
  QCOMPARE(cap.downloads.size(), 1);
  QCOMPARE(cap.downloads.first().asset.type, QStringLiteral("front"));
  QCOMPARE(cap.totalBytes, qint64(7));
}

void TestScrapeDownloadDispatcher::hashShortCircuitBody_isNotPersisted() {
  // A "MD5OK"-class body means the server confirmed the local file matches —
  // the dispatcher must NOT add it to the downloads (existing file stays put).
  FakeProvider provider;
  provider.okBytes[QStringLiteral("http://x/match")] = QByteArray("MD5OK");
  provider.okBytes[QStringLiteral("http://x/real")] = QByteArray("REALBYTES");

  ScrapeDownloadDispatcher d;
  ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = &provider;
  d.setConfig(cfg);
  Capture cap;
  cap.wire(d);

  d.dispatch({asset("front", "http://x/match"), asset("back", "http://x/real")});

  QCOMPARE(cap.finishedCount, 1);
  QCOMPARE(cap.downloads.size(), 1);
  QCOMPARE(cap.downloads.first().asset.type, QStringLiteral("back"));
}

void TestScrapeDownloadDispatcher::noProvider_finishesEmptyImmediately() {
  ScrapeDownloadDispatcher d; // no config.provider
  Capture cap;
  cap.wire(d);
  d.dispatch({asset("front", "http://x/a")});
  QCOMPARE(cap.finishedCount, 1);
  QCOMPARE(cap.downloads.size(), 0);
  QCOMPARE(cap.totalBytes, qint64(0));
}

void TestScrapeDownloadDispatcher::progressedFiresPerCompletionWithRunningBytes() {
  FakeProvider provider; // all use the 5-byte "BYTES" fallback
  ScrapeDownloadDispatcher d;
  ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = &provider;
  d.setConfig(cfg);
  Capture cap;
  cap.wire(d);

  d.dispatch({asset("a", "http://x/1"), asset("b", "http://x/2"), asset("c", "http://x/3")});

  QCOMPARE(cap.progress.size(), size_t(3));
  // completed climbs 1,2,3; total stays 3; bytesSoFar accumulates 5,10,15.
  QCOMPARE(std::get<0>(cap.progress[0]), 1);
  QCOMPARE(std::get<1>(cap.progress[0]), 3);
  QCOMPARE(std::get<2>(cap.progress[0]), qint64(5));
  QCOMPARE(std::get<0>(cap.progress[2]), 3);
  QCOMPARE(std::get<2>(cap.progress[2]), qint64(15));
}

QTEST_MAIN(TestScrapeDownloadDispatcher)
#include "test_scrapedownloaddispatcher.moc"
