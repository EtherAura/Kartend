#ifndef SCRAPERSETTINGSPANEL_H
#define SCRAPERSETTINGSPANEL_H

#include "isettingspanel.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
struct SettingsModel;

/// Settings panel for the scraper subsystem: speed/quality presets
/// (Fastest / Balanced / BestQuality / Custom), four numeric pacing
/// fields, and the per-asset re-scrape policy. Switching the preset
/// snaps the four numeric fields to a canned set; picking Custom
/// unlocks them for manual editing. Mirrors the deferred-save shape
/// of `GeneralSettingsPanel` — emit `changed()` and the host dialog
/// persists.
class ScraperSettingsPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScraperSettingsPanel)
public:
  explicit ScraperSettingsPanel(QWidget *parent = nullptr);
  ~ScraperSettingsPanel() override;

  void setModel(SettingsModel *model);
  // ISettingsPanel (Kartend-ny2ki). load() was refresh(); save() flushes via
  // the existing private writeModel() (the panel's live-edit flush); clear()
  // is a no-op — this global panel is always backed by the single live model.
  void load() override;
  void save() override;
  void clear() override {}

signals:
  void changed();

private slots:
  /// Detect-threads action (Kartend-1adgj): fire ssuserInfos.php with the
  /// current member credentials and surface the result in the label beside
  /// the button. Extracted from the connectChangeSignals lambda so the
  /// threading / ScreenScraperProviderHelpers logic stays out of the wiring.
  void onDetectThreadsClicked();

private:
  void buildLayout();
  /// Performance / Re-scrape group builders (Kartend-1adgj): each constructs
  /// one QGroupBox + QFormLayout with its rows + per-widget configuration.
  /// buildLayout composes both and applies the uniformLabelColumn fix-ups.
  QGroupBox *buildPerformanceGroup();
  QGroupBox *buildBehaviorGroup();
  void connectChangeSignals();
  /// Single source of truth (Kartend-1adgj) for the three conditional gates —
  /// rescrape warning, skip-recent-days, max-hashable-size — derived from the
  /// current combo selections. Called from load() and the relevant change
  /// handlers instead of duplicating the show/hide rules at each site.
  void updateConditionalVisibility();
  void applyPresetToFields();
  void writeModel();

  SettingsModel *m_model = nullptr;

  QComboBox *m_presetCombo = nullptr;
  QSpinBox *m_maxDimSpin = nullptr;
  QSpinBox *m_concurrencySpin = nullptr;
  QSpinBox *m_throttleSpin = nullptr;
  QSpinBox *m_batchItemSpin = nullptr;
  QCheckBox *m_preferJpgCheck = nullptr;
  /// Toggle for silent auto-resume of an interrupted scrape on next
  /// launch (Kartend-1uvp). Off by default so first-time users see the
  /// modal Resume / Discard prompt and learn the recovery path.
  QCheckBox *m_autoResumeCheck = nullptr;
  QCheckBox *m_autoScrapeEntityArtCheck = nullptr;
  /// Toggle for scrape diagnostic logging. Off by default; when on,
  /// scrape activity is written to `scrape.log` in the config dir for
  /// post-mortem diagnosis of a misbehaving or crashed scrape.
  QCheckBox *m_scrapeLoggingCheck = nullptr;
  /// Live read-out of the authenticated user's SS account state
  /// (thread allowance, daily quota usage). Updated by a ssuserInfos
  /// lookup triggered from the Detect button below the spinbox.
  QLabel *m_detectedThreadsLabel = nullptr;
  QPushButton *m_detectButton = nullptr;
  QComboBox *m_rescrapeCombo = nullptr;
  QLabel *m_rescrapeWarning = nullptr;
  /// "Skip if scraped within the last N days" gate for Skip rescrape
  /// mode. 0 disables (legacy "always skip already-scraped" behaviour);
  /// N > 0 lets items become eligible for refresh after N days. Only
  /// rendered (label + spinbox) while Skip mode is selected — the value
  /// has no effect in the other modes.
  QSpinBox *m_skipRecentDaysSpin = nullptr;
  QLabel *m_skipRecentDaysLabel = nullptr;
  /// Fallback region for ScreenScraper's region-keyed fields (title,
  /// release date, box art). Each item still honours its own
  /// matched-ROM region first; this only backstops items whose region
  /// has no entry. Stores the SS region shortname as item data.
  QComboBox *m_regionCombo = nullptr;
  /// Hash-mode policy (Kartend-ou0a). See
  /// ScraperOptions::ScraperHashMode for semantics.
  /// SizeGated reveals m_maxHashableSizeSpin; the other two hide it.
  QComboBox *m_hashModeCombo = nullptr;
  QSpinBox *m_maxHashableSizeSpin = nullptr;
  QLabel *m_maxHashableSizeLabel = nullptr;
  /// Region-source policy (Kartend-ou0a). See
  /// ScraperOptions::ScraperRegionSource for semantics.
  QComboBox *m_regionSourceCombo = nullptr;
  bool m_loading = false; // suppress changed() during programmatic loads
};

#endif // SCRAPERSETTINGSPANEL_H
