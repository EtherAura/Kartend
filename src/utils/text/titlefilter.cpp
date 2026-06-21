#include "titlefilter.h"

#include "collection/collectionconfig.h"
#include "errorutils.h"

#include <atomic>

namespace {

struct CollectionEntry {
  bool enabled = false;
  QList<QRegularExpression> compiled;
};

QReadWriteLock &registryLock() {
  static QReadWriteLock lock;
  return lock;
}

QHash<int, CollectionEntry> &registry() {
  static QHash<int, CollectionEntry> map;
  return map;
}

// Mirrors registry() non-emptiness so apply() can fast-path the common
// "no collection has any exclusion patterns" case without taking the read lock.
// Written under the write lock alongside every registry() mutation.
std::atomic<bool> &registryNonEmpty() {
  static std::atomic<bool> flag{false};
  return flag;
}

} // namespace

namespace TitleFilter {

QList<QRegularExpression> compilePatterns(const QStringList &patterns,
                                          const QString &diagnosticContext) {
  QList<QRegularExpression> compiled;
  compiled.reserve(patterns.size());
  for (const QString &raw : patterns) {
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    // ReDoS guard (Kartend-omin4): these patterns run via QString::remove(re)
    // once PER displayed item on the UI / DB-interception path, and can arrive
    // from imported collection configs, not just hand entry. Reject implausibly
    // long patterns outright. (Qt/PCRE2 also bounds catastrophic backtracking
    // via its internal match limit; this caps the per-item cost up to it.)
    constexpr int kMaxPatternLength = 256;
    if (trimmed.size() > kMaxPatternLength) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(
              ErrorUtils::ErrorCode::InvalidArgument,
              QStringLiteral("Title-exclusion regex too long; skipped"),
              QStringLiteral("TitleFilter::compilePatterns"))
              .withDetails(QStringLiteral("context=%1 length=%2 max=%3")
                               .arg(diagnosticContext.isEmpty() ? QStringLiteral("<unset>")
                                                                : diagnosticContext)
                               .arg(trimmed.size())
                               .arg(kMaxPatternLength)));
      continue;
    }
    QRegularExpression re(trimmed);
    if (!re.isValid()) {
      // Skip invalid patterns rather than wedging the whole list. Surfacing
      // this via the standard error logger gives the user a chance to fix
      // the typo from the toolbar popup without crashing the title pipeline.
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::InvalidArgument,
                                            QStringLiteral("Invalid title-exclusion regex skipped"),
                                            QStringLiteral("TitleFilter::compilePatterns"))
              .withDetails(QStringLiteral("context=%1 pattern=%2 error=%3")
                               .arg(diagnosticContext.isEmpty() ? QStringLiteral("<unset>")
                                                                : diagnosticContext,
                                    trimmed, re.errorString())));
      continue;
    }
    compiled.append(std::move(re));
  }
  return compiled;
}

void rebuildFromCollections(const QList<CollectionConfig> &collections) {
  QHash<int, CollectionEntry> next;
  next.reserve(collections.size());
  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &c = collections[i];
    if (c.filter.titleExclusionPatterns.isEmpty()) {
      continue;
    }
    CollectionEntry entry;
    entry.enabled = c.filter.titleExclusionEnabled;
    entry.compiled = compilePatterns(c.filter.titleExclusionPatterns, c.name);
    if (entry.compiled.isEmpty()) {
      continue;
    }
    next.insert(i, entry);
  }
  QWriteLocker locker(&registryLock());
  registry() = std::move(next);
  registryNonEmpty().store(!registry().isEmpty(), std::memory_order_release);
}

QString apply(int collectionIndex, const QString &displayName) {
  if (collectionIndex < 0 || displayName.isEmpty()) {
    return displayName;
  }
  // Fast path for the common case where no collection has any compiled
  // exclusion patterns: skip the read lock + hash lookup entirely.
  if (!registryNonEmpty().load(std::memory_order_acquire)) {
    return displayName;
  }
  // Copy the compiled patterns (QList is copy-on-write, so this is cheap) under
  // the read lock, then drop the lock before the regex loop + simplified(). apply()
  // runs per item on the DB-interception/UI path, and holding the read lock across
  // the whole match would block a settings save's write lock
  // (rebuildFromCollections) for the full per-item matching duration (Kartend-99o3).
  QList<QRegularExpression> compiled;
  {
    QReadLocker locker(&registryLock());
    const auto it = registry().constFind(collectionIndex);
    if (it == registry().constEnd() || !it->enabled || it->compiled.isEmpty()) {
      return displayName;
    }
    compiled = it->compiled;
  }
  QString out = displayName;
  for (const QRegularExpression &re : compiled) {
    out.remove(re);
  }
  // Patterns commonly leave double spaces or leading/trailing whitespace
  // ("Game (USA) (Rev 1)" → "Game  " when both region and revision strip).
  // simplified() collapses runs of whitespace and trims edges so the visible
  // title looks deliberate even when the user's regex didn't anchor cleanly.
  return out.simplified();
}

void clearForTests() {
  QWriteLocker locker(&registryLock());
  registry().clear();
  registryNonEmpty().store(false, std::memory_order_release);
}

} // namespace TitleFilter
