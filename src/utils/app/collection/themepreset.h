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

} // namespace ThemePresetIO

#endif // KARTEND_UTILS_APP_COLLECTION_THEMEPRESET_H
