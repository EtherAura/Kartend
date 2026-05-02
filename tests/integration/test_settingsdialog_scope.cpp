#include "test_settingsdialog_scope.h"

#include "collectionutils.h"
#include "settingsdialog.h"

#include <QComboBox>
#include <QList>
#include <QSignalSpy>
#include <QTest>

namespace {

QList<CollectionConfig> makeCollections() {
  // Minimal collection list — the dialog only reads these via the public
  // constructor and never touches the filesystem unless the user clicks
  // a Browse/Save button, which these tests do not.
  CollectionConfig parent;
  parent.name = QStringLiteral("Parent");
  CollectionConfig child;
  child.name = QStringLiteral("Child");
  child.parentCollectionIndex = 0;
  child.isSubcollection = true;
  CollectionConfig sibling;
  sibling.name = QStringLiteral("Sibling");
  return {parent, child, sibling};
}

} // namespace

void TestSettingsDialogScope::testDefaultScopeIsCurrent() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  QCOMPARE(dialog.currentSettingsScope(), SettingsDialog::SettingsScope::Current);
}

void TestSettingsDialogScope::testScopeChangeEmitsSignal() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  QSignalSpy spy(&dialog, &SettingsDialog::settingsScopeChanged);
  QVERIFY(spy.isValid());

  auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("settingsScopeComboBox"));
  QVERIFY2(combo, "settingsScopeComboBox should exist in the .ui file");
  QCOMPARE(combo->count(), 3);

  combo->setCurrentIndex(1);
  QCOMPARE(dialog.currentSettingsScope(),
           SettingsDialog::SettingsScope::CurrentAndSubcollections);
  QCOMPARE(spy.count(), 1);

  combo->setCurrentIndex(2);
  QCOMPARE(dialog.currentSettingsScope(), SettingsDialog::SettingsScope::All);
  QCOMPARE(spy.count(), 2);

  combo->setCurrentIndex(0);
  QCOMPARE(dialog.currentSettingsScope(), SettingsDialog::SettingsScope::Current);
  QCOMPARE(spy.count(), 3);
}

void TestSettingsDialogScope::testScopeChangeSignalDoesNotFireForSameValue() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  QSignalSpy spy(&dialog, &SettingsDialog::settingsScopeChanged);

  auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("settingsScopeComboBox"));
  QVERIFY(combo);

  // Setting to the already-active index must not re-emit. onSettingsScopeChanged
  // guards against churn so dependent UI (Kartend-c06 field gating) stays stable.
  combo->setCurrentIndex(0);
  QCOMPARE(spy.count(), 0);
}
