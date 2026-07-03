#ifndef SCRAPEPENDINGSTATE_H
#define SCRAPEPENDINGSTATE_H

#include <QByteArray>
#include <QString>

#include "scraperservice.h"

/// Serialization of the resumable scrape snapshot (pending-scrape.json),
/// extracted from ScraperService: pure JSON <-> ScraperService::PendingState
/// conversion plus the atomic file write. The service keeps the POLICY —
/// ownership gating, debounce, when to persist/clear, the live-instance
/// check — and delegates the byte-level work here.
///
/// Versioning: v1 = pre-entity, path-only queues. v2 added entity-job
/// descriptors, the not_found bucket, and the re-scrape-failed state
/// (failed_items / sidecar_failures / quota_exhausted). New builds still
/// ACCEPT v1 files on upgrade (they have no entity jobs to lose); an OLD
/// build reading a v2 file trips its own version check and discards
/// explicitly rather than silently dropping work it can't represent.
namespace Scraper {
namespace ScrapePendingState {

/// Canonical on-disk location: `<AppConfigLocation>/pending-scrape.json`.
[[nodiscard]] QString filePath();
/// Sibling ownership-lock path (`pending-scrape.json.lock`).
[[nodiscard]] QString lockFilePath();

/// Serialize @p snap to the compact v2 JSON document. Queue entries that are
/// neither entity jobs nor carry remaining items are dropped (nothing to
/// resume for them).
[[nodiscard]] QByteArray serialize(const ScraperService::PendingState &snap);

/// Parse @p bytes back into a PendingState. Returns a default (invalid)
/// state — with a logged warning — on parse errors or unsupported versions;
/// otherwise hasState reflects whether any resumable queue survived.
[[nodiscard]] ScraperService::PendingState deserialize(const QByteArray &bytes);

/// Atomically write @p bytes to @p path (QSaveFile: temp sibling + rename),
/// creating the parent directory. A crash mid-write can never leave a
/// truncated file — a partial temp fails deserialize()'s JSON parse, which
/// silently suppresses the resume prompt on next launch. Logs on failure.
bool atomicWrite(const QString &path, const QByteArray &bytes);

} // namespace ScrapePendingState
} // namespace Scraper

#endif // SCRAPEPENDINGSTATE_H
