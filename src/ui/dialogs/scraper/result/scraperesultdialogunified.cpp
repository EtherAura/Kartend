// Implementation for ScrapeResultDialogUnified — the unified-flow half of
// ScrapeResultDialog. Methods here read / write host state and host UI
// widgets via the friend-class privilege declared on ScrapeResultDialog.
// The collection-selection, thumbnail, and value-marquee responsibilities
// live in the sibling helpers ScrapeResultSelectionModel,
// ScrapeResultThumbnailLoader, and ValueMarqueeTicker (each owns the state
// its methods mutate); this class keeps the live-view builder, the
// auto / interactive queue walker, and the ScraperService signal handlers.
#include "scraperesultdialogunified.h"

#include "scraperesultdialog.h"

#include "applicationcontext.h"
#include "batchprogressview.h"
#include "collection/collectionconfig.h"
#include "collection/validationhelpers.h"
#include "flowlayout.h"
#include "formbuilders.h"
#include "isettingsmanager.h"
#include "mediatypecheckboxbuilder.h"
#include "metadataproviderregistry.h"
#include "scraperesultselectionmodel.h"
#include "scraperesultthumbnailloader.h"
#include "singleitemview.h"
#include "uiconstants/dialog.h"
#include "valuemarqueeticker.h"

#include <limits>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "idatabasemanager.h"
#include "metadatalookupprovider.h"
#include "pathutils.h"
#include "scrapejobgrouping.h"
#include "scrapelogging.h"

ScrapeResultDialogUnified::ScrapeResultDialogUnified(ScrapeResultDialog *dlg)
    : QObject(dlg), m_dlg(dlg) {}

ScrapeResultDialogUnified::~ScrapeResultDialogUnified() = default;

void ScrapeResultDialogUnified::resetRunState() {
  // Run-scoped controller state. The session-scoped members
  // (m_allSeenCustomKeys / m_customFieldEdits / m_typedChipCount and
  // m_lastQuotaResetText) deliberately survive across runs — see the
  // header docs.
  m_rateSamples.clear();
  m_interactiveItems.clear();
  m_shownCollectionName.clear();
}

namespace {
/// Shorten provider-specific keys for the label cell so every label
/// fits in the fixed label-column width. Common SS prefixes
/// (classification_/screenscraper_/rom_) get stripped because they
/// add no info once the keys are grouped together. Underscores
/// collapse to spaces. Full original key still shows in the cell's
/// tooltip via the caller.
QString prettifyCustomKey(const QString &key) {
  QString r = key;
  if (r.startsWith(QLatin1String("classification_"))) {
    r = r.mid(QStringLiteral("classification_").length()).toUpper();
  } else if (r.startsWith(QLatin1String("screenscraper_"))) {
    r = r.mid(QStringLiteral("screenscraper_").length());
  } else if (r.startsWith(QLatin1String("rom_"))) {
    r = r.mid(QStringLiteral("rom_").length());
  }
  r.replace(QLatin1Char('_'), QLatin1Char(' '));
  return r;
}
} // namespace

QList<Scraper::MediaAsset> ScrapeResultDialogUnified::selectInteractiveMediaForApply(
    const Scraper::ScrapedItem &detail) const {
  // Derive the asset list from the setup-view media-type checkboxes
  // (same filter auto-mode uses). Each matching MediaAsset from the
  // candidate detail is queued for the host's download pass.
  QSet<QString> filter;
  for (auto it = m_dlg->m_mediaTypeChecks.constBegin(); it != m_dlg->m_mediaTypeChecks.constEnd();
       ++it) {
    if (it.key() == QLatin1String("_metadata")) continue;
    if (it.value()->isChecked()) filter.insert(it.key().toLower());
  }
  QList<Scraper::MediaAsset> selected;
  for (const auto &asset : detail.media) {
    if (!asset.url.isValid()) continue;
    if (filter.isEmpty()) {
      // Empty filter falls back to "front cover only" — same legacy
      // behaviour BatchScrapeRunner uses.
      if (asset.type.compare(QStringLiteral("front"), Qt::CaseInsensitive) == 0) {
        selected.append(asset);
        break;
      }
    } else if (filter.contains(asset.type.toLower())) {
      selected.append(asset);
    }
  }
  return selected;
}

void ScrapeResultDialogUnified::buildUnifiedPanel() {
  // Kartend-etbol: this method used to construct the entire unified panel
  // inline (421 lines). It is now a short assembler that stitches together
  // one helper per UI section, adding each to the page's root layout in the
  // original top-to-bottom order. The helpers build the exact same widget
  // tree and member wiring as before — pure structural extraction.
  m_dlg->m_unifiedPage = new QWidget(m_dlg->m_modeStack);
  auto *root = new QVBoxLayout(m_dlg->m_unifiedPage);
  root->setContentsMargins(UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                           UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                           UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                           UIConstants::ScrapeResultDialog::CONTENT_MARGIN);
  root->setSpacing(UIConstants::ScrapeResultDialog::ROOT_LAYOUT_SPACING);

  // ── Setup view: a vertical splitter so the collection/items panel gets the
  //    majority of the height and the user can drag the divider for even more
  //    room (Kartend-1hose). Tree/list on top; the "What to scrape" + mode +
  //    scrape-options controls sit below inside a scroll area so they never crowd
  //    out the list and gracefully scroll when the window is short.
  auto *setupSplit = new QSplitter(Qt::Vertical, m_dlg->m_unifiedPage);
  m_dlg->m_setupVerticalSplitter = setupSplit;
  setupSplit->setChildrenCollapsible(false);
  setupSplit->addWidget(buildCollectionAndItemsPanel());

  QGroupBox *mediaTypesGroup = nullptr;
  QWidget *modeRowContainer = nullptr;
  buildMediaTypesGroup(mediaTypesGroup, modeRowContainer);
  auto *controls = new QWidget(setupSplit);
  auto *controlsCol = new QVBoxLayout(controls);
  controlsCol->setContentsMargins(0, 0, 0, 0);
  controlsCol->setSpacing(UIConstants::ScrapeResultDialog::ROOT_LAYOUT_SPACING);
  controlsCol->addWidget(mediaTypesGroup);
  controlsCol->addWidget(modeRowContainer);
  controlsCol->addWidget(buildScrapeOptionsGroup());
  // No scroll area: the default window size (below) is tall enough to show every
  // control, and a scrollbar here would shift the 3-column media grid and
  // misalign it. The controls keep their full height; extra vertical space goes
  // to the tree/list, which the user can grow further by dragging the divider.
  setupSplit->addWidget(controls);
  setupSplit->setStretchFactor(0, 1);
  setupSplit->setStretchFactor(1, 0);
  root->addWidget(setupSplit, 1);

  // ── Live view: currently-scraping metadata panel ────────────────
  root->addWidget(buildLiveMetadataPanel());

  // ── Live view: recent media + progress/status labels ────────────
  QGroupBox *thumbsGroup = nullptr;
  QWidget *currentLabel = nullptr;
  QWidget *progressBar = nullptr;
  QWidget *timingLabel = nullptr;
  QWidget *countsLabel = nullptr;
  QWidget *quotaLabel = nullptr;
  buildProgressLabels(thumbsGroup, currentLabel, progressBar, timingLabel, countsLabel, quotaLabel);
  root->addWidget(thumbsGroup);
  root->addWidget(currentLabel);
  root->addWidget(progressBar);
  root->addWidget(timingLabel);
  root->addWidget(countsLabel);
  root->addWidget(quotaLabel);
}

QWidget *ScrapeResultDialogUnified::buildCollectionAndItemsPanel() {
  // ── Top: collection tree (left) + items list (right) ────────────
  auto *splitter = new QSplitter(Qt::Horizontal, m_dlg->m_unifiedPage);
  m_dlg->m_unifiedSplitterContainer = splitter; // tracked so we can hide during a run

  m_dlg->m_collectionTree = new QTreeWidget(splitter);
  m_dlg->m_collectionTree->setHeaderLabel(tr("Collections"));
  m_dlg->m_collectionTree->setMinimumWidth(
      UIConstants::ScrapeResultDialog::COLLECTION_TREE_MIN_WIDTH);
  m_dlg->m_collectionTree->setRootIsDecorated(true);
  m_dlg->m_collectionTree->setAnimated(true);
  m_dlg->m_collectionTree->header()->setStretchLastSection(true);
  connect(m_dlg->m_collectionTree, &QTreeWidget::currentItemChanged, m_dlg->m_selectionModel.get(),
          &ScrapeResultSelectionModel::onCollectionTreeCurrentChanged);
  connect(m_dlg->m_collectionTree, &QTreeWidget::itemChanged, m_dlg->m_selectionModel.get(),
          &ScrapeResultSelectionModel::onCollectionCheckChanged);
  splitter->addWidget(m_dlg->m_collectionTree);

  auto *rightContainer = new QWidget(splitter);
  auto *rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto *itemsHeaderRow = new QHBoxLayout;
  m_dlg->m_itemsHeaderLabel =
      new QLabel(tr("Select a collection to see its items."), rightContainer);
  itemsHeaderRow->addWidget(m_dlg->m_itemsHeaderLabel, 1);
  auto *itemsSelectAll = new QPushButton(tr("Select all"), rightContainer);
  auto *itemsSelectNone = new QPushButton(tr("Select none"), rightContainer);
  itemsHeaderRow->addWidget(itemsSelectAll);
  itemsHeaderRow->addWidget(itemsSelectNone);
  rightLayout->addLayout(itemsHeaderRow);
  // Bulk-toggle every visible row's checkbox + the underlying
  // inclusion map; mirrors what a user would do row-by-row. No-op
  // when no collection is currently displayed.
  connect(itemsSelectAll, &QPushButton::clicked, this,
          [this]() { m_dlg->m_selectionModel->setAllItemsChecked(true); });
  connect(itemsSelectNone, &QPushButton::clicked, this,
          [this]() { m_dlg->m_selectionModel->setAllItemsChecked(false); });
  m_dlg->m_unifiedItemsList = new QListWidget(rightContainer);
  m_dlg->m_unifiedItemsList->setSelectionMode(QAbstractItemView::NoSelection);
  // Hide the horizontal scrollbar — a long filename still scrolls into view via
  // shift-wheel / arrow keys, but the bar no longer sits under the list (where it
  // showed for even a few pixels of overflow and pushed the rows out of
  // alignment). Vertical scrolling / its bar are unaffected (Kartend-1hose).
  m_dlg->m_unifiedItemsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_dlg->m_unifiedItemsList->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  connect(m_dlg->m_unifiedItemsList, &QListWidget::itemChanged, m_dlg->m_selectionModel.get(),
          &ScrapeResultSelectionModel::onItemCheckChanged);
  rightLayout->addWidget(m_dlg->m_unifiedItemsList, 1);
  splitter->addWidget(rightContainer);

  // Hand the (decoupled) selection model its view widgets now that they exist
  // (Kartend-hhv2u). The model is constructed before this panel is built, so
  // the widgets are injected here rather than at construction.
  m_dlg->m_selectionModel->setView(m_dlg->m_collectionTree, m_dlg->m_unifiedItemsList,
                                   m_dlg->m_itemsHeaderLabel);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  return splitter;
}

void ScrapeResultDialogUnified::buildMediaTypesGroup(QGroupBox *&mediaTypesGroup,
                                                     QWidget *&modeRowContainer) {
  // ── Middle: media types ─────────────────────────────────────────
  // MediaTypeCheckboxBuilder owns the curated SS media-type table,
  // the 3-column grid layout, and the Select-all/none bulk-toggle
  // wiring. The dialog keeps `m_dlg->m_mediaTypeChecks` because the filter
  // derivation later in onApply / auto-mode reads back the per-type
  // checked state.
  m_dlg->m_mediaTypesGroup =
      MediaTypeCheckboxBuilder::build(m_dlg->m_unifiedPage, m_dlg->m_mediaTypeChecks);
  mediaTypesGroup = m_dlg->m_mediaTypesGroup;

  // ── Mode toggle ─────────────────────────────────────────────────
  // Wrap the radio row in a container widget so we can hide the whole
  // row (label + both radios) while a scrape is running. QHBoxLayout
  // alone isn't a widget, so we'd otherwise have to toggle each child.
  m_dlg->m_modeRowContainer = new QWidget(m_dlg->m_unifiedPage);
  auto *modeRow = new QHBoxLayout(m_dlg->m_modeRowContainer);
  modeRow->setContentsMargins(0, 0, 0, 0);
  auto *modeLabel = new QLabel(tr("Mode:"), m_dlg->m_modeRowContainer);
  m_dlg->m_modeAutoRadio =
      new QRadioButton(tr("Auto-accept (first candidate)"), m_dlg->m_modeRowContainer);
  m_dlg->m_modeInteractiveRadio =
      new QRadioButton(tr("Interactive (pick candidate per item)"), m_dlg->m_modeRowContainer);
  m_dlg->m_modeAutoRadio->setChecked(true);
  modeRow->addWidget(modeLabel);
  modeRow->addWidget(m_dlg->m_modeAutoRadio);
  modeRow->addWidget(m_dlg->m_modeInteractiveRadio);
  modeRow->addStretch(1);
  modeRowContainer = m_dlg->m_modeRowContainer;
}

QWidget *ScrapeResultDialogUnified::buildScrapeOptionsGroup() {
  // Setup-view duplicates of the Settings → Scraper knobs users tweak most, so a
  // scrape can be re-aimed without a Settings round-trip (Kartend-1hose). Labels
  // and userData mirror scrapersettingspanel.cpp verbatim; changes persist back
  // into ScraperOptions via persistScrapeOptions().
  auto *group = new QGroupBox(tr("Scrape options"), m_dlg->m_unifiedPage);
  m_dlg->m_setupOptionsContainer = group;
  auto *form = new QFormLayout(group);
  form->setContentsMargins(UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                           UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                           UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                           UIConstants::ScrapeResultDialog::CONTENT_MARGIN);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  m_dlg->m_setupRescrapeCombo = new QComboBox(group);
  m_dlg->m_setupRescrapeCombo->addItem(tr("Overwrite — replace existing assets"),
                                       static_cast<int>(ScraperRescrapeMode::Overwrite));
  m_dlg->m_setupRescrapeCombo->addItem(
      tr("Fill missing — keep existing, only download what's missing"),
      static_cast<int>(ScraperRescrapeMode::FillMissing));
  m_dlg->m_setupRescrapeCombo->addItem(
      tr("Update changed — compare bytes, write only if different"),
      static_cast<int>(ScraperRescrapeMode::UpdateChanged));
  m_dlg->m_setupRescrapeCombo->addItem(
      tr("Skip — don't re-scrape items that already have metadata"),
      static_cast<int>(ScraperRescrapeMode::Skip));
  form->addRow(tr("Re-scrape policy:"), m_dlg->m_setupRescrapeCombo);

  m_dlg->m_setupRegionCombo = new QComboBox(group);
  m_dlg->m_setupRegionCombo->addItem(tr("World"), QStringLiteral("wor"));
  m_dlg->m_setupRegionCombo->addItem(tr("USA"), QStringLiteral("us"));
  m_dlg->m_setupRegionCombo->addItem(tr("Europe"), QStringLiteral("eu"));
  m_dlg->m_setupRegionCombo->addItem(tr("Japan"), QStringLiteral("jp"));
  m_dlg->m_setupRegionCombo->addItem(tr("United Kingdom"), QStringLiteral("uk"));
  m_dlg->m_setupRegionCombo->addItem(tr("France"), QStringLiteral("fr"));
  m_dlg->m_setupRegionCombo->addItem(tr("Germany"), QStringLiteral("de"));
  m_dlg->m_setupRegionCombo->addItem(tr("Spain"), QStringLiteral("sp"));
  m_dlg->m_setupRegionCombo->addItem(tr("Italy"), QStringLiteral("it"));
  m_dlg->m_setupRegionCombo->addItem(tr("Brazil"), QStringLiteral("br"));
  m_dlg->m_setupRegionCombo->addItem(tr("Australia"), QStringLiteral("au"));
  m_dlg->m_setupRegionCombo->addItem(tr("Korea"), QStringLiteral("kr"));
  m_dlg->m_setupRegionCombo->setToolTip(
      tr("Fallback region for titles, dates, and box art when an item's own region "
         "has no entry."));
  form->addRow(tr("Fallback region:"), m_dlg->m_setupRegionCombo);

  m_dlg->m_setupRefreshWindowSpin = new QSpinBox(group);
  m_dlg->m_setupRefreshWindowSpin->setRange(0, 365);
  m_dlg->m_setupRefreshWindowSpin->setSuffix(tr(" days"));
  m_dlg->m_setupRefreshWindowSpin->setSpecialValueText(tr("Always skip covered items"));
  m_dlg->m_setupRefreshWindowSpin->setToolTip(
      tr("Under Fill missing / Skip, re-scrape an already-covered item only when its "
         "last scrape is older than this. 0 = never refresh."));
  form->addRow(tr("Refresh items older than:"), m_dlg->m_setupRefreshWindowSpin);

  m_dlg->m_setupItemConcurrencySpin = new QSpinBox(group);
  m_dlg->m_setupItemConcurrencySpin->setRange(1, 16);
  m_dlg->m_setupItemConcurrencySpin->setToolTip(tr("How many items scrape in parallel."));
  form->addRow(tr("Items in parallel:"), m_dlg->m_setupItemConcurrencySpin);

  m_dlg->m_setupPresetCombo = new QComboBox(group);
  m_dlg->m_setupPresetCombo->addItem(tr("Fastest"), static_cast<int>(ScraperPreset::Fastest));
  m_dlg->m_setupPresetCombo->addItem(tr("Balanced"), static_cast<int>(ScraperPreset::Balanced));
  m_dlg->m_setupPresetCombo->addItem(tr("Best Quality"),
                                     static_cast<int>(ScraperPreset::BestQuality));
  m_dlg->m_setupPresetCombo->addItem(tr("Custom"), static_cast<int>(ScraperPreset::Custom));
  m_dlg->m_setupPresetCombo->setToolTip(
      tr("Media speed/quality: image size cap, per-host concurrency, and JPG re-encode. "
         "Custom keeps whatever you set in Settings."));
  form->addRow(tr("Media preset:"), m_dlg->m_setupPresetCombo);

  // Persist any change straight back to the scraper settings.
  connect(m_dlg->m_setupRescrapeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int) { persistScrapeOptions(); });
  connect(m_dlg->m_setupRegionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int) { persistScrapeOptions(); });
  connect(m_dlg->m_setupPresetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int) { persistScrapeOptions(); });
  // editingFinished (not valueChanged) so holding an arrow / typing a number
  // writes the INI once on commit, not once per step.
  connect(m_dlg->m_setupRefreshWindowSpin, &QSpinBox::editingFinished, this,
          [this]() { persistScrapeOptions(); });
  connect(m_dlg->m_setupItemConcurrencySpin, &QSpinBox::editingFinished, this,
          [this]() { persistScrapeOptions(); });
  return group;
}

void ScrapeResultDialogUnified::loadScrapeOptionsFromSettings() {
  if (!m_dlg->m_scraperCtx.generalSettings || !m_dlg->m_setupRescrapeCombo) return;
  const ScraperOptions &opts = m_dlg->m_scraperCtx.generalSettings->scraper.options;
  // Block change signals so populating the controls doesn't trigger a spurious
  // persist back over the very values we're loading.
  const QSignalBlocker b1(m_dlg->m_setupRescrapeCombo);
  const QSignalBlocker b2(m_dlg->m_setupRegionCombo);
  const QSignalBlocker b3(m_dlg->m_setupRefreshWindowSpin);
  const QSignalBlocker b4(m_dlg->m_setupItemConcurrencySpin);
  const QSignalBlocker b5(m_dlg->m_setupPresetCombo);
  m_dlg->m_setupRescrapeCombo->setCurrentIndex(
      m_dlg->m_setupRescrapeCombo->findData(static_cast<int>(opts.rescrapeMode)));
  const int regionIdx = m_dlg->m_setupRegionCombo->findData(opts.preferredScraperRegion);
  m_dlg->m_setupRegionCombo->setCurrentIndex(regionIdx >= 0 ? regionIdx : 0);
  m_dlg->m_setupRefreshWindowSpin->setValue(qBound(0, opts.skipRecentScrapeDays, 365));
  m_dlg->m_setupItemConcurrencySpin->setValue(qBound(1, opts.batchItemConcurrency, 16));
  m_dlg->m_setupPresetCombo->setCurrentIndex(
      m_dlg->m_setupPresetCombo->findData(static_cast<int>(opts.preset)));
}

void ScrapeResultDialogUnified::persistScrapeOptions() {
  if (!m_dlg->m_scraperCtx.generalSettings || !m_dlg->m_scraperCtx.ctx ||
      !m_dlg->m_setupRescrapeCombo) {
    return;
  }
  ISettingsManager *sm = m_dlg->m_scraperCtx.ctx->settingsManager();
  if (!sm) return;
  // Mutate the shared live GeneralSettings (== MainWindow's m_generalSettings) so
  // the running service — which reads these live, per collection — picks the
  // change up, then persist. saveGeneralSettings re-clamps the ranges and fires
  // scraperOptionsChanged only on a real change (Kartend-1hose).
  ScraperOptions &opts = m_dlg->m_scraperCtx.generalSettings->scraper.options;
  opts.rescrapeMode =
      static_cast<ScraperRescrapeMode>(m_dlg->m_setupRescrapeCombo->currentData().toInt());
  opts.preferredScraperRegion = m_dlg->m_setupRegionCombo->currentData().toString();
  opts.skipRecentScrapeDays = m_dlg->m_setupRefreshWindowSpin->value();
  opts.batchItemConcurrency = m_dlg->m_setupItemConcurrencySpin->value();
  opts.preset = static_cast<ScraperPreset>(m_dlg->m_setupPresetCombo->currentData().toInt());
  // The scrape reads the numeric media fields, not opts.preset, so stamp them to
  // match the chosen preset (Custom is a no-op — hand-tuned values stay).
  applyScraperPreset(opts, opts.preset);
  (void)sm->saveGeneralSettings(*m_dlg->m_scraperCtx.generalSettings);
}

QGroupBox *ScrapeResultDialogUnified::buildLiveMetadataPanel() {
  // ── Live view: currently-scraping metadata panel ────────────────
  // 10-column QGridLayout: FIVE (label, value) pairs per row.
  // Cross-row alignment is the whole point — every label sits in
  // one of cols {0, 2, 4, 6, 8}, every short value in one of cols
  // {1, 3, 5, 7, 9}, so labels line up vertically across all rows
  // AND every row has the same five-chip rhythm. Wide fields
  // (Description) span all value cols. Custom fields container
  // also spans the row.
  m_dlg->m_liveMetadataGroup = new QGroupBox(tr("Currently scraping"), m_dlg->m_unifiedPage);
  // Kartend-kggn8: hand the ticker the group it animates instead of it reaching
  // back through a friend pointer.
  m_dlg->m_marqueeTicker->setLiveMetadataGroup(m_dlg->m_liveMetadataGroup);
  auto *metaOuter = new QVBoxLayout(m_dlg->m_liveMetadataGroup);
  metaOuter->setContentsMargins(UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                                UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                                UIConstants::ScrapeResultDialog::CONTENT_MARGIN,
                                UIConstants::ScrapeResultDialog::CONTENT_MARGIN);
  metaOuter->setSpacing(UIConstants::ScrapeResultDialog::SECTION_LAYOUT_SPACING);
  // ── Interactive candidate picker row ──────────────────────────────
  // Visible only while the service is waiting on the user to pick a
  // candidate (interactive mode). Selecting a row re-fetches detail
  // and refreshes the live metadata fields below. Stays hidden in
  // auto mode so the layout is identical to non-interactive scrapes
  // until the user explicitly turns interactive on.
  m_dlg->m_interactiveCandidateRow = new QWidget(m_dlg->m_liveMetadataGroup);
  auto *candRow = new QHBoxLayout(m_dlg->m_interactiveCandidateRow);
  candRow->setContentsMargins(0, 0, 0, 0);
  candRow->setSpacing(UIConstants::ScrapeResultDialog::SECTION_LAYOUT_SPACING);
  auto *candLbl = new QLabel(tr("Candidate:"), m_dlg->m_interactiveCandidateRow);
  candLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  candLbl->setMinimumWidth(UIConstants::ScrapeResultDialog::CANDIDATE_LABEL_MIN_WIDTH);
  candRow->addWidget(candLbl);
  m_dlg->m_interactiveCandidateCombo = new QComboBox(m_dlg->m_interactiveCandidateRow);
  m_dlg->m_interactiveCandidateCombo->setObjectName(QStringLiteral("interactiveCandidateCombo"));
  m_dlg->m_interactiveCandidateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  candRow->addWidget(m_dlg->m_interactiveCandidateCombo, /*stretch=*/1);
  // Combobox is the visible candidate picker in the unified live view;
  // the SingleItemScrapeView's QListWidget is the data owner. Forward
  // selection to the view's list so its onCandidateSelected drives the
  // detail fetch (Kartend-xvci step 4) — the host's detailLoaded
  // dispatch then calls applyScrapedItemToLive + enables Apply.
  connect(m_dlg->m_interactiveCandidateCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int idx) {
            if (m_dlg->m_unifiedPhase != ScrapeResultDialog::UnifiedPhase::InteractivePicking) {
              return;
            }
            auto *list = m_dlg->m_singleItemView->candidateList();
            if (list && idx >= 0 && idx < list->count()) {
              list->setCurrentRow(idx);
            }
          });
  m_dlg->m_interactiveCandidateRow->hide();
  metaOuter->addWidget(m_dlg->m_interactiveCandidateRow);
  // Metadata host uses a plain vertical layout so the dialog can be
  // resized freely. Title + Description rows stretch horizontally to
  // fill the available width; everything else lives inside a backdrop
  // frame whose contents reflow via FlowLayout (typed fields above,
  // custom fields below). This keeps the panel readable on ultrawide
  // screens (more chips per row) AND low-res screens (chips wrap to
  // additional rows instead of overflowing the dialog width).
  auto *metaGridHost = new QWidget(m_dlg->m_liveMetadataGroup);
  auto *metaCol = new QVBoxLayout(metaGridHost);
  metaCol->setContentsMargins(0, 0, 0, 0);
  metaCol->setSpacing(UIConstants::ScrapeResultDialog::SECTION_LAYOUT_SPACING);

  // Uniform label width across every row so labels visually align.
  constexpr int kLabelW = UIConstants::ScrapeResultDialog::META_LABEL_WIDTH;
  constexpr int kValueChipW = UIConstants::ScrapeResultDialog::META_VALUE_CHIP_WIDTH;

  auto makeFieldEdit = [this](bool bold = false) {
    auto *edit = new QLineEdit(m_dlg->m_liveMetadataGroup);
    edit->setReadOnly(true);
    edit->setFrame(true);
    if (bold) {
      QFont f = edit->font();
      f.setBold(true);
      edit->setFont(f);
    }
    return edit;
  };
  auto sizedLabel = [this](const QString &text) {
    auto *lbl = new QLabel(text, m_dlg->m_liveMetadataGroup);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setFixedWidth(kLabelW);
    return lbl;
  };

  // ── Title row: label + stretching bold chip ───────────────────
  // Title is the most prominent field, so it gets its own full-width
  // row that grows with the dialog.
  m_dlg->m_liveMetadataTitle = makeFieldEdit(/*bold=*/true);
  m_dlg->m_liveMetadataTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_dlg->m_liveMetadataTitle->setMinimumWidth(kValueChipW * 2);
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UIConstants::ScrapeResultDialog::SECTION_LAYOUT_SPACING);
    row->addWidget(sizedLabel(tr("Title:")));
    row->addWidget(m_dlg->m_liveMetadataTitle, /*stretch=*/1);
    metaCol->addLayout(row);
  }

  // ── Description row: top-aligned label + multi-line browser ───
  m_dlg->m_liveMetadataDescription = new QTextBrowser(m_dlg->m_liveMetadataGroup);
  m_dlg->m_liveMetadataDescription->setOpenExternalLinks(true);
  m_dlg->m_liveMetadataDescription->setMinimumHeight(
      UIConstants::ScrapeResultDialog::DESCRIPTION_MIN_HEIGHT);
  m_dlg->m_liveMetadataDescription->setMaximumHeight(
      UIConstants::ScrapeResultDialog::DESCRIPTION_MAX_HEIGHT);
  m_dlg->m_liveMetadataDescription->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UIConstants::ScrapeResultDialog::SECTION_LAYOUT_SPACING);
    auto *lbl = sizedLabel(tr("Description:"));
    lbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
    row->addWidget(lbl);
    row->addWidget(m_dlg->m_liveMetadataDescription, /*stretch=*/1);
    metaCol->addLayout(row);
  }

  // ── Backdrop frame for the post-description metadata ───────────
  // Wraps the typed short fields AND the custom-fields section in a
  // distinct visual container with a flat alternate-base tint + soft
  // rounded border. Inside, FlowLayouts let the chips wrap as the
  // dialog is resized.
  auto *postDescFrame = new QFrame(m_dlg->m_liveMetadataGroup);
  postDescFrame->setObjectName(QStringLiteral("scraperMetadataBackdrop"));
  postDescFrame->setFrameShape(QFrame::NoFrame);
  postDescFrame->setStyleSheet(QStringLiteral("QFrame#scraperMetadataBackdrop {"
                                              "  background: palette(alternate-base);"
                                              "  border: 1px solid palette(midlight);"
                                              "  border-radius: 10px;"
                                              "}"));
  postDescFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *postOuter = new QVBoxLayout(postDescFrame);
  postOuter->setContentsMargins(UIConstants::ScrapeResultDialog::POST_SECTION_H_MARGIN,
                                UIConstants::ScrapeResultDialog::POST_SECTION_V_MARGIN,
                                UIConstants::ScrapeResultDialog::POST_SECTION_H_MARGIN,
                                UIConstants::ScrapeResultDialog::POST_SECTION_V_MARGIN);
  postOuter->setSpacing(UIConstants::ScrapeResultDialog::ROOT_LAYOUT_SPACING);

  // Builds a fixed-size "chip" container holding a right-aligned label and
  // a read-only QLineEdit (FormBuilders::makeChipPair, Kartend-t06mx) so the
  // typed-fields flow shares one chip-assembly source with the custom-fields
  // flow below. Returns the wrapper widget the FlowLayout treats as a single
  // item.
  auto makeChipPair = [&](const QString &label, QLineEdit *edit) -> QWidget * {
    return FormBuilders::makeChipPair(postDescFrame, label, edit, kLabelW, kValueChipW);
  };

  // ── Combined chip flow ────────────────────────────────────────
  // Single FlowLayout for ALL short metadata chips: typed fields
  // first, then custom fields appended after. Sharing one layout
  // means custom-field chips fill in directly after Tags on the same
  // row (instead of always starting a new row), so the bottom row
  // never has just one orphaned chip when there's horizontal room.
  //
  // Kartend-etbol: the nine typed fields used to be nine hand-repeated
  // "new QLineEdit + setReadOnly + makeChipPair" blocks. They now live in
  // one {label, &memberPtr} table walked by a single loop, so a styling /
  // read-only / chip-layout tweak lands once. Order matches the original
  // (Publisher … Tags) so the rendered chip sequence is unchanged. The
  // QLineEdit/chip construction inside the loop body is identical to the
  // per-field code it replaces.
  const struct {
    QString label;
    QLineEdit **member;
  } kTypedFields[] = {
      {tr("Publisher:"), &m_dlg->m_liveMetadataPublisher},
      {tr("Developer:"), &m_dlg->m_liveMetadataDeveloper},
      {tr("Released:"), &m_dlg->m_liveMetadataReleased},
      {tr("Source:"), &m_dlg->m_liveMetadataSource},
      {tr("Genre:"), &m_dlg->m_liveMetadataGenre},
      {tr("Players:"), &m_dlg->m_liveMetadataPlayers},
      {tr("Rating:"), &m_dlg->m_liveMetadataContentRating},
      {tr("Runtime:"), &m_dlg->m_liveMetadataRuntime},
      {tr("Tags:"), &m_dlg->m_liveMetadataTags},
  };

  m_dlg->m_liveExtrasContainer = new QWidget(postDescFrame);
  // FlowLayout computes height-for-width so the container grows /
  // shrinks naturally as chips wrap on resize. No min height — that
  // would create dead space on wide windows.
  m_dlg->m_liveExtrasContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *extrasLayout = new FlowLayout(m_dlg->m_liveExtrasContainer, /*margin=*/0,
                                      /*hSp=*/8, /*vSp=*/6);
  m_dlg->m_liveExtrasContainer->setLayout(extrasLayout);
  for (const auto &field : kTypedFields) {
    auto *edit = new QLineEdit(postDescFrame);
    edit->setReadOnly(true);
    *field.member = edit;
    extrasLayout->addWidget(makeChipPair(field.label, edit));
  }
  // populateCustomFields appends custom-key chips AFTER these typed
  // chips. m_typedChipCount marks the boundary so re-renders only
  // tear down the custom chips, leaving typed chips in place.
  m_typedChipCount = extrasLayout->count();
  postOuter->addWidget(m_dlg->m_liveExtrasContainer);

  // Add the backdrop frame to the outer vertical column.
  metaCol->addWidget(postDescFrame);

  // Pre-seed the custom-fields section with every key the SS parser
  // can emit (gathered from screenscraperparser.cpp). The user sees
  // the full placeholder grid the moment the panel opens — values
  // fill in as the scrape produces them. Other providers that emit
  // keys outside this list still get rendered (populateCustomFields
  // adds new keys dynamically), the seeded list just covers the
  // common case so the section isn't empty on first open.
  static const QStringList kKnownSSCustomKeys = {
      QStringLiteral("classification_esrb"),
      QStringLiteral("classification_pegi"),
      QStringLiteral("classification_usk"),
      QStringLiteral("classification_cero"),
      QStringLiteral("cloneof"),
      QStringLiteral("colors"),
      QStringLiteral("controls"),
      QStringLiteral("families"),
      QStringLiteral("languages"),
      QStringLiteral("modes"),
      QStringLiteral("notgame"),
      QStringLiteral("rating"),
      QStringLiteral("region"),
      QStringLiteral("resolution"),
      QStringLiteral("rom_crc"),
      QStringLiteral("rom_filename"),
      QStringLiteral("rom_md5"),
      QStringLiteral("rom_serial"),
      QStringLiteral("rom_sha1"),
      QStringLiteral("rom_size"),
      QStringLiteral("rom_type"),
      QStringLiteral("rotation"),
      QStringLiteral("screenscraper_companyid"),
      QStringLiteral("screenscraper_groupid"),
      QStringLiteral("screenscraper_id"),
      QStringLiteral("topstaff"),
  };
  for (const QString &k : kKnownSSCustomKeys) m_allSeenCustomKeys.insert(k);
  // Initial render with empty values — populateCustomFields handles
  // the persistent-cell creation against the seeded union.
  populateCustomFields({});

  // No fixed min height — title row + description row + backdrop
  // FlowLayouts each compute their own height-for-width, so the
  // panel naturally grows on narrow widths (chips wrap to more
  // rows) and stays compact on wide widths.

  // Attach the typed-fields grid below the candidate row in the
  // group's outer vertical layout.
  metaOuter->addWidget(metaGridHost);

  m_dlg->m_liveMetadataGroup->hide();
  return m_dlg->m_liveMetadataGroup;
}

void ScrapeResultDialogUnified::buildProgressLabels(QGroupBox *&thumbsGroup, QWidget *&currentLabel,
                                                    QWidget *&progressBar, QWidget *&timingLabel,
                                                    QWidget *&countsLabel, QWidget *&quotaLabel) {
  // ── Live view: recent media thumbnails ──────────────────────────
  // Compact horizontal filmstrip — auto-scrolls to keep the newest
  // thumbnail visible, no manual scrollbars. Items are tightly
  // packed (small spacing + no text label) so several covers fit
  // across the dialog width.
  m_dlg->m_liveThumbsGroup = new QGroupBox(tr("Recent media"), m_dlg->m_unifiedPage);
  auto *thumbsLayout = new QVBoxLayout(m_dlg->m_liveThumbsGroup);
  thumbsLayout->setContentsMargins(UIConstants::ScrapeResultDialog::THUMBS_STRIP_MARGIN,
                                   UIConstants::ScrapeResultDialog::THUMBS_STRIP_MARGIN,
                                   UIConstants::ScrapeResultDialog::THUMBS_STRIP_MARGIN,
                                   UIConstants::ScrapeResultDialog::THUMBS_STRIP_MARGIN);
  m_dlg->m_liveThumbsStrip = new QListWidget(m_dlg->m_liveThumbsGroup);
  // Kartend-kggn8: hand the loader the filmstrip it appends to instead of it
  // reaching back through a friend pointer.
  m_dlg->m_thumbLoader->setLiveThumbsStrip(m_dlg->m_liveThumbsStrip);
  m_dlg->m_liveThumbsStrip->setViewMode(QListView::IconMode);
  m_dlg->m_liveThumbsStrip->setIconSize(QSize(UIConstants::ScrapeResultDialog::THUMB_ICON_SIZE,
                                              UIConstants::ScrapeResultDialog::THUMB_ICON_SIZE));
  m_dlg->m_liveThumbsStrip->setFlow(QListView::LeftToRight);
  m_dlg->m_liveThumbsStrip->setWrapping(false);
  m_dlg->m_liveThumbsStrip->setMovement(QListView::Static);
  m_dlg->m_liveThumbsStrip->setSelectionMode(QAbstractItemView::NoSelection);
  m_dlg->m_liveThumbsStrip->setMaximumHeight(
      UIConstants::ScrapeResultDialog::THUMBS_STRIP_MAX_HEIGHT);
  m_dlg->m_liveThumbsStrip->setUniformItemSizes(true);
  m_dlg->m_liveThumbsStrip->setSpacing(UIConstants::ScrapeResultDialog::THUMBS_STRIP_SPACING);
  // Slightly larger than icon so items fit snugly with minimal gap.
  m_dlg->m_liveThumbsStrip->setGridSize(QSize(UIConstants::ScrapeResultDialog::THUMB_GRID_SIZE,
                                              UIConstants::ScrapeResultDialog::THUMB_GRID_SIZE));
  m_dlg->m_liveThumbsStrip->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_dlg->m_liveThumbsStrip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_dlg->m_liveThumbsStrip->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_dlg->m_liveThumbsStrip->setFrameShape(QFrame::NoFrame);
  thumbsLayout->addWidget(m_dlg->m_liveThumbsStrip);
  m_dlg->m_liveThumbsGroup->hide();
  thumbsGroup = m_dlg->m_liveThumbsGroup;

  // ── Progress + status (visible during scrape) ───────────────────
  m_dlg->m_unifiedCurrentLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedCurrentLabel->setWordWrap(true);
  m_dlg->m_unifiedCurrentLabel->hide();
  currentLabel = m_dlg->m_unifiedCurrentLabel;

  m_dlg->m_unifiedProgressBar = new QProgressBar(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedProgressBar->setRange(0, 100);
  m_dlg->m_unifiedProgressBar->setValue(0);
  m_dlg->m_unifiedProgressBar->setTextVisible(true);
  m_dlg->m_unifiedProgressBar->hide();
  progressBar = m_dlg->m_unifiedProgressBar;

  m_dlg->m_unifiedTimingLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedTimingLabel->setWordWrap(true);
  m_dlg->m_unifiedTimingLabel->hide();
  timingLabel = m_dlg->m_unifiedTimingLabel;

  m_dlg->m_unifiedCountsLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedCountsLabel->hide();
  // The error count is rendered as a link when non-zero; rich text
  // is needed for the anchor. Clicking it opens the failure list.
  m_dlg->m_unifiedCountsLabel->setTextFormat(Qt::RichText);
  m_dlg->m_unifiedCountsLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                                       Qt::LinksAccessibleByKeyboard);
  connect(m_dlg->m_unifiedCountsLabel, &QLabel::linkActivated, this,
          [this](const QString &) { showScrapeErrorDetails(); });
  countsLabel = m_dlg->m_unifiedCountsLabel;

  // ScreenScraper request-quota readout. Hidden until a live scrape
  // delivers a valid quota via the service's quotaUpdated signal
  // (non-SS providers never do, so the row stays absent for them).
  m_dlg->m_unifiedQuotaLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedQuotaLabel->setWordWrap(true);
  m_dlg->m_unifiedQuotaLabel->hide();
  quotaLabel = m_dlg->m_unifiedQuotaLabel;
}

void ScrapeResultDialogUnified::applyScrapedItemToLive(const Scraper::ScrapedItem &item) {
  // New item → reset every marquee back to position 0 so each cell
  // restarts the L→R animation from the head of the new value.
  m_dlg->m_marqueeTicker->resetCells();
  auto setFromStart = [](QLineEdit *e, const QString &t) {
    e->setText(t);
    e->setCursorPosition(0);
  };
  setFromStart(m_dlg->m_liveMetadataTitle, item.title);
  setFromStart(m_dlg->m_liveMetadataPublisher, item.publisher);
  setFromStart(m_dlg->m_liveMetadataReleased, item.releaseDate);
  setFromStart(m_dlg->m_liveMetadataDeveloper, item.developer);
  setFromStart(m_dlg->m_liveMetadataGenre, item.genre);
  setFromStart(m_dlg->m_liveMetadataPlayers, item.players);
  setFromStart(m_dlg->m_liveMetadataContentRating, item.contentRating);
  setFromStart(m_dlg->m_liveMetadataRuntime,
               item.runtimeSeconds > 0
                   ? ScrapeResultDialog::formatDuration(qint64(item.runtimeSeconds) * 1000LL)
                   : QString());
  setFromStart(m_dlg->m_liveMetadataTags, item.tagsJson);
  setFromStart(m_dlg->m_liveMetadataSource, item.sourceProviderId);
  m_dlg->m_liveMetadataDescription->setText(item.description);
  populateCustomFields(item.customFields);
}

void ScrapeResultDialogUnified::populateCustomFields(const QHash<QString, QString> &fields) {
  if (!m_dlg->m_liveExtrasContainer || !m_dlg->m_liveExtrasContainer->layout()) return;
  auto *layout = m_dlg->m_liveExtrasContainer->layout();
  if (!layout) return;
  // Merge incoming keys into the session-wide union so new keys
  // emitted mid-scrape get a placeholder chip added to the flow.
  bool newKeyAdded = false;
  for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
    if (!m_allSeenCustomKeys.contains(it.key())) {
      m_allSeenCustomKeys.insert(it.key());
      newKeyAdded = true;
    }
  }
  // First-pass build or new-key arrival → rebuild every chip-pair so
  // sorted-key order stays stable across the full union. Only the
  // chips from m_typedChipCount onward are torn down — the leading
  // typed-field chips (Publisher … Tags) stay in place.
  if (m_customFieldEdits.isEmpty() || newKeyAdded) {
    while (layout->count() > m_typedChipCount) {
      QLayoutItem *child = layout->takeAt(m_typedChipCount);
      if (!child) break;
      if (auto *w = child->widget()) w->deleteLater();
      delete child;
    }
    m_customFieldEdits.clear();
    QStringList keys = m_allSeenCustomKeys.values();
    std::sort(keys.begin(), keys.end());
    // Custom-field cells share the EXACT same label + chip widths as
    // the typed-fields chips above so columns align visually across
    // both flows.
    constexpr int kCustomLabelW = 90;
    constexpr int kCustomValueW = 90;
    for (const QString &key : keys) {
      auto *chip = new QWidget(m_dlg->m_liveExtrasContainer);
      auto *h = new QHBoxLayout(chip);
      h->setContentsMargins(0, 0, 0, 0);
      h->setSpacing(4);
      auto *lbl = new QLabel(prettifyCustomKey(key) + QLatin1Char(':'), chip);
      lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      lbl->setFixedWidth(kCustomLabelW);
      lbl->setToolTip(key);
      auto *edit = new QLineEdit(chip);
      edit->setReadOnly(true);
      edit->setFrame(true);
      edit->setFixedWidth(kCustomValueW);
      h->addWidget(lbl);
      h->addWidget(edit);
      chip->setFixedSize(kCustomLabelW + 4 + kCustomValueW, edit->sizeHint().height() + 2);
      layout->addWidget(chip);
      m_customFieldEdits.insert(key, edit);
    }
  }
  // Update value text in every persistent cell — empty string for
  // keys absent from the current item so the slot stays visible but
  // blank rather than disappearing.
  for (auto it = m_customFieldEdits.constBegin(); it != m_customFieldEdits.constEnd(); ++it) {
    const QString value = fields.value(it.key()).trimmed();
    it.value()->setText(value);
    it.value()->setCursorPosition(0);
    it.value()->setToolTip(value);
  }
}

void ScrapeResultDialogUnified::startUnifiedScrape(int preCollectionIndex,
                                                   const QString &preItemPath) {
  m_dlg->m_mode = ScrapeResultDialog::Mode::Unified;
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Setup;
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_unifiedPage);
  m_dlg->m_applyButton->hide();
  // Repurpose the dialog button area: hide the legacy Apply, show the
  // dedicated Scrape button. Scrape + Close are built (hidden) once in the
  // host's buildUi — no connects happen anywhere in this open path. Close
  // hides the dialog (the ScraperService keeps running); Cancel stops the
  // active scrape entirely.
  m_dlg->m_scrapeButton->show();
  // Re-attach to a running service if there is one — we should
  // come up directly in the Live view, not the Setup view. Don't
  // touch the tree / items state in that case: the user might have
  // configured a selection earlier and we shouldn't overwrite it
  // mid-run — the same reason this branch must NOT resetRunState().
  // Setup view rebuilds the tree as it always has.
  if (m_dlg->m_service && m_dlg->m_service->isActive()) {
    setUnifiedSetupEnabled(false);
    m_dlg->m_closeButton->show();
    // Restart the 1-second live tick (the scrapeStarted handler missed
    // this run because we connected after it already fired) with a
    // fresh rate window.
    m_rateSamples.clear();
    m_dlg->m_liveTickTimer.start();
    m_dlg->m_marqueeTicker->start();
    // Sync the Live view from the service's current snapshot so the
    // user immediately sees where the scrape is sitting instead of
    // waiting for the next signal tick.
    updateUnifiedProgressLabel();
    applyScrapedItemToLive(m_dlg->m_service->lastScrapedItem());
    m_dlg->m_unifiedCurrentLabel->setText(
        tr("Collection: %1 — scraping: %2")
            .arg(m_dlg->m_service->currentCollectionName(),
                 QFileInfo(m_dlg->m_service->currentItemPath()).fileName()));
    m_shownCollectionName = m_dlg->m_service->currentCollectionName();
    // Restore the recent-media thumbnail strip from the service so
    // the user doesn't see an empty band when re-entering. Icon-only
    // rows + auto-scroll to the latest match the same shape as the
    // itemCompleted-driven append path.
    m_dlg->m_liveThumbsStrip->clear();
    // Restore the strip off the GUI thread: appendThumbAsync decodes +
    // scales each thumb on the thread pool and appends one row per
    // completion (the same path the live itemCompleted append uses), so
    // re-entering the dialog no longer blocks the UI decoding the whole
    // recent-media history inline. The PDFium-abort guard, 12-row cap, and
    // auto-scroll-to-newest all live inside appendThumbAsync.
    for (const QString &p : m_dlg->m_service->recentMediaPaths()) {
      if (p.isEmpty()) continue;
      m_dlg->m_thumbLoader->appendThumbAsync(p);
    }
    // If paused mid-interactive, kick service back into action so the
    // user's resumed UI gets a picker.
    if (m_dlg->m_service->state() == Scraper::ScraperService::State::PausedInteractive) {
      m_dlg->m_service->resumePaused();
    }
    return; // skip populateCollectionTree — keep existing selection
  }
  // Fresh setup (no live run to re-attach to): wipe everything run-scoped
  // in one place so nothing from the previous run survives the reopen.
  m_dlg->resetRunState();
  setUnifiedSetupEnabled(true);
  // Reflect the current scraper settings in the setup-view quick options each
  // time the dialog opens fresh (Kartend-1hose).
  loadScrapeOptionsFromSettings();
  m_dlg->m_selectionModel->populateCollectionTree();

  // Right-click flow: pre-check exactly the requested collection +
  // its single item, leaving every other collection in the unchecked
  // default state.
  m_dlg->m_selectionModel->preCheckSingleItem(preCollectionIndex, preItemPath);

  // Provider-aware media ticks (Kartend-6e90v): when the dialog opens
  // scoped to one collection, re-tick the "What to scrape" grid with the
  // curated default set of the provider that collection resolves to —
  // e.g. a launcher-import Steam collection gets screenshot / background /
  // video instead of the ROM-tuned front-only default, so a hand-run
  // scrape fetches the media the provider actually supplies. Providers
  // without a curated set (empty list) leave the table defaults alone.
  // The un-scoped flow keeps the global defaults: its target mixes
  // collections with potentially different providers.
  if (CollectionUtils::isValidIndex(preCollectionIndex, m_dlg->m_scraperCtx.collections)) {
    const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[preCollectionIndex];
    MediaTypeCheckboxBuilder::applyProviderDefaults(
        m_dlg->m_mediaTypeChecks, MetadataProviderRegistry::defaultMediaTypesForCollection(cfg));
  }
}

void ScrapeResultDialogUnified::setUnifiedSetupEnabled(bool enabled) {
  // Setup widgets — collection tree, items list, media-types group,
  // mode toggle — are HIDDEN while a scrape is running. The Live view
  // (metadata + thumbnails + progress) takes their place and gets the
  // full vertical space. Setup widgets reappear when the scrape ends.
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->setEnabled(enabled);
  if (m_dlg->m_unifiedSplitterContainer) m_dlg->m_unifiedSplitterContainer->setVisible(enabled);
  if (m_dlg->m_mediaTypesGroup) m_dlg->m_mediaTypesGroup->setVisible(enabled);
  if (m_dlg->m_modeRowContainer) m_dlg->m_modeRowContainer->setVisible(enabled);
  // Scrape-options group is Setup-only too — no mid-run edits (Kartend-1hose).
  if (m_dlg->m_setupOptionsContainer) m_dlg->m_setupOptionsContainer->setVisible(enabled);
  // The whole setup view lives in one vertical splitter; hide it as a unit so the
  // live view gets the full height during a run (Kartend-1hose).
  if (m_dlg->m_setupVerticalSplitter) m_dlg->m_setupVerticalSplitter->setVisible(enabled);

  const bool showProgress = !enabled;
  m_dlg->m_unifiedCurrentLabel->setVisible(showProgress);
  m_dlg->m_unifiedProgressBar->setVisible(showProgress);
  m_dlg->m_unifiedTimingLabel->setVisible(showProgress);
  m_dlg->m_unifiedCountsLabel->setVisible(showProgress);
  // The quota label is shown only once a live scrape reports a valid
  // quota (the quotaUpdated handler reveals it). Returning to the
  // setup view always hides it; entering the live view leaves it
  // hidden until that first quota update arrives.
  if (m_dlg->m_unifiedQuotaLabel && !showProgress) m_dlg->m_unifiedQuotaLabel->hide();
  if (m_dlg->m_liveMetadataGroup) m_dlg->m_liveMetadataGroup->setVisible(showProgress);
  if (m_dlg->m_liveThumbsGroup) m_dlg->m_liveThumbsGroup->setVisible(showProgress);
  if (m_dlg->m_closeButton) m_dlg->m_closeButton->setVisible(showProgress);
  // No explicit layout invalidation — Qt handles re-flow when widgets
  // hide/show, and forcing it was contributing to the dialog auto-
  // resizing on every toggle. The minimum-size pin in the ctor keeps
  // the window stable.
}
