// Internal helpers shared between querymanager.cpp and
// querymanagerstatichelpers.cpp. Not part of the public API; do not include
// outside src/modules/query/.
#ifndef QUERYMANAGERHELPERS_H
#define QUERYMANAGERHELPERS_H

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
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWaitCondition>
#include <atomic>
#include <memory>

#include "queryhelpers.h"
#include "uiconstants.h"

namespace QueryManagerInternal {

inline auto buildFtsPrefixQuery(const QString &raw) -> QString {
  return QueryHelpers::buildFtsPrefixQuery(raw);
}

inline auto canonicalKeyPath(const QString &absPath, bool dedup,
                             QHash<QString, QString> *canonicalPathCache)
    -> QString {
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
// Filesystem-scan helpers (deduplicated from querymanager.cpp,
// querymanagerscan.cpp, querymanagerscanandsave.cpp, querymanagerpersist.cpp)
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
                                  const DirSignatureSample &candidate,
                                  int maxSamples) -> void {
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
                                  const QVector<DirSignatureSample> &samples)
    -> QString {
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

inline auto parseDirSignatureJson(const QString &json,
                                  bool &includeSubfoldersOut,
                                  QVector<DirSignatureSample> &samplesOut)
    -> bool {
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
    s.mtimeSec =
        static_cast<qint64>(o.value(QStringLiteral("t")).toDouble(0.0));
    samplesOut.append(std::move(s));
  }
  return !samplesOut.isEmpty();
}

inline auto seedDirSignatureFromFilesystem(const QString &rootPath,
                                           bool includeSubfolders) -> QString {
  QVector<DirSignatureSample> samples;
  samples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  QFileInfo rootInfo(rootPath);
  if (!rootInfo.exists()) {
    return QString();
  }
  addDirSignatureSample(
      samples,
      DirSignatureSample{QString(), rootInfo.lastModified().toSecsSinceEpoch()},
      UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  if (includeSubfolders) {
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    int inspected = 0;
    while (it.hasNext() &&
           inspected < UIConstants::Database::DIR_SIGNATURE_SEED_MAX_DIRS) {
      it.next();
      const QString absPath = it.filePath();
      const QString relPath = QDir(rootPath).relativeFilePath(absPath);
      const qint64 mtimeSec =
          QFileInfo(absPath).lastModified().toSecsSinceEpoch();
      addDirSignatureSample(samples, DirSignatureSample{relPath, mtimeSec},
                            UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
      ++inspected;
    }
  }

  return buildDirSignatureJson(includeSubfolders, samples);
}

inline auto dirSignatureStillValid(const QString &rootPath,
                                   bool includeSubfolders,
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
    const QString absPath =
        s.relPath.isEmpty() ? rootPath : root.absoluteFilePath(s.relPath);
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
                    std::shared_ptr<std::atomic_bool> cancelToken,
                    ScanCompletionQueue *queue)
      : m_dirPath(std::move(dirPath)), m_rootPath(std::move(rootPath)),
        m_nameFilters(std::move(nameFilters)),
        m_cancelToken(std::move(cancelToken)), m_queue(queue) {
    setAutoDelete(true);
  }

  void run() override {
    if (!m_queue || !m_cancelToken) {
      return;
    }

    // Scan this directory (non-recursively) but emit bounded chunks so a single
    // huge folder cannot allocate an unbounded QStringList/QHash in memory.
    QDir rootDir(m_rootPath);
    QDirIterator iterator(m_dirPath, m_nameFilters, QDir::Files,
                          QDirIterator::NoIteratorFlags);

    auto pushChunk = [&](DirectoryScanResult &&chunk) {
      if (!m_queue) {
        return;
      }

      // Backpressure: block (with timeout) if the queue is full, so memory
      // stays bounded even when directory scans outpace the consumer.
      QMutexLocker locker(&m_queue->mutex);
      while (m_queue->ready.size() >=
                 UIConstants::Database::SCAN_READY_MAX_RESULTS &&
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
    chunk.relativePaths.reserve(
        UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
    chunk.timestamps.reserve(UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE *
                             2);

    while (iterator.hasNext()) {
      if (m_cancelToken->load(std::memory_order_acquire)) {
        break;
      }

      iterator.next();
      const QString filePath = iterator.filePath();
      const QString relativePath = rootDir.relativeFilePath(filePath);
      const QFileInfo info = iterator.fileInfo();

      chunk.relativePaths.append(relativePath);
      chunk.timestamps.insert(relativePath, info.lastModified());

      if (chunk.relativePaths.size() >=
          UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE) {
        pushChunk(std::move(chunk));
        chunk = DirectoryScanResult{};
        chunk.relativePaths.reserve(
            UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
        chunk.timestamps.reserve(
            UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE * 2);
      }
    }

    if (!m_cancelToken->load(std::memory_order_acquire) &&
        !chunk.relativePaths.isEmpty()) {
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
  auto operator=(const SynchronousPragmaGuard &)
      -> SynchronousPragmaGuard & = delete;

  SynchronousPragmaGuard(SynchronousPragmaGuard &&) = delete;
  auto operator=(SynchronousPragmaGuard &&)
      -> SynchronousPragmaGuard & = delete;

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

} // namespace QueryManagerInternal

#endif // QUERYMANAGERHELPERS_H
