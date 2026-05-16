// Async worker pool that drives a batch scrape over one collection.
// Per-item: provider.lookup → provider.fetchDetail (auto-pick first
// candidate) → optional fetchMediaBytes for the primary cover →
// Scraper::applyScrapedItem. Multiple items run through this chain in
// parallel when itemConcurrency > 1; per-provider rate limits are
// honoured by the underlying Scraper::HttpClient throttle, so high
// item concurrency just stacks more work behind the per-host gate.
//
// Threading: the runner itself lives on the main thread (HTTP provider
// callbacks fire there). Per-item DB writes are dispatched to a
// dedicated ScrapeWriteWorker on its own QThread so the SQLite save
// doesn't tax the UI event loop. File I/O continues to run on the
// global QThreadPool via QtConcurrent::run.
#include "batchscraperunner.h"

#include <utility>

#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QTimer>

#include "idatabasemanager.h"
#include "loggingcategories.h"
#include "scrapepersistence.h"
#include "scrapewriteworker.h"

namespace Scraper {

namespace {
/// Bounded join for the write worker thread. Ample for the worst case
/// (a single SQLite save on a busy disk) without risking a hang at
/// shutdown if the disk is wedged — we'd rather leak the thread than
/// block the UI on close. Mirrors DatabaseManager's SHUTDOWN_WAIT_MS
/// budget for the same reason.
constexpr int kWriteWorkerShutdownWaitMs = 2000;
} // namespace

BatchScrapeRunner::BatchScrapeRunner(IDatabaseManager *db,
                                     std::shared_ptr<MetadataLookupProvider> provider,
                                     QString collectionUuid, QStringList paths, QString artworkDir,
                                     bool fetchPrimaryCover, Scraper::RescrapeMode rescrapeMode,
                                     int itemConcurrency, QObject *parent)
    : QObject(parent), m_db(db), m_provider(std::move(provider)),
      m_collectionUuid(std::move(collectionUuid)), m_paths(std::move(paths)),
      m_artworkDir(std::move(artworkDir)), m_fetchPrimaryCover(fetchPrimaryCover),
      m_rescrapeMode(rescrapeMode), m_itemConcurrency(std::max(1, itemConcurrency)) {}

BatchScrapeRunner::~BatchScrapeRunner() {
  shutdownWriteWorker();
}

void BatchScrapeRunner::start() {
  // Spin up the write worker eagerly. We could lazy-start it on the
  // first applyAndFinish, but doing it here pulls the connection-open
  // cost into the start() tick (off-screen) instead of paying it at
  // the first item completion (where it would interleave with the
  // user-visible "first item scraped" event).
  ensureWriteWorkerStarted();

  // Always defer first work to the next event-loop tick. Callers
  // typically wire up signal handlers right after constructing the
  // runner and calling start(); emitting `finished` synchronously
  // here (in the no-provider / empty-list cases) would beat the
  // caller's connect and silently swallow the result.
  QTimer::singleShot(0, this, [this]() {
    if (!m_provider) {
      m_summary.firstFailures.append(QStringLiteral("No metadata provider configured."));
      if (!m_finishedEmitted) {
        m_finishedEmitted = true;
        emit finished(m_summary);
      }
      return;
    }
    // filterAlreadyScraped is the batch-level expression of the
    // `Skip` re-scrape mode (drop items that already have metadata).
    // For Overwrite / FillMissing / UpdateChanged we want every item
    // to flow through so the per-asset persistence gate inside
    // applyScrapedItem can decide what to actually write.
    if (m_rescrapeMode == Scraper::RescrapeMode::Skip) {
      filterAlreadyScraped();
    }
    pump();
  });
}

void BatchScrapeRunner::ensureWriteWorkerStarted() {
  // Null-DB carve-out: the test fixture and a few "metadata-only / no
  // persistence" callers pass nullptr. In that mode the runner
  // historically just no-op'd the save and treated it as success — we
  // preserve that by skipping the worker entirely. applyAndFinish has
  // a matching null-worker branch that runs the completion synchronously.
  if (!m_db) return;
  if (m_writeWorker) return;

  // Connection name keyed on the runner address so concurrent runners
  // (e.g. a stale dialog session that overlapped with a fresh start)
  // don't collide in Qt's global QSqlDatabase registry.
  const QString connectionName =
      QStringLiteral("kartend_scrape_writer_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);

  // Threads aren't parented to the runner — same rationale as
  // DatabaseManager's worker threads (~QObject would auto-delete a
  // child thread mid-run, and ~QThread qFatals on a still-running
  // thread). shutdownWriteWorker() handles the bounded join.
  m_writeThread = new QThread();
  m_writeThread->setObjectName(QStringLiteral("ScrapeWriteWorker"));
  m_writeWorker = new ScrapeWriteWorker(connectionName);
  m_writeWorker->moveToThread(m_writeThread);
  // Worker self-destructs when its thread exits, so we don't have to
  // delete it explicitly on the main thread (which would race with
  // any in-flight queued performWrite slot dispatch).
  QObject::connect(m_writeThread, &QThread::finished, m_writeWorker, &QObject::deleteLater);
  // writeCompleted crosses the worker→main thread boundary as a
  // queued signal (Qt::AutoConnection auto-detects the thread
  // mismatch). The slot reads m_pendingWrites + ticks counters on
  // the main thread, which keeps all runner-state mutation single-
  // threaded.
  QObject::connect(m_writeWorker, &ScrapeWriteWorker::writeCompleted, this,
                   &BatchScrapeRunner::onWriteCompleted);

  m_writeThread->start();
  // Open the SQLite connection ON the worker thread (Qt SQL pins a
  // connection to its opening thread). Queued invocation guarantees
  // the slot runs in the worker's event loop, even though we're
  // calling from main.
  QMetaObject::invokeMethod(m_writeWorker, &ScrapeWriteWorker::openConnection,
                            Qt::QueuedConnection);
  m_writeWorkerShutdownDone = false;
}

void BatchScrapeRunner::shutdownWriteWorker() {
  if (m_writeWorkerShutdownDone) return;
  m_writeWorkerShutdownDone = true;

  if (m_writeWorker && m_writeThread) {
    // Queue the close so it runs on the worker thread (closing a
    // QSqlDatabase from a thread other than its owner is undefined).
    QMetaObject::invokeMethod(m_writeWorker, &ScrapeWriteWorker::closeConnection,
                              Qt::QueuedConnection);
  }
  if (m_writeThread) {
    m_writeThread->quit();
    if (m_writeThread->wait(kWriteWorkerShutdownWaitMs)) {
      delete m_writeThread;
    }
    // If wait() timed out the thread is intentionally leaked (and the
    // worker with it via its parent-thread association). Same trade
    // DatabaseManager makes — better than a UI hang on shutdown.
    m_writeThread = nullptr;
    m_writeWorker = nullptr;
  }
}

void BatchScrapeRunner::cancel() {
  m_cancelled = true;
  // Don't emit finished here — let the in-flight callbacks observe the
  // flag and drain. The last completing callback emits finished()
  // once m_inFlight returns to 0, which avoids a double-emit if any
  // chain happens to land synchronously while cancel() is running.
  // The write worker is left running so any in-flight writeCompleted
  // signals can still be received and the per-item slots drained
  // cleanly; the destructor handles the eventual thread shutdown.
}

void BatchScrapeRunner::filterAlreadyScraped() {
  if (!m_db || m_collectionUuid.isEmpty()) return;
  QStringList kept;
  kept.reserve(m_paths.size());
  for (const QString &path : m_paths) {
    const auto md = m_db->loadItemMetadata(m_collectionUuid, path);
    if (md.source.isEmpty()) {
      kept.append(path);
    }
  }
  m_paths = std::move(kept);
}

void BatchScrapeRunner::pump() {
  if (m_cancelled) {
    if (m_inFlight == 0 && !m_finishedEmitted) {
      m_finishedEmitted = true;
      emit finished(m_summary);
    }
    return;
  }
  // Fill empty slots until either the queue is exhausted or we've hit
  // the per-batch concurrency cap. Each new slot starts its own
  // independent lookup → detail → apply chain.
  while (m_inFlight < m_itemConcurrency && m_queueCursor < m_paths.size()) {
    auto state = std::make_shared<ItemState>();
    state->path = m_paths[m_queueCursor];
    state->queueIndex = m_queueCursor;
    ++m_queueCursor;
    ++m_inFlight;
    startItem(state);
  }
  // Queue drained and nothing in flight → done.
  if (m_inFlight == 0 && m_queueCursor >= m_paths.size() && !m_finishedEmitted) {
    m_finishedEmitted = true;
    emit finished(m_summary);
  }
}

void BatchScrapeRunner::startItem(std::shared_ptr<ItemState> state) {
  // Emit progress BEFORE the network call so the UI shows
  // "Scraping <name>" while the request is in flight.
  emit progress(m_summary.scraped + m_summary.skipped + m_summary.errors,
                static_cast<int>(m_paths.size()), QFileInfo(state->path).fileName());

  const QString query = QFileInfo(state->path).completeBaseName();
  MetadataLookupProvider::LookupContext ctx{query, state->path};
  // QPointer guard: each per-item chain can outlive the runner if the
  // caller deletes us after cancel(). The lambda checks the pointer
  // before touching member state.
  QPointer<BatchScrapeRunner> self(this);
  m_provider->lookup(ctx, [self,
                           state](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
    if (self.isNull()) return;
    if (self->m_cancelled) {
      self->itemFinished();
      return;
    }
    if (result.isError()) {
      self->recordError(
          QStringLiteral("%1: %2").arg(QFileInfo(state->path).fileName(), result.error().message));
      return;
    }
    const auto candidates = result.value();
    if (candidates.isEmpty()) {
      ++self->m_summary.skipped;
      self->itemFinished();
      return;
    }
    // Auto-pick the first candidate. The provider ranks candidates
    // by relevance, so the first one is what an interactive scrape
    // would default to.
    // NOTE on shared provider state: ScreenScraperProvider keeps a
    // single-entry detail cache (m_lastDetail). fetchDetail reads
    // it synchronously, so the cache survives interleaving — the
    // lookup-callback writes m_lastDetail and immediately calls
    // fetchDetail which reads it, all before the Qt event loop
    // can dispatch another item's lookup-completion. Safe so long
    // as the provider's lookup callback is the one that calls
    // fetchDetail (which it is, here).
    self->m_provider->fetchDetail(
        candidates.first(), [self, state](ErrorUtils::Result<Scraper::ScrapedItem> result) {
          if (self.isNull()) return;
          if (self->m_cancelled) {
            self->itemFinished();
            return;
          }
          if (result.isError()) {
            self->recordError(QStringLiteral("%1: %2").arg(QFileInfo(state->path).fileName(),
                                                           result.error().message));
            return;
          }
          const auto scraped = result.value();
          // Media-fetch pass. Two modes depending on whether the
          // user supplied an explicit media-type filter:
          //   • filter empty → legacy "front cover only" path
          //     (gated by m_fetchPrimaryCover). Backwards-compat
          //     for callers that pre-date the per-type checkbox UI.
          //   • filter non-empty → fetch every asset whose `type`
          //     matches one of the requested types, in parallel.
          //     Pending-fetch count tracked on a shared aggregator
          //     so the final applyScrapedItem fires exactly once
          //     per item, with all successful bytes attached.
          if (!self->m_fetchPrimaryCover) {
            self->applyAndFinish(state, scraped, {});
            return;
          }
          const bool useFilter = !self->m_mediaTypeFilter.isEmpty();
          QList<Scraper::MediaAsset> wantedAssets;
          for (const Scraper::MediaAsset &m : scraped.media) {
            if (!m.url.isValid()) continue;
            if (useFilter) {
              if (self->m_mediaTypeFilter.contains(m.type.toLower())) {
                wantedAssets.append(m);
              }
            } else if (m.type.compare(QStringLiteral("front"), Qt::CaseInsensitive) == 0) {
              wantedAssets.append(m);
              break; // legacy path: at most one cover per item.
            }
          }
          if (wantedAssets.isEmpty()) {
            self->applyAndFinish(state, scraped, {});
            return;
          }
          // Shared aggregator: every parallel fetch decrements
          // `pending` on completion; the last one out commits.
          // shared_ptr because the lambdas outlive any one fetch.
          struct Aggregator {
            int pending = 0;
            QList<Scraper::PendingMediaWrite> writes;
          };
          auto agg = std::make_shared<Aggregator>();
          agg->pending = wantedAssets.size();
          for (const auto &asset : wantedAssets) {
            self->m_provider->fetchMediaBytes(
                asset.url, [self, state, scraped, asset, agg](ErrorUtils::Result<QByteArray> r) {
                  if (self.isNull()) return;
                  if (self->m_cancelled) {
                    if (--agg->pending == 0) {
                      self->itemFinished();
                    }
                    return;
                  }
                  if (r.isOk() && !r.value().isEmpty()) {
                    // Track byte count regardless of write success
                    // — the user's bandwidth was already spent.
                    self->m_totalBytesDownloaded += r.value().size();
                    Scraper::PendingMediaWrite w;
                    w.asset = asset;
                    w.bytes = r.value();
                    agg->writes.append(w);
                  }
                  // Asset fetch failures are non-fatal — partial
                  // success is better than failing the whole item
                  // because one 404'd asset.
                  if (--agg->pending == 0) {
                    self->applyAndFinish(state, scraped, agg->writes);
                  }
                });
          }
        });
  });
}

void BatchScrapeRunner::applyAndFinish(std::shared_ptr<ItemState> state,
                                       const Scraper::ScrapedItem &scraped,
                                       const QList<Scraper::PendingMediaWrite> &writes) {
  if (m_cancelled) {
    itemFinished();
    return;
  }
  const QString baseName = QFileInfo(state->path).completeBaseName();
  // Strip text fields when the user opted out of metadata writes —
  // the persistence layer keeps any existing DB values when the
  // scraped fields are empty (pickNonEmpty in scrapepersistence.cpp).
  // Custom fields collapse to empty so the merge becomes a no-op.
  // sourceProviderId is also cleared so the item's `source` column
  // doesn't get overwritten to attribute the (skipped) text scrape.
  Scraper::ScrapedItem effective = scraped;
  if (!m_writeMetadata) {
    effective.title.clear();
    effective.description.clear();
    effective.genre.clear();
    effective.developer.clear();
    effective.publisher.clear();
    effective.releaseDate.clear();
    effective.contentRating.clear();
    effective.players.clear();
    effective.tagsJson.clear();
    effective.customFields.clear();
    effective.sourceProviderId.clear();
    effective.runtimeSeconds = -1;
  }

  // ── File-I/O phase (QThreadPool) ──────────────────────────────
  // Artwork writes + the existence probes / byte comparisons inside
  // writeMediaFiles' rescrape gate run on the global QThreadPool so
  // the main UI thread doesn't stall once per item. The result hops
  // back to the main thread via the watcher's finished slot — from
  // there the DB save is dispatched to the dedicated ScrapeWriteWorker
  // thread (which owns its own QSqlDatabase connection), and the
  // per-item state machine resumes when the worker queues the
  // writeCompleted signal back here.
  auto *watcher = new QFutureWatcher<Scraper::MediaWriteResult>(this);
  QPointer<BatchScrapeRunner> self(this);
  connect(
      watcher, &QFutureWatcher<Scraper::MediaWriteResult>::finished, this,
      [self, watcher, state, effective, baseName]() {
        watcher->deleteLater();
        if (self.isNull()) return;
        if (self->m_cancelled) {
          self->itemFinished();
          return;
        }
        const Scraper::MediaWriteResult writeRes = watcher->result();

        // Probe for an existing primary-cover file on disk and append
        // it to the thumbnail-strip paths when no fresh write
        // happened — FillMissing / UpdateChanged commonly skip
        // writing if the file already exists, but the user still
        // wants a visual ping for each item. Probes the canonical
        // subdirs the persistence layer can use (top-level mirror,
        // /front/, /covers/, /box-2D/, /screenshot/).
        QStringList thumbPaths = writeRes.writtenPaths;
        if (thumbPaths.isEmpty() && !self->m_artworkDir.isEmpty()) {
          static const QStringList kProbeDirs = {QString(), QStringLiteral("front"),
                                                 QStringLiteral("covers"), QStringLiteral("box-2D"),
                                                 QStringLiteral("screenshot")};
          static const QStringList kProbeExts = {QStringLiteral("png"), QStringLiteral("jpg"),
                                                 QStringLiteral("jpeg"), QStringLiteral("webp")};
          for (const QString &dir : kProbeDirs) {
            QString prefix = self->m_artworkDir;
            if (!dir.isEmpty()) prefix += QLatin1Char('/') + dir;
            prefix += QLatin1Char('/') + baseName + QLatin1Char('.');
            bool found = false;
            for (const QString &ext : kProbeExts) {
              const QString p = prefix + ext;
              if (QFileInfo::exists(p)) {
                thumbPaths.append(p);
                found = true;
                break;
              }
            }
            if (found) break;
          }
        }

        // ── DB-write phase: dispatch ──────────────────────
        // Stash everything onWriteCompleted needs (the original
        // ItemState for cancellation/error context, the effective
        // ScrapedItem for the itemCompleted signal payload, the
        // thumbPaths the dialog renders) keyed on a fresh
        // requestId. The worker emits writeCompleted(requestId,
        // ok) once the SQLite save returns.
        //
        // Null-DB carve-out: the test fixture and a few
        // metadata-only callers pass nullptr. With no worker thread
        // we keep the legacy synchronous "treat as success" path so
        // existing tests keep passing.
        if (!self->m_db || !self->m_writeWorker) {
          ++self->m_summary.scraped;
          self->m_summary.mediaWritten += writeRes.mediaWritten;
          emit self->itemCompleted(self->m_summary.scraped + self->m_summary.skipped +
                                       self->m_summary.errors,
                                   static_cast<int>(self->m_paths.size()), effective, thumbPaths);
          self->itemFinished();
          return;
        }

        const quint64 requestId = ++self->m_nextWriteId;
        PendingWrite pending;
        pending.state = state;
        pending.scraped = effective;
        pending.writtenPaths = thumbPaths;
        pending.mediaWritten = writeRes.mediaWritten;
        pending.baseName = baseName;
        if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
          pending.dispatchedAtMs = QDateTime::currentMSecsSinceEpoch();
        }
        self->m_pendingWrites.insert(requestId, std::move(pending));

        // Queued cross-thread invocation. The worker handles the
        // load → merge → save against its own connection and
        // queues the writeCompleted signal back to the runner's
        // (main) thread.
        QMetaObject::invokeMethod(
            self->m_writeWorker, "performWrite", Qt::QueuedConnection, Q_ARG(quint64, requestId),
            Q_ARG(QString, self->m_collectionUuid), Q_ARG(QString, state->path),
            Q_ARG(Scraper::ScrapedItem, effective),
            Q_ARG(Scraper::NonStandardArtworkList, writeRes.nonStandardArtwork));
      });
  watcher->setFuture(QtConcurrent::run(
      [artworkDir = m_artworkDir, baseName, writes, effective, rescrapeMode = m_rescrapeMode]() {
        // Human-readable JSON sidecar alongside the artwork. `effective`
        // is blank when the user opted out of metadata, so the sidecar
        // helper self-skips in that case.
        (void)Scraper::writeMetadataSidecar(artworkDir, baseName, effective, rescrapeMode);
        return Scraper::writeMediaFiles(artworkDir, baseName, writes, rescrapeMode);
      }));
}

void BatchScrapeRunner::onWriteCompleted(quint64 requestId, bool ok) {
  // Look up the pending row before doing anything else — a stray
  // duplicate emit (shouldn't happen, but defensive) would otherwise
  // double-tick counters.
  auto it = m_pendingWrites.find(requestId);
  if (it == m_pendingWrites.end()) return;
  PendingWrite pending = std::move(it.value());
  m_pendingWrites.erase(it);

  if (m_cancelled) {
    // Cancellation may have raced ahead of the worker reply. Don't
    // emit per-item completion; just drain the in-flight slot so the
    // queue can wind down.
    itemFinished();
    return;
  }

  if (!ok) {
    recordError(QStringLiteral("%1: metadata save failed").arg(pending.baseName));
    return;
  }

  // Worker bypassed DatabaseManager's main-thread cache. Invalidate
  // the per-item slot here so the next sidebar refresh sees the
  // freshly-saved metadata instead of the pre-scrape cached row.
  if (m_db) {
    m_db->invalidateMetadataCacheItem(m_collectionUuid, pending.state->path);
  }

  ++m_summary.scraped;
  m_summary.mediaWritten += pending.mediaWritten;

  if (pending.dispatchedAtMs > 0) {
    // Perf trace: dispatch→writeCompleted latency = the window during
    // which the user's main thread is unblocked (vs the synchronous
    // pre-gnkb path where every per-item save blocked the UI for the
    // SQLite write duration). Drives Kartend-5vwt item 3 data.
    const qint64 latencyMs = QDateTime::currentMSecsSinceEpoch() - pending.dispatchedAtMs;
    qCDebug(lcPerfTrace).nospace() << "BatchScrapeRunner write: latencyMs=" << latencyMs
                                   << " requestId=" << requestId << " base=" << pending.baseName;
  }

  // Notify observers (ScraperService → dialog Live view) of the
  // freshly-written (or existing fallback) paths.
  emit itemCompleted(m_summary.scraped + m_summary.skipped + m_summary.errors,
                     static_cast<int>(m_paths.size()), pending.scraped, pending.writtenPaths);
  itemFinished();
}

void BatchScrapeRunner::recordError(const QString &reason) {
  ++m_summary.errors;
  if (m_summary.firstFailures.size() < 5) {
    m_summary.firstFailures.append(reason);
  }
  itemFinished();
}

void BatchScrapeRunner::itemFinished() {
  // One slot freed up. Drop the in-flight count and either start
  // another item from the queue (steady state) or emit `finished` if
  // the queue is drained AND no items are left in flight.
  --m_inFlight;
  pump();
}

} // namespace Scraper
