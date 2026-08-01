// Sibling TU: ScrapeWriteWorker lifecycle + the DB-write dispatch/completion
// path for BatchScrapeRunner. Lives here: ensureWriteWorkerStarted /
// shutdownWriteWorker (worker thread + connection lifecycle), the
// onMediaWriteFinished continuation that dispatches performWrite to the
// worker (plus its resolveThumbnailPaths helper), and the onWriteCompleted
// slot that receives the worker's queued reply. The core queue pump and the
// per-item lookup/detail chain stay in batchscraperunner.cpp.
#include "batchscraperunner.h"

#include <atomic>

#include <QDateTime>
#include <QFileInfo>
#include <QThread>

#include "idatabasemanager.h"
#include "loggingcategories.h"
#include "scrapewriteworker.h"

namespace Scraper {

namespace {
/// Bounded join for the write worker thread. Ample for the worst case
/// (a single SQLite save on a busy disk) without risking a hang at
/// shutdown if the disk is wedged — we'd rather leak the thread than
/// block the UI on close. Kartend-n5m1c: lowered from 2000ms — this wait
/// runs back-to-back with the media-write drain on the GUI thread, so the
/// two budgets stacked to ~4s of unresponsive window on wedged storage.
/// The teardown is abandon-safe (the leaked thread + connection are reaped
/// at process exit), so a shorter budget costs only a leaked thread in the
/// rare timeout case, not correctness. Capped at ~2s combined now.
constexpr int kWriteWorkerShutdownWaitMs = 1000;
} // namespace

void BatchScrapeRunner::ensureWriteWorkerStarted() {
  // Null-DB carve-out: the test fixture and a few "metadata-only / no
  // persistence" callers pass nullptr. In that mode the runner
  // historically just no-op'd the save and treated it as success — we
  // preserve that by skipping the worker entirely. applyAndFinish has
  // a matching null-worker branch that runs the completion synchronously.
  if (!dbMgr()) return;
  if (m_writeWorker) return;

  // Connection name keyed on a process-global monotonic counter (the
  // g_connectionInstanceId pattern from databasemanager.cpp). It was
  // previously keyed on the runner's heap address — but when
  // shutdownWriteWorker()'s bounded join times out the thread (and its
  // still-registered connection) is intentionally leaked, and a later
  // runner allocated at the SAME address would addDatabase() under the
  // identical name, replacing the leaked thread's in-use connection
  // (Qt-documented UB, Kartend-fux2w). A counter never reuses a name.
  static std::atomic<quint64> s_writerInstanceId{0};
  const QString connectionName =
      QStringLiteral("kartend_scrape_writer_%1")
          .arg(s_writerInstanceId.fetch_add(1, std::memory_order_relaxed));

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

void BatchScrapeRunner::onMediaWriteFinished(const std::shared_ptr<ItemState> &state,
                                             const Scraper::ScrapedItem &effective,
                                             const QString &baseName,
                                             const Scraper::MediaWriteResult &writeRes) {
  if (m_cancelled) {
    itemFinished();
    return;
  }

  // Fold writeMediaFiles' genuine failures (disk full, mkpath failure, unsafe
  // remote-derived path) into the summary — previously they were dropped
  // here, so a disk-full batch reported "scraped N, 0 media" with zero
  // diagnostics (Kartend-jjyst.4). Benign rescrape-policy skips are not in
  // writeFailures, so FillMissing's kept-existing files don't flood the list.
  m_summary.mediaWriteFailures += static_cast<int>(writeRes.writeFailures.size());
  for (const QString &f : writeRes.writeFailures) {
    if (m_summary.firstFailures.size() >= kMaxReportedFailures) break;
    m_summary.firstFailures.append(QStringLiteral("%1: %2").arg(baseName, f));
  }

  const QStringList thumbPaths = resolveThumbnailPaths(baseName, writeRes.writtenPaths);

  // ── DB-write phase: dispatch ──────────────────────
  // Null-DB carve-out: the test fixture and a few metadata-only callers pass
  // nullptr. With no worker thread we keep the legacy synchronous "treat as
  // success" path so existing tests keep passing.
  if (!dbMgr() || !m_writeWorker) {
    ++m_summary.scraped;
    resetFatalStreak();
    m_summary.mediaWritten += writeRes.mediaWritten;
    if (writeRes.sidecarFailed) ++m_summary.sidecarFailures;
    m_remainingPaths.removeOne(state->path);
    emit itemCompleted(m_summary.processedItems(), totalItemCount(), effective, thumbPaths);
    itemFinished();
    return;
  }

  // Stash everything onWriteCompleted needs (the original ItemState for
  // cancellation/error context, the effective ScrapedItem for the
  // itemCompleted payload, the thumbPaths the dialog renders) keyed on a fresh
  // requestId. The worker emits writeCompleted(requestId, ok) once the SQLite
  // save returns.
  const quint64 requestId = ++m_nextWriteId;
  PendingWrite pending;
  pending.state = state;
  pending.scraped = effective;
  pending.writtenPaths = thumbPaths;
  pending.mediaWritten = writeRes.mediaWritten;
  pending.baseName = baseName;
  pending.sidecarFailed = writeRes.sidecarFailed;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    pending.dispatchedAtMs = QDateTime::currentMSecsSinceEpoch();
  }
  // Stall guard for the DB-save leg, mirroring the lookup and artwork-write
  // watchdogs: a performWrite wedged on stalled storage never queues its
  // writeCompleted reply, and without a watchdog this item's slot stayed
  // occupied and the batch hung at <100% forever. On fire, drop the pending
  // row first (the onTimeout hook) and error the item via the shared
  // onStepTimedOut path — a worker reply that limps in later then finds no
  // row and is ignored by onWriteCompleted's unknown-id early return.
  pending.saveWatchdog =
      armStepWatchdog(state, QStringLiteral("database save"),
                      [this, requestId]() { m_pendingWrites.remove(requestId); });
  m_pendingWrites.insert(requestId, pending);

  // Queued cross-thread invocation. The worker handles the load → merge → save
  // against its own connection and queues writeCompleted back to the (main)
  // thread.
  QMetaObject::invokeMethod(m_writeWorker, "performWrite", Qt::QueuedConnection,
                            Q_ARG(quint64, requestId), Q_ARG(QString, m_collectionUuid),
                            Q_ARG(QString, state->path), Q_ARG(Scraper::ScrapedItem, effective),
                            Q_ARG(Scraper::NonStandardArtworkList, writeRes.nonStandardArtwork));
}

QStringList BatchScrapeRunner::resolveThumbnailPaths(const QString &baseName,
                                                     const QStringList &writtenPaths) const {
  // Probe for an existing primary-cover file on disk and append it to the
  // thumbnail-strip paths when no fresh write happened — FillMissing /
  // UpdateChanged commonly skip writing if the file already exists, but the
  // user still wants a visual ping for each item. Probes the canonical subdirs
  // the persistence layer can use (top-level mirror, /front/, /covers/,
  // /box-2D/, /screenshot/).
  QStringList thumbPaths = writtenPaths;
  if (thumbPaths.isEmpty() && !m_artworkDir.isEmpty()) {
    static const QStringList kProbeDirs = {QString(), QStringLiteral("front"),
                                           QStringLiteral("covers"), QStringLiteral("box-2D"),
                                           QStringLiteral("screenshot")};
    static const QStringList kProbeExts = {QStringLiteral("png"), QStringLiteral("jpg"),
                                           QStringLiteral("jpeg"), QStringLiteral("webp")};
    for (const QString &dir : kProbeDirs) {
      QString prefix = m_artworkDir;
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
  return thumbPaths;
}

void BatchScrapeRunner::onWriteCompleted(quint64 requestId, bool ok) {
  // Look up the pending row before doing anything else — a stray
  // duplicate emit (shouldn't happen, but defensive) would otherwise
  // double-tick counters.
  auto it = m_pendingWrites.find(requestId);
  if (it == m_pendingWrites.end()) return;
  PendingWrite pending = std::move(it.value());
  m_pendingWrites.erase(it);
  // Normal reply beat the save watchdog — retire its timer. (A reply that
  // LOST the race never gets here: the watchdog's onTimeout removed the row,
  // so the lookup above already returned.)
  pending.saveWatchdog.finish();

  if (m_cancelled) {
    // Cancellation may have raced ahead of the worker reply. Don't
    // emit per-item completion; just drain the in-flight slot so the
    // queue can wind down.
    itemFinished();
    return;
  }

  if (!ok) {
    recordError(QStringLiteral("%1: metadata save failed").arg(pending.baseName),
                pending.state ? pending.state->path : QString());
    return;
  }

  // Worker bypassed DatabaseManager's main-thread cache. Invalidate
  // the per-item slot here so the next sidebar refresh sees the
  // freshly-saved metadata instead of the pre-scrape cached row.
  if (auto *db = dbMgr()) {
    db->invalidateMetadataCacheItem(m_collectionUuid, pending.state->path);
  }

  ++m_summary.scraped;
  resetFatalStreak();
  m_summary.mediaWritten += pending.mediaWritten;
  if (pending.sidecarFailed) ++m_summary.sidecarFailures;
  if (pending.state) {
    m_remainingPaths.removeOne(pending.state->path);
  }

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
  emit itemCompleted(m_summary.processedItems(), totalItemCount(), pending.scraped,
                     pending.writtenPaths);
  itemFinished();
}

} // namespace Scraper
