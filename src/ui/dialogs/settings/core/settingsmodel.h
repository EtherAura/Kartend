#ifndef SETTINGSMODEL_H
#define SETTINGSMODEL_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "imainwindow.h"
#include <functional>
#include <QList>
#include <QString>

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

  /// Optional bridges to the DAT-audit feature, wired by SettingsDialog from its
  /// IMainWindow parent (null in tests / when there is no main window). The
  /// configuration panel uses them to launch an audit for the current
  /// collection and to show a "last audited N ago · X present · Y missing"
  /// hint (Kartend-4mqkof, Kartend-m6qsb.8). The launch takes a
  /// CollectionConfig (not an index) so the panel can pass its working copy
  /// with unsaved edits applied (Kartend-6wn0p).
  std::function<void(const CollectionConfig &collection)> openDatAudit;
  std::function<IMainWindow::DatAuditStatus(const QString &collectionUuid)> datAuditStatus;
};

#endif // SETTINGSMODEL_H
