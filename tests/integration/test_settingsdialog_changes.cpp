#include "test_settingsdialog_changes.h"

#include "collectionutils.h"
#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QList>
#include <QSpinBox>
#include <QTest>

namespace {

QList<CollectionConfig> singleCollection() {
  CollectionConfig c;
  c.name = QStringLiteral("Edit Target");
  c.gridLayout.gridWidth = 5;
  c.gridLayout.itemWidth = 200;
  c.gridLayout.itemHeight = 300;
  c.gridLayout.fontSize = 12;
  c.gridLayout.cornerRadius = 4;
  c.hideTitles = false;
  c.folderBrowsing.includeContentSubfolders = false;
  c.launcher.launcherPath = QStringLiteral("/usr/bin/example");
  c.viewType = ViewType::Grid;
  return {c};
}

} // namespace

void TestSettingsDialogChanges::cleanDialogReportsNoChanges() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  // Right after load with no UI mutations the dirty checker must say clean.
  // Without this baseline, every other test in this slot would race against
  // the load path's synthetic widget setters firing changed() signals.
  QVERIFY2(!dialog.hasUnsavedChanges(),
           "SettingsDialog reports dirty immediately after construction");
}

void TestSettingsDialogChanges::editingLineEditMarksDirty() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *line = dialog.findChild<QLineEdit *>(QStringLiteral("launcherLineEdit"));
  QVERIFY2(line, "launcherLineEdit must exist on the dialog .ui");
  QVERIFY(!dialog.hasUnsavedChanges());
  line->setText(QStringLiteral("/usr/bin/different"));
  QVERIFY2(dialog.hasUnsavedChanges(),
           "Mutating launcherLineEdit must flip the dialog into dirty state");
}

void TestSettingsDialogChanges::togglingCheckBoxMarksDirty() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("hideTitlesCheckBox"));
  QVERIFY2(box, "hideTitlesCheckBox must exist on the dialog .ui");
  QVERIFY(!dialog.hasUnsavedChanges());
  // Original value is false; toggling produces a real diff.
  box->setChecked(!box->isChecked());
  QVERIFY(dialog.hasUnsavedChanges());
}

void TestSettingsDialogChanges::spinBoxValueChangeMarksDirty() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *spin = dialog.findChild<QSpinBox *>(QStringLiteral("gridWidthSpinBox"));
  QVERIFY2(spin, "gridWidthSpinBox must exist on the dialog .ui");
  QVERIFY(!dialog.hasUnsavedChanges());
  spin->setValue(spin->value() + 2);
  QVERIFY(dialog.hasUnsavedChanges());
}

void TestSettingsDialogChanges::comboIndexChangeMarksDirty() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("viewTypeComboBox"));
  QVERIFY2(combo, "viewTypeComboBox must exist on the dialog .ui");
  if (combo->count() < 2) {
    QSKIP("viewTypeComboBox has too few entries to flip");
  }
  QVERIFY(!dialog.hasUnsavedChanges());
  // Pick an index different from the current one; comboEnumChanged() in
  // settingsdialogchecks compares against (int)originalConfig.viewType.
  const int target = (combo->currentIndex() == 0) ? 1 : 0;
  combo->setCurrentIndex(target);
  QVERIFY(dialog.hasUnsavedChanges());
}

void TestSettingsDialogChanges::revertingMutationReportsClean() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *spin = dialog.findChild<QSpinBox *>(QStringLiteral("gridWidthSpinBox"));
  QVERIFY(spin);
  const int original = spin->value();
  // Drive the value away then back; the dirty detector must report clean
  // again because checkBasicFieldChanges compares current to original, not
  // a "has-ever-mutated" flag.
  spin->setValue(original + 3);
  QVERIFY(dialog.hasUnsavedChanges());
  spin->setValue(original);
  QVERIFY2(!dialog.hasUnsavedChanges(),
           "Reverting a spinbox to its original value must clear the dirty flag");
}
