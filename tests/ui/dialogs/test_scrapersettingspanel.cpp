// Kartend-0eeuk: ScraperSettingsPanel model round-trip + preset rules. The
// panel mirrors GeneralSettings::scraper.options through a SettingsModel;
// these tests pin the deferred-save contract: load() reflects every option
// into its widget without emitting changed() (the m_loading gate), picking a
// preset snaps the four tied fields to the canned snapshot, editing a tied
// numeric (or the JPG toggle) demotes the preset to Custom while the
// independent batch spinbox does not, the rescrape mode drives the warning /
// refresh-window visibility, the hash mode gates the size spinbox, load()
// clamps out-of-range persisted values, and save() flushes widgets over a
// model changed behind the panel's back. The network-backed Detect button is
// never clicked. Driven headlessly — the panel is never shown.

#include "scrapersettingspanel.h"

#include "collection/generalsettings.h"
#include "settingsmodel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTest>

namespace {

QComboBox *comboWithToolTip(const QWidget &panel, const QString &needle) {
  const auto combos = panel.findChildren<QComboBox *>();
  for (QComboBox *c : combos) {
    if (c->toolTip().contains(needle)) {
      return c;
    }
  }
  return nullptr;
}

QSpinBox *spinWithToolTip(const QWidget &panel, const QString &needle) {
  const auto spins = panel.findChildren<QSpinBox *>();
  for (QSpinBox *s : spins) {
    if (s->toolTip().contains(needle)) {
      return s;
    }
  }
  return nullptr;
}

QCheckBox *checkWithText(const QWidget &panel, const QString &needle) {
  const auto checks = panel.findChildren<QCheckBox *>();
  for (QCheckBox *c : checks) {
    if (c->text().contains(needle)) {
      return c;
    }
  }
  return nullptr;
}

QLabel *labelWithText(const QWidget &panel, const QString &needle) {
  const auto labels = panel.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->text().contains(needle)) {
      return l;
    }
  }
  return nullptr;
}

QComboBox *presetCombo(const QWidget &p) {
  return comboWithToolTip(p, QStringLiteral("Fastest favors"));
}
QComboBox *rescrapeCombo(const QWidget &p) {
  return comboWithToolTip(p, QStringLiteral("Per-asset policy"));
}
QComboBox *regionCombo(const QWidget &p) {
  return comboWithToolTip(p, QStringLiteral("only the fallback for items"));
}
QComboBox *hashModeCombo(const QWidget &p) {
  return comboWithToolTip(p, QStringLiteral("hash-based ID"));
}
QComboBox *regionSourceCombo(const QWidget &p) {
  return comboWithToolTip(p, QStringLiteral("Picks the region"));
}
QSpinBox *maxDimSpin(const QWidget &p) {
  return spinWithToolTip(p, QStringLiteral("Maximum width/height"));
}
QSpinBox *concurrencySpin(const QWidget &p) {
  return spinWithToolTip(p, QStringLiteral("Number of media downloads"));
}
QSpinBox *throttleSpin(const QWidget &p) {
  return spinWithToolTip(p, QStringLiteral("Minimum gap"));
}
QSpinBox *batchSpin(const QWidget &p) {
  return spinWithToolTip(p, QStringLiteral("items can be scraped in parallel"));
}
QSpinBox *skipDaysSpin(const QWidget &p) {
  return spinWithToolTip(p, QStringLiteral("drop items whose data"));
}
QSpinBox *maxHashableSpin(const QWidget &p) {
  return spinWithToolTip(p, QStringLiteral("skip the hash step"));
}
QCheckBox *preferJpgCheck(const QWidget &p) {
  return checkWithText(p, QStringLiteral("Prefer JPG"));
}

void pickData(QComboBox *combo, int data) {
  combo->setCurrentIndex(combo->findData(data));
}

} // namespace

class TestScraperSettingsPanel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

  void loadReflectsModelWithoutEmittingChanged();
  void presetPickSnapsTiedFieldsAndWritesModel();
  void numericEditDemotesPresetToCustom();
  void jpgToggleDemotesPresetToCustom();
  void batchSpinEditKeepsPreset();
  void rescrapeModeDrivesWarningAndRefreshWindowVisibility();
  void hashModeGatesSizeSpin();
  void loadClampsOutOfRangePersistedValues();
  void saveFlushesWidgetsOverExternalModelEdit();
};

void TestScraperSettingsPanel::loadReflectsModelWithoutEmittingChanged() {
  GeneralSettings gs;
  auto &opts = gs.scraper.options;
  opts.preset = ScraperPreset::Custom;
  opts.mediaMaxDimension = 777;
  opts.mediaConcurrency = 5;
  opts.mediaThrottleMs = 250;
  opts.batchItemConcurrency = 7;
  opts.preferJpgOutput = true;
  opts.rescrapeMode = ScraperRescrapeMode::UpdateChanged;
  opts.skipRecentScrapeDays = 42;
  opts.scrapeAutoResume = true;
  opts.scrapeLogging = true;
  opts.preferredScraperRegion = QStringLiteral("jp");
  opts.hashMode = ScraperOptions::ScraperHashMode::SizeGated;
  opts.maxHashableSizeMB = 512;
  opts.regionSource = ScraperOptions::ScraperRegionSource::FilenameWhenAvailable;

  SettingsModel model;
  model.generalSettings = &gs;

  ScraperSettingsPanel panel;
  QSignalSpy changed(&panel, &ScraperSettingsPanel::changed);
  panel.setModel(&model);

  // Programmatic loads must stay silent — a changed() here would mark the
  // settings dialog dirty the moment it opens.
  QCOMPARE(changed.count(), 0);

  QCOMPARE(presetCombo(panel)->currentData().toInt(), static_cast<int>(ScraperPreset::Custom));
  QCOMPARE(maxDimSpin(panel)->value(), 777);
  QCOMPARE(concurrencySpin(panel)->value(), 5);
  QCOMPARE(throttleSpin(panel)->value(), 250);
  QCOMPARE(batchSpin(panel)->value(), 7);
  QVERIFY(preferJpgCheck(panel)->isChecked());
  QCOMPARE(rescrapeCombo(panel)->currentData().toInt(),
           static_cast<int>(ScraperRescrapeMode::UpdateChanged));
  QCOMPARE(skipDaysSpin(panel)->value(), 42);
  QVERIFY(checkWithText(panel, QStringLiteral("Silently resume"))->isChecked());
  QVERIFY(checkWithText(panel, QStringLiteral("Enable scrape logging"))->isChecked());
  QCOMPARE(regionCombo(panel)->currentData().toString(), QStringLiteral("jp"));
  QCOMPARE(hashModeCombo(panel)->currentData().toInt(),
           static_cast<int>(ScraperOptions::ScraperHashMode::SizeGated));
  QCOMPARE(maxHashableSpin(panel)->value(), 512);
  QVERIFY(!maxHashableSpin(panel)->isHidden()); // SizeGated reveals the limit
  QCOMPARE(regionSourceCombo(panel)->currentData().toInt(),
           static_cast<int>(ScraperOptions::ScraperRegionSource::FilenameWhenAvailable));
  // UpdateChanged shows the warning and hides the refresh window.
  QVERIFY(!labelWithText(panel, QStringLiteral("Update changed still pays"))->isHidden());
  QVERIFY(skipDaysSpin(panel)->isHidden());
}

void TestScraperSettingsPanel::presetPickSnapsTiedFieldsAndWritesModel() {
  GeneralSettings gs; // defaults: Balanced
  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);
  QSignalSpy changed(&panel, &ScraperSettingsPanel::changed);

  pickData(presetCombo(panel), static_cast<int>(ScraperPreset::Fastest));
  QCOMPARE(maxDimSpin(panel)->value(), 640);
  QCOMPARE(concurrencySpin(panel)->value(), 3);
  QCOMPARE(throttleSpin(panel)->value(), 50);
  QVERIFY(preferJpgCheck(panel)->isChecked()); // only Fastest opts into JPG
  QCOMPARE(changed.count(), 1);
  QCOMPARE(gs.scraper.options.preset, ScraperPreset::Fastest);
  QCOMPARE(gs.scraper.options.mediaMaxDimension, 640);
  QCOMPARE(gs.scraper.options.preferJpgOutput, true);

  pickData(presetCombo(panel), static_cast<int>(ScraperPreset::BestQuality));
  QCOMPARE(maxDimSpin(panel)->value(), 0); // 0 = full resolution
  QCOMPARE(concurrencySpin(panel)->value(), 1);
  QCOMPARE(throttleSpin(panel)->value(), 150);
  QVERIFY(!preferJpgCheck(panel)->isChecked());
  QCOMPARE(changed.count(), 2);

  // Picking Custom leaves the values alone — no snap-back.
  pickData(presetCombo(panel), static_cast<int>(ScraperPreset::Custom));
  QCOMPARE(maxDimSpin(panel)->value(), 0);
  QCOMPARE(gs.scraper.options.preset, ScraperPreset::Custom);
}

void TestScraperSettingsPanel::numericEditDemotesPresetToCustom() {
  GeneralSettings gs; // Balanced
  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);
  QSignalSpy changed(&panel, &ScraperSettingsPanel::changed);

  maxDimSpin(panel)->setValue(999);
  // The dropdown must not lie about what is in effect.
  QCOMPARE(presetCombo(panel)->currentData().toInt(), static_cast<int>(ScraperPreset::Custom));
  QCOMPARE(gs.scraper.options.preset, ScraperPreset::Custom);
  QCOMPARE(gs.scraper.options.mediaMaxDimension, 999);
  // The other tied fields keep their Balanced values — demotion is not a
  // snap.
  QCOMPARE(gs.scraper.options.mediaConcurrency, 2);
  QCOMPARE(changed.count(), 1);
}

void TestScraperSettingsPanel::jpgToggleDemotesPresetToCustom() {
  GeneralSettings gs; // Balanced, preferJpg false
  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);

  preferJpgCheck(panel)->setChecked(true);
  QCOMPARE(presetCombo(panel)->currentData().toInt(), static_cast<int>(ScraperPreset::Custom));
  QCOMPARE(gs.scraper.options.preferJpgOutput, true);
}

void TestScraperSettingsPanel::batchSpinEditKeepsPreset() {
  GeneralSettings gs; // Balanced
  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);
  QSignalSpy changed(&panel, &ScraperSettingsPanel::changed);

  batchSpin(panel)->setValue(9);
  // Batch parallelism is independent of the speed/quality preset.
  QCOMPARE(presetCombo(panel)->currentData().toInt(), static_cast<int>(ScraperPreset::Balanced));
  QCOMPARE(gs.scraper.options.preset, ScraperPreset::Balanced);
  QCOMPARE(gs.scraper.options.batchItemConcurrency, 9);
  QCOMPARE(changed.count(), 1);
}

void TestScraperSettingsPanel::rescrapeModeDrivesWarningAndRefreshWindowVisibility() {
  GeneralSettings gs; // FillMissing default
  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);

  QLabel *warning = labelWithText(panel, QStringLiteral("Update changed still pays"));
  QSpinBox *days = skipDaysSpin(panel);
  QVERIFY(warning);
  QVERIFY(days);

  // FillMissing (from load): refresh window applies, no warning.
  QVERIFY(warning->isHidden());
  QVERIFY(!days->isHidden());

  pickData(rescrapeCombo(panel), static_cast<int>(ScraperRescrapeMode::UpdateChanged));
  QVERIFY(!warning->isHidden());
  QVERIFY(days->isHidden());
  QCOMPARE(gs.scraper.options.rescrapeMode, ScraperRescrapeMode::UpdateChanged);

  pickData(rescrapeCombo(panel), static_cast<int>(ScraperRescrapeMode::Skip));
  QVERIFY(warning->isHidden());
  QVERIFY(!days->isHidden());

  pickData(rescrapeCombo(panel), static_cast<int>(ScraperRescrapeMode::Overwrite));
  QVERIFY(warning->isHidden());
  QVERIFY(days->isHidden());
}

void TestScraperSettingsPanel::hashModeGatesSizeSpin() {
  GeneralSettings gs; // Always default
  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);

  QSpinBox *limit = maxHashableSpin(panel);
  QVERIFY(limit);
  QVERIFY(limit->isHidden()); // Always-hash hides the limit

  pickData(hashModeCombo(panel), static_cast<int>(ScraperOptions::ScraperHashMode::SizeGated));
  QVERIFY(!limit->isHidden());
  QCOMPARE(gs.scraper.options.hashMode, ScraperOptions::ScraperHashMode::SizeGated);

  pickData(hashModeCombo(panel), static_cast<int>(ScraperOptions::ScraperHashMode::Never));
  QVERIFY(limit->isHidden());
  QCOMPARE(gs.scraper.options.hashMode, ScraperOptions::ScraperHashMode::Never);
}

void TestScraperSettingsPanel::loadClampsOutOfRangePersistedValues() {
  // Hand-edited config: values outside the documented ranges must land
  // clamped in the widgets, and an unknown region falls back to entry 0.
  GeneralSettings gs;
  gs.scraper.options.skipRecentScrapeDays = 9999;
  gs.scraper.options.maxHashableSizeMB = 0;
  gs.scraper.options.preferredScraperRegion = QStringLiteral("zz");

  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);

  QCOMPARE(skipDaysSpin(panel)->value(), 365);
  QCOMPARE(maxHashableSpin(panel)->value(), 1);
  QCOMPARE(regionCombo(panel)->currentIndex(), 0);
  QCOMPARE(regionCombo(panel)->currentData().toString(), QStringLiteral("wor"));
}

void TestScraperSettingsPanel::saveFlushesWidgetsOverExternalModelEdit() {
  GeneralSettings gs;
  SettingsModel model;
  model.generalSettings = &gs;
  ScraperSettingsPanel panel;
  panel.setModel(&model);

  throttleSpin(panel)->setValue(425); // writes through + demotes to Custom

  // Simulate something else clobbering the model behind the panel's back;
  // save() must restore the widgets' truth via the writeModel flush.
  gs.scraper.options.mediaThrottleMs = 1;
  gs.scraper.options.preset = ScraperPreset::Balanced;
  panel.save();
  QCOMPARE(gs.scraper.options.mediaThrottleMs, 425);
  QCOMPARE(gs.scraper.options.preset, ScraperPreset::Custom);
}

QTEST_MAIN(TestScraperSettingsPanel)
#include "test_scrapersettingspanel.moc"
