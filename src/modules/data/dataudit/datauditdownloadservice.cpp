#include "datauditdownloadservice.h"

#include <utility>

#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <QTemporaryDir>

#include "datlookup.h"      // DatLookup::probeHeaderFromFile
#include "errorutils.h"     // ErrorUtils::Result
#include "nointroparse.h"   // NoIntroParse::DailySet
#include "redumpdownload.h" // RedumpDownload::run

DatAuditDownloadService::DatAuditDownloadService(ProvenanceAccess access)
    : m_access(std::move(access)) {}

DatAuditDownloadService::Outcome
DatAuditDownloadService::performDownload(const Request &req,
                                         const NoIntroDownload::CancelToken &cancel,
                                         const NoIntroDownload::ProgressFn &progress) {
  // Download to a temp dir → extract DATs into the library, so a failed or
  // cancelled run never leaves a partial zip behind. Shared by the manual
  // Download page and the "check for updates" re-download (Kartend-m6qsb.31).
  QTemporaryDir tmp;
  const QString dest = tmp.isValid() ? tmp.path() : req.libraryDir;
  NoIntroDownload::Options niOpts = req.niOpts;
  ErrorUtils::Result<NoIntroDownload::DownloadResult> dl =
      req.redump ? RedumpDownload::run(req.redumpSlug, dest, progress, cancel) : [&] {
        niOpts.destDir = dest;
        return NoIntroDownload::run(niOpts, progress, cancel);
      }();
  if (dl.isError()) {
    Outcome fail;
    fail.error = dl.error().message;
    return fail;
  }
  auto ex = NoIntroDownload::extractDatsTo(dl.value().zipPath, req.libraryDir, cancel);
  if (ex.isError()) {
    Outcome fail;
    fail.error = ex.error().message;
    fail.packName = dl.value().packName;
    return fail;
  }
  Outcome out;
  out.ok = true;
  out.packName = dl.value().packName;
  out.datPaths = ex.value();
  out.datCount = static_cast<int>(out.datPaths.size());
  out.source = req.redump ? QStringLiteral("redump") : QStringLiteral("nointro");
  out.slug = req.redump ? req.redumpSlug : QString();
  out.systemId = req.redump ? 0 : niOpts.systemId;
  // Redump has no cheap version endpoint — its revision is the DAT header
  // version, read from the freshly-extracted file; No-Intro uses the pack date
  // (Kartend-m6qsb.26).
  out.version = req.redump ? (out.datPaths.isEmpty()
                                  ? QString()
                                  : DatLookup::probeHeaderFromFile(out.datPaths.first()).version)
                           : req.packDate;
  return out;
}

QFuture<DatAuditDownloadService::Outcome>
DatAuditDownloadService::startDownload(const Request &req,
                                       const NoIntroDownload::CancelToken &cancel,
                                       const NoIntroDownload::ProgressFn &progress) const {
  return QtConcurrent::run(
      [req, cancel, progress] { return performDownload(req, cancel, progress); });
}

QList<DatLibraryState::Provenance> DatAuditDownloadService::trackedProvenance() const {
  return m_access.loadAll ? m_access.loadAll() : QList<DatLibraryState::Provenance>{};
}

QFuture<DatAuditDownloadService::UpdateResult>
DatAuditDownloadService::checkUpdates(const QList<DatLibraryState::Provenance> &tracked,
                                      const QString &libraryDir,
                                      const NoIntroDownload::CancelToken &cancel) const {
  return QtConcurrent::run([tracked, libraryDir, cancel]() -> UpdateResult {
    UpdateResult r;
    r.checked = static_cast<int>(tracked.size());
    // Detect: ask each source its current revision (No-Intro pack date is cheap;
    // Redump has no version endpoint, so this downloads the small DAT to read
    // its header — it's re-downloaded in the apply step below, which is fine for
    // single-system Redump DATs).
    const auto fetchLatest = [&cancel](const DatLibraryState::Provenance &p) -> QString {
      if (cancel && cancel->load()) {
        return QString();
      }
      if (p.source == QLatin1String("nointro") && p.systemId > 0) {
        auto f = NoIntroDownload::fetchDailyForm(p.systemId, cancel);
        return f.isOk() ? f.value().packDate : QString();
      }
      if (p.source == QLatin1String("redump") && !p.slug.isEmpty()) {
        QTemporaryDir tmp;
        auto dl = RedumpDownload::run(p.slug, tmp.path(), nullptr, cancel);
        if (dl.isError()) {
          return QString();
        }
        auto ex = NoIntroDownload::extractDatsTo(dl.value().zipPath, tmp.path(), cancel);
        if (ex.isError() || ex.value().isEmpty()) {
          return QString();
        }
        return DatLookup::probeHeaderFromFile(ex.value().first()).version;
      }
      return QString();
    };
    const QList<DatLibraryState::Provenance> outdated =
        DatLibraryState::outdatedAmong(tracked, fetchLatest);
    // Apply: re-download each outdated catalogue into the library.
    for (const DatLibraryState::Provenance &p : outdated) {
      if (cancel && cancel->load()) {
        break;
      }
      Request req;
      req.redump = p.source == QLatin1String("redump");
      req.redumpSlug = p.slug;
      req.libraryDir = libraryDir;
      if (!req.redump) {
        req.niOpts.systemId = p.systemId;
        req.niOpts.datType = QStringLiteral("dat");
        // The original set selection isn't stored in provenance — re-fetch the
        // form and use its default-checked sets.
        if (auto f = NoIntroDownload::fetchDailyForm(p.systemId, cancel); f.isOk()) {
          req.packDate = f.value().packDate;
          for (const NoIntroParse::DailySet &s : f.value().sets) {
            if (s.checkedByDefault) {
              req.niOpts.selectedSets.append(s.field);
            }
          }
        } else {
          req.packDate = p.version;
        }
      }
      r.downloads.append(performDownload(req, cancel, nullptr));
    }
    return r;
  });
}

void DatAuditDownloadService::recordProvenance(const Outcome &o) const {
  // Record where each DAT came from so "check for updates" can later ask the
  // source for a newer revision (Kartend-m6qsb.26). The actual DB write goes
  // through the injected accessor so this service never opens a QSqlDatabase.
  if (o.datPaths.isEmpty() || !m_access.record) {
    return;
  }
  for (const QString &path : o.datPaths) {
    DatLibraryState::Provenance pr;
    const QString canonical = QFileInfo(path).canonicalFilePath();
    pr.canonicalPath = canonical.isEmpty() ? path : canonical;
    pr.source = o.source;
    pr.slug = o.slug;
    pr.systemId = o.systemId;
    pr.version = o.version;
    m_access.record(pr);
  }
}
