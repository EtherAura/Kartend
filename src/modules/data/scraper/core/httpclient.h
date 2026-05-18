#ifndef SCRAPER_HTTPCLIENT_H
#define SCRAPER_HTTPCLIENT_H

#include <functional>

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QUrl>

#include "errorutils.h"

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
QT_END_NAMESPACE

namespace Scraper {

/// Async HTTP wrapper used by every API-backed metadata provider.
///
/// One QNetworkAccessManager singleton serves the whole scraper layer
/// so we get connection reuse for free. A per-host token-bucket throttle
/// honours upstream rate limits (MusicBrainz wants 1 req/sec, others
/// have looser caps); the bucket parameters are configurable per host
/// via `setRateLimit`.
///
/// Tests exercise the parser layers directly with canned QByteArrays
/// rather than mocking this class — the request-issuing path is a
/// thin wrapper over Qt and not worth a separate fake.
class HttpClient : public QObject {
  Q_OBJECT
public:
  /// Process-wide singleton. Lazily constructed on first call.
  /// Lifetime tied to QApplication; safe to call from any main-thread
  /// context after QApplication exists.
  static HttpClient *instance();

  using ResponseCallback = std::function<void(ErrorUtils::Result<QByteArray> response)>;

  /// Issue an HTTP GET with the supplied User-Agent (required by some
  /// providers — MusicBrainz rejects bare requests). The callback fires
  /// on the main thread when the reply completes (success or error).
  /// Requests for the same host are queued behind any in-flight ones
  /// to honour the rate limit.
  void get(const QUrl &url, const QString &userAgent, ResponseCallback callback);

  /// Configure the rate limit for a host. `intervalMs` is the minimum
  /// delay between consecutive request *starts* (not completions, so
  /// the network time of one request overlaps with the next's throttle
  /// window). `maxConcurrent` caps in-flight requests for this host
  /// — set above 1 only when the upstream provider documents support
  /// for concurrent threads from a single key. The default (no rule
  /// set) is unlimited. Idempotent.
  void setRateLimit(const QString &host, int intervalMs, int maxConcurrent = 1);

  /// Cancels any pending requests in the queue (in-flight requests
  /// still complete and fire their callbacks). Useful for test
  /// teardown.
  void clearPending();

private:
  explicit HttpClient(QObject *parent = nullptr);
  ~HttpClient() override;

  struct PendingRequest {
    QUrl url;
    QString userAgent;
    ResponseCallback callback;
  };

  void enqueue(const QString &host, PendingRequest request);
  void drainHost(const QString &host);
  void send(const QString &host, PendingRequest request);

  QNetworkAccessManager *m_qnam = nullptr;
  struct HostPolicy {
    int intervalMs = 0;    // min ms between successive request *starts*
    int maxConcurrent = 1; // cap of in-flight requests for this host
  };
  /// Per-host pacing rules. Missing entry = no throttle, unlimited
  /// concurrency.
  QHash<QString, HostPolicy> m_rateLimits;
  /// Per-host queue of pending requests.
  QHash<QString, QQueue<PendingRequest>> m_queues;
  /// Per-host in-flight count (number of replies awaiting completion).
  /// Used together with HostPolicy::maxConcurrent to gate dispatch.
  QHash<QString, int> m_inFlight;
  /// Per-host monotonic timer used to measure the gap to the next
  /// allowed start. We share one QElapsedTimer per host so cross-host
  /// pacing stays independent; a started timer is what the inter-start
  /// check reads.
  QHash<QString, QElapsedTimer> m_lastStartTimer;
  /// Per-host scheduled-wakeup flag so we don't pile multiple
  /// singleShot timers on top of each other while a host is paced.
  QHash<QString, bool> m_drainScheduled;
};

} // namespace Scraper

#endif // SCRAPER_HTTPCLIENT_H
