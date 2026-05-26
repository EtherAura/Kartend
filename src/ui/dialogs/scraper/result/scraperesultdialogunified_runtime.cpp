// Sibling TU of scraperesultdialogunified.cpp — runtime / service-driven
// flow.
//
// Holds the service signal handlers (onServiceScrapeStarted, onServiceItemBegan,
// onServiceItemCompleted, onServicePickerNeeded, onServiceScrapeFinished,
// onServiceScrapePaused, onServiceQuotaUpdated, plus the per-item /
// per-collection orchestration that begins from onScrapeClicked
// (startNextCollectionInQueue, runAutoCollection, runInteractiveCollection,
// interactiveNextItem, interactiveOnLookupResult, interactiveOnApplied,
// finishCurrentApply, interactiveOnSkipped) and the diagnostic helpers
// (totalCheckedItemCount, updateUnifiedProgressLabel, showScrapeErrorDetails).
//
// All remain ScrapeResultDialogUnified members; this is purely a TU split
// to bring the parent file out of the ~1900 LOC zone. The setup/UI assembly
// half (buildUnifiedPanel, populateCollectionTree + tree-checkbox handlers,
// rebuildItemsList, setUnifiedSetupEnabled, startUnifiedScrape) stays in
// scraperesultdialogunified.cpp.

#include "scraperesultdialogunified.h"

#include "scraperesultdialog.h"

#include "applicationcontext.h"
#include "batchprogressview.h"
#include "mediatypecheckboxbuilder.h"
#include "singleitemview.h"

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

void ScrapeResultDialogUnified::onServiceScrapeStarted(int total) {
  qCInfo(lcScrapeTimings) << "DIALOG service.scrapeStarted total=" << total;
  setUnifiedSetupEnabled(false);
  m_dlg->m_unifiedProgressBar->setRange(0, std::max(1, total));
  m_dlg->m_unifiedProgressBar->setValue(m_dlg->m_service->itemsCompleted());
  // Reset rate-window samples. The seen-keys union is left intact so
  // the pre-seeded known SS keys (plus any keys accumulated during
  // prior runs in this session) stay visible — values clear naturally
  // as each item rewrites them.
  m_dlg->m_rateSamples.clear();
  if (!m_dlg->m_liveTickTimerInited) {
    m_dlg->m_liveTickTimer.setInterval(1000);
    connect(&m_dlg->m_liveTickTimer, &QTimer::timeout, this,
            &ScrapeResultDialogUnified::updateUnifiedProgressLabel);
    m_dlg->m_liveTickTimerInited = true;
  }
  m_dlg->m_liveTickTimer.start();
  // Value-marquee timer: scrolls overflowing chip text L→R then wraps.
  // Lazy-init on first scrapeStart.
  if (!m_dlg->m_marqueeTimerInited) {
    m_dlg->m_marqueeTimer.setInterval(150);
    connect(&m_dlg->m_marqueeTimer, &QTimer::timeout, this,
            &ScrapeResultDialogUnified::tickValueMarquees);
    m_dlg->m_marqueeTimerInited = true;
  }
  m_dlg->m_marqueePauseTicks.clear();
  m_dlg->m_marqueeTimer.start();
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
  if (collectionName != m_dlg->m_shownCollectionName) {
    m_dlg->m_shownCollectionName = collectionName;
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
  // straight from the service. Doing both used to race: this slot
  // would set the value, then the helper would reset it from the
  // legacy m_dlg->m_unifiedItemsCompletedAcross (always 0 in service mode),
  // so the bar stayed at zero. Shared field-population path so auto
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
    appendThumbAsync(p);
  }
  updateUnifiedProgressLabel();
}

void ScrapeResultDialogUnified::onServicePickerNeeded(
    const QString &itemPath, const QString &itemName,
    const QList<Scraper::ScrapeCandidate> &candidates,
    std::shared_ptr<MetadataLookupProvider> provider, const QString &artworkDir) {
  Q_UNUSED(artworkDir);
  Q_UNUSED(itemName);
  // Stay on the unified live view (don't flip to the legacy single-item
  // page). Surface a candidate combo at the top of the metadata panel;
  // the existing live fields show the selected candidate's data; Apply
  // button confirms.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractivePicking;
  m_dlg->m_mode = ScrapeResultDialog::Mode::Unified;
  m_dlg->m_interactiveProvider = provider;
  m_dlg->m_interactiveItems = {itemPath};
  m_dlg->m_interactiveCursor = 0;
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
                          << "skipped=" << s.skipped << "errors=" << s.errors;
  m_dlg->m_liveTickTimer.stop();
  m_dlg->m_marqueeTimer.stop();
  m_dlg->m_marqueePauseTicks.clear();
  if (m_dlg->m_interactiveCandidateRow) m_dlg->m_interactiveCandidateRow->hide();
  // Reset phase so a subsequent Scrape click isn't rejected by the
  // "if (m_dlg->m_unifiedPhase != Setup) return;" guard in onScrapeClicked.
  // Interactive runs leave the phase at InteractivePicking; auto runs
  // leave it at Setup. We unconditionally snap back here.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Setup;
  setUnifiedSetupEnabled(true);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  if (m_dlg->m_applyButton) m_dlg->m_applyButton->hide();
  // Quota-exhausted stop: setUnifiedSetupEnabled(true) hid the progress
  // label, but the user needs to see WHY the scrape ended early — and
  // when they can resume. Re-show the current-status label with the
  // quota message. The reset time comes from the live quota readout
  // when we have one (the label still holds it); otherwise fall back
  // to the generic "midnight UTC" wording.
  if (s.quotaExhausted) {
    // m_dlg->m_lastQuotaResetText is the local-time HH:mm captured from the
    // last live quota update; fall back to the generic wording when no
    // quota update arrived (e.g. the very first item hit 430 before
    // any ssuser block was parsed).
    const QString resetText =
        m_dlg->m_lastQuotaResetText.isEmpty() ? tr("midnight UTC") : m_dlg->m_lastQuotaResetText;
    m_dlg->m_unifiedCurrentLabel->setText(
        tr("Scrape stopped — ScreenScraper's daily quota is exhausted. "
           "Resume after it resets (%1).")
            .arg(resetText));
    m_dlg->m_unifiedCurrentLabel->show();
  }
  emit m_dlg->unifiedScrapeFinished(s.scraped, s.skipped, s.errors, s.firstFailures);
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
  m_dlg->m_lastQuotaResetText = quota.resetAtUtc.toLocalTime().toString(QStringLiteral("HH:mm"));
  m_dlg->m_unifiedQuotaLabel->setText(tr("ScreenScraper: %1 / %2 requests today · resets %3")
                                          .arg(quota.dailyUsed)
                                          .arg(quota.dailyMax)
                                          .arg(m_dlg->m_lastQuotaResetText));
  m_dlg->m_unifiedQuotaLabel->show();
}

int ScrapeResultDialogUnified::totalCheckedItemCount() const {
  int total = 0;
  for (auto it = m_dlg->m_itemSelectionByCollection.constBegin();
       it != m_dlg->m_itemSelectionByCollection.constEnd(); ++it) {
    total += it.value().size();
  }
  return total;
}

void ScrapeResultDialogUnified::updateUnifiedProgressLabel() {
  // Service-driven path: counters live on the ScraperService, not on
  // the legacy m_dlg->m_unified* fields. Read from whichever is the source
  // of truth for the active run.
  int total = 0;
  int done = 0;
  qint64 startMs = 0;
  int scraped = 0;
  int skipped = 0;
  int errors = 0;
  if (m_dlg->m_service && m_dlg->m_service->isActive()) {
    total = m_dlg->m_service->totalItems();
    done = m_dlg->m_service->itemsCompleted();
    startMs = m_dlg->m_service->startedAtUnixMs();
    const auto s = m_dlg->m_service->summary();
    scraped = s.scraped;
    skipped = s.skipped;
    errors = s.errors;
  } else {
    total = totalCheckedItemCount();
    done = m_dlg->m_unifiedItemsCompletedAcross;
    startMs = m_dlg->m_unifiedStartMs;
    scraped = m_dlg->m_unifiedScrapedTotal;
    skipped = m_dlg->m_unifiedSkippedTotal;
    errors = m_dlg->m_unifiedErrorsTotal;
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
    m_dlg->m_rateSamples.append({nowMs, bytes});
    while (m_dlg->m_rateSamples.size() > 1 &&
           nowMs - m_dlg->m_rateSamples.first().first > kWindowMs) {
      m_dlg->m_rateSamples.removeFirst();
    }
    if (m_dlg->m_rateSamples.size() >= 2) {
      const auto &oldest = m_dlg->m_rateSamples.first();
      const auto &newest = m_dlg->m_rateSamples.last();
      const qint64 deltaMs = std::max<qint64>(1, newest.first - oldest.first);
      const qint64 deltaBytes = std::max<qint64>(0, newest.second - oldest.second);
      const double mibPerSec = (deltaBytes / (1024.0 * 1024.0)) / (deltaMs / 1000.0);
      if (mibPerSec >= 1.0) {
        rateStr = tr("%1 MiB/s").arg(mibPerSec, 0, 'f', 1);
      } else {
        rateStr = tr("%1 KiB/s").arg(mibPerSec * 1024.0, 0, 'f', 0);
      }
    }
  }
  m_dlg->m_unifiedTimingLabel->setText(
      tr("Items %1/%2 · Elapsed %3 · ETA %4 · %5")
          .arg(done)
          .arg(total)
          .arg(ScrapeResultDialog::formatDuration(elapsedMs), etaStr, rateStr));
  int mediaWritten = 0;
  if (m_dlg->m_service) mediaWritten = m_dlg->m_service->summary().mediaWritten;
  // Render the error count as a clickable link when there are errors,
  // so the user can open the recorded failure messages. Substituted
  // into the %4 slot rather than baked into the tr() string so the
  // translatable text stays markup-free.
  const QString errorsField =
      errors > 0 ? QStringLiteral("<a href=\"kartend:scrape-errors\">%1</a>").arg(errors)
                 : QString::number(errors);
  m_dlg->m_unifiedCountsLabel->setText(tr("Scraped %1 items, %2 media  ·  Skipped %3  ·  Errors %4")
                                           .arg(QString::number(scraped),
                                                QString::number(mediaWritten),
                                                QString::number(skipped), errorsField));
}

void ScrapeResultDialogUnified::showScrapeErrorDetails() {
  // Gather failure messages from both the service summary (live /
  // service-driven runs) and the in-dialog accumulator (the fallback
  // orchestration path); dedupe so a message recorded by both isn't
  // listed twice.
  QStringList failures = m_dlg->m_unifiedFailures;
  if (m_dlg->m_service) {
    for (const QString &failure : m_dlg->m_service->summary().firstFailures) {
      if (!failures.contains(failure)) {
        failures.append(failure);
      }
    }
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
  layout->addWidget(buttons);
  dlg.resize(640, 420);
  dlg.exec();
}

void ScrapeResultDialogUnified::onScrapeClicked() {
  if (m_dlg->m_unifiedPhase != ScrapeResultDialog::UnifiedPhase::Setup) return;
  if (!m_dlg->m_scraperCtx.collections) return;
  // Walk the tree in display order (including subcollections under
  // their parents) so the user sees a predictable collection
  // sequence in the progress label. Recursive walk because the tree
  // is now hierarchical (top-level + subcollection rows).
  QList<QTreeWidgetItem *> rowsInOrder;
  std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
    if (!item) return;
    rowsInOrder.append(item);
    for (int i = 0; i < item->childCount(); ++i) walk(item->child(i));
  };
  for (int i = 0; i < m_dlg->m_collectionTree->topLevelItemCount(); ++i) {
    walk(m_dlg->m_collectionTree->topLevelItem(i));
  }
  // Translate checked rows into ScraperService::CollectionJob entries.
  // Each job carries the collection uuid + artwork dir resolved here
  // (the service's persistence layer keys jobs by these so resume
  // can survive a config reorder).
  QList<Scraper::ScraperService::CollectionJob> serviceQueue;
  // Legacy/test fallback queue uses the dialog's own CollectionJob
  // shape; populated in parallel so the in-dialog orchestration still
  // runs when no service is wired.
  m_dlg->m_unifiedQueue.clear();
  m_dlg->m_unifiedQueueCursor = 0;
  m_dlg->m_unifiedScrapedTotal = 0;
  m_dlg->m_unifiedSkippedTotal = 0;
  m_dlg->m_unifiedErrorsTotal = 0;
  m_dlg->m_unifiedFailures.clear();
  m_dlg->m_unifiedItemsCompletedAcross = 0;
  m_dlg->m_unifiedCancelled = false;
  m_dlg->m_unifiedStartMs = QDateTime::currentMSecsSinceEpoch();
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
      checkedOrder.append(m_dlg->m_treeItemToCollectionIndex.value(row, -1));
    }
  }
  const auto ownerGroups = ScrapeJobGrouping::byOwningCollection(
      checkedOrder, m_dlg->m_itemSelectionByCollection, m_dlg->m_itemOwnerByCollection,
      static_cast<int>(m_dlg->m_scraperCtx.collections->size()));
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

    ScrapeResultDialog::CollectionJob job;
    job.collectionIndex = owner;
    job.collectionName = cfg.name;
    job.items = items;
    m_dlg->m_unifiedQueue.append(job);
  }
  if (serviceQueue.isEmpty()) {
    QMessageBox::information(m_dlg, tr("Scraper"),
                             tr("Pick at least one collection (and one item) before scraping."));
    return;
  }
  // Translate user picks into runner config. Filter keys are
  // lowercased: the runner matches each asset's `type.toLower()`
  // against this set, so a mixed-case key (e.g. "support-2D") would
  // otherwise never match and that media type would silently never
  // download.
  QSet<QString> mediaFilter;
  bool writeMetadata = true;
  for (auto it = m_dlg->m_mediaTypeChecks.constBegin(); it != m_dlg->m_mediaTypeChecks.constEnd();
       ++it) {
    if (it.key() == QLatin1String("_metadata")) {
      writeMetadata = it.value()->isChecked();
      continue;
    }
    if (it.value()->isChecked()) mediaFilter.insert(it.key().toLower());
  }
  const auto mode = m_dlg->m_modeAutoRadio->isChecked()
                        ? Scraper::ScraperService::Mode::Auto
                        : Scraper::ScraperService::Mode::Interactive;

  if (m_dlg->m_service) {
    // Production path: hand the queue off to the long-lived service
    // and let it drive. The dialog's signal handlers (wired in
    // setScraperService) flip the UI into Live view + advance progress
    // from there. Close button now relevant.
    qCInfo(lcScrapeTimings) << "DIALOG onScrapeClicked: service path, queue size="
                            << serviceQueue.size() << "mode="
                            << (mode == Scraper::ScraperService::Mode::Auto ? "auto"
                                                                            : "interactive")
                            << "writeMetadata=" << writeMetadata << "mediaFilter=" << mediaFilter;
    setUnifiedSetupEnabled(false);
    if (m_dlg->m_closeButton) m_dlg->m_closeButton->show();
    m_dlg->m_service->startScrape(serviceQueue, mode, mediaFilter, writeMetadata);
    return;
  }

  // Fallback path: no service wired — run the orchestration in the
  // dialog itself (matches the v1 behaviour for tests / unit harnesses
  // that haven't been migrated to the service yet). This path keeps
  // working but does NOT survive dialog close / app exit.
  setUnifiedSetupEnabled(false);
  m_dlg->m_unifiedProgressBar->setRange(0, totalCheckedItemCount());
  m_dlg->m_unifiedProgressBar->setValue(0);
  updateUnifiedProgressLabel();
  startNextCollectionInQueue();
}

void ScrapeResultDialogUnified::startNextCollectionInQueue() {
  if (m_dlg->m_unifiedCancelled || m_dlg->m_unifiedQueueCursor >= m_dlg->m_unifiedQueue.size()) {
    // All done — fire summary signal, leave the dialog open with the
    // final state visible (caller can dismiss).
    m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Done;
    m_dlg->m_unifiedCurrentLabel->setText(tr("Finished."));
    emit m_dlg->unifiedScrapeFinished(m_dlg->m_unifiedScrapedTotal, m_dlg->m_unifiedSkippedTotal,
                                      m_dlg->m_unifiedErrorsTotal, m_dlg->m_unifiedFailures);
    m_dlg->accept();
    return;
  }
  const ScrapeResultDialog::CollectionJob &job = m_dlg->m_unifiedQueue[m_dlg->m_unifiedQueueCursor];
  m_dlg->m_unifiedCurrentLabel->setText(tr("Collection: %1  (%2 of %3)")
                                            .arg(job.collectionName)
                                            .arg(m_dlg->m_unifiedQueueCursor + 1)
                                            .arg(m_dlg->m_unifiedQueue.size()));
  updateUnifiedProgressLabel();
  if (m_dlg->m_modeAutoRadio->isChecked()) {
    m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::AutoRunning;
    runAutoCollection(job.collectionIndex, job.items);
  } else {
    m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractiveLookingUp;
    runInteractiveCollection(job.collectionIndex, job.items);
  }
}

void ScrapeResultDialogUnified::runAutoCollection(int collectionIndex, const QStringList &items) {
  // Kartend-m02z: ScraperContext now carries the full ApplicationContext;
  // a missing/null ctx is treated like the legacy missing databaseManager.
  auto *runAutoDb = m_dlg->m_scraperCtx.ctx ? m_dlg->m_scraperCtx.ctx->databaseManager() : nullptr;
  if (!m_dlg->m_scraperCtx.providerBuilder || !runAutoDb || !m_dlg->m_scraperCtx.generalSettings ||
      !m_dlg->m_scraperCtx.collections) {
    ++m_dlg->m_unifiedErrorsTotal;
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  std::shared_ptr<MetadataLookupProvider> provider =
      m_dlg->m_scraperCtx.providerBuilder(collectionIndex);
  if (!provider) {
    m_dlg->m_unifiedFailures.append(
        tr("%1: no provider applies")
            .arg(m_dlg->m_unifiedQueue[m_dlg->m_unifiedQueueCursor].collectionName));
    m_dlg->m_unifiedErrorsTotal += items.size();
    m_dlg->m_unifiedItemsCompletedAcross += items.size();
    ++m_dlg->m_unifiedQueueCursor;
    updateUnifiedProgressLabel();
    startNextCollectionInQueue();
    return;
  }
  const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[collectionIndex];
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
  const Scraper::RescrapeMode rescrapeMode = static_cast<Scraper::RescrapeMode>(
      m_dlg->m_scraperCtx.generalSettings->scraperOptions.rescrapeMode);
  const int itemConcurrency =
      m_dlg->m_scraperCtx.generalSettings->scraperOptions.batchItemConcurrency;
  const int skipRecentDays =
      m_dlg->m_scraperCtx.generalSettings->scraperOptions.skipRecentScrapeDays;

  // Translate the user's media-type checkboxes into the runner's
  // filter set. The synthetic `_metadata` key gates text-field
  // persistence and is consumed here (stripped from the filter set,
  // routed to setWriteMetadata instead). Empty media filter → runner
  // falls back to legacy "front only" behaviour; non-empty → runner
  // fetches every matching type per item in parallel.
  QSet<QString> mediaFilter;
  bool writeMetadata = true;
  for (auto it = m_dlg->m_mediaTypeChecks.constBegin(); it != m_dlg->m_mediaTypeChecks.constEnd();
       ++it) {
    if (it.key() == QLatin1String("_metadata")) {
      writeMetadata = it.value()->isChecked();
      continue;
    }
    if (it.value()->isChecked()) mediaFilter.insert(it.key().toLower());
  }

  auto *runner = new Scraper::BatchScrapeRunner(
      m_dlg->m_scraperCtx.ctx, std::move(provider), uuid, items, artworkDir,
      /*fetchPrimaryCover=*/true, rescrapeMode, itemConcurrency, skipRecentDays, this);
  runner->setMediaTypeFilter(mediaFilter);
  runner->setWriteMetadata(writeMetadata);
  m_dlg->m_batchRunner = runner;

  connect(
      runner, &Scraper::BatchScrapeRunner::progress, this,
      [this](int done, int total, const QString &name) {
        // `done` is per-collection; aggregate across queue items
        // for the dialog's outer progress. m_dlg->m_unifiedItemsCompletedAcross
        // accumulates the prior queue items' completions before this
        // collection started — the +done below is per-collection
        // progress on top of that running total.
        const int totalAcross = totalCheckedItemCount();
        m_dlg->m_unifiedProgressBar->setRange(0, totalAcross);
        m_dlg->m_unifiedProgressBar->setValue(m_dlg->m_unifiedItemsCompletedAcross + done);
        m_dlg->m_unifiedCurrentLabel->setText(
            tr("Scraping: %1  (item %2 of %3 in this collection)").arg(name).arg(done).arg(total));
        updateUnifiedProgressLabel();
      });
  connect(runner, &Scraper::BatchScrapeRunner::finished, this,
          [this, runner, items](const Scraper::BatchScrapeRunner::Summary &s) {
            m_dlg->m_unifiedScrapedTotal += s.scraped;
            m_dlg->m_unifiedSkippedTotal += s.skipped;
            m_dlg->m_unifiedErrorsTotal += s.errors;
            m_dlg->m_unifiedFailures.append(s.firstFailures);
            m_dlg->m_unifiedItemsCompletedAcross += items.size();
            m_dlg->m_batchRunner = nullptr;
            runner->deleteLater();
            ++m_dlg->m_unifiedQueueCursor;
            startNextCollectionInQueue();
          });
  runner->start();
}

void ScrapeResultDialogUnified::runInteractiveCollection(int collectionIndex,
                                                         const QStringList &items) {
  if (!m_dlg->m_scraperCtx.providerBuilder) {
    ++m_dlg->m_unifiedErrorsTotal;
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  m_dlg->m_interactiveProvider = m_dlg->m_scraperCtx.providerBuilder(collectionIndex);
  m_dlg->m_interactiveItems = items;
  m_dlg->m_interactiveCursor = 0;
  m_dlg->m_interactiveCollectionIndex = collectionIndex;
  if (!m_dlg->m_interactiveProvider) {
    m_dlg->m_unifiedFailures.append(
        tr("%1: no provider applies")
            .arg(m_dlg->m_unifiedQueue[m_dlg->m_unifiedQueueCursor].collectionName));
    m_dlg->m_unifiedErrorsTotal += items.size();
    m_dlg->m_unifiedItemsCompletedAcross += items.size();
    ++m_dlg->m_unifiedQueueCursor;
    updateUnifiedProgressLabel();
    startNextCollectionInQueue();
    return;
  }
  interactiveNextItem();
}

void ScrapeResultDialogUnified::interactiveNextItem() {
  if (m_dlg->m_unifiedCancelled || m_dlg->m_interactiveCursor >= m_dlg->m_interactiveItems.size()) {
    // End of items for this collection — advance the outer queue.
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  const QString filePath = m_dlg->m_interactiveItems[m_dlg->m_interactiveCursor];
  m_dlg->m_unifiedCurrentLabel->setText(tr("Looking up: %1").arg(QFileInfo(filePath).fileName()));
  // Issue the lookup; once candidates land we flip to the single-item
  // page and let the user pick.
  const QString queryText = QFileInfo(filePath).completeBaseName();
  MetadataLookupProvider::LookupContext ctx{queryText, filePath};
  QPointer<ScrapeResultDialog> guard(m_dlg);
  m_dlg->m_interactiveProvider->lookup(
      ctx, [guard](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> r) {
        if (guard.isNull()) return;
        guard->m_unified->interactiveOnLookupResult(r);
      });
}

void ScrapeResultDialogUnified::interactiveOnLookupResult(
    ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
  if (m_dlg->m_unifiedCancelled) {
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  if (result.isError() || result.value().isEmpty()) {
    if (result.isError()) {
      ++m_dlg->m_unifiedErrorsTotal;
      m_dlg->m_unifiedFailures.append(QStringLiteral("%1: %2").arg(
          QFileInfo(m_dlg->m_interactiveItems[m_dlg->m_interactiveCursor]).fileName(),
          result.error().message));
    } else {
      ++m_dlg->m_unifiedSkippedTotal;
    }
    ++m_dlg->m_unifiedItemsCompletedAcross;
    ++m_dlg->m_interactiveCursor;
    updateUnifiedProgressLabel();
    interactiveNextItem();
    return;
  }
  // Flip to the single-item picker page for this item. Hand the
  // candidate list to the view (Kartend-xvci step 4) — its
  // setProviderAndCandidates populates the list widget, hides the
  // panel on single-result responses, pre-selects row 0, and the
  // resulting onCandidateSelected fetches detail. After the user
  // clicks Apply or Cancel we catch the result via interactiveOnApplied
  // / interactiveOnSkipped.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractivePicking;
  m_dlg->m_singleItemView->clearMediaRows();
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_singleItemView);
  m_dlg->m_applyButton->show();
  m_dlg->m_applyButton->setEnabled(false);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->hide();
  m_dlg->m_singleItemView->setProviderAndCandidates(m_dlg->m_interactiveProvider.get(),
                                                    result.value());
}

void ScrapeResultDialogUnified::interactiveOnApplied() {
  // Called when the user's Apply finishes (m_dlg->m_result populated).
  // Honour the `_metadata` checkbox by stripping textual fields when
  // unchecked — applyResult downstream will then preserve whatever's
  // in the DB instead of overwriting with the scrape's text.
  ScrapeResultDialog::Result delivered = m_dlg->m_result;
  auto *metaCheck = m_dlg->m_mediaTypeChecks.value(QStringLiteral("_metadata"));
  if (metaCheck && !metaCheck->isChecked()) {
    delivered.item.title.clear();
    delivered.item.description.clear();
    delivered.item.genre.clear();
    delivered.item.developer.clear();
    delivered.item.publisher.clear();
    delivered.item.releaseDate.clear();
    delivered.item.contentRating.clear();
    delivered.item.players.clear();
    delivered.item.tagsJson.clear();
    delivered.item.customFields.clear();
    delivered.item.sourceProviderId.clear();
    delivered.item.runtimeSeconds = -1;
  }
  if (m_dlg->m_scraperCtx.applyResult) {
    m_dlg->m_scraperCtx.applyResult(m_dlg->m_interactiveCollectionIndex,
                                    m_dlg->m_interactiveItems[m_dlg->m_interactiveCursor],
                                    delivered);
  }
  ++m_dlg->m_unifiedScrapedTotal;
  ++m_dlg->m_unifiedItemsCompletedAcross;
  ++m_dlg->m_interactiveCursor;
  updateUnifiedProgressLabel();
  // Switch back to the unified page for the next item's lookup phase.
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_unifiedPage);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  m_dlg->m_applyButton->hide();
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractiveLookingUp;
  interactiveNextItem();
}

void ScrapeResultDialogUnified::finishCurrentApply() {
  // Unified interactive picker: don't close the dialog — advance to
  // the next item instead.
  if (m_dlg->m_mode == ScrapeResultDialog::Mode::Unified &&
      m_dlg->m_unifiedPhase == ScrapeResultDialog::UnifiedPhase::InteractivePicking) {
    // Service-driven path: persist via the caller's applyResult hook,
    // then tell the service to advance. The service emits
    // `pickerNeeded` for the next item, which the dialog's signal
    // handler flips us into. Stays in the same window throughout.
    if (m_dlg->m_service) {
      ScrapeResultDialog::Result delivered = m_dlg->m_result;
      auto *metaCheck = m_dlg->m_mediaTypeChecks.value(QStringLiteral("_metadata"));
      if (metaCheck && !metaCheck->isChecked()) {
        delivered.item.title.clear();
        delivered.item.description.clear();
        delivered.item.genre.clear();
        delivered.item.developer.clear();
        delivered.item.publisher.clear();
        delivered.item.releaseDate.clear();
        delivered.item.contentRating.clear();
        delivered.item.players.clear();
        delivered.item.tagsJson.clear();
        delivered.item.customFields.clear();
        delivered.item.sourceProviderId.clear();
        delivered.item.runtimeSeconds = -1;
      }
      if (m_dlg->m_scraperCtx.applyResult && !m_dlg->m_interactiveItems.isEmpty()) {
        // The service is the source of truth for which collection
        // is being processed (the dialog may be reattached to a
        // resumed run where m_dlg->m_interactiveCollectionIndex is stale).
        const int idx = m_dlg->m_service->currentCollectionIndex();
        m_dlg->m_scraperCtx.applyResult(idx, m_dlg->m_interactiveItems.first(), delivered);
      }
      // Stay on the unified page. Apply button hides until the next
      // pickerNeeded signal arrives (which re-enables it with the
      // next item's candidates). The interactive candidate row also
      // hides momentarily — the next pickerNeeded re-shows it.
      if (m_dlg->m_applyButton) m_dlg->m_applyButton->hide();
      if (m_dlg->m_interactiveCandidateRow) m_dlg->m_interactiveCandidateRow->hide();
      m_dlg->m_service->applyPick(delivered.item);
      return;
    }
    interactiveOnApplied();
    return;
  }
  m_dlg->accept();
}

void ScrapeResultDialogUnified::interactiveOnSkipped() {
  ++m_dlg->m_unifiedSkippedTotal;
  ++m_dlg->m_unifiedItemsCompletedAcross;
  ++m_dlg->m_interactiveCursor;
  updateUnifiedProgressLabel();
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_unifiedPage);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  m_dlg->m_applyButton->hide();
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractiveLookingUp;
  interactiveNextItem();
}
