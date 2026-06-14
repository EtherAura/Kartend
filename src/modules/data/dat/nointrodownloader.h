#ifndef NOINTRODOWNLOADER_H
#define NOINTRODOWNLOADER_H

#include <atomic>
#include <functional>
#include <memory>

#include <QString>
#include <QStringList>

#include "errorutils.h"
#include "nointroparse.h"

/// Drives the No-Intro DAT-o-MATIC daily-download flow end to end
/// (Kartend-m6qsb.16) and hands back a pack zip on disk. All page parsing is
/// delegated to NoIntroParse; this layer owns only the request sequence,
/// cookie jar, manual redirect handling, progress, and cancellation.
///
/// Blocking by design: run() drives a local QEventLoop, so the caller must
/// invoke it on a worker thread (the audit window uses QtConcurrent::run).
/// The site is automation-hostile, so the flow makes the minimum requests,
/// sends a real User-Agent + Referer, and surfaces every failure mode
/// (page changed, no pack, network) as a Result error instead of guessing.
namespace NoIntroDownload {

using CancelToken = std::shared_ptr<std::atomic<bool>>;

/// Progress over the final pack transfer. received/total are bytes;
/// total is -1 until the server declares Content-Length.
struct Progress {
  qint64 received = 0;
  qint64 total = -1;
  QString phase; ///< "Requesting…" / "Preparing…" / "Downloading…"
};
using ProgressFn = std::function<void(const Progress &)>;

/// What to fetch. `selectedSets` are NoIntroParse::DailySet::field names; when
/// empty the downloader uses the page's default-checked sets.
struct Options {
  int systemId = 0;
  QString datType = QStringLiteral("dat");
  QStringList selectedSets;
  QString destDir; ///< Folder to write the pack zip into (e.g. a temp dir).
};

/// The downloaded pack.
struct DownloadResult {
  QString zipPath;  ///< Absolute path of the saved pack zip.
  QString packName; ///< Server-declared filename (Content-Disposition).
};

/// Probe step 1 only: fetch the daily page for `systemId` and parse the form,
/// so the UI can show the available sets / pack date before committing to a
/// download. Blocking; run off the UI thread.
[[nodiscard]] ErrorUtils::Result<NoIntroParse::DailyForm> fetchDailyForm(int systemId,
                                                                         const CancelToken &cancel);

/// Run the full 4-step flow and return the saved pack. `progress` is optional;
/// `cancel` aborts promptly between and during requests.
[[nodiscard]] ErrorUtils::Result<DownloadResult>
run(const Options &opts, const ProgressFn &progress, const CancelToken &cancel);

/// Extract every `*.dat` from a downloaded pack zip into `destDir` (flattened
/// — No-Intro packs nest the DATs one folder deep). Returns the destination
/// paths of the extracted DATs. Shells out to 7z / unzip / bsdtar (whichever
/// is on PATH), mirroring the auditor's archive handling; returns an error
/// when no extractor is available or the zip yields no DATs. Existing files of
/// the same name are overwritten (a re-download is a refresh).
[[nodiscard]] ErrorUtils::Result<QStringList>
extractDatsTo(const QString &zipPath, const QString &destDir, const CancelToken &cancel);

} // namespace NoIntroDownload

#endif // NOINTRODOWNLOADER_H
