#ifndef SCRAPEDOWNLOADDISPATCHER_H
#define SCRAPEDOWNLOADDISPATCHER_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include "scrapepersistence.h" // Scraper::PendingMediaWrite, Scraper::RescrapeMode
#include "scrapertypes.h"      // Scraper::MediaAsset

class MetadataLookupProvider;

namespace Scraper {

/// Kartend-dpehr: non-UI orchestration of the "download the user-selected
/// media assets" step, lifted out of ScrapeResultDialog::dispatchSelectedDownloads.
///
/// Runs, per selected asset:
///   1. cross-collection shared-asset dedup — if the file already lives in a
///      sibling collection's `_shared/`, read its bytes from disk instead of
///      re-fetching (synchronous);
///   2. per-game CRC short-circuit — when re-scraping FillMissing/UpdateChanged
///      and the destination already exists, append md5/sha1 hash hints to the
///      URL so the server can reply "*OK" and we skip the byte transfer;
///   3. otherwise fire the asset through the provider's throttled
///      fetchMediaBytes.
///
/// Progress + completion are reported via signals so the dialog only drives its
/// progress UI. Unit-testable with a fake MetadataLookupProvider — no QDialog.
///
/// Lifetime (Kartend-5g0g2): each fetch completion is guarded by a
/// QPointer<ScrapeDownloadDispatcher>, so a callback that fires after the
/// dispatcher is destroyed is a no-op. finished() is always the LAST thing a
/// path does (its slot may delete the dialog that owns this dispatcher), so no
/// member is touched after the emit.
class ScrapeDownloadDispatcher : public QObject {
  Q_OBJECT
public:
  struct Config {
    MetadataLookupProvider *provider = nullptr;
    /// Roots searched by ScrapeAssetDedup::findExistingSharedAsset for the
    /// cross-collection `_shared/` dedup hit.
    QStringList sharedSearchPaths;
    /// Empty => no per-game CRC short-circuit; every selected asset is fetched.
    QString rescrapeArtworkDir;
    QString rescrapeBaseName;
    RescrapeMode rescrapeMode = RescrapeMode::Overwrite;
  };

  explicit ScrapeDownloadDispatcher(QObject *parent = nullptr);

  void setConfig(const Config &config) { m_config = config; }

  /// Begin dispatching @p selected. Asynchronous in the general case: emits
  /// progressed() per *network* completion and finished() exactly once when
  /// every asset has resolved. When every asset is a dedup hit the whole thing
  /// resolves synchronously and finished() fires before dispatch() returns.
  /// @p applyTimer is an optional shared stopwatch used only for the
  /// lcScrapeTimings diagnostics; pass null to skip the timing logs.
  void dispatch(const QList<MediaAsset> &selected,
                const std::shared_ptr<QElapsedTimer> &applyTimer = nullptr);

signals:
  /// Emitted after each *network* completion (dedup hits resolve silently,
  /// mirroring the dialog's original cadence). completed == total - pending.
  void progressed(int completed, int total, qint64 bytesSoFar);
  /// Emitted exactly once when every selected asset has resolved. Carries the
  /// payloads to persist, the running byte total, and the types (lowercase)
  /// whose fetch errored or returned no bytes. The failed list feeds
  /// ScrapedItem::mediaFetchFailedThisRun so the mediaAbsent merge doesn't
  /// prune a marker as "satisfied" when the download actually failed — only
  /// user-deselected assets (never dispatched, so never in this list) prune
  /// (Kartend-jjyst.14). Dedup hits and hash short-circuits are successes.
  void finished(const QList<PendingMediaWrite> &downloads, qint64 totalBytes,
                const QStringList &failedTypes);

private:
  /// Emit finished() exactly once, when every asset has resolved AND the
  /// dispatch loop has finished iterating. The loop guard matters when the
  /// provider invokes callbacks synchronously (tests, or a future cached
  /// provider): without it the last in-loop completion and the post-loop
  /// all-dedup check would both fire. Sets the once-flag before the emit so a
  /// re-entrant slot can't double-fire; touches no member after the emit (the
  /// slot may delete this dispatcher — Kartend-5g0g2).
  void emitFinishedIfDone();

  Config m_config;
  // Per-dispatch accumulators. One dispatch runs at a time (the dialog disables
  // Apply for the duration of a download run), so a single set suffices.
  QList<PendingMediaWrite> m_downloads;
  /// Lowercased asset types whose fetch errored or came back empty — the
  /// finished() payload the caller stamps onto mediaFetchFailedThisRun.
  QStringList m_failedTypes;
  qint64 m_bytes = 0;
  int m_total = 0;
  int m_pending = 0;
  bool m_loopDone = false;
  bool m_finishedEmitted = false;
};

} // namespace Scraper

#endif // SCRAPEDOWNLOADDISPATCHER_H
