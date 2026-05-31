#ifndef SCRAPER_HTTPCLIENT_H
#define SCRAPER_HTTPCLIENT_H

#include <functional>

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QQueue>
#include <QString>
#include <QStringList>
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
  Q_DISABLE_COPY_MOVE(HttpClient)
public:
  /// Process-wide singleton. Lazily constructed on first call.
  /// Lifetime tied to QApplication; safe to call from any main-thread
  /// context after QApplication exists.
  ///
  /// The QApplication-scoped lifetime is intentional, not an oversight.
  /// HttpClient owns a single QNetworkAccessManager + the per-host rate
  /// limit + the in-flight queue; tying that state to ApplicationManager
  /// (which can in principle be torn down and rebuilt mid-process for
  /// headless kart import/export) would invalidate every queued request
  /// and rebuild the QNAM connection pool on each cycle. Since QApplication
  /// lives for the entire process and is destroyed exactly once at exit,
  /// the singleton naturally outlives every consumer.
  ///
  /// instance() Q_ASSERTs qApp is alive — if you call it before QApplication
  /// construction or after `QCoreApplication::quit()` has completed, the
  /// assertion fires in debug builds.
  static HttpClient *instance();

  using ResponseCallback = std::function<void(ErrorUtils::Result<QByteArray> response)>;

  /// Raw request headers as ordered name/value byte pairs, applied
  /// verbatim via QNetworkRequest::setRawHeader. Callers pass a
  /// User-Agent here (some upstreams reject bare requests) and any
  /// credential header such as `Authorization: Bearer <token>`. Headers
  /// are never logged, so this is the leak-safe way to send a secret —
  /// see get().
  using RawHeaders = QList<QPair<QByteArray, QByteArray>>;

  /// Default response-size cap. A hostile or buggy server can stream
  /// gigabytes into RAM otherwise — none of our legitimate scraper
  /// payloads (JSON metadata up to a few hundred KB, box art and
  /// screenshots in the low MB, manuals/videos in the tens of MB) come
  /// anywhere near this. Tuned to leave a wide margin above any real
  /// response while still stopping a runaway long before OOM.
  static constexpr qint64 kDefaultMaxResponseBytes = 256LL * 1024 * 1024;

  /// Issue an HTTP GET. @p headers are applied verbatim as raw request
  /// headers (see RawHeaders): callers pass a User-Agent here (required
  /// by some providers — MusicBrainz rejects bare requests) and any
  /// credential header such as Authorization. The callback fires on the
  /// main thread when the reply completes (success or error). Requests
  /// for the same host are queued behind any in-flight ones to honour
  /// the rate limit.
  ///
  /// Headers are never logged. Query strings can be (the ENQ trace logs
  /// them to show media presets) and are only redacted on a best-effort
  /// keyword basis, and they additionally leak via Referer and upstream
  /// proxy access logs — so a credential belongs in a header, not the
  /// URL (Kartend-0gp7).
  ///
  /// @p maxResponseBytes caps the response body. If the server streams
  /// more than this, the reply is aborted and the callback fires with
  /// ErrorCode::ResponseTooLarge. Pass <= 0 to disable the cap (not
  /// recommended for untrusted upstreams).
  ///
  /// @p expectedContentTypePrefix optionally constrains the response's
  /// Content-Type header (case-insensitive starts-with match). Set to
  /// e.g. "image/" on per-item media fetches so a misconfigured or
  /// hostile upstream that returns HTML / JSON / executable bytes is
  /// rejected before the body reaches the caller's decode path
  /// (Kartend-9ryx). Empty (default) disables the check.
  ///
  /// @p allowedHostSuffixes optionally pins the request — and every
  /// redirect it follows — to a set of trusted domains (SSRF
  /// defense-in-depth, Kartend-pugp.2). Each entry matches a host that
  /// equals it or is a subdomain of it (so "screenscraper.fr" allows
  /// "neoclone.screenscraper.fr" but not "evilscreenscraper.fr"). When
  /// non-empty: the initial URL's host is checked here at the choke
  /// point (refused with ErrorCode::InvalidArgument before any network
  /// work, like the https guard), and the request runs under
  /// UserVerifiedRedirectPolicy so a cross-domain redirect target is
  /// aborted *before* it is contacted rather than followed. Empty
  /// (default) leaves the request unrestricted and on the standard
  /// NoLessSafeRedirectPolicy — set it only for callers whose URL is
  /// derived from an untrusted response (ScreenScraper medias[].url).
  void get(const QUrl &url, const RawHeaders &headers, ResponseCallback callback,
           qint64 maxResponseBytes = kDefaultMaxResponseBytes,
           const QString &expectedContentTypePrefix = QString(),
           const QStringList &allowedHostSuffixes = QStringList());

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
    RawHeaders headers;
    ResponseCallback callback;
    qint64 maxResponseBytes = kDefaultMaxResponseBytes;
    /// See get(): non-empty -> the reply's Content-Type must
    /// case-insensitively start with this string or the request fails
    /// with ErrorCode::InvalidArgument before the body reaches the
    /// caller (Kartend-9ryx).
    QString expectedContentTypePrefix;
    /// See get(): non-empty -> the host (and every redirect host) must
    /// match one of these domain suffixes, and the request runs under
    /// UserVerifiedRedirectPolicy so off-domain redirects are aborted
    /// before they are contacted (Kartend-pugp.2).
    QStringList allowedHostSuffixes;
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
