#ifndef KARTEND_UTILS_APP_COLLECTION_THEMEPRESET_H
#define KARTEND_UTILS_APP_COLLECTION_THEMEPRESET_H

// Theme preset — shareable bundle of per-collection appearance settings.
// Holds the visual clusters (grid layout, sidebar appearance, view
// background, list-view options) plus the standalone appearance scalars
// (horizontalAlignment, customFontFamily). Excludes paths, launcher
// config, scraper overrides, and filter rules — those are not "theme"
// (they describe the collection's contents, not its look).
//
// Lives next to the leaf clusters so a future kart-bundle / settings
// dialog can import the same struct without depending on the JSON layer.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "../collectiontypes.h"
#include "collectionbackground.h"
#include "errorutils.h"
#include "gridlayoutpreferences.h"
#include "listviewoptions.h"
#include "sidebarappearance.h"

struct CollectionConfig;

struct ThemePreset {
  /// Human-readable name shown in the import-preview dialog. Defaults to
  /// the source collection's name on export; the user can rename in a
  /// future revision.
  QString name;
  /// Free-form description (e.g. author, intended use). Optional.
  QString description;
  /// Schema-version field — bump when adding new appearance clusters so
  /// older builds reading a newer preset know to surface a "some fields
  /// were ignored" warning. Current schema = 1.
  int schemaVersion = 1;

  GridLayoutPreferences gridLayout;
  SidebarAppearance sidebar;
  CollectionBackground background;
  ListViewOptions listView;
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Center;
  QString customFontFamily;

  bool operator==(const ThemePreset &other) const {
    return name == other.name && description == other.description &&
           schemaVersion == other.schemaVersion && gridLayout == other.gridLayout &&
           sidebar == other.sidebar && background == other.background &&
           listView == other.listView && horizontalAlignment == other.horizontalAlignment &&
           customFontFamily == other.customFontFamily;
  }
  bool operator!=(const ThemePreset &other) const { return !(*this == other); }
};

namespace ThemePresetIO {

/// Serializes the preset to a JSON object. The shape is stable and
/// versioned via the embedded schemaVersion field so external tooling can
/// round-trip presets.
[[nodiscard]] QJsonObject toJson(const ThemePreset &preset);

/// Inverse of toJson. Returns an error when the input isn't a JSON object,
/// when schemaVersion is missing / negative, or when the schemaVersion is
/// newer than the current build understands (so we don't silently apply a
/// partially-parsed forward-compat preset).
[[nodiscard]] ErrorUtils::Result<ThemePreset> fromJson(const QJsonObject &obj);

/// Writes the preset to `filePath` as pretty-printed JSON via QSaveFile
/// (crash-safe atomic rename). Caller controls extension — convention is
/// `*.kartend-theme.json`.
[[nodiscard]] ErrorUtils::Result<bool> exportToFile(const ThemePreset &preset,
                                                    const QString &filePath);

/// Reads + parses the preset file at `filePath`.
[[nodiscard]] ErrorUtils::Result<ThemePreset> importFromFile(const QString &filePath);

/// Copies the preset's appearance fields onto `target`, overwriting any
/// existing values. Non-theme fields (paths, launcher, scraper, etc.) on
/// `target` are left untouched. Re-clamps numeric ranges via
/// CollectionConfig::clampValues() so an imported preset with out-of-band
/// values stays inside the UI-safe envelope.
void applyTo(const ThemePreset &preset, CollectionConfig &target);

/// Builds a preset that captures `source`'s current appearance fields.
/// `name` falls back to `source.name` when empty.
[[nodiscard]] ThemePreset fromCollection(const CollectionConfig &source, const QString &name = {});

/// Human-readable preview of the fields the import would change against
/// the current `target`. Each entry is a one-liner like "Grid width: 8 → 12".
/// Empty list means the preset matches `target` exactly — useful UX for
/// avoiding a no-op apply.
[[nodiscard]] QStringList describeChanges(const ThemePreset &preset,
                                          const CollectionConfig &target);

// ─── Layout profile registry helpers ───────────────────────────────
// Multi-preset persistence for Kartend-tfux. Stores user-named layout
// profiles as a JSON array under layout_profiles.json (see
// SettingsUtils::getLayoutProfilesPath). Profiles are otherwise identical
// to single ThemePresets — name is the natural key. The registry uses
// stable JSON keys + the same schemaVersion gate so older builds reading
// a newer file refuse rather than silently misinterpret.

/// Reads the layout-profiles JSON registry. A missing file returns an
/// empty list (treated as "no profiles yet"); a malformed file returns an
/// error so the user can repair / delete it.
[[nodiscard]] ErrorUtils::Result<QList<ThemePreset>> loadRegistry(const QString &filePath);

/// Atomically writes the layout-profiles registry via QSaveFile.
[[nodiscard]] ErrorUtils::Result<bool> saveRegistry(const QList<ThemePreset> &profiles,
                                                    const QString &filePath);

/// Adds @p preset to the registry, replacing any existing entry with the
/// same trimmed name (case-insensitive). Returns the modified list — the
/// caller saves it. Empty names are rejected (returns the original list
/// unchanged) so users can't create an unselectable "(unnamed)" row.
[[nodiscard]] QList<ThemePreset> addOrReplace(const QList<ThemePreset> &registry,
                                              const ThemePreset &preset);

/// Removes the first entry matching @p name (case-insensitive, trimmed).
[[nodiscard]] QList<ThemePreset> removeByName(const QList<ThemePreset> &registry,
                                              const QString &name);

} // namespace ThemePresetIO

#endif // KARTEND_UTILS_APP_COLLECTION_THEMEPRESET_H
