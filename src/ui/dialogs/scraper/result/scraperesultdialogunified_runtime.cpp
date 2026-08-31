// Sibling TU of scraperesultdialogunified.cpp — runtime / service-driven
// flow.
//
// Holds the service signal handlers (onServiceScrapeStarted, onServiceItemBegan,
// onServiceItemCompleted, onServicePickerNeeded, onServiceScrapeFinished,
// onServiceScrapePaused, onServiceQuotaUpdated), the scrape kickoff
// (onScrapeClicked, which hands the queue to the ScraperService) plus the
// interactive Apply hop (finishCurrentApply) and the diagnostic helpers
// (updateUnifiedProgressLabel, showScrapeErrorDetails). The dialog drives a
// single ScraperService — the in-dialog fallback orchestration was removed in
// Kartend-4qx7m (it was dead: production always wires a service, no test drove
// it).
//
// All remain ScrapeResultDialogUnified members; this is purely a TU split
// to bring the parent file out of the ~1900 LOC zone. The live-view assembly
// half (buildUnifiedPanel, setUnifiedSetupEnabled, startUnifiedScrape) stays
// in scraperesultdialogunified.cpp; the collection-tree + items-list selection
// state moved to ScrapeResultSelectionModel.

#include <utility>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "scraperesultdialogunified.h"

#include "scraperesultdialog.h"

#include "applicationcontext.h"
#include "batchprogressview.h"
#include "entityjobbuilder.h"
#include "mediatypecheckboxbuilder.h"
#include "scraperesultselectionmodel.h"
#include "scraperesultthumbnailloader.h"
#include "singleitemview.h"
#include "transferrateformat.h"
#include "valuemarqueeticker.h"

#include <algorithm>
#include <limits>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
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
#include <QStackedWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "idatabasemanager.h"
#include "imagedecodeutils.h"
#include "metadatalookupprovider.h"
#include "pathutils.h"
#include "scrapejobgrouping.h"
#include "scrapelogging.h"

namespace {

// Clears every scraped textual field on @p item — used when the user unchecks
// the "_metadata" pseudo-type so apply preserves the DB's existing text rather
// than overwriting it with the scrape. Keep in sync with ScrapedItem's textual
// fields (Kartend-iq04t).
void stripTextualFields(Scraper::ScrapedItem &item) {
  item.title.clear();
  item.description.clear();
  item.genre.clear();
  item.developer.clear();
  item.publisher.clear();
  item.releaseDate.clear();
  item.contentRating.clear();
  item.players.clear();
  item.tagsJson.clear();
  item.customFields.clear();
  item.sourceProviderId.clear();
  item.runtimeSeconds = -1;
}

} // namespace

void ScrapeResultDialogUnified::onServiceScrapeStarted(int total) {
  qCInfo(lcScrapeTimings) << "DIALOG service.scrapeStarted total=" << total;
  // Run boundary: wipe all run-scoped dialog state in one place —
  // mirrors ScraperService::startScrape, which just reset its own
  // counterparts. The seen-keys union is session-scoped and left intact
  // so the pre-seeded known SS keys (plus any keys accumulated during
  // prior runs in this session) stay visible — values clear naturally
  // as each item rewrites them.
  m_dlg->resetRunState();
  setUnifiedSetupEnabled(false);
  m_dlg->m_unifiedProgressBar->setRange(0, std::max(1, total));
  m_dlg->m_unifiedProgressBar->setValue(m_dlg->m_service->itemsCompleted());
  // Live tick (wired once in the host's buildUi) + value-marquee timer:
  // the latter scrolls overflowing chip text L→R then wraps.
  m_dlg->m_liveTickTimer.start();
  m_dlg->m_marqueeTicker->start();
  updateUnifiedProgressLabel();
}

void ScrapeResultDialogUnified::onServiceItemBegan(int done, int total,
                                                   const QString &collectionName,
                                                   const QString &name) {
  Q_UNUSED(total);
  Q_UNUSED(done);
  // Hidden dialog → skip the label-update work. With high
  // batchItemConcurrency this fires several times per second; not
  // worth updating widgets nobody can see.
  if (!m_dlg->isVisible()) return;
  qCDebug(lcScrapeTimings) << "DIALOG service.itemBegan name=" << name;
  // Refresh the collection label HERE, not only in the itemCompleted
  // handler below: itemCompleted fires only on a successful scrape, so
  // a collection whose items all error (or all skip) would otherwise
  // leave the label frozen on the last collection that produced a
  // success while the scrape churns on. itemBegan fires for every item
  // whatever the outcome. Gated on an actual collection change so
  // batchItemConcurrency > 1 doesn't re-set the label as each parallel
  // item in the same collection starts.
  if (collectionName != m_shownCollectionName) {
    m_shownCollectionName = collectionName;
    m_dlg->m_unifiedCurrentLabel->setText(tr("Collection: %1").arg(collectionName));
  }
  // The metadata panel and the richer "last scraped" label form are
  // still updated together by the itemCompleted handler so they always
  // describe the same completed item — deliberately not touched here,
  // where many parallel items mid-lookup would wipe the panel every
  // few hundred ms.
  updateUnifiedProgressLabel();
}

void ScrapeResultDialogUnified::onServiceItemCompleted(int done, int total,
                                                       const Scraper::ScrapedItem &scraped,
                                                       const QStringList &mediaPaths) {
  Q_UNUSED(done);
  Q_UNUSED(total);
  // Hidden dialog → skip every UI update. The service still tracks
  // recentMediaPaths + lastScrapedItem internally, and startUnifiedScrape
  // rebuilds the thumb strip + metadata from that snapshot when the
  // dialog is reopened. No visible work means no reason to decode +
  // smooth-scale thumbnails on the main thread per completed item.
  if (!m_dlg->isVisible()) return;
  // Don't poke the progress bar here — updateUnifiedProgressLabel
  // (called below) is the single source of truth and reads counters
  // straight from the service. Shared field-population path so auto
  // and interactive modes render to identical widgets.
  applyScrapedItemToLive(scraped);
  // Sync the "currently scraping" label with whatever just landed in
  // the metadata panel — both update together so label and fields
  // always describe the same item even when concurrency has many
  // items in flight.
  QString displayName = scraped.title;
  if (displayName.isEmpty() && !mediaPaths.isEmpty()) {
    displayName = QFileInfo(mediaPaths.first()).completeBaseName();
  }
  if (displayName.isEmpty()) {
    displayName = QFileInfo(m_dlg->m_service->currentItemPath()).fileName();
  }
  m_dlg->m_unifiedCurrentLabel->setText(
      tr("Collection: %1 — last scraped: %2")
          .arg(m_dlg->m_service->currentCollectionName(), displayName));
  // Append new media paths to the thumb strip via async decode/scale
  // (off the UI thread). Each completed decode auto-scrolls the strip
  // to its own freshly-added row, so the newest cover is always
  // visible. The strip is icon-only — less crowded, fits more
  // thumbnails — and bounded inside the watcher's finished slot so a
  // long batch doesn't grow it unbounded.
  for (const QString &p : mediaPaths) {
    if (p.isEmpty()) continue;
    m_dlg->m_thumbLoader->appendThumbAsync(p);
  }
  updateUnifiedProgressLabel();
}

void ScrapeResultDialogUnified::onServicePickerNeeded(
    const QString &itemPath, const QString &itemName,
    const QList<Scraper::ScrapeCandidate> &candidates,
    const std::shared_ptr<MetadataLookupProvider> &provider, const QString &artworkDir) {
  Q_UNUSED(artworkDir);
  Q_UNUSED(itemName);
  // Stay on the unified live view (don't flip to the legacy single-item
  // page). Surface a candidate combo at the top of the metadata panel;
  // the existing live fields show the selected candidate's data; Apply
  // button confirms.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractivePicking;
  m_dlg->m_mode = ScrapeResultDialog::Mode::Unified;
  m_interactiveItems = {itemPath};
  m_dlg->m_singleItemView->clearMediaRows();
  // Populate the candidate combo from the lookup result. Block signals
  // during the refill so the first-row change doesn't trigger a stray
  // detail fetch before the view's setProviderAndCandidates below
  // installs the matching candidate-list state.
  {
    QSignalBlocker blocker(m_dlg->m_interactiveCandidateCombo);
    m_dlg->m_interactiveCandidateCombo->clear();
    for (const auto &c : candidates) {
      QString label = c.displayName;
      if (!c.subtitle.isEmpty()) label += QStringLiteral(" — ") + c.subtitle;
      if (c.matchScore >= 0) label += QStringLiteral("  (%1)").arg(c.matchScore);
      m_dlg->m_interactiveCandidateCombo->addItem(label);
    }
  }
  m_dlg->m_interactiveCandidateRow->setVisible(candidates.size() > 0);
  if (m_dlg->m_applyButton) {
    m_dlg->m_applyButton->show();
    m_dlg->m_applyButton->setEnabled(false);
  }
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->hide();
  // Hand the candidate list + provider to the view so its candidate-
  // selection slot fetches detail for row 0 and emits detailLoaded —
  // the host's signal dispatch then calls applyScrapedItemToLive +
  // enables Apply (Kartend-xvci step 4/5).
  m_dlg->m_singleItemView->setProviderAndCandidates(provider.get(), candidates);
}

void ScrapeResultDialogUnified::onServiceScrapeFinished(const Scraper::ScraperService::Summary &s) {
  qCInfo(lcScrapeTimings) << "DIALOG service.scrapeFinished scraped=" << s.scraped
                          << "skipped=" << s.skipped << "errors=" << s.errors
                          << "notFound=" << s.notFound;
  m_dlg->m_liveTickTimer.stop();
  m_dlg->m_marqueeTicker->stop();
  if (m_dlg->m_interactiveCandidateRow) m_dlg->m_interactiveCandidateRow->hide();
  // Reset phase so a subsequent Scrape click isn't rejected by the
  // "if (m_dlg->m_unifiedPhase != Setup) return;" guard in onScrapeClicked.
  // Interactive runs leave the phase at InteractivePicking; auto runs
  // leave it at Setup. We unconditionally snap back here.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Setup;
  setUnifiedSetupEnabled(true);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  if (m_dlg->m_applyButton) m_dlg->m_applyButton->hide();
  // Re-render the counts label from the FINAL summary: the live tick
  // stopped above and updateUnifiedProgressLabel bails once the service is
  // idle, so the label would otherwise keep the last mid-run tick's numbers
  // and disagree with the completion popup (which reports these finals) by
  // whatever landed in the run's last second — entity outcomes and
  // not-found bursts at the queue tail especially.
  setUnifiedCountsLabel(s);
  // setUnifiedSetupEnabled(true) hid the counts label, but when the run
  // recorded errors that label's "Errors N" link is the ONLY entry point to
  // the failure list and the re-queue affordance (Kartend-jjjo5) — and the
  // post-run idle state is precisely when a re-queue can actually start.
  // Keep it visible, same pattern as the quota re-show below.
  if (s.errors > 0) m_dlg->m_unifiedCountsLabel->show();
  // Quota-exhausted stop: setUnifiedSetupEnabled(true) hid the progress
  // label, but the user needs to see WHY the scrape ended early — and
  // when they can resume. Re-show the current-status label with the
  // quota message. The reset time comes from the live quota readout
  // when we have one (the label still holds it); otherwise fall back
  // to the generic "midnight UTC" wording.
  if (s.quotaExhausted) {
    // Provider-neutral wording (Kartend-oa1ry): any provider's request/daily
    // quota — ScreenScraper's 430/431 or a 429 from TMDB et al. — can stop the
    // run. m_lastQuotaResetText is the local-time HH:mm captured from
    // ScreenScraper's last live quota update; only show a concrete reset time
    // when we actually have one (other providers don't report it).
    QString msg = tr("Scrape stopped — the provider's request quota is exhausted.");
    if (!m_lastQuotaResetText.isEmpty()) {
      msg += QLatin1Char(' ') + tr("Resume after it resets (%1).").arg(m_lastQuotaResetText);
    } else {
      msg += QLatin1Char(' ') + tr("Resume after the quota resets.");
    }
    m_dlg->m_unifiedCurrentLabel->setText(msg);
    m_dlg->m_unifiedCurrentLabel->show();
  }
  emit m_dlg->unifiedScrapeFinished(s);
}

void ScrapeResultDialogUnified::onServiceScrapePaused() {
  m_dlg->m_unifiedCurrentLabel->setText(tr("Scrape paused — close to keep paused, or "
                                           "reopen to continue."));
}

void ScrapeResultDialogUnified::onServiceQuotaUpdated(const Scraper::QuotaStatus &quota) {
  if (!m_dlg->m_unifiedQuotaLabel) return;
  // dailyMax 0 = quota unknown (SS didn't report a ceiling); keep the
  // row hidden rather than showing "N / 0".
  if (!quota.valid || quota.dailyMax <= 0) {
    m_dlg->m_unifiedQuotaLabel->hide();
    return;
  }
  m_lastQuotaResetText = quota.resetAtUtc.toLocalTime().toString(QStringLiteral("HH:mm"));
  m_dlg->m_unifiedQuotaLabel->setText(tr("ScreenScraper: %1 / %2 requests today · resets %3")
                                          .arg(quota.dailyUsed)
                                          .arg(quota.dailyMax)
                                          .arg(m_lastQuotaResetText));
  m_dlg->m_unifiedQuotaLabel->show();
}

void ScrapeResultDialogUnified::updateUnifiedProgressLabel() {
  // Counters live on the ScraperService — the single source of truth. When no
  // run is active the totals stay zero and we bail at the `total <= 0` guard
  // below.
  int total = 0;
  int done = 0;
  qint64 startMs = 0;
  if (m_dlg->m_service && m_dlg->m_service->isActive()) {
    total = m_dlg->m_service->totalItems();
    done = m_dlg->m_service->itemsCompleted();
    startMs = m_dlg->m_service->startedAtUnixMs();
  }
  if (total <= 0) return;
  m_dlg->m_unifiedProgressBar->setRange(0, total);
  m_dlg->m_unifiedProgressBar->setValue(done);
  const qint64 elapsedMs = std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - startMs);
  QString etaStr = QStringLiteral("—");
  if (done > 0 && total > done) {
    const qint64 etaMs =
        static_cast<qint64>((double(elapsedMs) / double(done)) * double(total - done));
    etaStr = ScrapeResultDialog::formatDuration(etaMs);
  }
  QString rateStr = QStringLiteral("0 KiB/s");
  if (m_dlg->m_service) {
    // Sliding-window rate: keep samples from the last ~10 seconds
    // and compute (newest.bytes - oldest.bytes) / window-duration.
    // Total-bytes ÷ total-elapsed underreports because lookup-API
    // calls + provider throttling create long no-download stretches
    // that dilute the average.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 bytes = m_dlg->m_service->totalBytesDownloaded();
    constexpr qint64 kWindowMs = 10000;
    m_rateSamples.append({nowMs, bytes});
    while (m_rateSamples.size() > 1 && nowMs - m_rateSamples.first().first > kWindowMs) {
      m_rateSamples.removeFirst();
    }
    if (m_rateSamples.size() >= 2) {
      const auto &oldest = m_rateSamples.first();
      const auto &newest = m_rateSamples.last();
      const qint64 deltaMs = newest.first - oldest.first;
      const qint64 deltaBytes = std::max<qint64>(0, newest.second - oldest.second);
      rateStr = TransferRateFormat::formatTransferRate(deltaBytes, deltaMs);
    }
  }
  m_dlg->m_unifiedTimingLabel->setText(
      tr("Items %1/%2 · Elapsed %3 · ETA %4 · %5")
          .arg(done)
          .arg(total)
          .arg(ScrapeResultDialog::formatDuration(elapsedMs), etaStr, rateStr));
  if (m_dlg->m_service) {
    setUnifiedCountsLabel(m_dlg->m_service->summary());
  }
}

void ScrapeResultDialogUnified::setUnifiedCountsLabel(const Scraper::ScraperService::Summary &s) {
  // "Not found" = items the provider had no entry for (HTTP 404 / no match)
  // — shown apart from Errors so an unmatched-but-fine library reads 0
  // errors (Kartend-e8aag). Render the error count as a clickable link when
  // there are errors, so the user can open the recorded failure messages.
  // Substituted into the %5 slot rather than baked into the tr() string so
  // the translatable text stays markup-free.
  const QString errorsField =
      s.errors > 0 ? QStringLiteral("<a href=\"kartend:scrape-errors\">%1</a>").arg(s.errors)
                   : QString::number(s.errors);
  m_dlg->m_unifiedCountsLabel->setText(
      tr("Scraped %1 items, %2 media  ·  Skipped %3  ·  Not found %4  ·  Errors %5")
          .arg(QString::number(s.scraped), QString::number(s.mediaWritten),
               QString::number(s.skipped), QString::number(s.notFound), errorsField));
}

void ScrapeResultDialogUnified::showScrapeErrorDetails() {
  // Failure messages come straight off the service summary (the single
  // source of truth for the run).
  QStringList failures;
  if (m_dlg->m_service) {
    failures = m_dlg->m_service->summary().firstFailures;
  }

  // The total error count comes straight off the summary; failures is
  // what was actually retained (capped — see kMaxReportedFailures).
  const int totalErrors = m_dlg->m_service ? m_dlg->m_service->summary().errors : failures.size();

  // A resizable dialog with a scrollable list — a misconfigured
  // collection can report hundreds of failures, far past what a
  // QMessageBox can show without clipping.
  QDialog dlg(m_dlg);
  dlg.setWindowTitle(tr("Scrape errors"));
  auto *layout = new QVBoxLayout(&dlg);
  if (failures.isEmpty()) {
    // The error counter advanced but no message was captured.
    layout->addWidget(
        new QLabel(tr("No further error detail was recorded for this scrape."), &dlg));
  } else {
    QString header = tr("The scrape reported the following errors:");
    if (failures.size() < totalErrors) {
      // More items errored than messages were retained — say so rather
      // than letting the user assume the list is complete.
      header = tr("Showing %1 of %2 errors:").arg(failures.size()).arg(totalErrors);
    }
    layout->addWidget(new QLabel(header, &dlg));
    auto *list = new QListWidget(&dlg);
    list->addItems(failures);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setWordWrap(true);
    layout->addWidget(list);
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  // Kartend-jjjo5: offer to re-queue just the errored items. Excludes
  // not-found / skipped by construction (they never enter failedItems), since
  // a true not-found won't change on retry against the same provider. Only
  // when the run recorded re-queueable failures AND the service is idle so a
  // fresh scrape can actually start — during a live run (where the phase is
  // still Setup for auto scrapes) startScrape would refuse (returning false),
  // so offering the button mid-run would be a pointless dead end.
  bool doRescrape = false;
  const bool canRescrape = m_dlg->m_service && !m_dlg->m_service->isActive() &&
                           !m_dlg->m_service->summary().failedItems.isEmpty() &&
                           m_dlg->m_unifiedPhase == ScrapeResultDialog::UnifiedPhase::Setup;
  if (canRescrape) {
    QPushButton *rescrapeBtn =
        buttons->addButton(tr("Re-scrape failed items"), QDialogButtonBox::ActionRole);
    connect(rescrapeBtn, &QPushButton::clicked, &dlg, [&doRescrape, &dlg]() {
      doRescrape = true;
      dlg.accept();
    });
  }
  layout->addWidget(buttons);
  dlg.resize(640, 420);
  dlg.exec();
  if (doRescrape) rescrapeFailedItems();
}

void ScrapeResultDialogUnified::rescrapeFailedItems() {
  // Guarded the same way as onScrapeClicked: only from Setup, with collections,
  // and only while the service is idle (startScrape refuses otherwise).
  if (m_dlg->m_unifiedPhase != ScrapeResultDialog::UnifiedPhase::Setup) return;
  if (!m_dlg->m_service || m_dlg->m_service->isActive() || !m_dlg->m_scraperCtx.collections) return;
  const auto failedItems = m_dlg->m_service->summary().failedItems;
  if (failedItems.isEmpty()) return;

  // Group the errored items by their owning collection, preserving first-seen
  // order so the progress label sequence stays predictable. Entity failures
  // carry a discriminator (isEntity) and their own EntityScrapeTarget — they
  // must be rebuilt AS entity jobs, not stuffed into a game item list where
  // rescrape would dispatch the systemeid as a bogus game lookup.
  QList<int> ownerOrder;
  QHash<int, QStringList> pathsByOwner;
  QHash<int, QList<Scraper::EntityScrapeTarget>> entitiesByOwner;
  const int collectionCount = static_cast<int>(m_dlg->m_scraperCtx.collections->size());
  for (const auto &fi : failedItems) {
    if (fi.collectionIndex < 0 || fi.collectionIndex >= collectionCount) continue;
    if (!pathsByOwner.contains(fi.collectionIndex) && !entitiesByOwner.contains(fi.collectionIndex))
      ownerOrder.append(fi.collectionIndex);
    if (fi.isEntity) {
      entitiesByOwner[fi.collectionIndex].append(fi.entity);
    } else {
      pathsByOwner[fi.collectionIndex].append(fi.path);
    }
  }

  // Rebuild CollectionJobs per owner — same resolution as onScrapeClicked
  // (uuid + artwork dir keyed off the live CollectionConfig). An owner can
  // yield both a game job (its errored item paths) and one entity job per
  // errored entity; pump() already handles a mixed entity+game queue.
  QList<Scraper::ScraperService::CollectionJob> serviceQueue;
  for (int owner : ownerOrder) {
    const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[owner];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
    const QStringList paths = pathsByOwner.value(owner);
    if (!paths.isEmpty()) {
      Scraper::ScraperService::CollectionJob sJob;
      sJob.collectionIndex = owner;
      sJob.collectionUuid = uuid;
      sJob.collectionName = cfg.name;
      sJob.artworkDir = artworkDir;
      sJob.items = paths;
      serviceQueue.append(sJob);
    }
    for (const Scraper::EntityScrapeTarget &entity : entitiesByOwner.value(owner)) {
      Scraper::ScraperService::CollectionJob eJob;
      eJob.collectionIndex = owner;
      eJob.collectionUuid = uuid;
      eJob.collectionName = cfg.name;
      eJob.artworkDir = artworkDir;
      eJob.entity = entity; // isEntityJob() true → dispatched via fetchEntity()
      serviceQueue.append(eJob);
    }
  }
  if (serviceQueue.isEmpty()) return;

  // Reuse the run's media/metadata + mode selections (the setup controls still
  // hold them — onServiceScrapeFinished restored the Setup view).
  bool writeMetadata = true;
  const QSet<QString> mediaFilter = buildMediaFilter(writeMetadata);
  const auto mode = m_dlg->m_modeAutoRadio->isChecked()
                        ? Scraper::ScraperService::Mode::Auto
                        : Scraper::ScraperService::Mode::Interactive;
  qCInfo(lcScrapeTimings) << "DIALOG rescrapeFailedItems: re-queue collections="
                          << serviceQueue.size();
  setUnifiedSetupEnabled(false);
  if (m_dlg->m_closeButton) m_dlg->m_closeButton->show();
  if (!m_dlg->m_service->startScrape(serviceQueue, mode, mediaFilter, writeMetadata)) {
    // Refused (service went active since the idle guard above) — restore the
    // setup view rather than leaving it disabled with no run driving it.
    setUnifiedSetupEnabled(true);
  }
}

QList<Scraper::ScraperService::CollectionJob>
ScrapeResultDialogUnified::buildEntityJobs(int collectionIndex, const QString &uuid,
                                           const QString &artworkDir) const {
  // Kartend-ud6q2: the rules moved to Scraper::buildEntityJobs so the
  // background creation-time fetch (which has no dialog) enqueues exactly what
  // this dialog does. Kept as a thin member because the call sites here
  // already hold the dialog context rather than the collection list.
  if (!m_dlg->m_scraperCtx.collections) return {};
  return Scraper::buildEntityJobs(*m_dlg->m_scraperCtx.collections, collectionIndex, uuid,
                                  artworkDir, m_dlg->m_scraperCtx.providerBuilder);
}

bool ScrapeResultDialogUnified::startEntityScrape(int collectionIndex) {
  if (!m_dlg->m_service || !m_dlg->m_scraperCtx.collections) return false;
  if (collectionIndex < 0 || collectionIndex >= m_dlg->m_scraperCtx.collections->size())
    return false;
  // A run may be live with the dialog hidden (Close detaches, the service keeps
  // going). Launching entity art now would wipe the live-view state below and
  // startScrape would refuse anyway — surface the reason and leave the running
  // scrape's dialog state untouched (same idle guard as rescrapeFailedItems).
  if (m_dlg->m_service->isActive()) {
    QMessageBox::information(m_dlg, tr("Scrape collection info"),
                             tr("A scrape is already running — wait for it to finish."));
    return false;
  }

  // Same collection resolution as onScrapeClicked / rescrape (uuid + artwork dir
  // keyed off the live CollectionConfig).
  const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[collectionIndex];
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);

  // Entity art routes to the collection's OWN provider (the coordinator dispatches
  // by collectionIndex, not entity type): Platform for a ScreenScraper (games)
  // collection, Collection for a TMDB (video) collection (Kartend-ckepd.4 / .5).
  if (!m_dlg->m_scraperCtx.providerBuilder ||
      !m_dlg->m_scraperCtx.providerBuilder(collectionIndex)) {
    QMessageBox::information(m_dlg, tr("Scrape collection info"),
                             tr("No scraper is configured for \"%1\".").arg(cfg.name));
    return false;
  }
  const QList<Scraper::ScraperService::CollectionJob> queue =
      buildEntityJobs(collectionIndex, uuid, artworkDir);
  if (queue.isEmpty()) {
    QMessageBox::information(
        m_dlg, tr("Scrape collection info"),
        tr("No collection- or platform-level info can be fetched for \"%1\".").arg(cfg.name));
    return false;
  }

  // No per-item selection for entity scrapes — reuse the unified page shell but
  // skip the item-grid setup phase: init the page, wipe run-scoped state, then
  // start the service. onServiceScrapeStarted flips the dialog to the Live view.
  m_dlg->m_mode = ScrapeResultDialog::Mode::Unified;
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Setup;
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_unifiedPage);
  m_dlg->m_applyButton->hide();
  m_dlg->m_scrapeButton->hide(); // no manual Scrape step for a one-shot entity fetch
  m_dlg->resetRunState();

  // Auto mode, all entity media, write metadata. The download → _shared / config
  // routing is the persistence sink (Kartend-ckepd.3 / .5). startScrape can
  // only refuse if the service went active behind the guard above — propagate
  // the false so the controller doesn't show a dialog with nothing running.
  return m_dlg->m_service->startScrape(queue, Scraper::ScraperService::Mode::Auto,
                                       /*mediaFilter=*/{}, /*writeMetadata=*/true);
}

QSet<QString> ScrapeResultDialogUnified::buildMediaFilter(bool &writeMetadata) const {
  writeMetadata = true;
  QSet<QString> mediaFilter;
  for (auto it = m_dlg->m_mediaTypeChecks.constBegin(); it != m_dlg->m_mediaTypeChecks.constEnd();
       ++it) {
    if (it.key() == QLatin1String("_metadata")) {
      writeMetadata = it.value()->isChecked();
      continue;
    }
    if (it.value()->isChecked()) {
      mediaFilter.insert(it.key().toLower());
    }
  }
  return mediaFilter;
}

void ScrapeResultDialogUnified::onScrapeClicked() {
  if (m_dlg->m_unifiedPhase != ScrapeResultDialog::UnifiedPhase::Setup) return;
  if (!m_dlg->m_scraperCtx.collections) return;
  // Walk the tree in display order (including subcollections under
  // their parents) so the user sees a predictable collection
  // sequence in the progress label.
  const QList<QTreeWidgetItem *> rowsInOrder = treeRowsInDisplayOrder();
  // Kartend-2mt7v: info-only mode queues entity jobs for the checked rows
  // and nothing else — no owner grouping, no item jobs, no per-item media.
  const bool infoOnly = m_dlg->m_infoOnlyCheck && m_dlg->m_infoOnlyCheck->isChecked();
  // Translate checked rows into ScraperService::CollectionJob entries.
  // Each job carries the collection uuid + artwork dir resolved here
  // (the service's persistence layer keys jobs by these so resume
  // can survive a config reorder).
  QList<Scraper::ScraperService::CollectionJob> serviceQueue;
  // Resolve every checked item to its owning collection, then emit one
  // job per owner. A "shell" parent collection displays the items of
  // its subcollections; checking the parent row pulls those items in,
  // but each item's scraped artwork + metadata must land on the
  // subcollection that owns it — not the parent. ScrapeJobGrouping does
  // the (pure, unit-tested) grouping; owners keep tree display order so
  // the progress label stays predictable.
  QList<int> checkedOrder;
  for (QTreeWidgetItem *row : rowsInOrder) {
    if (row->checkState(0) == Qt::Checked) {
      checkedOrder.append(m_dlg->m_selectionModel->collectionIndexForRow(row));
    }
  }
  const auto ownerGroups = ScrapeJobGrouping::byOwningCollection(
      checkedOrder, m_dlg->m_selectionModel->itemSelectionByCollection(),
      m_dlg->m_selectionModel->itemOwnerByCollection(),
      static_cast<int>(m_dlg->m_scraperCtx.collections->size()));

  // Ride the entity art (platform/collection logo + background) along with
  // the scrape (user decision 2026-08-17): scraping a collection ALSO
  // fetches its art, so the tree/home icons populate without a separate
  // action. Enqueued for EVERY checked row AND every item owner — the two
  // sets genuinely differ in both directions (Kartend-ob1c9.2 field report,
  // "not all entities scraped"): a checked SHELL parent ("Nintendo" holding
  // subcollections) owns no items so it forms no owner group, and checking
  // only that parent makes the CHILDREN owners without their rows being
  // checked. All entity jobs go first — a handful of cheap fetches that put
  // every collection's art on disk before the per-item grind starts (and
  // before a quota death could starve them at the queue tail). The
  // coordinator wires landed art into cfg.collectionIcon / headerLogoImage /
  // backgroundImage; buildEntityJobs itself drops playlists and
  // provider-less collections, and a shell whose platform can't resolve
  // lands in the not-found bucket, not in errors.
  QSet<int> entityQueued;
  const auto appendEntityJobsOnce = [this, &serviceQueue, &entityQueued](int index) {
    if (index < 0 || index >= m_dlg->m_scraperCtx.collections->size()) return;
    if (entityQueued.contains(index)) return;
    entityQueued.insert(index);
    const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[index];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
    serviceQueue.append(buildEntityJobs(index, uuid, artworkDir));
  };
  for (int checkedIndex : checkedOrder) {
    appendEntityJobsOnce(checkedIndex);
  }
  // Owner groups derive from the ITEM selection maps, which info-only mode
  // deliberately leaves untouched (its check states are set with the tree's
  // signals blocked) — entity jobs there come from the checked rows alone.
  if (!infoOnly) {
    for (const auto &group : ownerGroups) {
      appendEntityJobsOnce(group.first);
    }
  }
  if (infoOnly) {
    if (serviceQueue.isEmpty()) {
      QMessageBox::information(m_dlg, tr("Scraper"),
                               tr("Check at least one collection before scraping."));
      return;
    }
    // Entity jobs carry their own media (logo + background + metadata) and
    // have no candidate picker — mirror the per-collection entity launch:
    // auto mode, empty item-media filter, metadata writes on.
    qCInfo(lcScrapeTimings) << "DIALOG onScrapeClicked: info-only path, queue size="
                            << serviceQueue.size();
    setUnifiedSetupEnabled(false);
    if (m_dlg->m_closeButton) m_dlg->m_closeButton->show();
    if (!m_dlg->m_service->startScrape(serviceQueue, Scraper::ScraperService::Mode::Auto,
                                       /*mediaFilter=*/{}, /*writeMetadata=*/true)) {
      setUnifiedSetupEnabled(true);
    }
    return;
  }

  for (const auto &group : ownerGroups) {
    const int owner = group.first;
    const QStringList &items = group.second;
    if (items.isEmpty()) continue;
    const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[owner];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);

    Scraper::ScraperService::CollectionJob sJob;
    sJob.collectionIndex = owner;
    sJob.collectionUuid = uuid;
    sJob.collectionName = cfg.name;
    sJob.artworkDir = artworkDir;
    sJob.items = items;
    serviceQueue.append(sJob);
  }
  if (serviceQueue.isEmpty()) {
    QMessageBox::information(m_dlg, tr("Scraper"),
                             tr("Pick at least one collection (and one item) before scraping."));
    return;
  }
  // Translate user picks into runner config (see buildMediaFilter).
  bool writeMetadata = true;
  const QSet<QString> mediaFilter = buildMediaFilter(writeMetadata);
  const auto mode = m_dlg->m_modeAutoRadio->isChecked()
                        ? Scraper::ScraperService::Mode::Auto
                        : Scraper::ScraperService::Mode::Interactive;

  // Hand the queue off to the long-lived service and let it drive. The
  // dialog's signal handlers (wired in setScraperService) flip the UI into
  // Live view + advance progress from there. Close button now relevant.
  qCInfo(lcScrapeTimings) << "DIALOG onScrapeClicked: service path, queue size="
                          << serviceQueue.size() << "mode="
                          << (mode == Scraper::ScraperService::Mode::Auto ? "auto" : "interactive")
                          << "writeMetadata=" << writeMetadata << "mediaFilter=" << mediaFilter;
  setUnifiedSetupEnabled(false);
  if (m_dlg->m_closeButton) m_dlg->m_closeButton->show();
  if (!m_dlg->m_service->startScrape(serviceQueue, mode, mediaFilter, writeMetadata)) {
    // Refused (a run is already active) — restore the setup view rather than
    // leaving it disabled with no run driving it.
    setUnifiedSetupEnabled(true);
  }
}

void ScrapeResultDialogUnified::finishCurrentApply() {
  // Unified interactive picker: don't close the dialog — advance to the next
  // item instead. Persist via the caller's applyResult hook, then tell the
  // service to advance; it emits `pickerNeeded` for the next item, which the
  // dialog's signal handler flips us into. Stays in the same window throughout.
  if (m_dlg->m_mode == ScrapeResultDialog::Mode::Unified &&
      m_dlg->m_unifiedPhase == ScrapeResultDialog::UnifiedPhase::InteractivePicking) {
    ScrapeResultDialog::Result delivered = m_dlg->m_result;
    auto *metaCheck = m_dlg->m_mediaTypeChecks.value(QStringLiteral("_metadata"));
    if (metaCheck && !metaCheck->isChecked()) {
      stripTextualFields(delivered.item);
    }
    if (m_dlg->m_scraperCtx.applyResult && !m_interactiveItems.isEmpty()) {
      // The service is the source of truth for which collection is being
      // processed (the dialog may be reattached to a resumed run).
      const int idx = m_dlg->m_service->currentCollectionIndex();
      m_dlg->m_scraperCtx.applyResult(idx, m_interactiveItems.first(), delivered);
    }
    // Stay on the unified page. Apply button hides until the next pickerNeeded
    // signal arrives (which re-enables it with the next item's candidates). The
    // interactive candidate row also hides momentarily — the next pickerNeeded
    // re-shows it.
    if (m_dlg->m_applyButton) m_dlg->m_applyButton->hide();
    if (m_dlg->m_interactiveCandidateRow) m_dlg->m_interactiveCandidateRow->hide();
    m_dlg->m_service->applyPick(delivered.item);
    return;
  }
  m_dlg->accept();
}
