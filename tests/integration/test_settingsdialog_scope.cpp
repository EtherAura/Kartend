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

namespace {

// Kartend-c06: list of widget object names that should be disabled whenever
// the active Settings Mode is anything other than `Current`. Mirrors the
// gatedFields[] array in SettingsDialog::applyScopeFieldGating().
const char *const kGatedFieldNames[] = {
    "parentCollectionComboBox",
    "mediaDirLineEdit",
    "browseMediaDirButton",
    "recursiveImportContentButton",
    "artworkDirLineEdit",
    "browseArtworkDirButton",
    "placeholderArtworkLineEdit",
    "browsePlaceholderArtworkButton",
    "fileExtensionsLineEdit",
    "launcherLineEdit",
    "browseLauncherButton",
    "coreLineEdit",
    "browseCoreButton",
    "launchParamsLineEdit",
    "extractArchivesCheckBox",
    "extractedExtensionLineEdit",
    "includeContentSubfoldersCheckBox",
    "includeArtworkSubfoldersCheckBox",
    "showAllSubcollectionItemsCheckBox",
    "showAllSubfolderItemsCheckBox",
    "hideSubfolderTitlesCheckBox",
    "showHiddenFoldersCheckBox",
};

} // namespace

void TestSettingsDialogScope::testNonPropagatableFieldsDisabledInWiderScope() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("settingsScopeComboBox"));
  QVERIFY(combo);

  combo->setCurrentIndex(1); // CurrentAndSubcollections
  for (const char *name : kGatedFieldNames) {
    auto *w = dialog.findChild<QWidget *>(QString::fromLatin1(name));
    QVERIFY2(w, qPrintable(QStringLiteral("missing widget: %1").arg(name)));
    QVERIFY2(!w->isEnabled(),
             qPrintable(QStringLiteral("%1 should be disabled when scope is "
                                       "CurrentAndSubcollections")
                            .arg(name)));
  }

  combo->setCurrentIndex(2); // All
  for (const char *name : kGatedFieldNames) {
    auto *w = dialog.findChild<QWidget *>(QString::fromLatin1(name));
    QVERIFY(w);
    QVERIFY2(!w->isEnabled(),
             qPrintable(QStringLiteral("%1 should be disabled when scope is All")
                            .arg(name)));
  }
}

void TestSettingsDialogScope::testFieldsReEnabledWhenScopeReturnsToCurrent() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("settingsScopeComboBox"));
  QVERIFY(combo);

  combo->setCurrentIndex(2); // All — disables everything
  combo->setCurrentIndex(0); // back to Current — must re-enable

  for (const char *name : kGatedFieldNames) {
    auto *w = dialog.findChild<QWidget *>(QString::fromLatin1(name));
    QVERIFY(w);
    QVERIFY2(w->isEnabled(),
             qPrintable(QStringLiteral("%1 should be re-enabled when scope "
                                       "returns to Current")
                            .arg(name)));
  }
}
