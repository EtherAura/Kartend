// Unit tests for Scraper::ScrapeDownloadDispatcher (Kartend-dpehr) — the
// non-UI download orchestration extracted from ScrapeResultDialog. No QDialog:
// a fake MetadataLookupProvider feeds canned fetchMediaBytes responses so the
// dispatcher's tally / progress / completion / skip logic is exercised in
// isolation. Signals are captured via direct lambda connections (same thread),
// so QList<PendingMediaWrite> needs no metatype registration.
#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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
  QStringList failedTypes;
  qint64 totalBytes = -1;
  std::vector<std::tuple<int, int, qint64>> progress; // (completed, total, bytesSoFar)

  void wire(ScrapeDownloadDispatcher &d) {
    QObject::connect(
        &d, &ScrapeDownloadDispatcher::finished, &d,
        [this](const QList<PendingMediaWrite> &dl, qint64 bytes, const QStringList &failed) {
          ++finishedCount;
          downloads = dl;
          totalBytes = bytes;
          failedTypes = failed;
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
  void emptyOkBody_reportsFailedTypeNotPersisted();
  void dedupHitUnreadable_fallsBackToNetworkFetch();
  void dedupHitUnreadable_networkFailureLandsInFailedTypes();
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
  QVERIFY(cap.failedTypes.isEmpty());
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

  d.dispatch({asset("front", "http://x/ok"), asset("Back", "http://x/fail")});

  QCOMPARE(cap.finishedCount, 1);
  // The failed asset is dropped; partial success is preserved.
  QCOMPARE(cap.downloads.size(), 1);
  QCOMPARE(cap.downloads.first().asset.type, QStringLiteral("front"));
  QCOMPARE(cap.totalBytes, qint64(7));
  // ... but the failure is REPORTED (lowercased) so the caller can keep the
  // type's mediaAbsent marker instead of pruning it as satisfied — a failed
  // interactive download must not read as "user deselected it"
  // (Kartend-jjyst.14).
  QCOMPARE(cap.failedTypes, QStringList{QStringLiteral("back")});
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
  // A hash hit means the local file already satisfies the asset — a success,
  // not a failed fetch (Kartend-jjyst.14).
  QVERIFY(cap.failedTypes.isEmpty());
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

void TestScrapeDownloadDispatcher::emptyOkBody_reportsFailedTypeNotPersisted() {
  // An HTTP-ok response with an empty body delivers nothing — same failure
  // classification as the batch aggregator's "isOk && !isEmpty" success test:
  // not persisted, and reported in failedTypes (Kartend-jjyst.14).
  FakeProvider provider;
  provider.okBytes[QStringLiteral("http://x/empty")] = QByteArray();
  provider.okBytes[QStringLiteral("http://x/real")] = QByteArray("REAL");

  ScrapeDownloadDispatcher d;
  ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = &provider;
  d.setConfig(cfg);
  Capture cap;
  cap.wire(d);

  d.dispatch({asset("video", "http://x/empty"), asset("front", "http://x/real")});

  QCOMPARE(cap.finishedCount, 1);
  QCOMPARE(cap.downloads.size(), 1);
  QCOMPARE(cap.downloads.first().asset.type, QStringLiteral("front"));
  QCOMPARE(cap.totalBytes, qint64(4));
  QCOMPARE(cap.failedTypes, QStringList{QStringLiteral("video")});
}

void TestScrapeDownloadDispatcher::dedupHitUnreadable_fallsBackToNetworkFetch() {
  // Kartend-jjyst.17: a shared-asset dedup hit whose local file read yields no
  // bytes (zero-byte / unreadable copy) used to resolve silently — neither
  // downloaded nor in failedTypes, so the asset just vanished from the result.
  // It must fall back to the network fetch instead. A sibling asset with a
  // readable dedup copy proves the happy dedup path still short-circuits.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString goodPath =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  const QString emptyPath =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/fanart/platform_4.png"));
  QVERIFY(QDir().mkpath(QFileInfo(goodPath).absolutePath()));
  QVERIFY(QDir().mkpath(QFileInfo(emptyPath).absolutePath()));
  {
    QFile f(goodPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.write(QByteArray("SHAREDBYTES")) > 0);
  }
  {
    QFile f(emptyPath); // exists, but reads back zero bytes
    QVERIFY(f.open(QIODevice::WriteOnly));
  }

  FakeProvider provider;
  provider.okBytes[QStringLiteral("http://x/fanart")] = QByteArray("NETBYTES");

  ScrapeDownloadDispatcher d;
  ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = &provider;
  cfg.sharedSearchPaths = {tmp.path()};
  d.setConfig(cfg);
  Capture cap;
  cap.wire(d);

  MediaAsset good = asset(QStringLiteral("wheel"), QStringLiteral("http://x/wheel"));
  good.scope = Scraper::MediaScope::Platform;
  good.scopeKey = QStringLiteral("4");
  MediaAsset empty = asset(QStringLiteral("fanart"), QStringLiteral("http://x/fanart"));
  empty.scope = Scraper::MediaScope::Platform;
  empty.scopeKey = QStringLiteral("4");
  d.dispatch({good, empty});

  QCOMPARE(cap.finishedCount, 1);
  // Only the unreadable dedup hit went to the network; the readable one
  // short-circuited from disk.
  QCOMPARE(provider.fetchCalls, 1);
  QCOMPARE(cap.downloads.size(), 2);
  QCOMPARE(cap.downloads.first().asset.type, QStringLiteral("wheel"));
  QCOMPARE(cap.downloads.first().bytes, QByteArray("SHAREDBYTES"));
  QCOMPARE(cap.downloads.last().asset.type, QStringLiteral("fanart"));
  QCOMPARE(cap.downloads.last().bytes, QByteArray("NETBYTES"));
  QVERIFY(cap.failedTypes.isEmpty());
}

void TestScrapeDownloadDispatcher::dedupHitUnreadable_networkFailureLandsInFailedTypes() {
  // Kartend-jjyst.17, failure leg: when the fallback network fetch fails too,
  // the type must land in failedTypes (absent-marker protection + summary
  // failure tick) rather than vanish.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString emptyPath =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/fanart/platform_4.png"));
  QVERIFY(QDir().mkpath(QFileInfo(emptyPath).absolutePath()));
  {
    QFile f(emptyPath); // exists, but reads back zero bytes
    QVERIFY(f.open(QIODevice::WriteOnly));
  }

  FakeProvider provider;
  provider.errorUrls.insert(QStringLiteral("http://x/fanart"));

  ScrapeDownloadDispatcher d;
  ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = &provider;
  cfg.sharedSearchPaths = {tmp.path()};
  d.setConfig(cfg);
  Capture cap;
  cap.wire(d);

  MediaAsset empty = asset(QStringLiteral("fanart"), QStringLiteral("http://x/fanart"));
  empty.scope = Scraper::MediaScope::Platform;
  empty.scopeKey = QStringLiteral("4");
  d.dispatch({empty});

  QCOMPARE(cap.finishedCount, 1);
  QCOMPARE(provider.fetchCalls, 1);
  QVERIFY(cap.downloads.isEmpty());
  QCOMPARE(cap.failedTypes, QStringList{QStringLiteral("fanart")});
}

QTEST_MAIN(TestScrapeDownloadDispatcher)
#include "test_scrapedownloaddispatcher.moc"
