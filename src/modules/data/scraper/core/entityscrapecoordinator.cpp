// Entity-scrape execution engine, split out of scraperservice.cpp (see the
// class doc in the header for the friend + back-pointer rationale). Bodies
// moved verbatim; service state is reached through m_svc.
#include "entityscrapecoordinator.h"

#include "entitymetadata.h"
#include "idatabasemanager.h"
#include "itemmetadata.h"
#include <algorithm>

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
#include "pathutils.h"
#include "scraperservice.h"
#include "screenscrapercompanyregistry.h"
#include "wikidatalogoprovider.h"

namespace Scraper {

namespace {
Q_LOGGING_CATEGORY(lcEntityScrape, "kartend.scraperservice.entity", QtWarningMsg)

/// The Wikidata/Wikipedia collection-data provider for @p collectionIndex —
/// via the injected builder when the context carries one (tests), else
/// constructed inline (production). Takes the context by reference: this
/// free function is not the service's friend, the coordinator is — call
/// sites pass m_svc->m_ctx. Kartend-445su.
std::shared_ptr<MetadataLookupProvider>
makeCollectionDataProvider(const ScraperService::Context &ctx, int collectionIndex) {
  if (ctx.collectionDataProviderBuilder) {
    return ctx.collectionDataProviderBuilder(collectionIndex);
  }
  return std::make_shared<WikidataLogoProvider>(
      [collections = ctx.collections, idx = collectionIndex]() -> const CollectionConfig * {
        if (!collections || idx < 0 || idx >= collections->size()) return nullptr;
        return &(*collections)[idx];
      });
}
} // namespace

void EntityScrapeCoordinator::startEntityCollection() {
  auto &job = m_svc->m_queue[m_svc->m_queueCursor];
  qCInfo(lcEntityScrape) << "startEntityCollection idx=" << job.collectionIndex
                         << "name=" << job.collectionName
                         << "entityType=" << static_cast<int>(job.entity.type);
  auto provider =
      m_svc->m_ctx.providerBuilder ? m_svc->m_ctx.providerBuilder(job.collectionIndex) : nullptr;
  // Capability routing (Kartend-445su): a Collection-typed job whose primary
  // provider cannot scrape Collection entities (ScreenScraper is
  // Platform-only) is dispatched to the Wikidata/Wikipedia data provider
  // instead of dead-ending in the provider's InvalidArgument branch. This is
  // what makes collection/group data ride along with EVERY entity launch —
  // games collections included — rather than only via the not-found
  // fallback.
  if (provider && job.entity.type == Scraper::ScrapeEntityType::Collection &&
      !provider->supportedEntities().contains(Scraper::ScrapeEntityType::Collection)) {
    provider = makeCollectionDataProvider(m_svc->m_ctx, job.collectionIndex);
  }
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
    const std::shared_ptr<MetadataLookupProvider> &provider, quint64 generation,
    bool wikiFallbackTried) {
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
    // Consecutive-429 escalation (Kartend-jjyst.15): a lone 429 fails just
    // this entity below, but a streak escalates to the same graceful stop as
    // a quota death — the job stays queued as the resume point (an entity
    // retry costs one fetch).
    if (err.httpStatus == 429) {
      if (m_svc->noteRateLimited429()) {
        qCWarning(lcEntityScrape) << "entity fetch for" << job.collectionName
                                  << "hit repeated HTTP 429 rate limits — stopping the queue "
                                     "with a resume point";
        m_svc->stopForQuotaExhaustion();
        return;
      }
    } else {
      m_svc->m_consecutive429Count = 0;
    }
    // Kartend-e8aag: a not-found entity (e.g. a niche platform with no catalog
    // entry) is counted apart from genuine errors.
    if (err.code == ErrorUtils::ErrorCode::RemoteResourceNotFound || err.httpStatus == 404) {
      // Wikidata logo fallback (Kartend-czna3): a Platform entity no system
      // matches is exactly the manufacturer-shell case ("Nintendo" is a
      // company, not a console) — retry the job once through the Wikidata
      // provider with a Collection target before booking the not-found.
      // Guarded by wikiFallbackTried so the fallback's own not-found lands
      // in the bucket instead of recursing.
      if (!wikiFallbackTried && job.entity.type == Scraper::ScrapeEntityType::Platform &&
          m_svc->m_ctx.collections) {
        // Shells frequently carry no artwork directory of their own —
        // substitute the first non-empty root so the logo has a home (the
        // navigation sidebar and the startup matching pass search every
        // collection's root, so any of them serves).
        auto &mutableJob = m_svc->m_queue[m_svc->m_queueCursor];
        if (mutableJob.artworkDir.isEmpty()) {
          for (const CollectionConfig &c : *m_svc->m_ctx.collections) {
            const QString root = PathUtils::validateAndExpandPath(c.artworkDirectory, c.name);
            if (!root.isEmpty()) {
              mutableJob.artworkDir = root;
              break;
            }
          }
        }
        auto fallback = makeCollectionDataProvider(m_svc->m_ctx, job.collectionIndex);
        Scraper::EntityScrapeTarget wikiTarget;
        wikiTarget.type = Scraper::ScrapeEntityType::Collection;
        wikiTarget.identity = job.collectionUuid;
        wikiTarget.collectionIndex = job.collectionIndex;
        qCInfo(lcEntityScrape) << "platform entity not found for" << job.collectionName
                               << "— trying the Wikidata logo fallback";
        QPointer<ScraperService> self(m_svc);
        fallback->fetchEntity(
            wikiTarget, [self, fallback, generation](
                            const ErrorUtils::Result<Scraper::ScrapedItem> &fallbackResult) {
              if (self.isNull() || self->m_runGeneration != generation) return;
              self->m_entityCoordinator.onEntityFetchComplete(fallbackResult, fallback, generation,
                                                              /*wikiFallbackTried=*/true);
            });
        return;
      }
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
  // A delivered entity proves the limiter let us through — it ends any
  // consecutive-429 run (Kartend-jjyst.15).
  m_svc->m_consecutive429Count = 0;
  m_svc->m_lastScrapedItem = item;
  if (item.media.isEmpty() || !provider) {
    // No art to download (or no provider for the media fetch) — count the
    // metadata-only success and advance. The textual result still lands in
    // entity_metadata (Kartend-445su).
    persistEntityMetadata(item, job.entity, job.collectionUuid, /*artPath=*/{});
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
    /// Set when the shared consecutive-429 streak tripped during this
    /// fan-out — same settle-time stop as quotaHit (Kartend-jjyst.15).
    bool rateLimit429Stop = false;
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
        // A delivered asset ends any consecutive-429 run (Kartend-jjyst.15).
        self->m_consecutive429Count = 0;
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
          } else if (err.httpStatus == 429 && self->noteRateLimited429()) {
            // Media CDNs are the realistic 429 source; a streak across the
            // fan-out escalates to the same settle-time stop as a quota hit
            // (Kartend-jjyst.15).
            agg->rateLimit429Stop = true;
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
      if (agg->rateLimit429Stop) {
        // Same reasoning as the quota stop above: the job is atomic and cheap
        // to retry, so leave it queued as the resume point instead of erroring
        // it against a limiter that isn't letting up (Kartend-jjyst.15).
        qCWarning(lcEntityScrape) << "platform media fetches for" << collectionName
                                  << "hit repeated HTTP 429 rate limits — stopping the queue "
                                     "with a resume point";
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
  // Include skip-because-present destinations: a FillMissing/UpdateChanged
  // re-run whose files already exist must still wire them into the config —
  // an empty writtenPaths otherwise left the collection art unset after e.g.
  // a config reset (Kartend-jjyst.5). Written paths first so a fresh write
  // wins when both report the same type.
  const QString logoPath = applyEntityArtToConfig(collectionUuid, collectionIndex, item.media,
                                                  res.writtenPaths + res.existingPaths);
  // The queue has not advanced yet (finishEntityItem below), so the cursor
  // still addresses this job — its entity target types the metadata row.
  persistEntityMetadata(item, m_svc->m_queue[m_svc->m_queueCursor].entity, collectionUuid,
                        logoPath);
  ++m_svc->m_summary.scraped;
  emit m_svc->itemCompleted(m_svc->m_itemsCompleted + 1, m_svc->m_totalItemsAtStart, item,
                            res.writtenPaths);
  finishEntityItem();
}

QString EntityScrapeCoordinator::applyEntityArtToConfig(const QString &collectionUuid,
                                                        int collectionIndex,
                                                        const QList<Scraper::MediaAsset> &assets,
                                                        const QStringList &landedPaths) {
  if (!m_svc->m_ctx.collections) return {};
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
        return {};
      }
    }
  }
  if (collectionIndex < 0 || collectionIndex >= m_svc->m_ctx.collections->size()) {
    return {};
  }
  // Resolve a landed path (written this run, or kept-existing under the
  // rescrape policy) by the `_shared/<type>/` segment the router placed it
  // under — anchored on the full segment so a parent artworkDir component can't
  // false-match, and queried by explicit type so the result is deterministic
  // regardless of network-completion order (Kartend-ckepd.3 review).
  const auto pathForType = [&landedPaths](const QString &type) -> QString {
    const QString seg = QStringLiteral("/_shared/") + type + QStringLiteral("/");
    for (const QString &p : landedPaths) {
      if (p.contains(seg)) return p;
    }
    return {};
  };
  // Pick per config slot via the provider-declared role/priority on each
  // asset (EntityArtRole): the coordinator carries no provider-specific
  // type-string vocabulary — a second entity-capable provider tags its own
  // assets instead of mimicking another provider's canonical names. Lower
  // entityRolePriority wins within a role; assets whose file never landed
  // (fetch miss, write failure) are passed over for the next-best candidate.
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
  // A config slot is scrape-writable only when it is empty or already points
  // at scrape-owned art (a `_shared/` path this coordinator wired on an
  // earlier run). A user-chosen image lives outside `_shared/` and is a
  // deliberate per-collection decision made in the collection settings dialog
  // — entity scrapes now ride along with EVERY collection scrape (user
  // decision 2026-08-17), so overwriting here would silently revert the
  // user's icon on every rescrape. Manual choice wins; the scraped file still
  // lands on disk for the user to pick later. The rescrape policy is not
  // consulted: it governs scraped FILES, and a hand-picked icon is a config
  // choice, not a scraped file.
  const auto scrapeOwned = [](const QString &current) {
    return current.isEmpty() || current.contains(QStringLiteral("/_shared/"));
  };
  bool changed = false;
  if (!logo.isEmpty()) {
    if (scrapeOwned(cfg.background.headerLogoImage)) {
      cfg.background.headerLogoImage = logo;
      changed = true;
    }
    if (scrapeOwned(cfg.collectionIcon)) {
      cfg.collectionIcon = logo;
      changed = true;
    }
  }
  if (!background.isEmpty() && scrapeOwned(cfg.background.backgroundImage)) {
    cfg.background.backgroundImage = background;
    changed = true;
  }
  if (!changed) return logo;
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
  return logo;
}

void EntityScrapeCoordinator::persistEntityMetadata(const Scraper::ScrapedItem &item,
                                                    const Scraper::EntityScrapeTarget &target,
                                                    const QString &collectionUuid,
                                                    const QString &artPath) {
  IDatabaseManager *db = m_svc->m_ctx.ctx ? m_svc->m_ctx.ctx->databaseManager() : nullptr;
  if (!db) return;

  EntityMetadataStore::EntityMetadata meta;
  // Enum → wire string: the column is a persistence format, so the mapping
  // is explicit here rather than trusting enum ordering.
  switch (target.type) {
  case Scraper::ScrapeEntityType::Platform:
    meta.entityType = QLatin1String(EntityMetadataStore::kTypePlatform);
    break;
  case Scraper::ScrapeEntityType::Collection:
    meta.entityType = QLatin1String(EntityMetadataStore::kTypeCollection);
    break;
  case Scraper::ScrapeEntityType::Category:
    meta.entityType = QLatin1String(EntityMetadataStore::kTypeCategory);
    break;
  case Scraper::ScrapeEntityType::Game:
    return; // Game results belong to item_metadata; never write them here.
  }
  meta.entityIdentity = target.identity.isEmpty() ? collectionUuid : target.identity;
  meta.collectionUuid = collectionUuid;
  meta.title = item.title;
  meta.description = item.description;
  meta.artPath = artPath;
  meta.source = item.sourceProviderId;

  // Fold the provider's loose fields plus the typed ones the entity table
  // has no columns for into custom_fields, under the store's well-known
  // keys. Provider-supplied keys win — a provider that explicitly fills
  // "manufacturer" knows better than the developer/publisher heuristic.
  ItemMetadataStore::CustomFieldList fields;
  QStringList keys = item.customFields.keys();
  keys.sort();
  for (const QString &key : keys) {
    fields.append({key, item.customFields.value(key)});
  }
  const auto hasKey = [&fields](const QString &key) {
    return std::any_of(fields.cbegin(), fields.cend(),
                       [&key](const auto &pair) { return pair.first == key; });
  };
  const QString manufacturer = !item.developer.isEmpty() ? item.developer : item.publisher;
  if (!manufacturer.isEmpty() && !hasKey(QLatin1String(EntityMetadataStore::kFieldManufacturer))) {
    fields.append({QLatin1String(EntityMetadataStore::kFieldManufacturer), manufacturer});
  }
  if (!item.releaseDate.isEmpty() &&
      !hasKey(QLatin1String(EntityMetadataStore::kFieldReleaseDate))) {
    fields.append({QLatin1String(EntityMetadataStore::kFieldReleaseDate), item.releaseDate});
  }
  meta.customFields = ItemMetadataStore::serializeCustomFields(fields);

  if (meta.isEmpty()) {
    return; // Nothing textual scraped and no art — don't write noise rows.
  }
  // A metadata-write failure is logged by the manager but never errors the
  // job: the art (the scrape's primary product) already landed.
  if (!db->saveEntityMetadata(meta)) {
    qCWarning(lcEntityScrape) << "entity metadata for" << meta.entityIdentity
                              << "did not persist (see database log)";
  }
}

void EntityScrapeCoordinator::applyManufacturerLogos() {
  if (!m_svc->m_ctx.collections) return;
  // The matching core lives on the registry module so the SAME pass can run
  // at app startup without a service context (see applyToCollections' doc).
  QList<CollectionConfig> &collections = *m_svc->m_ctx.collections;
  const bool changed = ScreenScraperCompanyRegistry::applyToCollections(
      collections, ScreenScraperCompanyRegistry::defaultPath());
  if (!changed) return;
  qCInfo(lcEntityScrape) << "manufacturer logos matched — persisting collection config";
  if (m_svc->m_ctx.ctx) {
    if (auto *sm = m_svc->m_ctx.ctx->settingsManager()) {
      const auto r = sm->saveCollections(collections);
      if (r.isError()) {
        qCWarning(lcEntityScrape) << "Failed to persist matched manufacturer logos:"
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
