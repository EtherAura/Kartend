#ifndef SCRAPERSETTINGSPANEL_H
#define SCRAPERSETTINGSPANEL_H

#include <QWidget>

class QCheckBox;
class QComboBox;
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
class ScraperSettingsPanel : public QWidget {
  Q_OBJECT
public:
  explicit ScraperSettingsPanel(QWidget *parent = nullptr);
  ~ScraperSettingsPanel() override;

  void setModel(SettingsModel *model);
  void refresh();

signals:
  void changed();

private:
  void buildLayout();
  void connectChangeSignals();
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
  /// Live read-out of the authenticated user's SS account state
  /// (thread allowance, daily quota usage). Updated by a ssuserInfos
  /// lookup triggered from the Detect button below the spinbox.
  QLabel *m_detectedThreadsLabel = nullptr;
  QPushButton *m_detectButton = nullptr;
  QComboBox *m_rescrapeCombo = nullptr;
  QLabel *m_rescrapeWarning = nullptr;
  bool m_loading = false; // suppress changed() during programmatic loads
};

#endif // SCRAPERSETTINGSPANEL_H
