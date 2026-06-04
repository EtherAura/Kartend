#include "titlefilter.h"

#include "collection/collectionconfig.h"
#include "errorutils.h"

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
}

QString apply(int collectionIndex, const QString &displayName) {
  if (collectionIndex < 0 || displayName.isEmpty()) {
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
}

} // namespace TitleFilter
