#include "scrapelock.h"

#include <QDir>
#include <QFileInfo>
#include <QLockFile>

namespace Scraper {

ScrapeLock::~ScrapeLock() {
  release();
}

bool ScrapeLock::acquire(const QString &lockFilePath) {
  if (m_lock && m_lock->isLocked()) return true;
  QDir().mkpath(QFileInfo(lockFilePath).absolutePath());
  m_lock = std::make_unique<QLockFile>(lockFilePath);
  // Disable time-based staleness: a scrape legitimately runs for minutes or
  // hours, so the lock file's age says nothing. QLockFile still treats a
  // lock whose owning PID is no longer running as stale (PID + process-name
  // check) and reclaims it — exactly the crashed-owner case we *want* to
  // resume from.
  m_lock->setStaleLockTime(0);
  if (m_lock->tryLock(0)) return true;
  m_lock.reset();
  return false;
}

void ScrapeLock::release() {
  if (m_lock) {
    m_lock->unlock(); // also removes the .lock file
    m_lock.reset();
  }
}

bool ScrapeLock::isHeld() const {
  return m_lock && m_lock->isLocked();
}

bool ScrapeLock::ownedByLiveInstance(const QString &lockFilePath) {
  QLockFile probe(lockFilePath);
  probe.setStaleLockTime(0);
  if (probe.tryLock(0)) {
    // Acquired it ourselves → no live owner (nobody scraping, or the
    // previous owner crashed and its stale lock was reclaimed). Drop it
    // again; an actual run takes its own lock via acquire().
    probe.unlock();
    return false;
  }
  // tryLock failed: a live process holds it iff the error is
  // LockFailedError. Any other error (permissions, etc.) is treated as
  // "not owned" so a transient glitch can't strand a real resume.
  return probe.error() == QLockFile::LockFailedError;
}

} // namespace Scraper
