// Shared blocking-request event loop for the DAT downloaders. The redirect
// policy is the only per-caller difference: NoIntroDownload reads redirects
// manually (a 302 carries the download id it must parse), RedumpDownload
// follows them within an on-host allowlist.
#include "dathttp.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace DatHttp {

namespace {

// A real desktop UA: DAT sites (DAT-o-MATIC in particular) serve the
// automation-friendly path to browsers and we are, functionally, performing
// the exact clicks a user would.
constexpr char kUserAgent[] =
    "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0";

// Response-size cap (Kartend-85zrx). The whole body is buffered with readAll()
// before inspection, so a hostile/compromised upstream or a MITM could stream
// unbounded data into memory and OOM the process. Abort once received bytes
// cross this ceiling, mirroring the scraper HttpClient's downloadProgress→abort
// pattern. 200 MiB comfortably fits real DAT packs (~tens of MiB).
constexpr qint64 kMaxResponseBytes = 200LL * 1024 * 1024;

} // namespace

bool cancelled(const CancelToken &c) {
  return c && c->load(std::memory_order_relaxed);
}

QNetworkReply *blockingRequest(QNetworkAccessManager &nam, const QUrl &url, const QByteArray &post,
                               const QString &referer, const CancelToken &cancel,
                               const std::function<void(qint64, qint64)> &onProgress,
                               const RedirectAllow &allowRedirect) {
  QNetworkRequest req(url);
  req.setRawHeader("User-Agent", kUserAgent);
  // Bound a stalled/slowloris server so the download worker can't hang
  // indefinitely (the cancel token only fires on explicit user cancel).
  // Matches the scraper HttpClient's 30s transfer timeout (Kartend-vu8io).
  req.setTransferTimeout(30000);
  if (!referer.isEmpty()) {
    req.setRawHeader("Referer", referer.toUtf8());
  }
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   allowRedirect ? QNetworkRequest::UserVerifiedRedirectPolicy
                                 : QNetworkRequest::ManualRedirectPolicy);

  QNetworkReply *reply = nullptr;
  if (post.isNull()) {
    reply = nam.get(req);
  } else {
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    reply = nam.post(req, post);
  }

  if (allowRedirect) {
    // UserVerifiedRedirectPolicy makes QNAM pause on each hop until
    // redirectAllowed(), so a rejected target is aborted before it is ever
    // contacted. The aborted reply finishes with OperationCanceledError,
    // which the callers surface as a download failure.
    QObject::connect(reply, &QNetworkReply::redirected, reply,
                     [reply, allowRedirect](const QUrl &target) {
                       if (allowRedirect(target)) {
                         emit reply->redirectAllowed();
                         return;
                       }
                       reply->abort();
                     });
  }

  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  if (onProgress) {
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                     [&onProgress](qint64 rcv, qint64 total) { onProgress(rcv, total); });
  }
  // Response-size cap: abort the reply once received bytes cross the ceiling so
  // a runaway response can't be buffered into memory before readAll() inspects
  // it (Kartend-85zrx). The aborted reply finishes with OperationCanceledError,
  // which the callers surface as a download failure.
  QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                   [reply](qint64 rcv, qint64 /*total*/) {
                     if (rcv > kMaxResponseBytes && reply->isRunning()) {
                       reply->abort();
                     }
                   });
  // Poll the cancel token on a short timer so a mid-transfer cancel aborts the
  // reply (which then finishes with OperationCanceledError and quits the loop).
  QTimer cancelTimer;
  if (cancel) {
    QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [reply, &cancel]() {
      if (cancelled(cancel) && reply->isRunning()) {
        reply->abort();
      }
    });
    cancelTimer.start(200);
  }
  if (!reply->isFinished()) {
    loop.exec();
  }
  return reply;
}

} // namespace DatHttp
