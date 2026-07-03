// Off-thread prep behind LibraryToolsController::artworkWizardInteractive. See
// the header for the contract; the body is what the button handler used to run
// inline on the GUI thread (item-path query + filter + directory listing),
// moved here so it runs on the thread pool against its own SQLite connection.
#include "artworkwizardservice.h"

#include <atomic>

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "errorutils.h"

namespace ArtworkWizardService {

namespace {
// Progress-callback throttle: report every this-many examined rows so a
// 10k-item collection posts ~150 queued GUI hops instead of one per row.
constexpr int PROGRESS_STRIDE = 64;
} // namespace

Prepared prepare(const QString &dbPath, const QString &collectionUuid, const QString &artworkDir,
                 const ProgressFn &progress) {
  Prepared out;
  if (dbPath.isEmpty()) {
    return out;
  }

  // Unique connection name per call so concurrent / sequential runs never
  // collide in QSqlDatabase's global registry (same shape as BulkEditService).
  static std::atomic<quint64> connSeq{0};
  const QString conn = QStringLiteral("kartend_artworkwizard_%1")
                           .arg(connSeq.fetch_add(1, std::memory_order_relaxed));
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::critical(ErrorUtils::ErrorCode::DatabaseConnectionFailed,
                                             "Failed to open artwork-wizard connection",
                                             "ArtworkWizardService::prepare")
              .withDetails(db.lastError().text()));
    } else {
      // Wait out transient writer contention (e.g. a scan commit on the
      // manager's worker connection) instead of failing the whole prep.
      QSqlQuery(db).exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

      QSqlQuery q(db);
      // Same enumeration the GUI-thread path used (DatabaseManager::
      // loadAllItemPathsForCollection) so the wizard sees the rows the rest of
      // the app sees, in the same name order.
      if (!q.prepare(
              QStringLiteral("SELECT path, artwork_path FROM items "
                             "WHERE collection_uuid = ? ORDER BY name COLLATE NOCASE ASC"))) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                            "Failed to prepare item-path enumeration",
                                            "ArtworkWizardService::prepare")
                .withDetails(q.lastError().text()));
      } else {
        q.addBindValue(collectionUuid);
        if (!q.exec()) {
          ErrorUtils::logError(
              ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                              "Failed to enumerate item paths",
                                              "ArtworkWizardService::prepare")
                  .withDetails(q.lastError().text()));
        } else {
          out.dbOk = true;
          // Collect first so the progress callback has a real total; the row
          // set is the same one the filter loop below consumes.
          struct Row {
            QString path;
            QString artworkPath;
          };
          QList<Row> rows;
          while (q.next()) {
            rows.append({q.value(0).toString(), q.value(1).toString()});
          }
          const int total = static_cast<int>(rows.size());
          if (progress) {
            progress(0, total);
          }
          int done = 0;
          for (const Row &row : rows) {
            if (row.artworkPath.trimmed().isEmpty()) {
              Item item;
              item.filePath = row.path;
              item.itemName = QFileInfo(row.path).completeBaseName();
              out.queue.append(item);
            }
            ++done;
            if (progress && (done % PROGRESS_STRIDE == 0 || done == total)) {
              progress(done, total);
            }
          }
        }
      }
    }
  }
  // db (and its queries) are out of scope before removeDatabase, so Qt doesn't
  // warn about a connection still in use.
  QSqlDatabase::removeDatabase(conn);

  // Snapshot the artwork directory listing once — only needed when there is a
  // queue to rank. The wizard re-uses this stable listing for every entry
  // (these directories typically hold hundreds of files).
  if (!out.queue.isEmpty() && !artworkDir.trimmed().isEmpty()) {
    QDir dir(artworkDir);
    const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    out.directoryFiles.reserve(entries.size());
    for (const QString &name : entries) {
      out.directoryFiles.append(dir.absoluteFilePath(name));
    }
  }
  return out;
}

} // namespace ArtworkWizardService
