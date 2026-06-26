#ifndef KARTEND_UTILS_APP_COLLECTION_STARTUP_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_STARTUP_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Startup
// behavior: which collection (or the synthetic Home view) to open, the
// first-run wizard gate, and the optional intro video clip.

#include <QString>

struct StartupSettings {
  // Collection to open on startup. Empty = open the first root collection.
  // Takes priority over the default selection when set; falls back to default
  // if the named collection no longer exists.
  QString startupCollection;
  // Synthetic "Home" view at startup showing one tile per root collection.
  // Takes priority over startupCollection when both are set.
  bool useHomeView = false;
  // Optional override for the Home view's title label and toolbar button.
  // Empty homeViewLabel falls back to the localized "Home" string;
  // homeViewIcon is an absolute path.
  QString homeViewLabel;
  QString homeViewIcon;
  // Flipped true after the wizard completes or is skipped so it never
  // re-prompts. Help -> Setup Wizard can re-run it without flipping this back.
  bool firstRunComplete = false;
  // Optional one-shot intro clip shown above the main window on launch,
  // skippable via any key or click. The enable bool is independent of the
  // path so a configured path survives being toggled off.
  bool startupVideoEnabled = false;
  QString startupVideoPath;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const StartupSettings &) const = default;
  // Trim the free-text path/label fields (Kartend audit S-07; folded by
  // GeneralSettings::normalizedForSave).
  void normalize() {
    startupVideoPath = startupVideoPath.trimmed();
    homeViewLabel = homeViewLabel.trimmed();
    homeViewIcon = homeViewIcon.trimmed();
  }
};

#endif // KARTEND_UTILS_APP_COLLECTION_STARTUP_SETTINGS_H
