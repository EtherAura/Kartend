#ifndef SCRAPEWRITEWORKER_H
#define SCRAPEWRITEWORKER_H

#include <QList>
#include <QObject>
#include <QPair>
#include <QSqlDatabase>
#include <QString>

#include "scrapepersistence.h"
#include "scrapertypes.h"

namespace Scraper {

/// Worker-thread DB writer for BatchScrapeRunner.
///
/// Owns its own QSqlDatabase connection (separate from the main-thread
/// DatabaseManager connection) so that per-item metadata writes during a
/// batch scrape do not run on the main thread. SQLite WAL mode lets one
/// writer + UI-thread readers coexist without blocking, so the user can
/// keep navigating the library while a 50K-item batch grinds through.
///
/// Threading contract:
///   - Constructed on the main thread.
///   - moveToThread(workerThread) BEFORE workerThread->start().
///   - First main-thread caller queues `openConnection` (queued
///     connection) so the QSqlDatabase is created on the worker thread —
///     Qt's SQL module pins a connection to the thread that opened it.
///   - `performWrite` runs on the worker thread; the runner posts to it
///     via QMetaObject::invokeMethod with Qt::QueuedConnection.
///   - `writeCompleted` is emitted on the worker thread; the runner
///     receives it on the main thread via Qt::AutoConnection (queued
///     across thread boundaries).
///   - On shutdown the runner queues `closeConnection`, then
///     workerThread->quit() + wait().
///
/// The connection name is unique per worker instance — a process can host
/// many concurrent runners (e.g. when a previous Scraper dialog was left
/// open and the user starts another) and Qt requires distinct connection
/// names per QSqlDatabase.
class ScrapeWriteWorker : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScrapeWriteWorker)
public:
  explicit ScrapeWriteWorker(QString connectionName, QObject *parent = nullptr);
  ~ScrapeWriteWorker() override;

public slots:
  /// Open the SQLite connection on the current (worker) thread. Idempotent —
  /// repeated calls are no-ops while the connection stays valid. Failure to
  /// open is logged and leaves `m_db` invalid; subsequent `performWrite`
  /// calls will then fail-fast and emit `writeCompleted(..., false)` so
  /// the runner can record the per-item error like any other save failure.
  void openConnection();

  /// Close + remove the worker connection. Call before quitting the
  /// worker thread so the connection isn't leaked into Qt's global
  /// driver registry (QSqlDatabase::removeDatabase warns when called
  /// from a thread that doesn't own the connection).
  void closeConnection();

  /// Perform a per-item merged save on the worker thread. Emits
  /// `writeCompleted(requestId, ok)` on completion. `requestId` is an
  /// opaque token chosen by the runner so it can correlate the reply
  /// back to the right pending item (with itemConcurrency > 1 several
  /// requests are in flight).
  void performWrite(quint64 requestId, QString collectionUuid, QString sourcePath,
                    Scraper::ScrapedItem scraped,
                    Scraper::NonStandardArtworkList nonStandardArtwork);

signals:
  /// Emitted on the worker thread after `performWrite` finishes. Crosses
  /// the thread boundary as a queued signal so the runner observes it on
  /// its own (main) thread.
  void writeCompleted(quint64 requestId, bool ok);

private:
  QString m_connectionName;
  QSqlDatabase m_db;
};

} // namespace Scraper

#endif // SCRAPEWRITEWORKER_H
