// Unit tests for GeneralSettings equality + save-time normalization
// (Kartend-6oqat). These pin down the contract the settings dialog's whole-
// struct dirty-check now depends on, replacing the ~210-line hand-enumerated
// checkGeneralSettingsChanges that silently went stale whenever a new deferred
// field was added (e.g. retroarchConfigPath, Kartend-hvdow):
//
//  - operator== covers every settings sub-struct and EXCLUDES the runtime-only
//    lastSelectedItems map.
//  - it detects a change in fields the old hand-enumeration never compared
//    (the completeness win).
//  - normalizedForSave() trims exactly the free-text path/title fields, so a
//    whitespace-only edit to one of them is not a change — while leaving every
//    other string field whitespace-sensitive.
#include "collection/generalsettings.h"

#include <QObject>
#include <QString>
#include <QTest>

class TestGeneralSettingsEquality : public QObject {
  Q_OBJECT
private slots:
  void defaultsCompareEqual();
  void lastSelectedItemsExcludedFromEquality();
  void changeInEachSubStructIsDetected();
  void previouslyUncheckedFieldsAreNowDetected();
  void normalizeTrimsFreeTextFieldsOnly();
  void whitespaceOnlyEditToTrimmedFieldIsNotDirty();
};

void TestGeneralSettingsEquality::defaultsCompareEqual() {
  const GeneralSettings a;
  const GeneralSettings b;
  QVERIFY(a == b);
  QVERIFY(!(a != b));
}

void TestGeneralSettingsEquality::lastSelectedItemsExcludedFromEquality() {
  GeneralSettings a;
  GeneralSettings b;
  // lastSelectedItems is runtime selection state (no INI key), not a user
  // setting — a difference there must NOT make the two compare unequal, or a
  // selection change would make the settings dialog look dirty.
  b.lastSelectedItems.insert(3, 42);
  QVERIFY2(a == b, "lastSelectedItems must not participate in settings equality");
}

void TestGeneralSettingsEquality::changeInEachSubStructIsDetected() {
  // One representative field per settings sub-struct: a difference in any of
  // them must flip equality (the operator covers the whole struct).
  {
    GeneralSettings g;
    g.input.rememberSelection = !g.input.rememberSelection;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.keybindings.keyConfirm = g.keybindings.keyConfirm + 1;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.gamepad.gamepadUseDpad = !g.gamepad.gamepadUseDpad;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.scraper.options.mediaConcurrency = g.scraper.options.mediaConcurrency + 1;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.scraper.credentials.insert(QStringLiteral("tmdb"),
                                 {{QStringLiteral("k"), QStringLiteral("v")}});
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.attract.attractModeEnabled = !g.attract.attractModeEnabled;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.marquee.marqueeMode = g.marquee.marqueeMode + 1;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.splash.bootSplashEnabled = !g.splash.bootSplashEnabled;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.runtimeDetection.runtimeDetectionEnabled = !g.runtimeDetection.runtimeDetectionEnabled;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.toolbar.toolbarShowGridViewButton = !g.toolbar.toolbarShowGridViewButton;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.view.showTitleInPlaceholder = !g.view.showTitleInPlaceholder;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.appearance.titleTintSaturation = g.appearance.titleTintSaturation + 1;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.startup.startupVideoEnabled = !g.startup.startupVideoEnabled;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.media.pixmapCacheSizeMB = g.media.pixmapCacheSizeMB + 1;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.history.historyEnabled = !g.history.historyEnabled;
    QVERIFY(g != GeneralSettings());
  }
  {
    GeneralSettings g;
    g.launchers.retroarchConfigPath = QStringLiteral("/etc/retroarch.cfg");
    QVERIFY(g != GeneralSettings());
  }
}

void TestGeneralSettingsEquality::previouslyUncheckedFieldsAreNowDetected() {
  // These fields exist on settings sub-structs but were NOT in the old
  // hand-enumerated checkGeneralSettingsChanges — changing them used to be
  // persisted on save yet never lit the Save button (the same latent bug class
  // as Kartend-hvdow). The whole-struct compare now catches them.
  {
    GeneralSettings g;
    g.appearance.uiTextZoomPercent = g.appearance.uiTextZoomPercent + 10;
    QVERIFY2(g != GeneralSettings(), "uiTextZoomPercent change must now be detected");
  }
  {
    GeneralSettings g;
    g.appearance.globalUiFontFamily = QStringLiteral("Noto Sans");
    QVERIFY2(g != GeneralSettings(), "globalUiFontFamily change must now be detected");
  }
  {
    GeneralSettings g;
    g.view.fullscreen = !g.view.fullscreen;
    QVERIFY2(g != GeneralSettings(), "view.fullscreen change must now be detected");
  }
}

void TestGeneralSettingsEquality::normalizeTrimsFreeTextFieldsOnly() {
  GeneralSettings g;
  g.launchers.retroarchConfigPath = QStringLiteral("  /etc/ra.cfg  ");
  g.splash.bootSplashTitle = QStringLiteral("  Hi  ");
  g.startup.homeViewLabel = QStringLiteral("  Home  ");
  // A field deliberately left out of the trim set stays whitespace-sensitive.
  g.marquee.marqueeScreenName = QStringLiteral("  HDMI-A-1  ");

  const GeneralSettings n = g.normalizedForSave();
  QCOMPARE(n.launchers.retroarchConfigPath, QStringLiteral("/etc/ra.cfg"));
  QCOMPARE(n.splash.bootSplashTitle, QStringLiteral("Hi"));
  QCOMPARE(n.startup.homeViewLabel, QStringLiteral("Home"));
  QCOMPARE(n.marquee.marqueeScreenName, QStringLiteral("  HDMI-A-1  "));
}

void TestGeneralSettingsEquality::whitespaceOnlyEditToTrimmedFieldIsNotDirty() {
  // The dialog dirty-check compares normalizedForSave() on both sides, so a
  // whitespace-only edit to a trimmed field reads as clean...
  GeneralSettings baseline;
  baseline.launchers.retroarchConfigPath = QStringLiteral("/etc/ra.cfg");
  GeneralSettings edited = baseline;
  edited.launchers.retroarchConfigPath = QStringLiteral("  /etc/ra.cfg  ");
  QVERIFY2(edited.normalizedForSave() == baseline.normalizedForSave(),
           "whitespace-only edit to a trimmed field must not register as a change");

  // ...while a whitespace edit to a non-trimmed field is still a real change.
  GeneralSettings edited2 = baseline;
  edited2.marquee.marqueeScreenName = QStringLiteral("HDMI-A-1 ");
  GeneralSettings base2 = baseline;
  base2.marquee.marqueeScreenName = QStringLiteral("HDMI-A-1");
  QVERIFY2(edited2.normalizedForSave() != base2.normalizedForSave(),
           "whitespace edit to a non-trimmed string field must still be detected");
}

QTEST_MAIN(TestGeneralSettingsEquality)
#include "test_generalsettingsequality.moc"
