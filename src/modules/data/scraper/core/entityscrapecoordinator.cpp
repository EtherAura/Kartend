// Entity-scrape execution engine, split out of scraperservice.cpp (see the
// class doc in the header for the friend + back-pointer rationale). Bodies
// moved verbatim; service state is reached through m_svc.
#include "entityscrapecoordinator.h"

#include <atomic>
#include <limits>
#include <memory>

#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>

#include "applicationcontext.h"
#include "batchscraperunner.h"
#include "collection/typehelpers.h"
#include "isettingsmanager.h"
#include "scraperservice.h"

namespace Scraper {

namespace {
Q_LOGGING_CATEGORY(lcEntityScrape, "kartend.scraperservice.entity", QtWarningMsg)
} // namespace

void EntityScrapeCoordinator::startEntityCollection() {
  auto &job = m_svc->m_queue[m_svc->m_queueCursor];
  qCInfo(lcEntityScrape) << "startEntityCollection idx=" << job.collectionIndex
                         << "name=" << job.collectionName
                         << "entityType=" << static_cast<int>(job.entity.type);
  auto provider =
      m_svc->m_ctx.providerBuilder ? m_svc->m_ctx.providerBuilder(job.collectionIndex) : nullptr;
  if (!provider) {
    // No provider resolves for this entity — count it as a single error and
    // advance, mirroring startAutoCollection's no-provider branch.
    ++m_svc->m_summary.errors;
    if (m_svc->m_summary.firstFailures.size() < kMaxReportedFailures) {
      m_svc->m_summary.firstFailures.append(
          QStringLiteral("%1: no provider applies").arg(job.collectionName));
    }
    if (m_svc->m_summary.failedItems.size() < kMaxReportedFailures) {
      m_svc->m_summary.failedItems.append({job.collectionIndex, job.entity.identity,
                                           job.collectionUuid, /*isEntity=*/true, job.entity});
    }
    ++m_svc->m_itemsCompleted;
    ++m_svc->m_queueCursor;
    m_svc->schedulePersist();
    m_svc->pump();
    return;
  }
  // Surface a "began" progress tick so the dialog labels the in-flight entity.
  emit m_svc->itemBegan(m_svc->m_itemsCompleted, m_svc->m_totalItemsAtStart, job.collectionName,
                        job.collectionName);
  // Capture the shared_ptr provider in the callback so it outlives the async
  // fetch; QPointer guards against the service being torn down first; the run
  // generation rejects a callback that resolves after a cancel-then-restart.
  const Scraper::EntityScrapeTarget target = job.entity;
  const quint64 generation = m_svc->m_runGeneration;
  QPointer<ScraperService> self(m_svc);
  provider->fetchEntity(
      target, [self, provider, generation](const ErrorUtils::Result<Scraper::ScrapedItem> &result) {
        if (self.isNull() || self->m_runGeneration != generation) return;
        self->m_entityCoordinator.onEntityFetchComplete(result, provider, generation);
      });
}

void EntityScrapeCoordinator::onEntityFetchComplete(
    const ErrorUtils::Result<Scraper::ScrapedItem> &result,
    const std::shared_ptr<MetadataLookupProvider> &provider, quint64 generation) {
  // Stale result after cancel/finish — don't advance a queue we no longer own.
  if (m_svc->m_state != ScraperService::State::RunningAuto &&
      m_svc->m_state != ScraperService::State::RunningInteractive)
    return;
  if (m_svc->m_queueCursor >= m_svc->m_queue.size()) return;
  const auto &job = m_svc->m_queue[m_svc->m_queueCursor];
  if (result.isError()) {
    const auto &err = result.error();
    // Quota exhaustion (as classified by the provider that made the request)
    // kills every remaining request, entity or game alike — stop the queue
    // and keep THIS job queued as the resume point (the fetch consumed no
    // art; a post-reset resume re-fires it for the price of one request).
    // Without this a mixed entity+game queue kept machine-gunning after
    // quota death.
    if (provider && provider->isQuotaExhausted(err)) {
      qCWarning(lcEntityScrape) << "entity fetch for" << job.collectionName
                                << "hit provider quota (HTTP" << err.httpStatus
                                << ") — stopping the queue with a resume point";
      m_svc->stopForQuotaExhaustion();
      return;
    }
    // Kartend-e8aag: a not-found entity (e.g. a niche platform with no catalog
    // entry) is counted apart from genuine errors.
    if (err.code == ErrorUtils::ErrorCode::RemoteResourceNotFound || err.httpStatus == 404) {
      ++m_svc->m_summary.notFound;
    } else {
      ++m_svc->m_summary.errors;
      if (m_svc->m_summary.firstFailures.size() < kMaxReportedFailures) {
        m_svc->m_summary.firstFailures.append(
            QStringLiteral("%1: %2").arg(job.collectionName, err.userFacingSummary()));
      }
      // Kartend-jjjo5: an errored entity is re-queueable — tagged as an entity
      // so rescrapeFailedItems() rebuilds it as an entity job, not a game
      // lookup of the systemeid.
      if (m_svc->m_summary.failedItems.size() < kMaxReportedFailures) {
        m_svc->m_summary.failedItems.append({job.collectionIndex, job.entity.identity,
                                             job.collectionUuid, /*isEntity=*/true, job.entity});
      }
    }
    finishEntityItem();
    return;
  }
  const Scraper::ScrapedItem &item = result.value();
  m_svc->m_lastScrapedItem = item;
  if (item.media.isEmpty() || !provider) {
    // No art to download (or no provider for the media fetch) — count the
    // metadata-only success and advance.
    ++m_svc->m_summary.scraped;
    emit m_svc->itemCompleted(m_svc->m_itemsCompleted + 1, m_svc->m_totalItemsAtStart, item, {});
    finishEntityItem();
    return;
  }
  // Kartend-ckepd.3: download the platform art, write it to the collection's
  // _shared art dir (MediaScope::Platform routing), then wire it into the
  // collection's config. Async fan-out mirrors BatchScrapeRunner's media
  // aggregator; the generation token rejects callbacks from a cancelled run.
  const Scraper::RescrapeMode rescrapeMode =
      m_svc->m_ctx.generalSettings ? static_cast<Scraper::RescrapeMode>(
                                         m_svc->m_ctx.generalSettings->scraper.options.rescrapeMode)
                                   : Scraper::RescrapeMode::Overwrite;
  const QString artworkDir = job.artworkDir;
  const int collectionIndex = job.collectionIndex;
  // writeMediaFiles short-circuits on an empty baseName; platform assets are
  // _shared-scoped (baseName unused for their path), so any non-empty value
  // works — use the systemeid.
  const QString baseName =
      job.entity.identity.isEmpty() ? QStringLiteral("platform") : job.entity.identity;
  const QString collectionUuid = job.collectionUuid;
  const QString collectionName = job.collectionName;
  const QString entityIdentity = job.entity.identity;
  // Full entity target so an all-media-failed job re-queues AS an entity (the
  // identity string alone can't rebuild the job — needs type + collectionIndex).
  const Scraper::EntityScrapeTarget entityTarget = job.entity;
  struct MediaAgg {
    int pending = 0;
    QList<Scraper::PendingMediaWrite> writes;
    /// Fetches that errored or came back empty — a job whose fetches ALL land
    /// here is an errored entity, not a zero-art success (Kartend-xzqel).
    int failures = 0;
    QString firstFailureSummary;
    /// Any provider-classified quota failure — stops the queue with a
    /// resume point once the fan-out settles.
    bool quotaHit = false;
  };
  auto agg = std::make_shared<MediaAgg>();
  agg->pending = static_cast<int>(item.media.size());
  QPointer<ScraperService> self(m_svc);
  for (const auto &asset : item.media) {
    provider->fetchMediaBytes(asset.url, [self, provider, generation, item, asset, agg, artworkDir,
                                          collectionIndex, collectionUuid, collectionName,
                                          entityIdentity, entityTarget, baseName,
                                          rescrapeMode](const ErrorUtils::Result<QByteArray> &r) {
      if (self.isNull() || self->m_runGeneration != generation) return;
      if (r.isOk() && !r.value().isEmpty()) {
        Scraper::PendingMediaWrite w;
        w.asset = asset;
        w.bytes = r.value();
        agg->writes.append(w);
      } else {
        // A dropped fetch must be visible in the aggregate, not silently
        // swallowed — otherwise an all-failed job (auth error, quota,
        // wrong endpoint params) reports success with zero art.
        ++agg->failures;
        if (r.isError()) {
          const auto &err = r.error();
          if (agg->firstFailureSummary.isEmpty()) {
            agg->firstFailureSummary = err.userFacingSummary();
          }
          if (provider && provider->isQuotaExhausted(err)) {
            agg->quotaHit = true;
          }
        } else if (agg->firstFailureSummary.isEmpty()) {
          agg->firstFailureSummary = QStringLiteral("empty media response");
        }
      }
      if (--agg->pending > 0) return;
      // Last asset out — settle the job.
      if (self->m_state != ScraperService::State::RunningAuto &&
          self->m_state != ScraperService::State::RunningInteractive)
        return;
      if (self->m_queueCursor >= self->m_queue.size()) return;
      if (agg->quotaHit) {
        // Unlike a game item (whose metadata/DB row already landed, so the
        // runner keeps partial assets), an entity job is atomic and cheap
        // to retry — one fetch. Leave it queued as the resume point rather
        // than consuming it with whatever art beat the quota to the door.
        qCWarning(lcEntityScrape) << "platform media fetch for" << collectionName
                                  << "hit provider quota — stopping the queue with a resume point";
        self->stopForQuotaExhaustion();
        return;
      }
      if (agg->writes.isEmpty() && agg->failures > 0) {
        // Every media fetch failed — that's an errored entity (the whole
        // point of a platform scrape is the art), not a success.
        ++self->m_summary.errors;
        if (self->m_summary.firstFailures.size() < kMaxReportedFailures) {
          self->m_summary.firstFailures.append(
              QStringLiteral("%1: platform art download failed: %2")
                  .arg(collectionName, agg->firstFailureSummary));
        }
        if (self->m_summary.failedItems.size() < kMaxReportedFailures) {
          self->m_summary.failedItems.append(
              {collectionIndex, entityIdentity, collectionUuid, /*isEntity=*/true, entityTarget});
        }
        self->m_entityCoordinator.finishEntityItem();
        return;
      }
      if (agg->failures > 0) {
        qCWarning(lcEntityScrape) << agg->failures << "of" << item.media.size()
                                  << "platform media fetches failed for" << collectionName
                                  << "— writing the assets that succeeded";
      }
      // Write off the GUI thread (Kartend-blfub); the watcher continuation
      // books the summary, wires the config, and advances the queue.
      self->m_entityCoordinator.dispatchEntityMediaWrite(item, collectionUuid, collectionIndex,
                                                         artworkDir, baseName, agg->writes,
                                                         rescrapeMode, generation);
    });
  }
}

void EntityScrapeCoordinator::dispatchEntityMediaWrite(
    const Scraper::ScrapedItem &item, const QString &collectionUuid, int collectionIndex,
    const QString &artworkDir, const QString &baseName,
    const QList<Scraper::PendingMediaWrite> &writes, Scraper::RescrapeMode rescrapeMode,
    quint64 generation) {
  // Mirror BatchScrapeRunner's file-I/O phase: writeMediaFiles (write + fsync
  // inside atomicWriteFile) runs on the global QThreadPool so slow/wedged/
  // network storage can't freeze the window mid-scrape, and the shared cancel
  // token lets cancel() interrupt the fan-out between assets (Kartend-blfub).
  auto *watcher = new QFutureWatcher<Scraper::MediaWriteResult>(m_svc);
  QPointer<ScraperService> self(m_svc);
  // Step watchdog (same budget + env knob as the runner's): a write blocked in
  // an uninterruptible syscall on a wedged mount would otherwise park the
  // queue forever — the watcher's finished would simply never fire. Whichever
  // of watchdog/completion fires first wins; the other no-ops via writeDone.
  auto writeDone = std::make_shared<bool>(false);
  auto *watchdog = new QTimer(m_svc);
  watchdog->setSingleShot(true);
  QObject::connect(watchdog, &QTimer::timeout, m_svc, [self, watchdog, writeDone, generation]() {
    watchdog->deleteLater();
    if (self.isNull() || *writeDone) return;
    *writeDone = true;
    if (self->m_runGeneration != generation) return;
    if (self->m_state != ScraperService::State::RunningAuto &&
        self->m_state != ScraperService::State::RunningInteractive)
      return;
    if (self->m_queueCursor >= self->m_queue.size()) return;
    const auto &job = self->m_queue[self->m_queueCursor];
    qCWarning(lcEntityScrape) << "platform art write for" << job.collectionName << "exceeded"
                              << BatchScrapeRunner::stepWatchdogMs()
                              << "ms; erroring the item and advancing (storage unresponsive?)";
    ++self->m_summary.errors;
    if (self->m_summary.firstFailures.size() < kMaxReportedFailures) {
      self->m_summary.firstFailures.append(
          QStringLiteral("%1: platform art write timed out (storage unresponsive?)")
              .arg(job.collectionName));
    }
    if (self->m_summary.failedItems.size() < kMaxReportedFailures) {
      self->m_summary.failedItems.append({job.collectionIndex, job.entity.identity,
                                          job.collectionUuid, /*isEntity=*/true, job.entity});
    }
    self->m_entityCoordinator.finishEntityItem();
  });
  QObject::connect(watcher, &QFutureWatcher<Scraper::MediaWriteResult>::finished, m_svc,
                   [self, watcher, writeDone, generation, item, collectionUuid, collectionIndex]() {
                     watcher->deleteLater();
                     if (self.isNull() || *writeDone) return;
                     *writeDone = true;
                     if (self->m_runGeneration != generation) return;
                     self->m_entityCoordinator.onEntityMediaWriteFinished(
                         item, collectionUuid, collectionIndex, watcher->result());
                   });
  // Prune settled futures so the drain list stays O(in-flight).
  m_svc->m_inFlightEntityWrites.removeIf(
      [](const QFuture<Scraper::MediaWriteResult> &f) { return f.isFinished(); });
  const QFuture<Scraper::MediaWriteResult> writeFuture = QtConcurrent::run(
      [artworkDir, baseName, writes, rescrapeMode, cancel = m_svc->m_entityWriteCancel]() {
        // Cancelled while queued (pool saturated): skip the writes entirely.
        if (cancel->load(std::memory_order_acquire)) return Scraper::MediaWriteResult{};
        return Scraper::writeMediaFiles(artworkDir, baseName, writes, rescrapeMode, cancel);
      });
  m_svc->m_inFlightEntityWrites.append(writeFuture);
  watcher->setFuture(writeFuture);
  watchdog->start(BatchScrapeRunner::stepWatchdogMs());
}

void EntityScrapeCoordinator::onEntityMediaWriteFinished(const Scraper::ScrapedItem &item,
                                                         const QString &collectionUuid,
                                                         int collectionIndex,
                                                         const Scraper::MediaWriteResult &res) {
  if (m_svc->m_state != ScraperService::State::RunningAuto &&
      m_svc->m_state != ScraperService::State::RunningInteractive)
    return;
  if (m_svc->m_queueCursor >= m_svc->m_queue.size()) return;
  m_svc->m_summary.mediaWritten += res.mediaWritten;
  applyEntityArtToConfig(collectionUuid, collectionIndex, item.media, res.writtenPaths);
  ++m_svc->m_summary.scraped;
  emit m_svc->itemCompleted(m_svc->m_itemsCompleted + 1, m_svc->m_totalItemsAtStart, item,
                            res.writtenPaths);
  finishEntityItem();
}

void EntityScrapeCoordinator::applyEntityArtToConfig(const QString &collectionUuid,
                                                     int collectionIndex,
                                                     const QList<Scraper::MediaAsset> &assets,
                                                     const QStringList &writtenPaths) {
  if (!m_svc->m_ctx.collections) return;
  // Defensive re-resolution (Kartend-8zd3q): the index was captured when the
  // job dispatched, but the collections list can change under a live run
  // (settings dialog, playlist resync). Verify the collection at the index
  // still carries the job's UUID; on mismatch re-resolve by UUID, and refuse
  // to write if it no longer resolves — never mutate another collection's
  // config. Legacy jobs without a UUID keep the index-only bounds check.
  if (!collectionUuid.isEmpty()) {
    const bool indexStillMatches =
        collectionIndex >= 0 && collectionIndex < m_svc->m_ctx.collections->size() &&
        CollectionUtils::computeCollectionUuid((*m_svc->m_ctx.collections)[collectionIndex]) ==
            collectionUuid;
    if (!indexStillMatches) {
      collectionIndex = CollectionUtils::indexForUuid(*m_svc->m_ctx.collections, collectionUuid);
      if (collectionIndex < 0) {
        qCWarning(lcEntityScrape)
            << "Not applying scraped platform art: the target collection no longer resolves";
        return;
      }
    }
  }
  if (collectionIndex < 0 || collectionIndex >= m_svc->m_ctx.collections->size()) {
    return;
  }
  // Resolve a written path by the `_shared/<type>/` segment the router wrote it
  // under — anchored on the full segment so a parent artworkDir component can't
  // false-match, and queried by explicit type so the result is deterministic
  // regardless of network-completion order (Kartend-ckepd.3 review).
  const auto pathForType = [&writtenPaths](const QString &type) -> QString {
    const QString seg = QStringLiteral("/_shared/") + type + QStringLiteral("/");
    for (const QString &p : writtenPaths) {
      if (p.contains(seg)) return p;
    }
    return {};
  };
  // Pick per config slot via the provider-declared role/priority on each
  // asset (EntityArtRole): the coordinator carries no provider-specific
  // type-string vocabulary — a second entity-capable provider tags its own
  // assets instead of mimicking another provider's canonical names. Lower
  // entityRolePriority wins within a role; assets whose file never landed
  // (fetch miss, write skip) are passed over for the next-best candidate.
  const auto pathForRole = [&](Scraper::EntityArtRole role) -> QString {
    QString best;
    int bestPriority = std::numeric_limits<int>::max();
    for (const Scraper::MediaAsset &asset : assets) {
      if (asset.entityRole != role || asset.entityRolePriority >= bestPriority) continue;
      const QString p = pathForType(asset.type);
      if (!p.isEmpty()) {
        best = p;
        bestPriority = asset.entityRolePriority;
      }
    }
    return best;
  };
  const QString logo = pathForRole(Scraper::EntityArtRole::Logo);
  const QString background = pathForRole(Scraper::EntityArtRole::Background);

  CollectionConfig &cfg = (*m_svc->m_ctx.collections)[collectionIndex];
  bool changed = false;
  if (!logo.isEmpty()) {
    cfg.background.headerLogoImage = logo;
    cfg.collectionIcon = logo;
    changed = true;
  }
  if (!background.isEmpty()) {
    cfg.background.backgroundImage = background;
    changed = true;
  }
  if (!changed) return;
  // Persist through the settings manager so the INI write keeps its atomic
  // sync + 0600 hardening and fires the hot-reload signals (config-write map).
  if (m_svc->m_ctx.ctx) {
    if (auto *sm = m_svc->m_ctx.ctx->settingsManager()) {
      const auto r = sm->saveCollections(*m_svc->m_ctx.collections);
      if (r.isError()) {
        qCWarning(lcEntityScrape) << "Failed to persist scraped platform art to config:"
                                  << r.error().message;
      }
    }
  }
}

void EntityScrapeCoordinator::finishEntityItem() {
  ++m_svc->m_itemsCompleted;
  ++m_svc->m_queueCursor;
  m_svc->schedulePersist();
  m_svc->pump();
}

} // namespace Scraper
