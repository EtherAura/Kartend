#ifndef SCANSERVICE_INTERNAL_H
#define SCANSERVICE_INTERNAL_H

#include <functional>
#include <optional>
#include <utility>

#include <QElapsedTimer>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>

#include "batchsizes.h"
#include "errorutils.h"

// Shared scan-pipeline constants + helpers. These were previously copy-pasted
// into the anonymous namespaces of scanservice.cpp and scanservice_persist.cpp
// with a "keep in sync" comment; centralised here so both TUs share one
// definition (Kartend-to3ie).
namespace ScanServiceInternal {

inline constexpr int BATCH_SIZE = KartendDb::BatchSizes::FilesystemScanBatch;
inline constexpr int COMMIT_INTERVAL_BATCHES = KartendDb::BatchSizes::ScanCommitInterval;
inline constexpr int PROGRESS_REPORT_INTERVAL = 50000;

// Kartend-a911.2: executes a prepared query and logs a DatabaseQueryFailed
// warning on failure. Returns nullopt on success, the constructed ErrorContext
// on failure (already logged) so callers that also need to emit errorOccurred
// or unwind control flow can do so without rebuilding the error. Centralises
// bind-count diagnostics + lastError forwarding across the scan pipeline.
[[nodiscard]] inline std::optional<ErrorUtils::ErrorContext>
execAndLog(QSqlQuery &query, const QString &failureMessage, const QString &callerLocation) {
  if (query.exec()) {
    return std::nullopt;
  }
  auto err = ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                               failureMessage, callerLocation)
                 .withDetails(query.lastError().text());
  ErrorUtils::logError(err);
  return err;
}

// Kartend-7axx: the four scan/save pipelines all open-coded the same throttle
// pattern (lambda over a QElapsedTimer + a lastEmitMs cursor). Factor it once
// so adding a new pipeline doesn't grow a fifth copy.
class ScanProgressThrottle {
public:
  using Emitter = std::function<void(int processed, int total)>;

  ScanProgressThrottle(int minIntervalMs, Emitter emitter)
      : m_minIntervalMs(minIntervalMs), m_emitter(std::move(emitter)) {
    m_timer.start();
    // Initialise so the first non-force report always fires (legacy behaviour
    // from each pipeline's open-coded copy).
    m_lastEmitMs = -minIntervalMs;
  }

  /// Named `report`, not `emit`, because Qt's emit-keyword macro would expand
  /// to nothing and produce a syntax error at the call site.
  void report(int processed, int total, bool force = false) {
    if (force) {
      m_emitter(processed, total);
      m_lastEmitMs = m_timer.elapsed();
      return;
    }
    const qint64 nowMs = m_timer.elapsed();
    if (nowMs - m_lastEmitMs < m_minIntervalMs) return;
    m_emitter(processed, total);
    m_lastEmitMs = nowMs;
  }

private:
  QElapsedTimer m_timer;
  qint64 m_lastEmitMs = 0;
  int m_minIntervalMs;
  Emitter m_emitter;
};

} // namespace ScanServiceInternal

#endif // SCANSERVICE_INTERNAL_H
