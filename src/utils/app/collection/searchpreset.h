#ifndef KARTEND_UTILS_APP_COLLECTION_SEARCHPRESET_H
#define KARTEND_UTILS_APP_COLLECTION_SEARCHPRESET_H

// Named, reusable snapshot of "how I am looking at this library right now":
// the search box's contents plus the filter and sort state around it. Kartend
// already understands structured queries (`played:true tag:soundtrack live`)
// but every one of them has to be retyped; a query worth composing twice is
// worth keeping.
//
// Mirrors the PresentationProfile / ThemePreset registry shape (Kartend-tfux)
// so the settings surfaces, the .kart bundle, and any future import/export all
// meet the same JSON-array-of-named-objects on disk.

#include <QJsonObject>
#include <QList>
#include <QString>

#include "../collectiontypes.h" // SortMode
#include "errorutils.h"

struct ViewSettings;

struct SearchPreset {
  /// Shown in the picker; must be non-empty to enter the registry.
  QString name;
  /// Free-form note about what the preset is for.
  QString description;
  /// Bump when adding fields, so an older build refuses a newer preset
  /// instead of applying a half-understood one.
  int schemaVersion = 1;

  /// Search box contents, structured tokens included — this is the part that
  /// carries `played:false`, `tag:x`, `missing:artwork` and the free text.
  QString searchText;

  // ─── Filter + sort state (mirrors ViewSettings) ──────────────────────
  SortMode sortMode = SortMode::NameAscending;
  bool excludeSubfoldersFromSort = false;
  /// Empty means "show every collection type".
  QString collectionTypeFilter;
  bool hideSubcollectionTiles = false;

  bool operator==(const SearchPreset &) const = default;
};

namespace SearchPresetIO {

[[nodiscard]] QJsonObject toJson(const SearchPreset &preset);
[[nodiscard]] ErrorUtils::Result<SearchPreset> fromJson(const QJsonObject &obj);

/// Writes the preset's filter/sort fields onto `target`. Fields outside the
/// filter surface — column widths, chrome toggles, the placeholder-title
/// overlay — are deliberately untouched: applying a saved query should not
/// rearrange the window. `searchText` is not a ViewSettings field; the caller
/// puts it in the search box.
void applyTo(const SearchPreset &preset, ViewSettings &target);

/// Snapshot the filter/sort fields of `source` plus the supplied search text.
/// `name` defaults to "Current" when empty, matching the profile registries.
[[nodiscard]] SearchPreset fromViewSettings(const ViewSettings &source, const QString &searchText,
                                            const QString &name = {});

// ─── Registry helpers (same shape as PresentationProfileIO) ────────────
[[nodiscard]] ErrorUtils::Result<QList<SearchPreset>> loadRegistry(const QString &filePath);
[[nodiscard]] ErrorUtils::Result<bool> saveRegistry(const QList<SearchPreset> &presets,
                                                    const QString &filePath);
/// Name-keyed, case-insensitive: saving over an existing name replaces it in
/// place rather than appending a second entry the picker cannot tell apart.
[[nodiscard]] QList<SearchPreset> addOrReplace(const QList<SearchPreset> &registry,
                                               const SearchPreset &preset);
[[nodiscard]] QList<SearchPreset> removeByName(const QList<SearchPreset> &registry,
                                               const QString &name);

} // namespace SearchPresetIO

#endif // KARTEND_UTILS_APP_COLLECTION_SEARCHPRESET_H
