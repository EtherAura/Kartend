#ifndef SETTINGSMODEL_H
#define SETTINGSMODEL_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include <QList>

/// Aggregates the per-dialog data state SettingsDialog edits during its
/// lifetime. Step 1 of the SettingsDialog "thin shell" goal — currently a
/// non-owning pointer aggregate aimed at SettingsDialog's existing fields, so
/// in-place call sites continue to compile without rewrites. Future steps
/// promote ownership onto the model and have panels observe the model
/// directly instead of receiving a bare GeneralSettings*.
struct SettingsModel {
  /// Live collection list — committed snapshots after each Save.
  QList<CollectionConfig> *collections = nullptr;
  /// In-memory copy reflecting the user's pending edits.
  QList<CollectionConfig> *workingCollections = nullptr;
  /// Per-row snapshot used for dirty detection on the active collection.
  CollectionConfig *originalCollection = nullptr;
  /// Global preferences edited via the dialog.
  GeneralSettings *generalSettings = nullptr;
  /// General-settings snapshot used for dirty detection.
  GeneralSettings *originalGeneralSettings = nullptr;
  /// True when no unsaved row-level change exists.
  bool *collectionSaved = nullptr;
  /// Index of the row in workingCollections the user is currently editing.
  /// Negative when no row is selected — panels guard their accessors.
  int *currentIndex = nullptr;
};

#endif // SETTINGSMODEL_H
