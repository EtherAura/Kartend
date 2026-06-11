// Kartend-0eeuk: LauncherEditorDialog field mapping + preset locking. Pins
// the launcher() getter contract (trimmed fields, presetId carried only when
// the combo exists), the preset combo's fill-and-lock behavior (picking a
// preset stamps its values and disables the form, Inline re-enables it, an
// initial presetId preselects its combo row, blank preset names fall back to
// the id label), the retroarch-only core-row visibility that live-tracks the
// launcher path, and the detected-core dropdown driven off the
// retroarchConfigOverride directory (a temp dir keeps the host's real
// RetroArch install out of the test). Driven headlessly — never shown or
// exec()'d.

#include "launchereditordialog.h"

#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>

namespace {

QLineEdit *editWithPlaceholder(const QWidget &dlg, const QString &needle) {
  const auto edits = dlg.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->placeholderText().contains(needle)) {
      return e;
    }
  }
  return nullptr;
}

QLineEdit *nameEdit(const QWidget &dlg) {
  return editWithPlaceholder(dlg, QStringLiteral("Display name"));
}
QLineEdit *launcherEdit(const QWidget &dlg) {
  return editWithPlaceholder(dlg, QStringLiteral("Executable path"));
}
QLineEdit *coreEdit(const QWidget &dlg) {
  return editWithPlaceholder(dlg, QStringLiteral("Libretro core"));
}
QLineEdit *paramsEdit(const QWidget &dlg) {
  return editWithPlaceholder(dlg, QStringLiteral("Optional parameters"));
}

QComboBox *coreCombo(const QWidget &dlg) {
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->toolTip().contains(QStringLiteral("Cores detected"))) {
      return c;
    }
  }
  return nullptr;
}

QComboBox *presetCombo(const QWidget &dlg) {
  const auto combos = dlg.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->count() > 0 && c->itemText(0) == QStringLiteral("Inline (no preset)")) {
      return c;
    }
  }
  return nullptr;
}

LauncherPreset makePreset(const QString &id, const QString &name) {
  LauncherPreset p;
  p.id = id;
  p.name = name;
  p.launcherPath = QStringLiteral("/opt/%1/bin/run").arg(id);
  p.corePath = QString();
  p.launchParameters = QStringLiteral("--preset=%1").arg(id);
  return p;
}

LauncherConfig makeInitial() {
  LauncherConfig c;
  c.name = QStringLiteral("Player");
  c.launcherPath = QStringLiteral("/usr/bin/vlc");
  c.launchParameters = QStringLiteral("--fullscreen");
  return c;
}

} // namespace

class TestLauncherEditorDialog : public QObject {
  Q_OBJECT

private slots:
  void launcherGetterTrimsFieldsAndPreservesEmptyName();
  void initialConfigPopulatesForm();
  void coreRowVisibilityTracksRetroarchLauncherPath();
  void initialPresetIdPreselectsAndLocksFields();
  void pickingPresetFillsFieldsAndCarriesId();
  void switchingToInlineUnlocksFields();
  void blankPresetNameFallsBackToIdLabel();
  void coreComboListsCoresFromOverrideDirectory();
  void emptyOverrideDirectoryDisablesCoreCombo();
};

void TestLauncherEditorDialog::launcherGetterTrimsFieldsAndPreservesEmptyName() {
  LauncherEditorDialog dlg(nullptr, LauncherConfig{}, QStringLiteral("Add Launcher"));
  nameEdit(dlg)->setText(QStringLiteral("   ")); // whitespace-only stays empty
  launcherEdit(dlg)->setText(QStringLiteral("  /usr/bin/mpv  "));
  coreEdit(dlg)->setText(QStringLiteral("  /lib/core.so  "));
  paramsEdit(dlg)->setText(QStringLiteral(" --loop "));

  const LauncherConfig out = dlg.launcher();
  QCOMPARE(out.name, QString()); // chooser falls back to the exe basename
  QCOMPARE(out.launcherPath, QStringLiteral("/usr/bin/mpv"));
  QCOMPARE(out.corePath, QStringLiteral("/lib/core.so"));
  QCOMPARE(out.launchParameters, QStringLiteral("--loop"));
  // No preset list → no combo → no preset reference.
  QVERIFY(!presetCombo(dlg));
  QCOMPARE(out.presetId, QString());
}

void TestLauncherEditorDialog::initialConfigPopulatesForm() {
  const LauncherConfig initial = makeInitial();
  LauncherEditorDialog dlg(nullptr, initial, QStringLiteral("Edit Launcher"));
  QCOMPARE(nameEdit(dlg)->text(), initial.name);
  QCOMPARE(launcherEdit(dlg)->text(), initial.launcherPath);
  QCOMPARE(paramsEdit(dlg)->text(), initial.launchParameters);
  QCOMPARE(dlg.launcher(), initial);
}

void TestLauncherEditorDialog::coreRowVisibilityTracksRetroarchLauncherPath() {
  // Non-retroarch launcher: the libretro core row is noise and starts
  // hidden (seeded from the initial path, not just the live edit).
  LauncherEditorDialog dlg(nullptr, makeInitial(), QStringLiteral("Edit"));
  QWidget *coreRow = coreEdit(dlg)->parentWidget();
  QVERIFY(coreRow);
  QVERIFY(coreRow->isHidden());

  // Typing a retroarch path reveals the row the moment the text changes…
  launcherEdit(dlg)->setText(QStringLiteral("/usr/bin/retroarch"));
  QVERIFY(!coreRow->isHidden());

  // …case-insensitively, on the file name only…
  launcherEdit(dlg)->setText(QStringLiteral("C:/Games/RetroArch.exe"));
  QVERIFY(!coreRow->isHidden());

  // …and switching away hides it again. A retroarch-named directory on
  // the path must not count.
  launcherEdit(dlg)->setText(QStringLiteral("/opt/retroarch/bin/other-player"));
  QVERIFY(coreRow->isHidden());
}

void TestLauncherEditorDialog::initialPresetIdPreselectsAndLocksFields() {
  const QList<LauncherPreset> presets{makePreset(QStringLiteral("p1"), QStringLiteral("First")),
                                      makePreset(QStringLiteral("p2"), QStringLiteral("Second"))};
  LauncherConfig initial;
  initial.presetId = QStringLiteral("p2");

  LauncherEditorDialog dlg(nullptr, initial, QStringLiteral("Edit"), presets);
  QComboBox *combo = presetCombo(dlg);
  QVERIFY(combo);
  // Combo rows: 0 = Inline, presets at 1..N — p2 sits at 2.
  QCOMPARE(combo->currentIndex(), 2);

  // The preset drives the values and the form locks so the inline copy
  // can't silently desync from the preset.
  QCOMPARE(nameEdit(dlg)->text(), QStringLiteral("Second"));
  QCOMPARE(launcherEdit(dlg)->text(), QStringLiteral("/opt/p2/bin/run"));
  QCOMPARE(paramsEdit(dlg)->text(), QStringLiteral("--preset=p2"));
  QVERIFY(!nameEdit(dlg)->isEnabled());
  QVERIFY(!launcherEdit(dlg)->isEnabled());
  QVERIFY(!coreEdit(dlg)->isEnabled());
  QVERIFY(!paramsEdit(dlg)->isEnabled());

  const LauncherConfig out = dlg.launcher();
  QCOMPARE(out.presetId, QStringLiteral("p2"));
  QCOMPARE(out.launcherPath, QStringLiteral("/opt/p2/bin/run"));
}

void TestLauncherEditorDialog::pickingPresetFillsFieldsAndCarriesId() {
  const QList<LauncherPreset> presets{makePreset(QStringLiteral("p1"), QStringLiteral("First"))};
  LauncherEditorDialog dlg(nullptr, makeInitial(), QStringLiteral("Edit"), presets);
  QComboBox *combo = presetCombo(dlg);
  QVERIFY(combo);
  QCOMPARE(combo->currentIndex(), 0); // no initial presetId → Inline

  combo->setCurrentIndex(1);
  QCOMPARE(nameEdit(dlg)->text(), QStringLiteral("First"));
  QCOMPARE(launcherEdit(dlg)->text(), QStringLiteral("/opt/p1/bin/run"));
  QVERIFY(!nameEdit(dlg)->isEnabled());
  QCOMPARE(dlg.launcher().presetId, QStringLiteral("p1"));
}

void TestLauncherEditorDialog::switchingToInlineUnlocksFields() {
  const QList<LauncherPreset> presets{makePreset(QStringLiteral("p1"), QStringLiteral("First"))};
  LauncherConfig initial;
  initial.presetId = QStringLiteral("p1");
  LauncherEditorDialog dlg(nullptr, initial, QStringLiteral("Edit"), presets);
  QComboBox *combo = presetCombo(dlg);
  QVERIFY(combo);
  QVERIFY(!nameEdit(dlg)->isEnabled());

  combo->setCurrentIndex(0); // back to Inline
  // Fields re-enable with whatever was last shown (the preset's values).
  QVERIFY(nameEdit(dlg)->isEnabled());
  QVERIFY(launcherEdit(dlg)->isEnabled());
  QCOMPARE(launcherEdit(dlg)->text(), QStringLiteral("/opt/p1/bin/run"));
  // The reference is dropped: the launcher becomes a free-form inline copy.
  QCOMPARE(dlg.launcher().presetId, QString());
}

void TestLauncherEditorDialog::blankPresetNameFallsBackToIdLabel() {
  const QList<LauncherPreset> presets{makePreset(QStringLiteral("p9"), QStringLiteral("   "))};
  LauncherEditorDialog dlg(nullptr, LauncherConfig{}, QStringLiteral("Edit"), presets);
  QComboBox *combo = presetCombo(dlg);
  QVERIFY(combo);
  QCOMPARE(combo->itemText(1), QStringLiteral("p9"));
}

void TestLauncherEditorDialog::coreComboListsCoresFromOverrideDirectory() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  for (const QString &core :
       {QStringLiteral("beta_libretro.so"), QStringLiteral("alpha_libretro.so")}) {
    QFile f(dir.filePath(core));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
  }

  LauncherEditorDialog dlg(nullptr, LauncherConfig{}, QStringLiteral("Edit"), {},
                           /*retroarchConfigOverride=*/dir.path());
  QComboBox *combo = coreCombo(dlg);
  QVERIFY(combo);
  QVERIFY(combo->isEnabled());
  // Placeholder row + the two cores, sorted by stripped display name.
  QCOMPARE(combo->count(), 3);
  QCOMPARE(combo->itemText(1), QStringLiteral("alpha"));
  QCOMPARE(combo->itemText(2), QStringLiteral("beta"));

  // Activating a core stamps its path into the edit — the value
  // launcher() reads back.
  combo->setCurrentIndex(2);
  emit combo->activated(2);
  QCOMPARE(coreEdit(dlg)->text(), QDir(dir.path()).filePath(QStringLiteral("beta_libretro.so")));
  QCOMPARE(dlg.launcher().corePath, QDir(dir.path()).filePath(QStringLiteral("beta_libretro.so")));
}

void TestLauncherEditorDialog::emptyOverrideDirectoryDisablesCoreCombo() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LauncherEditorDialog dlg(nullptr, LauncherConfig{}, QStringLiteral("Edit"), {},
                           /*retroarchConfigOverride=*/dir.path());
  QComboBox *combo = coreCombo(dlg);
  QVERIFY(combo);
  QVERIFY(!combo->isEnabled());
  QCOMPARE(combo->count(), 1);
  QCOMPARE(combo->itemText(0), QStringLiteral("No cores detected"));

  // Activating the placeholder must not stamp anything into the edit.
  emit combo->activated(0);
  QVERIFY(coreEdit(dlg)->text().isEmpty());
}

QTEST_MAIN(TestLauncherEditorDialog)
#include "test_launchereditordialog.moc"
