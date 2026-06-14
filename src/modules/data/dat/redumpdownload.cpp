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

bool cancelled(const CancelToken &c) {
  return c && c->load(std::memory_order_relaxed);
}

// Blocking GET driven by a local event loop (so the caller runs it on a worker
// thread). Follows redirects (no token/session to preserve, unlike No-Intro).
// Returns the owned reply; caller inspects error()/headers/body.
QNetworkReply *blockingGet(QNetworkAccessManager &nam, const QUrl &url, const CancelToken &cancel,
                           const std::function<void(qint64, qint64)> &onProgress) {
  QNetworkRequest req(url);
  req.setRawHeader("User-Agent", kUserAgent);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply *reply = nam.get(req);

  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  if (onProgress) {
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                     [&onProgress](qint64 rcv, qint64 total) { onProgress(rcv, total); });
  }
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
