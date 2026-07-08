// Network orchestrator for the No-Intro daily-download flow. Parsing lives in
// NoIntroParse; this file is the request dance + cookie jar + cancellation.
#include "nointrodownloader.h"

#include "archivesafety.h"
#include "dathttp.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace NoIntroDownload {

namespace {

constexpr char kBase[] = "https://datomatic.no-intro.org/";

// Decompressed-size ceiling for the pack extraction in extractDatsTo (Kartend
// audit SEC-02): the extractor runs count-free, so a malicious pack could
// otherwise inflate to fill the temp volume. 2 GiB is far above any real DAT
// pack (XML, ~tens of MiB) while bounding a bomb; further capped by free space
// minus a margin, mirroring RomHasher / LaunchManager archive extraction.
constexpr qint64 kMaxExtractedDatBytes = 2LL * 1024 * 1024 * 1024;
constexpr qint64 kExtractFreeSpaceMargin = 256LL * 1024 * 1024;

using DatHttp::cancelled;

QString dailyUrl(int systemId) {
  return QStringLiteral("%1index.php?page=download&op=daily&s=%2")
      .arg(QLatin1String(kBase))
      .arg(systemId);
}

// One blocking request via the shared DatHttp event loop. `post` empty => GET.
// Returns the reply (caller owns it) so headers/redirect/body are all
// inspectable. Manual redirects: step 2's 302 carries the download id we must
// read, so no redirect is followed.
QNetworkReply *blockingRequest(QNetworkAccessManager &nam, const QUrl &url, const QByteArray &post,
                               const QString &referer, const CancelToken &cancel,
                               const std::function<void(qint64, qint64)> &onProgress) {
  return DatHttp::blockingRequest(nam, url, post, referer, cancel, onProgress,
                                  /*allowRedirect=*/nullptr);
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
    const QUrl resolvedUrl = QUrl(dUrl).resolved(redirect);
    // Security: only follow a redirect that stays on https + the no-intro.org
    // host. An absolute redirect from a hostile/MITM response could otherwise
    // point at http://, file://, or an internal host (Kartend audit SEC-01).
    if (resolvedUrl.scheme() != QLatin1String("https") ||
        !(resolvedUrl.host() == QLatin1String("no-intro.org") ||
          resolvedUrl.host().endsWith(QLatin1String(".no-intro.org")))) {
      return ErrorContext::error(ErrorCode::InvalidArgument,
                                 "DAT-o-MATIC returned an unexpected redirect target",
                                 "NoIntroDownload::run")
          .withDetails(resolvedUrl.toString());
    }
    managerUrl = resolvedUrl.toString();
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
  // bsdtar first (libarchive's extract defaults refuse ".." and writes
  // through symlinks); unzip is dropped — it recreates symlink entries and
  // then writes through them, the zip-slip-via-symlink primitive the safety
  // scan below exists to stop.
  QString tool;
  for (const QString &cmd : {QStringLiteral("bsdtar"), QStringLiteral("7z")}) {
    if (!QStandardPaths::findExecutable(cmd).isEmpty()) {
      tool = cmd;
      break;
    }
  }
  if (tool.isEmpty()) {
    return ErrorContext::error(
        ErrorCode::FileNotFound,
        "No archive tool found to unpack the DAT pack (install bsdtar or 7z)",
        "NoIntroDownload::extractDatsTo");
  }

  // Refuse packs whose listing shows symlink/hardlink entries or path-escape
  // attempts before anything is written: the .dat copy-out below walks with
  // NoSymLinks, but a write routed THROUGH a symlink entry during extraction
  // lands outside the temp dir where that walk never looks.
  if (const auto scan = ArchiveSafety::scanArchiveEntries(abs); scan.isError()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "DAT pack failed the pre-extraction safety scan",
                               "NoIntroDownload::extractDatsTo")
        .withDetails(scan.error().userFacingSummary());
  }

  QTemporaryDir tmp;
  if (!tmp.isValid()) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not create a temporary folder",
                               "NoIntroDownload::extractDatsTo");
  }
  QStringList args;
  if (tool == QLatin1String("7z")) {
    args << QStringLiteral("x") << QStringLiteral("-y") << abs;
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
  // Zip-bomb guard (Kartend audit SEC-02): the extractor above runs count-free,
  // so cap decompressed output at min(absolute cap, free space - margin) and
  // kill the process if a malicious pack inflates past it.
  const qint64 freeAvail = QStorageInfo(tmp.path()).bytesAvailable();
  qint64 extractCeiling = kMaxExtractedDatBytes;
  if (freeAvail >= 0) {
    extractCeiling = qMin<qint64>(extractCeiling, freeAvail - kExtractFreeSpaceMargin);
  }
  const auto extractedBytes = [&tmp]() -> qint64 {
    qint64 total = 0;
    QDirIterator walk(tmp.path(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
      walk.next();
      total += walk.fileInfo().size();
    }
    return total;
  };

  // Poll so a cancel — or a zip bomb crossing the ceiling — kills the extractor
  // instead of running a large unpack out.
  while (!proc.waitForFinished(200)) {
    if (cancelled(cancel)) {
      proc.kill();
      proc.waitForFinished(2000);
      return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                                 "NoIntroDownload::extractDatsTo");
    }
    if (extractedBytes() > extractCeiling) {
      proc.kill();
      proc.waitForFinished(2000);
      return ErrorContext::error(
          ErrorCode::InvalidArgument,
          "DAT pack exceeded the decompressed-size limit during extraction (possible zip bomb)",
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
