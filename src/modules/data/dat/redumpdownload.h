#ifndef REDUMPDOWNLOAD_H
#define REDUMPDOWNLOAD_H

#include <atomic>
#include <functional>
#include <memory>

#include <QList>
#include <QString>

#include "errorutils.h"
#include "nointrodownloader.h" // Progress / ProgressFn / DownloadResult / CancelToken
#include "redumpparse.h"

/// In-app DAT download from redump.org (Kartend-m6qsb.23). Far simpler than
/// the No-Intro flow: a single GET per system, no form/cookie/token. Reuses
/// NoIntroDownload's Progress/DownloadResult value types and extractDatsTo()
/// for unpacking; only the request shape differs.
///
/// Verified flow (2026-06-13): GET https://redump.org/datfile/<slug>/ →
/// application/zip containing one Logiqx .dat. The systems list comes from a
/// single GET of /datfile/ (parsed by RedumpParse). No combined "all" pack —
/// an all-systems download iterates slugs.
namespace RedumpDownload {

using NoIntroDownload::CancelToken;
using NoIntroDownload::DownloadResult;
using NoIntroDownload::Progress;
using NoIntroDownload::ProgressFn;

/// Fetch + parse the redump.org systems list (blocking; run off the UI
/// thread). Empty/error when the index can't be reached or parsed.
[[nodiscard]] ErrorUtils::Result<QList<RedumpParse::System>>
fetchSystems(const CancelToken &cancel);

/// Download one system's DAT pack zip into `destDir`. `slug` is a
/// RedumpParse::System::slug. Blocking; run off the UI thread.
[[nodiscard]] ErrorUtils::Result<DownloadResult> run(const QString &slug, const QString &destDir,
                                                     const ProgressFn &progress,
                                                     const CancelToken &cancel);

} // namespace RedumpDownload

#endif // REDUMPDOWNLOAD_H
