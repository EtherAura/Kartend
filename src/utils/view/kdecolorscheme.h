#ifndef KDECOLORSCHEME_H
#define KDECOLORSCHEME_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

#include "errorutils.h"

struct CollectionConfig;
struct GeneralSettings;

/// Discovery + parsing for KDE Plasma .colors files.
///
/// Handles two sources of presets:
///  - System-installed schemes under $XDG_DATA_DIRS/color-schemes (every
///    Plasma install ships ~30 of these — Breeze Light/Dark, Oxygen, etc.)
///  - Three Kartend-bundled fallback schemes shipped inside the app's
///    Qt resource system under `:/themes/` so non-KDE installs still see
///    something usable in the picker.
///
/// The parser only reads the well-known `[Colors:View|Window|Selection]`
/// sections (BackgroundNormal/BackgroundAlternate/ForegroundNormal/
/// DecorationFocus). Everything else in a .colors file (button colors,
/// inactive variants, color-effects) is ignored — Kartend's per-collection
/// appearance fields don't have those concepts.
namespace KdeColorScheme {

/// Lightweight directory entry for a discovered scheme — the full color
/// load happens lazily on `load(filePath)`.
struct SchemeInfo {
  /// Pretty label for the picker. Falls back to the file basename when
  /// the .colors file omits its [General]/Name field.
  QString displayName;
  /// Either an absolute filesystem path or a Qt resource path
  /// (`:/themes/...`). Pass directly to `load()`.
  QString filePath;
  /// True for files coming out of Kartend's bundled resource set; the
  /// picker uses this to surface them under a separate "Bundled" header.
  bool isBundled = false;
};

/// All discovered schemes, deduplicated by displayName (system entries
/// win over bundled ones with the same name so user customizations
/// shadow our fallbacks). Sorted: bundled first (so a fresh non-KDE
/// install sees Kartend's own schemes at the top), then alphabetical.
[[nodiscard]] QList<SchemeInfo> discover();

/// Parsed color scheme. Keys are well-known section/key combos; values
/// are valid QColors (default-constructed for missing entries — call
/// `Scheme::has(...)` before reading if you care).
struct Scheme {
  QString displayName;
  /// Colors keyed on `Section/Key` (e.g. "View/BackgroundNormal").
  /// Internal — the apply* helpers below resolve via `color()`.
  QHash<QString, QColor> colors;

  [[nodiscard]] bool has(const QString &section, const QString &key) const;
  [[nodiscard]] QColor color(const QString &section, const QString &key,
                             const QColor &fallback = QColor()) const;
};

/// Parse a .colors file from a filesystem path or Qt resource path. The
/// scheme's `displayName` is taken from `[General]/Name` and falls back
/// to the file basename when absent.
[[nodiscard]] ErrorUtils::Result<Scheme> load(const QString &filePath);

/// Apply the parsed scheme's colors to a CollectionConfig. Touches only
/// the color-typed fields (12 of them); vignette/blur/parallax/fonts
/// stay where the user has them. Missing scheme entries leave the
/// matching CollectionConfig field untouched (additive merge).
void applyToCollection(const Scheme &scheme, CollectionConfig &config);

/// Apply the parsed scheme's accent color to GeneralSettings::titleBaseColor
/// (pulled from `[Colors:Selection]/BackgroundNormal`). Other GeneralSettings
/// appearance fields (titleTintSaturation, titleTintLightness, fonts) are
/// intentionally not touched — those are user-pref territory, not theme.
void applyToGeneralSettings(const Scheme &scheme, GeneralSettings &settings);

} // namespace KdeColorScheme

#endif // KDECOLORSCHEME_H
