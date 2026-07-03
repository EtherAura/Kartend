// redump.org DAT downloader — one GET per system, no form/cookie/token.
#include "redumpdownload.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "nointroparse.h" // filenameFromContentDisposition

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace RedumpDownload {

namespace {

constexpr char kBase[] = "https://redump.org/";
constexpr char kUserAgent[] =
    "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0";

// Response-size cap (Kartend-85zrx). The whole body is buffered with readAll()
// before inspection, so a hostile/compromised upstream or a MITM could stream
// unbounded data into memory and OOM the process. Abort once received bytes
// cross this ceiling, mirroring the scraper HttpClient's downloadProgress→abort
// pattern. 200 MiB comfortably fits real DAT packs (~tens of MiB).
constexpr qint64 kMaxResponseBytes = 200LL * 1024 * 1024;

bool cancelled(const CancelToken &c) {
  return c && c->load(std::memory_order_relaxed);
}

// True when a redirect target stays on https + redump.org (or a subdomain).
// The leading-dot boundary is load-bearing — a plain endsWith("redump.org")
// would also accept look-alikes like "evilredump.org" (mirrors the scraper
// HttpClient's hostMatchesAllowlist).
bool isAllowedRedirectTarget(const QUrl &target) {
  if (target.scheme() != QLatin1String("https")) {
    return false;
  }
  const QString host = target.host();
  return host == QLatin1String("redump.org") || host.endsWith(QLatin1String(".redump.org"));
}

// Blocking GET driven by a local event loop (so the caller runs it on a worker
// thread). Follows redirects (no token/session to preserve, unlike No-Intro),
// but only within https + redump.org: like the No-Intro flow's SEC-01 check
// and the scraper HttpClient's host pinning, a 3xx from a compromised
// upstream must not be able to steer the download — whose body feeds the
// archive extractor — to an arbitrary HTTPS host. UserVerifiedRedirectPolicy
// makes QNAM pause on each hop until redirectAllowed(), so an off-host target
// is aborted before it is ever contacted.
// Returns the owned reply; caller inspects error()/headers/body.
QNetworkReply *blockingGet(QNetworkAccessManager &nam, const QUrl &url, const CancelToken &cancel,
                           const std::function<void(qint64, qint64)> &onProgress) {
  QNetworkRequest req(url);
  req.setRawHeader("User-Agent", kUserAgent);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::UserVerifiedRedirectPolicy);
  // Bound a stalled/slowloris server so the download worker can't hang
  // indefinitely (the cancel token only fires on explicit user cancel).
  // Matches the scraper HttpClient's 30s transfer timeout (Kartend-vu8io).
  req.setTransferTimeout(30000);
  QNetworkReply *reply = nam.get(req);
  QObject::connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &target) {
    if (isAllowedRedirectTarget(target)) {
      emit reply->redirectAllowed();
      return;
    }
    // Off-host/off-scheme redirect: abort before the target is contacted.
    // Surfaces as OperationCanceledError, which the callers report as a
    // download failure.
    reply->abort();
  });

  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  if (onProgress) {
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                     [&onProgress](qint64 rcv, qint64 total) { onProgress(rcv, total); });
  }
  // Response-size cap: abort the reply once received bytes cross the ceiling so
  // a runaway response can't be buffered into memory before readAll() inspects
  // it (Kartend-85zrx). The aborted reply finishes with OperationCanceledError,
  // which the caller surfaces as a download failure.
  QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                   [reply](qint64 rcv, qint64 /*total*/) {
                     if (rcv > kMaxResponseBytes && reply->isRunning()) {
                       reply->abort();
                     }
                   });
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

} // namespace

ErrorUtils::Result<QList<RedumpParse::System>> fetchSystems(const CancelToken &cancel) {
  QNetworkAccessManager nam;
  std::unique_ptr<QNetworkReply> reply(blockingGet(
      nam, QUrl(QStringLiteral("%1datfile/").arg(QLatin1String(kBase))), cancel, nullptr));
  if (cancelled(cancel)) {
    return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                               "RedumpDownload::fetchSystems");
  }
  if (reply->error() != QNetworkReply::NoError) {
    return ErrorContext::error(ErrorCode::UnknownError, "Could not reach redump.org",
                               "RedumpDownload::fetchSystems")
        .withDetails(reply->errorString());
  }
  const QList<RedumpParse::System> systems = RedumpParse::parseSystems(reply->readAll());
  if (systems.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "redump.org returned an unexpected page (no systems list). The "
                               "site may have changed.",
                               "RedumpDownload::fetchSystems");
  }
  return systems;
}

ErrorUtils::Result<DownloadResult> run(const QString &slug, const QString &destDir,
                                       const ProgressFn &progress, const CancelToken &cancel) {
  if (slug.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No system selected",
                               "RedumpDownload::run");
  }
  if (destDir.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No destination folder",
                               "RedumpDownload::run");
  }
  if (progress) {
    progress(Progress{0, -1, QStringLiteral("Downloading…")});
  }

  QNetworkAccessManager nam;
  const QUrl url(QStringLiteral("%1datfile/%2/").arg(QLatin1String(kBase), slug));
  std::unique_ptr<QNetworkReply> reply(
      blockingGet(nam, url, cancel, [&progress](qint64 rcv, qint64 total) {
        if (progress) {
          progress(Progress{rcv, total, QStringLiteral("Downloading…")});
        }
      }));
  if (cancelled(cancel)) {
    return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled", "RedumpDownload::run");
  }
  if (reply->error() != QNetworkReply::NoError) {
    return ErrorContext::error(ErrorCode::UnknownError, "Download failed", "RedumpDownload::run")
        .withDetails(reply->errorString());
  }
  const QByteArray body = reply->readAll();
  const QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
  if (!ctype.contains(QLatin1String("zip"), Qt::CaseInsensitive) &&
      !ctype.contains(QLatin1String("octet-stream"), Qt::CaseInsensitive) &&
      !ctype.contains(QLatin1String("download"), Qt::CaseInsensitive)) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "redump.org returned a page instead of a download (unknown system "
                               "or the site changed).",
                               "RedumpDownload::run")
        .withDetails(ctype);
  }

  QString name = NoIntroParse::filenameFromContentDisposition(
      reply->header(QNetworkRequest::ContentDispositionHeader).toString());
  if (name.isEmpty()) {
    name = QStringLiteral("redump-%1.zip").arg(slug);
  }
  name = QFileInfo(name).fileName(); // strip any smuggled path separators

  QDir().mkpath(destDir);
  const QString zipPath = QDir(destDir).filePath(name);
  QFile out(zipPath);
  if (!out.open(QIODevice::WriteOnly) || out.write(body) != body.size()) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not save the downloaded pack",
                               "RedumpDownload::run")
        .withDetails(out.errorString());
  }
  out.close();
  return DownloadResult{zipPath, name};
}

} // namespace RedumpDownload
