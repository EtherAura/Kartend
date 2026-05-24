#ifndef KARTEND_UTILS_DB_VARIANTGROUPING_H
#define KARTEND_UTILS_DB_VARIANTGROUPING_H

#include <QList>
#include <QString>
#include <QStringList>

/// Same-basename / different-extension variant detector.
///
/// Many media libraries carry the same underlying work in several formats
/// (e.g. `Concert.mp4` + `Concert.mkv` + `Concert.flac`), or a single title
/// alongside an alternate cut (`Movie.mkv` + `Movie.directors-cut.mkv`).
/// VariantGrouping::groupByBaseName collapses an item list into named
/// groups keyed by their `completeBaseName()` so the inspector dialog can
/// show "X has N variants" at a glance.
///
/// The helper is pure: no DB access, no Qt object lifetime, no filesystem
/// touches. The caller passes the raw path list it already pulled from
/// IDatabaseManager::loadAllItemPathsForCollection. Pure-helper shape keeps
/// the function unit-testable without dragging in QSqlDatabase.
namespace VariantGrouping {

struct VariantGroup {
  /// completeBaseName() of every member, e.g. "Concert" for "Concert.mkv".
  /// Comparison is case-insensitive so a stray cover (`Concert.MKV`) groups
  /// with the lowercase siblings.
  QString baseName;
  /// Absolute paths in the order they appeared in the input. Always size > 1
  /// for a group emitted by groupByBaseName.
  QStringList paths;
};

/// Bucket @p itemPaths by `QFileInfo::completeBaseName()` (case-insensitive)
/// and return only buckets with two or more members. Single-member buckets
/// are dropped because they aren't variants of anything. Result is sorted
/// alphabetically by baseName for stable presentation.
[[nodiscard]] QList<VariantGroup> groupByBaseName(const QStringList &itemPaths);

} // namespace VariantGrouping

#endif // KARTEND_UTILS_DB_VARIANTGROUPING_H
