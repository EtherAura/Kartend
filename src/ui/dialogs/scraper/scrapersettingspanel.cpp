#include "scrapersettingspanel.h"

#include "collection/generalsettings.h"
#include "scrapelogger.h"
#include "screenscraperparser.h"
#include "screenscraperprovider.h"
#include "settingsmodel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// Value-column widget width caps shared across the settings dialog so
// numeric fields and dropdowns don't stretch into oversized lozenges.
// Mirrors the per-.ui maximumWidth values in the rest of the panels.
constexpr int kSpinMaxWidth = 120;
constexpr int kComboMaxWidth = 320;
// Minimum width of the label column. Matches the `minimumSize` set on
// every QLabel in the other settings panels so label edges line up
// across groupboxes even when their label text is short.
constexpr int kLabelMinWidth = 200;

// Walk every row in the form, set the same minimum width on each
// label widget. QFormLayout::addRow(QString, …) auto-creates a QLabel
// that defaults to its text's sizeHint, so short labels collapse left
// of long ones. A uniform min width keeps the label column aligned.
void uniformLabelColumn(QFormLayout *form) {
  for (int row = 0; row < form->rowCount(); ++row) {
    QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
    if (!item) continue;
    if (auto *label = qobject_cast<QLabel *>(item->widget())) {
      label->setMinimumWidth(kLabelMinWidth);
    }
  }
}

// Preset snapshots — switching the combo to anything other than Custom
// stamps these onto the three numeric fields. Custom leaves the user's
// existing values alone (they get back the controls editability).
//
// Concurrency note: SS's per-account "threads allowed" cap (1-8
// depending on tier) does NOT scale bandwidth. SS enforces a fixed
// per-account bytes-per-second ceiling that gets split between
// however many concurrent streams you open. Empirical aggregate
// throughput measured on a premium 6-thread account:
//   concurrency=3 → 159 KiB/s aggregate
//   concurrency=6 → ~100 KiB/s aggregate (each stream slower)
// So presets bias LOW: more concurrency hurts more than it helps
// past the per-account cap. HTTP/2 is enabled to avoid the *network*-
// side TCP-fairness collapse (which would make this even worse), but
// the *server*-side rate cap is the real ceiling here.
struct PresetSnap {
  int maxDim;
  int concurrency;
  int throttleMs;
  // Stamps the SS `outputformat=jpg` query param onto image URLs. Only
  // the Fastest preset opts in by default — JPG is lossy, and the
  // Balanced/BestQuality presets favor fidelity. Custom users can flip
  // the checkbox manually without changing presets.
  bool preferJpg;
};
constexpr PresetSnap kFastest{640, 3, 50, true};
constexpr PresetSnap kBalanced{1024, 2, 100, false};
constexpr PresetSnap kBestQuality{0, 1, 150, false};

} // namespace

ScraperSettingsPanel::ScraperSettingsPanel(QWidget *parent) : QWidget(parent) {
  buildLayout();
  connectChangeSignals();
}

ScraperSettingsPanel::~ScraperSettingsPanel() = default;

void ScraperSettingsPanel::buildLayout() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(8);

  // ── Performance group ─────────────────────────────────────────
  // Rows ordered by widget type: dropdown first, then spin boxes,
  // then checkbox, then the Detect button + result label. The
  // explainer paragraph sits at the bottom of the group so the
  // form rows above stay tightly aligned.
  auto *perfGroup = new QGroupBox(tr("Performance"), this);
  auto *perfForm = new QFormLayout(perfGroup);
  perfForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  perfForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

  m_presetCombo = new QComboBox(perfGroup);
  m_presetCombo->addItem(tr("Fastest"), static_cast<int>(GeneralSettings::ScraperPreset::Fastest));
  m_presetCombo->addItem(tr("Balanced"),
                         static_cast<int>(GeneralSettings::ScraperPreset::Balanced));
  m_presetCombo->addItem(tr("Best Quality"),
                         static_cast<int>(GeneralSettings::ScraperPreset::BestQuality));
  m_presetCombo->addItem(tr("Custom"), static_cast<int>(GeneralSettings::ScraperPreset::Custom));
  m_presetCombo->setMaximumWidth(kComboMaxWidth);
  m_presetCombo->setToolTip(tr("Fastest favors small downloads + high parallelism. Best Quality "
                               "requests original-resolution assets. Custom unlocks the three "
                               "numeric fields below."));
  perfForm->addRow(tr("Preset:"), m_presetCombo);

  m_maxDimSpin = new QSpinBox(perfGroup);
  m_maxDimSpin->setRange(0, 8192);
  m_maxDimSpin->setSingleStep(64);
  m_maxDimSpin->setSpecialValueText(tr("Original (full resolution)"));
  m_maxDimSpin->setSuffix(tr(" px"));
  m_maxDimSpin->setMaximumWidth(kSpinMaxWidth);
  m_maxDimSpin->setToolTip(tr("Maximum width/height the scraper asks the provider for. "
                              "0 = full resolution. Server-side resizing dramatically "
                              "speeds up downloads but loses detail."));
  perfForm->addRow(tr("Image max dimension:"), m_maxDimSpin);

  m_concurrencySpin = new QSpinBox(perfGroup);
  m_concurrencySpin->setRange(1, 16);
  m_concurrencySpin->setMaximumWidth(kSpinMaxWidth);
  m_concurrencySpin->setToolTip(tr("Number of media downloads that can run at the same time. "
                                   "Higher values saturate bandwidth faster but risk hitting "
                                   "the provider's per-account thread cap."));
  perfForm->addRow(tr("Concurrent media downloads:"), m_concurrencySpin);

  m_throttleSpin = new QSpinBox(perfGroup);
  m_throttleSpin->setRange(0, 5000);
  m_throttleSpin->setSingleStep(25);
  m_throttleSpin->setSuffix(tr(" ms"));
  m_throttleSpin->setMaximumWidth(kSpinMaxWidth);
  m_throttleSpin->setToolTip(tr("Minimum gap between consecutive media-download starts. "
                                "Higher values throttle the scraper; 0 disables pacing."));
  perfForm->addRow(tr("Throttle between starts:"), m_throttleSpin);

  m_batchItemSpin = new QSpinBox(perfGroup);
  m_batchItemSpin->setRange(1, 16);
  m_batchItemSpin->setMaximumWidth(kSpinMaxWidth);
  m_batchItemSpin->setToolTip(tr("How many items can be scraped in parallel during a batch "
                                 "scrape. 1 = strictly serial (one item at a time). "
                                 "4-8 matches Skyscraper-style worker pools and is the biggest "
                                 "speed win for whole-collection batch runs. Doesn't affect "
                                 "single-item interactive scrapes (always 1 there).\n\n"
                                 "ScreenScraper docs define 'threads' as ROM-level parallelism "
                                 "(one in-flight ROM per thread). Setting this to your account's "
                                 "thread allowance matches SS's intended workload model. Click "
                                 "Detect below to read your current allowance from ssuserInfos."));
  perfForm->addRow(tr("Batch: items in parallel:"), m_batchItemSpin);

  // JPG output checkbox — opted in by Fastest, opt-out elsewhere. JPG
  // re-encoding by SS shrinks images to roughly a third of the PNG
  // size at the cost of some fidelity (visible mostly on box art with
  // sharp text). Independent of mediaMaxDimension so a user can ask
  // for full-res JPG too.
  m_preferJpgCheck = new QCheckBox(tr("Prefer JPG output (smaller, lossy)"), perfGroup);
  m_preferJpgCheck->setToolTip(tr("Asks ScreenScraper to re-encode images as JPG instead of PNG. "
                                  "Roughly 3-5x smaller for typical scrape sizes; introduces "
                                  "JPEG artifacts so it's off by default for Balanced and "
                                  "Best Quality. Has no effect on videos or manuals."));
  perfForm->addRow(QString(), m_preferJpgCheck);

  // Detect-threads row: a button kicks off ssuserInfos.php; the label
  // next to it shows the parsed result (or the SS-side error) once
  // the reply lands. Lets premium users see exactly which tier SS is
  // granting them without having to scrape a real ROM first.
  auto *detectRow = new QHBoxLayout;
  m_detectButton = new QPushButton(tr("Detect from SS account"), perfGroup);
  m_detectedThreadsLabel = new QLabel(tr("(not yet detected)"), perfGroup);
  // PlaceholderText foreground role gives the muted-secondary feel the
  // original mid-palette stylesheet aimed at, but with enough contrast
  // to stay readable. Reused below for the explainer paragraph and the
  // re-scrape warning.
  m_detectedThreadsLabel->setForegroundRole(QPalette::PlaceholderText);
  QFont detectedFont = m_detectedThreadsLabel->font();
  detectedFont.setItalic(true);
  m_detectedThreadsLabel->setFont(detectedFont);
  m_detectedThreadsLabel->setWordWrap(true);
  detectRow->addWidget(m_detectButton);
  detectRow->addWidget(m_detectedThreadsLabel, /*stretch=*/1);
  perfForm->addRow(QString(), detectRow);

  // Explainer block for interactive-vs-batch concurrency semantics.
  // The two knobs solve different problems: mediaConcurrency
  // (single ROM, multiple in-flight downloads per ROM) only helps
  // up to the per-account bytes/sec ceiling — past ~3 it actively
  // hurts because each stream gets a smaller slice. batchItemConcurrency
  // (multiple ROMs in flight) is SS's documented model and scales
  // close to linearly with your thread allowance.
  auto *explainer = new QLabel(tr("<b>Concurrent media downloads</b> applies within a single "
                                  "ROM (interactive single-item scrape). SS rate-limits per "
                                  "account; past ~3 simultaneous streams to one ROM, each "
                                  "stream gets a smaller slice of the same per-account budget. "
                                  "<br/><br/>"
                                  "<b>Batch: items in parallel</b> applies during a "
                                  "whole-collection batch scrape. SS's documented 'thread' "
                                  "model puts one ROM in flight per thread, so set this to "
                                  "your account's thread allowance for best wallclock."),
                               perfGroup);
  explainer->setWordWrap(true);
  explainer->setForegroundRole(QPalette::PlaceholderText);
  perfForm->addRow(explainer);

  root->addWidget(perfGroup);

  // ── Re-scrape & metadata group ──────────────────────────────
  // Same widget-type ordering: dropdowns first, spin box next,
  // checkboxes last. The warning + skip-window pair sit under
  // their owning combo so the conditional show/hide stays local.
  auto *behaviorGroup = new QGroupBox(tr("Re-scrape && Metadata"), this);
  auto *behaviorForm = new QFormLayout(behaviorGroup);
  behaviorForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  behaviorForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

  m_rescrapeCombo = new QComboBox(behaviorGroup);
  m_rescrapeCombo->addItem(tr("Overwrite — replace existing assets"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::Overwrite));
  m_rescrapeCombo->addItem(tr("Fill missing — keep existing, only download what's missing"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::FillMissing));
  // (Fill missing also skips the provider request entirely for items
  // whose checkboxes are already fully covered — see the refresh
  // window spinbox below.)
  m_rescrapeCombo->addItem(tr("Update changed — compare bytes, write only if different"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::UpdateChanged));
  m_rescrapeCombo->addItem(tr("Skip — don't re-scrape items that already have metadata"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::Skip));
  m_rescrapeCombo->setMaximumWidth(kComboMaxWidth);
  m_rescrapeCombo->setToolTip(tr("Per-asset policy: applied independently to each cover / "
                                 "screenshot / fanart / etc. so a new asset still downloads "
                                 "even when other assets are kept."));
  behaviorForm->addRow(tr("Re-scrape policy:"), m_rescrapeCombo);

  // The warning under the combo only shows when UpdateChanged is
  // selected. The mode has to download every asset anyway so it can
  // compare bytes — meaningfully slower than FillMissing. Bold + italic
  // gives emphasis without relying on the highlight role (a vivid
  // selection color that read as garish next to ordinary text).
  m_rescrapeWarning = new QLabel(tr("⚠ Update changed still pays the full download cost for every "
                                    "asset (the bytes have to land in memory before they can be "
                                    "compared). Use Fill missing for the fast path."),
                                 behaviorGroup);
  m_rescrapeWarning->setWordWrap(true);
  QFont warningFont = m_rescrapeWarning->font();
  warningFont.setBold(true);
  warningFont.setItalic(true);
  m_rescrapeWarning->setFont(warningFont);
  m_rescrapeWarning->hide();
  behaviorForm->addRow(m_rescrapeWarning);

  // Fallback region for ScreenScraper's region-keyed fields. Each
  // scraped item first honours its own matched-ROM region — a Japanese
  // cart keeps its Japanese title and box art — so this only decides
  // what to fall back to when the item's region has no entry. Free-text
  // fields (description, genres, ...) ignore this and follow the
  // application language instead. Data is the SS region shortname.
  m_regionCombo = new QComboBox(behaviorGroup);
  m_regionCombo->addItem(tr("World"), QStringLiteral("wor"));
  m_regionCombo->addItem(tr("USA"), QStringLiteral("us"));
  m_regionCombo->addItem(tr("Europe"), QStringLiteral("eu"));
  m_regionCombo->addItem(tr("Japan"), QStringLiteral("jp"));
  m_regionCombo->addItem(tr("United Kingdom"), QStringLiteral("uk"));
  m_regionCombo->addItem(tr("France"), QStringLiteral("fr"));
  m_regionCombo->addItem(tr("Germany"), QStringLiteral("de"));
  m_regionCombo->addItem(tr("Spain"), QStringLiteral("sp"));
  m_regionCombo->addItem(tr("Italy"), QStringLiteral("it"));
  m_regionCombo->addItem(tr("Brazil"), QStringLiteral("br"));
  m_regionCombo->addItem(tr("Australia"), QStringLiteral("au"));
  m_regionCombo->addItem(tr("Korea"), QStringLiteral("kr"));
  m_regionCombo->setMaximumWidth(kComboMaxWidth);
  m_regionCombo->setToolTip(tr("Each scraped item uses its own region for the title, release "
                               "date, and box art. This region is only the fallback for items "
                               "whose own region has no entry. Descriptions and other text "
                               "always follow the application language."));
  behaviorForm->addRow(tr("Fallback region:"), m_regionCombo);

  // Kartend-ou0a: hash-mode + region-source. Two new policy knobs so
  // users can opt out of hash-ID for slow archive collections and choose
  // when to trust their own filename's region tag over SS's match.
  m_hashModeCombo = new QComboBox(behaviorGroup);
  m_hashModeCombo->addItem(
      tr("Always hash"),
      static_cast<int>(GeneralSettings::ScraperOptions::ScraperHashMode::Always));
  m_hashModeCombo->addItem(
      tr("Skip large files…"),
      static_cast<int>(GeneralSettings::ScraperOptions::ScraperHashMode::SizeGated));
  m_hashModeCombo->addItem(
      tr("Never hash (filename only)"),
      static_cast<int>(GeneralSettings::ScraperOptions::ScraperHashMode::Never));
  m_hashModeCombo->setMaximumWidth(kComboMaxWidth);
  m_hashModeCombo->setToolTip(
      tr("ScreenScraper's hash-based ID is the most reliable match path but "
         "the slowest for big archives — extracting a multi-GB PS2 .zip can "
         "take several minutes per item.\n\n"
         "• Always hash: try every file regardless of size.\n"
         "• Skip large files: only hash files at or under the size limit "
         "(below); larger files fall back to filename-based matching.\n"
         "• Never hash: filename-only matching for every item."));
  behaviorForm->addRow(tr("ROM hashing:"), m_hashModeCombo);

  m_maxHashableSizeSpin = new QSpinBox(behaviorGroup);
  m_maxHashableSizeSpin->setRange(1, 65536);
  m_maxHashableSizeSpin->setSingleStep(256);
  m_maxHashableSizeSpin->setSuffix(tr(" MB"));
  m_maxHashableSizeSpin->setMaximumWidth(kSpinMaxWidth);
  m_maxHashableSizeSpin->setToolTip(
      tr("Files larger than this skip the hash step and fall through to "
         "filename-based matching. Only meaningful when ROM hashing is set "
         "to Skip large files."));
  m_maxHashableSizeLabel = new QLabel(tr("Skip hashing files over:"), behaviorGroup);
  behaviorForm->addRow(m_maxHashableSizeLabel, m_maxHashableSizeSpin);
  // Initial visibility set by load(); kept hidden until that point so the
  // form layout doesn't flash an unconfigured spinner.
  m_maxHashableSizeLabel->hide();
  m_maxHashableSizeSpin->hide();

  m_regionSourceCombo = new QComboBox(behaviorGroup);
  m_regionSourceCombo->addItem(
      tr("Trust scraper match (use filename only when hash fails)"),
      static_cast<int>(GeneralSettings::ScraperOptions::ScraperRegionSource::TrustScraperFirst));
  m_regionSourceCombo->addItem(
      tr("Trust filename region when present"),
      static_cast<int>(
          GeneralSettings::ScraperOptions::ScraperRegionSource::FilenameWhenAvailable));
  m_regionSourceCombo->addItem(
      tr("Always use scraper match (ignore filename)"),
      static_cast<int>(GeneralSettings::ScraperOptions::ScraperRegionSource::ScraperOnly));
  m_regionSourceCombo->setMaximumWidth(kComboMaxWidth);
  m_regionSourceCombo->setToolTip(
      tr("Picks the region used for the title, release date, and box art:\n\n"
         "• Trust scraper match: the scraper's matched-ROM region wins. "
         "If hashing didn't run (file too big, archive extract failed), "
         "the No-Intro region tag in the filename (e.g. '(Japan)') is "
         "used instead.\n"
         "• Trust filename region: a No-Intro tag in the filename always "
         "wins over the scraper match. Use this when ScreenScraper's per-"
         "file region tagging is unreliable (hash collisions between "
         "regional variants).\n"
         "• Always use scraper match: never look at the filename — useful "
         "when filenames are noisy and you trust hash-based identification."));
  behaviorForm->addRow(tr("Region detection:"), m_regionSourceCombo);

  // Refresh-window gate that pairs with both Skip and Fill missing.
  // Under Skip the filter drops items with any metadata; under
  // Fill missing it drops items whose checked fields are all already
  // on disk. The spinbox lets users mark items as eligible for
  // refresh once they pass the chosen age. 0 disables the window
  // (legacy: skip covered items regardless of age). Range 0..365 —
  // matches SettingsManager's clamp. Visible only when Skip or Fill
  // missing is the active rescrape mode; the label toggles in
  // lockstep so the form layout stays clean.
  m_skipRecentDaysSpin = new QSpinBox(behaviorGroup);
  m_skipRecentDaysSpin->setRange(0, 365);
  m_skipRecentDaysSpin->setSingleStep(1);
  m_skipRecentDaysSpin->setSuffix(tr(" days"));
  m_skipRecentDaysSpin->setSpecialValueText(tr("Always skip (no refresh)"));
  m_skipRecentDaysSpin->setMaximumWidth(kSpinMaxWidth);
  m_skipRecentDaysSpin->setToolTip(
      tr("Skip and Fill missing modes drop items whose data is already "
         "on disk — Skip if any metadata is present, Fill missing if "
         "every ticked checkbox in the scrape dialog has a file. Set "
         "this to a number of days to let those items become eligible "
         "for re-scraping after the chosen age — e.g. 30 means items "
         "covered longer than 30 days ago will refresh on the next "
         "batch.\n\n"
         "Items without a readable scrape timestamp (sidecar-only "
         "imports, hand-edited entries) stay skipped regardless of "
         "the window. 0 disables the window entirely so every covered "
         "item is skipped."));
  m_skipRecentDaysLabel = new QLabel(tr("Refresh items scraped before:"), behaviorGroup);
  behaviorForm->addRow(m_skipRecentDaysLabel, m_skipRecentDaysSpin);
  m_skipRecentDaysLabel->hide();
  m_skipRecentDaysSpin->hide();

  // Auto-resume toggle (Kartend-1uvp). Off by default — first-time users
  // see the modal Resume / Discard prompt on next launch after an
  // interrupted scrape, which teaches them the recovery path. Power
  // users running unattended overnight batches flip this on so a crash
  // + relaunch self-heals without a dialog blocking the resume.
  m_autoResumeCheck =
      new QCheckBox(tr("Silently resume interrupted scrapes on next launch"), behaviorGroup);
  m_autoResumeCheck->setToolTip(
      tr("If a scrape is interrupted (process exit / crash mid-batch), "
         "Kartend writes a snapshot of the queue to "
         "pending-scrape.json. On next launch:\n\n"
         "• Off (default): a modal prompt asks whether to resume or "
         "discard the snapshot.\n"
         "• On: the queue resumes silently, and the Scraper window is "
         "raised so the Live view + Cancel/Close buttons are reachable."));
  behaviorForm->addRow(QString(), m_autoResumeCheck);

  // Scrape logging toggle. Off by default — diagnostic logging has a
  // per-message disk-write cost, so it is opt-in for users debugging a
  // misbehaving or crashed scrape.
  m_scrapeLoggingCheck = new QCheckBox(tr("Enable scrape logging"), behaviorGroup);
  m_scrapeLoggingCheck->setToolTip(tr("Records detailed scrape activity to a log file so a scrape "
                                      "that crashes or stalls can be diagnosed afterwards (a "
                                      "windowed build has no visible console output).\n\n"
                                      "When on, scrape messages are written to:\n%1\n\n"
                                      "The file is size-capped; the previous run rolls over to "
                                      "scrape.log.old. Leave off for normal use.")
                                       .arg(ScrapeLogger::logFilePath()));
  behaviorForm->addRow(QString(), m_scrapeLoggingCheck);

  uniformLabelColumn(perfForm);
  uniformLabelColumn(behaviorForm);

  root->addWidget(behaviorGroup);
  root->addStretch(1);
}

void ScraperSettingsPanel::connectChangeSignals() {
  // Preset combo: snap the numeric fields when the user picks a
  // non-Custom preset, and gate editability on Custom only.
  connect(m_presetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (m_loading) return;
    applyPresetToFields();
    writeModel();
    emit changed();
  });

  // Numeric fields: editing one demotes the preset to "Custom" so the
  // dropdown doesn't lie about what's in effect.
  auto onNumericChanged = [this]() {
    if (m_loading) return;
    const auto preset =
        static_cast<GeneralSettings::ScraperPreset>(m_presetCombo->currentData().toInt());
    if (preset != GeneralSettings::ScraperPreset::Custom) {
      QSignalBlocker b(m_presetCombo);
      m_presetCombo->setCurrentIndex(
          m_presetCombo->findData(static_cast<int>(GeneralSettings::ScraperPreset::Custom)));
    }
    writeModel();
    emit changed();
  };
  connect(m_maxDimSpin, qOverload<int>(&QSpinBox::valueChanged), this, onNumericChanged);
  connect(m_concurrencySpin, qOverload<int>(&QSpinBox::valueChanged), this, onNumericChanged);
  connect(m_throttleSpin, qOverload<int>(&QSpinBox::valueChanged), this, onNumericChanged);
  // JPG toggle is part of the preset — flipping it manually demotes
  // the preset to Custom so the dropdown stays honest.
  connect(m_preferJpgCheck, &QCheckBox::toggled, this,
          [onNumericChanged](bool) { onNumericChanged(); });
  // batchItemConcurrency is independent of the preset — it doesn't
  // affect single-item interactive scrapes, only batch worker count.
  // Edits don't demote the preset to Custom.
  connect(m_batchItemSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });

  connect(m_rescrapeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (m_loading) return;
    const auto mode =
        static_cast<GeneralSettings::ScraperRescrapeMode>(m_rescrapeCombo->currentData().toInt());
    m_rescrapeWarning->setVisible(mode == GeneralSettings::ScraperRescrapeMode::UpdateChanged);
    // The refresh-window controls only have meaning under modes that
    // pre-filter the queue (Skip, Fill missing). Hide them under
    // Overwrite / Update changed where every item already flows
    // through to the provider regardless.
    const bool windowApplies = mode == GeneralSettings::ScraperRescrapeMode::Skip ||
                               mode == GeneralSettings::ScraperRescrapeMode::FillMissing;
    if (m_skipRecentDaysLabel) m_skipRecentDaysLabel->setVisible(windowApplies);
    if (m_skipRecentDaysSpin) m_skipRecentDaysSpin->setVisible(windowApplies);
    writeModel();
    emit changed();
  });

  // Skip-window spinbox: independent of preset; emits changed() so the
  // host dialog persists. The visibility tracks the rescrape combo (see
  // the lambda above); no toggling needed here.
  connect(m_skipRecentDaysSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });

  // Auto-resume is a behaviour toggle independent of the speed/quality
  // preset; flipping it doesn't demote the preset to Custom.
  connect(m_autoResumeCheck, &QCheckBox::toggled, this, [this](bool) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });

  // Scrape logging is a diagnostic toggle, also independent of the
  // preset.
  connect(m_scrapeLoggingCheck, &QCheckBox::toggled, this, [this](bool) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });

  // Fallback region is a metadata-content preference, independent of
  // the speed/quality preset.
  connect(m_regionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });

  // Kartend-ou0a: hash mode + size limit + region source. Hash-mode
  // change drives the size-spinner visibility (only SizeGated shows it)
  // and also writes back the model so the change is durable.
  connect(m_hashModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (m_loading) return;
    const auto mode = static_cast<GeneralSettings::ScraperOptions::ScraperHashMode>(
        m_hashModeCombo->currentData().toInt());
    const bool sizeGated = mode == GeneralSettings::ScraperOptions::ScraperHashMode::SizeGated;
    if (m_maxHashableSizeLabel) m_maxHashableSizeLabel->setVisible(sizeGated);
    if (m_maxHashableSizeSpin) m_maxHashableSizeSpin->setVisible(sizeGated);
    writeModel();
    emit changed();
  });
  connect(m_maxHashableSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });
  connect(m_regionSourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });

  // Detect-threads button: fire ssuserInfos.php with the current
  // member credentials and surface the result in the label next to
  // the button. QPointer guard so a panel destroyed before the
  // reply lands doesn't UAF.
  connect(m_detectButton, &QPushButton::clicked, this, [this]() {
    if (!m_model || !m_model->generalSettings) return;
    m_detectedThreadsLabel->setText(tr("Querying ScreenScraper…"));
    m_detectButton->setEnabled(false);
    QPointer<ScraperSettingsPanel> guard(this);
    ScreenScraperProviderHelpers::fetchUserInfo(
        m_model->generalSettings,
        [guard](ErrorUtils::Result<ScreenScraperParser::ScreenScraperUserInfo> r) {
          if (guard.isNull()) return;
          guard->m_detectButton->setEnabled(true);
          if (r.isError()) {
            guard->m_detectedThreadsLabel->setText(
                ScraperSettingsPanel::tr("Detection failed: %1").arg(r.error().message));
            return;
          }
          const auto &info = r.value();
          // Sanity-bound the API reply before stamping it into the
          // spinbox so a corrupt response can't drive the runtime
          // past safe limits. 1..16 matches the spinbox range.
          if (info.maxThreads > 0) {
            guard->m_batchItemSpin->setValue(qBound(1, info.maxThreads, 16));
          }
          QStringList parts;
          parts << ScraperSettingsPanel::tr("threads: %1").arg(info.maxThreads);
          if (!info.level.isEmpty()) {
            parts << ScraperSettingsPanel::tr("tier: %1").arg(info.level);
          }
          if (info.maxDownloadSpeedKBps > 0) {
            parts
                << ScraperSettingsPanel::tr("download cap: %1 KB/s").arg(info.maxDownloadSpeedKBps);
          }
          if (info.maxRequestsPerDay > 0) {
            // Inline the failed-lookup count when SS reports either a
            // KO quota or any KO requests today — gives the user a
            // heads-up before they hit HTTP 431.
            QString quotaPart = ScraperSettingsPanel::tr("%1 / %2 requests today")
                                    .arg(info.requestsToday)
                                    .arg(info.maxRequestsPerDay);
            if (info.maxRequestsKoPerDay > 0 || info.requestsKoToday > 0) {
              quotaPart += ScraperSettingsPanel::tr(" (%1/%2 failed)")
                               .arg(info.requestsKoToday)
                               .arg(info.maxRequestsKoPerDay);
            }
            parts << quotaPart;
          }
          if (info.maxRequestsPerMinute > 0) {
            parts << ScraperSettingsPanel::tr("%1 req/min").arg(info.maxRequestsPerMinute);
          }
          guard->m_detectedThreadsLabel->setText(parts.join(QStringLiteral(" · ")));
        });
  });
}

void ScraperSettingsPanel::applyPresetToFields() {
  const auto preset =
      static_cast<GeneralSettings::ScraperPreset>(m_presetCombo->currentData().toInt());
  PresetSnap snap;
  switch (preset) {
  case GeneralSettings::ScraperPreset::Fastest:
    snap = kFastest;
    break;
  case GeneralSettings::ScraperPreset::Balanced:
    snap = kBalanced;
    break;
  case GeneralSettings::ScraperPreset::BestQuality:
    snap = kBestQuality;
    break;
  case GeneralSettings::ScraperPreset::Custom:
    return; // leave existing values alone
  }
  // Block numeric signals while snapping so we don't re-trigger the
  // Custom-demotion path and stamp the user's selection back to Custom.
  QSignalBlocker b1(m_maxDimSpin);
  QSignalBlocker b2(m_concurrencySpin);
  QSignalBlocker b3(m_throttleSpin);
  QSignalBlocker b4(m_preferJpgCheck);
  m_maxDimSpin->setValue(snap.maxDim);
  m_concurrencySpin->setValue(snap.concurrency);
  m_throttleSpin->setValue(snap.throttleMs);
  m_preferJpgCheck->setChecked(snap.preferJpg);
}

void ScraperSettingsPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void ScraperSettingsPanel::refresh() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  m_loading = true;
  const auto &opts = m_model->generalSettings->scraperOptions;
  m_presetCombo->setCurrentIndex(m_presetCombo->findData(static_cast<int>(opts.preset)));
  m_maxDimSpin->setValue(opts.mediaMaxDimension);
  m_concurrencySpin->setValue(opts.mediaConcurrency);
  m_throttleSpin->setValue(opts.mediaThrottleMs);
  m_batchItemSpin->setValue(opts.batchItemConcurrency);
  m_preferJpgCheck->setChecked(opts.preferJpgOutput);
  m_rescrapeCombo->setCurrentIndex(m_rescrapeCombo->findData(static_cast<int>(opts.rescrapeMode)));
  m_rescrapeWarning->setVisible(opts.rescrapeMode ==
                                GeneralSettings::ScraperRescrapeMode::UpdateChanged);
  if (m_skipRecentDaysSpin) {
    QSignalBlocker b(m_skipRecentDaysSpin);
    m_skipRecentDaysSpin->setValue(qBound(0, opts.skipRecentScrapeDays, 365));
  }
  // Mirror the visibility rule from the rescrape-combo change handler.
  const bool windowApplies = opts.rescrapeMode == GeneralSettings::ScraperRescrapeMode::Skip ||
                             opts.rescrapeMode == GeneralSettings::ScraperRescrapeMode::FillMissing;
  if (m_skipRecentDaysLabel) m_skipRecentDaysLabel->setVisible(windowApplies);
  if (m_skipRecentDaysSpin) m_skipRecentDaysSpin->setVisible(windowApplies);
  if (m_autoResumeCheck) {
    QSignalBlocker b(m_autoResumeCheck);
    m_autoResumeCheck->setChecked(opts.scrapeAutoResume);
  }
  if (m_scrapeLoggingCheck) {
    QSignalBlocker b(m_scrapeLoggingCheck);
    m_scrapeLoggingCheck->setChecked(opts.scrapeLogging);
  }
  if (m_regionCombo) {
    const int regionIdx = m_regionCombo->findData(opts.preferredScraperRegion);
    // An unrecognised persisted region (hand-edited config) falls back
    // to the first entry rather than leaving the combo blank.
    m_regionCombo->setCurrentIndex(regionIdx >= 0 ? regionIdx : 0);
  }
  if (m_hashModeCombo) {
    const int idx = m_hashModeCombo->findData(static_cast<int>(opts.hashMode));
    m_hashModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  }
  if (m_maxHashableSizeSpin) {
    QSignalBlocker b(m_maxHashableSizeSpin);
    m_maxHashableSizeSpin->setValue(qBound(1, opts.maxHashableSizeMB, 65536));
  }
  const bool sizeGated =
      opts.hashMode == GeneralSettings::ScraperOptions::ScraperHashMode::SizeGated;
  if (m_maxHashableSizeLabel) m_maxHashableSizeLabel->setVisible(sizeGated);
  if (m_maxHashableSizeSpin) m_maxHashableSizeSpin->setVisible(sizeGated);
  if (m_regionSourceCombo) {
    const int idx = m_regionSourceCombo->findData(static_cast<int>(opts.regionSource));
    m_regionSourceCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  }
  m_loading = false;
}

void ScraperSettingsPanel::writeModel() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  auto &opts = m_model->generalSettings->scraperOptions;
  opts.preset = static_cast<GeneralSettings::ScraperPreset>(m_presetCombo->currentData().toInt());
  opts.mediaMaxDimension = m_maxDimSpin->value();
  opts.mediaConcurrency = m_concurrencySpin->value();
  opts.mediaThrottleMs = m_throttleSpin->value();
  opts.batchItemConcurrency = m_batchItemSpin->value();
  opts.preferJpgOutput = m_preferJpgCheck->isChecked();
  opts.rescrapeMode =
      static_cast<GeneralSettings::ScraperRescrapeMode>(m_rescrapeCombo->currentData().toInt());
  if (m_skipRecentDaysSpin) {
    opts.skipRecentScrapeDays = m_skipRecentDaysSpin->value();
  }
  if (m_autoResumeCheck) {
    opts.scrapeAutoResume = m_autoResumeCheck->isChecked();
  }
  if (m_scrapeLoggingCheck) {
    opts.scrapeLogging = m_scrapeLoggingCheck->isChecked();
  }
  if (m_regionCombo) {
    opts.preferredScraperRegion = m_regionCombo->currentData().toString();
  }
  if (m_hashModeCombo) {
    opts.hashMode = static_cast<GeneralSettings::ScraperOptions::ScraperHashMode>(
        m_hashModeCombo->currentData().toInt());
  }
  if (m_maxHashableSizeSpin) {
    opts.maxHashableSizeMB = m_maxHashableSizeSpin->value();
  }
  if (m_regionSourceCombo) {
    opts.regionSource = static_cast<GeneralSettings::ScraperOptions::ScraperRegionSource>(
        m_regionSourceCombo->currentData().toInt());
  }
}
