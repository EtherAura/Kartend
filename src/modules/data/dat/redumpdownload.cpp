// redump.org DAT downloader — one GET per system, no form/cookie/token.
#include "redumpdownload.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "dathttp.h"
#include "nointroparse.h" // filenameFromContentDisposition

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace RedumpDownload {

namespace {

constexpr char kBase[] = "https://redump.org/";

using DatHttp::cancelled;

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

// Blocking GET via the shared DatHttp event loop. Follows redirects (no
// token/session to preserve, unlike No-Intro), but only within https +
// redump.org: like the No-Intro flow's SEC-01 check and the scraper
// HttpClient's host pinning, a 3xx from a compromised upstream must not be
// able to steer the download — whose body feeds the archive extractor — to an
// arbitrary HTTPS host. An off-host/off-scheme target is aborted before it is
// ever contacted.
// Returns the owned reply; caller inspects error()/headers/body.
QNetworkReply *blockingGet(QNetworkAccessManager &nam, const QUrl &url, const CancelToken &cancel,
                           const std::function<void(qint64, qint64)> &onProgress) {
  return DatHttp::blockingRequest(nam, url, QByteArray(), QString(), cancel, onProgress,
                                  isAllowedRedirectTarget);
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
