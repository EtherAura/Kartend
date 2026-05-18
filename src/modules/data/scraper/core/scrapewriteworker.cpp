// Worker-thread DB writer for BatchScrapeRunner. See header for the
// threading contract.
#include "scrapewriteworker.h"

#include <utility>

#include <QLoggingCategory>
#include <QMetaType>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include "databaseschema.h"
#include "scrapepersistence.h"

Q_LOGGING_CATEGORY(lcScrapeWriteWorker, "kartend.scrape.writeworker", QtWarningMsg)

namespace Scraper {

ScrapeWriteWorker::ScrapeWriteWorker(QString connectionName, QObject *parent)
    : QObject(parent), m_connectionName(std::move(connectionName)) {
  // Queued connections cross thread boundaries by copying arguments via
  // Qt's metatype system. ScrapedItem isn't a built-in QMetaType, so it
  // needs registration before the first cross-thread invocation. Idempotent.
  qRegisterMetaType<Scraper::ScrapedItem>("Scraper::ScrapedItem");
  qRegisterMetaType<Scraper::NonStandardArtworkList>("Scraper::NonStandardArtworkList");
}

ScrapeWriteWorker::~ScrapeWriteWorker() {
  // Best-effort close in case the owner skipped the queued closeConnection
  // (e.g. shutdown sequence raced with destruction). Safe to call from
  // any thread for an already-closed connection — m_db.isValid() is false.
  if (m_db.isValid()) {
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

void ScrapeWriteWorker::openConnection() {
  if (m_db.isValid() && m_db.isOpen()) return;

  m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!DatabaseSchema::openConnection(m_db, dbPath)) {
    qCWarning(lcScrapeWriteWorker)
        << "openConnection failed for connectionName=" << m_connectionName;
    // Leave m_db invalid; performWrite will fail-fast and the runner
    // surfaces per-item save errors via its existing error path.
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
    return;
  }
  // Same pragmas as the main connection (WAL, synchronous=NORMAL, etc.) —
  // critical for the multi-writer story to actually work and for the
  // worker not to fight the main connection's locking model.
  DatabaseSchema::applyConnectionPragmas(m_db);

  // Schema-readiness sanity check (Kartend-76vv). The worker doesn't run
  // createTables / applySchemaMigrations on its own — it relies on
  // DatabaseManager having done that already against the main-thread
  // connection (always true in production because DatabaseManager
  // constructs first). If a future refactor reorders construction, or
  // someone wires a runner without a DatabaseManager, every performWrite
  // would silently fail with 'metadata save failed' for every item.
  // Better to fail loudly here once than flood the error log with
  // per-item failures.
  QSqlQuery probe(m_db);
  if (!probe.exec(QStringLiteral(
          "SELECT name FROM sqlite_master WHERE type='table' AND name='item_metadata'")) ||
      !probe.next()) {
    qCWarning(lcScrapeWriteWorker)
        << "openConnection: schema not ready (item_metadata table missing) "
           "for connectionName="
        << m_connectionName
        << "— DatabaseManager must run schema migrations before the "
           "worker connects. Closing connection; subsequent performWrite "
           "calls will fail-fast and the runner records per-item save "
           "failures.";
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

void ScrapeWriteWorker::closeConnection() {
  if (!m_db.isValid()) return;
  m_db.close();
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_connectionName);
}

void ScrapeWriteWorker::performWrite(quint64 requestId, QString collectionUuid, QString sourcePath,
                                     Scraper::ScrapedItem scraped,
                                     Scraper::NonStandardArtworkList nonStandardArtwork) {
  // Lazy open guard — covers the case where the runner posts the first
  // write before the queued openConnection slot has run (the Qt event
  // loop on the worker thread should always run openConnection first
  // since it was queued first, but defensive).
  if (!m_db.isValid() || !m_db.isOpen()) {
    openConnection();
  }
  const bool ok = Scraper::saveScrapedMetadataDirect(m_db, collectionUuid, sourcePath, scraped,
                                                     nonStandardArtwork);
  emit writeCompleted(requestId, ok);
}

} // namespace Scraper
