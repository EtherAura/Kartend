#ifndef KARTEND_UTILS_DB_COLLECTIONHEALTH_H
#define KARTEND_UTILS_DB_COLLECTIONHEALTH_H

#include <QString>
#include <QStringList>

#include "collection/collectionconfig.h"

// Read-only diagnostics for a single collection. Pure function over
// pre-fetched inputs so the dashboard logic stays unit-testable without a
// running QueryManager / filesystem fixture beyond a QTemporaryDir.

namespace CollectionHealth {

struct ItemPath {
  QString path;
  /// items.artwork_path from the scanner. Empty means "no artwork
  /// matched on the last scan" — the missing-artwork count counts these.
  QString artworkPath;
};

struct LauncherIssue {
  /// Index into CollectionConfig::launcher.launcherAt(...). 0 = primary.
  int launcherIndex = 0;
  /// Display label (resolved name or basename) so the dashboard row is
  /// scannable without the user mentally mapping indices back.
  QString launcherLabel;
  /// One-line summary of what's wrong, e.g. "Executable not found in
  /// PATH: emulator-x", "No launcher path configured".
  QString issue;
};

struct Report {
  /// Total items in the collection (the denominator for the percentages
  /// the UI may render).
  int totalItems = 0;
  /// Items whose `items.path` does not exist on disk at audit time.
  /// Capped at `maxSamples` to keep the dialog responsive on libraries
  /// with thousands of broken paths; the count is the real total.
  int missingFileCount = 0;
  QStringList missingFileSamples;
  /// Items where `items.artwork_path` is empty / NULL.
  int missingArtworkCount = 0;
  QStringList missingArtworkSamples;
  /// Per-launcher validation issues (empty = all launchers usable).
  QList<LauncherIssue> launcherIssues;
};

/// Maximum number of per-category sample paths returned from analyze().
/// The dashboard shows "and N more…" for the overflow so the dialog
/// renders quickly even on a fully-broken library.
constexpr int kMaxSamples = 20;

/// Builds a Report from already-fetched DB rows + the live collection
/// config. Filesystem existence is checked here (one `QFileInfo` per
/// row); the launcher checks are delegated to a caller-supplied
/// `launcherValidator` closure so we don't drag in LaunchManager's
/// validate-path logic at this header level.
///
/// `launcherValidator(path) -> bool` returns true iff the launcher
/// executable resolves on disk / PATH. Empty paths are NOT passed —
/// the analyzer flags them as "No launcher path configured" itself.
[[nodiscard]] Report analyze(const CollectionConfig &collection, const QList<ItemPath> &items,
                             const std::function<bool(const QString &)> &launcherValidator);

} // namespace CollectionHealth

#endif // KARTEND_UTILS_DB_COLLECTIONHEALTH_H
