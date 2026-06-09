// Modal review dialog for scrape results. Provider-agnostic — drives
// the picked MetadataLookupProvider for detail + media downloads, so
// adding new providers (ScreenScraper, TMDB, Open Library) doesn't
// touch this file.
#include "scraperesultdialog.h"

#include "applicationcontext.h"
#include "batchprogressview.h"
#include "durationformat.h"
#include "flowlayout.h"
#include "mediatypecheckboxbuilder.h"
#include "scrapedownloaddispatcher.h"
#include "scraperesultdialogunified.h"
#include "scraperesultselectionmodel.h"
#include "scraperesultthumbnailloader.h"
#include "singleitemview.h"
#include "valuemarqueeticker.h"

#include <limits>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QImage>
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
#include <QSplitter>
#include <QtConcurrent/QtConcurrentRun>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

#include "extensionutils.h"
#include "idatabasemanager.h"
#include "imagedecodeutils.h"
#include "metadatalookupprovider.h"
#include "pathutils.h"
#include "scrapejobgrouping.h"
#include "scrapelogging.h"
#include "uiconstants/dialog.h"

namespace {

// FlowLayout extracted to flowlayout.{h,cpp} (Kartend-3fkz step 1).
// renderDetailHtml + the detail HTML payload moved into SingleItemScrapeView
// alongside the candidate-selection flow (Kartend-xvci step 4).

} // namespace

ScrapeResultDialog::ScrapeResultDialog(MetadataLookupProvider *provider,
                                       QList<Scraper::ScrapeCandidate> candidates, QWidget *parent)
    : QDialog(parent), m_unified(std::make_unique<ScrapeResultDialogUnified>(this)),
      m_selectionModel(std::make_unique<ScrapeResultSelectionModel>(this)),
      m_thumbLoader(std::make_unique<ScrapeResultThumbnailLoader>(this)),
      m_marqueeTicker(std::make_unique<ValueMarqueeTicker>(this)) {
  setWindowTitle(tr("Scraper"));
  setModal(true);
  resize(UIConstants::ScrapeResultDialog::DEFAULT_WIDTH,
         UIConstants::ScrapeResultDialog::DEFAULT_HEIGHT);
  // Hard floor so the dialog stays usable on low-res screens, but
  // well below the preferred size so the user can shrink the window
  // and the FlowLayout-based metadata chips wrap accordingly. The
  // user can still drag-resize larger; we just clamp the lower edge.
  setMinimumSize(UIConstants::ScrapeResultDialog::MIN_WIDTH,
                 UIConstants::ScrapeResultDialog::MIN_HEIGHT);
  buildUi();

  // Hand the candidate list + cache + fetch driver to the view. This
  // populates m_candidateList, hides it on single-result responses, and
  // pre-selects row 0 so the user sees a detail immediately. Provider
  // ownership moved into the view in Kartend-xvci step 4 — the view
  // also owns m_detailCache and the candidate-selection slot.
  m_singleItemView->setProviderAndCandidates(provider, std::move(candidates));

  // Provider health probe — fired once on dialog open. Default
  // implementation is a noop, so non-SS providers stay quiet. The
  // QPointer guard handles the case where the user dismisses the
  // dialog before the async probe lands.
  if (provider) {
    QPointer<ScrapeResultDialog> guard(this);
    provider->fetchHealthStatus([guard](const MetadataLookupProvider::HealthStatus &status) {
      if (guard.isNull()) return;
      if (status.humanStatus.isEmpty() && !status.refuseScrape) return;
      guard->m_singleItemView->healthLabel()->setText(status.humanStatus);
      guard->m_singleItemView->healthLabel()->show();
      if (status.refuseScrape) {
        guard->m_healthBlocksApply = true;
        // Even when a candidate gets selected later, the host's
        // detailLoaded handler re-checks m_healthBlocksApply before
        // re-enabling Apply. The toolTip points the user at what
        // the Apply gate is waiting on.
        guard->m_applyButton->setEnabled(false);
        guard->m_applyButton->setToolTip(status.humanStatus);
      }
    });
  }
}

ScrapeResultDialog::~ScrapeResultDialog() = default;

namespace {
// Refcounted across every live instance — the main window queries
// this via isAnyInstanceVisible() to gate item-selection input while
// the user is mid-scrape.
int g_visibleInstanceCount = 0;
} // namespace

bool ScrapeResultDialog::isAnyInstanceVisible() {
  return g_visibleInstanceCount > 0;
}

void ScrapeResultDialog::closeEvent(QCloseEvent *event) {
  if (m_mode == Mode::Unified) {
    // Unified flow: closing must not cancel the underlying scrape.
    // Pause if interactive mid-pick so the service doesn't fire the
    // next item's picker into a dead UI; auto mode just keeps going.
    if (m_service && m_service->state() == Scraper::ScraperService::State::RunningInteractive) {
      m_service->pauseInteractive();
    }
    hide();
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

void ScrapeResultDialog::hideEvent(QHideEvent *event) {
  // The background scrape keeps producing itemCompleted signals while
  // the dialog is hidden. Stop the periodic timers here so the
  // *invisible* UI doesn't keep running — service still ticks, but we
  // don't burn CPU updating widgets nobody can see. The itemCompleted
  // slot also short-circuits its pixmap-scale work via isVisible().
  m_liveTickTimer.stop();
  m_marqueeTicker->pause();
  if (g_visibleInstanceCount > 0) {
    --g_visibleInstanceCount;
  }
  QDialog::hideEvent(event);
}

void ScrapeResultDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  ++g_visibleInstanceCount;
  // Resume periodic UI updates when the dialog becomes visible again.
  // Only restart while the service is still actively scraping —
  // ticks at idle are pure waste. Note startUnifiedScrape also creates
  // these timers on the running-service path; this branch just covers
  // a plain show() after a hide().
  if (m_service && m_service->isActive()) {
    if (m_liveTickTimerInited && !m_liveTickTimer.isActive()) m_liveTickTimer.start();
    m_marqueeTicker->resume();
  }
}

void ScrapeResultDialog::setSharedAssetSearchPaths(const QStringList &paths) {
  m_sharedSearchPaths = paths;
}

void ScrapeResultDialog::setRescrapeContext(const QString &artworkDir, const QString &baseName,
                                            Scraper::RescrapeMode rescrapeMode) {
  m_rescrapeArtworkDir = artworkDir;
  m_rescrapeBaseName = baseName;
  m_rescrapeMode = rescrapeMode;
}

void ScrapeResultDialog::buildUi() {
  auto *root = new QVBoxLayout(this);

  // Outer two-page stack: the single-item splitter and the batch
  // progress panel live as sibling pages so setBatchRunner can flip
  // the dialog into a progress-only view without recreating any of
  // the single-item state.
  m_modeStack = new QStackedWidget(this);

  // ── Single-item page ────────────────────────────────────────────
  m_singleItemView = new SingleItemScrapeView(m_modeStack);
  m_modeStack->addWidget(m_singleItemView);
  // Apply-button state follows the view's detail-load lifecycle. The
  // host owns the Apply button (the view is mode-agnostic) and gates it
  // on the health-blocks-apply flag set by the provider's health probe.
  // When the unified-interactive flow is the active picker, the same
  // detailLoaded also drives the live-metadata panel via the unified
  // controller (Kartend-xvci step 5 dispatch).
  connect(m_singleItemView, &SingleItemScrapeView::detailLoaded, this,
          [this](const Scraper::ScrapedItem &item) {
            m_applyButton->setEnabled(!m_healthBlocksApply);
            if (m_mode == Mode::Unified && m_unifiedPhase == UnifiedPhase::InteractivePicking) {
              m_unified->applyScrapedItemToLive(item);
            }
          });
  connect(m_singleItemView, &SingleItemScrapeView::detailFailed, this,
          [this]() { m_applyButton->setEnabled(false); });
  connect(m_singleItemView, &SingleItemScrapeView::detailLoading, this,
          [this]() { m_applyButton->setEnabled(false); });

  // ── Batch progress page ─────────────────────────────────────────
  m_batchView = new BatchScrapeProgressView(m_modeStack);
  m_modeStack->addWidget(m_batchView);
  connect(m_batchView, &BatchScrapeProgressView::finished, this,
          [this](const Scraper::BatchScrapeRunner::Summary &) { accept(); });

  // ── Unified setup page ──────────────────────────────────────────
  m_unified->buildUnifiedPanel();
  m_modeStack->addWidget(m_unifiedPage);

  root->addWidget(m_modeStack, 1);

  // Kartend-ou0a: stage label that lives outside the mode-stack so it's
  // visible during every flow (single-item picker wait, batch progress,
  // unified setup→running transition). Hidden when empty so it doesn't
  // reserve vertical space on fast scrapes. ScraperService::itemStageChanged
  // drives its text via the connect() below in setScraperService.
  m_stageLabel = new QLabel(this);
  m_stageLabel->setWordWrap(true);
  QFont stageFont = m_stageLabel->font();
  stageFont.setItalic(true);
  m_stageLabel->setFont(stageFont);
  m_stageLabel->setStyleSheet(QStringLiteral("padding: 4px 8px;"));
  m_stageLabel->hide();
  root->addWidget(m_stageLabel);

  // "Skip this item" shown in lockstep with m_stageLabel (see the
  // itemStageChanged handler in setScraperService): lets the user
  // abandon a single stuck/large item mid hash/extraction without
  // cancelling the whole run.
  m_skipItemButton = new QPushButton(tr("Skip this item"), this);
  m_skipItemButton->setToolTip(
      tr("Stop scraping the current item and move on. The rest of the run keeps going."));
  m_skipItemButton->hide();
  connect(m_skipItemButton, &QPushButton::clicked, this, [this]() {
    skipCurrentScrapeItem();
    // Disable until the next item's stage starts so a lingering click can't
    // read as "skip the next one too"; the stage handler re-enables on show.
    m_skipItemButton->setEnabled(false);
  });
  auto *skipRow = new QHBoxLayout();
  skipRow->setContentsMargins(0, 0, 0, 0);
  skipRow->addWidget(m_skipItemButton);
  skipRow->addStretch(1);
  root->addLayout(skipRow);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  m_applyButton = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
  m_applyButton->setEnabled(false);
  // Cancel semantics:
  //  • Single-item, pre-Apply  → reject() (no work done yet).
  //  • Single-item, mid-Apply  → accept() and commit whatever finished.
  //    The user has already paid the bandwidth cost for those assets;
  //    abandoning them just means they re-download next time.
  //  • Batch                   → forward to runner->cancel(); the
  //    runner drains its in-flight callbacks and emits `finished`,
  //    which we treat as a normal completion (the dialog accept()s
  //    via the connected slot in setBatchRunner).
  connect(buttons, &QDialogButtonBox::rejected, this, [this]() {
    if (m_mode == Mode::Batch) {
      if (m_batchRunner) m_batchRunner->cancel();
      return;
    }
    if (m_mode == Mode::Unified) {
      // Setup phase: just reject the dialog (no scrape has started).
      // Running phase, service-owned: ask the service to cancel;
      //   the service drains in-flight items and emits
      //   scrapeFinished, the dialog's signal handler flips back
      //   to the Setup view. The dialog itself doesn't close.
      // Running phase, legacy in-dialog orchestration: flip the
      //   cancel flag so the next chain hop stops.
      if (m_service && m_service->isActive()) {
        m_service->cancel();
        return;
      }
      if (m_unifiedPhase == UnifiedPhase::Setup || m_unifiedPhase == UnifiedPhase::Done) {
        reject();
        return;
      }
      m_unifiedCancelled = true;
      if (m_batchRunner) m_batchRunner->cancel();
      return;
    }
    if (m_downloadsTotal > 0) {
      qCInfo(lcScrapeTimings) << "DIALOG cancel mid-download — committing"
                              << m_result.downloads.size() << "of" << m_downloadsTotal;
      accept();
    } else {
      reject();
    }
  });
  connect(m_applyButton, &QPushButton::clicked, this, &ScrapeResultDialog::onApply);
  root->addWidget(buttons);
}

void ScrapeResultDialog::setScraperContext(const ScraperContext &ctx) {
  m_scraperCtx = ctx;
}

void ScrapeResultDialog::startUnifiedScrape(int preCollectionIndex, const QString &preItemPath) {
  m_unified->startUnifiedScrape(preCollectionIndex, preItemPath);
}

void ScrapeResultDialog::setScraperService(Scraper::ScraperService *service) {
  qCInfo(lcScrapeTimings) << "DIALOG setScraperService called service=" << service
                          << "current=" << m_service << "this=" << this;
  if (m_service == service) {
    qCInfo(lcScrapeTimings) << "DIALOG setScraperService: same service, skipping connect";
    return;
  }
  m_service = service;
  if (!m_service) return;
  qCInfo(lcScrapeTimings) << "DIALOG setScraperService: establishing connections";
  // Connect to every signal the dialog needs to keep its Live view in
  // sync. Each handler was an inline lambda before Kartend-3fkz step 2
  // pulled the bodies out into named private slots — setScraperService
  // is now a connect table, and each handler is reviewable on its own.
  connect(m_service, &Scraper::ScraperService::scrapeStarted, this,
          &ScrapeResultDialog::onServiceScrapeStarted);
  connect(m_service, &Scraper::ScraperService::itemBegan, this,
          &ScrapeResultDialog::onServiceItemBegan);
  connect(m_service, &Scraper::ScraperService::itemCompleted, this,
          &ScrapeResultDialog::onServiceItemCompleted);
  connect(m_service, &Scraper::ScraperService::pickerNeeded, this,
          &ScrapeResultDialog::onServicePickerNeeded);
  connect(m_service, &Scraper::ScraperService::scrapeFinished, this,
          &ScrapeResultDialog::onServiceScrapeFinished);
  connect(m_service, &Scraper::ScraperService::scrapePaused, this,
          &ScrapeResultDialog::onServiceScrapePaused);
  connect(m_service, &Scraper::ScraperService::quotaUpdated, this,
          &ScrapeResultDialog::onServiceQuotaUpdated);
  // Kartend-ou0a: route the provider's "Hashing ROM…" / "Extracting
  // archive…" stage into the in-dialog label so the user can see
  // what's holding the scrape up, rather than staring at a blank
  // setup page for the multi-minute extraction.
  connect(m_service, &Scraper::ScraperService::itemStageChanged, this,
          [this](const QString &stage) {
            if (!m_stageLabel) return;
            if (stage.isEmpty()) {
              m_stageLabel->clear();
              m_stageLabel->hide();
              if (m_skipItemButton) m_skipItemButton->hide();
            } else {
              m_stageLabel->setText(stage);
              m_stageLabel->show();
              // A stage is running — offer the per-item skip (re-enable in
              // case a previous skip click left the button disabled).
              if (m_skipItemButton) {
                m_skipItemButton->setEnabled(true);
                m_skipItemButton->show();
              }
            }
          });
  // Clear the stage label when candidates arrive or the scrape ends —
  // the provider already emits an empty-string stage on completion, but
  // these are the user-visible "we're done with that work" moments and
  // make the cleanup explicit even if the provider missed a clear.
  connect(m_service, &Scraper::ScraperService::pickerNeeded, this, [this]() {
    if (m_stageLabel) {
      m_stageLabel->clear();
      m_stageLabel->hide();
    }
    if (m_skipItemButton) m_skipItemButton->hide();
  });
  connect(m_service, &Scraper::ScraperService::scrapeFinished, this, [this]() {
    if (m_stageLabel) {
      m_stageLabel->clear();
      m_stageLabel->hide();
    }
    if (m_skipItemButton) m_skipItemButton->hide();
  });
}

void ScrapeResultDialog::skipCurrentScrapeItem() {
  // Unified-auto runs through the long-lived ScraperService; the legacy
  // in-dialog unified path drives a BatchScrapeRunner directly. Prefer the
  // service when it's active, else fall back to the bound runner.
  if (m_service && m_service->isActive()) {
    m_service->skipCurrentItem();
  } else if (m_batchRunner) {
    m_batchRunner->skipCurrentItem();
  }
}

void ScrapeResultDialog::onServiceScrapeStarted(int total) {
  m_unified->onServiceScrapeStarted(total);
}

void ScrapeResultDialog::onServiceItemBegan(int done, int total, const QString &collectionName,
                                            const QString &name) {
  m_unified->onServiceItemBegan(done, total, collectionName, name);
}

void ScrapeResultDialog::onServiceItemCompleted(int done, int total,
                                                const Scraper::ScrapedItem &scraped,
                                                const QStringList &mediaPaths) {
  m_unified->onServiceItemCompleted(done, total, scraped, mediaPaths);
}

void ScrapeResultDialog::onServicePickerNeeded(
    const QString &itemPath, const QString &itemName,
    const QList<Scraper::ScrapeCandidate> &candidates,
    const std::shared_ptr<MetadataLookupProvider> &provider, const QString &artworkDir) {
  m_unified->onServicePickerNeeded(itemPath, itemName, candidates, provider, artworkDir);
}

void ScrapeResultDialog::onServiceScrapeFinished(const Scraper::ScraperService::Summary &s) {
  m_unified->onServiceScrapeFinished(s);
}

void ScrapeResultDialog::onServiceScrapePaused() {
  m_unified->onServiceScrapePaused();
}

void ScrapeResultDialog::onServiceQuotaUpdated(const Scraper::QuotaStatus &quota) {
  m_unified->onServiceQuotaUpdated(quota);
}

void ScrapeResultDialog::showScrapeErrorDetails() {
  m_unified->showScrapeErrorDetails();
}

void ScrapeResultDialog::onScrapeClicked() {
  m_unified->onScrapeClicked();
}

void ScrapeResultDialog::onApply() {
  if (!m_singleItemView->hasDetail()) {
    return;
  }
  const Scraper::ScrapedItem &currentDetail = m_singleItemView->currentDetail();
  m_result.item = currentDetail;
  m_result.downloads.clear();

  // Mode-keyed media selection. Unified-Interactive has no per-item
  // checkbox panel in the live view (Kartend-xvci step 5 moved the
  // filter into ScrapeResultDialogUnified); legacy single-item / batch
  // walks the SingleItemScrapeView's media checkboxes.
  QList<Scraper::MediaAsset> selected;
  if (m_mode == Mode::Unified && m_unifiedPhase == UnifiedPhase::InteractivePicking) {
    selected = m_unified->selectInteractiveMediaForApply(currentDetail);
  } else {
    for (const auto &row : m_singleItemView->mediaRows()) {
      if (row.first->isChecked()) {
        selected.append(row.second);
      }
    }
  }

  MetadataLookupProvider *provider = m_singleItemView->provider();
  if (selected.isEmpty() || !provider) {
    m_unified->finishCurrentApply();
    return;
  }

  m_applyButton->setEnabled(false);
  m_singleItemView->candidateList()->setEnabled(false);
  m_downloadsTotal = selected.size();
  m_downloadedBytes = 0;
  m_downloadStartMs = QDateTime::currentMSecsSinceEpoch();
  m_singleItemView->statusLabel()->setText(tr("Downloading %1 media items…").arg(m_downloadsTotal));

  // Trace the dispatch + each completion so we can tell where time
  // goes during interactive scrape: dialog-side dispatch loop, HttpClient
  // throttle, or actual network duration.
  auto applyTimer = std::make_shared<QElapsedTimer>();
  applyTimer->start();
  // First selected asset's URL gets logged so we can confirm whether
  // the active preset injected `maxwidth/maxheight` query params.
  // (Otherwise the throttling/concurrency lines tell us nothing
  // about whether the resize policy actually took effect.)
  const QString sample =
      selected.isEmpty() ? QStringLiteral("(none)") : selected.first().url.toString().left(200);
  qCInfo(lcScrapeTimings) << "DIALOG dispatch begin total=" << m_downloadsTotal
                          << "first_url=" << sample;

  dispatchSelectedDownloads(selected, applyTimer);
}

void ScrapeResultDialog::dispatchSelectedDownloads(
    const QList<Scraper::MediaAsset> &selected, const std::shared_ptr<QElapsedTimer> &applyTimer) {
  // Kartend-dpehr: the download orchestration — cross-collection shared-asset
  // dedup, per-game CRC short-circuit, the parallel throttled async fetch, and
  // the Kartend-5g0g2 use-after-free guard — now lives in the non-UI
  // Scraper::ScrapeDownloadDispatcher (unit-tested without a QDialog). The
  // dialog only configures it, forwards progress to its UI, and on completion
  // records the payloads + finishes the apply.
  //
  // The dispatcher is parented to the dialog: if the dialog is destroyed
  // mid-download, the dispatcher dies with it and its QPointer-guarded fetch
  // callbacks no-op — the same lifetime guarantee the inline version had.
  if (!m_downloadDispatcher) {
    m_downloadDispatcher = new Scraper::ScrapeDownloadDispatcher(this);
    connect(m_downloadDispatcher, &Scraper::ScrapeDownloadDispatcher::progressed, this,
            [this](int completed, int /*total*/, qint64 bytesSoFar) {
              m_downloadedBytes = bytesSoFar;
              updateSingleItemProgress(completed);
            });
    connect(m_downloadDispatcher, &Scraper::ScrapeDownloadDispatcher::finished, this,
            [this](const QList<Scraper::PendingMediaWrite> &downloads, qint64 totalBytes) {
              m_downloadedBytes = totalBytes;
              // m_result.downloads was cleared in onApply() before dispatch.
              for (const auto &d : downloads) {
                m_result.downloads.append(MediaDownload{d.asset, d.bytes});
              }
              // finishCurrentApply() may accept()/delete the dialog — last touch.
              m_unified->finishCurrentApply();
            });
  }

  Scraper::ScrapeDownloadDispatcher::Config cfg;
  cfg.provider = m_singleItemView->provider();
  cfg.sharedSearchPaths = m_sharedSearchPaths;
  cfg.rescrapeArtworkDir = m_rescrapeArtworkDir;
  cfg.rescrapeBaseName = m_rescrapeBaseName;
  cfg.rescrapeMode = m_rescrapeMode;
  m_downloadDispatcher->setConfig(cfg);
  m_downloadDispatcher->dispatch(selected, applyTimer);
}

QString ScrapeResultDialog::formatDuration(qint64 ms) {
  // Delegates to the shared helper; the member is kept so existing call sites
  // and the moc-visible signature stay unchanged.
  return DurationFormat::formatDurationMs(ms);
}

void ScrapeResultDialog::updateSingleItemProgress(int completed) {
  // Wall-clock throughput + count + ETA. Bytes/sec drives the rate
  // readout (mirrors aggregate network behaviour rather than per-asset
  // spikes — more useful when mixed image / video downloads are in
  // flight). ETA is items-based (elapsed / completed × remaining)
  // because asset sizes vary by an order of magnitude inside one
  // scrape; per-bytes ETA would whiplash when a 30 MB video lands
  // next to a 50 KB cover.
  const qint64 elapsedMs =
      std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_downloadStartMs);
  const double mibPerSec = (m_downloadedBytes / (1024.0 * 1024.0)) / (elapsedMs / 1000.0);
  QString rateStr;
  if (mibPerSec >= 1.0) {
    rateStr = tr("%1 MiB/s").arg(mibPerSec, 0, 'f', 1);
  } else {
    rateStr = tr("%1 KiB/s").arg(mibPerSec * 1024.0, 0, 'f', 0);
  }
  QString etaStr = QStringLiteral("—");
  if (completed > 0 && m_downloadsTotal > completed) {
    const qint64 etaMs = static_cast<qint64>((double(elapsedMs) / double(completed)) *
                                             double(m_downloadsTotal - completed));
    etaStr = formatDuration(etaMs);
  }
  m_singleItemView->statusLabel()->setText(tr("Downloaded %1 of %2 (%3) · ETA %4")
                                               .arg(completed)
                                               .arg(m_downloadsTotal)
                                               .arg(rateStr)
                                               .arg(etaStr));
}

void ScrapeResultDialog::setBatchRunner(Scraper::BatchScrapeRunner *runner,
                                        const QString &collectionName, int totalItems) {
  m_mode = Mode::Batch;
  m_batchRunner = runner;
  // Flip to the batch page; the single-item splitter stays alive but
  // hidden. Apply is meaningless in batch mode — the runner drives
  // every per-item decision — so we hide it. Cancel keeps its slot in
  // the button row and forwards to runner->cancel() via the lambda in
  // buildUi. The BatchScrapeProgressView observes the runner's signals
  // and emits its own `finished` signal, which the host accepts via
  // the connect set up in buildUi.
  if (m_modeStack) m_modeStack->setCurrentWidget(m_batchView);
  if (m_applyButton) m_applyButton->hide();
  m_batchView->setRunner(runner, collectionName, totalItems);
}

Scraper::BatchScrapeRunner::Summary ScrapeResultDialog::batchSummary() const {
  return m_batchView ? m_batchView->summary() : Scraper::BatchScrapeRunner::Summary{};
}
