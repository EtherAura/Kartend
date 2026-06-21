// Network orchestrator for the No-Intro daily-download flow. Parsing lives in
// NoIntroParse; this file is the request dance + cookie jar + cancellation.
#include "nointrodownloader.h"

#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace NoIntroDownload {

namespace {

constexpr char kBase[] = "https://datomatic.no-intro.org/";
// A real desktop UA: the site serves the automation-friendly path to browsers
// and we are, functionally, performing the exact clicks a user would.
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

QString dailyUrl(int systemId) {
  return QStringLiteral("%1index.php?page=download&op=daily&s=%2")
      .arg(QLatin1String(kBase))
      .arg(systemId);
}

// Issue one request and block on a local event loop until it finishes (or the
// cancel token trips, which aborts the reply). `post` empty => GET. Returns the
// reply (caller owns it) so headers/redirect/body are all inspectable.
// Progress on the reply is forwarded only when onProgress is set.
QNetworkReply *blockingRequest(QNetworkAccessManager &nam, const QUrl &url, const QByteArray &post,
                               const QString &referer, const CancelToken &cancel,
                               const std::function<void(qint64, qint64)> &onProgress) {
  QNetworkRequest req(url);
  req.setRawHeader("User-Agent", kUserAgent);
  // Bound a stalled/slowloris server so the download worker can't hang
  // indefinitely (the cancel token only fires on explicit user cancel).
  // Matches the scraper HttpClient's 30s transfer timeout (Kartend-vu8io).
  req.setTransferTimeout(30000);
  if (!referer.isEmpty()) {
    req.setRawHeader("Referer", referer.toUtf8());
  }
  // Manual redirects: step 2's 302 carries the download id we must read.
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

  QNetworkReply *reply = nullptr;
  if (post.isNull()) {
    reply = nam.get(req);
  } else {
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    reply = nam.post(req, post);
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

QString formEncode(const QList<QPair<QString, QString>> &fields) {
  QUrlQuery q;
  for (const auto &kv : fields) {
    q.addQueryItem(kv.first, kv.second);
  }
  return q.toString(QUrl::FullyEncoded);
}

} // namespace

ErrorUtils::Result<NoIntroParse::DailyForm> fetchDailyForm(int systemId,
                                                           const CancelToken &cancel) {
  if (systemId <= 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No system selected",
                               "NoIntroDownload::fetchDailyForm");
  }
  QNetworkAccessManager nam;
  nam.setCookieJar(new QNetworkCookieJar(&nam));
  std::unique_ptr<QNetworkReply> reply(
      blockingRequest(nam, QUrl(dailyUrl(systemId)), QByteArray(), QString(), cancel, nullptr));
  if (cancelled(cancel)) {
    return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                               "NoIntroDownload::fetchDailyForm");
  }
  if (reply->error() != QNetworkReply::NoError) {
    return ErrorContext::error(ErrorCode::UnknownError, "Could not reach DAT-o-MATIC",
                               "NoIntroDownload::fetchDailyForm")
        .withDetails(reply->errorString());
  }
  const NoIntroParse::DailyForm form = NoIntroParse::parseDailyPage(reply->readAll());
  if (!form.valid) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "DAT-o-MATIC returned an unexpected page (no daily download form). "
                               "The site may have changed or be temporarily blocking automated "
                               "access.",
                               "NoIntroDownload::fetchDailyForm");
  }
  return form;
}

ErrorUtils::Result<DownloadResult> run(const Options &opts, const ProgressFn &progress,
                                       const CancelToken &cancel) {
  if (opts.systemId <= 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No system selected",
                               "NoIntroDownload::run");
  }
  if (opts.destDir.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No destination folder",
                               "NoIntroDownload::run");
  }
  const auto tick = [&progress](const QString &phase, qint64 rcv, qint64 total) {
    if (progress) {
      progress(Progress{rcv, total, phase});
    }
  };

  QNetworkAccessManager nam;
  nam.setCookieJar(new QNetworkCookieJar(&nam));
  const QString dUrl = dailyUrl(opts.systemId);

  // Step 1 — daily page (also seeds the session cookie).
  tick(QStringLiteral("Requesting…"), 0, -1);
  NoIntroParse::DailyForm form;
  {
    std::unique_ptr<QNetworkReply> r(
        blockingRequest(nam, QUrl(dUrl), QByteArray(), QString(), cancel, nullptr));
    if (cancelled(cancel)) {
      return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                                 "NoIntroDownload::run");
    }
    if (r->error() != QNetworkReply::NoError) {
      return ErrorContext::error(ErrorCode::UnknownError, "Could not reach DAT-o-MATIC",
                                 "NoIntroDownload::run")
          .withDetails(r->errorString());
    }
    form = NoIntroParse::parseDailyPage(r->readAll());
  }
  if (!form.valid) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "DAT-o-MATIC returned an unexpected page (no daily download form).",
                               "NoIntroDownload::run");
  }

  // Step 2 — POST the selection; expect a 302 to the manager page.
  QList<QPair<QString, QString>> fields;
  fields.append(
      {QStringLiteral("dat_type"), opts.datType.isEmpty() ? form.defaultDatType : opts.datType});
  const QStringList sets = !opts.selectedSets.isEmpty() ? opts.selectedSets : [&form] {
    QStringList d;
    for (const auto &s : form.sets) {
      if (s.checkedByDefault) {
        d.append(s.field);
      }
    }
    return d;
  }();
  for (const QString &set : sets) {
    fields.append({set, QStringLiteral("Ok")});
  }
  fields.append({form.requestField, QStringLiteral("Request")});

  tick(QStringLiteral("Preparing…"), 0, -1);
  QString managerUrl;
  {
    std::unique_ptr<QNetworkReply> r(
        blockingRequest(nam, QUrl(dUrl), formEncode(fields).toUtf8(), dUrl, cancel, nullptr));
    if (cancelled(cancel)) {
      return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                                 "NoIntroDownload::run");
    }
    if (r->error() != QNetworkReply::NoError) {
      return ErrorContext::error(ErrorCode::UnknownError, "DAT-o-MATIC rejected the request",
                                 "NoIntroDownload::run")
          .withDetails(r->errorString());
    }
    const QUrl redirect = r->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirect.isEmpty()) {
      return ErrorContext::error(ErrorCode::InvalidArgument,
                                 "DAT-o-MATIC did not return a prepared download (no pack "
                                 "available for this selection, or the site changed).",
                                 "NoIntroDownload::run");
    }
    managerUrl = QUrl(dUrl).resolved(redirect).toString();
  }

  // Step 3 — confirm page; parse the one-time token.
  QString token;
  {
    std::unique_ptr<QNetworkReply> r(
        blockingRequest(nam, QUrl(managerUrl), QByteArray(), dUrl, cancel, nullptr));
    if (cancelled(cancel)) {
      return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                                 "NoIntroDownload::run");
    }
    if (r->error() != QNetworkReply::NoError) {
      return ErrorContext::error(ErrorCode::UnknownError, "Could not open the prepared download",
                                 "NoIntroDownload::run")
          .withDetails(r->errorString());
    }
    token = NoIntroParse::parseConfirmToken(r->readAll());
  }
  if (token.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "DAT-o-MATIC did not offer a download button (the prepared link may "
                               "have expired or the site changed).",
                               "NoIntroDownload::run");
  }

  // Step 4 — POST the token; stream the application/zip body to disk.
  tick(QStringLiteral("Downloading…"), 0, -1);
  std::unique_ptr<QNetworkReply> r(blockingRequest(
      nam, QUrl(managerUrl), formEncode({{token, QStringLiteral("Download!!")}}).toUtf8(),
      managerUrl, cancel,
      [&tick](qint64 rcv, qint64 total) { tick(QStringLiteral("Downloading…"), rcv, total); }));
  if (cancelled(cancel)) {
    return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled", "NoIntroDownload::run");
  }
  if (r->error() != QNetworkReply::NoError) {
    return ErrorContext::error(ErrorCode::UnknownError, "Download failed", "NoIntroDownload::run")
        .withDetails(r->errorString());
  }
  const QByteArray body = r->readAll();
  const QString ctype = r->header(QNetworkRequest::ContentTypeHeader).toString();
  if (!ctype.contains(QLatin1String("zip"), Qt::CaseInsensitive) &&
      !ctype.contains(QLatin1String("octet-stream"), Qt::CaseInsensitive)) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "DAT-o-MATIC returned a page instead of a download (rate-limited or "
                               "the link expired). Try again in a moment.",
                               "NoIntroDownload::run")
        .withDetails(ctype);
  }

  QString name = NoIntroParse::filenameFromContentDisposition(
      r->header(QNetworkRequest::ContentDispositionHeader).toString());
  if (name.isEmpty()) {
    name = QStringLiteral("no-intro-pack-%1.zip").arg(form.packDate);
  }
  // Strip any path separators a hostile header could smuggle in.
  name = QFileInfo(name).fileName();

  QDir().mkpath(opts.destDir);
  const QString zipPath = QDir(opts.destDir).filePath(name);
  QFile out(zipPath);
  if (!out.open(QIODevice::WriteOnly)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not save the downloaded pack",
                               "NoIntroDownload::run")
        .withDetails(out.errorString());
  }
  if (out.write(body) != body.size()) {
    out.close();
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not write the downloaded pack",
                               "NoIntroDownload::run");
  }
  out.close();

  return DownloadResult{zipPath, name};
}

ErrorUtils::Result<QStringList> extractDatsTo(const QString &zipPath, const QString &destDir,
                                              const CancelToken &cancel) {
  if (zipPath.isEmpty() || !QFileInfo(zipPath).isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Pack file does not exist",
                               "NoIntroDownload::extractDatsTo")
        .withDetails(zipPath);
  }
  if (destDir.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No destination folder",
                               "NoIntroDownload::extractDatsTo");
  }

  // Pick the first available extractor (same set the auditor uses).
  struct Tool {
    QString cmd;
    QStringList args; // before the temp-dir-cwd extraction
  };
  const QString abs = QFileInfo(zipPath).absoluteFilePath();
  QString tool;
  for (const QString &cmd :
       {QStringLiteral("7z"), QStringLiteral("unzip"), QStringLiteral("bsdtar")}) {
    if (!QStandardPaths::findExecutable(cmd).isEmpty()) {
      tool = cmd;
      break;
    }
  }
  if (tool.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "No archive tool found to unpack the DAT pack (install 7z or unzip)",
                               "NoIntroDownload::extractDatsTo");
  }

  QTemporaryDir tmp;
  if (!tmp.isValid()) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not create a temporary folder",
                               "NoIntroDownload::extractDatsTo");
  }
  QStringList args;
  if (tool == QLatin1String("7z")) {
    args << QStringLiteral("x") << QStringLiteral("-y") << abs;
  } else if (tool == QLatin1String("unzip")) {
    args << QStringLiteral("-o") << abs;
  } else { // bsdtar
    args << QStringLiteral("-xf") << abs;
  }
  QProcess proc;
  proc.setWorkingDirectory(tmp.path());
  proc.start(tool, args);
  if (!proc.waitForStarted(10000)) {
    return ErrorContext::error(ErrorCode::UnknownError, "Could not start the archive tool",
                               "NoIntroDownload::extractDatsTo");
  }
  // Poll so a cancel kills the extractor instead of running a ~100MB unpack out.
  while (!proc.waitForFinished(200)) {
    if (cancelled(cancel)) {
      proc.kill();
      proc.waitForFinished(2000);
      return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                                 "NoIntroDownload::extractDatsTo");
    }
  }
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    return ErrorContext::error(ErrorCode::UnknownError, "Failed to unpack the DAT pack",
                               "NoIntroDownload::extractDatsTo")
        .withDetails(QString::fromUtf8(proc.readAllStandardError()).left(300));
  }

  QDir().mkpath(destDir);
  QStringList extracted;
  QDirIterator it(tmp.path(), {QStringLiteral("*.dat")}, QDir::Files | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString src = it.next();
    const QString dst = QDir(destDir).filePath(QFileInfo(src).fileName());
    if (QFile::exists(dst)) {
      QFile::remove(dst); // refresh in place
    }
    if (QFile::rename(src, dst) || QFile::copy(src, dst)) {
      extracted.append(dst);
    }
  }
  if (extracted.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "The downloaded pack contained no DAT files",
                               "NoIntroDownload::extractDatsTo")
        .withDetails(zipPath);
  }
  return extracted;
}

} // namespace NoIntroDownload
