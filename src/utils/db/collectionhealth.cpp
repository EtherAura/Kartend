#include "collectionhealth.h"

#include <QFileInfo>

#include "collection/launcherconfig.h"

namespace CollectionHealth {

Report analyze(const CollectionConfig &collection, const QList<ItemPath> &items,
               const std::function<bool(const QString &)> &launcherValidator) {
  Report report;
  report.totalItems = items.size();

  for (const ItemPath &item : items) {
    if (item.path.isEmpty()) {
      continue;
    }
    // The scanner stores absolute paths in items.path (post-v13). The
    // existence check here doubles as a permissions check via QFileInfo's
    // implicit stat() — symlinks are followed.
    if (!QFileInfo::exists(item.path)) {
      report.missingFileCount += 1;
      if (report.missingFileSamples.size() < kMaxSamples) {
        report.missingFileSamples.append(item.path);
      }
    }
    if (item.artworkPath.trimmed().isEmpty()) {
      report.missingArtworkCount += 1;
      if (report.missingArtworkSamples.size() < kMaxSamples) {
        report.missingArtworkSamples.append(item.path);
      }
    }
  }

  // Launcher validation: walk every launcher entry the collection has
  // configured, not just the primary, because a per-item launcher
  // override may pin a non-primary one. An empty path is flagged
  // separately from a "not found" path so the dashboard surface tells
  // the user what to fix.
  const int launcherCount = collection.launcher.launcherCount();
  for (int i = 0; i < launcherCount; ++i) {
    const LauncherConfig launcher = collection.launcher.launcherAt(i);
    const QString trimmedPath = launcher.launcherPath.trimmed();
    LauncherIssue issue;
    issue.launcherIndex = i;
    issue.launcherLabel = launcher.name.trimmed().isEmpty()
                              ? collection.launcher.launcherDisplayName(i)
                              : launcher.name.trimmed();
    if (trimmedPath.isEmpty()) {
      issue.issue = QStringLiteral("No launcher path configured.");
      report.launcherIssues.append(issue);
      continue;
    }
    if (launcherValidator && !launcherValidator(trimmedPath)) {
      issue.issue = QStringLiteral("Executable not found: %1").arg(trimmedPath);
      report.launcherIssues.append(issue);
    }
  }

  return report;
}

} // namespace CollectionHealth
