#include "connectionpragmas.h"

#include "errorutils.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

using namespace ErrorUtils;

namespace MediaDbConnectionInit {

void applyPragmas(QSqlDatabase &db, const PragmaConfig &cfg, const QString &loggingContext) {
  QSqlQuery query(db);

  if (cfg.enableForeignKeys) {
    if (!query.exec("PRAGMA foreign_keys = ON")) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to enable foreign keys", loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
    }
  }

  if (cfg.enableWalWithFallback) {
    if (!query.exec("PRAGMA journal_mode = WAL")) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to enable WAL mode, falling back to DELETE mode",
                                       loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
      if (!query.exec("PRAGMA journal_mode = DELETE")) {
        auto fallbackErr =
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                  "Failed to set DELETE journal mode", loggingContext)
                .withDetails(query.lastError().text());
        ErrorUtils::logError(fallbackErr);
      }
    }
  }

  if (cfg.busyTimeoutMs > 0) {
    const QString busyTimeoutPragma =
        QStringLiteral("PRAGMA busy_timeout = %1").arg(cfg.busyTimeoutMs);
    if (!query.exec(busyTimeoutPragma)) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to set busy timeout",
                                       loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
    }
  }

  if (cfg.setSynchronousNormal) {
    if (!query.exec("PRAGMA synchronous = NORMAL")) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to set synchronous mode", loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
    }
  }
}

} // namespace MediaDbConnectionInit
