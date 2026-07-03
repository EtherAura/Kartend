#ifndef SCRAPELOCK_H
#define SCRAPELOCK_H

#include <memory>

#include <QString>

class QLockFile;

namespace Scraper {

/// RAII ownership marker for the pending-scrape state file, extracted from
/// ScraperService. A held lock tells a second Kartend instance that the
/// scrape whose state file sits next to the lock is LIVE — it must neither
/// offer to resume it nor write over it. Time-based staleness is disabled
/// (a scrape legitimately runs for hours); QLockFile still reclaims a lock
/// whose owning PID is dead, which is exactly the crashed-owner case a
/// resume prompt exists for.
class ScrapeLock {
public:
  ScrapeLock() = default;
  ~ScrapeLock();
  ScrapeLock(const ScrapeLock &) = delete;
  ScrapeLock &operator=(const ScrapeLock &) = delete;

  /// Take (or confirm) the lock at @p lockFilePath. Returns true when
  /// acquired or already held by this object; false when another live
  /// process owns it.
  [[nodiscard]] bool acquire(const QString &lockFilePath);

  /// Drop the lock and remove its on-disk file. Safe when not held.
  void release();

  [[nodiscard]] bool isHeld() const;

  /// True when the lock at @p lockFilePath belongs to a still-running
  /// process — i.e. the sibling state file is a LIVE scrape, not an
  /// interrupted one. False when nobody holds it or the previous owner
  /// crashed (stale lock, reclaimed by the probe and dropped again).
  [[nodiscard]] static bool ownedByLiveInstance(const QString &lockFilePath);

private:
  std::unique_ptr<QLockFile> m_lock;
};

} // namespace Scraper

#endif // SCRAPELOCK_H
