#ifndef KARTEND_UTILS_APP_COLLECTION_PRESENTATIONPROFILE_H
#define KARTEND_UTILS_APP_COLLECTION_PRESENTATIONPROFILE_H

// Named bundle of attract / marquee / splash settings the user can save
// and apply to GeneralSettings. Mirrors the ThemePreset (Kartend-y3rg)
// pattern — both shapes live next to their consumer leaf structs so
// future kart-bundle / settings dialog work can import them without
// dragging in the persistence layer.

#include <QJsonObject>
#include <QList>
#include <QString>

#include "errorutils.h"

struct GeneralSettings;

struct PresentationProfile {
  /// Human-readable name shown in the picker. Must be non-empty before
  /// the profile can be saved into the registry.
  QString name;
  /// Free-form description.
  QString description;
  /// Schema-version field — bump when adding new attract / marquee
  /// fields so older builds reading a newer profile refuse rather than
  /// silently apply a partially-parsed forward-compat profile.
  int schemaVersion = 1;

  // ─── Attract mode ──────────────────────────────────────────────────
  bool attractModeEnabled = false;
  int attractModeIdleTimeoutSec = 120;
  bool attractModeAutoScrollEnabled = true;
  double attractModeScrollSpeed = 1.0;
  bool attractModeAdvanceSelectionEnabled = false;
  int attractModeAdvanceSelectionIntervalSec = 5;
  bool attractModeAdvanceSelectionRandom = false;

  // ─── Marquee / secondary monitor ───────────────────────────────────
  bool marqueeEnabled = false;
  QString marqueeScreenName;
  int marqueeMode = 0;

  // ─── Splash screens ────────────────────────────────────────────────
  bool bootSplashEnabled = true;
  bool resumeFocusSplashEnabled = true;
  QString bootSplashTitle;
  QString bootSplashSubtitle;
  QString resumeFocusSplashTitle;
  QString resumeFocusSplashSubtitle;

  bool operator==(const PresentationProfile &other) const {
    return name == other.name && description == other.description &&
           schemaVersion == other.schemaVersion && attractModeEnabled == other.attractModeEnabled &&
           attractModeIdleTimeoutSec == other.attractModeIdleTimeoutSec &&
           attractModeAutoScrollEnabled == other.attractModeAutoScrollEnabled &&
           qFuzzyCompare(1.0 + attractModeScrollSpeed, 1.0 + other.attractModeScrollSpeed) &&
           attractModeAdvanceSelectionEnabled == other.attractModeAdvanceSelectionEnabled &&
           attractModeAdvanceSelectionIntervalSec == other.attractModeAdvanceSelectionIntervalSec &&
           attractModeAdvanceSelectionRandom == other.attractModeAdvanceSelectionRandom &&
           marqueeEnabled == other.marqueeEnabled && marqueeScreenName == other.marqueeScreenName &&
           marqueeMode == other.marqueeMode && bootSplashEnabled == other.bootSplashEnabled &&
           resumeFocusSplashEnabled == other.resumeFocusSplashEnabled &&
           bootSplashTitle == other.bootSplashTitle &&
           bootSplashSubtitle == other.bootSplashSubtitle &&
           resumeFocusSplashTitle == other.resumeFocusSplashTitle &&
           resumeFocusSplashSubtitle == other.resumeFocusSplashSubtitle;
  }
  bool operator!=(const PresentationProfile &other) const { return !(*this == other); }
};

namespace PresentationProfileIO {

[[nodiscard]] QJsonObject toJson(const PresentationProfile &profile);
[[nodiscard]] ErrorUtils::Result<PresentationProfile> fromJson(const QJsonObject &obj);

/// Atomic file I/O via QSaveFile (matches ThemePreset's pattern).
[[nodiscard]] ErrorUtils::Result<bool> exportToFile(const PresentationProfile &profile,
                                                    const QString &filePath);
[[nodiscard]] ErrorUtils::Result<PresentationProfile> importFromFile(const QString &filePath);

/// Writes the profile's fields onto `target`. Non-presentation fields
/// (keybindings, scraper, scanning policy, etc.) on `target` are left
/// untouched.
void applyTo(const PresentationProfile &profile, GeneralSettings &target);

/// Snapshots the presentation-relevant fields of `source` into a new
/// PresentationProfile. `name` defaults to "Current" when empty.
[[nodiscard]] PresentationProfile fromGeneralSettings(const GeneralSettings &source,
                                                      const QString &name = {});

// ─── Registry helpers (Kartend-tfux pattern) ────────────────────────
[[nodiscard]] ErrorUtils::Result<QList<PresentationProfile>> loadRegistry(const QString &filePath);
[[nodiscard]] ErrorUtils::Result<bool> saveRegistry(const QList<PresentationProfile> &profiles,
                                                    const QString &filePath);
[[nodiscard]] QList<PresentationProfile> addOrReplace(const QList<PresentationProfile> &registry,
                                                      const PresentationProfile &profile);
[[nodiscard]] QList<PresentationProfile> removeByName(const QList<PresentationProfile> &registry,
                                                      const QString &name);

} // namespace PresentationProfileIO

#endif // KARTEND_UTILS_APP_COLLECTION_PRESENTATIONPROFILE_H
