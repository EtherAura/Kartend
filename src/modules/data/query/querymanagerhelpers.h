// Internal helpers shared between querymanager.cpp and
// querymanagerstatichelpers.cpp. Not part of the public API; do not include
// outside src/modules/query/.
#ifndef QUERYMANAGERHELPERS_H
#define QUERYMANAGERHELPERS_H

#include <atomic>
#include <memory>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QRunnable>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWaitCondition>

#include "collection/collectionconfig.h"
#include "queryhelpers.h"
#include "uiconstants/database.h"

class PreparedStatementCache;

namespace QueryManagerInternal {

inline auto buildFtsPrefixQuery(const QString &raw) -> QString {
  return QueryHelpers::buildFtsPrefixQuery(raw);
}

inline auto canonicalKeyPath(const QString &absPath, bool dedup,
                             QHash<QString, QString> *canonicalPathCache) -> QString {
  if (!dedup) {
    return absPath;
  }

  if (canonicalPathCache) {
    auto it = canonicalPathCache->constFind(absPath);
    if (it != canonicalPathCache->constEnd()) {
      return it.value();
    }
  }

  QString canon = QFileInfo(absPath).canonicalFilePath();
  if (canon.isEmpty()) {
    canon = QDir::cleanPath(absPath);
  }

  if (canonicalPathCache) {
    canonicalPathCache->insert(absPath, canon);
  }
  return canon;
}

inline auto displayNameForBase(const QString &baseName) -> QString {
  return QueryHelpers::displayNameForBase(baseName);
}

template <typename Map, typename Key, typename Value>
inline void insertIfAbsent(Map &map, const Key &key, const Value &value) {
  if (map.find(key) == map.end()) {
    map.insert(key, value);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Filesystem-scan helpers — used by the scan subsystem (scanservice.cpp).
// ─────────────────────────────────────────────────────────────────────────────

/// Result struct for parallel directory scanning.
/// Holds files found in a single directory plus their timestamps.
struct DirectoryScanResult {
  QStringList relativePaths;
  QHash<QString, QDateTime> timestamps;
};

struct DirSignatureSample {
  QString relPath; // Relative to collection root. Empty means root.
  qint64 mtimeSec = 0;
};

inline auto addDirSignatureSample(QVector<DirSignatureSample> &samples,
                                  const DirSignatureSample &candidate, int maxSamples) -> void {
  if (maxSamples <= 0) {
    return;
  }
  for (const auto &s : samples) {
    if (s.relPath == candidate.relPath) {
      return;
    }
  }

  if (samples.size() < maxSamples) {
    samples.append(candidate);
  } else {
    int minIndex = 0;
    for (int i = 1; i < samples.size(); ++i) {
      if (samples[i].mtimeSec < samples[minIndex].mtimeSec) {
        minIndex = i;
      }
    }
    if (candidate.mtimeSec <= samples[minIndex].mtimeSec) {
      return;
    }
    samples[minIndex] = candidate;
  }
}

inline auto buildDirSignatureJson(bool includeSubfolders,
                                  const QVector<DirSignatureSample> &samples) -> QString {
  QJsonObject root;
  root.insert(QStringLiteral("v"), 1);
  root.insert(QStringLiteral("sub"), includeSubfolders);

  QJsonArray arr;
  for (const auto &s : samples) {
    QJsonObject o;
    o.insert(QStringLiteral("p"), s.relPath);
    o.insert(QStringLiteral("t"), static_cast<double>(s.mtimeSec));
    arr.append(o);
  }
  root.insert(QStringLiteral("s"), arr);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

inline auto parseDirSignatureJson(const QString &json, bool &includeSubfoldersOut,
                                  QVector<DirSignatureSample> &samplesOut) -> bool {
  includeSubfoldersOut = false;
  samplesOut.clear();

  if (json.trimmed().isEmpty()) {
    return false;
  }

  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return false;
  }

  const QJsonObject obj = doc.object();
  if (obj.value(QStringLiteral("v")).toInt() != 1) {
    return false;
  }

  includeSubfoldersOut = obj.value(QStringLiteral("sub")).toBool(false);

  const QJsonValue sVal = obj.value(QStringLiteral("s"));
  if (!sVal.isArray()) {
    return false;
  }
  const QJsonArray arr = sVal.toArray();
  samplesOut.reserve(arr.size());
  for (const auto &v : arr) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject o = v.toObject();
    DirSignatureSample s;
    s.relPath = o.value(QStringLiteral("p")).toString();
    s.mtimeSec = static_cast<qint64>(o.value(QStringLiteral("t")).toDouble(0.0));
    samplesOut.append(std::move(s));
  }
  return !samplesOut.isEmpty();
}

inline auto seedDirSignatureFromFilesystem(const QString &rootPath, bool includeSubfolders)
    -> QString {
  QVector<DirSignatureSample> samples;
  samples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  QFileInfo rootInfo(rootPath);
  if (!rootInfo.exists()) {
    return QString();
  }
  addDirSignatureSample(samples,
                        DirSignatureSample{QString(), rootInfo.lastModified().toSecsSinceEpoch()},
                        UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  if (includeSubfolders) {
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    int inspected = 0;
    while (it.hasNext() && inspected < UIConstants::Database::DIR_SIGNATURE_SEED_MAX_DIRS) {
      it.next();
      const QString absPath = it.filePath();
      const QString relPath = QDir(rootPath).relativeFilePath(absPath);
      const qint64 mtimeSec = QFileInfo(absPath).lastModified().toSecsSinceEpoch();
      addDirSignatureSample(samples, DirSignatureSample{relPath, mtimeSec},
                            UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
      ++inspected;
    }
  }

  return buildDirSignatureJson(includeSubfolders, samples);
}

inline auto dirSignatureStillValid(const QString &rootPath, bool includeSubfolders,
                                   const QString &storedSignature) -> bool {
  bool storedSub = false;
  QVector<DirSignatureSample> samples;
  if (!parseDirSignatureJson(storedSignature, storedSub, samples)) {
    return false;
  }
  if (storedSub != includeSubfolders) {
    return false;
  }
  if (samples.isEmpty()) {
    return false;
  }

  QDir root(rootPath);
  for (const auto &s : samples) {
    const QString absPath = s.relPath.isEmpty() ? rootPath : root.absoluteFilePath(s.relPath);
    QFileInfo info(absPath);
    if (!info.exists()) {
      return false;
    }
    const qint64 currentSec = info.lastModified().toSecsSinceEpoch();
    if (currentSec != s.mtimeSec) {
      return false;
    }
  }

  return true;
}

struct ScanCompletionQueue {
  QMutex mutex;
  QWaitCondition hasResult;
  QWaitCondition hasSpace;
  QVector<DirectoryScanResult> ready;
  int inFlight = 0;
};

class DirectoryScanTask final : public QRunnable {
public:
  DirectoryScanTask(QString dirPath, QString rootPath, QStringList nameFilters,
                    std::shared_ptr<std::atomic_bool> cancelToken, ScanCompletionQueue *queue)
      : m_dirPath(std::move(dirPath)), m_rootPath(std::move(rootPath)),
        m_nameFilters(std::move(nameFilters)), m_cancelToken(std::move(cancelToken)),
        m_queue(queue) {
    setAutoDelete(true);
  }

  void run() override {
    if (!m_queue || !m_cancelToken) {
      return;
    }

    // Scan this directory (non-recursively) but emit bounded chunks so a single
    // huge folder cannot allocate an unbounded QStringList/QHash in memory.
    QDir rootDir(m_rootPath);
    // QDir::System keeps symlinks visible when their targets are temporarily
    // unreachable; see querymanagerscan.cpp scanMediaDirectory for context.
    QDirIterator iterator(m_dirPath, m_nameFilters, QDir::Files | QDir::System,
                          QDirIterator::NoIteratorFlags);

    auto pushChunk = [&](DirectoryScanResult &&chunk) {
      if (!m_queue) {
        return;
      }

      // Backpressure: block (with timeout) if the queue is full, so memory
      // stays bounded even when directory scans outpace the consumer.
      QMutexLocker locker(&m_queue->mutex);
      while (m_queue->ready.size() >= UIConstants::Database::SCAN_READY_MAX_RESULTS &&
             !m_cancelToken->load(std::memory_order_acquire)) {
        m_queue->hasSpace.wait(&m_queue->mutex, 50);
      }

      if (m_cancelToken->load(std::memory_order_acquire)) {
        return;
      }

      m_queue->ready.append(std::move(chunk));
      m_queue->hasResult.wakeOne();
    };

    DirectoryScanResult chunk;
    chunk.relativePaths.reserve(UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
    chunk.timestamps.reserve(UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE * 2);

    while (iterator.hasNext()) {
      if (m_cancelToken->load(std::memory_order_acquire)) {
        break;
      }

      iterator.next();
      const QString filePath = iterator.filePath();
      const QString relativePath = rootDir.relativeFilePath(filePath);
      const QFileInfo info = iterator.fileInfo();

      // See querymanagerscan.cpp scanMediaDirectory for why broken-symlink
      // mtimes need an epoch fallback (NOT NULL constraint).
      QDateTime mtime = info.lastModified();
      if (!mtime.isValid()) {
        mtime = QDateTime::fromSecsSinceEpoch(0);
      }
      chunk.relativePaths.append(relativePath);
      chunk.timestamps.insert(relativePath, mtime);

      if (chunk.relativePaths.size() >= UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE) {
        pushChunk(std::move(chunk));
        chunk = DirectoryScanResult{};
        chunk.relativePaths.reserve(UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
        chunk.timestamps.reserve(UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE * 2);
      }
    }

    if (!m_cancelToken->load(std::memory_order_acquire) && !chunk.relativePaths.isEmpty()) {
      pushChunk(std::move(chunk));
    }

    // Mark this directory task as complete.
    {
      QMutexLocker locker(&m_queue->mutex);
      --m_queue->inFlight;
      m_queue->hasResult.wakeOne();
    }
  }

private:
  QString m_dirPath;
  QString m_rootPath;
  QStringList m_nameFilters;
  std::shared_ptr<std::atomic_bool> m_cancelToken;
  ScanCompletionQueue *m_queue = nullptr;
};

class SynchronousPragmaGuard {
public:
  explicit SynchronousPragmaGuard(QSqlDatabase &db) : m_db(db) {}

  SynchronousPragmaGuard(const SynchronousPragmaGuard &) = delete;
  auto operator=(const SynchronousPragmaGuard &) -> SynchronousPragmaGuard & = delete;

  SynchronousPragmaGuard(SynchronousPragmaGuard &&) = delete;
  auto operator=(SynchronousPragmaGuard &&) -> SynchronousPragmaGuard & = delete;

  ~SynchronousPragmaGuard() {
    if (!m_db.isOpen()) {
      return;
    }
    QSqlQuery pragmaOn(m_db);
    pragmaOn.exec("PRAGMA synchronous = NORMAL");
  }

private:
  QSqlDatabase &m_db;
};

// Depth-tracking transaction guard for a single QSqlDatabase connection.
// SQLite has no nested BEGIN: a second transaction() silently fails, and a
// nested commit() would close the *outer* transaction early — so a cache build
// that calls helper methods which each BEGIN/COMMIT on their own was silently
// non-atomic, and a failed BEGIN/commit went unnoticed (Kartend-gv7f). Only the
// outermost guard issues transaction()/commit()/rollback(); nested guards defer
// to it. The depth counter is owned by the connection's manager and shared by
// reference so every transaction site on that connection coordinates.
//
// Usage:
//   DbTransaction txn(m_db, m_txnDepth);
//   if (!txn.active()) return false;   // outermost BEGIN failed -> bail
//   ... do work; on any error just `return false` (no manual rollback) ...
//   return txn.commit();               // real commit when outermost, else no-op
// A guard destroyed without a successful commit() rolls back (only when it owns
// the transaction), so every early-return error path is covered automatically.
class DbTransaction {
public:
  DbTransaction(QSqlDatabase &db, int &depth) : m_db(db), m_depth(depth) {
    m_outermost = (m_depth == 0);
    if (m_outermost) {
      m_began = m_db.transaction();
    }
    ++m_depth;
  }

  DbTransaction(const DbTransaction &) = delete;
  auto operator=(const DbTransaction &) -> DbTransaction & = delete;
  DbTransaction(DbTransaction &&) = delete;
  auto operator=(DbTransaction &&) -> DbTransaction & = delete;

  ~DbTransaction() {
    if (m_depth > 0) {
      --m_depth;
    }
    if (m_outermost && m_began && !m_committed) {
      m_db.rollback();
    }
  }

  // False only when this guard owns the transaction and BEGIN failed; the
  // caller should bail. Nested guards always return true (the outer guard owns
  // the live transaction).
  [[nodiscard]] bool active() const { return m_outermost ? m_began : true; }

  // Commit the transaction. Only the outermost guard commits for real; nested
  // guards defer to it and report success. Returns false if the real commit
  // failed or the outermost BEGIN never succeeded.
  bool commit() {
    if (!m_outermost) {
      return true;
    }
    if (!m_began) {
      return false;
    }
    m_committed = m_db.commit();
    return m_committed;
  }

private:
  QSqlDatabase &m_db;
  int &m_depth;
  bool m_outermost = false;
  bool m_began = false;
  bool m_committed = false;
};

// ───────────────────────────────────────────────────────────────────────────
// Pure list/map post-processing — promoted out of the QueryManager class.
// Operate only on caller-supplied containers (no QSqlDatabase, no worker
// state), so they live here as free functions instead of widening
// querymanager.h. Definitions are in querymanagerstatichelpers.cpp.
// ───────────────────────────────────────────────────────────────────────────

/// Appends one collection's resolved file list to the combined output maps,
/// applying optional canonical-path dedup across collections.
void appendFileMapsAndListCanonical(int collectionIndex, const CollectionConfig &expandedCollection,
                                    const QString &mappingArtworkDir, const QStringList &filePaths,
                                    QStringList &allFilePaths,
                                    QHash<QString, QString> &allFileNames,
                                    QHash<QString, QString> &fileToArtworkDir,
                                    QHash<QString, QString> &fileToMediaDir,
                                    QHash<QString, int> &fileToCollectionIndex, bool dedup,
                                    QSet<QString> *seenCanonicalPaths = nullptr,
                                    QHash<QString, QString> *canonicalPathCache = nullptr);

/// Sorts file paths in place according to the given SortMode.
void sortFiles(QStringList &allFilePaths, SortMode mode = SortMode::NameAscending);

// ───────────────────────────────────────────────────────────────────────────
// Connection-level item operations shared by the load and scan paths.
// They belong to neither QueryManager nor ScanService — they operate over a
// borrowed worker connection (and, for the delete, its prepared-statement
// cache) — so they live here as free functions callable from both. Both were
// QueryManager members before the ScanService extraction. Definitions are in
// querymanagerstatichelpers.cpp.
// ───────────────────────────────────────────────────────────────────────────

/// Deletes a collection's items rows and the collection row itself. Retries
/// with exponential backoff on SQLite lock contention. No-op if @p db is
/// closed.
void clearCollectionFromDatabaseByUuid(QSqlDatabase &db, PreparedStatementCache &cache,
                                       const QString &collectionUuid);

/// One-time reconcile for the v13 path-convention change: rewrites pre-v13
/// relative items.path values to ABSOLUTE form and backfills rel_path,
/// preserving every other column. Gated by the `items_paths_absolutized` meta
/// flag, so it is a cheap no-op once every non-playlist collection has been
/// processed. Must run before any scan so the absolute-vs-absolute join in
/// deleteItemsMissingFromScan stays consistent.
void maybeAbsolutizeItemPaths(QSqlDatabase &db, const QList<CollectionConfig> &allCollections);

} // namespace QueryManagerInternal

#endif // QUERYMANAGERHELPERS_H
