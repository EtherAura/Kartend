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
#include <QPair>
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
#include "dbtxn.h"
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

  // inFlight and handoffSeq are std::atomic only to hand ThreadSanitizer the
  // happens-before edges it cannot derive from the mutex here. Every access to
  // both — and to `ready` — is already under `mutex`, so the locking alone is
  // correct; but TSan loses the QMutex lock epoch across
  // QWaitCondition::wait(&mutex, msecs)'s annotated release/reacquire and then
  // reports false data races whose two stacks are BOTH flagged holding this
  // mutex (TSan's own record serializes them, so they cannot race). Same
  // QWaitCondition epoch-loss class as the `run*lambda*` entry in
  // tests/suppressions/tsan.txt; resolving it in code keeps genuine scan races
  // visible instead of widening a suppression (Kartend-bl8w0). Both atomics
  // only ADD synchronization over the existing mutex, so neither can mask a
  // real race.
  //
  // inFlight: outstanding DirectoryScanTask count; the driver wait-loops use it
  //   as their QWaitCondition predicate. (FP: worker `--inFlight` vs driver
  //   `++inFlight`.)
  std::atomic<int> inFlight = 0;
  // handoffSeq: producer->consumer payload handshake. A worker release-stores
  //   (fetch_add) after appending a chunk to `ready`; the driver acquire-loads
  //   after popping one, so the chunk's QStringList/QHash read as synchronized.
  //   (FP: worker chunk-build vs driver chunk-read.)
  std::atomic<quint64> handoffSeq = 0;
};

class DirectoryScanTask final : public QRunnable {
public:
  DirectoryScanTask(QString dirPath, QString rootPath, QStringList nameFilters,
                    std::shared_ptr<std::atomic_bool> cancelToken,
                    std::shared_ptr<ScanCompletionQueue> queue)
      : m_dirPath(std::move(dirPath)), m_rootPath(std::move(rootPath)),
        m_nameFilters(std::move(nameFilters)), m_cancelToken(std::move(cancelToken)),
        m_queue(std::move(queue)) {
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
      // Release-store pairs with the scan driver's acquire-load when it pops
      // this chunk — gives TSan a happens-before edge for the chunk payload
      // across the lossy QWaitCondition epoch (ScanCompletionQueue::handoffSeq).
      m_queue->handoffSeq.fetch_add(1, std::memory_order_release);
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
  // shared_ptr, not a raw pointer: a task can still be inside its final
  // QMutexLocker teardown (unlocking m_queue->mutex) after the scan driver's
  // drain loop observed inFlight==0 and returned, which would otherwise touch a
  // destroyed mutex on the driver's reclaimed stack (Kartend-bl8w0). Co-owning
  // the queue — exactly like the cancellation token above — lets the last
  // finisher (driver or a slow worker) free it safely.
  std::shared_ptr<ScanCompletionQueue> m_queue;
};

// SynchronousPragmaGuard was deleted in Kartend-fux2w: after Kartend-y9if
// changed the scan paths from synchronous=OFF to NORMAL, the guard "restored"
// NORMAL to NORMAL — a pure no-op that implied a protection which no longer
// existed (every connection already opens at NORMAL via ConnectionPragmas).

// Depth-tracking transaction guard — promoted to KartendDb::DbTransaction
// (utils/db/dbtxn.h, Kartend-3ibqt) so every DB-writing file shares it; the
// alias below keeps this namespace's existing use sites working unchanged.
using KartendDb::DbTransaction;

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

/// Folds one collection's relative-keyed scan/DB metadata (mtimes, sizes)
/// into maps keyed by the same path form sortFiles receives: the absolute
/// path under @p mediaDir, run through @p canonicalPathCache when the load
/// deduped via canonical paths. First insertion wins, matching the
/// first-occurrence-wins dedup in appendFileMapsAndListCanonical.
/// (Kartend-m9r1s)
void buildSortMetadata(const QString &mediaDir, const QHash<QString, QDateTime> &relTimestamps,
                       const QHash<QString, qint64> &relSizes,
                       const QHash<QString, QString> *canonicalPathCache,
                       QHash<QString, qint64> &mtimeMsByPath, QHash<QString, qint64> &sizeByPath);

/// Sorts file paths in place according to the given SortMode.
///
/// For the Date/Size modes the sort key for each path is looked up in
/// @p mtimeMsByPath / @p sizeByPath first (values the caller already has from
/// the scan or the items table — see buildSortMetadata); only paths missing
/// from the map are statted. Passing nullptr preserves the historical
/// stat-everything behavior (Kartend-m9r1s).
void sortFiles(QStringList &allFilePaths, SortMode mode = SortMode::NameAscending,
               const QHash<QString, qint64> *mtimeMsByPath = nullptr,
               const QHash<QString, qint64> *sizeByPath = nullptr);

// ───────────────────────────────────────────────────────────────────────────
// Cross-load canonical-path cache plumbing (Kartend-4fmu1). The persistent
// map lives on QueryManager (m_persistentCanonicalPaths: absolute path ->
// (mtime ms, canonical path)); these helpers move entries between it and the
// per-load cache canonicalKeyPath consults, with the mtime the load already
// has in hand (scan- or items-table-sourced) as the validity stamp. Paths
// without a known mtime simply never persist — they realpath each load,
// exactly as before.
// ───────────────────────────────────────────────────────────────────────────

/// Pre-seeds @p perLoadCache with still-valid canonical resolutions from
/// @p persistentCache, so canonicalKeyPath skips the realpath walk for them.
/// An entry is valid when the persisted mtime equals the relative-keyed mtime
/// in @p relTimestamps (resolved against @p mediaDir like the load itself).
void seedCanonicalPathCache(const QString &mediaDir, const QHash<QString, QDateTime> &relTimestamps,
                            const QHash<QString, QPair<qint64, QString>> &persistentCache,
                            QHash<QString, QString> &perLoadCache);

/// Stores this load's resolved canonical paths (fresh AND re-validated seeds)
/// back into @p persistentCache, stamped with the same mtimes used by
/// seedCanonicalPathCache.
void harvestCanonicalPathCache(const QString &mediaDir,
                               const QHash<QString, QDateTime> &relTimestamps,
                               const QHash<QString, QString> &perLoadCache,
                               QHash<QString, QPair<qint64, QString>> &persistentCache);

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
