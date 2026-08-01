// SettingsDialog dirty tracking: pins one representative field per former
// check*Changes bucket, so a field silently dropping out of change detection
// (the failure mode that loses user edits on switch/close) breaks a test
// here. hasUnsavedChanges() is now a whole-struct compare — it builds the
// CollectionConfig that Save would write and diffs it against the original
// snapshot — so these tests double as regression pins that the extraction
// pipeline covers every historical bucket. The dialog is constructed
// headlessly (never shown) and driven through findChild + the public
// hasUnsavedChanges(), matching the established dialog-test pattern.

#include "collection/collectionconfig.h"
#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace {

QList<CollectionConfig> twoCollections() {
  CollectionConfig first;
  first.name = QStringLiteral("First");
  first.launcher.launcherPath = QStringLiteral("/usr/bin/player");
  LauncherConfig alternate;
  alternate.name = QStringLiteral("Alternate");
  alternate.launcherPath = QStringLiteral("/usr/bin/alternate");
  first.launcher.additionalLaunchers = {alternate};
  first.extensions = QStringList{QStringLiteral("mp4")};

  CollectionConfig second;
  second.name = QStringLiteral("Second");
  return {first, second};
}

} // namespace

class TestSettingsDialogChecks : public QObject {
  Q_OBJECT

private slots:
  void baselineIsClean();
  void spacingSpinBox_marksDirtyAndRevertsClean();
  void extensionsEdit_marksDirty();
  void itemDimension_marksDirtyAndRevertsClean();
  void treeRename_marksDirtyAndRevertsClean();
  void parentComboChange_marksDirtyAndRevertsClean();
  void vignetteToggle_marksDirtyAndRevertsClean();
  void effectsToggle_marksDirty();
  void backgroundValue_marksDirtyAndRevertsClean();
  void listFontSize_marksDirtyAndRevertsClean();
  void defaultLauncherCombo_marksDirtyAndRevertsClean();
  void generalSettingsToggle_marksDirtyAndRevertsClean();
};

void TestSettingsDialogChecks::baselineIsClean() {
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  QVERIFY2(!dialog.hasUnsavedChanges(),
           "Fresh dialog must be clean — a dirty baseline poisons every slot below");
  QCOMPARE(dialog.getSelectedCollectionIndex(), 0);
}

void TestSettingsDialogChecks::spacingSpinBox_marksDirtyAndRevertsClean() {
  // Spacing runs through the spacingUiToInternal mapping in the panel's
  // save() — a representative for the value-mapped fields (UI value differs
  // from the persisted value).
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *spin = dialog.findChild<QSpinBox *>(QStringLiteral("horizontalSpacingSpinBox"));
  QVERIFY2(spin, "horizontalSpacingSpinBox must exist on the appearance layout panel");
  const int original = spin->value();
  QVERIFY(!dialog.hasUnsavedChanges());
  spin->setValue(original + 1);
  QVERIFY(dialog.hasUnsavedChanges());
  spin->setValue(original);
  QVERIFY2(!dialog.hasUnsavedChanges(),
           "Reverting spacing must clear the dirty flag (value compare, not touch-tracking)");
}

void TestSettingsDialogChecks::extensionsEdit_marksDirty() {
  // Extension edits reach the whole-struct compare through
  // ConfigurationPanel::save()'s parsed extension list.
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("fileExtensionsLineEdit"));
  QVERIFY2(edit, "fileExtensionsLineEdit must exist on the configuration panel");
  QVERIFY(!dialog.hasUnsavedChanges());
  edit->setText(QStringLiteral("mp4, mkv"));
  QVERIFY2(dialog.hasUnsavedChanges(), "Extension edits must mark the dialog dirty");
}

void TestSettingsDialogChecks::itemDimension_marksDirtyAndRevertsClean() {
  // Item dimensions reach the whole-struct compare through
  // AppearanceLayoutPanel::save().
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *spin = dialog.findChild<QSpinBox *>(QStringLiteral("itemWidthSpinBox"));
  QVERIFY2(spin, "itemWidthSpinBox must exist on the appearance layout panel");
  const int original = spin->value();
  QVERIFY(!dialog.hasUnsavedChanges());
  spin->setValue(original + 10);
  QVERIFY(dialog.hasUnsavedChanges());
  spin->setValue(original);
  QVERIFY(!dialog.hasUnsavedChanges());
}

void TestSettingsDialogChecks::treeRename_marksDirtyAndRevertsClean() {
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("collectionTreeWidget"));
  QVERIFY2(tree, "collectionTreeWidget must exist on the collections tree shell");
  QTreeWidgetItem *item = tree->topLevelItem(0);
  QVERIFY(item);
  QCOMPARE(item->text(0), QStringLiteral("First"));

  QVERIFY(!dialog.hasUnsavedChanges());
  item->setText(0, QStringLiteral("First Renamed"));
  QVERIFY2(dialog.hasUnsavedChanges(), "Tree rename must register as an unsaved change");
  item->setText(0, QStringLiteral("First"));
  QVERIFY2(!dialog.hasUnsavedChanges(),
           "Renaming back to the original name must clear the dirty flag");
}

void TestSettingsDialogChecks::parentComboChange_marksDirtyAndRevertsClean() {
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("parentCollectionComboBox"));
  QVERIFY2(combo, "parentCollectionComboBox must exist on the configuration panel");
  // Rows: None + Second ("First" itself is excluded).
  QCOMPARE(combo->count(), 2);
  QCOMPARE(combo->currentIndex(), 0);

  QVERIFY(!dialog.hasUnsavedChanges());
  combo->setCurrentIndex(1);
  QVERIFY2(dialog.hasUnsavedChanges(), "Parent selection must register as an unsaved change");
  combo->setCurrentIndex(0);
  QVERIFY(!dialog.hasUnsavedChanges());
}

void TestSettingsDialogChecks::vignetteToggle_marksDirtyAndRevertsClean() {
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("vignetteEnabledCheckBox"));
  QVERIFY2(box, "vignetteEnabledCheckBox must exist on the appearance colors panel");
  QVERIFY(!dialog.hasUnsavedChanges());
  box->setChecked(!box->isChecked());
  QVERIFY2(dialog.hasUnsavedChanges(), "Vignette toggle must register as an unsaved change");
  box->setChecked(!box->isChecked());
  QVERIFY(!dialog.hasUnsavedChanges());
}

void TestSettingsDialogChecks::effectsToggle_marksDirty() {
  // The effects panel (parallax / backdrop blur) reaches the compare
  // through AppearanceEffectsPanel::save().
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("wallpaperParallaxCheckBox"));
  QVERIFY2(box, "wallpaperParallaxCheckBox must exist on the appearance effects panel");
  QVERIFY(!dialog.hasUnsavedChanges());
  box->setChecked(!box->isChecked());
  QVERIFY(dialog.hasUnsavedChanges());
}

void TestSettingsDialogChecks::backgroundValue_marksDirtyAndRevertsClean() {
  // Background edits reach the whole-struct compare through
  // AppearanceColorsPanel::save()'s type-routed value slots.
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("backgroundValueEdit"));
  QVERIFY2(edit, "backgroundValueEdit must exist on the appearance colors panel");
  const QString original = edit->text();
  QVERIFY(!dialog.hasUnsavedChanges());
  edit->setText(QStringLiteral("#123456"));
  QVERIFY2(dialog.hasUnsavedChanges(), "Background value edits must mark the dialog dirty");
  edit->setText(original);
  QVERIFY(!dialog.hasUnsavedChanges());
}

void TestSettingsDialogChecks::listFontSize_marksDirtyAndRevertsClean() {
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *spin = dialog.findChild<QSpinBox *>(QStringLiteral("listFontSizeSpinBox"));
  QVERIFY2(spin, "listFontSizeSpinBox must exist on the appearance list panel");
  const int original = spin->value();
  QVERIFY(!dialog.hasUnsavedChanges());
  spin->setValue(original + 3);
  QVERIFY2(dialog.hasUnsavedChanges(), "List font size must register as an unsaved change");
  spin->setValue(original);
  QVERIFY(!dialog.hasUnsavedChanges());
}

void TestSettingsDialogChecks::defaultLauncherCombo_marksDirtyAndRevertsClean() {
  // The default-launcher index is a dialog-owned special case in
  // extractUIFieldValues (only written while the combo has entries).
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("defaultLauncherComboBox"));
  QVERIFY2(combo, "defaultLauncherComboBox must exist on the launcher panel");
  QVERIFY2(combo->count() >= 2,
           "Fixture provides primary + one additional launcher, so the combo must "
           "offer at least two rows");
  QCOMPARE(combo->currentIndex(), 0);

  QVERIFY(!dialog.hasUnsavedChanges());
  combo->setCurrentIndex(1);
  QVERIFY(dialog.hasUnsavedChanges());
  combo->setCurrentIndex(0);
  QVERIFY(!dialog.hasUnsavedChanges());
}

void TestSettingsDialogChecks::generalSettingsToggle_marksDirtyAndRevertsClean() {
  // General settings dirty-check is a whole-struct compare against the
  // baseline snapshot; the panel writes into the live model on every toggle.
  SettingsDialog dialog(nullptr, twoCollections(), 0);
  auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("rememberSelectionCheckBox"));
  QVERIFY2(box, "rememberSelectionCheckBox must exist on the general settings panel");
  QVERIFY(!dialog.hasUnsavedChanges());
  box->setChecked(!box->isChecked());
  QVERIFY2(dialog.hasUnsavedChanges(),
           "General-settings edits must register via checkGeneralSettingsChanges");
  box->setChecked(!box->isChecked());
  QVERIFY2(!dialog.hasUnsavedChanges(),
           "Toggling back must be clean again — the check compares structs, not history");
}

QTEST_MAIN(TestSettingsDialogChecks)
#include "test_settingsdialogchecks.moc"
