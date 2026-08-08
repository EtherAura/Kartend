// LauncherImportDialog scope selector (Kartend-el5st): the per-scope count
// shown against each source, the default check state following the scope,
// and the rule that an explicit user tick survives a later scope change.
// The dialog only picks — detection and sync live in the controller — so the
// rows here are synthetic.
#include <QCheckBox>
#include <QComboBox>
#include <QObject>
#include <QTest>

#include "dialogs/collection/launcherimportdialog.h"

class TestLauncherImportDialog : public QObject {
  Q_OBJECT

private slots:
  void defaultsToOwnedScope();
  void rowLabelFollowsSelectedScope();
  void defaultCheckStateFollowsScopeCount();
  void explicitUserChoiceSurvivesScopeChange();
  void nonSteamSourcesReadTheSameAtEveryScope();

private:
  /// Steam-shaped row: the three tiers differ, as they do on a real install.
  static LauncherImportSourceRow steamRow(int installed = 3, int owned = 71, int recognized = 123,
                                          bool alreadyImported = false) {
    LauncherImportSourceRow row;
    row.id = QStringLiteral("steam");
    row.displayName = QStringLiteral("Steam");
    row.available = true;
    row.gameCount = installed;
    row.ownedGameCount = owned;
    row.recognizedGameCount = recognized;
    row.alreadyImported = alreadyImported;
    return row;
  }

  /// Everything that only ever sees installed apps reports one number.
  static LauncherImportSourceRow flatpakRow(int count = 4) {
    LauncherImportSourceRow row;
    row.id = QStringLiteral("flatpak");
    row.displayName = QStringLiteral("Flatpak");
    row.available = true;
    row.gameCount = count;
    row.ownedGameCount = count;
    row.recognizedGameCount = count;
    return row;
  }

  /// The dialog's scope combo, found by the values it carries.
  static QComboBox *scopeCombo(LauncherImportDialog &dialog) {
    for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
      if (combo->count() == 3 && combo->itemData(2).toInt() ==
                                     static_cast<int>(LauncherImportScopeChoice::AllRecognized)) {
        return combo;
      }
    }
    return nullptr;
  }

  static QCheckBox *checkFor(LauncherImportDialog &dialog, const QString &displayName) {
    for (QCheckBox *check : dialog.findChildren<QCheckBox *>()) {
      if (check->text().startsWith(displayName)) {
        return check;
      }
    }
    return nullptr;
  }

  static void selectScope(LauncherImportDialog &dialog, LauncherImportScopeChoice choice) {
    QComboBox *combo = scopeCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(static_cast<int>(choice)));
  }
};

void TestLauncherImportDialog::defaultsToOwnedScope() {
  // Owned, not the widest: it is the broadest tier that cannot list a game
  // the user does not own, so the default never imports unplayable entries.
  LauncherImportDialog dialog;
  dialog.setSources({steamRow()});
  QCOMPARE(dialog.selectedScope(), LauncherImportScopeChoice::Owned);
}

void TestLauncherImportDialog::rowLabelFollowsSelectedScope() {
  LauncherImportDialog dialog;
  dialog.setSources({steamRow()});

  selectScope(dialog, LauncherImportScopeChoice::InstalledOnly);
  QCheckBox *steam = checkFor(dialog, QStringLiteral("Steam"));
  QVERIFY(steam);
  QVERIFY2(steam->text().contains(QStringLiteral("3")),
           qPrintable(QStringLiteral("installed label was: ") + steam->text()));

  selectScope(dialog, LauncherImportScopeChoice::Owned);
  QVERIFY2(steam->text().contains(QStringLiteral("71")),
           qPrintable(QStringLiteral("owned label was: ") + steam->text()));

  selectScope(dialog, LauncherImportScopeChoice::AllRecognized);
  QVERIFY2(steam->text().contains(QStringLiteral("123")),
           qPrintable(QStringLiteral("recognized label was: ") + steam->text()));
}

void TestLauncherImportDialog::defaultCheckStateFollowsScopeCount() {
  // A source with nothing to import at the narrow tier but games at a wider
  // one: the default tick has to re-evaluate, or widening the scope would
  // leave the user staring at games the Import button refuses to take.
  LauncherImportDialog dialog;
  dialog.setSources({steamRow(/*installed=*/0, /*owned=*/71, /*recognized=*/123)});

  selectScope(dialog, LauncherImportScopeChoice::InstalledOnly);
  QCheckBox *steam = checkFor(dialog, QStringLiteral("Steam"));
  QVERIFY(steam);
  QVERIFY(!steam->isChecked());
  QVERIFY(dialog.selectedSourceIds().isEmpty());

  selectScope(dialog, LauncherImportScopeChoice::Owned);
  QVERIFY(steam->isChecked());
  QCOMPARE(dialog.selectedSourceIds(), QStringList{QStringLiteral("steam")});
}

void TestLauncherImportDialog::explicitUserChoiceSurvivesScopeChange() {
  // Re-defaulting is a convenience, not a veto: once the user has clicked the
  // box themselves, changing the scope may relabel the row but must not undo
  // their choice.
  LauncherImportDialog dialog;
  dialog.setSources({steamRow()});
  QCheckBox *steam = checkFor(dialog, QStringLiteral("Steam"));
  QVERIFY(steam);
  QVERIFY(steam->isChecked()); // default for an unimported source with games

  steam->click(); // user unticks
  QVERIFY(!steam->isChecked());

  selectScope(dialog, LauncherImportScopeChoice::AllRecognized);
  QVERIFY2(!steam->isChecked(), "scope change re-ticked a box the user had cleared");
  QVERIFY(dialog.selectedSourceIds().isEmpty());
  // …and the label still tracked the scope.
  QVERIFY(steam->text().contains(QStringLiteral("123")));
}

void TestLauncherImportDialog::nonSteamSourcesReadTheSameAtEveryScope() {
  // The scope is a Steam concept; Flatpak/Lutris counts must not move with
  // it, or the picker would imply a breadth those sources do not have.
  LauncherImportDialog dialog;
  dialog.setSources({flatpakRow(4)});
  QCheckBox *flatpak = checkFor(dialog, QStringLiteral("Flatpak"));
  QVERIFY(flatpak);

  selectScope(dialog, LauncherImportScopeChoice::InstalledOnly);
  const QString installedLabel = flatpak->text();
  selectScope(dialog, LauncherImportScopeChoice::AllRecognized);
  QCOMPARE(flatpak->text(), installedLabel);
}

QTEST_MAIN(TestLauncherImportDialog)
#include "test_launcherimportdialog.moc"
