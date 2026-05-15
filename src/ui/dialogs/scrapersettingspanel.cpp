#include "scrapersettingspanel.h"

#include "collectionutils.h"
#include "screenscraperparser.h"
#include "screenscraperprovider.h"
#include "settingsmodel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

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

  auto *form = new QFormLayout;
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  m_presetCombo = new QComboBox(this);
  m_presetCombo->addItem(tr("Fastest"), static_cast<int>(GeneralSettings::ScraperPreset::Fastest));
  m_presetCombo->addItem(tr("Balanced"),
                         static_cast<int>(GeneralSettings::ScraperPreset::Balanced));
  m_presetCombo->addItem(tr("Best Quality"),
                         static_cast<int>(GeneralSettings::ScraperPreset::BestQuality));
  m_presetCombo->addItem(tr("Custom"), static_cast<int>(GeneralSettings::ScraperPreset::Custom));
  m_presetCombo->setToolTip(tr("Fastest favors small downloads + high parallelism. Best Quality "
                               "requests original-resolution assets. Custom unlocks the three "
                               "numeric fields below."));
  form->addRow(tr("Preset:"), m_presetCombo);

  m_maxDimSpin = new QSpinBox(this);
  m_maxDimSpin->setRange(0, 8192);
  m_maxDimSpin->setSingleStep(64);
  m_maxDimSpin->setSpecialValueText(tr("Original (full resolution)"));
  m_maxDimSpin->setSuffix(tr(" px"));
  m_maxDimSpin->setToolTip(tr("Maximum width/height the scraper asks the provider for. "
                              "0 = full resolution. Server-side resizing dramatically "
                              "speeds up downloads but loses detail."));
  form->addRow(tr("Image max dimension:"), m_maxDimSpin);

  m_concurrencySpin = new QSpinBox(this);
  m_concurrencySpin->setRange(1, 16);
  m_concurrencySpin->setToolTip(tr("Number of media downloads that can run at the same time. "
                                   "Higher values saturate bandwidth faster but risk hitting "
                                   "the provider's per-account thread cap."));
  form->addRow(tr("Concurrent media downloads:"), m_concurrencySpin);

  m_throttleSpin = new QSpinBox(this);
  m_throttleSpin->setRange(0, 5000);
  m_throttleSpin->setSingleStep(25);
  m_throttleSpin->setSuffix(tr(" ms"));
  m_throttleSpin->setToolTip(tr("Minimum gap between consecutive media-download starts. "
                                "Higher values throttle the scraper; 0 disables pacing."));
  form->addRow(tr("Throttle between starts:"), m_throttleSpin);

  // JPG output checkbox — opted in by Fastest, opt-out elsewhere. JPG
  // re-encoding by SS shrinks images to roughly a third of the PNG
  // size at the cost of some fidelity (visible mostly on box art with
  // sharp text). Independent of mediaMaxDimension so a user can ask
  // for full-res JPG too.
  m_preferJpgCheck = new QCheckBox(tr("Prefer JPG output (smaller, lossy)"), this);
  m_preferJpgCheck->setToolTip(tr("Asks ScreenScraper to re-encode images as JPG instead of PNG. "
                                  "Roughly 3-5x smaller for typical scrape sizes; introduces "
                                  "JPEG artifacts so it's off by default for Balanced and "
                                  "Best Quality. Has no effect on videos or manuals."));
  form->addRow(QString(), m_preferJpgCheck);

  m_batchItemSpin = new QSpinBox(this);
  m_batchItemSpin->setRange(1, 16);
  m_batchItemSpin->setToolTip(tr("How many items can be scraped in parallel during a batch "
                                 "scrape. 1 = strictly serial (one item at a time). "
                                 "4-8 matches Skyscraper-style worker pools and is the biggest "
                                 "speed win for whole-collection batch runs. Doesn't affect "
                                 "single-item interactive scrapes (always 1 there).\n\n"
                                 "ScreenScraper docs define 'threads' as ROM-level parallelism "
                                 "(one in-flight ROM per thread). Setting this to your account's "
                                 "thread allowance matches SS's intended workload model. Click "
                                 "Detect below to read your current allowance from ssuserInfos."));
  form->addRow(tr("Batch: items in parallel:"), m_batchItemSpin);

  // Detect-threads row: a button kicks off ssuserInfos.php; the label
  // next to it shows the parsed result (or the SS-side error) once
  // the reply lands. Lets premium users see exactly which tier SS is
  // granting them without having to scrape a real ROM first.
  auto *detectRow = new QHBoxLayout;
  m_detectButton = new QPushButton(tr("Detect from SS account"), this);
  m_detectedThreadsLabel = new QLabel(tr("(not yet detected)"), this);
  m_detectedThreadsLabel->setStyleSheet("color: palette(mid); font-style: italic;");
  m_detectedThreadsLabel->setWordWrap(true);
  detectRow->addWidget(m_detectButton);
  detectRow->addWidget(m_detectedThreadsLabel, /*stretch=*/1);
  form->addRow(QString(), detectRow);

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
                               this);
  explainer->setWordWrap(true);
  explainer->setStyleSheet("color: palette(mid); padding: 4px 0px;");
  form->addRow(explainer);

  auto *separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  form->addRow(separator);

  m_rescrapeCombo = new QComboBox(this);
  m_rescrapeCombo->addItem(tr("Overwrite — replace existing assets"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::Overwrite));
  m_rescrapeCombo->addItem(tr("Fill missing — keep existing, only download what's missing"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::FillMissing));
  m_rescrapeCombo->addItem(tr("Update changed — compare bytes, write only if different"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::UpdateChanged));
  m_rescrapeCombo->addItem(tr("Skip — don't re-scrape items that already have metadata"),
                           static_cast<int>(GeneralSettings::ScraperRescrapeMode::Skip));
  m_rescrapeCombo->setToolTip(tr("Per-asset policy: applied independently to each cover / "
                                 "screenshot / fanart / etc. so a new asset still downloads "
                                 "even when other assets are kept."));
  form->addRow(tr("Re-scrape policy:"), m_rescrapeCombo);

  // The warning under the combo only shows when UpdateChanged is
  // selected. The mode has to download every asset anyway so it can
  // compare bytes — meaningfully slower than FillMissing.
  m_rescrapeWarning = new QLabel(tr("⚠ Update changed still pays the full download cost for every "
                                    "asset (the bytes have to land in memory before they can be "
                                    "compared). Use Fill missing for the fast path."),
                                 this);
  m_rescrapeWarning->setWordWrap(true);
  m_rescrapeWarning->setStyleSheet("color: palette(highlight); font-style: italic;");
  m_rescrapeWarning->hide();
  form->addRow(m_rescrapeWarning);

  // Auto-resume toggle (Kartend-1uvp). Off by default — first-time users
  // see the modal Resume / Discard prompt on next launch after an
  // interrupted scrape, which teaches them the recovery path. Power
  // users running unattended overnight batches flip this on so a crash
  // + relaunch self-heals without a dialog blocking the resume.
  m_autoResumeCheck = new QCheckBox(tr("Silently resume interrupted scrapes on next launch"), this);
  m_autoResumeCheck->setToolTip(
      tr("If a scrape is interrupted (process exit / crash mid-batch), "
         "Kartend writes a snapshot of the queue to "
         "pending-scrape.json. On next launch:\n\n"
         "• Off (default): a modal prompt asks whether to resume or "
         "discard the snapshot.\n"
         "• On: the queue resumes silently, and the Scraper window is "
         "raised so the Live view + Cancel/Close buttons are reachable."));
  form->addRow(QString(), m_autoResumeCheck);

  root->addLayout(form);
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
  if (m_autoResumeCheck) {
    QSignalBlocker b(m_autoResumeCheck);
    m_autoResumeCheck->setChecked(opts.scrapeAutoResume);
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
  if (m_autoResumeCheck) {
    opts.scrapeAutoResume = m_autoResumeCheck->isChecked();
  }
}
